#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


EXPECTED_RELEASE = "0.58.1"
EXPECTED_ENCODING_ABI = "pto-isa-0.58.1-mode-function-v1"
EXPECTED_ENCODING_PROJECTION_SHA256 = (
    "89b872d6eaf0252200bc9349d49b9346e2a69d894cdcc2dcd0fd71911c1e0b8c"
)
EXPECTED_SOURCE_COMMIT = "c381465b2b8e457e162a4246ee58bb9a2c5b49fd"
EXPECTED_SOURCE_TREE = "463a19db3d6ba70022f18bdbca0d4b2c6ed586e4"
EXPECTED_CATALOGS = {
    "command_forms": {
        "count": 74,
        "sha256": "300a3a57a8728e6c4770da6fff0202b372ec2830edb8dc978dc141d1c26424d0",
    },
    "scalar_forms": {
        "count": 474,
        "sha256": "9f3841d568ffa73fcb43bf4fd365d3c4dba42d27acffa7e273e0f403c0f0c602",
    },
    "tile_operations": {
        "count": 109,
        "sha256": "f163dea8be281fd67173713d373b60f95a9c3c4e558adcdf8034cc213507a1a3",
    },
    "extension_encoding_reservations": {
        "count": 32,
        "sha256": "bdb82b839b98984779d9a1394f6b308f141052ef0b520e5bedb8e87dadd883d4",
    },
}
EXPECTED_CODEC_COUNTS = {
    "forms": 765,
    "fields": 2661,
    "pieces": 3401,
    "constraints": 780,
}
EXPECTED_CATALOG_CONTENT_SHA256 = "c1750250ec295e690bd22c20fd7c7f350db5e1bb4ce2417493dc094d7f007878"
EXPECTED_LOCK_CONTENT_SHA256 = "fec69d22b2757ebb8da3876b16e1d5845af188f107f06d05422af15513309dfd"
EXPECTED_RELEASE_MANIFEST_CONTENT_SHA256 = (
    "3f8f746b52aa14ad39c6be83d0ebf3bc260c992c4d3e932b10cef612d0217f6c"
)


def c_string(value: str) -> str:
    value = value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{value}"'


def parse_int(value: str) -> int:
    text = str(value).strip()
    if text.lower().startswith("0x"):
        return int(text, 16)
    if text.lower().startswith("0b"):
        return int(text, 2)
    return int(text, 10)


def parse_hex(value: str) -> int:
    text = str(value).strip().lower()
    if not text.startswith("0x"):
        raise ValueError(f"expected hex string, got {value!r}")
    return int(text, 16)


def op_name(op: str) -> str:
    return {
        "==": "Eq",
        "!=": "Ne",
        "<": "Lt",
        "<=": "Le",
        ">": "Gt",
        ">=": "Ge",
    }[op]


def pattern_to_mask_match(pattern: str) -> tuple[int, int]:
    width_bits = len(pattern)
    mask = 0
    match = 0
    for idx, ch in enumerate(pattern):
        bit = width_bits - 1 - idx
        if ch == ".":
            continue
        if ch == "1":
            match |= 1 << bit
        mask |= 1 << bit
    return mask, match


def build_reg_aliases(spec: dict) -> tuple[dict[str, int], list[str]]:
    alias_to_code: dict[str, int] = {}
    code_to_asm = [""] * 32
    entries = ((spec.get("registers") or {}).get("reg5") or {}).get("entries") or []
    for entry in entries:
        code = int(entry["code"])
        asm = str(entry.get("asm", ""))
        if 0 <= code < 32:
            code_to_asm[code] = asm
        alias_to_code[asm.upper()] = code
        alias_to_code[str(entry.get("name", "")).upper()] = code
        for alias in entry.get("aliases", []):
            alias_to_code[str(alias).upper()] = code
    return alias_to_code, code_to_asm


