#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


RUNNER_PATH = Path(__file__).with_name("run_model_diff_suite.py")
SPEC = importlib.util.spec_from_file_location("model_diff_runner", RUNNER_PATH)
assert SPEC is not None and SPEC.loader is not None
model_diff_runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(model_diff_runner)


class ModelDiffRunnerTests(unittest.TestCase):
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
