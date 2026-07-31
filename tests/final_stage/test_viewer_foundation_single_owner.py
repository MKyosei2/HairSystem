from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
AGGREGATOR = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer.cpp"
PARTS = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer_parts"
RECOVERY = PARTS / "02a_required_runtime_foundation.inl"

SIGNATURES = (
    "GpuHairViewer::CreateUiControls()",
    "GpuHairViewer::ApplyWorkspaceMode(WorkspaceMode workspace)",
    "GpuHairViewer::ApplyGroomToolPreset(GroomTool tool)",
    "GpuHairViewer::GetWorkspaceDisplayName() const",
    "GpuHairViewer::GetRenderViewportRect() const",
    "GpuHairViewer::CompileShaderFromFile(",
    "GpuHairViewer::CreateStructuredBuffer(",
)


class ViewerFoundationSingleOwnerTests(unittest.TestCase):
    def test_recovery_part_is_included_once(self) -> None:
        source = AGGREGATOR.read_text(encoding="utf-8")
        include = (
            '#include '
            '"GpuHairViewer_parts/02a_required_runtime_foundation.inl"'
        )
        self.assertEqual(source.count(include), 1)

    def test_recovery_part_owns_each_foundation_function_once(self) -> None:
        source = RECOVERY.read_text(encoding="utf-8")
        for signature in SIGNATURES:
            self.assertEqual(source.count(signature), 1, signature)

    def test_other_parts_do_not_duplicate_foundation_functions(self) -> None:
        sources = {
            path.name: path.read_text(encoding="utf-8")
            for path in PARTS.glob("*.inl")
            if path.name != RECOVERY.name
        }
        joined = "\n".join(sources.values())

        for signature in SIGNATURES:
            self.assertEqual(joined.count(signature), 0, signature)


if __name__ == "__main__":
    unittest.main()
