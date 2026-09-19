from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts"
SHADERS = ROOT / "apps/dx11_viewer/shaders"


class ThesisEditEvaluationContractTests(unittest.TestCase):
    def test_files(self):
        for relative in [
            "include/hairgpu/ThesisEditEvaluation.h",
            "apps/dx11_viewer/src/GpuHairViewer_parts/19_thesis_practical_edit_evaluation.inl",
            "apps/dx11_viewer/shaders/HairThesisLocalEditInvalidate.hlsl",
            "tools/final_stage/hairgpu_thesis_practical_matrix.py",
            "tools/final_stage/hairgpu_thesis_practical_report.py",
        ]:
            self.assertTrue((ROOT / relative).is_file(), relative)

    def test_routes(self):
        cpp = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        p03 = (PARTS / "03_simulation_reset_and_frame_step.inl").read_text(encoding="utf-8")
        p10 = (PARTS / "10_phase1_telemetry_csv_jsonl_summary.inl").read_text(encoding="utf-8")
        p12 = (PARTS / "12_strand_pick_gizmo_and_brush_edit.inl").read_text(encoding="utf-8")
        p16 = (PARTS / "16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
        p18 = (PARTS / "18_novelty01_render_dirty_range_consumer.inl").read_text(encoding="utf-8")

        self.assertIn("hairgpu/ThesisEditEvaluation.h", cpp)
        self.assertIn("19_thesis_practical_edit_evaluation.inl", cpp)
        self.assertIn("FlushThesisPracticalEditEpisodes();", p18)
        self.assertIn("ReleaseThesisPracticalEditEvaluation();", p18)
        self.assertIn("PrepareThesisPracticalEditEvaluation();", header)
        self.assertIn("InjectThesisPracticalLocalEdit();", header)
        self.assertIn("MarkThesisPracticalFirstPresent(HRESULT presentResult);", header)
        self.assertIn("BuildThesisUpdateVisualizationCategories", header)
        self.assertIn("HAIRGPU_THESIS_EDIT_EVAL", p03)
        self.assertIn("InjectThesisPracticalLocalEdit()", p10)
        self.assertIn("RecordThesisPracticalEditEpisode(sorted);", p12)
        self.assertIn("PrepareThesisPracticalEditEvaluation();", p16)
        self.assertIn("ApplyThesisPracticalPendingEditInvalidation();", p16)

    def test_no_csv_io_in_record(self):
        text = (PARTS / "19_thesis_practical_edit_evaluation.inl").read_text(encoding="utf-8")
        begin = text.index("void GpuHairViewer::RecordThesisPracticalEditEpisode")
        end = text.index("bool GpuHairViewer::InjectThesisPracticalLocalEdit")
        self.assertNotIn("std::ofstream", text[begin:end])
        self.assertIn("bufferedEpisodes.push_back", text[begin:end])

    def test_edit_to_first_present_latency_is_measured_and_exported(self):
        schema = (ROOT / "include/hairgpu/ThesisEditEvaluation.h").read_text(encoding="utf-8")
        practical = (PARTS / "19_thesis_practical_edit_evaluation.inl").read_text(encoding="utf-8")
        render = (PARTS / "06_render_mesh_and_visibility_recovery.inl").read_text(encoding="utf-8")

        self.assertIn("hairgpu-thesis-edit-eval-v3", schema)
        for field in [
            "editCommitQpc",
            "firstPresentQpc",
            "firstPresentedFrameIndex",
            "firstPresentObserved",
            "editToFirstPresentMilliseconds",
        ]:
            self.assertIn(field, schema)
        self.assertIn("QueryPerformanceCounter(&editCommit)", practical)
        self.assertIn("void GpuHairViewer::MarkThesisPracticalFirstPresent", practical)
        self.assertIn("edit_to_first_present_ms", practical)
        self.assertIn("MarkThesisPracticalFirstPresent(m_lastPresentResult);", render)

    def test_dedicated_gpu_pass_timings_are_resolved_to_samples(self):
        timing = (PARTS / "02_resource_creation_and_shader_loading.inl").read_text(encoding="utf-8")
        baseline = (PARTS / "03_simulation_reset_and_frame_step.inl").read_text(encoding="utf-8")
        proposal = (PARTS / "16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
        render_prep = (PARTS / "18_novelty01_render_dirty_range_consumer.inl").read_text(encoding="utf-8")

        self.assertIn("BeginGpuPassTiming(GpuPassId::FollowerReconstruction)", baseline)
        self.assertIn("EndGpuPassTiming(GpuPassId::FollowerReconstruction)", baseline)
        self.assertIn("BeginGpuPassTiming(GpuPassId::FollowerReconstruction)", proposal)
        self.assertIn("EndGpuPassTiming(GpuPassId::FollowerReconstruction)", proposal)
        self.assertIn("BeginGpuPassTiming(GpuPassId::RenderPreparation)", render_prep)
        self.assertIn("EndGpuPassTiming(GpuPassId::RenderPreparation)", render_prep)
        self.assertIn(
            "sample.followerReconstructionGpuMilliseconds = passMilliseconds[static_cast<size_t>(GpuPassId::FollowerReconstruction)]",
            timing,
        )
        self.assertIn(
            "sample.renderPreparationGpuMilliseconds = passMilliseconds[static_cast<size_t>(GpuPassId::RenderPreparation)]",
            timing,
        )

    def test_update_state_palette_uses_execution_categories(self):
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        practical = (PARTS / "19_thesis_practical_edit_evaluation.inl").read_text(encoding="utf-8")
        sync = (PARTS / "04_constant_buffers_and_active_set_sync.inl").read_text(encoding="utf-8")
        shader = (SHADERS / "HairStrandInstanced.hlsl").read_text(encoding="utf-8")
        controls = (PARTS / "07_draw_path_and_gpu_timing.inl").read_text(encoding="utf-8")
        keys = (PARTS / "15_shutdown_and_tail.inl").read_text(encoding="utf-8")

        self.assertIn("ThesisUpdateState = 4", header)
        self.assertIn("runtime->activeGuides", practical)
        self.assertIn("lastEditedGuides", practical)
        self.assertIn("lastFallbackVisual", practical)
        self.assertIn("BuildThesisUpdateVisualizationCategories(editCategories)", sync)
        self.assertIn("DebugColorMode == 4u", shader)
        self.assertIn("0 Reused, 1 Updated, 2 Edited, 3 Fallback", shader)
        self.assertIn("debugColorSelection <= 4", controls)
        self.assertIn("% 5u", keys)

    def test_shader_contract(self):
        runtime = (SHADERS / "HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8")
        local = (SHADERS / "HairThesisLocalEditInvalidate.hlsl").read_text(encoding="utf-8")
        self.assertIn("HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1", runtime)
        self.assertIn("hardFlags |= N01_DIRTY_EDIT;", runtime)
        self.assertIn("HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1", local)
        self.assertIn("block.dirtyFlags |= N01_DIRTY_EDIT;", local)

    def test_thesis_shader_is_deployed(self):
        vcx = (ROOT / "HairSystem.vcxproj").read_text(encoding="utf-8")
        cmake = (ROOT / "apps/dx11_viewer/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("HairThesisLocalEditInvalidate.hlsl", vcx)
        self.assertIn("HairThesisLocalEditInvalidate.hlsl", cmake)

    def test_report_contains_new_latency_and_gpu_metrics(self):
        report = (ROOT / "tools/final_stage/hairgpu_thesis_practical_report.py").read_text(encoding="utf-8")
        for marker in [
            "hairgpu-thesis-practical-report-v3",
            "edit_to_first_present_ms",
            "follower_reconstruction_gpu_median_ms",
            "follower_reconstruction_gpu_p95_ms",
            "render_preparation_gpu_median_ms",
            "render_preparation_gpu_p95_ms",
        ]:
            self.assertIn(marker, report)
        for counter in [
            "counter_0", "counter_1", "counter_3", "counter_43", "counter_44",
            "counter_50", "counter_51", "counter_52", "counter_55", "counter_56",
        ]:
            self.assertIn(counter, report)


if __name__ == "__main__":
    unittest.main()
