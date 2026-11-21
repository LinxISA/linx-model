#!/usr/bin/env python3
from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path


MODEL_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(MODEL_ROOT / "tools/isa"))
from gen_pto_scoped_scalar import runtime_binding  # noqa: E402


def make_form(action: str, address_kind: str, fields: list[str]) -> dict:
    return {
        "form_id": "synthetic",
        "fields": [{"name": name} for name in fields],
        "agu": {
            "action": action,
            "address_kind": address_kind,
            "update_mode": "None",
            "offset_scale": 2,
            "size_bytes": 4,
            "signed_load": action == "Load",
        },
    }


class ScopedRuntimeGeneratorTests(unittest.TestCase):
    def test_action_specific_register_fields_are_emitted(self) -> None:
        load = runtime_binding(make_form("Load", "Immediate", ["RegDst", "SrcL", "simm12"]), "load")
        store = runtime_binding(make_form("Store", "Immediate", ["SrcL", "SrcR", "simm12"]), "store")
        hl_store = runtime_binding(
            make_form("Store", "Immediate", ["SrcD", "SrcR", "simm22"]), "hl-store"
        )
        pcr_store = runtime_binding(make_form("Store", "PCRelative", ["SrcL", "simm"]), "pcr-store")
        self.assertEqual(load[-3:], ("SrcL", "RegDst", "simm12"))
        self.assertEqual(store[-3:], ("SrcR", "SrcL", "simm12"))
        self.assertEqual(hl_store[-3:], ("SrcR", "SrcD", "simm22"))
        self.assertEqual(pcr_store[-3:], ("None", "SrcL", "simm"))

    def test_missing_or_extra_required_field_fails_closed(self) -> None:
        form = make_form("Load", "Immediate", ["RegDst", "SrcL", "simm12"])
        missing = copy.deepcopy(form)
        missing["fields"] = [field for field in missing["fields"] if field["name"] != "SrcL"]
        with self.assertRaisesRegex(ValueError, "unexpected runtime field set"):
            runtime_binding(missing, "missing")
        extra = copy.deepcopy(form)
        extra["fields"].append({"name": "SrcR"})
        with self.assertRaisesRegex(ValueError, "unexpected runtime field set"):
            runtime_binding(extra, "extra")


if __name__ == "__main__":
    unittest.main()
