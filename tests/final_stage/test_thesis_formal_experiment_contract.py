from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_matrix_module():
    final_run_stub = types.ModuleType("hairgpu_final_run")
    final_run_stub.execute_case = lambda *args, **kwargs: {}
    preflight_stub = types.ModuleType("hairgpu_novelty01_preflight")
    preflight_stub.preflight = lambda *args, **kwargs: {"passed": True, "errors": []}
    sys.modules["hairgpu_final_run"] = final_run_stub
    sys.modules["hairgpu_novelty01_preflight"] = preflight_stub

    path = ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py"
    spec = importlib.util.spec_from_file_location("thesis_n01_matrix_v2", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ThesisFormalExperimentContractV2Tests(unittest.TestCase):
    def test_canonical_100k_asset_uses_32_groups_per_block(self) -> None:
        app = (ROOT / "apps/dx11_viewer/src/AppMain.cpp").read_text(encoding="utf-8")
        self.assertIn("constexpr uint32_t kGroupsPerBlock = 32u;", app)
        self.assertIn('TextContainsKeyValue(text, "groupsPerBlock", "32")', app)
        self.assertNotIn("constexpr uint32_t kGroupsPerBlock = 8u;", app)

    def test_formal_contract_locks_single_binary_seeded_session_design(self) -> None:
        contract = json.loads(
            (ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(contract["documentRevision"], "main-single-binary-b0-p-formal-v2")
        self.assertEqual(contract["branch"], "main")
        design = contract["comparisonDesign"]
        self.assertEqual(design["primaryMethods"], ["B0", "P"])
        self.assertTrue(design["sameBinary"])
        self.assertTrue(design["sameCommit"])
        self.assertTrue(design["methodIndependentScenarioInputs"])
        self.assertTrue(design["requireScenarioInputHashParity"])
        self.assertEqual(design["seedInputAlgorithm"], "sha256-scenario-input-v1")
        self.assertTrue(design["sessionIsolation"])
        self.assertTrue(design["exactCommitAndTreeLock"])
        self.assertFalse(contract["baselines"]["B0"]["usesSgaptRepresentation"])

    def test_proposal_uses_common_timeline_and_disables_double_stimulus(self) -> None:
        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl"
        ).read_text(encoding="utf-8")
        marker = "ApplyScenarioForTick(m_simulationTickIndex, dt);"
        self.assertIn(marker, source)
        self.assertIn("HAIRGPU_N01_TIMELINE_OWNS_EXTERNAL_INPUTS", source)
        flag_position = source.index("HAIRGPU_N01_TIMELINE_OWNS_EXTERNAL_INPUTS")
        scenario_position = source.index(
            "const uint32_t scenario = runtime.designScenarioIndex", flag_position
        )
        return_position = source.index("return;", flag_position)
        self.assertLess(return_position, scenario_position)

    def test_seeded_inputs_are_deterministic_method_independent_and_nonempty(self) -> None:
        matrix = load_matrix_module()
        hashes: dict[str, tuple[bytes, bytes]] = {}
        for scenario in matrix.SCENARIOS:
            first = matrix.build_seeded_events(
                scenario=scenario,
                seed=1,
                warmup_frames=120,
                measurement_frames=360,
                duration_frames=7200,
            )
            repeated = matrix.build_seeded_events(
                scenario=scenario,
                seed=1,
                warmup_frames=120,
                measurement_frames=360,
                duration_frames=7200,
            )
            second_seed = matrix.build_seeded_events(
                scenario=scenario,
                seed=2,
                warmup_frames=120,
                measurement_frames=360,
                duration_frames=7200,
            )
            self.assertTrue(first, scenario)
            self.assertEqual(first, repeated, scenario)
            first_bytes = matrix._canonical_json_bytes(first)
            second_bytes = matrix._canonical_json_bytes(second_seed)
            self.assertNotEqual(first_bytes, second_bytes, scenario)
            hashes[scenario] = (first_bytes, second_bytes)
        self.assertEqual(len(hashes), 18)

    def test_materialized_timeline_hash_is_stable_for_b0_and_p_pair(self) -> None:
        matrix = load_matrix_module()
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            timeline_root = base / "timelines"
            timeline_root.mkdir()
            (timeline_root / "S09.json").write_text(
                json.dumps(
                    {
                        "schema": "hairgpu-novelty01-canonical-scenario-v1",
                        "scenario": "S09_StrongWind",
                        "novelty01DesignScenario": "S09",
                        "durationFrames": 7200,
                        "fixedTimeStepSeconds": 1.0 / 60.0,
                        "events": [],
                    }
                ),
                encoding="utf-8",
            )
            common = {"warmup_frames": 120, "measurement_frames": 360}
            case_b0 = dict(common)
            case_p = dict(common)
            b0 = matrix.materialize_seeded_timeline(
                case_b0, timeline_root, "S09", 5, base / "B0"
            )
            proposal = matrix.materialize_seeded_timeline(
                case_p, timeline_root, "S09", 5, base / "P"
            )
            self.assertEqual(b0["timelineSha256"], proposal["timelineSha256"])
            self.assertEqual(case_b0["scenario_input_sha256"], case_p["scenario_input_sha256"])

    def test_counterbalanced_plan_reverses_method_order(self) -> None:
        matrix = load_matrix_module()
        plan = list(
            matrix.iter_run_plan(
                variants=["B0", "P"],
                scenarios=["S01"],
                seeds=[1],
                repetitions=2,
                schedule="counterbalanced",
            )
        )
        self.assertEqual([row[-1] for row in plan], ["B0", "P", "P", "B0"])

    def test_b0_evidence_is_fail_closed(self) -> None:
        matrix = load_matrix_module()
        valid = {
            "requestedMethodCode": "B0",
            "appliedMethodCode": "B0",
            "observedMethodCode": "B0",
            "enableNoNoveltyBaseline": True,
            "enableSegmentGroupIntegratedState": False,
            "enableSegmentGroupSleep": False,
            "enableStaticCache": False,
            "n01UsesSgaptRepresentation": 0,
            "n01PayloadActiveCount": 0,
            "n01PayloadAllocatedCount": 0,
            "n01PayloadRetiredCount": 0,
            "n01PayloadReusedCount": 0,
        }
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            evidence = matrix.validate_independent_baseline_evidence(
                valid, output, variant="B0", dry_run=False
            )
            self.assertTrue(evidence["baselineContractComplete"])
            invalid = dict(valid)
            invalid.pop("observedMethodCode")
            with self.assertRaises(RuntimeError):
                matrix.validate_independent_baseline_evidence(
                    invalid, output, variant="B0", dry_run=False
                )

    def test_input_parity_rejects_method_hash_mismatch(self) -> None:
        matrix = load_matrix_module()
        valid = [
            {"caseId": "b0", "scenario": "S01", "seed": 1, "repetition": 0, "scenarioInputSha256": "a" * 64},
            {"caseId": "p", "scenario": "S01", "seed": 1, "repetition": 0, "scenarioInputSha256": "a" * 64},
        ]
        self.assertEqual(matrix.validate_scenario_input_parity(valid), [])
        invalid = [dict(valid[0]), dict(valid[1])]
        invalid[1]["scenarioInputSha256"] = "b" * 64
        self.assertTrue(matrix.validate_scenario_input_parity(invalid))

    def test_runner_is_session_isolated_and_never_mutates_git(self) -> None:
        source = (
            ROOT / "tools/final_stage/Run_Thesis_B0_P_Formal.ps1"
        ).read_text(encoding="utf-8")
        for stage in (
            "Prepare", "Asset", "Smoke", "Pilot", "Quality", "Performance",
            "Determinism", "Soak", "Report", "VerifyBranches", "FinalGate", "Archive",
        ):
            self.assertIn(f'"{stage}"', source)
        for marker in (
            "formal_session.json", "CURRENT_SESSION.txt", "sourceTreeSha",
            "Assert-NewOutputDirectory", "remote_branches_before.txt",
            "hairgpu_thesis_report.py", "hairgpu_thesis_final_gate.py",
        ):
            self.assertIn(marker, source)
        lowered = source.lower()
        for forbidden in (
            "git switch -c", "git checkout -b", "git push", "gh pr create",
        ):
            self.assertNotIn(forbidden, lowered)
        self.assertNotIn('"--force"', source)
        self.assertNotIn('"asset", "environment"', source)

    def test_report_and_final_gate_are_present(self) -> None:
        for relative in (
            "tools/final_stage/hairgpu_thesis_report.py",
            "tools/final_stage/hairgpu_thesis_final_gate.py",
        ):
            path = ROOT / relative
            self.assertTrue(path.is_file(), relative)
            compile(path.read_text(encoding="utf-8"), str(path), "exec")


if __name__ == "__main__":
    unittest.main()
