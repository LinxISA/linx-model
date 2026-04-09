#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def _run(cmd: list[str], *, env: dict[str, str] | None = None, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def _load_suite(path: Path) -> dict[str, Any]:
    import yaml  # type: ignore

    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
      raise SystemExit(f"error: invalid suite: {path}")
    return data


def _default_bin(root: Path, rel: str) -> Path:
    return root / rel


def _validate_trace(validator: Path, trace: Path, case: dict[str, Any]) -> subprocess.CompletedProcess[str]:
    cmd = [
        sys.executable,
        str(validator),
        "--trace",
        str(trace),
        "--expected-version",
        str(case.get("trace_version", "1.0")),
        "--check-ordering",
    ]
    for block_kind in case.get("require_block_kind_any_of", []):
        cmd.extend(["--require-block-kind-any-of", str(block_kind)])
    return _run(cmd)


def _compile_case(root: Path, workdir: Path, case: dict[str, Any], clang: Path, clangxx: Path, llvm_mc: Path, llc: Path, lld: Path) -> Path:
    src = root / str(case["source"])
    obj = workdir / "case.o"
    kind = str(case.get("source_kind", "")).strip().lower() or src.suffix.lower().lstrip(".")
    if kind in {"s", "asm"}:
        cmd = [str(llvm_mc), "-triple=linx64", "-filetype=obj", str(src), "-o", str(obj)]
        result = _run(cmd)
    elif kind in {"c", "cc", "cpp", "cxx"}:
        tool = clangxx if src.suffix.lower() in {".cc", ".cpp", ".cxx"} else clang
        cmd = [
            str(tool),
            "-target",
            "linx64",
            "-O2",
            "-ffreestanding",
            "-fno-builtin",
            "-fno-stack-protector",
            "-fno-exceptions",
            "-fno-unwind-tables",
            "-nostdlib",
            "-c",
            str(src),
            "-o",
            str(obj),
        ]
        result = _run(cmd)
    elif kind in {"ll", "ir"}:
        cmd = [str(llc), "-mtriple=linx64", "-filetype=obj", str(src), "-o", str(obj)]
        result = _run(cmd)
    else:
        raise SystemExit(f"error: unsupported source kind {kind!r} for {src}")
    if result.returncode != 0:
        raise SystemExit(result.stdout)
    linked = workdir / "linked.o"
    result = _run([str(lld), "-r", "-o", str(linked), str(obj)])
    if result.returncode != 0:
        raise SystemExit(result.stdout)
    return linked


