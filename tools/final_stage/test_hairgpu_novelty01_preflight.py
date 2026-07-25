from __future__ import annotations

import csv
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("hairgpu_novelty01_preflight.py")
SPEC = importlib.util.spec_from_file_location("hairgpu_novelty01_preflight", MODULE_PATH)
assert SPEC and SPEC.loader
preflight = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(preflight)


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


class Novelty01PreflightTests(unittest.TestCase):
    def make_matrix_config(self, root: Path) -> dict:
        scenario_dir = root / "scenarios"
        scenario_dir.mkdir(parents=True, exist_ok=True)
        scenarios = []
        for code in sorted(preflight.DEFAULT_REQUIRED_SCENARIOS):
            timeline = scenario_dir / f"{code}.json"
            write_json(timeline, {"scenario": code, "durationFrames": 10})
            scenarios.append({"code": code, "category": "test", "timeline": str(timeline)})

        asset_manifest = root / "asset_manifest.json"
        write_json(
            asset_manifest,
            {
                "sourceAssetPath": str(root / "asset.bin"),
                "sourceAssetSha256": "a" * 64,
            },
        )

        methods = [{"code": code, "settings": {}} for code in sorted(preflight.DEFAULT_REQUIRED_METHODS)]
        return {
            "suite_name": "test",
            "experiment_name": "test",
            "telemetry_dir": str(root / "telemetry"),
            "baseline_method": "external_state_array",
            "preflight": {
                "minimum_seed_count": 5,
                "minimum_warmup_frames": 2,
                "minimum_measurement_frames": 5,
                "required_resolution": [1920, 1080],
                "required_platform": "x64",
                "require_release_build": True,
                "require_vsync_disabled": True,
                "require_fixed_timestep": True,
                "fixed_timestep_seconds": 1.0 / 60.0,
                "fixed_timestep_tolerance_seconds": 1e-5,
                "require_hashes": [],
                "require_hardware_fields": [],
                "required_telemetry_fields": [],
            },
            "run_defaults": {
                "asset": "asset",
                "asset_manifest": str(asset_manifest),
                "build_configuration": "Release",
                "platform": "x64",
                "width": 1920,
                "height": 1080,
                "vsync": False,
                "fixed_timestep": True,
                "fixed_timestep_seconds": 1.0 / 60.0,
                "warmup_frames": 2,
                "measurement_frames": 5,
                "display_strand_count": 100000,
                "guide_strand_count": 3000,
                "particles_per_guide": 20,
                "solver_iterations": 5,
            },
            "seeds": [1, 11, 21, 31, 41],
            "methods": methods,
            "scenarios": scenarios,
            "runs": [],
        }

    def write_frame_csv(self, path: Path, values: list[float]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["frameMilliseconds"])
            writer.writeheader()
            for value in values:
                writer.writerow({"frameMilliseconds": value})

    def test_matrix_expands_to_900_unique_runs(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            config = self.make_matrix_config(root)
            issues = preflight.validate_config(config, root)
            self.assertFalse([issue for issue in issues if issue["severity"] == "error"])
            runs = preflight.expand_runs(config, root)
            self.assertEqual(900, len(runs))
            self.assertEqual(900, len({run["run_name"] for run in runs}))
            matrix_issues = preflight.validate_expanded_runs(config, runs)
            self.assertFalse([issue for issue in matrix_issues if issue["severity"] == "error"])

    def test_percentile_interpolation(self) -> None:
        values = [1.0, 2.0, 3.0, 4.0, 5.0]
        self.assertAlmostEqual(3.0, preflight.percentile(values, 0.5))
        self.assertAlmostEqual(4.8, preflight.percentile(values, 0.95))

    def test_pending_measurement_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            config = self.make_matrix_config(root)
            run = preflight.expand_runs(config, root)[0]
            summary = Path(run["summary_json"])
            settings = Path(run["settings_json"])
            frames = Path(run["frame_csv"])
            write_json(summary, {"status": "pending", "real_fps_available": False})
            write_json(settings, {"buildConfiguration": "Release", "platform": "x64"})
            self.write_frame_csv(frames, [10, 11, 12, 13, 14])
            issues: list[dict] = []
            preflight.validate_run_telemetry(run, config, root, issues)
            codes = {issue["code"] for issue in issues}
            self.assertIn("NON_REAL_STATUS", codes)
            self.assertIn("REAL_MEASUREMENT_FALSE", codes)

    def test_real_measurement_tail_statistics(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            config = self.make_matrix_config(root)
            run = preflight.expand_runs(config, root)[0]
            summary = Path(run["summary_json"])
            settings = Path(run["settings_json"])
            frames = Path(run["frame_csv"])
            write_json(summary, {"status": "measured", "realMeasurement": True})
            write_json(settings, {"buildConfiguration": "Release", "platform": "x64"})
            self.write_frame_csv(frames, [10.0, 12.0, 14.0, 18.0, 30.0])
            issues: list[dict] = []
            row = preflight.validate_run_telemetry(run, config, root, issues)
            self.assertFalse([issue for issue in issues if issue["severity"] == "error"])
            self.assertTrue(row["telemetry_valid"])
            self.assertEqual(5, row["measured_frame_count"])
            self.assertAlmostEqual(0.4, row["frame_budget_overrun_rate"])
            self.assertGreater(row["p99_frame_ms"], row["p95_frame_ms"])


if __name__ == "__main__":
    unittest.main()
