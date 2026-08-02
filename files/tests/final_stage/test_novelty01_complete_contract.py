from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class Novelty01CompleteContractTests(unittest.TestCase):
    def test_preflight_contract_covers_both_shader_families(self) -> None:
        module = load_module(
            "n01_preflight_complete",
            ROOT / "tools/final_stage/hairgpu_novelty01_preflight.py",
        )
        self.assertGreaterEqual(len(module.CHECKLIST), 47)
        self.assertEqual(len(module.ENTRY_POINTS), 50)
        self.assertEqual(
            set(module.RENDER_CACHE_ENTRY_POINTS),
            {"N01CopyGuideRenderCacheCS", "N01CopyFollowerRenderCacheCS"},
        )
        for obsolete in (
            "overlay",
            "replacement",
            "tools/final_stage/apply_formal_asset_binding_patch.py",
            "patches/hairgpu_final_run_formal_asset_binding.patch",
        ):
            self.assertIn(obsolete, module.OBSOLETE_PATHS)

    def test_current_main_runtime_foundation_is_preserved(self) -> None:
        source = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
        for marker in (
            "02a_required_runtime_foundation.inl",
            "16_segment_group_adaptive_payload_complete.inl",
            "17_novelty01_formal_evidence.inl",
            "18_novelty01_render_dirty_range_consumer.inl",
            "#define Render RenderLegacy",
            "#define StepNovelty01SingleFrame StepNovelty01SingleFrameCore",
        ):
            self.assertIn(marker, source)

    def test_formal_header_integration_replaces_private_macro_hack(self) -> None:
        source = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        self.assertNotIn("#define private", source)
        self.assertNotIn("#undef private", source)
        self.assertIn('#include "GpuHairViewer.h"', source)
        for marker in (
            "struct Novelty01RuntimeState;",
            "struct Novelty01RenderCacheState;",
            "void ExecuteNovelty01ExperimentTick(float dt);",
            "void StepNovelty01SingleFrame();",
            "void RenderLegacy();",
            "void ShutdownNovelty01Routed();",
        ):
            self.assertIn(marker, header)

    def test_sgapt_layout_and_feature_contract(self) -> None:
        header = (ROOT / "include/hairgpu/SegmentGroupAdaptivePayload.h").read_text(encoding="utf-8")
        for marker in (
            "struct alignas(16) SegmentGroupCore",
            "struct alignas(16) CoreExtension",
            "struct alignas(16) StaticCache",
            "struct alignas(16) DynamicPayload",
            "struct alignas(16) PayloadOwner",
            "Feature_OptionalPayload",
            "Feature_FixedGroupIndex",
            "Feature_DelayedPayloadReuse",
            "Feature_FollowerDependency",
            "Feature_RenderDirtyRange",
            "Counter_PayloadOwnerMismatch",
            "Counter_DuplicatePayloadOwner",
            "Counter_InvalidPayloadIndex",
            "Counter_NaN",
            "Counter_Inf",
            "Counter_SafeStop",
        ):
            self.assertIn(marker, header)

    def test_proposal_and_independent_baseline_routes(self) -> None:
        runtime = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl"
        ).read_text(encoding="utf-8")
        evidence = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl"
        ).read_text(encoding="utf-8")
        matrix = (
            ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py"
        ).read_text(encoding="utf-8")
        contract = (
            ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json"
        ).read_text(encoding="utf-8")
        shader = (
            ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
        ).read_text(encoding="utf-8")
        self.assertIn("ExperimentMethodId::P_IntegratedSegmentGroup", runtime)
        self.assertIn("ExecuteExperimentSimulationTickLegacy(dt);", runtime)
        self.assertIn('baseline_contract.get("method", "P")', matrix)
        self.assertIn('"B1"', contract)
        self.assertIn('"method": "B1"', contract)
        self.assertIn('"B8"', contract)
        self.assertIn('"method": "B8"', contract)
        self.assertIn("StepSimulation(dt);", evidence)
        self.assertIn("groundTruthPositions", evidence)
        self.assertIn("N01CompareGroundTruthCS", shader)

    def test_render_dirty_range_has_a_bounded_real_consumer(self) -> None:
        consumer = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl"
        ).read_text(encoding="utf-8")
        shader = (
            ROOT / "apps/dx11_viewer/shaders/HairNovelty01RenderDirtyCache.hlsl"
        ).read_text(encoding="utf-8")
        for marker in (
            "runtime->renderDirtyRanges.srv.Get()",
            "cache->renderPositions.uav.Get()",
            "Feature_RenderDirtyRange",
            "CopyResource",
            "RenderLegacy();",
            "N01DispatchGroups(followerPointCount, 256u)",
            "D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION",
        ):
            self.assertIn(marker, consumer)
        for marker in (
            "N01CopyGuideRenderCacheCS",
            "N01CopyFollowerRenderCacheCS",
            "N01RenderDirtyRanges[followerIndex]",
            "SV_DispatchThreadID",
            "linearPointIndex / gParticlesPerStrand",
        ):
            self.assertIn(marker, shader)
        self.assertNotIn(
            "m_context->Dispatch(\n            m_asset.followerStrandCount",
            consumer,
        )

    def test_payload_lifecycle_and_fallback_are_connected(self) -> None:
        shader = (
            ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
        ).read_text(encoding="utf-8")
        for marker in (
            "N01ValidatePayloadOwnersCS",
            "N01WritePayloadOwnersCS",
            "N01RetirePayloadOwnersCS",
            "safeReuseEpoch",
            "N01_COUNTER_PAYLOAD_OVERFLOW",
            "N01_CORE_BASELINE_FALLBACK",
            "N01_COUNTER_DUPLICATE_PAYLOAD_OWNER",
            "N01_COUNTER_INVALID_PAYLOAD_INDEX",
        ):
            self.assertIn(marker, shader)

    def test_direct_formal_runner_owns_asset_binding(self) -> None:
        source = (ROOT / "tools/final_stage/hairgpu_final_run.py").read_text(encoding="utf-8")
        for marker in (
            'asset_manifest.get("_resolvedMembers", {})',
            '"HAIRGPU_FORMAL_ASSET_PATH": formal_asset_path',
            '"HAIRGPU_FORMAL_ASSET_SHA256": formal_asset_sha256',
            '"HAIRGPU_FORMAL_TOPOLOGY_SHA256": formal_topology_sha256',
            '"HAIRGPU_GENERATOR_SETTINGS_SHA256": generator_settings_sha256',
        ):
            self.assertIn(marker, source)
        self.assertNotIn("apply_formal_asset_binding_patch", source)

    def test_workflows_are_real_root_workflows_without_runtime_patchers(self) -> None:
        for relative in (
            ".github/workflows/windows-build-test.yml",
            ".github/workflows/novelty01-formal-gate.yml",
        ):
            path = ROOT / relative
            self.assertTrue(path.is_file(), relative)
            text = path.read_text(encoding="utf-8")
            self.assertIn("actions/checkout@v4", text)
            self.assertIn("fetch-depth: 0", text)
            for forbidden in (
                "path.write_text(text",
                "apply_viewer_dedup.py",
                "replacement/",
                "overlay/",
            ):
                self.assertNotIn(forbidden, text)

    def test_duplicate_package_trees_and_obsolete_patchers_are_removed(self) -> None:
        for relative in (
            "overlay",
            "replacement",
            "tools/apply_novelty01_69_3_complete.py",
            "tools/apply_novelty01_69_4_complete.py",
            "tools/apply_novelty01_complete.py",
            "tools/apply_novelty01_sgapt.py",
            "tools/final_stage/apply_formal_asset_binding_patch.py",
            "patches/hairgpu_final_run_formal_asset_binding.patch",
        ):
            self.assertFalse((ROOT / relative).exists(), relative)


if __name__ == "__main__":
    unittest.main()
