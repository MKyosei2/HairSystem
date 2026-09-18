from __future__ import annotations
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

class ThesisEditEvaluationContractTests(unittest.TestCase):
    def test_files(self):
        for r in [
            "include/hairgpu/ThesisEditEvaluation.h",
            "apps/dx11_viewer/src/GpuHairViewer_parts/19_thesis_practical_edit_evaluation.inl",
            "apps/dx11_viewer/shaders/HairThesisLocalEditInvalidate.hlsl",
            "tools/final_stage/hairgpu_thesis_practical_matrix.py",
            "tools/final_stage/hairgpu_thesis_practical_report.py",
        ]: self.assertTrue((ROOT/r).is_file(), r)
    def test_routes(self):
        cpp=(ROOT/"apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
        h=(ROOT/"apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        p03=(ROOT/"apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl").read_text(encoding="utf-8")
        p10=(ROOT/"apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl").read_text(encoding="utf-8")
        p12=(ROOT/"apps/dx11_viewer/src/GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl").read_text(encoding="utf-8")
        p16=(ROOT/"apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
        self.assertIn('hairgpu/ThesisEditEvaluation.h',cpp); self.assertIn('19_thesis_practical_edit_evaluation.inl',cpp); self.assertIn('FlushThesisPracticalEditEpisodes();',cpp)
        self.assertIn('PrepareThesisPracticalEditEvaluation();',h); self.assertIn('InjectThesisPracticalLocalEdit();',h)
        self.assertIn('HAIRGPU_THESIS_EDIT_EVAL',p03); self.assertIn('InjectThesisPracticalLocalEdit()',p10)
        self.assertIn('RecordThesisPracticalEditEpisode(sorted);',p12)
        self.assertIn('PrepareThesisPracticalEditEvaluation();',p16); self.assertIn('ApplyThesisPracticalPendingEditInvalidation();',p16)
    def test_no_csv_io_in_record(self):
        t=(ROOT/"apps/dx11_viewer/src/GpuHairViewer_parts/19_thesis_practical_edit_evaluation.inl").read_text(encoding="utf-8")
        a=t.index('void GpuHairViewer::RecordThesisPracticalEditEpisode'); b=t.index('bool GpuHairViewer::InjectThesisPracticalLocalEdit')
        self.assertNotIn('std::ofstream',t[a:b]); self.assertIn('bufferedEpisodes.push_back',t[a:b])
    def test_shader_contract(self):
        runtime=(ROOT/"apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8")
        local=(ROOT/"apps/dx11_viewer/shaders/HairThesisLocalEditInvalidate.hlsl").read_text(encoding="utf-8")
        self.assertIn('HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1',runtime); self.assertIn('hardFlags |= N01_DIRTY_EDIT;',runtime)
        self.assertIn('HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1',local); self.assertIn('block.dirtyFlags |= N01_DIRTY_EDIT;',local)
    def test_report_reuses_counter_abi(self):
        t=(ROOT/"tools/final_stage/hairgpu_thesis_practical_report.py").read_text(encoding="utf-8")
        for c in ['counter_0','counter_1','counter_3','counter_43','counter_44','counter_50','counter_51','counter_52','counter_55','counter_56']: self.assertIn(c,t)
if __name__=='__main__': unittest.main()
