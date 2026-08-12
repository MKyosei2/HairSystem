from __future__ import annotations

import importlib
import math
import sys
import unittest
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
FINAL_STAGE = ROOT / "tools" / "final_stage"
if str(FINAL_STAGE) not in sys.path:
    sys.path.insert(0, str(FINAL_STAGE))


class Thesis7030RuntimeValidationBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.shim = importlib.import_module("hairgpu_novelty01_matrix_7030")

    def test_execute_case_uses_7030_validation_policy(self) -> None:
        bound = self.shim.legacy.final_run.execute_case.__globals__.get(
            "validate_outputs"
        )
        self.assertIs(bound, self.shim._validate_outputs_7030)

    def test_observed_thermal_and_clock_misses_are_nonfatal_outcomes(self) -> None:
        payload = {
            "valid": False,
            "qualityGatePassed": False,
            "issues": [
                {
                    "severity": "error",
                    "code": "E_HARDWARE_GPU_TEMPERATURE_SPREAD",
                    "message": "spread",
                    "expected": 12.0,
                    "observed": 23.0,
                },
                {
                    "severity": "error",
                    "code": "E_HARDWARE_GPU_CLOCK_DROP",
                    "message": "clock",
                    "expected": 0.15,
                    "observed": 0.9226519337016574,
                },
            ],
        }
        result = self.shim._demote_nonfatal_outcomes(payload)
        self.assertTrue(result["valid"])
        self.assertFalse(result["qualityGatePassed"])
        self.assertEqual(len(result["nonFatalOutcomeIssues"]), 2)
        self.assertTrue(
            all(item["severity"] == "quality" for item in result["issues"])
        )

    def test_exact_production_binding_applies_demotion(self) -> None:
        payload = {
            "valid": False,
            "qualityGatePassed": False,
            "issues": [
                {
                    "severity": "error",
                    "code": "E_HARDWARE_GPU_TEMPERATURE_SPREAD",
                    "expected": 12.0,
                    "observed": 23.0,
                }
            ],
        }
        old = self.shim._original_validate_outputs
        try:
            self.shim._original_validate_outputs = lambda *args, **kwargs: payload
            bound = self.shim.legacy.final_run.execute_case.__globals__[
                "validate_outputs"
            ]
            result = bound(object(), object(), object(), object())
        finally:
            self.shim._original_validate_outputs = old
        self.assertTrue(result["valid"])
        self.assertEqual(result["issues"][0]["severity"], "quality")

    def test_missing_thermal_observation_remains_fatal(self) -> None:
        payload = {
            "valid": False,
            "qualityGatePassed": False,
            "issues": [
                {
                    "severity": "error",
                    "code": "E_HARDWARE_GPU_TEMPERATURE_SPREAD",
                    "expected": 12.0,
                    "observed": None,
                }
            ],
        }
        result = self.shim._demote_nonfatal_outcomes(payload)
        self.assertFalse(result["valid"])
        self.assertEqual(result["issues"][0]["severity"], "error")

    def test_hardware_identity_and_evidence_failures_remain_fatal(self) -> None:
        for code, observed in (
            ("E_HARDWARE_GPU_NAME", "Wrong GPU"),
            ("E_HARDWARE_AC_POWER", False),
            ("E_HARDWARE_POWER_SCHEME", "balanced"),
            ("E_HARDWARE_GPU_CLOCK_SAMPLES", 0),
            ("E_HARDWARE_GPU_STABILITY_WINDOW", "wrong-policy"),
            ("E_NAN_STATE", 1),
        ):
            with self.subTest(code=code):
                result = self.shim._demote_nonfatal_outcomes(
                    {
                        "valid": False,
                        "qualityGatePassed": False,
                        "issues": [
                            {
                                "severity": "error",
                                "code": code,
                                "observed": observed,
                            }
                        ],
                    }
                )
                self.assertFalse(result["valid"])
                self.assertEqual(result["issues"][0]["severity"], "error")

    def test_nan_and_infinite_outcome_values_are_not_demoted(self) -> None:
        for observed in (math.nan, math.inf, -math.inf):
            with self.subTest(observed=observed):
                result = self.shim._demote_nonfatal_outcomes(
                    {
                        "valid": False,
                        "qualityGatePassed": False,
                        "issues": [
                            {
                                "severity": "error",
                                "code": "E_HARDWARE_GPU_CLOCK_DROP",
                                "observed": observed,
                            }
                        ],
                    }
                )
                self.assertFalse(result["valid"])


if __name__ == "__main__":
    unittest.main()
