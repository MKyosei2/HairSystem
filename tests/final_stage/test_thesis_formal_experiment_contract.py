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
        "caseId", "scenario", "seed", "sourceFrameIndex",
        "frameMilliseconds", "gpuHairMilliseconds", "totalStrandCount",
        "availabilityMask", "metricAvailabilityMask", "syntheticMetricCount",
        "sameFrameReadbackCount", "nanStateCount", "infiniteStateCount",
        "fatalFailure", "fatalFailureCode",
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

    def test_csv_jsonl_serialization_is_canonical_in_viewer(self) -> None:
        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl"
        ).read_text(encoding="utf-8")

        csv_start = source.index("bool GpuHairViewer::ExportPerformanceCsv")
        json_start = source.index("bool GpuHairViewer::ExportPerformanceJsonl")
        settings_start = source.index("bool GpuHairViewer::ExportExperimentSettingsJson")
        csv_block = source[csv_start:json_start]
        json_block = source[json_start:settings_start]

        canonical = "out << std::fixed << std::setprecision(6);"
        self.assertIn(canonical, csv_block)
        self.assertIn(canonical, json_block)
        self.assertIn(
            "Formal CSV and JSONL are two serializations of the same PerformanceSample.",
            csv_block,
        )
        self.assertLess(csv_block.index(canonical), csv_block.index("sampleIndex,elapsedSeconds"))
        self.assertLess(json_block.index(canonical), json_block.index("for (const PerformanceSample& sample"))

        # Regression captured by the real 70.11 Smoke failure.  The canonical
        # exporter must emit the JSONL-side six-decimal representation to CSV too.
        self.assertEqual(format(7.192064, ".6f"), "7.192064")
        self.assertNotEqual("7.19206", format(7.192064, ".6f"))

    def test_matrix_never_monkeypatches_output_integrity_validation(self) -> None:
        source = (
            ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py"
        ).read_text(encoding="utf-8")

        # The Viewer now owns canonical serialization.  Matrix execution must use
        # hairgpu_final_run.validate_outputs directly and must not reinterpret or
        # clear E_CSV_JSONL_PARITY after validation.
        forbidden = (
            "CSV_DEFAULTFLOAT_SIGNIFICANT_DIGITS",
            "TELEMETRY_SERIALIZATION_POLICY",
            "_telemetry_scalar_serialization_equivalent",
            "_telemetry_files_are_serialization_equivalent",
            "_install_publication_output_integrity_compatibility",
            "_hairgpu_thesis_original_validate_outputs",
        )
        for marker in forbidden:
            self.assertNotIn(marker, source)

        self.assertNotIn("final_run.validate_outputs =", source)
        self.assertIn("final_run.execute_case", source)

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
        self.assertEqual(
            materialized["minimum_visual_non_background_ratio"],
            matrix.FORMAL_VISUAL_MIN_NON_BACKGROUND_RATIO,
        )
        self.assertEqual(
            materialized["minimum_visual_bounding_box_ratio"],
            matrix.FORMAL_VISUAL_MIN_BOUNDING_BOX_RATIO,
        )
        self.assertEqual(
            matrix.FORMAL_VISUAL_MIN_NON_BACKGROUND_RATIO,
            0.001,
        )
        self.assertEqual(
            matrix.FORMAL_VISUAL_MIN_BOUNDING_BOX_RATIO,
            0.001,
        )
        self.assertGreater(
            matrix.FORMAL_VISUAL_MIN_NON_BACKGROUND_RATIO,
            0.0,
        )
        self.assertGreater(
            matrix.FORMAL_VISUAL_MIN_BOUNDING_BOX_RATIO,
            0.0,
        )
        # Regression from the first real 70.12 Smoke capture.  It contained a
        # coherent 100k-strand foreground (332 sampled foreground points), so the
        # presence-only gate must not classify it as empty.
        self.assertGreaterEqual(
            0.004471380471380472,
            matrix.FORMAL_VISUAL_MIN_NON_BACKGROUND_RATIO,
        )
        self.assertGreaterEqual(
            0.007116126543209877,
            matrix.FORMAL_VISUAL_MIN_BOUNDING_BOX_RATIO,
        )
        self.assertEqual(materialized["minimum_visual_luminance_stddev"], 0.0)
        self.assertEqual(
            materialized["visual_validation_policy"],
            "foreground-occupancy-bounds-v3-100k-framing",
        )
        # S18 deliberately pushes the camera beyond the normal 2.5 m framing.
        # A single generic 0.5%/1.0% minimum was therefore incompatible with the
        # experiment matrix by construction.
        s18_events = matrix.build_seeded_events(
            scenario="S18",
            seed=1,
            warmup_frames=120,
            measurement_frames=360,
            duration_frames=7200,
        )
        far_camera = [
            event
            for event in s18_events
            if event.get("type") == "set_camera_distance"
        ]
        self.assertEqual(len(far_camera), 1)
        self.assertGreater(float(far_camera[0]["value"]), 2.5)
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

    def test_performance_profile_preserves_uncontaminated_measurement_mode(self) -> None:
        matrix = load_matrix_module()
        settings = matrix.PROFILE_SETTINGS["performance"]
        self.assertEqual(settings["warmup_frames"], 600)
        self.assertEqual(settings["measurement_frames"], 6000)
        self.assertEqual(settings["measurement_mode"], "performance")
        self.assertFalse(settings["ground_truth"])

        # The fix must not hide the crash by making Performance use the Quality
        # execution path.  Quality retains its independent evidence semantics.
        self.assertEqual(
            matrix.PROFILE_SETTINGS["formal"]["measurement_mode"],
            "quality",
        )

    def test_performance_gpu_barrier_is_post_measurement_only(self) -> None:
        aggregate = (
            ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
        ).read_text(encoding="utf-8")
        legacy = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl"
        ).read_text(encoding="utf-8")

        for marker in (
            "HAIRGPU_FORMAL_POST_MEASUREMENT_BARRIER_V1",
            "HairGpuWaitForGpuIdleAfterMeasurement",
            "D3D11_QUERY_EVENT",
            "D3D11_ASYNC_GETDATA_DONOTFLUSH",
            "QueueCounterReadback(m_globalFrameIndex);",
            "runExitCode = 94;",
            "Post-measurement GPU completion barrier failed or timed out.",
        ):
            self.assertIn(marker, aggregate)

        # Performance remains free of the Quality-mode per-frame counter copy.
        self.assertIn(
            "m_experimentCase.measurementMode != MeasurementMode::Performance",
            legacy,
        )
        barrier = aggregate.index("#define DrainGpuTimingAndCounterReadbacks()")
        run_include = aggregate.index(
            '#include "GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"'
        )
        self.assertLess(barrier, run_include)
        self.assertNotIn(
            "m_experimentCase.measurementMode = MeasurementMode::Quality",
            aggregate,
        )

    def test_access_violation_exit_code_is_explained_fail_closed(self) -> None:
        path = ROOT / "tools/final_stage/hairgpu_final_run.py"
        spec = importlib.util.spec_from_file_location(
            "thesis_final_run_exit_v39", path
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        for value in (3221225477, -1073741819):
            diagnostic = module.process_exit_diagnostic(value)
            self.assertEqual(diagnostic["hex32"], "0xC0000005")
            self.assertTrue(diagnostic["isWindowsNtStatus"])
            self.assertEqual(
                diagnostic["windowsStatusName"],
                "STATUS_ACCESS_VIOLATION",
            )

        source = path.read_text(encoding="utf-8")
        self.assertIn('"E_VIEWER_WINDOWS_EXCEPTION"', source)
        self.assertIn('"viewer_exit_diagnostic.json"', source)

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

    def test_process_sampler_coverage_uses_actual_probe_attempts(self) -> None:
        path = ROOT / "tools/final_stage/hairgpu_final_run.py"
        spec = importlib.util.spec_from_file_location(
            "thesis_final_run_sampler_regression_v1",
            path,
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        assert spec is not None and spec.loader is not None

        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        sampler = module.ProcessSampler(interval_seconds=3.0)
        sampler.pid = 123
        sampler.samples = [
            {
                "elapsedSeconds": float(index * 3),
                "privateBytesMiB": 100.0 + index,
                "workingSetMiB": 50.0 + index,
                "dxgiLocalUsageMiB": 500.0 + index,
                "dxgiNonLocalUsageMiB": 10.0,
                "handleCount": 100.0,
                "threadCount": 50.0,
                "gpuTemperatureC": 55.0,
                "gpuGraphicsClockMHz": 2500.0,
            }
            for index in range(5)
        ]

        summary = sampler.summary(expected_duration_seconds=35.0)
        self.assertEqual(summary["expectedSampleCount"], 11)
        self.assertEqual(summary["sampleCount"], 5)
        self.assertEqual(summary["coverageDenominatorSampleCount"], 5)
        self.assertEqual(summary["sampleCoverage"], 1.0)
        self.assertEqual(summary["gpuSampleCoverage"], 1.0)
        self.assertEqual(summary["resourceSampleCoverage"], 1.0)

        sampler.samples[-1] = dict(sampler.samples[-1])
        sampler.samples[-1].pop("privateBytesMiB")
        degraded = sampler.summary(expected_duration_seconds=35.0)
        self.assertAlmostEqual(degraded["sampleCoverage"], 0.8)
        self.assertLess(degraded["sampleCoverage"], 0.95)

        template = json.loads(
            (
                ROOT / "docs/sample_assets/final_runtime_suite_template.json"
            ).read_text(encoding="utf-8")
        )
        self.assertEqual(
            template["run_defaults"][
                "minimum_process_memory_sample_coverage"
            ],
            0.95,
        )

    def test_strict_hardware_profile_owns_duplicated_runtime_limits(self) -> None:
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
                "minimumGpuTimingCoverage": 0.99,
                "maximumProcessPrivateMemoryMiB": 32768.0,
                "targetProcessPrivateMemoryMiB": 1024.0,
                "maximumDxgiLocalUsageMiB": 16384.0,
                "minimumProcessMemorySampleCoverage": 0.95,
            },
        }
        case = matrix.build_case(
            {}, variant="B0", scenario="S01", seed=1, repetition=0,
            profile="formal", contract=contract,
            hardware_profile_contract=hardware,
        )
        self.assertTrue(case["hardware_profile_strict"])
        self.assertEqual(case["minimum_gpu_timing_coverage"], 0.99)
        self.assertEqual(case["maximum_process_private_memory_mib"], 32768.0)
        self.assertEqual(case["target_process_private_memory_mib"], 1024.0)
        self.assertEqual(case["maximum_dxgi_local_usage_mib"], 16384.0)
        self.assertEqual(case["minimum_process_memory_sample_coverage"], 0.95)

    def test_gpu_stability_uses_measurement_window_directional_drawdown(self) -> None:
        path = ROOT / "tools/final_stage/hairgpu_final_run.py"
        spec = importlib.util.spec_from_file_location(
            "thesis_final_run_gpu_stability_v1", path
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        sampler = module.ProcessSampler(interval_seconds=1.0)
        sampler.samples = [
            {"elapsedSeconds": 0.0, "gpuTemperatureC": 50.0, "gpuGraphicsClockMHz": 480.0},
            {"elapsedSeconds": 1.0, "gpuTemperatureC": 62.0, "gpuGraphicsClockMHz": 2700.0},
            {"elapsedSeconds": 2.0, "gpuTemperatureC": 64.0, "gpuGraphicsClockMHz": 2730.0},
            {"elapsedSeconds": 3.0, "gpuTemperatureC": 66.0, "gpuGraphicsClockMHz": 2715.0},
            {"elapsedSeconds": 4.0, "gpuTemperatureC": 68.0, "gpuGraphicsClockMHz": 2700.0},
        ]
        summary = sampler.summary(warmup_frames=1, measurement_frames=4)
        self.assertEqual(summary["gpuTemperatureMaxC"], 68.0)
        self.assertEqual(summary["gpuTemperatureSpreadC"], 6.0)
        self.assertLess(summary["gpuClockDropRatio"], 0.15)
        self.assertGreater(summary["gpuClockLifetimeRangeRatio"], 0.80)
        self.assertEqual(
            summary["gpuHealthWindowPolicy"],
            "actual-probe-sequence-warmup-fraction-loaded-v2",
        )

        sampler.samples[-1] = dict(sampler.samples[-1])
        sampler.samples[-1]["gpuGraphicsClockMHz"] = 1900.0
        degraded = sampler.summary(warmup_frames=1, measurement_frames=4)
        self.assertGreater(degraded["gpuClockDropRatio"], 0.15)

    def test_sampler_stop_never_adds_post_exit_gpu_sample(self) -> None:
        path = ROOT / "tools/final_stage/hairgpu_final_run.py"
        spec = importlib.util.spec_from_file_location(
            "thesis_final_run_stop_regression_v1", path
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        sampler = module.ProcessSampler(interval_seconds=1.0)
        sampler.pid = 999
        sampler.samples = [{"elapsedSeconds": 1.0}]
        calls = []
        sampler._sample_once = lambda: calls.append("sample")
        sampler.stop()
        self.assertEqual(calls, [])
        self.assertEqual(len(sampler.samples), 1)

    def test_strict_gpu_thresholds_remain_unchanged(self) -> None:
        profiles = json.loads(
            (ROOT / "benchmarks/novelty01/hardware_profiles.json").read_text(encoding="utf-8")
        )["profiles"]
        profile = profiles["measurement_rtx4070ti_super"]
        execution = profile["executionContract"]
        self.assertEqual(execution["maximumGpuTemperatureCelsius"], 88.0)
        self.assertEqual(execution["maximumGpuTemperatureSpreadCelsius"], 12.0)
        self.assertEqual(execution["maximumGpuClockDropRatio"], 0.15)
        self.assertEqual(execution["minimumGpuClockSamples"], 3)

    def test_soak_timeout_is_operational_not_a_quality_weakening(self) -> None:
        matrix = load_matrix_module()
        self.assertEqual(matrix.PROFILE_SETTINGS["soak"]["measurement_frames"], 108000)
        self.assertGreaterEqual(matrix.PROFILE_SETTINGS["soak"]["timeout_seconds"], 7200)
        final_run = (ROOT / "tools/final_stage/hairgpu_final_run.py").read_text(encoding="utf-8")
        self.assertIn('"gpuTemperatureMaxC"', final_run)
        self.assertIn('"gpuTemperatureSpreadC"', final_run)
        self.assertIn('"gpuClockDropRatio"', final_run)

    def test_final_gate_requires_exact_profile_run_counts(self) -> None:
        source = (ROOT / "tools/final_stage/hairgpu_thesis_final_gate.py").read_text(encoding="utf-8")
        compile(source, "hairgpu_thesis_final_gate.py", "exec")
        for marker in (
            "EXPECTED_MATRIX_RUN_COUNTS",
            '"smoke": 10',
            '"pilot": 56',
            '"formal": 540',
            '"performance": 540',
            '"determinism": 42',
            '"soak": 10',
            '"E_MATRIX_RUN_COUNT"',
            '"E_MATRIX_METHOD_RUN_COUNT"',
            '"E_MATRIX_PROFILE_SUMMARY_COUNT"',
        ):
            self.assertIn(marker, source)

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


    def test_soak_is_prequalified_and_gpu_stability_uses_loaded_samples(self) -> None:
        runner = (ROOT / "tools/final_stage/Run_Thesis_B0_P_Formal.ps1").read_text(encoding="utf-8")
        for required in (
            "function Invoke-SoakThermalConditioning",
            "SOAK THERMAL CONDITIONING (NOT THESIS EVIDENCE)",
            '"soak_full_prequalification"',
            '"pre_soak_conditioning"',
            "maximumEndpointSpreadC = 3.0",
            "thresholdsChanged = $false",
        ):
            self.assertIn(required, runner)
        self.assertLess(
            runner.index('"soak_full_prequalification"'),
            runner.index('Add-CompletedStage -Session $session -Name "Asset"'),
        )
        official_soak = runner.index('"Soak" {')
        self.assertLess(
            runner.index('"pre_soak_conditioning"', official_soak),
            runner.index('-Output (Join-Path $session.Directory "soak\\B0_vs_P")', official_soak),
        )

        final_path = ROOT / "tools/final_stage/hairgpu_final_run.py"
        source = final_path.read_text(encoding="utf-8")
        for required in (
            'VALIDATION_VERSION = "01-safe-v3.10-soak-loaded-gpu-stability"',
            'gpuHealthActivityFilterApplied',
            'utilization>=1pct-or-power>=10pct-limit-v1',
            'actual-probe-sequence-warmup-fraction-loaded-v2',
            'max-directional-drawdown-loaded-measurement-window-v2',
        ):
            self.assertIn(required, source)

        spec = importlib.util.spec_from_file_location("hairgpu_final_run_loaded_gpu_test", final_path)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        sampler = module.ProcessSampler(interval_seconds=1.0)
        sampler.samples = [
            {"elapsedSeconds": 0.0, "gpuTemperatureC": 65.0, "gpuGraphicsClockMHz": 2700.0, "gpuUtilizationPercent": 2.0, "gpuPowerDrawW": 31.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 1.0, "gpuTemperatureC": 66.0, "gpuGraphicsClockMHz": 2685.0, "gpuUtilizationPercent": 2.0, "gpuPowerDrawW": 30.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 2.0, "gpuTemperatureC": 52.0, "gpuGraphicsClockMHz": 210.0, "gpuUtilizationPercent": 0.0, "gpuPowerDrawW": 18.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 3.0, "gpuTemperatureC": 67.0, "gpuGraphicsClockMHz": 2670.0, "gpuUtilizationPercent": 2.0, "gpuPowerDrawW": 30.0, "gpuPowerLimitW": 285.0},
        ]
        summary = sampler.summary(warmup_frames=0, measurement_frames=4)
        self.assertTrue(summary["gpuHealthActivityFilterApplied"])
        self.assertEqual(summary["gpuHealthMeasurementSampleCount"], 4)
        self.assertEqual(summary["gpuHealthLoadedSampleCount"], 3)
        self.assertEqual(summary["gpuHealthSampleCount"], 3)
        self.assertLessEqual(summary["gpuTemperatureSpreadC"], 2.0)
        self.assertLess(summary["gpuClockDropRatio"], 0.15)

        throttled = module.ProcessSampler(interval_seconds=1.0)
        throttled.samples = [
            {"elapsedSeconds": 0.0, "gpuTemperatureC": 70.0, "gpuGraphicsClockMHz": 2700.0, "gpuUtilizationPercent": 3.0, "gpuPowerDrawW": 34.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 1.0, "gpuTemperatureC": 72.0, "gpuGraphicsClockMHz": 2715.0, "gpuUtilizationPercent": 3.0, "gpuPowerDrawW": 34.0, "gpuPowerLimitW": 285.0},
            {"elapsedSeconds": 2.0, "gpuTemperatureC": 73.0, "gpuGraphicsClockMHz": 1800.0, "gpuUtilizationPercent": 3.0, "gpuPowerDrawW": 33.0, "gpuPowerLimitW": 285.0},
        ]
        throttled_summary = throttled.summary(warmup_frames=0, measurement_frames=3)
        self.assertEqual(throttled_summary["gpuHealthLoadedSampleCount"], 3)
        self.assertGreater(throttled_summary["gpuClockDropRatio"], 0.15)

        hardware = json.loads((ROOT / "benchmarks/novelty01/hardware_profiles.json").read_text(encoding="utf-8"))
        contract = hardware["profiles"]["measurement_rtx4070ti_super"]["executionContract"]
        self.assertEqual(float(contract["maximumGpuTemperatureCelsius"]), 88.0)
        self.assertEqual(float(contract["maximumGpuTemperatureSpreadCelsius"]), 12.0)
        self.assertEqual(float(contract["maximumGpuClockDropRatio"]), 0.15)


if __name__ == "__main__":
    unittest.main()
