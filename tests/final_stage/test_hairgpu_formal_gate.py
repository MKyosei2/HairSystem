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

import hairgpu_final_run as final_run
import hairgpu_formal_gate as gate


class FormalGateTests(unittest.TestCase):
    def test_scenario_family(self) -> None:
        self.assertEqual(gate._scenario_family("S01_Idle"), "S01")
        self.assertEqual(gate._scenario_family("S18_FarCamera100k"), "S18")

    def test_canonical_hash_is_order_independent(self) -> None:
        self.assertEqual(
            gate.canonical_hash({"b": 2, "a": 1}),
            gate.canonical_hash({"a": 1, "b": 2}),
        )

    def test_percentile(self) -> None:
        self.assertEqual(gate.percentile([1.0, 2.0, 3.0], 50.0), 2.0)
        self.assertEqual(gate.percentile([5.0], 99.0), 5.0)
        self.assertIsNone(gate.percentile([], 99.0))

    def test_environment_alias_patch(self) -> None:
        def original(case, paths, repo_root, viewer_exe, asset_manifest):
            return {"ORIGINAL": "1"}

        builder = gate._patched_environment_builder(original)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            timeline = root / "timeline.json"
            manifest = root / "manifest.json"
            shader_root = root / "shaders"
            shader_root.mkdir()
            timeline.write_text("{}\n", encoding="utf-8")
            manifest.write_text("{}\n", encoding="utf-8")
            (shader_root / "a.hlsl").write_text("// shader\n", encoding="utf-8")
            case = {
                "case_id": "case",
                "width": 1920,
                "height": 1080,
                "guide_strand_count": 4000,
                "repetition_index": 2,
                "measurement_mode": "performance",
                "build_configuration": "Release",
                "platform": "x64",
                "hardware_profile": "low_gtx1660_reference",
                "method_settings": {"enableGpuResident": True},
                "scenario_timeline": str(timeline),
                "asset_manifest": str(manifest),
                "shader_root": str(shader_root),
            }
            paths = {"case": root / "case.json"}
            env = builder(case, paths, root, root / "viewer.exe", {})
            self.assertEqual(env["HAIRGPU_EXPERIMENT_CASE_ID"], "case")
            self.assertEqual(env["HAIRGPU_RENDER_WIDTH"], "1920")
            self.assertEqual(env["HAIRGPU_RENDER_HEIGHT"], "1080")
            self.assertEqual(env["HAIRGPU_GUIDE_COUNT"], "4000")
            self.assertEqual(env["HAIRGPU_REPEAT_INDEX"], "2")
            self.assertRegex(env["HAIRGPU_REQUESTED_METHOD_SETTINGS_SHA256"], r"^[0-9a-f]{64}$")
            self.assertEqual(
                env["HAIRGPU_REQUESTED_METHOD_SETTINGS_SHA256"],
                env["HAIRGPU_APPLIED_METHOD_SETTINGS_SHA256"],
            )

    def test_determinism_accepts_equal_replays(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            replay = {"id": "DET_TEST", "repetitions": 3}
            config = {"determinism_replays": [replay]}
            for index in range(3):
                run_name = f"DET_TEST_r{index:02d}"
                path = final_run.output_paths(output, run_name)["jsonl"]
                path.write_text(
                    json.dumps(
                        {
                            "stateSequenceHash": 11,
                            "compactionSequenceHash": 22,
                            "finalPositionHash": 33,
                            "determinismMismatchCount": 0,
                        }
                    )
                    + "\n",
                    encoding="utf-8",
                )
            self.assertEqual(gate.validate_determinism(config, output), [])

    def test_determinism_rejects_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            config = {"determinism_replays": [{"id": "DET_TEST", "repetitions": 2}]}
            for index, final_hash in enumerate((33, 44)):
                run_name = f"DET_TEST_r{index:02d}"
                path = final_run.output_paths(output, run_name)["jsonl"]
                path.write_text(
                    json.dumps(
                        {
                            "stateSequenceHash": 11,
                            "compactionSequenceHash": 22,
                            "finalPositionHash": final_hash,
                            "determinismMismatchCount": 0,
                        }
                    )
                    + "\n",
                    encoding="utf-8",
                )
            codes = {entry["code"] for entry in gate.validate_determinism(config, output)}
            self.assertIn("determinism_replay_mismatch", codes)


if __name__ == "__main__":
    unittest.main()