def _load_trace(path: Path) -> list[dict[str, Any]]:
    if not path.is_file():
        raise FileNotFoundError(path)
    out: list[dict[str, Any]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.strip():
            out.append(json.loads(raw))
    return out


def _normalized(rows: list[dict[str, Any]], ignore: set[str]) -> list[dict[str, Any]]:
    norm: list[dict[str, Any]] = []
    for row in rows:
        item = {k: v for k, v in row.items() if k not in ignore}
        norm.append(item)
    return norm


def _drop_boundary_selfloops(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return [
        row
        for row in rows
        if not row.get("next_pc") == row.get("pc")
        or not str(row.get("mnemonic", "")).startswith(("BSTART.", "C.BSTART"))
    ]


def _first_mismatch(ref_rows: list[dict[str, Any]], other_rows: list[dict[str, Any]]) -> dict[str, Any] | None:
    for idx, (ref_row, other_row) in enumerate(zip(ref_rows, other_rows)):
        if ref_row != other_row:
            differing = {
                key: {"ref": ref_row.get(key), "other": other_row.get(key)}
                for key in sorted(set(ref_row) | set(other_row))
                if ref_row.get(key) != other_row.get(key)
            }
            return {
                "index": idx,
                "ref": ref_row,
                "other": other_row,
                "fields": differing,
            }
    if len(ref_rows) != len(other_rows):
        return {
            "index": min(len(ref_rows), len(other_rows)),
            "ref_len": len(ref_rows),
            "other_len": len(other_rows),
        }
    return None


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run tools/model differential suite")
    ap.add_argument("--root", default="")
    ap.add_argument("--suite", default="avs/model/linx_model_diff_suite.yaml")
    ap.add_argument("--workdir", default="")
    args = ap.parse_args(argv)

    root = Path(args.root).resolve() if args.root else Path(__file__).resolve().parents[4]
    suite_path = (root / args.suite).resolve()
    suite = _load_suite(suite_path)

    llvm_mc = _default_bin(root, "compiler/llvm/build-linxisa-clang/bin/llvm-mc")
    clang = _default_bin(root, "compiler/llvm/build-linxisa-clang/bin/clang")
    clangxx = _default_bin(root, "compiler/llvm/build-linxisa-clang/bin/clang++")
    llc = _default_bin(root, "compiler/llvm/build-linxisa-clang/bin/llc")
    lld = _default_bin(root, "compiler/llvm/build-linxisa-clang/bin/ld.lld")
    qemu = _default_bin(root, "emulator/qemu/build/qemu-system-linx64")
    cli = _default_bin(root, "tools/model/build/linx_model_cli")
    validator = _default_bin(root, "tools/model/tests/avs/validate_minst_schema.py")

    if args.workdir:
        base = Path(args.workdir).resolve()
        base.mkdir(parents=True, exist_ok=True)
    else:
        base = Path(tempfile.mkdtemp(prefix="linx-model-suite."))

    ok = True
    summary: dict[str, Any] = {"suite": str(suite_path), "cases": []}
    for idx, case in enumerate(suite.get("cases", [])):
        case_id = str(case["id"])
        case_dir = base / f"{idx:02d}_{case_id}"
        case_dir.mkdir(parents=True, exist_ok=True)
        obj = _compile_case(root, case_dir, case, clang, clangxx, llvm_mc, llc, lld)

        ref_trace = case_dir / "ref.jsonl"
        ca_trace = case_dir / "ca.jsonl"
        qemu_trace = case_dir / "qemu.jsonl"

        result = _run([str(cli), "--engine", "ref", "--bin", str(obj), "--emit-minst-trace", str(ref_trace), "--max-cycles", "512"])
        if result.returncode != 0:
            summary["cases"].append({"id": case_id, "status": "fail", "stage": "ref", "log": result.stdout})
            ok = False
            continue
        result = _validate_trace(validator, ref_trace, case)
        if result.returncode != 0:
            summary["cases"].append({"id": case_id, "status": "fail", "stage": "ref_schema", "log": result.stdout})
            ok = False
            continue

        result = _run([str(cli), "--engine", "compare", "--bin", str(obj), "--emit-minst-trace", str(ca_trace), "--max-cycles", "512"])
        if result.returncode != 0:
            summary["cases"].append({"id": case_id, "status": "fail", "stage": "compare", "log": result.stdout})
            ok = False
            continue
        result = _validate_trace(validator, ca_trace, case)
        if result.returncode != 0:
            summary["cases"].append({"id": case_id, "status": "fail", "stage": "compare_schema", "log": result.stdout})
            ok = False
            continue

        env = dict(os.environ)
        env["LINX_MINST_TRACE"] = str(qemu_trace)
        result = _run([str(qemu), "-nographic", "-monitor", "none", "-machine", "virt", "-kernel", str(obj)], env=env)
        if result.returncode != 0:
            summary["cases"].append({"id": case_id, "status": "fail", "stage": "qemu", "log": result.stdout})
            ok = False
            continue
        result = _validate_trace(validator, qemu_trace, case)
        if result.returncode != 0:
            summary["cases"].append({"id": case_id, "status": "fail", "stage": "qemu_schema", "log": result.stdout})
            ok = False
            continue

        ignore = {
            "schema_version",
            "pc_hex",
            "next_pc_hex",
            "insn_hex",
            "bytes_hex",
            "asm",
            "dump",
        }
        ignore.update(str(item) for item in case.get("ignore_fields", []))
        try:
            ref_rows = _normalized(_load_trace(ref_trace), ignore)
            ca_rows = _normalized(_load_trace(ca_trace), ignore)
            qemu_rows = _normalized(_load_trace(qemu_trace), ignore)
        except FileNotFoundError as exc:
            summary["cases"].append(
                {
                    "id": case_id,
                    "status": "fail",
                    "stage": "qemu_trace",
                    "log": f"missing trace file: {str(exc)}",
                }
            )
            ok = False
            continue
        if case.get("drop_boundary_selfloops", False):
            ref_rows = _drop_boundary_selfloops(ref_rows)
            ca_rows = _drop_boundary_selfloops(ca_rows)
            qemu_rows = _drop_boundary_selfloops(qemu_rows)
        if ref_rows != ca_rows:
            summary["cases"].append(
                {
                    "id": case_id,
                    "status": "fail",
                    "stage": "ca_trace_diff",
                    "mismatch": _first_mismatch(ref_rows, ca_rows),
                }
            )
            ok = False
            continue
        if ref_rows != qemu_rows:
            summary["cases"].append(
                {
                    "id": case_id,
                    "status": "fail",
                    "stage": "trace_diff",
                    "mismatch": _first_mismatch(ref_rows, qemu_rows),
                }
            )
            ok = False
            continue

        summary["cases"].append({"id": case_id, "status": "pass"})

    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
