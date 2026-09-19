from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ThesisEditEvaluationContractTests(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_runtime_is_compiled_and_routed(self):
        cpp = self.read("apps/dx11_viewer/src/GpuHairViewer.cpp")
        header = self.read("apps/dx11_viewer/src/GpuHairViewer.h")
        p03 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl")
        p06 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl")
        p10 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl")
        p12 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl")
        p16 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl")

        self.assertIn('hairgpu/ThesisEditEvaluation.h', cpp)
        self.assertIn('19_thesis_practical_edit_evaluation.inl', cpp)
        for marker in (
            "PrepareThesisPracticalEditEvaluation",
            "InjectThesisPracticalLocalEdit",
            "MarkThesisPracticalFirstPresent",
            "FlushThesisPracticalEditEpisodes",
            "AppendThesisUpdateStateDebugLines",
        ):
            self.assertIn(marker, header)
        self.assertIn("InjectThesisPracticalLocalEdit()", p03)
        self.assertIn("InjectThesisPracticalLocalEdit()", p10)
        self.assertIn("RecordThesisPracticalEditEpisode(sorted);", p12)
        self.assertIn("PrepareThesisPracticalEditEvaluation();", p16)
        self.assertIn("ApplyThesisPracticalPendingEditInvalidation();", p16)
        self.assertIn("MarkThesisPracticalFirstPresent(m_lastPresentResult);", p06)
        self.assertIn("FlushThesisPracticalEditEpisodes();", p06)

    def test_gpu_pass_timing_is_real_and_source_frame_aligned(self):
        p02 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl")
        p03 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl")
        p16 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl")
        p18 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl")

        self.assertIn("BeginGpuPassTiming(GpuPassId::FollowerReconstruction);", p03)
        self.assertIn("EndGpuPassTiming(GpuPassId::FollowerReconstruction);", p03)
        self.assertIn("BeginGpuPassTiming(GpuPassId::FollowerReconstruction);", p16)
        self.assertIn("BeginGpuPassTiming(GpuPassId::RenderPreparation);", p18)
        self.assertIn("sample.followerReconstructionGpuMilliseconds =", p02)
        self.assertIn("sample.renderPreparationGpuMilliseconds =", p02)

    def test_commit_to_first_present_schema_and_csv(self):
        header = self.read("include/hairgpu/ThesisEditEvaluation.h")
        runtime = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/19_thesis_practical_edit_evaluation.inl")
        self.assertIn("hairgpu-thesis-edit-eval-v3", header)
        for marker in (
            "editCommitQpc",
            "firstPresentQpc",
            "firstPresentedFrameIndex",
            "firstPresentObserved",
            "editToFirstPresentMilliseconds",
        ):
            self.assertIn(marker, header)
        for column in (
            "edit_commit_qpc",
            "first_present_qpc",
            "first_present_frame",
            "first_present_observed",
            "edit_to_first_present_ms",
        ):
            self.assertIn(column, runtime)

    def test_thesis_update_state_overlay_is_debug_only(self):
        header = self.read("apps/dx11_viewer/src/GpuHairViewer.h")
        p01 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl")
        p08 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/08_ui_state_sync_and_controls.inl")
        p14 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/14_scalp_overlay_and_window_messages.inl")
        p16 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl")
        p19 = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/19_thesis_practical_edit_evaluation.inl")

        self.assertIn("ThesisUpdateState = 4", header)
        self.assertIn("AppendThesisUpdateStateDebugLines();", p14)
        self.assertIn("latestThesisUpdateCategories", p16)
        self.assertIn("Priority: Fallback > Edited > Updated > Reused", p19)
        self.assertIn("IsExperimentModeEnabled()", p19)
        self.assertIn("Edited=Magenta, Updated=Yellow, Reused=Cyan, Fallback=Red", p08)
        experiment = p01[p01.index("if (m_experimentCase.enabled)") :]
        self.assertIn("m_debugColorMode = DebugColorMode::Natural;", experiment)

    def test_report_consumes_new_evidence(self):
        report = self.read("tools/final_stage/hairgpu_thesis_practical_report.py")
        self.assertIn("hairgpu-thesis-practical-report-v3", report)
        for marker in (
            "edit_to_first_present_ms",
            "post_edit_follower_gpu_median_ms",
            "post_edit_follower_gpu_p95_ms",
            "post_edit_render_prep_gpu_median_ms",
            "post_edit_render_prep_gpu_p95_ms",
        ):
            self.assertIn(marker, report)

    def test_no_csv_io_in_edit_commit_path(self):
        runtime = self.read("apps/dx11_viewer/src/GpuHairViewer_parts/19_thesis_practical_edit_evaluation.inl")
        a = runtime.index("void GpuHairViewer::RecordThesisPracticalEditEpisode")
        b = runtime.index("bool GpuHairViewer::InjectThesisPracticalLocalEdit")
        self.assertNotIn("std::ofstream", runtime[a:b])
        self.assertIn("bufferedEpisodes.push_back", runtime[a:b])


if __name__ == "__main__":
    unittest.main()
