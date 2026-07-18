from __future__ import annotations

import json
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
                "particles_per_guide": 24,
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


if __name__ == "__main__":
    unittest.main()
