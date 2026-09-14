from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ThesisEditEvaluationContractTests(unittest.TestCase):
    def test_header_exists_without_counter_abi_changes(self) -> None:
        header = ROOT / "include/hairgpu/ThesisEditEvaluation.h"
        text = header.read_text(encoding="utf-8")
        self.assertIn("hairgpu-thesis-edit-eval-v1", text)
        self.assertNotIn("CounterIndex", text)

    def test_separate_invalidation_shader_contract(self) -> None:
        shader = (
            ROOT
            / "apps/dx11_viewer/shaders/HairThesisLocalEditInvalidate.hlsl"
        ).read_text(encoding="utf-8")
        self.assertIn("StructuredBuffer<uint> ThesisEditBlockIndices : register(t0);", shader)
        self.assertIn("RWStructuredBuffer<BlockDescriptor> ThesisBlocks : register(u0);", shader)
        self.assertIn("block.dirtyFlags |= N01_DIRTY_EDIT;", shader)

    def test_viewer_routes_local_edit_episode(self) -> None:
        cpp = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(
            encoding="utf-8"
        )
        part12 = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl"
        ).read_text(encoding="utf-8")
        part16 = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl"
        ).read_text(encoding="utf-8")
        shader = (
            ROOT
            / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
        ).read_text(encoding="utf-8")

        self.assertIn("19_thesis_practical_edit_evaluation.inl", cpp)
        self.assertIn("RecordThesisPracticalEditEpisode(sorted);", part12)
        self.assertIn("ApplyThesisPracticalPendingEditInvalidation();", part16)
        self.assertIn("HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1", shader)

    def test_legacy_local_edit_no_longer_unconditionally_forces_all(self) -> None:
        part03 = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl"
        ).read_text(encoding="utf-8")
        part10 = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl"
        ).read_text(encoding="utf-8")

        self.assertIn("InjectThesisPracticalLocalEdit()", part03)
        self.assertIn("InjectThesisPracticalLocalEdit()", part10)
        self.assertIn("if (!thesisLocalEdit)", part03)
        self.assertIn("if (!thesisLocalEdit)", part10)


if __name__ == "__main__":
    unittest.main()
