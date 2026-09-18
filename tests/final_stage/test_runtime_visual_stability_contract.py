from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer_parts"
SHADERS = ROOT / "apps" / "dx11_viewer" / "shaders"


class RuntimeVisualStabilityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(
            encoding="utf-8"
        )
        cls.startup = (PARTS / "01_window_device_and_ui_setup.inl").read_text(
            encoding="utf-8"
        )
        cls.run_loop = (PARTS / "02_resource_creation_and_shader_loading.inl").read_text(
            encoding="utf-8"
        )
        cls.sync = (PARTS / "04_constant_buffers_and_active_set_sync.inl").read_text(
            encoding="utf-8"
        )
        cls.draw = (PARTS / "05_active_set_scoring_and_runtime_update.inl").read_text(
            encoding="utf-8"
        )
        cls.title = (PARTS / "06_render_mesh_and_visibility_recovery.inl").read_text(
            encoding="utf-8"
        )
        cls.quick_edit = (PARTS / "09_settings_load_save_and_export.inl").read_text(
            encoding="utf-8"
        )
        cls.overlay = (PARTS / "14_scalp_overlay_and_window_messages.inl").read_text(
            encoding="utf-8"
        )
        cls.sim_shader = (SHADERS / "HairSim.hlsl").read_text(encoding="utf-8")
        cls.strand_shader = (SHADERS / "HairStrandInstanced.hlsl").read_text(
            encoding="utf-8"
        )
        cls.sample = (ROOT / "src/hairgpu_core/SampleAsset.cpp").read_text(
            encoding="utf-8"
        )
        cls.validation = (ROOT / "src/hairgpu_core/Validation.cpp").read_text(
            encoding="utf-8"
        )

    def test_interactive_startup_uses_current_stable_profile(self) -> None:
        for token in (
            'm_activePresetName = L"Stable Benchmark Wig"',
            "m_renderMode = RenderMode::StrandRibbon",
            "m_workspaceMode = WorkspaceMode::Sim",
            "m_interactionMode = InteractionMode::View",
            "m_selectedStrandIndex = -1",
            "m_debugColorMode = DebugColorMode::GuideFollower",
            "m_cpuReadbackMode = CpuReadbackMode::Off",
            "m_enableSelfCollision = false",
            "m_enableGridSelfCollision = false",
            "m_showScalpMesh = true",
        ):
            self.assertIn(token, self.startup)
        self.assertNotIn("m_renderMode = RenderMode::ParticleBillboard;", self.startup)

    def test_asset_cache_is_current_and_semantically_validated(self) -> None:
        self.assertIn("sample_hybrid_cache_v6_layout_g", self.startup)
        self.assertIn("ValidateHairAsset(cachedAsset)", self.startup)
        self.assertIn("cachedTopologyReport.valid", self.startup)
        self.assertIn("invalidGuidePrefixCount == 0u", self.startup)
        self.assertIn("invalidFollowerSuffixCount == 0u", self.startup)
        for legacy in ("sample_hybrid_cache_v1", "sample_hybrid_cache_v2", "sample_hybrid_cache_v3", "sample_hybrid_cache_v4", "sample_hybrid_cache_v5"):
            self.assertIn(legacy, self.startup)
        self.assertIn("std::filesystem::remove(entry.path()", self.startup)

    def test_interactive_loop_preserves_real_frame_truth(self) -> None:
        self.assertIn("SetMaximumFrameLatency(1u)", self.run_loop)
        self.assertIn("interactiveCadenceWarmupFrames", self.run_loop)
        self.assertIn("m_lastTick = cadenceWindowStart", self.run_loop)
        self.assertIn(
            "m_sessionWallSeconds += static_cast<double>(wallFrameSeconds);",
            self.run_loop,
        )
        self.assertIn("const double rawFrameSeconds", self.run_loop)
        self.assertIn("const float wallFrameSeconds", self.run_loop)
        self.assertNotIn("dt > 1.0f", self.run_loop)

    def test_follow_reconstruction_is_finite_and_bounded(self) -> None:
        follow = self.sim_shader[self.sim_shader.index("void FollowCS"):]
        self.assertNotIn("RotateVectorFromTo", follow)
        for token in (
            "guideCurrentLocal - guideRestLocal",
            "previousFollowerRoot",
            "maximumFollowerRadius",
            "secondaryVelocity",
            "maximumSecondaryLag",
            "targetResponse",
            "ResolveFollowerExternalCollisions",
            "authoredRadial",
            "OutputPreviousPositions[strandPointIndex] = float4(currentPosition",
        ):
            self.assertIn(token, follow)
        self.assertIn("length(target - followerRoot) > maximumFollowerRadius", follow)
        self.assertIn("target = translatedRest", follow)

    def test_instanced_hair_uses_current_weighted_oit_revision(self) -> None:
        self.assertTrue(
            self.strand_shader.lstrip().startswith(
                "// HAIRGPU_STRAND_RIBBON_VISIBILITY_V19_WEIGHTED_OIT_ROOT_COVERAGE"
            )
        )
        self.assertIn("HAIRGPU_STRAND_RIBBON_SHADER_ABI_1", self.strand_shader)
        self.assertIn("StructuredBuffer<uint> RenderTiers", self.strand_shader)
        self.assertIn("StructuredBuffer<uint> EditCategories", self.strand_shader)
        for token in (
            "ribbonSide",
            "distanceScaledHalfWidth",
            "rootMask",
            "rootAlphaBoost",
            "output.alpha",
        ):
            self.assertIn(token, self.strand_shader)
        self.assertNotIn("clip(", self.strand_shader)
        self.assertNotIn("discard;", self.strand_shader)

    def test_guide_follower_palette_is_high_contrast_and_formal_is_neutral(self) -> None:
        self.assertIn("float3(1.00f, 0.48f, 0.10f)", self.strand_shader)
        self.assertIn("float3(0.10f, 0.82f, 0.88f)", self.strand_shader)
        self.assertIn(
            "m_debugColorMode = DebugColorMode::GuideFollower;",
            self.startup,
        )
        experiment = self.startup[self.startup.index("if (m_experimentCase.enabled)") :]
        self.assertIn(
            "m_debugColorMode = DebugColorMode::Natural;",
            experiment,
        )

    def test_normal_preview_keeps_all_followers_visible(self) -> None:
        self.assertIn(
            "constants.renderFollowerStride = (fastEditPreview && heavyInteraction) ? 2u : 1u;",
            self.sync,
        )
        self.assertIn("constants.renderGuideOnlyAboveTier = 0xFFFFFFFFu;", self.sync)
        self.assertIn("constants.renderStrandAlpha = 1.0f;", self.sync)
        experiment = self.sync[self.sync.index("if (IsExperimentModeEnabled())") :]
        self.assertIn("constants.renderFollowerStride = 1u;", experiment)
        self.assertIn("constants.renderStrandAlpha = 1.0f;", experiment)

    def test_render_and_edit_classification_buffers_are_separate(self) -> None:
        self.assertIn("m_editVisualizationCategoryBuffer", self.header)
        self.assertIn("std::vector<uint32_t> renderTiers", self.sync)
        self.assertIn("std::vector<uint32_t> editCategories", self.sync)
        self.assertIn("BuildEditVisualizationCategories(editCategories)", self.sync)
        self.assertNotIn("BuildEditVisualizationCategories(renderTiers)", self.sync)

    def test_hidden_scalp_depth_prepass_is_not_reintroduced(self) -> None:
        self.assertIn("Do not render a hidden scalp depth pre-pass", self.draw)
        self.assertNotIn("m_depthOnlyBlendState", self.header)
        self.assertNotIn("m_depthOnlyBlendState", self.draw)
        self.assertNotIn("CreateBlendState(&depthOnlyBlend", self.run_loop)
        self.assertIn("m_showScalpMesh", self.draw)

    def test_weighted_oit_resources_are_recreated_with_swapchain(self) -> None:
        for token in (
            "m_hairOitAccumTexture",
            "m_hairOitAccumRtv",
            "m_hairOitAccumSrv",
            "m_hairOitRevealTexture",
            "m_hairOitRevealRtv",
            "m_hairOitRevealSrv",
        ):
            self.assertIn(token, self.header)
            self.assertIn(token, self.title)
        self.assertIn("CreateHairOitResources(m_width, m_height)", self.title)
        self.assertIn("DXGI_FORMAT_R16G16B16A16_FLOAT", self.title)
        self.assertIn("DXGI_FORMAT_R16_FLOAT", self.title)

    def test_asset_builder_preserves_crown_and_hairline_coverage(self) -> None:
        for token in (
            "EvaluateAllowedRootMinV",
            "return 0.0f;",
            "SampleAreaDistributedRootBinding",
            "for (uint32_t attempt = 0; attempt < 16u; ++attempt)",
            "EvaluateRootCoverageWeight",
            "const float topFlowMask",
            "Vec3 crownFlow",
            "const float supportMix = 0.0f",
        ):
            self.assertIn(token, self.sample)

    def test_generated_asset_uses_contiguous_guide_then_follower_layout(self) -> None:
        guide_marker = "Runtime and HLSL contracts require a contiguous guide prefix"
        follower_marker = "Append followers only after every guide has been emitted"
        self.assertIn(guide_marker, self.sample)
        self.assertIn(follower_marker, self.sample)
        self.assertLess(self.sample.index(guide_marker), self.sample.index(follower_marker))
        self.assertIn("invalidGuidePrefixCount", self.validation)
        self.assertIn("invalidFollowerSuffixCount", self.validation)
        self.assertIn("uint strandIndex = GuideStrandCount + followerIndex", self.sim_shader)

    def test_saved_selection_requires_explicit_groom_edit_mode(self) -> None:
        marker = self.quick_edit.index("if (!layer.strandSelection.empty()")
        selection = self.quick_edit[marker : marker + 500]
        self.assertIn("m_workspaceMode == WorkspaceMode::Groom", selection)
        self.assertIn("m_interactionMode == InteractionMode::Edit", selection)
        self.assertIn("m_selectedStrandIndex >= 0", self.overlay)
        self.assertIn("m_workspaceMode == WorkspaceMode::Groom", self.overlay)
        self.assertIn("m_interactionMode == InteractionMode::Edit", self.overlay)

    def test_title_reports_current_presentation_runtime_and_real_metrics(self) -> None:
        self.assertIn('[HAIRGPU V11 PRESENTATION-STABLE]', self.title)
        for token in (
            "sessionFPS=",
            "rollingFPS=",
            "onePctLow=",
            "instantFPS=",
            "gpuFrameMs=",
            "presentSync=",
            "evidence=",
        ):
            self.assertIn(token, self.title)


if __name__ == "__main__":
    unittest.main()
