#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


EXPECTED_SIZE_BYTES = {str(code): 1 << (code + 6) for code in range(1, 8)}
REQUIRED_SCENARIOS = {
    "tlsu-zero-mask-noop",
    "tlsu-first-allocation",
    "tlsu-subset-update",
    "tlsu-expansion-rejected",
    "tlsu-descriptor-mismatch-atomic",
    "tlsu-payload-mismatch-atomic",
    "tlsu-undefined-read",
    "tlsu-reset",
    "tmov-equal-mask",
    "tmov-mask-mismatch-rejected",
    "cube-shared-source-full-mask",
    "cube-shared-destination-rejected",
    "tgemv-shared-rejected",
}
EXPECTED_IDENTITY = {
    "release": "0.58.1",
    "encoding_abi": "pto-isa-0.58.1-mode-function-v1",
    "encoding_projection_sha256": "89b872d6eaf0252200bc9349d49b9346e2a69d894cdcc2dcd0fd71911c1e0b8c",
    "source_commit": "c381465b2b8e457e162a4246ee58bb9a2c5b49fd",
    "source_tree": "463a19db3d6ba70022f18bdbca0d4b2c6ed586e4",
    "catalog_counts": {
        "command_forms": 74,
        "extension_encoding_reservations": 32,
        "scalar_forms": 474,
        "tile_operations": 109,
    },
}


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"error: {message}")


def _load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    _require(isinstance(value, dict), "fixture root must be an object")
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the PTO v0.58 Shared differential fixture")
    parser.add_argument("--fixture", required=True)
    args = parser.parse_args()

    fixture = _load(Path(args.fixture))
    _require(fixture.get("schema") == "linx-pto-v058-shared-state-v1", "unexpected schema")
    _require(
        fixture.get("pto_isa_identity") == EXPECTED_IDENTITY,
        "fixture must target the exact PTO ISA 0.58.1 identity",
    )
    _require(fixture.get("pe_count") == 4, "fixture must use the architectural four-PE Core")
    _require(
        fixture.get("pe_mask_bits") == {"3": "PE0", "2": "PE1", "1": "PE2", "0": "PE3"},
        "PE mask must map bit3=PE0 through bit0=PE3",
    )
    _require(fixture.get("shared_tile_ids") == [0, 255], "fixture must cover S0..S255")
    _require(fixture.get("tsize_bytes_per_pe") == EXPECTED_SIZE_BYTES, "TSize table drifted")

    scenarios = fixture.get("scenarios")
    _require(isinstance(scenarios, list), "scenarios must be an array")
    ids = [case.get("id") for case in scenarios if isinstance(case, dict)]
    _require(len(ids) == len(set(ids)), "scenario ids must be unique")
    _require(set(ids) == REQUIRED_SCENARIOS, "scenario inventory drifted")

    for case in scenarios:
        _require(isinstance(case, dict), "each scenario must be an object")
        _require(case.get("operation") in {"TLSU", "TMOV", "CUBE", "TGEMV"},
                 f"{case.get('id')}: invalid operation")
        expected = case.get("expected")
        _require(isinstance(expected, dict), f"{case.get('id')}: expected must be an object")
        _require(expected.get("status") in {"applied", "no-op", "uninitialized", "rejected"},
                 f"{case.get('id')}: invalid expected status")
        if expected.get("status") == "rejected":
            _require(expected.get("state_unchanged") is True,
                     f"{case.get('id')}: rejection must be atomic")

    first = next(case for case in scenarios if case["id"] == "tlsu-first-allocation")
    mask = int(first["input"]["pe_mask"], 0)
    size_code = str(first["input"]["tsize_code"])
    expected_charge = mask.bit_count() * EXPECTED_SIZE_BYTES[size_code]
    _require(first["expected"]["core_charge_bytes"] == expected_charge,
             "first allocation Core charge must be popcount(mask) * bytes-per-PE")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