def build_forms(spec: dict) -> tuple[list[dict], list[dict], list[dict], list[dict]]:
    alias_to_code, _ = build_reg_aliases(spec)
    forms = []
    fields = []
    pieces = []
    constraints = []

    for inst in spec.get("instructions", []):
        encoding = inst.get("encoding") or {}
        parts = list(encoding.get("parts") or [])
        offsets = []
        bit_offset = 0
        for part in parts:
            offsets.append(bit_offset)
            bit_offset += int(part.get("width_bits", 0))

        pattern = "".join(
            str(parts[i].get("pattern", "")).replace(" ", "") for i in reversed(range(len(parts)))
        )
        length_bits = int(encoding.get("length_bits", inst.get("length_bits", len(pattern))))
        if len(pattern) != length_bits:
            pattern = (("." * length_bits) + pattern)[-length_bits:]
        mask, match = pattern_to_mask_match(pattern)

        form_field_start = len(fields)
        form_constraint_start = len(constraints)
        merged_fields: dict[str, dict] = {}
        field_order: list[str] = []

        for part_index, part in enumerate(parts):
            part_offset = offsets[part_index]
            for field in part.get("fields", []):
                name = str(field["name"])
                if name not in merged_fields:
                    merged_fields[name] = {
                        "name": name,
                        "signed_hint": -1,
                        "pieces": [],
                    }
                    field_order.append(name)
                if field.get("signed") is True:
                    merged_fields[name]["signed_hint"] = 1
                elif field.get("signed") is False and merged_fields[name]["signed_hint"] < 0:
                    merged_fields[name]["signed_hint"] = 0
                for piece in field.get("pieces", []):
                    merged_fields[name]["pieces"].append(
                        {
                            "insn_lsb": int(piece.get("insn_lsb", 0)) + part_offset,
                            "insn_msb": int(piece.get("insn_msb", 0)) + part_offset,
                            "width": int(piece.get("width", 0)),
                            "value_lsb": int(piece.get("value_lsb", 0) or 0),
                        }
                    )

            for constraint in part.get("constraints", []):
                raw_value = str(constraint["value"])
                try:
                    resolved = parse_int(raw_value)
                except Exception:
                    resolved = alias_to_code.get(raw_value.upper(), 0)
                constraints.append(
                    {
                        "field_name": str(constraint["field"]),
                        "op": op_name(str(constraint["op"])),
                        "value": resolved,
                        "value_raw": raw_value,
                    }
                )

        for field_name in field_order:
            merged = merged_fields[field_name]
            merged["pieces"].sort(key=lambda piece: (piece["value_lsb"], piece["insn_lsb"]))
            bit_width = 0
            for piece in merged["pieces"]:
                bit_width = max(bit_width, piece["value_lsb"] + piece["width"])
            piece_start = len(pieces)
            for piece in merged["pieces"]:
                pieces.append(
                    {
                        "insn_lsb": piece["insn_lsb"],
                        "width": piece["width"],
                        "value_lsb": piece["value_lsb"],
                    }
                )
            fields.append(
                {
                    "name": field_name,
                    "signed_hint": merged["signed_hint"],
                    "bit_width": bit_width,
                    "piece_start": piece_start,
                    "piece_count": len(merged["pieces"]),
                }
            )

        forms.append(
            {
                "uid": str(inst.get("uid", "")),
                "mnemonic": str(inst.get("mnemonic", "")),
                "asm_template": str(inst.get("asm", "") or ""),
                "encoding_kind": str(inst.get("encoding_kind", "") or ""),
                "group": str(inst.get("group", "") or ""),
                "uop_group": str(inst.get("uop_group", "") or ""),
                "uop_big_kind": str(inst.get("uop_big_kind", "") or ""),
                "length_bits": length_bits,
                "fixed_bits": int(mask).bit_count(),
                "mask": mask,
                "match": match,
                "field_start": form_field_start,
                "field_count": len(fields) - form_field_start,
                "constraint_start": form_constraint_start,
                "constraint_count": len(constraints) - form_constraint_start,
            }
        )

    return forms, fields, pieces, constraints


