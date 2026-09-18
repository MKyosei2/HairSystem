from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer_parts"


class RuntimeTruthAndAssetLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sample = (ROOT / "src" / "hairgpu_core" / "SampleAsset.cpp").read_text(
            encoding="utf-8"
        )
        cls.validation_header = (
            ROOT / "include" / "hairgpu" / "Validation.h"
        ).read_text(encoding="utf-8")
        cls.validation = (
            ROOT / "src" / "hairgpu_core" / "Validation.cpp"
        ).read_text(encoding="utf-8")
        cls.header = (
            ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer.h"
        ).read_text(encoding="utf-8")
        cls.startup = (PARTS / "01_window_device_and_ui_setup.inl").read_text(
            encoding="utf-8"
        )
        cls.run_loop = (
            PARTS / "02_resource_creation_and_shader_loading.inl"
        ).read_text(encoding="utf-8")
        cls.reset = (PARTS / "03_simulation_reset_and_frame_step.inl").read_text(
            encoding="utf-8"
        )
        cls.controls = (PARTS / "07_draw_path_and_gpu_timing.inl").read_text(
            encoding="utf-8"
        )
        cls.settings = (
            PARTS / "08_ui_state_sync_and_controls.inl"
        ).read_text(encoding="utf-8")
        cls.hair_sim = (
            ROOT / "apps" / "dx11_viewer" / "shaders" / "HairSim.hlsl"
        ).read_text(encoding="utf-8")

    def test_asset_builder_emits_contiguous_guide_prefix(self) -> None:
        guide_marker = "Runtime and HLSL contracts require a contiguous guide prefix"
        follower_marker = "Append followers only after every guide has been emitted"
        guide_pos = self.sample.index(guide_marker)
        follower_pos = self.sample.index(follower_marker)
        self.assertLess(guide_pos, follower_pos)
        guide_region = self.sample[guide_pos:follower_pos]
        follower_region = self.sample[follower_pos : follower_pos + 9000]
        self.assertIn("guideBindings.push_back(binding);", guide_region)
        self.assertIn("StrandFlags::Guide", guide_region)
        self.assertIn("guideBindings[guideIndex]", follower_region)
        self.assertIn("followerFollow.parentGuide = static_cast<int32_t>(guideIndex);", follower_region)

    def test_continuous_root_distribution_keeps_followers_near_parent_guides(self) -> None:
        self.assertNotIn("crownGuideQuota", self.sample)
        self.assertNotIn("transitionGuideQuota", self.sample)
        self.assertNotIn("frontHairlineGuideQuota", self.sample)
        self.assertIn(
            "const RootBinding binding = SampleAreaDistributedRootBinding(",
            self.sample,
        )
        self.assertIn("Lerp(0.0020f, 0.0065f, ring01)", self.sample)
        self.assertIn("Lerp(0.08f, 0.22f, ring01)", self.sample)
        self.assertIn("kVisualRootLiftMeters = 0.0045f", self.sample)
        self.assertIn(
            "const float rootInfluence = (1.0f - t) * (1.0f - t);",
            self.sample,
        )

    def test_hlsl_follower_contract_matches_suffix_layout(self) -> None:
        self.assertIn(
            "uint strandIndex = GuideStrandCount + followerIndex;",
            self.hair_sim,
        )
        self.assertIn("uint parentStart = parentGuide * ParticlesPerStrand;", self.hair_sim)

    def test_structural_validator_rejects_interleaved_assets(self) -> None:
        self.assertIn("invalidGuidePrefixCount", self.validation_header)
        self.assertIn("invalidFollowerSuffixCount", self.validation_header)
        self.assertIn("strandIndex < asset.guideStrandCount", self.validation)
        self.assertIn("invalidGuidePrefixCount", self.validation)
        self.assertIn("invalidFollowerSuffixCount", self.validation)

    def test_old_broken_asset_cache_cannot_be_reused(self) -> None:
        self.assertIn("sample_hybrid_cache_v6_layout_g", self.startup)
        self.assertIn('name.rfind(L"sample_hybrid_cache_v1"', self.startup)
        self.assertIn('name.rfind(L"sample_hybrid_cache_v2"', self.startup)
        self.assertIn('name.rfind(L"sample_hybrid_cache_v3"', self.startup)
        self.assertIn('name.rfind(L"sample_hybrid_cache_v4"', self.startup)
        self.assertIn('name.rfind(L"sample_hybrid_cache_v5"', self.startup)
        self.assertIn("cachedTopologyReport", self.startup)
        self.assertIn("formalTopologyReport", self.startup)
        self.assertIn("generatedTopologyReport", self.startup)
        self.assertIn("invalidGuidePrefixCount == 0u", self.startup)
        self.assertIn("invalidFollowerSuffixCount == 0u", self.startup)

    def test_long_stalls_are_not_replaced_with_sixty_fps(self) -> None:
        self.assertIn("const double rawFrameSeconds", self.run_loop)
        self.assertIn("const float wallFrameSeconds", self.run_loop)
        self.assertIn("std::clamp(wallFrameSeconds, 0.0f, 0.10f)", self.run_loop)
        self.assertNotIn("dt > 1.0f", self.run_loop)
        self.assertNotIn("cadenceWindowSeconds +=", self.run_loop)
        self.assertIn("m_lastFrameIntervalMilliseconds >= 100.0f", self.run_loop)

    def test_cpu_cannot_queue_unbounded_frames_ahead_of_gpu(self) -> None:
        self.assertIn("#include <dxgi1_2.h>", self.header)
        self.assertIn("IDXGIDevice1", self.run_loop)
        self.assertIn("SetMaximumFrameLatency(1u)", self.run_loop)

    def test_ui_reports_average_tail_and_stalls(self) -> None:
        self.assertIn("kRealtimeFrameHistoryCapacity = 600u", self.header)
        self.assertIn("Session FPS (all completed frames)", self.settings)
        self.assertIn("Rolling FPS (last 600 frames)", self.settings)
        self.assertIn("Instant FPS (0.5 s window)", self.settings)
        self.assertIn("Frame ms recent avg/p95/worst", self.settings)
        self.assertIn("Stalls >=100 ms recent/total", self.settings)
        self.assertIn("m_recentFrameIntervalsMilliseconds.fill(0.0f)", self.reset)

    def test_groom_mode_does_not_force_editability_color(self) -> None:
        self.assertIn("debugColorSelection >= 0", self.controls)
        self.assertNotIn(
            "m_debugColorMode = DebugColorMode::Editability;", self.controls
        )
        self.assertIn("settingsFormatVersion=2", self.settings)
        self.assertIn("settingsFormatVersion < 2u", self.settings)


if __name__ == "__main__":
    unittest.main()
