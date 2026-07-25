from __future__ import annotations

import json
import struct
import sys
import tempfile
import types
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[2] / "tools" / "final_stage"
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

# The repository contains the real Excel exporter. This fallback only makes the
# isolated eight-file patch testable before it is copied into the repository.
try:
    import hairgpu_final_excel  # noqa: F401
except ModuleNotFoundError:
    stub = types.ModuleType("hairgpu_final_excel")
    stub.export_workbook = lambda db_path, output_path: Path(output_path)
    sys.modules["hairgpu_final_excel"] = stub

import hairgpu_final_run as final_run
import hairgpu_runtime_suite as suite


class BaselineRuntimeSuiteTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.timeline = self.root / "S01_idle.json"
        self.timeline.write_text(
            json.dumps(
                {
                    "scenario": "S01_Idle",
                    "durationFrames": 7200,
                    "fixedTimeStepSeconds": 1.0 / 60.0,
                    "rootMotion": {"type": "constant"},
                    "wind": {"type": "constant"},
                    "colliders": [],
                    "events": [],
                }
            ),
            encoding="utf-8",
        )
        self.config = {
            "suite_name": "baseline-test",
            "experiment_name": "baseline-test",
            "source": {
                "viewer_exe": "viewer.exe",
                "asset_manifest": "asset.json",
                "schema": "schema.sql",
            },
            "preflight": {
                "required_method_codes": ["B0", "P"],
                "required_scenario_codes": ["S01_Idle"],
                "minimum_seed_count": 2,
            },
            "run_generation": {"mode": "cartesian_product"},
            "run_defaults": {
                "guide_count": 4000,
                "width": 1920,
                "height": 1080,
                "fixed_timestep": True,
                "fixed_timestep_seconds": 1.0 / 60.0,
                "warmup_frames": 600,
                "measurement_frames": 6000,
                "display_strand_count": 100000,
                "particles_per_guide": 128,
                "solver_iterations": 6,
            },
            "methods": [
                {"code": "B0_FullDynamic", "settings": {}},
                {"code": "P_IntegratedSegmentGroup", "settings": {}},
            ],
            "scenarios": [
                {
                    "code": "S01_Idle",
                    "timeline": str(self.timeline),
                    "assertions": {
                        "min_applied_events": 0,
                        "min_state_transitions": 1,
                    },
                }
            ],
            "seeds": [1103, 2207],
            "statistics": {
                "randomize_execution_order": False,
                "randomize_block_order": False,
            },
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_source_paths_and_legacy_guide_key_are_normalized(self) -> None:
        prepared = suite.prepare_suite_config(self.config)
        defaults = prepared["run_defaults"]
        self.assertEqual(defaults["viewer_exe"], "viewer.exe")
        self.assertEqual(defaults["asset_manifest"], "asset.json")
        self.assertEqual(prepared["schema"], "schema.sql")
        self.assertEqual(defaults["guide_strand_count"], 4000)
        self.assertNotIn("guide_count", defaults)

    def test_baseline_selection_is_exact_and_reproducible(self) -> None:
        prepared = suite.prepare_suite_config(
            self.config,
            only_methods=["B0"],
            only_scenarios=["S01_Idle"],
            only_seeds=[1103],
        )
        runs_a = suite.expand_run_matrix(prepared, self.root)
        runs_b = suite.expand_run_matrix(prepared, self.root)
        self.assertEqual(len(runs_a), 1)
        self.assertEqual(runs_a, runs_b)
        self.assertEqual(runs_a[0]["method"], "B0")
        self.assertEqual(runs_a[0]["seed"], 1103)
        self.assertEqual(prepared["preflight"]["required_method_codes"], ["B0"])
        self.assertEqual(
            prepared["scenario_matrix"][0]["required_methods"], ["B0"]
        )

    def test_b0_drops_novelty_only_assertions_and_allows_zero_events(self) -> None:
        prepared = suite.prepare_suite_config(
            self.config,
            only_methods=["B0"],
            only_seeds=[1103],
        )
        run = suite.expand_run_matrix(prepared, self.root)[0]
        self.assertEqual(run["assertions"], {"min_applied_events": 0})

    def test_negative_event_requirement_is_rejected_for_non_baseline(self) -> None:
        config = json.loads(json.dumps(self.config))
        config["scenarios"][0]["assertions"] = {"min_applied_events": -1}
        prepared = suite.prepare_suite_config(
            config,
            only_methods=["P"],
            only_seeds=[1103],
        )
        with self.assertRaises(ValueError):
            suite.expand_run_matrix(prepared, self.root)

    def test_idle_timeline_without_events_is_valid_when_zero_are_required(self) -> None:
        case = {
            "scenario": "S01_Idle",
            "warmup_frames": 600,
            "measurement_frames": 6000,
            "fixed_timestep_seconds": 1.0 / 60.0,
            "assertions": {"min_applied_events": 0},
        }
        self.assertEqual(final_run._validate_timeline(self.timeline, case), [])

    def test_discrete_event_contract_rejects_empty_timeline(self) -> None:
        case = {
            "scenario": "S01_Idle",
            "warmup_frames": 600,
            "measurement_frames": 6000,
            "fixed_timestep_seconds": 1.0 / 60.0,
            "assertions": {"min_applied_events": 1},
        }
        issues = final_run._validate_timeline(self.timeline, case)
        self.assertIn("E_TIMELINE_EVENTS", {item["code"] for item in issues})

    def test_b0_does_not_require_novelty_metrics(self) -> None:
        b0 = final_run._normalize_case(
            {"method": "B0", "scenario": "S13_RepairStorm"}
        )
        proposal = final_run._normalize_case(
            {"method": "P", "scenario": "S13_RepairStorm"}
        )
        self.assertNotIn("repairVerifiedSuccessRate", b0["required_quality_metrics"])
        self.assertIn("repairVerifiedSuccessRate", proposal["required_quality_metrics"])

    def test_quality_gate_failure_is_a_completed_evidence_case(self) -> None:
        telemetry = self.root / "telemetry"
        telemetry.mkdir()
        run = {
            "run_name": "B0__S01_Idle__seed1103__r00",
            "case_id": "B0__S01_Idle__seed1103__r00",
            "settings_hash": "a" * 64,
        }
        paths = suite.expected_telemetry_paths(self.root, run, telemetry)
        summary = {
            "validationStatus": "quality_gate_failed",
            "caseId": run["case_id"],
            "settingsSha256": run["settings_hash"],
            "canonicalResultSha256": "b" * 64,
        }
        validation = {
            "status": "quality_gate_failed",
            "isValid": True,
            "evidenceIntegrityPassed": True,
            "qualityGatePassed": False,
        }
        suite.write_json(paths["summary"], summary)
        suite.write_json(paths["settings"], {"ok": True})
        suite.write_json(paths["environment"], {"ok": True})
        suite.write_json(paths["validation"], validation)
        paths["frame_csv"].write_text("frame\n1\n", encoding="utf-8")
        paths["frame_jsonl"].write_text('{"frame":1}\n', encoding="utf-8")
        required = [
            paths["summary"],
            paths["frame_csv"],
            paths["frame_jsonl"],
            paths["settings"],
            paths["environment"],
            paths["validation"],
        ]
        manifest = {
            "canonicalResultSha256": summary["canonicalResultSha256"],
            "artifacts": [
                {
                    "path": path.name,
                    "sha256": suite.sha256_file(path),
                    "sizeBytes": path.stat().st_size,
                }
                for path in required
            ],
        }
        suite.write_json(paths["manifest"], manifest)
        self.assertTrue(suite.is_completed_case(self.root, run, telemetry))


class CompletedEnvironmentRegressionTests(unittest.TestCase):
    def test_special_runs_are_materialized(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            timeline = root / "timeline.json"
            timeline.write_text(json.dumps({
                "scenario": "S01_Idle", "durationFrames": 6600,
                "fixedTimeStepSeconds": 1.0 / 60.0,
                "events": [{"frame": 700, "type": "SetWind"}],
            }), encoding="utf-8")
            config = {
                "experiment_name": "special-test",
                "run_generation": {"mode": "cartesian_product"},
                "run_defaults": {
                    "guide_strand_count": 4000, "display_strand_count": 100000,
                    "particles_per_guide": 128, "solver_iterations": 6,
                    "warmup_frames": 600, "measurement_frames": 6000,
                    "fixed_timestep": True, "fixed_timestep_seconds": 1.0 / 60.0,
                    "width": 1920, "height": 1080,
                },
                "methods": [{"code": "P", "settings": {}}],
                "scenarios": [{"code": "S01_Idle", "timeline": str(timeline), "assertions": {"min_applied_events": 1}}],
                "seeds": [1103], "repetitions_per_seed": 1,
                "statistics": {"randomize_execution_order": False, "randomize_block_order": False},
                "determinism_replays": [{"id": "DET_TEST", "method": "P", "scenario": "S01_Idle", "seed": 1103, "repetitions": 3}],
                "long_runs": [{"id": "LONG_TEST", "method": "P", "scenario": "S01_Idle", "seed": 1103, "measurement_frames": 12000, "timeline_repeat": True}],
            }
            formal_runs = suite.expand_run_matrix(config, root)
            self.assertEqual(len(formal_runs), 1)
            self.assertTrue(all(run.get("special_kind") == "formal" for run in formal_runs))

            runs = suite.expand_all_runs(config, root)
            self.assertEqual(sum(run.get("special_run_kind") == "formal" for run in runs), 1)
            self.assertEqual(sum(run.get("special_run_kind") == "determinism" for run in runs), 3)
            self.assertEqual(sum(run.get("special_run_kind") == "long_run" for run in runs), 1)

    def test_timeline_repeat_is_materialized(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            timeline = root / "timeline.json"
            timeline.write_text(json.dumps({
                "scenario": "S01_Idle", "durationFrames": 100,
                "fixedTimeStepSeconds": 1.0 / 60.0,
                "events": [{"frame": 10, "type": "SetWind"}],
            }), encoding="utf-8")
            case = {
                "scenario_timeline": str(timeline), "timeline_repeat": True,
                "warmup_frames": 20, "measurement_frames": 250,
            }
            result = final_run._materialize_repeated_timeline(case, root, "repeat")
            materialized = json.loads(Path(result["scenario_timeline"]).read_text(encoding="utf-8"))
            self.assertEqual(materialized["durationFrames"], 270)
            self.assertEqual([event["frame"] for event in materialized["events"]], [10, 110, 210])

    def test_quality_gate_error_file_is_not_resumable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            telemetry = root / "telemetry"
            telemetry.mkdir()
            run = {"run_name": "case", "case_id": "case", "settings_hash": "a" * 64}
            paths = suite.expected_telemetry_paths(root, run, telemetry)
            paths["error"].write_text("{}", encoding="utf-8")
            self.assertFalse(suite.is_completed_case(root, run, telemetry))


    def test_formal_asset_identity_requires_observed_match(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            asset = Path(temporary) / "asset.hairbin"
            asset.write_bytes(b"formal-asset")
            case = final_run._normalize_case({
                "method": "P",
                "scenario": "S01_Idle",
                "formalAssetPath": str(asset),
                "formalAssetSha256": "a" * 64,
                "formalTopologySha256": "b" * 64,
                "generatorSettingsSha256": "c" * 64,
            })
            requested = dict(final_run.METHOD_PRESETS["P"])
            observed_flags = {
                key: requested[key] for key in final_run.METHOD_FLAG_KEYS
            }
            settings_hash = final_run.canonical_json_sha256(requested)
            settings = {
                "requested": requested,
                "applied": requested,
                "observedFlags": observed_flags,
                "observed": final_run._expected_runtime_settings(case),
                "requestedMethodSettingsSha256": settings_hash,
                "appliedMethodSettingsSha256": settings_hash,
                "observedMethodSettingsSha256": settings_hash,
                "methodFlagsMatched": True,
                "formalAssetLoaded": True,
                "formalAssetPath": str(asset),
                "observedAssetPath": str(asset.resolve()),
                "formalAssetSha256": "a" * 64,
                "formalTopologySha256": "b" * 64,
                "generatorSettingsSha256": "c" * 64,
            }
            issues, _ = final_run._compare_settings(settings, case)
            self.assertEqual(issues, [])
            settings["observedAssetPath"] = str(Path(temporary) / "wrong.hairbin")
            issues, _ = final_run._compare_settings(settings, case)
            self.assertIn(
                "E_FORMAL_ASSET_PATH_MISMATCH",
                {item["code"] for item in issues},
            )

    def test_frozen_asset_manifest_enforces_4000_24_128_6(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            asset = root / "asset.hairbin"
            topology = root / "topology.json"
            generator = root / "generator.json"
            asset.write_bytes(b"asset")
            topology.write_text('{"topology":1}', encoding="utf-8")
            generator.write_text('{"generator":1}', encoding="utf-8")
            manifest = {
                "manifestVersion": "novelty01-100k-v1.4",
                "assetFormatVersion": "novelty01-100k-v1.4",
                "freezeStatus": "FROZEN",
                "sourceAssetPath": str(asset),
                "sourceAssetSha256": final_run.sha256_file(asset),
                "sourceAssetSizeBytes": asset.stat().st_size,
                "generatedTopologyPath": str(topology),
                "generatedTopologySha256": final_run.sha256_file(topology),
                "generatorSettingsPath": str(generator),
                "generatorSettingsSha256": final_run.sha256_file(generator),
                "sourceCommitSha": final_run._git_commit_sha(
                    Path(__file__).resolve().parents[2]
                ) or "0" * 40,
                "sourceBranch": "Novelty",
                "generation": {
                    "displayStrandCount": 100000,
                    "primaryGuideStrandCount": 4000,
                    "followersPerGuide": 24,
                    "particlesPerGuide": 128,
                    "solverIterations": 6,
                },
            }
            manifest_path = root / "manifest.json"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            self.assertEqual(final_run._validate_asset_manifest(manifest_path), [])
            manifest["generation"]["particlesPerGuide"] = 24
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            issues = final_run._validate_asset_manifest(manifest_path)
            self.assertIn(
                "E_ASSET_GENERATION_CONTRACT",
                {item["code"] for item in issues},
            )




class ResourceStabilityGateTests(unittest.TestCase):
    def test_process_sampler_summarizes_handle_gdi_user_and_thread_growth(self) -> None:
        sampler = final_run.ProcessSampler(interval_seconds=1.0)
        sampler.pid = 123
        sampler.samples = [
            {
                "elapsedSeconds": 0.0,
                "privateBytesMiB": 500.0,
                "dxgiLocalUsageMiB": 1000.0,
                "handleCount": 100.0,
                "gdiObjectCount": 20.0,
                "userObjectCount": 10.0,
                "threadCount": 8.0,
            },
            {
                "elapsedSeconds": 30.0,
                "privateBytesMiB": 500.5,
                "dxgiLocalUsageMiB": 1000.5,
                "handleCount": 101.0,
                "gdiObjectCount": 20.5,
                "userObjectCount": 10.25,
                "threadCount": 8.0,
            },
            {
                "elapsedSeconds": 60.0,
                "privateBytesMiB": 501.0,
                "dxgiLocalUsageMiB": 1001.0,
                "handleCount": 102.0,
                "gdiObjectCount": 21.0,
                "userObjectCount": 10.5,
                "threadCount": 8.0,
            },
        ]
        summary = sampler.summary(expected_duration_seconds=3.0)
        self.assertEqual(summary["peakHandleCount"], 102.0)
        self.assertAlmostEqual(summary["handleCountSlopePerMin"], 2.0)
        self.assertAlmostEqual(summary["gdiObjectCountSlopePerMin"], 1.0)
        self.assertAlmostEqual(summary["userObjectCountSlopePerMin"], 0.5)
        self.assertAlmostEqual(summary["threadCountSlopePerMin"], 0.0)

    def test_long_run_gate_rejects_resource_growth(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            run = {
                "run_name": "LONG_RESOURCE",
                "special_kind": "long_run",
                "method": "B0",
                "scenario": "S03_Walk",
                "seed": 1103,
            }
            paths = final_run.output_paths(output, run["run_name"])
            suite.write_json(paths["validation"], {
                "processMemory": {
                    "sampleCoverage": 1.0,
                    "resourceSampleCoverage": 1.0,
                    "peakProcessPrivateMiB": 800.0,
                    "processPrivateSlopeMiBPerMin": 0.0,
                    "peakDxgiLocalUsageMiB": 2000.0,
                    "dxgiLocalUsageSlopeMiBPerMin": 0.0,
                    "peakHandleCount": 200.0,
                    "handleCountSlopePerMin": 3.0,
                    "peakGdiObjectCount": 50.0,
                    "gdiObjectCountSlopePerMin": 0.0,
                    "peakUserObjectCount": 20.0,
                    "userObjectCountSlopePerMin": 0.0,
                    "peakThreadCount": 12.0,
                    "threadCountSlopePerMin": 0.0,
                }
            })
            suite.write_json(paths["summary"], {"gpuTimingUnresolvedFrameCount": 0})
            paths["jsonl"].write_text(
                ''.join(json.dumps({"frameMilliseconds": 10.0}) + "\n" for _ in range(100)),
                encoding="utf-8",
            )
            issues = suite.evaluate_post_suite_gates(
                {
                    "long_run_gates": {
                        "minimum_process_memory_sample_coverage": 0.95,
                        "minimum_resource_sample_coverage": 0.95,
                        "max_peak_process_private_mib": 32768.0,
                        "max_process_private_slope_mib_per_min": 2.0,
                        "max_peak_dxgi_local_usage_mib": 6144.0,
                        "max_dxgi_local_usage_slope_mib_per_min": 1.0,
                        "max_peak_handle_count": 4096,
                        "max_handle_count_slope_per_min": 1.0,
                        "max_peak_gdi_object_count": 2048,
                        "max_gdi_object_count_slope_per_min": 0.25,
                        "max_peak_user_object_count": 2048,
                        "max_user_object_count_slope_per_min": 0.25,
                        "max_peak_thread_count": 256,
                        "max_thread_count_slope_per_min": 0.1,
                        "max_p99_degradation_ratio": 1.1,
                        "max_query_unresolved_slope": 0.0,
                    }
                },
                [run],
                output,
                "long-run",
            )
            self.assertIn("long_run_handle_slope", {item["code"] for item in issues})

class PrenoveltyCompletionGateTests(unittest.TestCase):
    def test_b0_uses_24_fps_gate(self) -> None:
        case = final_run._normalize_case({"method": "B0", "scenario": "S01_Idle"})
        self.assertEqual(case["evaluation_phase"], "prenovelty")
        self.assertEqual(case["minimum_average_fps"], 24.0)
        self.assertAlmostEqual(case["maximum_p95_frame_ms"], 41.6667, places=4)

    def test_proposal_keeps_60_fps_gate(self) -> None:
        case = final_run._normalize_case({"method": "P", "scenario": "S01_Idle"})
        self.assertEqual(case["evaluation_phase"], "proposal")
        self.assertEqual(case["minimum_average_fps"], 60.0)
        self.assertAlmostEqual(case["maximum_p95_frame_ms"], 16.6667, places=4)

    def test_prenovelty_smoke_matrix_is_explicit(self) -> None:
        self.assertEqual(suite.PRENOVELTY_SMOKE_METHODS, {"B0", "B6", "B8"})
        self.assertIn("S17_CloseCamera100k", suite.PRENOVELTY_SMOKE_SCENARIOS)



class P0VisualValidationTests(unittest.TestCase):
    @staticmethod
    def _write_bmp(path: Path, width: int, height: int, foreground: bool) -> None:
        row_stride = width * 4
        pixels = bytearray()
        for storage_y in range(height):
            y = height - 1 - storage_y
            for x in range(width):
                inside = (
                    foreground
                    and width // 4 <= x < width * 3 // 4
                    and height // 4 <= y < height * 3 // 4
                )
                r, g, b = ((190, 120, 55) if inside else (12, 18, 26))
                pixels.extend((b, g, r, 255))
        offset = 14 + 40
        file_size = offset + len(pixels)
        header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, offset)
        dib = struct.pack(
            "<IiiHHIIiiII",
            40, width, height, 1, 32, 0, len(pixels), 2835, 2835, 0, 0,
        )
        path.write_bytes(header + dib + pixels)

    def test_bmp_analysis_detects_visible_subject(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "visible.bmp"
            self._write_bmp(image, 64, 64, True)
            metrics = final_run.analyze_visual_bmp(image)
            self.assertEqual(metrics["width"], 64)
            self.assertGreater(metrics["nonBackgroundRatio"], 0.20)
            self.assertLess(metrics["nonBackgroundRatio"], 0.30)
            self.assertGreater(metrics["foregroundBoundingBoxRatio"], 0.20)
            self.assertEqual(len(metrics["averageHash256"]), 64)

    def test_blank_capture_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            paths = final_run.output_paths(output, "blank")
            self._write_bmp(paths["visual"], 64, 64, False)
            metrics, issues = final_run._validate_visual_capture({}, paths)
            self.assertEqual(metrics["nonBackgroundRatio"], 0.0)
            codes = {item["code"] for item in issues}
            self.assertIn("E_VISUAL_EMPTY", codes)
            self.assertIn("E_VISUAL_LOW_VARIANCE", codes)
            self.assertTrue(paths["visual_metrics"].is_file())

    def test_suite_visual_regression_uses_paired_baseline(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            runs = [
                {"run_name": "B0", "special_kind": "formal", "method": "B0", "scenario": "S01_Idle", "seed": 1103, "repetition_index": 0},
                {"run_name": "B6", "special_kind": "formal", "method": "B6", "scenario": "S01_Idle", "seed": 1103, "repetition_index": 0},
            ]
            baseline = {
                "averageHash256": "0" * 64,
                "nonBackgroundRatio": 0.40,
                "foregroundBoundingBoxRatio": 0.50,
            }
            compared = dict(baseline)
            for run, metrics in ((runs[0], baseline), (runs[1], compared)):
                final_run.write_json(
                    final_run.output_paths(output, run["run_name"])["visual_metrics"],
                    metrics,
                )
            issues = suite.evaluate_post_suite_gates(
                {"visual_regression": {"enabled": True, "baseline_method": "B0"}},
                runs,
                output,
                "formal",
            )
            self.assertEqual(issues, [])

    def test_suite_visual_regression_rejects_catastrophic_difference(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            runs = [
                {"run_name": "B0", "special_kind": "formal", "method": "B0", "scenario": "S01_Idle", "seed": 1103, "repetition_index": 0},
                {"run_name": "B6", "special_kind": "formal", "method": "B6", "scenario": "S01_Idle", "seed": 1103, "repetition_index": 0},
            ]
            final_run.write_json(final_run.output_paths(output, "B0")["visual_metrics"], {
                "averageHash256": "0" * 64,
                "nonBackgroundRatio": 0.40,
                "foregroundBoundingBoxRatio": 0.50,
            })
            final_run.write_json(final_run.output_paths(output, "B6")["visual_metrics"], {
                "averageHash256": "f" * 64,
                "nonBackgroundRatio": 0.02,
                "foregroundBoundingBoxRatio": 0.03,
            })
            issues = suite.evaluate_post_suite_gates(
                {"visual_regression": {"enabled": True, "baseline_method": "B0"}},
                runs,
                output,
                "formal",
            )
            codes = {item["code"] for item in issues}
            self.assertIn("visual_hash_regression", codes)
            self.assertIn("visual_coverage_regression", codes)
            self.assertIn("visual_bounds_regression", codes)

    def test_explicit_failed_visual_gate_is_rejected(self) -> None:
        self.assertFalse(final_run._summary_visual_validation_passed(
  {"visualValidationPassed": False}
        ))

    def test_legacy_summary_without_visual_field_remains_compatible(self) -> None:
        self.assertTrue(final_run._summary_visual_validation_passed({}))

    def test_suite_detects_visual_validation_issue(self) -> None:
        self.assertFalse(suite.visual_validation_passed({
  "issues": [{"code": "E_VISUAL_VALIDATION"}]
        }))
        self.assertTrue(suite.visual_validation_passed({"issues": []}))

if __name__ == "__main__":
    unittest.main()