def validate_authority(spec: dict, lock: dict, release_manifest: dict) -> dict[str, int]:
    checks = [
        (lock.get("release") == EXPECTED_RELEASE, "release mismatch"),
        (lock.get("encoding_abi") == EXPECTED_ENCODING_ABI, "encoding ABI mismatch"),
        (
            lock.get("encoding_projection_sha256") == EXPECTED_ENCODING_PROJECTION_SHA256,
            "projection hash mismatch",
        ),
        (
            (lock.get("source") or {}).get("commit") == EXPECTED_SOURCE_COMMIT,
            "source commit mismatch",
        ),
        (
            (lock.get("source") or {}).get("tree") == EXPECTED_SOURCE_TREE,
            "source tree mismatch",
        ),
        (spec.get("version") == EXPECTED_RELEASE, "catalog release mismatch"),
        (spec.get("instruction_count") == EXPECTED_CODEC_COUNTS["forms"], "form count mismatch"),
        (release_manifest.get("version") == EXPECTED_RELEASE, "release manifest mismatch"),
        (
            release_manifest.get("source_lock") == "isa/v0.58/pto-spec.lock.json",
            "release manifest source lock mismatch",
        ),
    ]
    for condition, message in checks:
        if not condition:
            raise ValueError(message)

    catalogs = lock.get("catalogs") or {}
    for name, expected in EXPECTED_CATALOGS.items():
        actual = catalogs.get(name) or {}
        if actual.get("sha256") != expected["sha256"]:
            raise ValueError(f"catalog hash mismatch: {name}")
        if actual.get("count") != expected["count"]:
            raise ValueError(f"catalog count mismatch: {name}")

    cardinality = release_manifest.get("cardinality") or {}
    for name, expected in (
        ("command_forms", 74),
        ("scalar_forms", 474),
        ("tile_operations", 109),
        ("extension_encoding_reservations", 32),
    ):
        if cardinality.get(name) != expected:
            raise ValueError(f"release manifest count mismatch: {name}")

    forms, fields, pieces, constraints = build_forms(spec)
    counts = {
        "forms": len(forms),
        "fields": len(fields),
        "pieces": len(pieces),
        "constraints": len(constraints),
    }
    if counts != EXPECTED_CODEC_COUNTS:
        raise ValueError(f"codec count mismatch: expected {EXPECTED_CODEC_COUNTS}, got {counts}")

    by_name = {form["mnemonic"]: form for form in forms}
    required_forms = {
        "B.FPATR": (0x7FFF, 0x2023),
        "BSTART.ICALL": (0xF83FFFFF, 0x50166001),
    }
    for mnemonic, (mask, match) in required_forms.items():
        form = by_name.get(mnemonic)
        if form is None or (form["mask"], form["match"]) != (mask, match):
            raise ValueError(f"required form mismatch: {mnemonic}")
    return counts


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def load_and_validate_authority(
    spec_path: Path, lock_path: Path, release_manifest_path: Path
) -> tuple[dict, dict[str, int]]:
    paths = {
        "catalog": Path(spec_path),
        "lock": Path(lock_path),
        "release manifest": Path(release_manifest_path),
    }
    for label, path in paths.items():
        if not path.is_file():
            raise ValueError(f"authority file is missing ({label}): {path}")
    raw = {label: path.read_bytes() for label, path in paths.items()}
    expected_hashes = {
        "catalog": EXPECTED_CATALOG_CONTENT_SHA256,
        "lock": EXPECTED_LOCK_CONTENT_SHA256,
        "release manifest": EXPECTED_RELEASE_MANIFEST_CONTENT_SHA256,
    }
    for label, expected in expected_hashes.items():
        actual = _sha256_bytes(raw[label])
        if actual != expected:
            message = "catalog content hash mismatch" if label == "catalog" else f"{label} content hash mismatch"
            raise ValueError(f"{message}: expected {expected}, got {actual}")
    spec = json.loads(raw["catalog"])
    lock = json.loads(raw["lock"])
    release_manifest = json.loads(raw["release manifest"])
    return spec, validate_authority(spec, lock, release_manifest)


def render_header(out_path: Path) -> None:
    text = """#pragma once

#include <cstddef>

#include "linx/model/isa/codec.hpp"

namespace linx::model::isa::generated {

extern const MinstFieldPieceDesc kFieldPieces[];
extern const std::size_t kFieldPieceCount;
extern const MinstFieldDesc kFields[];
extern const std::size_t kFieldCount;
extern const MinstConstraintDesc kConstraints[];
extern const std::size_t kConstraintCount;
extern const MinstFormDesc kForms[];
extern const std::size_t kFormCount;

}  // namespace linx::model::isa::generated
"""
    out_path.write_text(text)


def render_source(out_path: Path, forms: list[dict], fields: list[dict], pieces: list[dict], constraints: list[dict]) -> None:
    lines = [
        '#include "linx/model/isa/generated_tables.hpp"',
        "",
        "namespace linx::model::isa::generated {",
        "",
        "const MinstFieldPieceDesc kFieldPieces[] = {",
    ]
    for piece in pieces:
        lines.append(
            f"    {{.insn_lsb = {piece['insn_lsb']}, .width = {piece['width']}, .value_lsb = {piece['value_lsb']}}},"
        )
    lines.extend(
        [
            "};",
            f"const std::size_t kFieldPieceCount = {len(pieces)};",
            "",
            "const MinstFieldDesc kFields[] = {",
        ]
    )
    for field in fields:
        lines.append(
            "    {"
            f".name = {c_string(field['name'])}, "
            f".signed_hint = {field['signed_hint']}, "
            f".bit_width = {field['bit_width']}, "
            f".piece_start = {field['piece_start']}, "
            f".piece_count = {field['piece_count']}"
            "},"
        )
    lines.extend(
        [
            "};",
            f"const std::size_t kFieldCount = {len(fields)};",
            "",
            "const MinstConstraintDesc kConstraints[] = {",
        ]
    )
    for constraint in constraints:
        lines.append(
            "    {"
            f".field_name = {c_string(constraint['field_name'])}, "
            f".op = MinstConstraintOp::{constraint['op']}, "
            f".value = {constraint['value']}, "
            f".value_raw = {c_string(constraint['value_raw'])}"
            "},"
        )
    lines.extend(
        [
            "};",
            f"const std::size_t kConstraintCount = {len(constraints)};",
            "",
            "const MinstFormDesc kForms[] = {",
        ]
    )
    for form in forms:
        lines.append(
            "    {"
            f".uid = {c_string(form['uid'])}, "
            f".mnemonic = {c_string(form['mnemonic'])}, "
            f".asm_template = {c_string(form['asm_template'])}, "
            f".encoding_kind = {c_string(form['encoding_kind'])}, "
            f".group = {c_string(form['group'])}, "
            f".uop_group = {c_string(form['uop_group'])}, "
            f".uop_big_kind = {c_string(form['uop_big_kind'])}, "
            f".length_bits = {form['length_bits']}, "
            f".fixed_bits = {form['fixed_bits']}, "
            f".mask = 0x{form['mask']:x}ULL, "
            f".match = 0x{form['match']:x}ULL, "
            f".field_start = {form['field_start']}, "
            f".field_count = {form['field_count']}, "
            f".constraint_start = {form['constraint_start']}, "
            f".constraint_count = {form['constraint_count']}"
            "},"
        )
    lines.extend(
        [
            "};",
            f"const std::size_t kFormCount = {len(forms)};",
            "",
            "}  // namespace linx::model::isa::generated",
        ]
    )
    out_path.write_text("\n".join(lines) + "\n")


