from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def test_preflight_source_contract() -> None:
    module = load_module(
        "n01_preflight_complete",
        ROOT / "tools/final_stage/hairgpu_novelty01_preflight.py",
    )
    result = module.preflight(ROOT, require_fxc=False, telemetry=None)
    assert result["passed"], result
    assert result["checklistCount"] >= 42
    assert result["shaderEntryPointCount"] >= 47


def test_current_main_runtime_foundation_is_preserved() -> None:
    source = (
        ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
    ).read_text(encoding="utf-8")
    assert "02a_required_runtime_foundation.inl" in source
    assert "16_segment_group_adaptive_payload_complete.inl" in source
    assert "17_novelty01_formal_evidence.inl" in source
    assert "18_novelty01_render_dirty_range_consumer.inl" in source
    assert "#define Render RenderLegacy" in source
    assert "#define StepNovelty01SingleFrame StepNovelty01SingleFrameCore" in source


def test_sgapt_layout_and_feature_contract() -> None:
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
        assert marker in header


def test_proposal_only_route_and_ground_truth_solver() -> None:
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
    assert "ExperimentMethodId::P_IntegratedSegmentGroup" in runtime
    assert "ExecuteExperimentSimulationTickLegacy(dt);" in runtime
    assert "StepSimulation(dt);" in evidence
    assert "groundTruthPositions" in evidence
    assert "N01CompareGroundTruthCS" in (
        ROOT
        / "apps/dx11_viewer/shaders"
        / "HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    ).read_text(encoding="utf-8")


def test_render_dirty_range_has_a_real_consumer() -> None:
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
    assert "runtime->renderDirtyRanges.srv.Get()" in consumer
    assert "cache->renderPositions.uav.Get()" in consumer
    assert "Feature_RenderDirtyRange" in consumer
    assert "CopyResource" in consumer
    assert "RenderLegacy();" in consumer
    assert "N01CopyGuideRenderCacheCS" in shader
    assert "N01CopyFollowerRenderCacheCS" in shader
    assert "N01RenderDirtyRanges[followerIndex]" in shader


def test_payload_lifecycle_and_fallback_are_connected() -> None:
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
        assert marker in shader


def test_matrix_contains_all_controlled_variants_and_profiles() -> None:
    source = (
        ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py"
    ).read_text(encoding="utf-8")
    for item in ("B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "P"):
        assert f'"{item}"' in source
    for index in range(1, 16):
        assert f"A{index}" in source or 'range(1, 16)' in source
    for profile in ("smoke", "formal", "performance", "soak", "determinism", "gtx1660"):
        assert f'"{profile}"' in source
    assert "HAIRGPU_FORMAL_ASSET_PATH" in source
    assert "HAIRGPU_N01_RENDER_DIRTY_CACHE" in source


def test_design_scenarios_are_runtime_resolvable() -> None:
    matrix = load_module(
        "n01_matrix_complete",
        ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py",
    )
    assert set(matrix.SCENARIO_RUNTIME_NAMES) == {
        f"S{i:02d}" for i in range(1, 19)
    }
    assert len(set(matrix.SCENARIO_RUNTIME_NAMES.values())) == 18


def test_no_duplicate_overlay_tree_is_required() -> None:
    aggregator = (
        ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
    ).read_text(encoding="utf-8")
    assert "overlay/" not in aggregator
    assert "replacement/" not in aggregator
