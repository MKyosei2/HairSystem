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
    def test_preflight_source_contract(self) -> None:
        module = load_module(
            "n01_preflight_complete",
            ROOT / "tools/final_stage/hairgpu_novelty01_preflight.py",
        )
        result = module.preflight(
            ROOT,
            require_fxc=False,
            telemetry=None,
        )
        self.assertTrue(result["passed"], result)
        self.assertGreaterEqual(result["checklistCount"], 46)
        self.assertGreaterEqual(result["shaderEntryPointCount"], 50)

    def test_current_main_runtime_foundation_is_preserved(self) -> None:
        source = (
            ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
        ).read_text(encoding="utf-8")
        for marker in (
            "02a_required_runtime_foundation.inl",
            "16_segment_group_adaptive_payload_complete.inl",
            "17_novelty01_formal_evidence.inl",
            "18_novelty01_render_dirty_range_consumer.inl",
            "#define Render RenderLegacy",
            "#define StepNovelty01SingleFrame StepNovelty01SingleFrameCore",
        ):
            self.assertIn(marker, source)

    def test_sgapt_layout_and_feature_contract(self) -> None:
        header = (
            ROOT / "include/hairgpu/SegmentGroupAdaptivePayload.h"
        ).read_text(encoding="utf-8")
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

    def test_proposal_only_route_and_ground_truth_solver(self) -> None:
        runtime = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts"
            / "16_segment_group_adaptive_payload_complete.inl"
        ).read_text(encoding="utf-8")
        evidence = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts"
            / "17_novelty01_formal_evidence.inl"
        ).read_text(encoding="utf-8")
        shader = (
            ROOT
            / "apps/dx11_viewer/shaders"
            / "HairSegmentGroupAdaptivePayloadRuntime.hlsl"
        ).read_text(encoding="utf-8")
        self.assertIn(
            "ExperimentMethodId::P_IntegratedSegmentGroup",
            runtime,
        )
        self.assertIn(
            "ExecuteExperimentSimulationTickLegacy(dt);",
            runtime,
        )
        self.assertIn("StepSimulation(dt);", evidence)
        self.assertIn("groundTruthPositions", evidence)
        self.assertIn("N01CompareGroundTruthCS", shader)

    def test_render_dirty_range_has_a_real_consumer(self) -> None:
        consumer = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts"
            / "18_novelty01_render_dirty_range_consumer.inl"
        ).read_text(encoding="utf-8")
        shader = (
            ROOT
            / "apps/dx11_viewer/shaders"
            / "HairNovelty01RenderDirtyCache.hlsl"
        ).read_text(encoding="utf-8")
        for marker in (
            "runtime->renderDirtyRanges.srv.Get()",
            "cache->renderPositions.uav.Get()",
            "Feature_RenderDirtyRange",
            "CopyResource",
            "RenderLegacy();",
        ):
            self.assertIn(marker, consumer)
        for marker in (
            "N01CopyGuideRenderCacheCS",
            "N01CopyFollowerRenderCacheCS",
            "N01RenderDirtyRanges[followerIndex]",
        ):
            self.assertIn(marker, shader)

    def test_payload_lifecycle_and_fallback_are_connected(self) -> None:
        shader = (
            ROOT
            / "apps/dx11_viewer/shaders"
            / "HairSegmentGroupAdaptivePayloadRuntime.hlsl"
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

    def test_matrix_contains_all_controlled_variants_and_profiles(self) -> None:
        source = (
            ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py"
        ).read_text(encoding="utf-8")
        for item in (
            "B0", "B1", "B2", "B3", "B4",
            "B5", "B6", "B7", "B8", "P",
        ):
            self.assertIn(f'"{item}"', source)
        for index in range(1, 16):
            self.assertTrue(
                f"A{index}" in source or "range(1, 16)" in source
            )
        for profile in (
            "smoke",
            "formal",
            "performance",
            "soak",
            "determinism",
            "gtx1660",
        ):
            self.assertIn(f'"{profile}"', source)
        self.assertIn("HAIRGPU_FORMAL_ASSET_PATH", source)
        self.assertIn("HAIRGPU_N01_RENDER_DIRTY_CACHE", source)

    def test_design_scenarios_are_runtime_resolvable(self) -> None:
        matrix = load_module(
            "n01_matrix_complete",
            ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py",
        )
        self.assertEqual(
            set(matrix.SCENARIO_RUNTIME_NAMES),
            {f"S{i:02d}" for i in range(1, 19)},
        )
        self.assertEqual(
            len(set(matrix.SCENARIO_RUNTIME_NAMES.values())),
            18,
        )

    def test_no_duplicate_overlay_tree_is_required(self) -> None:
        aggregator = (
            ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
        ).read_text(encoding="utf-8")
        self.assertNotIn("overlay/", aggregator)
        self.assertNotIn("replacement/", aggregator)


if __name__ == "__main__":
    unittest.main()
