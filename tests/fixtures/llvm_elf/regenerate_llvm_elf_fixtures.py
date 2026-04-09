#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path


def _run(cmd: list[str]) -> None:
    result = subprocess.run(
        cmd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(result.stdout)


def _superproject_root(repo_root: Path) -> Path:
    return repo_root.parents[1]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Regenerate committed LLVM ELF fixtures for linx-model tests")
    ap.add_argument("--root", default=Path(__file__).resolve().parents[3], type=Path)
    ap.add_argument("--manifest", default=Path(__file__).resolve().with_name("manifest.json"), type=Path)
    ap.add_argument("--clang", default="")
    ap.add_argument("--clangxx", default="")
    ap.add_argument("--llvm-mc", dest="llvm_mc", default="")
    ap.add_argument("--llc", default="")
    ap.add_argument("--lld", default="")
    args = ap.parse_args(argv)

    repo_root = args.root.resolve()
    super_root = _superproject_root(repo_root)
    manifest_path = args.manifest.resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    tool_root = super_root / "compiler/llvm/build-linxisa-clang/bin"
    clang = Path(args.clang).resolve() if args.clang else tool_root / "clang"
    clangxx = Path(args.clangxx).resolve() if args.clangxx else tool_root / "clang++"
    llvm_mc = Path(args.llvm_mc).resolve() if args.llvm_mc else tool_root / "llvm-mc"
    llc = Path(args.llc).resolve() if args.llc else tool_root / "llc"
    lld = Path(args.lld).resolve() if args.lld else tool_root / "ld.lld"

    for tool in (clang, clangxx, llvm_mc, llc, lld):
        if not tool.is_file():
            raise SystemExit(f"error: missing tool {tool}")

    for case in manifest.get("cases", []):
        source = super_root / str(case["source"])
        out = repo_root / str(case["elf"])
        out.parent.mkdir(parents=True, exist_ok=True)
        kind = str(case.get("source_kind", "")).strip().lower() or source.suffix.lower().lstrip(".")
        with tempfile.TemporaryDirectory(prefix=f"linx-model-fixture-{case['id'].lower()}-") as td:
            tmpdir = Path(td)
            obj = tmpdir / "case.o"
            if kind in {"s", "asm"}:
                _run([str(llvm_mc), "-triple=linx64", "-filetype=obj", str(source), "-o", str(obj)])
            elif kind in {"c", "cc", "cpp", "cxx"}:
                tool = clangxx if source.suffix.lower() in {".cc", ".cpp", ".cxx"} else clang
                _run(
                    [
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
                        str(source),
                        "-o",
                        str(obj),
                    ]
                )
            elif kind in {"ll", "ir"}:
                _run([str(llc), "-mtriple=linx64", "-filetype=obj", str(source), "-o", str(obj)])
            else:
                raise SystemExit(f"error: unsupported source kind {kind!r} for {source}")
            _run([str(lld), "-r", "-o", str(out), str(obj)])

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
