from __future__ import annotations
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PARTS = ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts"


class ThesisEditEvaluationContractTests(unittest.TestCase):
    def test_files(self):
        for rel in [
            "include/hairgpu/ThesisEditEvaluation.h",
            "apps/dx11_viewer/src/GpuHairViewer_parts/19_thesis_practical_edit_evaluation.inl",
            "apps/dx11_viewer/shaders/HairThesisLocalEditInvalidate.hlsl",
            "tools/final_stage/hairgpu_thesis_practical_matrix.py",
            "tools/final_stage/hairgpu_thesis_practical_report.py",
        ]:
            self.assertTrue((ROOT / rel).is_file(), rel)

    def test_routes(self):
        cpp = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        p06 = (PARTS / "06_render_mesh_and_visibility_recovery.inl").read_text(encoding="utf-8")
        p10 = (PARTS / "10_phase1_telemetry_csv_jsonl_summary.inl").read_text(encoding="utf-8")
        p12 = (PARTS / "12_strand_pick_gizmo_and_brush_edit.inl").read_text(encoding="utf-8")
        p16 = (PARTS / "16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
        p18 = (PARTS / "18_novelty01_render_dirty_range_consumer.inl").read_text(encoding="utf-8")
        self.assertIn("hairgpu/ThesisEditEvaluation.h", cpp)
        self.assertIn("19_thesis_practical_edit_evaluation.inl", cpp)
        self.assertIn("PrepareThesisPracticalEditEvaluation();", header)
        self.assertIn("InjectThesisPracticalLocalEdit();", header)
        self.assertIn("MarkThesisPracticalFirstPresent(HRESULT presentResult);", header)
        self.assertIn("BuildThesisUpdateVisualizationCategories", header)
        self.assertIn("InjectThesisPracticalLocalEdit()", p10)
        self.assertIn("RecordThesisPracticalEditEpisode(sorted);", p12)
        self.assertIn("PrepareThesisPracticalEditEvaluation();", p16)
        self.assertIn("ApplyThesisPracticalPendingEditInvalidation();", p16)
        self.assertIn("MarkThesisPracticalFirstPresent(m_lastPresentResult);", p06)
        self.assertIn("FlushThesisPracticalEditEpisodes();", p18)

    def test_gpu_timing_is_written_back_to_source_frame_sample(self):
        p02 = (PARTS / "02_resource_creation_and_shader_loading.inl").read_text(encoding="utf-8")
        self.assertIn(
            "sample.followerReconstructionGpuMilliseconds = passMilliseconds",
            p02,
        )
        self.assertIn(
            "sample.renderPreparationGpuMilliseconds = passMilliseconds",
            p02,
        )
        self.assertIn("GpuPassId::FollowerReconstruction", p02)
        self.assertIn("GpuPassId::RenderPreparation", p02)

    def test_edit_to_first_present_contract(self):
        header = (ROOT / "include/hairgpu/ThesisEditEvaluation.h").read_text(encoding="utf-8")
        runtime = (PARTS / "19_thesis_practical_edit_evaluation.inl").read_text(encoding="utf-8")
        self.assertIn("hairgpu-thesis-edit-eval-v3", header)
        self.assertIn("editToFirstPresentMilliseconds", header)
        self.assertIn("firstPresentObserved", header)
        self.assertIn("QueryPerformanceCounter(&editCommit)", runtime)
        self.assertIn("MarkThesisPracticalFirstPresent", runtime)
        self.assertIn("edit_to_first_present_ms", runtime)

    def test_update_state_visualization_contract(self):
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        p04 = (PARTS / "04_constant_buffers_and_active_set_sync.inl").read_text(encoding="utf-8")
        p07 = (PARTS / "07_draw_path_and_gpu_timing.inl").read_text(encoding="utf-8")
        p08 = (PARTS / "08_ui_state_sync_and_controls.inl").read_text(encoding="utf-8")
        p15 = (PARTS / "15_shutdown_and_tail.inl").read_text(encoding="utf-8")
        shader = (ROOT / "apps/dx11_viewer/shaders/HairStrandInstanced.hlsl").read_text(encoding="utf-8")
        self.assertIn("ThesisUpdateState = 4", header)
        self.assertIn("BuildThesisUpdateVisualizationCategories(editCategories)", p04)
        self.assertIn("debugColorSelection <= 4", p07)
        self.assertIn(
            "Thesis Update [Edited=Magenta, Updated=Yellow, Reused=Cyan, Fallback=Red]",
            p08,
        )
        self.assertIn("% 5u", p15)
        self.assertIn("DebugColorMode == 4u", shader)
        self.assertIn("fallback", shader)
        self.assertIn("reused", shader)

    def test_no_csv_io_in_record(self):
        runtime = (PARTS / "19_thesis_practical_edit_evaluation.inl").read_text(encoding="utf-8")
        start = runtime.index("void GpuHairViewer::RecordThesisPracticalEditEpisode")
        end = runtime.index("bool GpuHairViewer::InjectThesisPracticalLocalEdit")
        self.assertNotIn("std::ofstream", runtime[start:end])
        self.assertIn("bufferedEpisodes.push_back", runtime[start:end])

    def test_shader_contract(self):
        runtime = (ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8")
        local = (ROOT / "apps/dx11_viewer/shaders/HairThesisLocalEditInvalidate.hlsl").read_text(encoding="utf-8")
        self.assertIn("HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1", runtime)
        self.assertIn("hardFlags |= N01_DIRTY_EDIT;", runtime)
        self.assertIn("HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1", local)
        self.assertIn("block.dirtyFlags |= N01_DIRTY_EDIT;", local)

    def test_report_reuses_counter_abi_and_adds_timings(self):
        report = (ROOT / "tools/final_stage/hairgpu_thesis_practical_report.py").read_text(encoding="utf-8")
        for counter in [
            "counter_0",
            "counter_1",
            "counter_3",
            "counter_43",
            "counter_44",
            "counter_50",
            "counter_51",
            "counter_52",
            "counter_55",
            "counter_56",
        ]:
            self.assertIn(counter, report)
        self.assertIn("hairgpu-thesis-practical-report-v3", report)
        self.assertIn("edit_to_first_present_ms", report)
        self.assertIn("followerReconstructionGpuMilliseconds", report)
        self.assertIn("renderPreparationGpuMilliseconds", report)


if __name__ == "__main__":
    unittest.main()
