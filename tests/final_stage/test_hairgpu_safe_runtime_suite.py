from __future__ import annotations

import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[2] / "tools" / "final_stage" / "hairgpu_safe_runtime_suite.py"
SPEC = importlib.util.spec_from_file_location("hairgpu_safe_runtime_suite", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
suite = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(suite)


class SafeRuntimeSuiteTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.config_dir = self.root / "docs" / "sample_assets"
        self.config_dir.mkdir(parents=True)
        self.timeline_dir = self.root / "benchmarks" / "novelty01" / "scenarios"
        self.timeline_dir.mkdir(parents=True)
        self.asset = self.root / "asset.bin"
        self.asset.write_bytes(b"frozen-asset")
        self.generator = self.root / "generator.json"
        self.generator.write_text('{"seed":1}\n', encoding="utf-8")
        self.timeline = self.timeline_dir / "S01_idle.json"
        self.timeline.write_text(
            json.dumps(
                {
                    "scenario": "S01_Idle",
                    "version": 1,
                    "durationFrames": 7000,
                    "fixedTimeStepSeconds": 1.0 / 60.0,
                    "seedDriven": True,
                    "rootMotion": {"type": "constant"},
                    "wind": {"type": "constant"},
                    "colliders": [],
                    "events": [],
                }
            ),
            encoding="utf-8",
        )
        self.manifest = self.config_dir / "asset_manifest.json"
        self.manifest.write_text(
            json.dumps(
                {
                    "freezeStatus": "FROZEN",
                    "sourceAssetPath": str(self.asset),
                    "sourceAssetSha256": suite.sha256_file(self.asset),
                    "generatedTopologySha256": "a" * 64,
                    "generatorSettingsSha256": suite.sha256_file(self.generator),
                    "generation": {
                        "displayStrandCount": 100000,
                        "requireStableTopologyHash": True,
                    },
                }
            ),
            encoding="utf-8",
        )
        self.hardware = self.root / "benchmarks" / "novelty01" / "hardware_profiles.json"
        self.hardware.write_text(
            json.dumps({"profiles": {"test": {"role": "test"}}}),
            encoding="utf-8",
        )
        self.config_path = self.config_dir / "suite.json"
        self.config = self.make_config()
        self.config_path.write_text(json.dumps(self.config), encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def make_config(self) -> dict:
        return {
            "suite_version": "test-v1",
            "suite_name": "test",
            "experiment_name": "test",
            "source": {
                "repository": "owner/repo",
                "branch": "Novelty",
                "commit": "1" * 40,
                "commit_mode": "declared",
            },
            "baseline_method": "baseline",
            "hardware_profiles": str(self.hardware),
            "preflight": {
                "strict": True,
                "require_release_build": True,
                "required_platform": "x64",
                "require_vsync_disabled": True,
                "require_fixed_timestep": True,
                "fixed_timestep_seconds": 1.0 / 60.0,
                "fixed_timestep_tolerance_seconds": 1.0e-5,
                "minimum_warmup_frames": 600,
                "minimum_measurement_frames": 6000,
                "minimum_seed_count": 2,
                "required_resolution": [1920, 1080],
                "frame_budget_milliseconds": 16.67,
                "required_method_codes": ["baseline", "proposal"],
                "require_frozen_asset": True,
                "verify_source_asset_file": True,
                "require_seed_driven_timeline": True,
                "require_source_commit_match": True,
                "require_real_measurement": True,
                "require_identity_match": True,
                "require_method_setting_acknowledgement": True,
                "require_no_fatal_runtime_failure": True,
                "max_invalid_frame_fraction": 0.0,
                "require_hashes": [
                    "commitSha",
                    "executableSha256",
                    "shaderBundleSha256",
                    "assetSha256",
                    "settingsSha256",
                    "scenarioTimelineSha256",
                    "assetManifestSha256",
                    "appliedMethodSettingsSha256",
                ],
                "require_hardware_fields": [
                    "cpuName",
                    "gpuName",
                    "gpuDriver",
                    "operatingSystem",
                    "hardwareProfile",
                ],
                "required_telemetry_fields": [],
            },
            "statistics": {
                "randomize_execution_order": True,
                "execution_order_seed": 123,
                "minimum_matched_seed_count": 2,
                "confidence_level": 0.95,
                "bootstrap_iterations": 100,
            },
            "run_defaults": {
                "asset": "asset",
                "asset_manifest": str(self.manifest),
                "build_configuration": "Release",
                "platform": "x64",
                "hardware_profile": "test",
                "width": 1920,
                "height": 1080,
                "vsync": False,
                "fixed_timestep": True,
                "fixed_timestep_seconds": 1.0 / 60.0,
                "warmup_frames": 600,
                "measurement_frames": 6000,
                "display_strand_count": 100000,
            },
            "seeds": [1, 2],
            "methods": [
                {"code": "baseline", "settings": {"enableFeature": False}},
                {"code": "proposal", "settings": {"enableFeature": True}},
            ],
            "scenarios": [
                {
                    "code": "S01_Idle",
                    "category": "normal",
                    "timeline": str(self.timeline),
                }
            ],
            "determinism_replays": [
                {
                    "id": "D01",
                    "scenario": "S01_Idle",
                    "method": "proposal",
                    "seed": 1,
                    "repetitions": 2,
                }
            ],
            "runs": [],
        }

    def test_digest_format_is_exact(self) -> None:
        self.assertTrue(suite.is_sha256("a" * 64))
        self.assertFalse(suite.is_sha256("a" * 63))
        self.assertFalse(suite.is_sha256("g" * 64))
        self.assertTrue(suite.is_commit_sha("b" * 40))
        self.assertFalse(suite.is_commit_sha("b" * 64))

    def test_matrix_randomization_is_reproducible_and_complete(self) -> None:
        expanded_a, runs_a = suite.expand_matrix(self.config, self.config_path)
        expanded_b, runs_b = suite.expand_matrix(self.config, self.config_path)
        self.assertEqual([run["case_id"] for run in runs_a], [run["case_id"] for run in runs_b])
        self.assertEqual(len(runs_a), 6)  # 1 scenario * 2 methods * 2 seeds + 2 deterministic repeats
        self.assertEqual(len({run["case_id"] for run in runs_a}), len(runs_a))
        self.assertTrue(all(suite.is_sha256(run["settings_hash"]) for run in runs_a))
        self.assertEqual(expanded_a["source"]["commit"], "1" * 40)

    def test_static_contract_rejects_unfrozen_asset(self) -> None:
        payload = json.loads(self.manifest.read_text(encoding="utf-8"))
        payload["freezeStatus"] = "UNFROZEN_REJECT_FOR_PAPER_EVIDENCE"
        self.manifest.write_text(json.dumps(payload), encoding="utf-8")
        expanded, runs = suite.expand_matrix(self.config, self.config_path)
        issues = suite.validate_static_contract(expanded, self.config_path, runs)
        self.assertIn("unfrozen_asset", {issue["code"] for issue in issues})

    def test_static_contract_rejects_timeline_identity_mismatch(self) -> None:
        payload = json.loads(self.timeline.read_text(encoding="utf-8"))
        payload["scenario"] = "WRONG"
        self.timeline.write_text(json.dumps(payload), encoding="utf-8")
        expanded, runs = suite.expand_matrix(self.config, self.config_path)
        issues = suite.validate_static_contract(expanded, self.config_path, runs)
        self.assertIn("timeline_identity_mismatch", {issue["code"] for issue in issues})

    def make_valid_evidence(self, run: dict, frame_ms: float = 10.0) -> dict:
        method_settings = run["method_settings"]
        runtime = {
            "status": "complete",
            "realMeasurement": True,
            "buildConfiguration": "Release",
            "platform": "x64",
            "vsyncEnabled": False,
            "fixedTimeStepEnabled": True,
            "fixedTimeStepSeconds": 1.0 / 60.0,
            "renderWidth": 1920,
            "renderHeight": 1080,
            "displayStrandCount": 100000,
            "caseId": run["case_id"],
            "appliedMethodCode": run["method"],
            "scenario": run["scenario"],
            "seed": run["seed"],
            "commitSha": "1" * 40,
            "executableSha256": "2" * 64,
            "shaderBundleSha256": "3" * 64,
            "assetSha256": suite.sha256_file(self.asset),
            "settingsSha256": run["settings_hash"],
            "scenarioTimelineSha256": run["scenario_timeline_sha256"],
            "assetManifestSha256": run["asset_manifest_sha256"],
            "appliedMethodSettingsSha256": run["method_settings_sha256"],
            "assetFrozen": True,
            "hardwareProfile": "test",
            "cpuName": "CPU",
            "gpuName": "GPU",
            "gpuDriver": "Driver",
            "operatingSystem": "Windows",
            "fatalRuntimeFailureCount": 0,
            "canonicalResultSha256": "4" * 64,
            **method_settings,
        }
        derived = {
            "avg_frame_ms": frame_ms,
            "p50_frame_ms": frame_ms,
            "p95_frame_ms": frame_ms,
            "p99_frame_ms": frame_ms,
            "p999_frame_ms": frame_ms,
            "worst_frame_ms": frame_ms,
            "sixty_fps_ratio": 1.0,
            "frame_budget_overrun_rate": 0.0,
            "invalid_frame_sample_rate": 0.0,
        }
        return {
            "summary_exists": True,
            "frame_exists": True,
            "settings_exists": True,
            "frame_row_count": 6000,
            "invalid_frame_count": 0,
            "frame_headers": ["frameMilliseconds"],
            "frame_ms": [frame_ms] * 6000,
            "combined": runtime,
            "runtime_combined": runtime,
            "case_snapshot": {},
            "derived": derived,
            "metrics": {
                "false_static_rate": 0.0,
                "repair_success_rate": 1.0,
                "collision_miss_rate": 0.0,
                "state_oscillation_rate": 0.0,
            },
            "canonical_result_sha256": "4" * 64,
        }

    def test_evidence_rejects_fixed_timestep_and_commit_mismatch(self) -> None:
        expanded, runs = suite.expand_matrix(self.config, self.config_path)
        run = next(run for run in runs if run["run_role"] == "primary")
        evidence = self.make_valid_evidence(run)
        evidence["runtime_combined"]["fixedTimeStepSeconds"] = 1.0 / 30.0
        evidence["runtime_combined"]["commitSha"] = "9" * 40
        manifest = json.loads(self.manifest.read_text(encoding="utf-8"))
        issues = suite.validate_run_evidence(expanded, run, evidence, manifest)
        codes = {issue["code"] for issue in issues}
        self.assertIn("fixed_timestep_mismatch", codes)
        self.assertIn("source_commit_mismatch", codes)

    def test_missing_baseline_pair_is_an_error(self) -> None:
        expanded, runs = suite.expand_matrix(self.config, self.config_path)
        primary = [run for run in runs if run["run_role"] == "primary"]
        evidence_rows = []
        for run in primary:
            if run["method"] == "baseline" and run["seed"] == 2:
                continue
            evidence = self.make_valid_evidence(run, 9.0 if run["method"] == "proposal" else 10.0)
            evidence.update(
                {
                    "valid": True,
                    "run_role": "primary",
                    "method": run["method"],
                    "scenario": run["scenario"],
                    "seed": run["seed"],
                    "asset": run["asset"],
                    "hardware_profile": run["hardware_profile"],
                }
            )
            evidence_rows.append(evidence)
        _, _, issues = suite.build_matched_pairs(expanded, evidence_rows)
        self.assertIn("missing_matched_baseline_pair", {issue["code"] for issue in issues})

    def test_determinism_hash_mismatch_is_an_error(self) -> None:
        evidences = [
            {
                "valid": True,
                "replay_group_id": "D01",
                "canonical_result_sha256": "a" * 64,
            },
            {
                "valid": True,
                "replay_group_id": "D01",
                "canonical_result_sha256": "b" * 64,
            },
        ]
        rows, issues = suite.validate_determinism(self.config, evidences)
        self.assertFalse(rows[0]["passed"])
        self.assertIn("determinism_replay_failed", {issue["code"] for issue in issues})


if __name__ == "__main__":
    unittest.main()
