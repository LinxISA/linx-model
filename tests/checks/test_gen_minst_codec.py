#!/usr/bin/env python3
from __future__ import annotations

import copy
import importlib.util
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


MODEL_ROOT = Path(__file__).resolve().parents[2]
SUPERPROJECT_ROOT = Path(
    os.environ.get("LINXISA_AUTHORITY_ROOT", MODEL_ROOT.parents[1])
).resolve()
GENERATOR_PATH = MODEL_ROOT / "tools/isa/gen_minst_codec.py"

module_spec = importlib.util.spec_from_file_location("gen_minst_codec", GENERATOR_PATH)
assert module_spec is not None and module_spec.loader is not None
gen_minst_codec = importlib.util.module_from_spec(module_spec)
module_spec.loader.exec_module(gen_minst_codec)


class GenMinstCodecTests(unittest.TestCase):
    def setUp(self) -> None:
        self.spec = json.loads(
            (SUPERPROJECT_ROOT / "isa/v0.58/linxisa-v0.58.json").read_text()
        )
        self.lock = json.loads(
            (SUPERPROJECT_ROOT / "isa/v0.58/pto-spec.lock.json").read_text()
        )
        self.release_manifest = json.loads(
            (SUPERPROJECT_ROOT / "isa/v0.58/release_manifest.json").read_text()
        )

    def test_same_count_nonrequired_encoding_mutation_fails_content_authentication(self) -> None:
        mutated = copy.deepcopy(self.spec)
        form = next(
            item for item in mutated["instructions"] if item["mnemonic"] == "ADDI"
        )
        pattern = form["encoding"]["parts"][0]["pattern"]
        replacement = "1" if pattern[0] != "1" else "0"
        form["encoding"]["parts"][0]["pattern"] = replacement + pattern[1:]
        self.assertEqual(len(mutated["instructions"]), 757)

        with tempfile.TemporaryDirectory() as td:
            spec_path = Path(td) / "linxisa-v0.58.json"
            lock_path = Path(td) / "pto-spec.lock.json"
            manifest_path = Path(td) / "release_manifest.json"
            spec_path.write_text(json.dumps(mutated), encoding="utf-8")
            lock_path.write_text(
                (SUPERPROJECT_ROOT / "isa/v0.58/pto-spec.lock.json").read_text(),
                encoding="utf-8",
            )
            manifest_path.write_text(
                (SUPERPROJECT_ROOT / "isa/v0.58/release_manifest.json").read_text(),
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, r"catalog content hash mismatch"):
                gen_minst_codec.load_and_validate_authority(
                    spec_path, lock_path, manifest_path
                )

    def test_first_use_exception_mutation_fails_content_authentication(self) -> None:
        mutated = copy.deepcopy(self.spec)
        first_use = mutated["state"]["system_registers"]["trapno_encoding"][
            "first_use_exception"
        ]
        first_use["cause_value"] = 5

        with tempfile.TemporaryDirectory() as td:
            spec_path = Path(td) / "linxisa-v0.58.json"
            lock_path = Path(td) / "pto-spec.lock.json"
            manifest_path = Path(td) / "release_manifest.json"
            spec_path.write_text(json.dumps(mutated), encoding="utf-8")
            lock_path.write_bytes(
                (SUPERPROJECT_ROOT / "isa/v0.58/pto-spec.lock.json").read_bytes()
            )
            manifest_path.write_bytes(
                (SUPERPROJECT_ROOT / "isa/v0.58/release_manifest.json").read_bytes()
            )

            with self.assertRaisesRegex(ValueError, r"catalog content hash mismatch"):
                gen_minst_codec.load_and_validate_authority(
                    spec_path, lock_path, manifest_path
                )

    def _assert_catalog_mutation_rejected(self, mutate) -> None:
        mutated = copy.deepcopy(self.spec)
        mutate(mutated)

        with tempfile.TemporaryDirectory() as td:
            spec_path = Path(td) / "linxisa-v0.58.json"
            lock_path = Path(td) / "pto-spec.lock.json"
            manifest_path = Path(td) / "release_manifest.json"
            spec_path.write_text(json.dumps(mutated), encoding="utf-8")
            lock_path.write_bytes(
                (SUPERPROJECT_ROOT / "isa/v0.58/pto-spec.lock.json").read_bytes()
            )
            manifest_path.write_bytes(
                (SUPERPROJECT_ROOT / "isa/v0.58/release_manifest.json").read_bytes()
            )

            with self.assertRaisesRegex(ValueError, r"catalog content hash mismatch"):
                gen_minst_codec.load_and_validate_authority(
                    spec_path, lock_path, manifest_path
                )

    def test_retired_scalar_branch_resurrection_fails_content_authentication(self) -> None:
        def resurrect_branch(spec) -> None:
            resurrected = copy.deepcopy(
                next(item for item in spec["instructions"] if item["mnemonic"] == "ADDI")
            )
            resurrected["mnemonic"] = "B.EQ"
            spec["instructions"].append(resurrected)
            spec["instruction_count"] += 1

        self._assert_catalog_mutation_rejected(resurrect_branch)

    def test_b_iot_pemode_regression_fails_content_authentication(self) -> None:
        def restore_pe_mask(spec) -> None:
            form = next(item for item in spec["instructions"] if item["mnemonic"] == "B.IOT")
            field = next(
                field
                for part in form["encoding"]["parts"]
                for field in part["fields"]
                if field["name"] == "PEMode"
            )
            field["name"] = "PE_MASK"

        self._assert_catalog_mutation_rejected(restore_pe_mask)

    def test_b_ios_sizecode_regression_fails_content_authentication(self) -> None:
        def restore_tsize(spec) -> None:
            form = next(item for item in spec["instructions"] if item["mnemonic"] == "B.IOS")
            field = next(
                field
                for part in form["encoding"]["parts"]
                for field in part["fields"]
                if field["name"] == "SizeCode"
            )
            field["name"] = "TSize"

        self._assert_catalog_mutation_rejected(restore_tsize)

    def test_b_fpatr_transpose_regression_fails_content_authentication(self) -> None:
        def remove_transpose_operand(spec) -> None:
            form = next(item for item in spec["instructions"] if item["mnemonic"] == "B.FPATR")
            fields = form["encoding"]["parts"][0]["fields"]
            fields[:] = [field for field in fields if field["name"] != "TransA"]

        self._assert_catalog_mutation_rejected(remove_transpose_operand)

    def test_explicit_authority_root_supports_standalone_freshness(self) -> None:
        checked = subprocess.run(
            [
                sys.executable,
                str(GENERATOR_PATH),
                "--authority-root",
                str(SUPERPROJECT_ROOT),
                "--header",
                str(MODEL_ROOT / "include/linx/model/isa/generated_tables.hpp"),
                "--source",
                str(MODEL_ROOT / "src/isa/generated_tables.cpp"),
                "--check",
            ],
            text=True,
            capture_output=True,
        )
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)

    def test_missing_standalone_authority_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            checked = subprocess.run(
                [
                    sys.executable,
                    str(GENERATOR_PATH),
                    "--authority-root",
                    str(Path(td) / "missing"),
                    "--check",
                ],
                text=True,
                capture_output=True,
            )
        self.assertNotEqual(checked.returncode, 0)
        self.assertIn("authority file is missing", checked.stdout + checked.stderr)

    def test_every_ctest_job_uses_exact_immutable_authority(self) -> None:
        workflow = (MODEL_ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        jobs = dict(
            re.findall(
                r"^  ([A-Za-z0-9_-]+):\n(.*?)(?=^  [A-Za-z0-9_-]+:\n|\Z)",
                workflow,
                flags=re.MULTILINE | re.DOTALL,
            )
        )
        authority_jobs = {
            name: body
            for name, body in jobs.items()
            if any(
                "-L unit" not in line
                for line in body.splitlines()
                if "ctest --test-dir" in line
            )
        }
        self.assertEqual(set(authority_jobs), {"st", "sanitizers"})
        for job_name, body in authority_jobs.items():
            with self.subTest(job=job_name):
                self.assertIn("repository: LinxISA/linx-isa", body)
                self.assertIn(
                    "ref: 81bfd0e42f20f5be10af3bd3a17492d586ca42a1", body
                )
                self.assertIn("path: linxisa-authority", body)
                self.assertIn(
                    "-DLINXISA_AUTHORITY_ROOT=${{ github.workspace }}/linxisa-authority",
                    body,
                )

    def test_exact_v0583_authority_and_codec_shape_are_accepted(self) -> None:
        counts = gen_minst_codec.validate_authority(
            self.spec, self.lock, self.release_manifest
        )
        self.assertEqual(
            counts,
            {"forms": 757, "fields": 2643, "pieces": 3375, "constraints": 792},
        )

        forms, *_ = gen_minst_codec.build_forms(self.spec)
        by_name = {form["mnemonic"]: form for form in forms}
        by_uid = {form["uid"]: form for form in forms}
        self.assertEqual(by_name["C.BSTART.STD"]["uid"], "8b40f078c14a")
        self.assertEqual(by_name["ADDI"]["uid"], "2decd0a93a0a")
        self.assertEqual(by_name["BSTART.TLOAD"]["uid"], "d0c18bb0ab15")
        self.assertEqual(
            (by_name["B.FPATR"]["mask"], by_name["B.FPATR"]["match"]),
            (0x7E7F, 0x2023),
        )
        self.assertEqual(by_name["B.FPATR"]["uid"], "30c307e06d4a")
        self.assertEqual(by_uid["c11eb189dd83"]["mnemonic"], "B.IOT")
        self.assertEqual(
            (by_uid["c11eb189dd83"]["mask"], by_uid["c11eb189dd83"]["match"]),
            (0xFC07F07F, 0x5013),
        )
        self.assertEqual(by_name["B.IOS"]["uid"], "4ba5ef98fdaa")
        self.assertEqual(
            (by_name["B.IOS"]["mask"], by_name["B.IOS"]["match"]),
            (0xF00871FF, 0x1013),
        )
        for retired in {"B.EQ", "B.GE", "B.GEU", "B.LT", "B.LTU", "B.NE", "B.NZ", "B.Z"}:
            self.assertNotIn(retired, by_name)
        self.assertEqual(
            (by_name["BSTART.ICALL"]["mask"], by_name["BSTART.ICALL"]["match"]),
            (0xF83FFFFF, 0x50166001),
        )

    def test_authority_mutations_fail_closed(self) -> None:
        mutations = [
            ("release", lambda lock: lock.__setitem__("release", "0.58.0")),
            ("encoding ABI", lambda lock: lock.__setitem__("encoding_abi", "wrong")),
            (
                "projection hash",
                lambda lock: lock.__setitem__("encoding_projection_sha256", "0" * 64),
            ),
            (
                "source commit",
                lambda lock: lock["source"].__setitem__("commit", "0" * 40),
            ),
            (
                "source tree",
                lambda lock: lock["source"].__setitem__("tree", "0" * 40),
            ),
            (
                "catalog hash",
                lambda lock: lock["catalogs"]["scalar_forms"].__setitem__(
                    "sha256", "0" * 64
                ),
            ),
            (
                "catalog count",
                lambda lock: lock["catalogs"]["command_forms"].__setitem__(
                    "count", 73
                ),
            ),
        ]
        for label, mutate in mutations:
            with self.subTest(label=label):
                lock = copy.deepcopy(self.lock)
                mutate(lock)
                with self.assertRaisesRegex(ValueError, label):
                    gen_minst_codec.validate_authority(
                        self.spec, lock, self.release_manifest
                    )

    def test_check_mode_rejects_stale_committed_output(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            temp = Path(td)
            header = temp / "generated_tables.hpp"
            source = temp / "generated_tables.cpp"
            common = [
                sys.executable,
                str(GENERATOR_PATH),
                "--spec",
                str(SUPERPROJECT_ROOT / "isa/v0.58/linxisa-v0.58.json"),
                "--lock",
                str(SUPERPROJECT_ROOT / "isa/v0.58/pto-spec.lock.json"),
                "--release-manifest",
                str(SUPERPROJECT_ROOT / "isa/v0.58/release_manifest.json"),
                "--header",
                str(header),
                "--source",
                str(source),
            ]
            generated = subprocess.run(common, text=True, capture_output=True)
            self.assertEqual(generated.returncode, 0, generated.stdout + generated.stderr)
            source.write_text(source.read_text() + "// stale\n")

            checked = subprocess.run([*common, "--check"], text=True, capture_output=True)

            self.assertNotEqual(checked.returncode, 0)
            self.assertIn("stale generated codec", checked.stdout + checked.stderr)


if __name__ == "__main__":
    unittest.main()
