from __future__ import annotations

import json
import tempfile
import time
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "docs" / "experiments" / "NOVELTY01_FORMAL_CONTRACT_7032.json"


class _ExitSoonProcess:
    pid = 12345

    def __init__(self) -> None:
        self.calls = 0

    def poll(self):
        self.calls += 1
        return 0 if self.calls >= 3 else None


class _NeverExitProcess:
    pid = 12346

    def poll(self):
        return None


class ThesisFormalExperimentContract7032Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        import sys

        tools = ROOT / "tools" / "final_stage"
        if str(tools) not in sys.path:
            sys.path.insert(0, str(tools))
        import hairgpu_novelty01_matrix_7032 as matrix7032

        cls.matrix = matrix7032
        cls.contract = json.loads(CONTRACT.read_text(encoding="utf-8"))

    def test_revision_and_90_run_contract_are_exact(self) -> None:
        self.assertEqual(self.matrix.FORMAL_REVISION, "70.32")
        self.assertEqual(self.contract["samplingRevision"], "paired-thesis-90-v1")
        budget = self.contract["comparisonDesign"]["runBudget"]
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
        self.assertFalse(self.contract["comparisonDesign"]["formalSmokeIncluded"])

    def test_guard_v2_contract_only_relaxes_normal_shutdown_race(self) -> None:
        guard = self.contract["formalWindowGuard"]
        self.assertEqual(guard["revision"], "fixed-client-window-guard-v2")
        self.assertEqual(guard["normalShutdownWindowGraceSeconds"], 2.0)
        self.assertFalse(guard["normalShutdownWindowLossIsViolation"])
        self.assertFalse(guard["autoRecoverAfterActualResize"])
        self.assertTrue(guard["viewerExit92RemainsFatal"])
        self.assertTrue(guard["guardFailureAfterNativeSuccessIsFatal"])
        self.assertEqual(guard["guardFailureExitCode"], 96)

    def test_native_nonzero_exit_is_never_replaced_by_guard(self) -> None:
        fail = {"status": "FAIL", "violations": [{"kind": "anything"}]}
        self.assertEqual(self.matrix.revision7031._guard_result_return_code(92, fail), 92)
        self.assertEqual(self.matrix.revision7031._guard_result_return_code(7, fail), 7)
        self.assertEqual(self.matrix.revision7031._guard_result_return_code(0, fail), 96)

    def test_normal_hwnd_loss_can_finish_during_bounded_exit_grace(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            guard = self.matrix._FormalWindowGuard7032(
                _ExitSoonProcess(),
                Path(temporary) / "guard.json",
            )
            self.assertTrue(guard._process_exited_within_shutdown_grace())

    def test_persistent_live_process_does_not_get_shutdown_exception(self) -> None:
        original = self.matrix.NORMAL_SHUTDOWN_WINDOW_GRACE_SECONDS
        try:
            self.matrix.NORMAL_SHUTDOWN_WINDOW_GRACE_SECONDS = 0.05
            with tempfile.TemporaryDirectory() as temporary:
                guard = self.matrix._FormalWindowGuard7032(
                    _NeverExitProcess(),
                    Path(temporary) / "guard.json",
                )
                started = time.monotonic()
                self.assertFalse(guard._process_exited_within_shutdown_grace())
                self.assertLess(time.monotonic() - started, 0.5)
        finally:
            self.matrix.NORMAL_SHUTDOWN_WINDOW_GRACE_SECONDS = original

    def test_production_bindings_remain_fail_closed(self) -> None:
        runtime_globals = self.matrix.revision7031.legacy.final_run.execute_case.__globals__
        self.assertIs(
            runtime_globals.get("run_process"),
            self.matrix.revision7031._run_process_7031,
        )
        self.assertIs(
            runtime_globals.get("validate_outputs"),
            self.matrix.revision7031.revision7030._validate_outputs_7030,
        )
        self.assertIs(
            self.matrix.revision7031._FormalWindowGuard,
            self.matrix._FormalWindowGuard7032,
        )


if __name__ == "__main__":
    unittest.main()
