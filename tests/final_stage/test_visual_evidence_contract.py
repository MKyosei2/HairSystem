from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer.h"
CAPTURE = (
    ROOT
    / "apps"
    / "dx11_viewer"
    / "src"
    / "GpuHairViewer_parts"
    / "06_render_mesh_and_visibility_recovery.inl"
)
RUN_LOOP = (
    ROOT
    / "apps"
    / "dx11_viewer"
    / "src"
    / "GpuHairViewer_parts"
    / "02_resource_creation_and_shader_loading.inl"
)


class VisualEvidenceContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header = HEADER.read_text(encoding="utf-8")
        cls.capture = CAPTURE.read_text(encoding="utf-8")
        cls.run_loop = RUN_LOOP.read_text(encoding="utf-8")

    def test_viewer_exposes_end_of_run_backbuffer_capture(self) -> None:
        self.assertIn("bool CaptureBackBufferBmp", self.header)
        self.assertIn("GpuHairViewer::CaptureBackBufferBmp", self.capture)
        self.assertIn("D3D11_USAGE_STAGING", self.capture)
        self.assertIn("D3D11_CPU_ACCESS_READ", self.capture)
        self.assertIn("ResolveSubresource", self.capture)

    def test_capture_uses_dedicated_post_measurement_render(self) -> None:
        loop_end = self.run_loop.index("DrainGpuTimingAndCounterReadbacks")
        environment_index = self.run_loop.index("HAIRGPU_VISUAL_CAPTURE_BMP")
        pending_index = self.run_loop.index("m_visualCapturePending = true")
        render_index = self.run_loop.index("Render();", pending_index)
        self.assertLess(environment_index, pending_index)
        self.assertLess(pending_index, render_index)
        self.assertLess(render_index, loop_end)
        self.assertIn("outside the measured frame set", self.run_loop)

    def test_capture_occurs_before_flip_discard_present(self) -> None:
        capture_index = self.capture.index("if (m_visualCapturePending)")
        present_index = self.capture.index("m_swapChain->Present")
        self.assertLess(capture_index, present_index)
        self.assertIn("flip-discard", self.capture)

    def test_failed_formal_capture_is_not_silently_accepted(self) -> None:
        self.assertIn("capture_error.txt", self.run_loop)
        self.assertIn("runExitCode = 91", self.run_loop)
        self.assertIn("return runExitCode", self.run_loop)


class RenderDepthStateContractTests(unittest.TestCase):
    """Hair keeps read-only depth; hidden scalp occlusion must stay disabled."""

    def test_hair_depth_state_exists_without_hidden_scalp_prepass(self) -> None:
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(
            encoding="utf-8"
        )
        resources = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
        ).read_text(encoding="utf-8")
        render = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_hairDepthState;",
            header,
        )
        self.assertIn(
            "CreateDepthStencilState(&hairDepthDesc, &m_hairDepthState)",
            resources,
        )
        self.assertIn("OMSetDepthStencilState(m_hairDepthState.Get(), 0)", render)
        self.assertNotIn("m_depthOnlyBlendState", header)
        self.assertNotIn("CreateBlendState(&depthOnlyBlend", resources)
        self.assertNotIn("m_depthOnlyBlendState", render)
        self.assertIn("Do not render a hidden scalp depth pre-pass", render)


if __name__ == "__main__":
    unittest.main()
