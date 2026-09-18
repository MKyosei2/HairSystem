from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class CurrentDeploymentProofContractTests(unittest.TestCase):
    def test_build_and_presentation_runtime_markers_exist(self) -> None:
        app = (ROOT / "apps/dx11_viewer/src/AppMain.cpp").read_text(encoding="utf-8")
        render = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl"
        ).read_text(encoding="utf-8")
        shader = (
            ROOT / "apps/dx11_viewer/shaders/HairStrandInstanced.hlsl"
        ).read_text(encoding="utf-8")
        self.assertIn("HAIRGPU_BUILD_V9_20260724_FORCE_APPLIED", app)
        self.assertIn("[HAIRGPU V11 PRESENTATION-STABLE]", render)
        self.assertTrue(
            shader.lstrip().startswith(
                "// HAIRGPU_STRAND_RIBBON_VISIBILITY_V19_WEIGHTED_OIT_ROOT_COVERAGE"
            )
        )

    def test_cache_schema_is_current_and_legacy_files_are_removed(self) -> None:
        startup = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl"
        ).read_text(encoding="utf-8")
        init = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
        ).read_text(encoding="utf-8")
        self.assertIn("sample_hybrid_cache_v6_layout_g", startup)
        for legacy in (
            'name.rfind(L"sample_hybrid_cache_v1"',
            'name.rfind(L"sample_hybrid_cache_v2"',
            'name.rfind(L"sample_hybrid_cache_v3"',
            'name.rfind(L"sample_hybrid_cache_v4"',
            'name.rfind(L"sample_hybrid_cache_v5"',
        ):
            self.assertIn(legacy, startup)
        self.assertIn("cachedTopologyReport.valid", startup)
        self.assertIn("HAIRGPU_RUNTIME_IDENTITY_V9.txt", init)

    def test_release_debugger_is_normal_interactive(self) -> None:
        project = (ROOT / "HairSystem.vcxproj").read_text(encoding="utf-8")
        self.assertNotIn("--local-capture-autocommit", project)
        self.assertIn(
            "<LocalDebuggerWorkingDirectory>$(OutDir)</LocalDebuggerWorkingDirectory>",
            project,
        )

    def test_force_apply_script_verifies_and_launches_exact_release_exe(self) -> None:
        script = (
            ROOT / "tools/final_stage/FORCE_APPLY_V9_AND_LAUNCH.bat"
        ).read_text(encoding="utf-8")
        self.assertIn("/p:Configuration=Release", script)
        self.assertIn("HAIRGPU_BUILD_V9_20260724_FORCE_APPLIED", script)
        self.assertIn("HAIRGPU_RUNTIME_IDENTITY_V9.txt", script)
        self.assertIn("bin\\Release\\HairSystem\\HairSystem.exe", script)


if __name__ == "__main__":
    unittest.main()
