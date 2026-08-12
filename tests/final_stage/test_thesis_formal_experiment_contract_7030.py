from __future__ import annotations

import json
import re
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "tools" / "final_stage" / "Run_Thesis_B0_P_Formal_7030.ps1"
LEGACY_RUNNER = ROOT / "tools" / "final_stage" / "Run_Thesis_B0_P_Formal.ps1"
MATRIX = ROOT / "tools" / "final_stage" / "hairgpu_novelty01_matrix_7030.py"
REPORT = ROOT / "tools" / "final_stage" / "hairgpu_thesis_report_7030.py"
FINAL_GATE = ROOT / "tools" / "final_stage" / "hairgpu_thesis_final_gate_7030.py"
CONTRACT = ROOT / "docs" / "experiments" / "NOVELTY01_FORMAL_CONTRACT_7030.json"


class ThesisFormalExperiment7030Tests(unittest.TestCase):
    def test_7030_contract_is_exactly_90_paired_runs_without_smoke(self) -> None:
        contract = json.loads(CONTRACT.read_text(encoding="utf-8"))
        design = contract["comparisonDesign"]
        budget = design["runBudget"]
        self.assertEqual(design["samplingPolicy"], "paired-thesis-90-v1")
        self.assertIs(design["formalSmokeIncluded"], False)
        self.assertIs(design["qualificationRequiredForFormalEvidence"], False)
        self.assertEqual(budget["formalTotalRuns"], 90)
        self.assertEqual(budget["expectedRunsPerMethod"], 45)
        self.assertEqual(
            budget["profileRunCounts"],
            {
                "pilot": 10,
                "formal": 36,
                "performance": 36,
                "determinism": 4,
                "soak": 4,
            },
        )
        self.assertNotIn("smoke", budget["profileRunCounts"])

    def test_7030_result_policy_records_threshold_misses(self) -> None:
        policy = json.loads(CONTRACT.read_text(encoding="utf-8"))["resultPolicy"]
        self.assertEqual(
            policy["fatalIssuePolicy"],
            "nan-inf-crash-input-mismatch-corruption-v1",
        )
        self.assertIs(policy["performanceThresholdMissIsFatal"], False)
        self.assertIs(policy["qualityThresholdMissIsFatal"], False)
        self.assertIs(policy["hardwareThermalOrClockThresholdMissIsFatal"], False)
        self.assertIs(policy["recordNonFatalOutcomes"], True)

    def test_7030_runner_detaches_qualification_and_removes_formal_smoke(self) -> None:
        source = RUNNER.read_text(encoding="utf-8")
        self.assertIn('"Qualification",', source)
        self.assertNotIn('\n        "Smoke",\n', source)
        self.assertIn('detach-qualification-from-asset', source)
        self.assertIn('smoke-to-qualification', source)
        self.assertIn('pilot-dependency', source)
        self.assertIn('Assert-CompletedStages -Session $session -Required @("Asset")', source)
        self.assertIn('paired-thesis-90-v1', source)
        self.assertIn('expectedFormalRunCount = 90', source)
        self.assertIn('expectedMethodRunCount = 45', source)
        self.assertIn('90 total / 45 B0 / 45 P', source)

    def test_7030_runtime_transform_patterns_match_7029_exactly_once(self) -> None:
        source = LEGACY_RUNNER.read_text(encoding="utf-8")
        patterns = {
            "stage-set": r'"Asset",\s*"Smoke",\s*"Pilot",',
            "sampling-policy": r'\$ReducedSamplingPolicy\s*=\s*"paired-thesis-100-v1"',
            "matrix-runtime-path": r'\$matrix\s*=\s*Join-Path \$Repo "tools\\final_stage\\hairgpu_novelty01_matrix\.py"',
            "report-runtime-path": r'\$reporter\s*=\s*Join-Path \$Repo "tools\\final_stage\\hairgpu_thesis_report\.py"',
            "final-gate-runtime-path": r'\$gate\s*=\s*Join-Path \$Repo "tools\\final_stage\\hairgpu_thesis_final_gate\.py"',
            "detach-qualification-from-asset": r'(?m)^[ \t]*Invoke-PreFormalQualificationSuite -Session \$session[ \t]*\r?$',
            "smoke-to-qualification": r'(?ms)^[ \t]*"Smoke"\s*\{.*?^[ \t]*"Pilot"\s*\{',
            "pilot-dependency": r'Assert-CompletedStages -Session \$session -Required @\("Smoke"\)',
            "remove-smoke-directory": r'"environment", "integrity", "logs", "smoke", "pilot",',
            "session-90-run-metadata": r'samplingPolicy = \$ReducedSamplingPolicy\s*expectedFormalRunCount = 100\s*expectedMethodRunCount = 50',
            "report-remove-smoke-profile": r'(?m)^[ \t]*smoke = 10[ \t]*\r?$',
            "report-summary-count-condition": r'\$summaries\.Count -ne 6',
            "report-summary-count-message": r'requires exactly 6 formal matrix summaries',
            "report-total-count-condition": r'\$total -ne 100 -or \$b0Total -ne 50 -or \$pTotal -ne 50',
            "report-total-count-message": r'Expected=100/50/50',
            "report-pass-message": r'Pre-Report evidence gate: PASS \(100 total / 50 B0 / 50 P\)',
            "thermal-conditioning-7030": r'(?ms)function Invoke-SoakThermalConditioning \{.*?^function Invoke-PreFormalQualificationSuite \{',
        }
        for label, pattern in patterns.items():
            with self.subTest(label=label):
                self.assertEqual(
                    len(re.findall(pattern, source, flags=re.DOTALL)),
                    1,
                    f"70.30 runtime transform {label!r} no longer has exactly one 70.29 target",
                )

    def test_7030_runner_uses_7030_matrix_report_and_final_gate(self) -> None:
        source = RUNNER.read_text(encoding="utf-8")
        self.assertIn("hairgpu_novelty01_matrix_7030.py", source)
        self.assertIn("hairgpu_thesis_report_7030.py", source)
        self.assertIn("hairgpu_thesis_final_gate_7030.py", source)
        self.assertIn("NOVELTY01_FORMAL_CONTRACT_7030.json", source)

    def test_7030_thermal_guard_rejects_idle_p8_and_is_nonfatal(self) -> None:
        source = RUNNER.read_text(encoding="utf-8")
        for marker in (
            '[string]$gpu.pstate -ne "P8"',
            '$minimumLoadedUtilizationPercent',
            '$minimumLoadedClockMHz',
            'p8IdleRejected = $true',
            'nonFatal = $true',
            'DIAGNOSTIC_ONLY_NOT_LOADED',
            'formal Soak continues',
        ):
            self.assertIn(marker, source)

    def test_7030_matrix_demotes_hardware_errors_but_keeps_integrity_validator(self) -> None:
        source = MATRIX.read_text(encoding="utf-8")
        self.assertIn('NON_FATAL_ERROR_PREFIXES = ("E_HARDWARE_",)', source)
        self.assertIn('item["severity"] = "quality"', source)
        self.assertIn('result["valid"] = not any(', source)
        self.assertIn('final_run.validate_outputs = _validate_outputs_7030', source)
        self.assertIn('determinismOutcomeIssues', source)
        self.assertIn('payload["determinismIssues"] = []', source)
        self.assertIn('scenarioInputParityIssues', source)

    def test_7030_final_gate_requires_90_45_45_without_smoke(self) -> None:
        source = FINAL_GATE.read_text(encoding="utf-8")
        self.assertIn('legacy.EXPECTED_TOTAL_RUN_COUNT = 90', source)
        self.assertIn('legacy.EXPECTED_METHOD_RUN_COUNT = 45', source)
        self.assertIn('"pilot": 10', source)
        self.assertNotIn('"smoke": 10', source)
        self.assertNotIn('\n    "Smoke",\n', source)
        self.assertIn('legacy.EXPECTED_SAMPLING_POLICY = SAMPLING_POLICY', source)

    def test_7030_report_exposes_nonfatal_outcomes(self) -> None:
        source = REPORT.read_text(encoding="utf-8")
        self.assertIn('expectedTotalRunCount"] = 90', source)
        self.assertIn('expectedMethodRunCount"] = 45', source)
        self.assertIn('formalSmokeIncluded"] = False', source)
        self.assertIn('qualityOutcomeIssues', source)
        self.assertIn('determinismOutcomeIssues', source)
        self.assertIn('fatalIntegrityIssues', source)


if __name__ == "__main__":
    unittest.main()
