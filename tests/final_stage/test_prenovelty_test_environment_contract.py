from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "final_stage"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import hairgpu_runtime_suite as suite


class PrenoveltyTestEnvironmentContractTests(unittest.TestCase):
    def test_suite_detects_current_branch_without_hardcoded_name(self) -> None:
        payload = json.loads(
            (ROOT / "docs/sample_assets/prenovelty_runtime_suite.json").read_text(
                encoding="utf-8"
            )
        )
        source = payload["source"]
        self.assertNotIn("required_branch", source)
        contract = source["branch_contract"]
        self.assertEqual(contract["mode"], "detect_current")
        self.assertTrue(contract["require_attached_head"])
        self.assertTrue(contract["record_commit"])

    def test_expand_run_matrix_does_not_modify_timeline_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            timeline = root / "idle.json"
            timeline.write_text(
                json.dumps(
                    {
                        "scenario": "S01_Idle",
                        "durationFrames": 6600,
                        "fixedTimeStepSeconds": 1.0 / 60.0,
                        "events": [],
                        "assertions": {"exactAppliedEventCount": 0},
                    }
                ),
                encoding="utf-8",
            )
            before = timeline.read_bytes()
            config = {
                "run_defaults": {
                    "guide_strand_count": 4000,
                    "followers_per_guide": 24,
                    "display_strand_count": 100000,
                    "particles_per_guide": 128,
                    "solver_iterations": 6,
                    "warmup_frames": 600,
                    "measurement_frames": 6000,
                    "fixed_timestep": True,
                    "fixed_timestep_seconds": 1.0 / 60.0,
                    "width": 1920,
                    "height": 1080,
                },
                "source": {},
                "methods": [{"code": "B0", "settings": {}}],
                "scenarios": [
                    {
                        "code": "S01_Idle",
                        "timeline": str(timeline),
                        "assertions": {"min_applied_events": 0},
                    }
                ],
                "seeds": [1103],
                "repetitions_per_seed": 1,
            }
            runs = suite.expand_run_matrix(config, root, ROOT)
            self.assertTrue(runs)
            self.assertEqual(before, timeline.read_bytes())

    def test_idle_zero_event_guard_is_present(self) -> None:
        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl"
        ).read_text(encoding="utf-8")
        self.assertIn("allowsZeroMeasuredEvents", source)
        self.assertIn("ExperimentScenarioId::S01_Idle", source)

    def test_startup20_uses_real_viewer_lifecycle_test(self) -> None:
        batch = (ROOT / "tools/final_stage/Run_Final_Runtime_Suite.bat").read_text(
            encoding="utf-8"
        )
        self.assertIn("hairgpu_startup_self_test.py", batch)
        self.assertIn("mode is required", batch)


if __name__ == "__main__":
    unittest.main()
