from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
AGGREGATOR = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer.cpp"
PARTS = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer_parts"


class ViewerFoundationSingleOwnerTests(unittest.TestCase):
    def test_recovery_part_is_not_included_when_original_owners_exist(self) -> None:
        source = AGGREGATOR.read_text(encoding="utf-8")
        self.assertNotIn("02a_required_runtime_foundation.inl", source)

    def test_original_parts_own_foundation_functions_once(self) -> None:
        sources = {
            path.name: path.read_text(encoding="utf-8")
            for path in PARTS.glob("*.inl")
            if path.name != "02a_required_runtime_foundation.inl"
        }
        joined = "\n".join(sources.values())
        for signature in (
            "GpuHairViewer::CreateUiControls()",
            "GpuHairViewer::ApplyWorkspaceMode(WorkspaceMode workspace)",
            "GpuHairViewer::ApplyGroomToolPreset(GroomTool tool)",
            "GpuHairViewer::GetWorkspaceDisplayName() const",
            "GpuHairViewer::GetRenderViewportRect() const",
            "GpuHairViewer::CompileShaderFromFile(",
            "GpuHairViewer::CreateStructuredBuffer(",
        ):
            self.assertEqual(joined.count(signature), 1, signature)


if __name__ == "__main__":
    unittest.main()