def format_generated_cpp(paths: list[Path]) -> None:
    clang_format = shutil.which("clang-format")
    if clang_format is None:
        raise RuntimeError("clang-format is required to generate committed Minst codec tables")
    subprocess.run([clang_format, "-i", *map(str, paths)], check=True)


def main() -> int:
    model_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--authority-root",
        default=os.environ.get("LINXISA_AUTHORITY_ROOT", ""),
        help="Exact LinxISA checkout containing isa/v0.58 authority files.",
    )
    parser.add_argument(
        "--spec",
        default=None,
    )
    parser.add_argument(
        "--lock",
        default=None,
    )
    parser.add_argument(
        "--release-manifest",
        default=None,
    )
    parser.add_argument(
        "--header",
        default=model_root / "include/linx/model/isa/generated_tables.hpp",
    )
    parser.add_argument(
        "--source",
        default=model_root / "src/isa/generated_tables.cpp",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail when the committed outputs differ from freshly generated tables.",
    )
    args = parser.parse_args()

    authority_root = Path(args.authority_root).expanduser().resolve() if args.authority_root else None
    if authority_root is None and not all((args.spec, args.lock, args.release_manifest)):
        candidate = model_root.parents[1]
        if (candidate / "isa/v0.58/linxisa-v0.58.json").is_file():
            authority_root = candidate
    if authority_root is None and not all((args.spec, args.lock, args.release_manifest)):
        raise SystemExit(
            "error: authority file is missing; set --authority-root or LINXISA_AUTHORITY_ROOT"
        )
    spec_path = Path(args.spec) if args.spec else authority_root / "isa/v0.58/linxisa-v0.58.json"
    lock_path = Path(args.lock) if args.lock else authority_root / "isa/v0.58/pto-spec.lock.json"
    release_manifest_path = (
        Path(args.release_manifest)
        if args.release_manifest
        else authority_root / "isa/v0.58/release_manifest.json"
    )
    try:
        spec, _ = load_and_validate_authority(spec_path, lock_path, release_manifest_path)
    except (ValueError, json.JSONDecodeError) as exc:
        raise SystemExit(f"error: {exc}") from exc
    forms, fields, pieces, constraints = build_forms(spec)
    header = Path(args.header)
    source = Path(args.source)
    if args.check:
        with tempfile.TemporaryDirectory(
            prefix=".linx-model-codec-check.", dir=model_root
        ) as td:
            temp_header = Path(td) / header.name
            temp_source = Path(td) / source.name
            render_header(temp_header)
            render_source(temp_source, forms, fields, pieces, constraints)
            format_generated_cpp([temp_header, temp_source])
            stale = [
                str(path)
                for path, fresh in ((header, temp_header), (source, temp_source))
                if not path.is_file() or path.read_bytes() != fresh.read_bytes()
            ]
            if stale:
                raise SystemExit("error: stale generated codec: " + ", ".join(stale))
    else:
        header.parent.mkdir(parents=True, exist_ok=True)
        source.parent.mkdir(parents=True, exist_ok=True)
        render_header(header)
        render_source(source, forms, fields, pieces, constraints)
        format_generated_cpp([header, source])
    print(f"generated {len(forms)} forms, {len(fields)} fields, {len(pieces)} pieces, {len(constraints)} constraints")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
