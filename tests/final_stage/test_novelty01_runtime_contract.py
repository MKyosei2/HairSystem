from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class Novelty01RuntimeContractTests(unittest.TestCase):
    def test_formal_asset_environment_uses_resolved_bundle_members(self) -> None:
        matrix = load_module(
            "n01_matrix_runtime_contract",
            ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py",
        )
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            manifest_path = base / "frozen_asset_manifest.json"
            asset = base / "asset.hairbin"
            topology = base / "topology.bin"
            settings = base / "settings.json"
            for path in (asset, topology, settings):
                path.write_bytes(b"x")
            manifest_path.write_text("{}", encoding="utf-8")
            manifest = {
                "sourceAssetSha256": "a" * 64,
                "generatedTopologySha256": "b" * 64,
                "generatorSettingsSha256": "c" * 64,
                "_resolvedMembers": {
                    "sourceAssetPath": str(asset),
                    "generatedTopologyPath": str(topology),
                    "generatorSettingsPath": str(settings),
                },
            }
            case = {
                "asset_manifest": str(manifest_path),
                "allowed_asset_source_branches": ["main", "Novelty01"],
            }
            with mock.patch.object(
                matrix.final_run,
                "validate_asset_manifest",
                return_value=(manifest, []),
            ):
                environment, observed = (
                    matrix.resolve_frozen_asset_environment(
                        case,
                        ROOT,
                    )
                )
            self.assertIs(observed, manifest)
            self.assertEqual(
                environment["HAIRGPU_FORMAL_ASSET_PATH"],
                str(asset.resolve()),
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

    def test_readiness_fails_closed_on_sgapt_invariants(self) -> None:
        readiness = (
            ROOT
            / "tools/final_stage/hairgpu_novelty01_readiness_smoke.py"
        ).read_text(encoding="utf-8")
        for counter_index in (
            35, 36, 37, 38, 47, 48,
            49, 58, 59, 60, 62, 63,
        ):
            self.assertIn(f"{counter_index}:", readiness)
        for marker in (
            "payloadActiveMax",
            "payloadOverflowMax",
            "renderRegeneratedVerticesMax",
            "groundTruthSamplesMax",
            "fullCpuGroupUploadCount",
            "sameFrameReadbackCount",
        ):
            self.assertIn(marker, readiness)

    def test_formal_telemetry_output_is_configured_before_shutdown(self) -> None:
        consumer = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts"
            / "18_novelty01_render_dirty_range_consumer.inl"
        ).read_text(encoding="utf-8")
        for marker in (
            "ConfigureNovelty01FormalTelemetryOutput();",
            "HAIRGPU_N01_TELEMETRY_DIR",
            "HAIRGPU_TELEMETRY_DIR",
            "HAIRGPU_TELEMETRY_RUN_NAME",
        ):
            self.assertIn(marker, consumer)

    def test_render_cache_is_gpu_resident_and_has_no_cpu_readback(self) -> None:
        consumer = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts"
            / "18_novelty01_render_dirty_range_consumer.inl"
        ).read_text(encoding="utf-8")
        for marker in (
            "D3D11_USAGE_STAGING",
            "D3D11_MAP_READ",
            "ReadBackCurrentParticlePositions",
            "Map(cache->renderPositions",
        ):
            self.assertNotIn(marker, consumer)
        for marker in (
            "CreateStructuredBuffer",
            "CSSetShaderResources",
            "CSSetUnorderedAccessViews",
            "Dispatch(",
        ):
            self.assertIn(marker, consumer)

    def test_readiness_runner_is_bounded_and_main_only(self) -> None:
        source = (
            ROOT
            / "tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1"
        ).read_text(encoding="utf-8")
        self.assertIn('if ($Branch -ne "main")', source)
        self.assertIn("--measurement-frames", source)
        self.assertIn('"600"', source)
        self.assertIn("--timeout-seconds", source)
        self.assertIn("NOVELTY01_READINESS_RESULT.json", source)
        self.assertNotIn("git push", source.lower())
        self.assertNotIn("git commit", source.lower())


if __name__ == "__main__":
    unittest.main()
