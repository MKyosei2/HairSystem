from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = (
    ROOT
    / "apps"
    / "dx11_viewer"
    / "src"
    / "GpuHairViewer_parts"
    / "02_resource_creation_and_shader_loading.inl"
)


class LocalDebugCaptureVectorContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE.read_text(encoding="utf-8-sig")

    def test_local_capture_starts_from_a_fresh_reserved_vector(self) -> None:
        self.assertIn(
            "uint32_t localDebugCapturedFrameCount = 0u;",
            self.source,
        )
        self.assertIn("m_performanceSamples.clear();", self.source)
        self.assertIn(
            "m_performanceSamples.reserve(\n"
            "                static_cast<size_t>(localDebugCaptureFrames));",
            self.source,
        )

    def test_local_capture_range_does_not_depend_on_shared_vector_size(self) -> None:
        self.assertIn(
            "localDebugCapturedFrameCount < localDebugCaptureFrames;",
            self.source,
        )
        self.assertNotIn(
            "m_performanceSamples.size() < "
            "static_cast<size_t>(localDebugCaptureFrames)",
            self.source,
        )

    def test_local_capture_count_advances_only_for_local_measurement_frames(self) -> None:
        expected = (
            "if (localDebugCaptureCurrentFrame)\n"
            "                {\n"
            "                    ++localDebugCapturedFrameCount;\n"
            "                }"
        )
        self.assertIn(expected, self.source)

    def test_local_capture_exit_uses_dedicated_count(self) -> None:
        self.assertIn(
            "localDebugCapturedFrameCount >= localDebugCaptureFrames",
            self.source,
        )
        self.assertNotIn(
            "m_performanceSamples.size() >= "
            "static_cast<size_t>(localDebugCaptureFrames)",
            self.source,
        )

    def test_front_erase_is_excluded_only_from_local_debug_capture(self) -> None:
        self.assertIn(
            "if (!localDebugCaptureEnabled &&\n"
            "                    m_performanceSamples.size() > 12000u)",
            self.source,
        )
        self.assertNotIn(
            "!localDebugCaptureEnabled &&\n"
            "                    !IsExperimentModeEnabled()",
            self.source,
        )
        self.assertIn(
            "constexpr size_t kInteractiveTrimBatch = 1024u;",
            self.source,
        )
        self.assertNotIn(
            "m_performanceSamples.erase(m_performanceSamples.begin(), "
            "m_performanceSamples.begin() + "
            "static_cast<std::ptrdiff_t>"
            "(m_performanceSamples.size() - 12000));",
            self.source,
        )


if __name__ == "__main__":
    unittest.main()
