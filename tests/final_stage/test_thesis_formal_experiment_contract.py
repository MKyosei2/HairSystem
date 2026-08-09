from __future__ import annotations

import csv
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
    final_run_stub.REQUIRED_FRAME_COLUMNS = {
        "gpuHairMilliseconds", "sourceFrameIndex"
    }
    def read_stub_csv(path):
        with Path(path).open("r", encoding="utf-8", newline="") as handle:
            return list(csv.DictReader(handle))

    final_run_stub.read_csv_rows = read_stub_csv
    final_run_stub.read_jsonl = lambda path: [
        json.loads(line)
        for line in Path(path).read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    final_run_stub.validate_outputs = lambda *args, **kwargs: {
        "valid": False,
        "qualityGatePassed": False,
        "issues": [{"severity": "error", "code": "E_CSV_JSONL_PARITY"}],
    }
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

        final_run_source = (
            ROOT / "tools/final_stage/hairgpu_final_run.py"
        ).read_text(encoding="utf-8")

        validator_start = final_run_source.index("def validate_asset_manifest(")
        validator_end = final_run_source.index(
            "\n\ndef validate_frozen_asset_with_viewer",
            validator_start,
        )
        formal_asset_validator = final_run_source[validator_start:validator_end]
        self.assertIn('"groupsPerBlock": 32,', formal_asset_validator)
        self.assertNotIn('"groupsPerBlock": 8,', formal_asset_validator)

        compatibility_start = final_run_source.index(
            "def _validate_asset_manifest("
        )
        compatibility_end = final_run_source.index(
            "\n\ndef _compare_settings",
            compatibility_start,
        )
        compatibility_validator = final_run_source[
            compatibility_start:compatibility_end
        ]
        self.assertIn("'groupsPerBlock': 32,", compatibility_validator)

        b2_start = final_run_source.index('"B2": _preset(')
        b2_end = final_run_source.index('"B3": _preset(', b2_start)
        b2_preset = final_run_source[b2_start:b2_end]
        self.assertIn('groupsPerBlock=8, stateStorage="ExternalArray",', b2_preset)

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

    def test_generated_formal_case_satisfies_viewer_assertions_contract(self) -> None:
        matrix = load_matrix_module()
        contract = json.loads(
            (ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json").read_text(
                encoding="utf-8"
            )
        )
        runtime_template = json.loads(
            (ROOT / "docs/sample_assets/final_runtime_suite_template.json").read_text(
                encoding="utf-8"
            )
        )
        base_template = dict(runtime_template["run_defaults"])

        viewer_source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/"
            / "10_phase1_telemetry_csv_jsonl_summary.inl"
        ).read_text(encoding="utf-8")
        self.assertIn(
            'root.FindAny({ "assertions", "scenario_assertions" })',
            viewer_source,
        )

        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            timeline_root = ROOT / "benchmarks/novelty01/sgapt_scenarios"
            for variant in ("B0", "P"):
                case = matrix.build_case(
                    base_template,
                    variant=variant,
                    scenario="S01",
                    seed=1,
                    repetition=0,
                    profile="smoke",
                    contract=contract,
                )
                self.assertIsInstance(case.get("assertions"), dict)

                matrix.materialize_seeded_timeline(
                    case, timeline_root, "S01", 1, base / variant
                )
                timeline = json.loads(
                    Path(case["scenario_timeline"]).read_text(encoding="utf-8")
                )
                self.assertGreater(len(timeline["events"]), 0)
                self.assertIsInstance(case.get("assertions"), dict)
                self.assertEqual(
                    case["assertions"]["min_applied_events"],
                    len(timeline["events"]),
                )
                self.assertIs(
                    timeline["assertions"]["timelineFinalFrameRequired"],
                    True,
                )

        malformed = dict(base_template)
        malformed["assertions"] = []
        with self.assertRaises(ValueError):
            matrix.build_case(
                malformed,
                variant="B0",
                scenario="S01",
                seed=1,
                repetition=0,
                profile="smoke",
                contract=contract,
            )

    def test_hardware_capture_prefers_nvidia_smi_for_vram_when_available(self) -> None:
        runner = (
            ROOT / "tools/final_stage/Run_Thesis_B0_P_Formal.ps1"
        ).read_text(encoding="utf-8")
        self.assertIn('--query-gpu=name,memory.total', runner)
        self.assertIn('--format=csv,noheader,nounits', runner)
        self.assertIn('gpuVramSource = $gpuVramSource', runner)
        self.assertIn('$gpuVramSource = "nvidia-smi"', runner)
        self.assertIn(
            '$gpuVramSource = "Win32_VideoController.AdapterRAM"',
            runner,
        )

    def test_csv_jsonl_parity_accepts_only_viewer_serialization_equivalence(self) -> None:
        matrix = load_matrix_module()
        self.assertEqual(matrix.CSV_DEFAULTFLOAT_SIGNIFICANT_DIGITS, 6)
        self.assertEqual(
            matrix.TELEMETRY_SERIALIZATION_POLICY,
            "csv-defaultfloat6-jsonl-fixed6-equivalent-v1",
        )
        # Exact regression captured by the 70.9 Smoke failure.
        self.assertTrue(
            matrix._telemetry_scalar_serialization_equivalent(
                "6.98458", "6.984576"
            )
        )
        # A genuinely different measurement must remain fail-closed.
        self.assertFalse(
            matrix._telemetry_scalar_serialization_equivalent(
                "6.98458", "6.98461"
            )
        )
        source = (
            ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py"
        ).read_text(encoding="utf-8")
        self.assertIn("Raw CSV/JSONL bytes are never rewritten.", source)
        self.assertIn("E_CSV_JSONL_PARITY", source)

    def test_serialization_shim_clears_only_format_only_parity_failure(self) -> None:
        matrix = load_matrix_module()
        matrix._install_publication_output_integrity_compatibility()
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            csv_path = base / "frames.csv"
            jsonl_path = base / "frames.jsonl"
            csv_path.write_text(
                "gpuHairMilliseconds,sourceFrameIndex\n6.98458,1\n",
                encoding="utf-8",
            )
            jsonl_path.write_text(
                '{"gpuHairMilliseconds":6.984576,"sourceFrameIndex":1}\n',
                encoding="utf-8",
            )
            paths = {"frames": csv_path, "jsonl": jsonl_path}
            case = {"required_telemetry_fields": ["gpuHairMilliseconds"]}
            accepted = matrix.final_run.validate_outputs(case, paths, {}, {})
            self.assertTrue(accepted["valid"])
            self.assertTrue(accepted["qualityGatePassed"])
            self.assertEqual(accepted["issues"], [])
            self.assertEqual(
                accepted["telemetrySerializationParityPolicy"],
                matrix.TELEMETRY_SERIALIZATION_POLICY,
            )

            jsonl_path.write_text(
                '{"gpuHairMilliseconds":6.98461,"sourceFrameIndex":1}\n',
                encoding="utf-8",
            )
            rejected = matrix.final_run.validate_outputs(case, paths, {}, {})
            self.assertFalse(rejected["valid"])
            self.assertEqual(
                [issue["code"] for issue in rejected["issues"]],
                ["E_CSV_JSONL_PARITY"],
            )

    def test_visual_gate_uses_foreground_occupancy_bounds_not_darkness(self) -> None:
        matrix = load_matrix_module()
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            manifest = base / "frozen_asset_manifest.json"
            manifest.write_text("{}", encoding="utf-8")
            shaders = base / "shaders"
            shaders.mkdir()
            template_path = base / "template.json"
            template = {
                "run_defaults": {},
                "source": {
                    "asset_manifest": str(manifest),
                    "shader_root": str(shaders),
                },
            }
            materialized = matrix.materialize_case_template(
                template, template_path
            )
        self.assertEqual(materialized["minimum_visual_luminance_stddev"], 0.0)
        self.assertEqual(
            materialized["visual_validation_policy"],
            "foreground-occupancy-bounds-v2",
        )
        final_run_source = (
            ROOT / "tools/final_stage/hairgpu_final_run.py"
        ).read_text(encoding="utf-8")
        for marker in (
            '"nonBackgroundRatio"',
            '"foregroundBoundingBoxRatio"',
            '"E_VISUAL_EMPTY"',
            '"E_VISUAL_BOUNDS_TOO_SMALL"',
            '"E_VISUAL_BOUNDS_TOO_LARGE"',
        ):
            self.assertIn(marker, final_run_source)

    def test_publication_profiles_enforce_runtime_hardware_contract(self) -> None:
        matrix = load_matrix_module()
        contract = {
            "baselines": {"B0": {"method": "B0"}},
            "scenarios": {
                "S01": {
                    "runtimeScenarioName": "S01_Idle",
                    "timelinePath": "S01.json",
                }
            },
        }
        hardware = {
            "target": {
                "gpuNameRegex": "RTX 4070 Ti SUPER",
                "minimumDedicatedVramMiB": 15000,
                "maximumDedicatedVramMiB": 17000,
            },
            "executionContract": {
                "acPowerRequired": True,
                "backgroundCaptureDisabled": True,
            },
        }
        for profile in ("formal", "performance", "determinism", "soak"):
            case = matrix.build_case(
                {},
                variant="B0",
                scenario="S01",
                seed=1,
                repetition=0,
                profile=profile,
                contract=contract,
                hardware_profile_contract=hardware,
            )
            self.assertTrue(case["hardware_profile_strict"], profile)
            self.assertEqual(
                case["hardware_profile"],
                "measurement_rtx4070ti_super",
                profile,
            )
            self.assertEqual(case["hardware_profile_contract"], hardware, profile)
            self.assertIsNot(case["hardware_profile_contract"], hardware)

        for profile in ("smoke", "pilot"):
            case = matrix.build_case(
                {},
                variant="B0",
                scenario="S01",
                seed=1,
                repetition=0,
                profile=profile,
                contract=contract,
            )
            self.assertFalse(case["hardware_profile_strict"], profile)
            self.assertNotIn("hardware_profile_contract", case)

    def test_fail_fast_matrix_still_writes_partial_summary(self) -> None:
        matrix = load_matrix_module()
        results = [{
            "caseId": "N01_smoke_B0_S01_seed001_r01",
            "variant": "B0",
            "scenario": "S01",
            "seed": 1,
            "repetition": 0,
            "scenarioInputSha256": "a" * 64,
            "physicalScenarioInputSha256": "b" * 64,
            "ok": False,
            "error": "injected",
        }]
        payload = matrix.build_matrix_summary(
            profile="smoke",
            variants=["B0", "P"],
            scenarios=["S01"],
            seeds=[1],
            repetitions=1,
            schedule="counterbalanced",
            results=results,
            started=0.0,
            dry_run=False,
            aborted=True,
            abort_reason="injected",
        )
        self.assertFalse(payload["passed"])
        self.assertTrue(payload["aborted"])
        self.assertEqual(payload["runCount"], 1)
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            path = matrix.write_matrix_summary(output, payload)
            loaded = json.loads(path.read_text(encoding="utf-8"))
        self.assertFalse(loaded["passed"])
        self.assertTrue(loaded["aborted"])
        self.assertEqual(loaded["abortReason"], "injected")

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
