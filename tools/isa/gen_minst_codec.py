#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path


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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--spec",
        default="/Users/zhoubot/linx-isa/isa/v0.4/linxisa-v0.4.json",
    )
    parser.add_argument(
        "--header",
        default="include/linx/model/isa/generated_tables.hpp",
    )
    parser.add_argument(
        "--source",
        default="src/isa/generated_tables.cpp",
    )
    args = parser.parse_args()

    spec = json.loads(Path(args.spec).read_text())
    forms, fields, pieces, constraints = build_forms(spec)
    render_header(Path(args.header))
    render_source(Path(args.source), forms, fields, pieces, constraints)
    print(f"generated {len(forms)} forms, {len(fields)} fields, {len(pieces)} pieces, {len(constraints)} constraints")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
