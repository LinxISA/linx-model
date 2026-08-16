#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


RUNNER_PATH = Path(__file__).with_name("run_model_diff_suite.py")
SPEC = importlib.util.spec_from_file_location("model_diff_runner", RUNNER_PATH)
assert SPEC is not None and SPEC.loader is not None
model_diff_runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(model_diff_runner)


class ModelDiffRunnerTests(unittest.TestCase):
    def test_consumer_cannot_reuse_stale_result_output(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            result = Path(td) / "result.bin"
            result.write_bytes(b"stale")
            completed = subprocess.CompletedProcess([], 0, "")
            with mock.patch.object(
                model_diff_runner, "_run", return_value=completed
            ):
                with self.assertRaisesRegex(SystemExit, "fresh result memory"):
                    model_diff_runner._run_with_fresh_output(
                        ["consumer"], result, consumer="qemu"
                    )
            self.assertFalse(result.exists())

    def test_consumer_must_create_result_after_invocation(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            result = Path(td) / "result.bin"

            def produce(_cmd: list[str], **_kwargs: object) -> subprocess.CompletedProcess[str]:
                result.write_bytes(b"fresh")
                return subprocess.CompletedProcess([], 0, "")

            with mock.patch.object(model_diff_runner, "_run", side_effect=produce):
                completed = model_diff_runner._run_with_fresh_output(
                    ["consumer"], result, consumer="ref"
                )
            self.assertEqual(completed.returncode, 0)
            self.assertEqual(result.read_bytes(), b"fresh")

    def test_dump_failure_cannot_leave_reusable_result(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            result = Path(td) / "result.bin"
            result.write_bytes(b"stale")
            failed = subprocess.CompletedProcess([], 7, "dump failed")
            with mock.patch.object(model_diff_runner, "_run", return_value=failed):
                completed = model_diff_runner._run_with_fresh_output(
                    ["consumer"], result, consumer="qemu"
                )
            self.assertEqual(completed.returncode, 7)
            self.assertFalse(result.exists())

    def test_release_profile_selects_only_required_cases(self) -> None:
        cases = [
            {"id": "dev", "required_in_profile": ["dev"]},
            {"id": "release", "required_in_profile": ["release-strict"]},
            {"id": "both", "required_in_profile": ["dev", "release-strict"]},
        ]
        selected = model_diff_runner._selected_cases(cases, "release-strict")
        self.assertEqual([case["id"] for case in selected], ["release", "both"])


if __name__ == "__main__":
    unittest.main()
