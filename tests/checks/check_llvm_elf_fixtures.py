#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def _run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def _load_rows(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.strip():
            rows.append(json.loads(raw))
    return rows


def _fail(case_id: str, stage: str, message: str) -> SystemExit:
    return SystemExit(f"error: {case_id} failed at {stage}: {message}")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Validate committed LLVM ELF fixtures for linx-model")
    ap.add_argument("--root", required=True)
    ap.add_argument("--cli", required=True)
    ap.add_argument("--validator", required=True)
    ap.add_argument("--manifest", required=True)
    args = ap.parse_args(argv)

    root = Path(args.root).resolve()
    cli = Path(args.cli).resolve()
    validator = Path(args.validator).resolve()
    manifest_path = Path(args.manifest).resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    for case in manifest.get("cases", []):
        case_id = str(case["id"])
        elf = root / str(case["elf"])
        if not elf.is_file():
            raise _fail(case_id, "fixture", f"missing ELF fixture: {elf}")

        disasm = _run([str(cli), "--bin", str(elf), "--disasm-only"])
        if disasm.returncode != 0:
            raise _fail(case_id, "disasm", disasm.stdout)
        disasm_text = disasm.stdout.lower()
        expected_disasm = [str(item) for item in case.get("expect_disasm_any_of", [])]
        if expected_disasm and not any(token.lower() in disasm_text for token in expected_disasm):
            raise _fail(case_id, "disasm_match", f"expected any of {expected_disasm!r} in disassembly")

        with tempfile.TemporaryDirectory(prefix=f"linx-model-fixture-{case_id.lower()}-") as td:
            tmpdir = Path(td)
            trace = tmpdir / f"{case_id}.jsonl"
            result = _run(
                [
                    str(cli),
                    "--engine",
                    "ref",
                    "--bin",
                    str(elf),
                    "--emit-minst-trace",
                    str(trace),
                    "--max-cycles",
                    str(case.get("max_cycles", 512)),
                ]
            )
            if result.returncode != 0:
                raise _fail(case_id, "ref", result.stdout)

            validate_cmd = [
                sys.executable,
                str(validator),
                "--trace",
                str(trace),
                "--expected-version",
                str(case.get("trace_version", "1.0")),
                "--check-ordering",
            ]
            for block_kind in case.get("require_block_kind_any_of", []):
                validate_cmd.extend(["--require-block-kind-any-of", str(block_kind)])
            validate = _run(validate_cmd)
            if validate.returncode != 0:
                raise _fail(case_id, "schema", validate.stdout)

            rows = _load_rows(trace)
            min_records = int(case.get("min_records", 1))
            if len(rows) < min_records:
                raise _fail(case_id, "trace_len", f"expected >= {min_records} rows, got {len(rows)}")

            expected_mnemonics = {str(item).upper() for item in case.get("expect_mnemonics_any_of", [])}
            if expected_mnemonics:
                seen = {str(row.get("mnemonic", "")).upper() for row in rows}
                if seen.isdisjoint(expected_mnemonics):
                    raise _fail(case_id, "mnemonic_match", f"expected any of {sorted(expected_mnemonics)!r}, got {sorted(seen)!r}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
