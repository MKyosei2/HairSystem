from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ThesisEditEvaluationContractTests(unittest.TestCase):
    def test_direct_integration_files_exist(self) -> None:
        self.assertTrue(
            (ROOT / "include/hairgpu/ThesisEditEvaluation.h").is_file()
        )
        self.assertTrue(
            (
                ROOT
                / "apps/dx11_viewer/src/GpuHairViewer_parts/"
                "19_thesis_practical_edit_evaluation.inl"
            ).is_file()
        )
        self.assertTrue(
            (
                ROOT
                / "apps/dx11_viewer/shaders/"
                "HairThesisLocalEditInvalidate.hlsl"
            ).is_file()
        )

    def test_existing_viewer_routes_thesis_edit(self) -> None:
        cpp = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(
            encoding="utf-8"
        )
        part03 = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/"
            "03_simulation_reset_and_frame_step.inl"
        ).read_text(encoding="utf-8")
        part10 = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/"
            "10_phase1_telemetry_csv_jsonl_summary.inl"
        ).read_text(encoding="utf-8")
        part12 = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/"
            "12_strand_pick_gizmo_and_brush_edit.inl"
        ).read_text(encoding="utf-8")
        part16 = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/"
            "16_segment_group_adaptive_payload_complete.inl"
        ).read_text(encoding="utf-8")

        self.assertIn("19_thesis_practical_edit_evaluation.inl", cpp)
        self.assertIn("InjectThesisPracticalLocalEdit()", part03)
        self.assertIn("InjectThesisPracticalLocalEdit()", part10)
        self.assertIn("RecordThesisPracticalEditEpisode(sorted);", part12)
        self.assertIn("ApplyThesisPracticalPendingEditInvalidation();", part16)

    def test_thesis_part_reuses_shared_environment_helpers(self) -> None:
        text = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/"
            "19_thesis_practical_edit_evaluation.inl"
        ).read_text(encoding="utf-8")
        self.assertIn("GetEnvironmentVariableString(", text)
        self.assertIn("EnvironmentValueIsTrue(", text)
        self.assertIn("EnvironmentValueToUInt(", text)
        self.assertNotIn("GetEnvironmentVariableW(", text)

    def test_runtime_shader_promotes_block_edit_dirty_reason(self) -> None:
        shader = (
            ROOT
            / "apps/dx11_viewer/shaders/"
            "HairSegmentGroupAdaptivePayloadRuntime.hlsl"
        ).read_text(encoding="utf-8")
        self.assertIn("HAIRGPU_THESIS_BLOCK_EDIT_INVALIDATION_V1", shader)
        self.assertIn("hardFlags |= N01_DIRTY_EDIT;", shader)


if __name__ == "__main__":
    unittest.main()
