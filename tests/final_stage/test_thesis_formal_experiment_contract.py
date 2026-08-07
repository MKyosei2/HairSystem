from __future__ import annotations

import hashlib
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
    spec = importlib.util.spec_from_file_location("thesis_n01_matrix_v3", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ThesisFormalExperimentContractV3Tests(unittest.TestCase):
    def test_canonical_100k_asset_uses_32_groups_per_block(self) -> None:
        app = (ROOT / "apps/dx11_viewer/src/AppMain.cpp").read_text(encoding="utf-8")
        self.assertIn("constexpr uint32_t kGroupsPerBlock = 32u;", app)
        self.assertIn('TextContainsKeyValue(text, "groupsPerBlock", "32")', app)
        self.assertNotIn("constexpr uint32_t kGroupsPerBlock = 8u;", app)

        template = json.loads(
            (ROOT / "docs/sample_assets/novelty01_100k_asset_manifest.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(template["generation"]["groupsPerBlock"], 32)
        self.assertEqual(template["sourceBranch"], "main")

        runtime_template = json.loads(
            (ROOT / "docs/sample_assets/final_runtime_suite_template.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            runtime_template["frozen_asset_contract"]["required_groups_per_block"],
            32,
        )

    def test_formal_contract_locks_single_binary_seeded_session_design(self) -> None:
        contract = json.loads(
            (ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(contract["documentRevision"], "main-single-binary-b0-p-formal-v3")
        self.assertEqual(contract["branch"], "main")
        self.assertEqual(
            contract["sourceCommit"],
            "c90737a74152dacc399d89e82cb06d273840e19d",
        )
        self.assertEqual(contract["sourceCommitRole"], "minimumSupportedAncestor")
        design = contract["comparisonDesign"]
        self.assertEqual(design["primaryMethods"], ["B0", "P"])
        self.assertTrue(design["sameBinary"])
        self.assertTrue(design["sameCommit"])
        self.assertTrue(design["sameCanonicalAsset"])
        self.assertTrue(design["methodIndependentScenarioInputs"])
        self.assertTrue(design["requireScenarioInputHashParity"])
        self.assertTrue(design["requirePhysicalInputHashParity"])
        self.assertEqual(
            design["seedInputAlgorithm"],
            "sha256-scenario-physical-input-v2",
        )
        self.assertTrue(design["sessionIsolation"])
        self.assertTrue(design["exactCommitAndTreeLock"])
        self.assertFalse(contract["baselines"]["B0"]["usesSgaptRepresentation"])

    def test_proposal_uses_common_timeline_and_disables_double_stimulus(self) -> None:
        source = (
            ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
        ).read_text(encoding="utf-8")
        for marker in (
            "ExecuteNovelty01ExperimentTickLegacy",
            "ApplyScenarioForTick(m_simulationTickIndex, dt);",
            "HAIRGPU_N01_TIMELINE_OWNS_EXTERNAL_INPUTS",
            "savedDesignScenarioIndex",
            "std::numeric_limits<uint32_t>::max()",
        ):
            self.assertIn(marker, source)
        call_position = source.index(
            "ApplyScenarioForTick(m_simulationTickIndex, dt);"
        )
        disable_position = source.index(
            "runtime->designScenarioIndex = std::numeric_limits<uint32_t>::max();"
        )
        legacy_position = source.index(
            "ExecuteNovelty01ExperimentTickLegacy(dt);", disable_position
        )
        self.assertLess(call_position, disable_position)
        self.assertLess(disable_position, legacy_position)

    def test_common_timeline_root_values_reach_both_simulation_routes(self) -> None:
        source = (
            ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
        ).read_text(encoding="utf-8")
        for marker in (
            "ApplyScenarioTimelineFrameLegacy(frameIndex);",
            "FormalScenarioRootState",
            "baseScalpPositions",
            "case ScenarioEventType::SetRootTranslation:",
            "case ScenarioEventType::SetRootRotation:",
            "case ScenarioEventType::TeleportRoot:",
            "kDegreesToRadians",
            "state.translation",
            "state.rotationRadians",
            "m_enableScalpMotion = false;",
            "rotatedZ + m_scalpCenter + state.translation",
            "m_restScalpParticles[index].x = transformed.x;",
        ):
            self.assertIn(marker, source)
        self.assertIn(
            "#define ApplyScenarioTimelineFrame "
            "ApplyScenarioTimelineFrameLegacy",
            source,
        )
        self.assertNotIn(
            "m_scalpCenter = m_scalpCenter + Vec3",
            source[source.index(
                "void hairgpu::viewer::GpuHairViewer::ApplyScenarioTimelineFrame("
            ):],
        )

    def test_seeded_physical_inputs_are_deterministic_and_unique_for_all_seeds(self) -> None:
        matrix = load_matrix_module()
        self.assertEqual(
            matrix.SEED_INPUT_ALGORITHM,
            "sha256-scenario-physical-input-v2",
        )
        for scenario in matrix.SCENARIOS:
            hashes: list[str] = []
            for seed in (1, 2, 3, 4, 5):
                first = matrix.build_seeded_events(
                    scenario=scenario,
                    seed=seed,
                    warmup_frames=120,
                    measurement_frames=360,
                    duration_frames=7200,
                )
                repeated = matrix.build_seeded_events(
                    scenario=scenario,
                    seed=seed,
                    warmup_frames=120,
                    measurement_frames=360,
                    duration_frames=7200,
                )
                self.assertTrue(first, scenario)
                self.assertEqual(first, repeated, scenario)
                hashes.append(
                    hashlib.sha256(matrix._canonical_json_bytes(first)).hexdigest()
                )
            self.assertEqual(len(set(hashes)), 5, scenario)

        for scenario in ("S07", "S08", "S12"):
            events = matrix.build_seeded_events(
                scenario=scenario,
                seed=1,
                warmup_frames=120,
                measurement_frames=360,
                duration_frames=7200,
            )
            collider_events = [
                event for event in events
                if event.get("type") == "set_collider_transform"
            ]
            self.assertTrue(collider_events, scenario)
            self.assertTrue(
                all(float(event["value"][1]) > 1.0 for event in collider_events),
                scenario,
            )

    def test_materialized_timeline_hashes_are_stable_for_b0_and_p_pair(self) -> None:
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
            self.assertEqual(
                b0["physicalInputSha256"], proposal["physicalInputSha256"]
            )
            self.assertEqual(
                case_b0["scenario_input_sha256"], case_p["scenario_input_sha256"]
            )
            self.assertEqual(
                case_b0["physical_scenario_input_sha256"],
                case_p["physical_scenario_input_sha256"],
            )

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

    def test_input_parity_rejects_timeline_physical_and_pair_mismatch(self) -> None:
        matrix = load_matrix_module()
        valid = [
            {
                "caseId": "b0",
                "variant": "B0",
                "scenario": "S01",
                "seed": 1,
                "repetition": 0,
                "scenarioInputSha256": "a" * 64,
                "physicalScenarioInputSha256": "c" * 64,
            },
            {
                "caseId": "p",
                "variant": "P",
                "scenario": "S01",
                "seed": 1,
                "repetition": 0,
                "scenarioInputSha256": "a" * 64,
                "physicalScenarioInputSha256": "c" * 64,
            },
        ]
        self.assertEqual(matrix.validate_scenario_input_parity(valid), [])

        physical_mismatch = [dict(valid[0]), dict(valid[1])]
        physical_mismatch[1]["physicalScenarioInputSha256"] = "d" * 64
        issues = matrix.validate_scenario_input_parity(physical_mismatch)
        self.assertTrue(any(row["issue"] == "methodPhysicalInputMismatch" for row in issues))

        incomplete = [dict(valid[0])]
        issues = matrix.validate_scenario_input_parity(incomplete)
        self.assertTrue(any(row["issue"] == "methodPairIncomplete" for row in issues))

    def test_runner_is_ordered_session_isolated_and_never_mutates_git(self) -> None:
        source = (
            ROOT / "tools/final_stage/Run_Thesis_B0_P_Formal.ps1"
        ).read_text(encoding="utf-8")
        for stage in (
            "Prepare", "Asset", "Smoke", "Pilot", "Quality", "Performance",
            "Determinism", "Soak", "Report", "VerifyBranches", "FinalGate", "Archive",
        ):
            self.assertIn(f'"{stage}"', source)
        for marker in (
            "formal_session.json",
            "CURRENT_SESSION.txt",
            "sourceTreeSha",
            "Assert-NewOutputDirectory",
            "Assert-CompletedStages",
            "Invoke-StaticContractChecks",
            "Verify frozen asset bytes and manifest",
            "local_branches_before.txt",
            "hairgpu_thesis_report.py",
            "hairgpu_thesis_final_gate.py",
            "sha256-scenario-physical-input-v2",
        ):
            self.assertIn(marker, source)
        lowered = source.lower()
        for forbidden in (
            "git switch -c", "git checkout -b", "git push", "gh pr create",
        ):
            self.assertNotIn(forbidden, lowered)
        self.assertNotIn('"--force"', source)
        self.assertNotIn("Refresh thesis statistical report", source)

    def test_report_and_final_gate_require_physical_hashes(self) -> None:
        report_path = ROOT / "tools/final_stage/hairgpu_thesis_report.py"
        gate_path = ROOT / "tools/final_stage/hairgpu_thesis_final_gate.py"
        report_source = report_path.read_text(encoding="utf-8")
        gate_source = gate_path.read_text(encoding="utf-8")
        compile(report_source, str(report_path), "exec")
        compile(gate_source, str(gate_path), "exec")
        self.assertIn("physicalScenarioInputHashMatch", report_source)
        self.assertIn("physicalScenarioInputSha256", gate_source)
        self.assertIn("E_VIEWER_HASH", gate_source)

    def test_obsolete_patch_artifacts_are_non_authoritative(self) -> None:
        authoritative_paths = (
            ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp",
            ROOT / "tools/final_stage/Run_Thesis_B0_P_Formal.ps1",
            ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py",
        )
        combined = "\n".join(
            path.read_text(encoding="utf-8") for path in authoritative_paths
        )
        for obsolete in (
            "thesis_formal_main.patch",
            "thesis_formal_v2.patch",
            "CXX_EXISTING_FILES.patch",
            "Apply_ThesisFormalFix.ps1",
            "Apply_ThesisFormalFix_v2.ps1",
        ):
            self.assertNotIn(obsolete, combined)

        contract = json.loads(
            (ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json").read_text(
                encoding="utf-8"
            )
        )
        source_of_truth = contract["sourceOfTruth"]
        self.assertFalse(source_of_truth["oldApplyScriptsReferencedByBuild"])
        self.assertTrue(source_of_truth["unappliedPatchArtifactsAreNonAuthoritative"])


if __name__ == "__main__":
    unittest.main()
