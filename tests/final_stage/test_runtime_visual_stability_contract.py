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
        self.assertIn('m_renderMode = RenderMode::StrandRibbon', source)
        self.assertNotIn('m_renderMode = RenderMode::ParticleBillboard;', source)

    def test_asset_cache_is_versioned_and_semantically_validated(self) -> None:
        source = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl").read_text(encoding="utf-8")
        self.assertIn("sample_hybrid_cache_v15_depth_occluded_single_volume", source)
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

    def test_followers_use_bounded_secondary_motion(self) -> None:
        source = (ROOT / "apps/dx11_viewer/shaders/HairSim.hlsl").read_text(encoding="utf-8")
        follow = source[source.index("void FollowCS"):]
        self.assertIn("secondaryVelocity", follow)
        self.assertIn("maximumSecondaryLag", follow)
        self.assertIn("targetResponse", follow)
        self.assertIn("OutputPreviousPositions[strandPointIndex] = float4(currentPosition", follow)
        self.assertIn("ResolveFollowerExternalCollisions", follow)
        self.assertIn("requiredClearance", source)
        self.assertIn("authoredRadial", follow)
        self.assertIn("single analytic follower collision pass", follow)
        self.assertNotIn("kinematic reconstructions of their guide", follow)

    def test_hair_uses_camera_facing_thin_ribbons(self) -> None:
        source = (ROOT / "apps/dx11_viewer/shaders/HairRibbon.hlsl").read_text(encoding="utf-8")
        self.assertIn("[maxvertexcount(4)]", source)
        self.assertIn("ribbonSide", source)
        self.assertIn("CameraPosition.xyz - midpoint", source)
        self.assertIn("worldTangent", source)
        self.assertNotIn("const uint sides", source)
        self.assertNotIn("0.0020f", source)

    def test_actual_instanced_hair_path_is_guaranteed_visible(self) -> None:
        source = (ROOT / "apps/dx11_viewer/shaders/HairStrandInstanced.hlsl").read_text(encoding="utf-8")
        self.assertIn("HAIRGPU_STRAND_RIBBON_VISIBILITY_V15_DEPTH_OCCLUDED", source)
        self.assertIn("StructuredBuffer<uint> StrandTiers", source)
        self.assertIn("ribbonSide", source)
        self.assertIn("screenStableFloor", source)
        self.assertIn("rootOpening", source)
        self.assertIn("rootAlpha", source)
        self.assertNotIn("float3(0.0f, 0.0f, 0.0f);\n    }\n    if (!IsPlausiblePosition(point1))", source)
        self.assertNotIn("max(RenderStrandAlpha, 0.58f)", source)
        self.assertIn("materialAlpha", source)
        self.assertIn("output.position = float4(0.0f, 0.0f, 2.0f, 1.0f)", source)
        self.assertNotIn("clip(", source)
        self.assertNotIn("discard;", source)
        self.assertNotIn("0.0018f", source)

    def test_default_preview_reduces_wall_density_without_hiding_formal_strands(self) -> None:
        source = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/04_constant_buffers_and_active_set_sync.inl").read_text(encoding="utf-8")
        self.assertIn("(editVisualization ? 2u : 1u)", source)
        self.assertIn("(editVisualization ? 0.22f : 0.26f)", source)
        self.assertIn("constants.renderFollowerStride = 1u", source)
        self.assertIn("constants.renderStrandAlpha = 0.080f", source)

    def test_preview_density_uses_spatially_distributed_follower_subset(self) -> None:
        shader = (ROOT / "apps/dx11_viewer/shaders/HairStrandInstanced.hlsl").read_text(encoding="utf-8")
        self.assertIn("keepProbability", shader)
        self.assertIn("visibilitySample", shader)
        self.assertIn("followerCoverageAlpha", shader)
        self.assertIn("pow(", shader)
        self.assertNotIn("(followerLocal % followerStride) == 0u", shader)
        self.assertIn("lerp(0.94f, 1.0f", shader)
        self.assertIn("isGuide ? 0.06f : 0.88f", shader)
        startup = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl").read_text(encoding="utf-8")
        self.assertIn("m_widthScale = 1.00f", startup)

    def test_runtime_rejects_incompatible_instanced_hair_shader_abi(self) -> None:
        runtime = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/02a_required_runtime_foundation.inl").read_text(encoding="utf-8")
        shader = (ROOT / "apps/dx11_viewer/shaders/HairStrandInstanced.hlsl").read_text(encoding="utf-8")
        self.assertIn("HAIRGPU_STRAND_RIBBON_SHADER_ABI_1", runtime)
        self.assertIn("HAIRGPU_STRAND_RIBBON_SHADER_ABI_1", shader)
        self.assertIn("rejected: incompatible strand shader ABI", runtime)
        self.assertNotIn("HAIRGPU_STRAND_RIBBON_VISIBILITY_V11_FULL_COVERAGE", runtime)
        project = (ROOT / "HairSystem.vcxproj").read_text(encoding="utf-8")
        self.assertIn("HairStrandInstanced.hlsl", project)
        self.assertIn("copy /Y", project)

    def test_balanced_root_distribution_and_cache_generation(self) -> None:
        sample = (ROOT / "src/hairgpu_core/SampleAsset.cpp").read_text(encoding="utf-8")
        self.assertIn("(guideCount * 18u) / 100u", sample)
        self.assertIn("(guideCount * 14u) / 100u", sample)
        self.assertNotIn("guideCount / 2u", sample)
        self.assertNotIn("guideCount / 4u", sample)
        startup = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl").read_text(encoding="utf-8")
        self.assertIn("sample_hybrid_cache_v15_depth_occluded_single_volume", startup)


    def test_generated_asset_uses_contiguous_guide_then_follower_layout(self) -> None:
        sample = (ROOT / "src/hairgpu_core/SampleAsset.cpp").read_text(encoding="utf-8")
        validation = (ROOT / "src/hairgpu_core/Validation.cpp").read_text(encoding="utf-8")
        shader = (ROOT / "apps/dx11_viewer/shaders/HairSim.hlsl").read_text(encoding="utf-8")
        self.assertIn("ReorderGeneratedGuideFollowerLayout", sample)
        self.assertIn("oldGuideIndex + 1u + followerIndex", sample)
        self.assertIn("guides must occupy [0, guideStrandCount)", validation)
        self.assertIn("uint strandIndex = GuideStrandCount + followerIndex", shader)

    def test_blended_hair_does_not_write_depth(self) -> None:
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        resources = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl").read_text(encoding="utf-8")
        draw = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl").read_text(encoding="utf-8")
        self.assertIn("D3D11_DEPTH_WRITE_MASK_ZERO", resources)
        self.assertIn("OMSetDepthStencilState(m_hairDepthState.Get()", draw)

    def test_follower_scalp_guard_is_near_root_only(self) -> None:
        source = (ROOT / "apps/dx11_viewer/shaders/HairSim.hlsl").read_text(encoding="utf-8")
        self.assertIn("if (strandT < 0.12f)", source)
        self.assertIn("lerp(0.0030f, 0.0012f", source)
        self.assertNotIn("if (strandT < 0.58f)", source)



    def test_interactive_preview_does_not_reenable_opaque_scalp(self) -> None:
        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl"
        ).read_text(encoding="utf-8")
        self.assertIn("m_showScalpMesh = m_experimentCase.enabled;", source)
        self.assertNotIn(
            "m_renderMode = RenderMode::StrandRibbon;\n        m_showScalpMesh = true;",
            source,
        )
        preset = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/08_ui_state_sync_and_controls.inl").read_text(encoding="utf-8")
        self.assertIn("m_showScalpMesh = IsExperimentModeEnabled();", preset)

    def test_window_title_exposes_scalp_and_visual_fix_version(self) -> None:
        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl"
        ).read_text(encoding="utf-8")
        self.assertIn('L"  scalp="', source)
        self.assertIn('L"  visualFix=V15  previewFollowers=100%  depthOccluder=on"', source)
        self.assertIn('L"Hair GPU Viewer | visualFix=V15 depthOccluder=on"', source)

    def test_startup_does_not_auto_select_or_draw_edit_gizmo(self) -> None:
        form = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/09_settings_load_save_and_export.inl").read_text(encoding="utf-8")
        startup = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl").read_text(encoding="utf-8")
        overlay = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/14_scalp_overlay_and_window_messages.inl").read_text(encoding="utf-8")
        self.assertNotIn("m_selectedStrandIndex = static_cast<int32_t>(layer.strandSelection.front())", form)
        self.assertIn("HAIRGPU_START_IN_GROOM", startup)
        self.assertIn("m_selectedStrandIndex = -1", startup)
        self.assertIn("showSelectedStrandOverlay", overlay)
        self.assertIn("m_workspaceMode == WorkspaceMode::Groom", overlay)
        self.assertIn("m_interactionMode == InteractionMode::Edit", overlay)

    def test_procedural_groom_uses_parted_flow_not_circular_crown_shell(self) -> None:
        sample = (ROOT / "src/hairgpu_core/SampleAsset.cpp").read_text(encoding="utf-8")
        self.assertIn("signedPartDistance", sample)
        self.assertIn("partFlowMask", sample)
        self.assertIn("scalpTangent * partSideSign", sample)
        self.assertIn("const float rootPhase = Clamp(t / 0.10f", sample)
        self.assertIn("const float fallPhase = Clamp((t - 0.10f) / 0.90f", sample)
        self.assertNotIn("const float crownFlowBlend = 0.62f", sample)


    def test_interactive_preview_uses_invisible_scalp_depth_occluder(self) -> None:
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        resources = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl").read_text(encoding="utf-8")
        draw = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl").read_text(encoding="utf-8")
        self.assertIn("m_depthOnlyBlendState", header)
        self.assertIn("RenderTargetWriteMask = 0u", resources)
        self.assertIn("!m_showScalpMesh && m_depthOnlyBlendState", draw)
        self.assertIn("OMSetBlendState(m_depthOnlyBlendState.Get()", draw)
        self.assertIn("OMSetDepthStencilState(m_depthState.Get()", draw)

    def test_procedural_groom_avoids_mid_length_balloon_shell(self) -> None:
        sample = (ROOT / "src/hairgpu_core/SampleAsset.cpp").read_text(encoding="utf-8")
        self.assertIn("singleVolumeBell", sample)
        self.assertIn("parameters.sideVolume * sideFactor * 0.10f", sample)
        self.assertIn("parameters.backVolume * backFactor * 0.12f", sample)
        self.assertNotIn("parameters.sideVolume * sideFactor * 0.55f", sample)
        self.assertNotIn("parameters.backVolume * backFactor * 0.70f", sample)



if __name__ == "__main__":
    unittest.main()
