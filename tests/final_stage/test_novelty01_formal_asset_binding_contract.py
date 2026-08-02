from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class FormalAssetBindingContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.runner = load_module(
            "hairgpu_final_run_direct_binding",
            ROOT / "tools/final_stage/hairgpu_final_run.py",
        )

    def _case(self) -> dict[str, object]:
        return {
            "case_id": "binding_contract",
            "run_name": "binding_contract",
            "variant": "P",
            "method": "P",
            "scenario": "S01_Idle",
            "seed": 1103,
            "repetition_index": 0,
            "measurement_mode": "quality",
            "width": 1920,
            "height": 1080,
            "fixed_timestep": True,
            "fixed_timestep_seconds": 1.0 / 60.0,
            "vsync": False,
            "present_sync_interval": 0,
            "warmup_frames": 120,
            "measurement_frames": 600,
            "guide_strand_count": 4000,
            "followers_per_guide": 24,
            "display_strand_count": 100000,
            "particles_per_guide": 128,
            "solver_iterations": 6,
            "gpu_timestamp_delay_frames": 6,
            "method_settings": {},
            "build_configuration": "Release",
            "platform": "x64",
            "hardware_profile": "",
            "scenario_timeline": "",
            "asset_manifest": "",
            "shader_root": "",
            "settings_hash": "d" * 64,
        }

    def test_direct_runner_emits_all_four_formal_asset_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            asset = (base / "asset.hairbin").resolve()
            asset.write_bytes(b"asset")
            viewer = (base / "viewer.exe").resolve()
            viewer.write_bytes(b"viewer")
            paths = self.runner.output_paths(base, "binding_contract")
            manifest = {
                "sourceAssetSha256": "a" * 64,
                "generatedTopologySha256": "b" * 64,
                "generatorSettingsSha256": "c" * 64,
                "_resolvedMembers": {
                    "sourceAssetPath": str(asset),
                },
            }

            environment = self.runner._build_environment(
                self._case(),
                paths,
                base,
                viewer,
                manifest,
            )

            self.assertEqual(
                environment["HAIRGPU_FORMAL_ASSET_PATH"],
                str(asset),
            )
            self.assertEqual(
                environment["HAIRGPU_FORMAL_ASSET_SHA256"],
                "a" * 64,
            )
            self.assertEqual(
                environment["HAIRGPU_FORMAL_TOPOLOGY_SHA256"],
                "b" * 64,
            )
            self.assertEqual(
                environment["HAIRGPU_GENERATOR_SETTINGS_SHA256"],
                "c" * 64,
            )

    def test_direct_runner_rejects_missing_resolved_asset(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            viewer = base / "viewer.exe"
            viewer.write_bytes(b"viewer")
            paths = self.runner.output_paths(base, "binding_contract")
            with self.assertRaises(self.runner.FormalRunError):
                self.runner._build_environment(
                    self._case(),
                    paths,
                    base,
                    viewer,
                    {
                        "sourceAssetSha256": "a" * 64,
                        "generatedTopologySha256": "b" * 64,
                        "generatorSettingsSha256": "c" * 64,
                        "_resolvedMembers": {},
                    },
                )

    def test_direct_runner_rejects_invalid_identity_hash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            asset = (base / "asset.hairbin").resolve()
            asset.write_bytes(b"asset")
            viewer = base / "viewer.exe"
            viewer.write_bytes(b"viewer")
            paths = self.runner.output_paths(base, "binding_contract")
            with self.assertRaises(self.runner.FormalRunError):
                self.runner._build_environment(
                    self._case(),
                    paths,
                    base,
                    viewer,
                    {
                        "sourceAssetSha256": "invalid",
                        "generatedTopologySha256": "b" * 64,
                        "generatorSettingsSha256": "c" * 64,
                        "_resolvedMembers": {
                            "sourceAssetPath": str(asset),
                        },
                    },
                )

    def test_runtime_patcher_artifacts_are_removed(self) -> None:
        self.assertFalse(
            (
                ROOT
                / "tools/final_stage/apply_formal_asset_binding_patch.py"
            ).exists()
        )
        self.assertFalse(
            (
                ROOT
                / "patches/hairgpu_final_run_formal_asset_binding.patch"
            ).exists()
        )


if __name__ == "__main__":
    unittest.main()
