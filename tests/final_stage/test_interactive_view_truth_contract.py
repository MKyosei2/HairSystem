from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer_parts"


class InteractiveViewTruthContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = (ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer.h").read_text(
            encoding="utf-8"
        )
        cls.startup = (PARTS / "01_window_device_and_ui_setup.inl").read_text(
            encoding="utf-8"
        )
        cls.run_loop = (PARTS / "02_resource_creation_and_shader_loading.inl").read_text(
            encoding="utf-8"
        )
        cls.reset = (PARTS / "03_simulation_reset_and_frame_step.inl").read_text(
            encoding="utf-8"
        )
        cls.camera = (PARTS / "05_active_set_scoring_and_runtime_update.inl").read_text(
            encoding="utf-8"
        )
        cls.title = (PARTS / "06_render_mesh_and_visibility_recovery.inl").read_text(
            encoding="utf-8"
        )
        cls.settings = (PARTS / "08_ui_state_sync_and_controls.inl").read_text(
            encoding="utf-8"
        )
        cls.quick_edit = (PARTS / "09_settings_load_save_and_export.inl").read_text(
            encoding="utf-8"
        )
        cls.brush = (PARTS / "12_strand_pick_gizmo_and_brush_edit.inl").read_text(
            encoding="utf-8"
        )
        cls.window = (PARTS / "14_scalp_overlay_and_window_messages.inl").read_text(
            encoding="utf-8"
        )
        cls.scalp_shader = (
            ROOT / "apps" / "dx11_viewer" / "shaders" / "ScalpMesh.hlsl"
        ).read_text(encoding="utf-8")

    def test_session_fps_never_forgets_a_runtime_freeze(self) -> None:
        self.assertIn("m_sessionEffectiveFps", self.header)
        self.assertIn("m_sessionWallSeconds", self.header)
        self.assertIn("m_sessionCompletedFrameCount", self.header)
        self.assertIn("m_sessionWallSeconds += static_cast<double>(wallFrameSeconds);", self.run_loop)
        self.assertIn("++m_sessionCompletedFrameCount;", self.run_loop)
        self.assertIn("sessionFPS=", self.title)
        self.assertIn("rollingFPS=", self.title)
        self.assertIn("onePctLow=", self.title)
        self.assertIn("instantFPS=", self.title)
        self.assertIn("Session FPS (all completed frames)", self.settings)
        self.assertIn("Rolling FPS (last 600 frames)", self.settings)
        self.assertIn("kRealtimeFrameHistoryCapacity = 600u", self.header)
        self.assertNotIn("dt > 1.0f", self.run_loop)

    def test_startup_is_sim_view_without_saved_edit_selection(self) -> None:
        stable_marker = self.startup.index("Stable startup is a reviewable simulation view")
        stable_slice = self.startup[stable_marker : stable_marker + 1000]
        self.assertIn("m_workspaceMode = WorkspaceMode::Sim;", stable_slice)
        self.assertIn("m_interactionMode = InteractionMode::View;", stable_slice)
        self.assertIn("m_debugColorMode = DebugColorMode::Natural;", stable_slice)
        self.assertIn("m_selectedStrandIndex = -1;", stable_slice)
        self.assertIn("m_cpuReadbackMode = CpuReadbackMode::Off;", stable_slice)
        self.assertIn("m_showScalpMesh = true;", stable_slice)
        self.assertIn("Loading the inline form may contain a saved strandSelection", self.run_loop)
        self.assertIn("m_selectedStrandIndex = -1;", self.run_loop)

    def test_saved_selection_only_activates_in_groom_edit(self) -> None:
        marker = self.quick_edit.index("if (!layer.strandSelection.empty()")
        selection_slice = self.quick_edit[marker : marker + 420]
        self.assertIn("m_workspaceMode == WorkspaceMode::Groom", selection_slice)
        self.assertIn("m_interactionMode == InteractionMode::Edit", selection_slice)

    def test_sim_view_cannot_start_brush_gizmo_or_click_pick(self) -> None:
        for function_name in ("BeginViewportBrushStroke", "BeginStrandGizmoDrag"):
            marker = self.brush.index(function_name)
            function_slice = self.brush[marker : marker + 600]
            self.assertIn("m_workspaceMode != WorkspaceMode::Groom", function_slice)
            self.assertIn("m_interactionMode != InteractionMode::Edit", function_slice)
            self.assertIn("return false;", function_slice)
        self.assertIn("const bool editInteraction", self.window)
        self.assertIn("Sim/View always orbits", self.window)
        self.assertIn("It never starts a strand edit", self.window)
        self.assertIn("m_workspaceMode == WorkspaceMode::Groom", self.window)
        self.assertIn("m_interactionMode == InteractionMode::Edit", self.window)

    def test_selected_strand_overlay_is_edit_only(self) -> None:
        overlay_marker = self.window.index("m_selectedStrandIndex >= 0")
        overlay_prefix = self.window[max(0, overlay_marker - 220) : overlay_marker + 300]
        self.assertIn("m_workspaceMode == WorkspaceMode::Groom", overlay_prefix)
        self.assertIn("m_interactionMode == InteractionMode::Edit", overlay_prefix)

    def test_stable_preset_clears_hidden_edit_costs(self) -> None:
        preset_marker = self.settings.index("if (presetIndex == 0)")
        preset_slice = self.settings[preset_marker : preset_marker + 1300]
        self.assertIn("m_selectedStrandIndex = -1;", preset_slice)
        self.assertIn("m_gizmoDragging = false;", preset_slice)
        self.assertIn("m_brushStrokeActive = false;", preset_slice)
        self.assertIn("m_cpuReadbackMode = CpuReadbackMode::Off;", preset_slice)
        self.assertIn("m_showScalpMesh = true;", preset_slice)

    def test_canonical_subject_uses_tight_centered_camera(self) -> None:
        self.assertIn("const bool canonicalBenchmarkWig", self.camera)
        self.assertIn(
            "Clamp(m_assetBoundsCenter.y + 0.015f, 1.42f, 1.54f)",
            self.camera,
        )
        self.assertIn("kTargetViewportOccupancy = 0.66f", self.camera)
        self.assertIn("Clamp(projectedFitDistance, 0.56f, 0.74f)", self.camera)

    def test_visible_scalp_is_not_rendered_as_a_black_hole(self) -> None:
        self.assertIn("mannequinFace", (
            PARTS / "10_phase1_telemetry_csv_jsonl_summary.inl"
        ).read_text(encoding="utf-8"))
        self.assertIn("baseColor * (0.52f + 0.44f * diffuse)", self.scalp_shader)


if __name__ == "__main__":
    unittest.main()
