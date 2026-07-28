from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    import sys
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def test_preflight_source_contract() -> None:
    module = load_module(
        "n01_preflight",
        ROOT / "tools/final_stage/hairgpu_novelty01_preflight.py",
    )
    result = module.preflight(ROOT, require_fxc=False, telemetry=None)
    assert result["passed"], result
    assert result["checklistCount"] >= 42
    assert result["shaderEntryPointCount"] >= 53


def test_matrix_has_all_baselines_and_ablations() -> None:
    source = (ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py").read_text(encoding="utf-8")
    for item in ("B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "P"):
        assert f'"{item}"' in source
    for index in range(1, 16):
        assert f'A{index}' in source or 'range(1, 16)' in source
    assert '108000' in source
    assert 'GTX1660' in source
    assert 'determinism' in source.lower()


def test_hlsl_modes_and_entry_points_are_balanced() -> None:
    module = load_module(
        "n01_preflight_entries",
        ROOT / "tools/final_stage/hairgpu_novelty01_preflight.py",
    )
    shader = (ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8")
    assert shader.count("{") == shader.count("}")
    assert shader.count("(") == shader.count(")")
    for entry, mode in module.ENTRY_POINTS.items():
        assert f"void {entry}(" in shader or f"void {entry}\n" in shader
        assert f"N01_BINDING_MODE == {mode}" in shader


def test_ground_truth_uses_original_full_solver() -> None:
    source = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl").read_text(encoding="utf-8")
    assert "StepSimulation(dt);" in source
    assert "groundTruthPositions" in source
    assert "N01CompareGroundTruthCS" in (ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8")


def test_novelty01_sources_are_text_only() -> None:
    files = (
        "include/hairgpu/SegmentGroupAdaptivePayload.h",
        "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",
        "apps/dx11_viewer/src/GpuHairViewer.cpp",
        "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",
        "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl",
        "tools/final_stage/hairgpu_novelty01_preflight.py",
        "tools/final_stage/hairgpu_novelty01_matrix.py",
    )
    for relative in files:
        path = ROOT / relative
        assert path.is_file(), path
        path.read_text(encoding="utf-8")


def test_formal_class_integration_has_no_private_macro() -> None:
    aggregator = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
    header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
    assert "#define private" not in aggregator
    assert "struct Novelty01RuntimeState;" in header
    assert "std::unique_ptr<Novelty01RuntimeState> m_novelty01Runtime" in header


def test_render_dirty_range_is_connected_to_renderer() -> None:
    runtime = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
    renderer = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl").read_text(encoding="utf-8")
    shader = (ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8")
    assert "renderPositionCache" in runtime
    assert "DrawInstancedIndirect" in runtime
    assert "GetNovelty01RenderPositionSrv()" in renderer
    assert "DrawNovelty01HairIndirect(" in renderer
    assert "N01UpdateFollowerRenderCacheCS" in shader


def test_design_scenarios_are_runtime_resolvable() -> None:
    import ast
    source = (ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py").read_text(encoding="utf-8")
    tree = ast.parse(source)
    mapping = None
    for node in tree.body:
        if isinstance(node, ast.Assign):
            names = [target.id for target in node.targets if isinstance(target, ast.Name)]
            if "SCENARIO_RUNTIME_NAMES" in names:
                mapping = ast.literal_eval(node.value)
                break
    assert mapping is not None
    assert set(mapping) == {f"S{i:02d}" for i in range(1, 19)}
    assert len(set(mapping.values())) == 18
    assert '"5" if scenario == "S17" else "50"' in source
    assert "HAIRGPU_N01_DESIGN_SCENARIO" in source


def test_scenario_fault_injection_is_connected() -> None:
    cpp = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
    shader = (ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8")
    for marker in ("S08 Single-strand edit", "S09 Continuous brush edit",
                   "S12 Static/dynamic boundary stress", "S13 State thrashing stress",
                   "S14 Multi-guide follower dependency", "S17 Payload pool stress",
                   "S18 Version/descriptor fault injection"):
        assert marker in cpp
    assert "N01InjectDesignScenarioCS" in shader
    assert "gPayloadCapacity + 17u" in shader


def test_false_dynamic_ground_truth_metric_is_exported() -> None:
    header = (ROOT / "include/hairgpu/SegmentGroupAdaptivePayload.h").read_text(encoding="utf-8")
    shader = (ROOT / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").read_text(encoding="utf-8")
    runtime = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
    assert "Counter_FalseDynamic = 31u" in header
    assert "N01_COUNTER_FALSE_DYNAMIC = 31u" in shader
    assert "groundTruthWouldPermitStatic" in shader
    assert "false_dynamic_samples" in runtime
