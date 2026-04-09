#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

DEFAULT_REQUIRED_FIELDS = [
    "cycle",
    "pc",
    "next_pc",
    "insn",
    "len",
    "lane_id",
    "mnemonic",
    "form_id",
    "opcode_class",
    "lifecycle",
    "block_kind",
    "src0_valid",
    "src0_kind",
    "src0_value",
    "src0_data",
    "src1_valid",
    "src1_kind",
    "src1_value",
    "src1_data",
    "dst0_valid",
    "dst0_kind",
    "dst0_value",
    "dst0_data",
    "mem_valid",
    "mem_is_load",
    "mem_is_store",
    "mem_addr",
    "mem_size",
    "mem_wdata",
    "mem_rdata",
    "trap_valid",
    "trap_cause",
    "traparg0",
]

VALID_LENGTHS = {16, 32, 48, 64}
VALID_BLOCK_KINDS = {"scalar", "sys", "tma", "vpar", "vseq", "cube", "tepl"}
VALID_OPCODE_CLASSES = {"invalid", "nop", "int", "fp", "branch", "load", "store", "atomic", "system"}
VALID_LIFECYCLES = {"allocated", "in_flight", "retired", "flushed", "traced"}


def _parse_version(ver: str, *, field: str) -> tuple[int, int]:
    match = re.fullmatch(r"\s*(\d+)\.(\d+)\s*", ver)
    if not match:
        raise SystemExit(f"error: invalid version format for {field}: {ver!r} (expected MAJOR.MINOR)")
    return int(match.group(1)), int(match.group(2))


def _load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for line_no, raw in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"error: {path}:{line_no}: invalid JSON: {exc}") from exc
        if not isinstance(row, dict):
            raise SystemExit(f"error: {path}:{line_no}: expected JSON object")
        rows.append(row)
    return rows


def _require_int(path: Path, row_idx: int, row: dict[str, Any], field: str) -> int:
    value = row.get(field)
    if not isinstance(value, int):
        raise SystemExit(f"error: {path}: row {row_idx} field {field!r} must be int (got {value!r})")
    return value


def _require_str(path: Path, row_idx: int, row: dict[str, Any], field: str) -> str:
    value = row.get(field)
    if not isinstance(value, str):
        raise SystemExit(f"error: {path}: row {row_idx} field {field!r} must be string (got {value!r})")
    return value


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Validate Linx Minst trace schema compatibility")
    ap.add_argument("--trace", required=True, help="Trace JSONL file")
    ap.add_argument("--expected-version", default="1.0", help="Consumer schema version (MAJOR.MINOR)")
    ap.add_argument(
        "--assume-trace-version",
        default="1.0",
        help="Used when rows do not contain schema_version",
    )
    ap.add_argument("--min-rows", type=int, default=1)
    ap.add_argument(
        "--require-block-kind-any-of",
        action="append",
        default=[],
        help="Require at least one row whose block_kind matches one of these values",
    )
    ap.add_argument(
        "--check-ordering",
        action="store_true",
        help="Fail when cycle or pc regress within one trace stream",
    )
    args = ap.parse_args(argv)

    trace_path = Path(args.trace)
    rows = _load_jsonl(trace_path)
    if len(rows) < args.min_rows:
        raise SystemExit(f"error: trace too short ({len(rows)} rows, required >= {args.min_rows}) at {trace_path}")

    expected_major, expected_minor = _parse_version(args.expected_version, field="--expected-version")
    schema_raw = rows[0].get("schema_version", args.assume_trace_version)
    actual_major, actual_minor = _parse_version(str(schema_raw), field="trace schema_version")
    if actual_major != expected_major:
        raise SystemExit(
            "error: trace schema major mismatch "
            f"(trace={actual_major}.{actual_minor}, expected={expected_major}.{expected_minor})"
        )
    if actual_minor < expected_minor:
        raise SystemExit(
            "error: trace schema minor too old "
            f"(trace={actual_major}.{actual_minor}, expected at least {expected_major}.{expected_minor})"
        )

    required_block_kinds = {value.strip().lower() for value in args.require_block_kind_any_of if value.strip()}
    observed_block_kinds: set[str] = set()
    prev_cycle: int | None = None
    prev_pc: int | None = None
    for idx, row in enumerate(rows):
        for field in DEFAULT_REQUIRED_FIELDS:
            if field not in row:
                raise SystemExit(f"error: {trace_path}: row {idx} missing required field {field!r}")

        cycle = _require_int(trace_path, idx, row, "cycle")
        pc = _require_int(trace_path, idx, row, "pc")
        _require_int(trace_path, idx, row, "next_pc")
        _require_int(trace_path, idx, row, "insn")
        length = _require_int(trace_path, idx, row, "len")
        if length not in VALID_LENGTHS:
            raise SystemExit(f"error: {trace_path}: row {idx} has invalid len {length!r}")

        block_kind = _require_str(trace_path, idx, row, "block_kind").strip().lower()
        if block_kind not in VALID_BLOCK_KINDS:
            raise SystemExit(f"error: {trace_path}: row {idx} has invalid block_kind {block_kind!r}")
        observed_block_kinds.add(block_kind)

        opcode_class = _require_str(trace_path, idx, row, "opcode_class").strip().lower()
        if opcode_class not in VALID_OPCODE_CLASSES:
            raise SystemExit(f"error: {trace_path}: row {idx} has invalid opcode_class {opcode_class!r}")

        lifecycle = _require_str(trace_path, idx, row, "lifecycle").strip().lower()
        if lifecycle not in VALID_LIFECYCLES:
            raise SystemExit(f"error: {trace_path}: row {idx} has invalid lifecycle {lifecycle!r}")

        mem_valid = _require_int(trace_path, idx, row, "mem_valid")
        mem_is_load = _require_int(trace_path, idx, row, "mem_is_load")
        mem_is_store = _require_int(trace_path, idx, row, "mem_is_store")
        if mem_valid not in {0, 1} or mem_is_load not in {0, 1} or mem_is_store not in {0, 1}:
            raise SystemExit(f"error: {trace_path}: row {idx} memory valid/load/store flags must be 0 or 1")
        if mem_is_load and mem_is_store:
            raise SystemExit(f"error: {trace_path}: row {idx} cannot set both mem_is_load and mem_is_store")
        if mem_valid == 0 and (mem_is_load != 0 or mem_is_store != 0):
            raise SystemExit(f"error: {trace_path}: row {idx} sets memory subtype without mem_valid")

        trap_valid = _require_int(trace_path, idx, row, "trap_valid")
        if trap_valid not in {0, 1}:
            raise SystemExit(f"error: {trace_path}: row {idx} trap_valid must be 0 or 1")

        if args.check_ordering:
            if prev_cycle is not None and cycle < prev_cycle:
                raise SystemExit(f"error: {trace_path}: row {idx} cycle regressed ({cycle} < {prev_cycle})")
            if prev_cycle is not None and cycle == prev_cycle and prev_pc is not None and pc < prev_pc:
                raise SystemExit(f"error: {trace_path}: row {idx} pc regressed within cycle ({pc} < {prev_pc})")
            prev_cycle = cycle
            prev_pc = pc

    if required_block_kinds and observed_block_kinds.isdisjoint(required_block_kinds):
        raise SystemExit(
            f"error: {trace_path}: required block kinds not observed; need one of "
            f"{sorted(required_block_kinds)}, saw {sorted(observed_block_kinds)}"
        )

    print(
        "ok: minst trace schema compatible "
        f"(rows={len(rows)}, trace={actual_major}.{actual_minor}, expected={expected_major}.{expected_minor})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
