from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ORCHESTRATOR = ROOT / "tools" / "final_stage" / "Run_Thesis_B0_P_Formal_7032_All.ps1"
DOC = ROOT / "docs" / "experiments" / "NOVELTY01_FORMAL_7032_ORCHESTRATION.md"


class ThesisFormalOrchestrator7032Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = ORCHESTRATOR.read_text(encoding="utf-8")
        cls.doc = DOC.read_text(encoding="utf-8")

    def test_7032_is_orchestration_only_and_keeps_7031_evidence(self) -> None:
        self.assertIn('$OrchestrationRevision = "70.32"', self.source)
        self.assertIn('$FormalRevision = "70.31"', self.source)
        self.assertIn("Run_Thesis_B0_P_Formal_7031.ps1", self.source)
        self.assertNotIn("Run_Thesis_B0_P_Formal_7032.ps1", self.source)
        self.assertIn("formal revision 70.31", self.doc)

    def test_power_plan_is_authoritative_by_guid_not_localized_name(self) -> None:
        guid = "8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c"
        self.assertGreaterEqual(self.source.count(guid), 1)
        self.assertIn("Get-ActivePowerSchemeGuid", self.source)
        self.assertIn("powerPlanGuid", self.source)
        self.assertIn(
            "localized power-plan name intentionally ignored",
            self.source.lower(),
        )
        self.assertNotIn("高パフォーマンス", self.source)
        self.assertNotRegex(
            self.source,
            r"powerPlan\s+-notmatch\s+.*High performance",
        )

    def test_complete_stage_order_is_fixed_and_excludes_smoke_and_qualification(self) -> None:
        expected = [
            "Prepare",
            "Asset",
            "Pilot",
            "Quality",
            "Performance",
            "Determinism",
            "Soak",
            "Report",
            "VerifyBranches",
            "FinalGate",
            "Archive",
        ]
        positions = []
        for stage in expected:
            token = f'Invoke-FormalStage -Stage "{stage}"'
            self.assertEqual(self.source.count(token), 1, token)
            positions.append(self.source.index(token))
        self.assertEqual(positions, sorted(positions))
        self.assertNotIn('Invoke-FormalStage -Stage "Smoke"', self.source)
        self.assertNotIn('Invoke-FormalStage -Stage "Qualification"', self.source)

    def test_cumulative_window_guard_checks_match_90_run_design(self) -> None:
        for count in (0, 10, 46, 82, 86, 90):
            self.assertIn(
                f"Assert-WindowGuardCount -ExpectedCount {count}",
                self.source,
            )
        self.assertIn('formal_window_guard_7031.json', self.source)
        self.assertIn('fixed-client-window-guard-v1', self.source)
        self.assertIn('viewerExit92RemainsFatal', self.source)

    def test_final_completion_requires_gate_session_archive_and_hash(self) -> None:
        self.assertIn('FORMAL_COMPLETE_VALID', self.source)
        self.assertIn('FinalGate did not PASS', self.source)
        self.assertIn('Get-FileHash -LiteralPath $archivePath -Algorithm SHA256', self.source)
        self.assertIn('Window Guard evidence: PASS', self.source)
        self.assertIn('Assert-FinalEvidence', self.source)

    def test_real_run_requires_exact_main_commit(self) -> None:
        self.assertIn('Real formal execution requires -ExpectedCommit', self.source)
        self.assertIn('origin/main does not match ExpectedCommit', self.source)
        self.assertIn('Local main does not match ExpectedCommit', self.source)
        self.assertRegex(self.source, r'ExpectedCommit.*40-character')

    def test_validate_only_path_is_hardware_free_and_exercises_7031_transform(self) -> None:
        self.assertIn('[switch]$ValidateOnly', self.source)
        validate_index = self.source.index('if ($ValidateOnly)')
        host_index = self.source.index('Assert-FormalHostPreflight\n', validate_index)
        self.assertLess(validate_index, host_index)
        validate_block = self.source[validate_index:host_index]
        self.assertIn('-ValidateTransformOnly', validate_block)
        self.assertIn('70.32 orchestrator parser/runtime validation: PASS', validate_block)

    def test_orchestrator_does_not_generate_another_dynamic_powershell_program(self) -> None:
        self.assertNotIn('[ScriptBlock]::Create', self.source)
        self.assertNotIn('$DriverScript', self.source)
        self.assertNotIn('$FullDriverSource', self.source)
        self.assertNotIn('[IO.File]::WriteAllText(\n        $Driver', self.source)


if __name__ == "__main__":
    unittest.main()
