from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer_parts"
SHADERS = ROOT / "apps" / "dx11_viewer" / "shaders"


class RibbonRenderContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer.h").read_text(
            encoding="utf-8"
        )
        cls.startup = (PARTS / "01_window_device_and_ui_setup.inl").read_text(
            encoding="utf-8"
        )
        cls.run_loop = (
            PARTS / "02_resource_creation_and_shader_loading.inl"
        ).read_text(encoding="utf-8")
        cls.reset = (PARTS / "03_simulation_reset_and_frame_step.inl").read_text(
            encoding="utf-8"
        )
        cls.render = (
            PARTS / "05_active_set_scoring_and_runtime_update.inl"
        ).read_text(encoding="utf-8")
        cls.controls = (
            PARTS / "07_draw_path_and_gpu_timing.inl"
        ).read_text(encoding="utf-8")
        cls.settings = (
            PARTS / "08_ui_state_sync_and_controls.inl"
        ).read_text(encoding="utf-8")
        cls.shader = (SHADERS / "HairStrandInstanced.hlsl").read_text(
            encoding="utf-8"
        )

    def test_startup_and_reset_select_production_ribbon(self) -> None:
        startup_marker = "Stable preview must start in the production ribbon path"
        marker_index = self.startup.index(startup_marker)
        startup_slice = self.startup[marker_index : marker_index + 500]
        self.assertIn("m_renderMode = RenderMode::StrandRibbon;", startup_slice)
        self.assertNotIn("m_renderMode = RenderMode::ParticleBillboard;", startup_slice)
        self.assertIn("m_renderMode = RenderMode::StrandRibbon;", self.reset)

    def test_unselected_combo_does_not_map_to_point_sprite(self) -> None:
        self.assertIn("CB_GETCURSEL returns CB_ERR (-1)", self.controls)
        self.assertNotIn("m_renderMode = (renderSel <= 0)", self.controls)
        self.assertIn("else if (renderSel == 0)", self.controls)
        self.assertIn("else if (renderSel == 2)", self.controls)
        self.assertIn("CB_ERR/unknown selection preserves the current mode", self.controls)

    def test_persisted_particle_mode_requires_explicit_debug_opt_in(self) -> None:
        self.assertIn("HAIRGPU_ALLOW_PARTICLE_DEBUG_RENDERING", self.settings)
        self.assertIn("!allowPersistedParticleDebug", self.settings)
        self.assertIn("m_renderMode = RenderMode::StrandRibbon;", self.settings)

    def test_formal_and_local_debug_capture_force_ribbon(self) -> None:
        self.assertIn("HAIRGPU_LOCAL_DEBUG_CAPTURE", self.run_loop)
        marker_index = self.run_loop.index(
            "Formal and local-debug captures must exercise the production ribbon renderer"
        )
        force_slice = self.run_loop[marker_index : marker_index + 500]
        self.assertIn("IsExperimentModeEnabled()", force_slice)
        self.assertIn("m_renderMode = RenderMode::StrandRibbon;", force_slice)

    def test_shader_preserves_loader_abi_marker(self) -> None:
        self.assertIn("HAIRGPU_STRAND_RIBBON_SHADER_ABI_1", self.shader)
        self.assertTrue(
            self.shader.lstrip().startswith("// HAIRGPU_STRAND_RIBBON_VISIBILITY_"),
            "The deployed strand shader must retain a visible revision marker.",
        )

    def test_camera_facing_width_uses_screen_plane_side_vector(self) -> None:
        self.assertIn("float3 ribbonSide = cross(viewDirection, tangent);", self.shader)
        self.assertIn("center + ribbonSide * (halfWidth * sideSign)", self.shader)
        self.assertIn("cross(tangent, ribbonSide)", self.shader)
        self.assertNotIn("const float angle", self.shader)
        self.assertNotIn("const float3 radialDir", self.shader)
        self.assertNotIn("float3 binormal", self.shader)

    def test_render_tiers_and_edit_categories_are_separate(self) -> None:
        sync = (PARTS / "04_constant_buffers_and_active_set_sync.inl").read_text(
            encoding="utf-8"
        )
        self.assertIn("m_editVisualizationCategoryBuffer", self.header)
        self.assertIn("StructuredBuffer<uint> RenderTiers : register(t2);", self.shader)
        self.assertIn("StructuredBuffer<uint> EditCategories : register(t3);", self.shader)
        self.assertIn("BuildEditVisualizationCategories(editCategories)", sync)
        self.assertNotIn("BuildEditVisualizationCategories(renderTiers)", sync)
        self.assertIn("m_editVisualizationCategoryBuffer.srv.Get()", self.render)
        self.assertIn("VSSetShaderResources(0, 4, hairSrvs)", self.render)

    def test_normal_edit_view_does_not_force_debug_colors_or_idle_decimation(self) -> None:
        sync = (PARTS / "04_constant_buffers_and_active_set_sync.inl").read_text(
            encoding="utf-8"
        )
        self.assertIn("constants.debugColorMode = static_cast<uint32_t>(m_debugColorMode);", sync)
        self.assertNotIn("effectiveDebugColorMode", sync)
        self.assertIn("(fastEditPreview && heavyInteraction) ? 2u : 1u", sync)
        self.assertIn("constants.renderGuideOnlyAboveTier = 0xFFFFFFFFu;", sync)

    def test_width_is_not_used_as_strand_validity(self) -> None:
        self.assertIn("distanceScaledHalfWidth", self.shader)
        self.assertIn("Width is a material property, never a validity signal", self.shader)
        self.assertIn("output.alpha", self.shader)
        self.assertNotIn("discard;", self.shader)
        self.assertNotIn("strandTier < RenderGuideOnlyAboveTier", self.shader)

    def test_camera_framing_uses_trimmed_bounds(self) -> None:
        self.assertIn("Camera framing is a presentation concern", self.render)
        self.assertIn("samples.size() / 1000u", self.render)
        self.assertIn("m_cameraDistance = Clamp(desiredDistance, 0.80f, 12.0f);", self.render)

    def test_hidden_scalp_depth_prepass_is_removed(self) -> None:
        self.assertIn("Do not render a hidden scalp depth pre-pass", self.render)
        self.assertNotIn("m_depthOnlyBlendState", self.render)
        self.assertNotIn("m_depthOnlyBlendState", self.header)
        self.assertNotIn("CreateBlendState(&depthOnlyBlend", self.run_loop)
        self.assertIn("m_showScalpMesh", self.render)

    def test_ribbon_failure_never_silently_falls_back_to_particles(self) -> None:
        self.assertIn("const bool ribbonRequested", self.render)
        self.assertIn("const bool ribbonReady", self.render)
        self.assertIn("if (ribbonRequested && ribbonReady)", self.render)
        self.assertIn("else if (!ribbonRequested", self.render)


if __name__ == "__main__":
    unittest.main()
