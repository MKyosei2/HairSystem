from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class RuntimeVisualStabilityContractTests(unittest.TestCase):
    def test_interactive_startup_uses_stable_profile(self) -> None:
        source = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl").read_text(encoding="utf-8")
        self.assertIn('m_activePresetName = L"Stable Benchmark Wig"', source)
        self.assertIn('m_enableSelfCollision = false', source)
        self.assertIn('m_enableActiveSetRuntime = false', source)

    def test_asset_cache_is_versioned_and_semantically_validated(self) -> None:
        source = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl").read_text(encoding="utf-8")
        self.assertIn("sample_hybrid_cache_v4_stable_roots", source)
        self.assertIn("ValidateHairAsset(cachedAsset)", source)
        self.assertIn("ValidateBenchmarkWigShape(cachedAsset)", source)

    def test_interactive_loop_is_paced_and_breaks_stale_velocity(self) -> None:
        source = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl").read_text(encoding="utf-8")
        self.assertIn("MsgWaitForMultipleObjectsEx", source)
        self.assertIn("SetMaximumFrameLatency(1u)", source)
        self.assertIn("kTemporalDiscontinuitySeconds", source)
        self.assertIn("m_previousPositions[m_currentBufferIndex].buffer.Get()", source)

    def test_follow_reconstruction_has_no_tangent_half_turn_dependency(self) -> None:
        source = (ROOT / "apps/dx11_viewer/shaders/HairSim.hlsl").read_text(encoding="utf-8")
        follow = source[source.index("void FollowCS"):]
        self.assertNotIn("RotateVectorFromTo", follow)
        self.assertIn("guideCurrentLocal - guideRestLocal", follow)
        self.assertIn("previousFollowerRoot", follow)
        self.assertIn("maximumFollowerRadius", follow)


if __name__ == "__main__":
    unittest.main()
