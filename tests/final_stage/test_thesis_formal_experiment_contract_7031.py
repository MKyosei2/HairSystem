from __future__ import annotations

import importlib
import json
import sys
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
FINAL_STAGE_DIR = ROOT / "tools" / "final_stage"
if str(FINAL_STAGE_DIR) not in sys.path:
    sys.path.insert(0, str(FINAL_STAGE_DIR))

RUNNER = FINAL_STAGE_DIR / "Run_Thesis_B0_P_Formal_7031.ps1"
BASE_RUNNER = FINAL_STAGE_DIR / "Run_Thesis_B0_P_Formal_7030.ps1"
MATRIX = FINAL_STAGE_DIR / "hairgpu_novelty01_matrix_7031.py"
REPORT = FINAL_STAGE_DIR / "hairgpu_thesis_report_7031.py"
FINAL_GATE = FINAL_STAGE_DIR / "hairgpu_thesis_final_gate_7031.py"
CONTRACT = ROOT / "docs" / "experiments" / "NOVELTY01_FORMAL_CONTRACT_7031.json"


class ThesisFormalExperiment7031Tests(unittest.TestCase):
    def test_contract_preserves_exact_90_run_design(self) -> None:
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

    def test_contract_keeps_fixed_workload(self) -> None:
        fixed = json.loads(CONTRACT.read_text(encoding="utf-8"))["fixedWorkload"]
        self.assertEqual(fixed["displayStrandCount"], 100000)
        self.assertEqual(fixed["guideStrandCount"], 4000)
        self.assertEqual(fixed["followersPerGuide"], 24)
        self.assertEqual(fixed["particlesPerStrand"], 128)
        self.assertEqual(fixed["solverIterations"], 6)
        self.assertEqual(fixed["groupsPerBlock"], 32)
        self.assertEqual(fixed["renderWidth"], 1920)
        self.assertEqual(fixed["renderHeight"], 1080)
        self.assertIs(fixed["vsync"], False)
        self.assertEqual(fixed["presentSyncInterval"], 0)

    def test_contract_requires_fail_closed_window_guard(self) -> None:
        guard = json.loads(CONTRACT.read_text(encoding="utf-8"))["formalWindowGuard"]
        self.assertEqual(guard["revision"], "fixed-client-window-guard-v1")
        self.assertIs(guard["required"], True)
        self.assertEqual(guard["expectedClientWidth"], 1920)
        self.assertEqual(guard["expectedClientHeight"], 1080)
        self.assertEqual(guard["expectedGuardRecords"], 90)
        self.assertIs(guard["disableResizeFrame"], True)
        self.assertIs(guard["disableMaximize"], True)
        self.assertIs(guard["autoRecoverAfterActualResize"], False)
        self.assertIs(guard["viewerExit92RemainsFatal"], True)
        self.assertEqual(guard["guardFailureExitCode"], 96)

    def test_runner_transforms_only_7030_revision_bindings(self) -> None:
        source = RUNNER.read_text(encoding="utf-8")
        self.assertIn("Run_Thesis_B0_P_Formal_7030.ps1", source)
        self.assertIn('$source.Replace("70.30", "70.31")', source)
        self.assertIn('$source.Replace("7030", "7031")', source)
        for marker in (
            "hairgpu_novelty01_matrix_7031.py",
            "hairgpu_thesis_report_7031.py",
            "hairgpu_thesis_final_gate_7031.py",
            "NOVELTY01_FORMAL_CONTRACT_7031.json",
            "test_thesis_formal_experiment_contract_7031.py",
            'formalRevision = "70.31"',
        ):
            self.assertIn(marker, source)
        self.assertTrue(BASE_RUNNER.is_file())

    def test_matrix_binds_guard_to_production_execute_case(self) -> None:
        module = importlib.import_module("hairgpu_novelty01_matrix_7031")
        runtime_globals = module.legacy.final_run.execute_case.__globals__
        self.assertIs(runtime_globals.get("run_process"), module._run_process_7031)
        self.assertIs(
            runtime_globals.get("validate_outputs"),
            module.revision7030._validate_outputs_7030,
        )
        self.assertEqual(module.revision7030.FORMAL_REVISION, "70.31")
        self.assertEqual(
            module.legacy.FORMAL_CONTRACT_RELATIVE,
            Path("docs/experiments/NOVELTY01_FORMAL_CONTRACT_7031.json"),
        )

    def test_guard_only_targets_formal_hairsystem_viewer(self) -> None:
        module = importlib.import_module("hairgpu_novelty01_matrix_7031")
        self.assertTrue(
            module._is_formal_viewer_command(
                [r"C:\x\HairSystem.exe", "--formal-run"]
            )
        )
        self.assertFalse(
            module._is_formal_viewer_command(
                [r"C:\x\HairSystem.exe", "--native-self-test"]
            )
        )
        self.assertFalse(
            module._is_formal_viewer_command(["python.exe", "--formal-run"])
        )
        self.assertFalse(module._is_formal_viewer_command([]))

    def test_native_exit_92_can_never_be_hidden(self) -> None:
        module = importlib.import_module("hairgpu_novelty01_matrix_7031")
        passing = {"status": "PASS", "violations": []}
        failing = {"status": "FAIL", "violations": [{"kind": "resize"}]}
        self.assertEqual(module._guard_result_return_code(92, passing), 92)
        self.assertEqual(module._guard_result_return_code(92, failing), 92)
        self.assertEqual(module._guard_result_return_code(3, passing), 3)

    def test_guard_failure_only_changes_nominal_success_to_fail_closed_96(self) -> None:
        module = importlib.import_module("hairgpu_novelty01_matrix_7031")
        self.assertEqual(
            module._guard_result_return_code(
                0, {"status": "PASS", "violations": []}
            ),
            0,
        )
        self.assertEqual(
            module._guard_result_return_code(
                0, {"status": "FAIL", "violations": []}
            ),
            96,
        )
        self.assertEqual(module._guard_result_return_code(0, None), 96)
        self.assertEqual(
            module._guard_result_return_code(
                0, {"status": "PASS", "violations": [{"kind": "client-area-changed"}]}
            ),
            96,
        )

    def test_window_guard_preserves_client_area_and_disables_resize_affordances(self) -> None:
        source = MATRIX.read_text(encoding="utf-8")
        for marker in (
            "WS_THICKFRAME",
            "WS_MAXIMIZEBOX",
            "SetWindowLongW",
            "AdjustWindowRectEx",
            "SetWindowPos",
            "SC_SIZE",
            "SC_MAXIMIZE",
            "GetClientRect",
            "formal_window_guard_7031.json",
            "autoRecoveryAfterActualResize\": False",
            "viewerExit92RemainsFatal\": True",
        ):
            self.assertIn(marker, source)
        self.assertIn("if native != 0:\n        return native", source)
        self.assertIn("Do not restore the window here", source)

    def test_report_requires_all_90_guard_records(self) -> None:
        source = REPORT.read_text(encoding="utf-8")
        self.assertIn("EXPECTED_WINDOW_GUARD_RECORDS = 90", source)
        self.assertIn("formal_window_guard_7031.json", source)
        self.assertIn('status = "FAIL"', source)
        self.assertIn('payload["formalWindowGuard"]', source)

    def test_final_gate_checks_guard_before_legacy_integrity_gate(self) -> None:
        source = FINAL_GATE.read_text(encoding="utf-8")
        audit_index = source.index("collect_window_guard_evidence(session)")
        legacy_index = source.index("revision7030.legacy.main()")
        self.assertLess(audit_index, legacy_index)
        self.assertIn('"status": "FAIL"', source)
        self.assertIn('payload["formalWindowGuard"]', source)
        self.assertIn("expectedTotalRunCount", source)
        self.assertIn("expectedMethodRunCount", source)

    def test_threshold_outcome_policy_is_not_weakened_or_reclassified(self) -> None:
        policy = json.loads(CONTRACT.read_text(encoding="utf-8"))["resultPolicy"]
        self.assertIs(policy["performanceThresholdMissIsFatal"], False)
        self.assertIs(policy["qualityThresholdMissIsFatal"], False)
        self.assertIs(policy["hardwareThermalOrClockThresholdMissIsFatal"], False)
        self.assertIs(policy["determinismMismatchIsFatal"], False)
        self.assertIn(
            "actual render client-area change during formal measurement",
            policy["failClosedOn"],
        )


if __name__ == "__main__":
    unittest.main()
