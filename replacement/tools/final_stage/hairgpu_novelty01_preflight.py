from __future__ import annotations

"""Strict source/runtime preflight for Novelty 01 SG-APT.

This checker intentionally fails closed. A missing source marker, an unknown Main
layout, a shader entry-point mismatch, a malformed formal telemetry file, or an
unavailable required Windows compiler produces a non-zero exit code.
"""

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable

BASE_MAIN_COMMIT = "8d19bb546f3505fe25436d6ffa5b390a9b83897a"
SOURCE_COMMIT = "a0521e258acdbd9d71262172d6d2ba208c338eeb"
SCHEMA = "novelty01-preflight-v4"

REQUIRED_FILES = (
    "include/hairgpu/SegmentGroupAdaptivePayload.h",
    "include/hairgpu/HairNoveltyTelemetry.h",
    "apps/dx11_viewer/src/GpuHairViewer.h",
    "HairSystem.vcxproj",
    "apps/dx11_viewer/src/GpuHairViewer_parts/15_shutdown_and_tail.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl",
    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",
    "apps/dx11_viewer/src/GpuHairViewer.cpp",
    "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl",
    "tools/final_stage/hairgpu_novelty01_preflight.py",
    "tools/final_stage/hairgpu_novelty01_matrix.py",
    "tools/final_stage/hairgpu_novelty01_report.py",
    "tools/final_stage/hairgpu_novelty01_resource_audit.py",
    "tools/final_stage/hairgpu_novelty01_gate.py",
    "tools/final_stage/hairgpu_novelty01_formal_suite.py",
    "tools/final_stage/Run_Novelty01_Formal_Matrix_And_Gate.bat",
    "tools/final_stage/Run_Novelty01_Complete_Verification.bat",
    ".github/workflows/novelty01-formal-gate.yml",
    "tests/final_stage/test_novelty01_complete_contract.py",
    "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json",
    "docs/experiments/NOVELTY01_IMPLEMENTATION_TRACEABILITY.json",
    "docs/experiments/NOVELTY01_RESOURCE_BINDING_CONTRACT.md",
    "docs/experiments/NOVELTY01_SAFE_EVALUATION_PROTOCOL.md",
    "docs/sample_assets/novelty01_frozen_parameters.json",
    "設計書/01_TopNovelty_NewStrandDataType.md",
)

# entry point -> N01_BINDING_MODE
ENTRY_POINTS = {
    "N01InitializeCS": 0,
    "N01BuildBlocksCS": 0,
    "N01InvalidateBlocksCS": 0,
    "N01DescribeClassifyCS": 0,
    "N01ApplyTransitionsFinalFlagsCS": 0,
    "N01FinalizeBlocksCS": 0,
    "N01ScanLocalCS": 1,
    "N01ScanBlocksCS": 1,
    "N01ScatterListsCS": 1,
    "N01BuildSleepingIdentityListCS": 1,
    "N01CarryNonOptionalPayloadsCS": 2,
    "N01RemapPayloadCS": 2,
    "N01ApplyStaticCS": 2,
    "N01RepairCS": 2,
    "N01UpdateCacheCS": 2,
    "N01ShadowValidateCS": 2,
    "N01ClearDynamicGuideLambdasCS": 3,
    "N01IntegrateDynamicGuidesCS": 3,
    "N01DistanceDynamicGuidesCS": 3,
    "N01BendDynamicGuidesCS": 3,
    "N01CollisionDynamicGuidesCS": 3,
    "N01StabilityGuardDynamicGuidesCS": 3,
    "N01IntegrateDynamicCS": 3,
    "N01DistanceConstraintCS": 3,
    "N01BendConstraintCS": 3,
    "N01CollisionCS": 3,
    "N01ClearHashCS": 3,
    "N01BuildHashCS": 3,
    "N01SelfCollisionCS": 3,
    "N01VelocityRecoveryCS": 3,
    "N01WatchdogCS": 3,
    "N01RootBindCS": 4,
    "N01FollowerLocalCS": 4,
    "N01BuildFollowerFlagsCS": 5,
    "N01BuildSharedCandidatesCS": 6,
    "N01ResolveSharedPointsCS": 6,
    "N01ValidatePayloadOwnersCS": 7,
    "N01WritePayloadOwnersCS": 7,
    "N01RetirePayloadOwnersCS": 7,
    "N01ClearGuideDirtyRangesCS": 8,
    "N01BuildGuideDirtyRangesCS": 8,
    "N01PropagateFollowerRenderRangesCS": 8,
    "N01FinalizeGuideDirtyRangeCountersCS": 8,
    "N01CompareGroundTruthCS": 9,
    "N01VerifyInvariantsCS": 9,
    "N01BuildIndirectArgumentsCS": 10,
    "N01MigrateCoreBuffersCS": 10,
    "N01ClearAtomicCompactionCountersCS": 11,
    "N01AtomicCompactCS": 11,
    "N01InjectDesignScenarioCS": 12,
    "N01InitializeRenderCacheCS": 13,
    "N01UpdateGuideRenderCacheCS": 13,
    "N01UpdateFollowerRenderCacheCS": 13,
}

# Design section 46, represented as test id -> markers which jointly prove the
# implementation is connected rather than merely mentioned in comments.
CHECKLIST = {
    "fixed_core_index": ("Feature_FixedGroupIndex", "SegmentGroupCore", "ownerGroupIndex"),
    "separate_dynamic_payload": ("DynamicPayload", "payloads[", "Feature_OptionalPayload"),
    "static_invalid_payload": ("payloadIndex = N01_INVALID", "N01_CORE_PAYLOAD_VALID"),
    "dynamic_only_payload_access": ("N01RemapPayloadCS", "extension.payloadIndex", "N01Payloads[payloadIndex]"),
    "promote_to_dynamic": ("N01ApplyTransitionsFinalFlagsCS", "N01_STATE_FORCED_DYNAMIC"),
    "demote_to_static": ("N01_STATE_STATIC_REUSE", "N01RetirePayloadOwnersCS"),
    "final_shape_saved": ("N01UpdateCacheCS", "cache.rootLocalEnd"),
    "epoch_or_fence_release": ("payloadRetireDelayFrames", "safeReuseEpoch", "kPayloadGenerationCount"),
    "pool_overflow_fallback": ("Counter_PayloadOverflow", "N01_CORE_BASELINE_FALLBACK"),
    "version_rejects_cache": ("Feature_VersionCheck", "N01_DIRTY_TOPOLOGY", "dependencyVersion"),
    "rigid_root_transform": ("N01GetRootFrame", "N01FromFrameLocal", "currentTangent"),
    "no_index_scaled_xyz": ("rootLocalEnd", "N01FromFrameLocal"),
    "hysteresis": ("Feature_Hysteresis", "gStaticPositionThresholdMm", "gDynamicPositionThresholdMm"),
    "minimum_residence": ("Feature_MinimumResidence", "minimumResidenceEndFrame"),
    "transition_budget": ("Feature_TransitionBudget", "gStaticTransitionBudget"),
    "hard_soft_split": ("hardFlags", "softFlags", "Feature_HardSoftDescriptor"),
    "positional_units": ("velocityDisplacementMm", "accelerationDisplacementMm", "rootRotationDisplacementMm"),
    "descriptor_age": ("ageFrames", "gDescriptorMaximumAge"),
    "incremental_block_invalidation": ("N01InvalidateBlocksCS", "Feature_IncrementalBlocks"),
    "root_not_all_dirty": ("gSweptRootBoundsMin", "N01AabbIntersects"),
    "block_skip_dispatch": ("Counter_BlockSkipped", "block.validFlags"),
    "shared_single_writer": ("N01ResolveSharedPointsCS", "targetPointIndex"),
    "stable_reduce": ("N01CandidateBetter", "sourceGroupIndex <"),
    "baseline_equivalent_solver": ("Feature_BaselineEquivalentSolve", "N01IntegrateDynamicGuidesCS", "N01DistanceDynamicGuidesCS", "N01BendDynamicGuidesCS", "N01CollisionDynamicGuidesCS", "RunNovelty01GroundTruth", "StepSimulation(dt)"),
    "follower_dependency_precomputed": ("followerOffsets", "followerEntries", "dependencyVersion"),
    "follower_local_reconstruct": ("N01FollowerLocalCS", "FollowerDirtyRange"),
    "render_dirty_range": ("RenderDirtyRange", "Counter_RenderRegeneratedVertices", "N01UpdateFollowerRenderCacheCS", "DrawNovelty01HairIndirect"),
    "measured_fast_path": ("proposalMillisecondsEwma", "fullDynamicMillisecondsEwma", "Feature_FullDynamicFastPath"),
    "watchdog_fallback": ("N01WatchdogCS", "N01_CORE_BASELINE_FALLBACK"),
    "ground_truth": ("N01CompareGroundTruthCS", "RunNovelty01GroundTruth"),
    "shadow_dynamic": ("N01ShadowValidateCS", "Feature_ShadowDynamic"),
    "false_static_metric": ("Counter_GroundTruthFalseStatic", "FalseStaticGroundTruth"),
    "false_dynamic_metric": ("Counter_FalseDynamic", "N01_COUNTER_FALSE_DYNAMIC", "false_dynamic_samples"),
    "payload_usage_metric": ("Counter_PayloadActive", "Counter_PayloadAllocated"),
    "byte_metric": ("Counter_GroupReadBytes", "Counter_PayloadWriteBytes"),
    "transition_metric": ("Counter_Transitions", "Counter_TransitionBytes"),
    "follower_points_metric": ("Counter_FollowerRebuiltPoints",),
    "render_vertices_metric": ("Counter_RenderRegeneratedVertices",),
    "B2_B8": ("B2_SleepingFlag", "B8_StrongestPractical"),
    "A1_A15": ("A1_NoOptionalPayload", "A15_NoStableCompaction"),
    "thirty_minute": ("108000", "soak"),
    "determinism": ("determinism", "stateSequenceHash", "compactionSequenceHash"),
    "gtx1660": ("GTX1660", "hardware_profile"),
    "design_scenarios": ("ApplyNovelty01DesignScenarioForTick", "N01InjectDesignScenarioCS", "S17 Payload pool stress", "S18 Version/descriptor fault injection"),
    "independent_baseline_routes": ("ExecuteNovelty01IndependentBaseline", "IndependentFullDynamicB1", "IndependentStrongestPracticalB8", "n01UsesSgaptRepresentation"),
    "variant_contract_guard": ("VariantExecutionContract", "VariantContractAcceptsFeatureMask", "N01ValidateExecutionContract"),
    "formal_merge_gate": ("hairgpu_novelty01_gate.py", "novelty01-formal-gate.yml", "fxc_entrypoints.json"),
    "formal_suite_orchestration": ("hairgpu_novelty01_formal_suite.py", "novelty01_determinism_summary.json", "novelty01_soak_summary.json"),
    "performance_export_trace": ("sample.n01ExecutionRoute", "n01UsesSgaptRepresentation", "ExportPerformanceJsonl"),
}

FORBIDDEN_PATTERNS = (
    r"\bTODO\b",
    r"\bFIXME\b",
    r"not\s+implemented",
    r"throw\s+std::logic_error\s*\(\s*\".*not",
)

@dataclass
class Finding:
    severity: str
    code: str
    message: str
    path: str = ""


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def balanced(text: str, left: str, right: str) -> bool:
    return text.count(left) == text.count(right)


def run(command: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, check=False)


def find_fxc() -> str | None:
    explicit = os.environ.get("FXC")
    if explicit and Path(explicit).is_file():
        return explicit
    return shutil.which("fxc.exe") or shutil.which("fxc")


def validate_shader_contract(root: Path, findings: list[Finding]) -> None:
    header_path = root / "include/hairgpu/SegmentGroupAdaptivePayload.h"
    shader_path = root / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    runtime_path = root / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl"
    if not (header_path.is_file() and shader_path.is_file() and runtime_path.is_file()):
        return

    header = read(header_path)
    shader = read(shader_path)
    runtime = read(runtime_path)

    actual_entries = re.findall(r"\bvoid\s+(N01[A-Za-z0-9_]+CS)\s*\(", shader)
    duplicate_entries = sorted({name for name in actual_entries if actual_entries.count(name) > 1})
    if duplicate_entries:
        findings.append(Finding(
            "error", "E_ENTRYPOINT_DUPLICATE",
            "Duplicate shader entry points: " + ", ".join(duplicate_entries),
            str(shader_path)))
    actual_set = set(actual_entries)
    mapped_set = set(ENTRY_POINTS)
    if actual_set != mapped_set:
        findings.append(Finding(
            "error", "E_ENTRYPOINT_MAP",
            "Entry-point map mismatch. Missing from map: " +
            ", ".join(sorted(actual_set - mapped_set)) +
            "; stale in map: " + ", ".join(sorted(mapped_set - actual_set)),
            str(shader_path)))

    loaded_set = set(re.findall(r'load\("(N01[A-Za-z0-9_]+CS)"', runtime))
    if loaded_set != actual_set:
        findings.append(Finding(
            "error", "E_ENTRYPOINT_LOAD",
            "Runtime shader-load mismatch. Missing loads: " +
            ", ".join(sorted(actual_set - loaded_set)) +
            "; unknown loads: " + ", ".join(sorted(loaded_set - actual_set)),
            str(runtime_path)))

    for mode in range(14):
        match = re.search(
            rf"#if N01_BINDING_MODE == {mode}\n(.*?)(?=\n#endif // mode {mode})",
            shader, flags=re.DOTALL)
        if not match:
            findings.append(Finding(
                "error", "E_BINDING_MODE", f"Missing binding mode {mode}.",
                str(shader_path)))
            continue
        body = match.group(1)
        uav_registers = [int(value) for value in re.findall(r"register\(u(\d+)\)", body)]
        srv_registers = [int(value) for value in re.findall(r"register\(t(\d+)\)", body)]
        if len(uav_registers) != len(set(uav_registers)):
            findings.append(Finding(
                "error", "E_UAV_REGISTER_DUPLICATE",
                f"Binding mode {mode} contains duplicate UAV registers.",
                str(shader_path)))
        if len(srv_registers) != len(set(srv_registers)):
            findings.append(Finding(
                "error", "E_SRV_REGISTER_DUPLICATE",
                f"Binding mode {mode} contains duplicate SRV registers.",
                str(shader_path)))
        if uav_registers and max(uav_registers) > 7:
            findings.append(Finding(
                "error", "E_D3D11_UAV_LIMIT",
                f"Binding mode {mode} exceeds the D3D11 eight-UAV limit.",
                str(shader_path)))

    cpp_match = re.search(
        r"struct\s+alignas\(16\)\s+RuntimeConstants\s*\{(.*?)\n\s*\};",
        header, flags=re.DOTALL)
    hlsl_match = re.search(
        r"cbuffer\s+Novelty01Constants\s*:\s*register\(b0\)\s*\{(.*?)\n\};",
        shader, flags=re.DOTALL)
    if not cpp_match or not hlsl_match:
        findings.append(Finding(
            "error", "E_CONSTANT_BUFFER_DECLARATION",
            "RuntimeConstants or Novelty01Constants is missing.",
            str(shader_path)))
    else:
        cpp_names: list[str] = []
        for raw in cpp_match.group(1).splitlines():
            declaration = re.match(
                r"(?:uint32_t|float)\s+([A-Za-z_][A-Za-z0-9_]*)"
                r"(?:\[[^\]]+\])?(?:\[[^\]]+\])?\s*(?:=.*)?;",
                raw.strip())
            if declaration:
                cpp_names.append(declaration.group(1))
        hlsl_names: list[str] = []
        for raw in hlsl_match.group(1).splitlines():
            declaration = re.match(
                r"(?:uint|float|float2|float3|float4|uint4)\s+"
                r"(g[A-Za-z_][A-Za-z0-9_]*)(?:\[[^\]]+\])?\s*;",
                raw.strip())
            if declaration:
                name = declaration.group(1)[1:]
                hlsl_names.append(name[:1].lower() + name[1:])
        if cpp_names != hlsl_names:
            mismatch = next((
                index for index, pair in enumerate(zip(cpp_names, hlsl_names))
                if pair[0] != pair[1]), min(len(cpp_names), len(hlsl_names)))
            findings.append(Finding(
                "error", "E_CONSTANT_BUFFER_LAYOUT",
                f"C++/HLSL constant-buffer field order differs at index {mismatch}.",
                str(shader_path)))

    counter_match = re.search(
        r"enum\s+CounterIndex\s*:\s*uint32_t\s*\{(.*?)\n\s*\};",
        header, flags=re.DOTALL)
    if not counter_match:
        findings.append(Finding(
            "error", "E_COUNTER_ENUM", "CounterIndex is missing.",
            str(header_path)))
    else:
        counter_values = [
            int(value) for _, value in re.findall(
                r"(Counter_[A-Za-z0-9_]+)\s*=\s*([0-9]+)u",
                counter_match.group(1))
        ]
        if counter_values != list(range(64)):
            findings.append(Finding(
                "error", "E_COUNTER_LAYOUT",
                "CounterIndex must contain exactly the contiguous range 0..63.",
                str(header_path)))

    missing_features = sorted({
        token for token in re.findall(r"\bFeature_[A-Za-z0-9_]+\b", shader)
        if token not in header
    })
    if missing_features:
        findings.append(Finding(
            "error", "E_FEATURE_CONTRACT",
            "Shader uses undeclared C++ feature bits: " + ", ".join(missing_features),
            str(shader_path)))


def validate_formal_contract(root: Path, findings: list[Finding]) -> None:
    contract_path = root / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json"
    frozen_path = root / "docs/sample_assets/novelty01_frozen_parameters.json"
    matrix_path = root / "tools/final_stage/hairgpu_novelty01_matrix.py"
    runtime_path = root / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl"
    project_path = root / "HairSystem.vcxproj"
    design_path = root / "設計書/01_TopNovelty_NewStrandDataType.md"
    if not all(path.is_file() for path in (contract_path, frozen_path, matrix_path, runtime_path, project_path, design_path)):
        return
    try:
        contract = json.loads(read(contract_path))
        frozen = json.loads(read(frozen_path))
    except Exception as exc:
        findings.append(Finding("error", "E_FORMAL_CONTRACT_JSON", str(exc), str(contract_path)))
        return
    if contract.get("schema") != "hairgpu-novelty01-formal-contract-v1":
        findings.append(Finding("error", "E_FORMAL_CONTRACT_SCHEMA", "Unexpected formal contract schema.", str(contract_path)))
    expected_baselines = {f"B{i}" for i in range(9)} | {"P"}
    baselines = contract.get("baselines")
    if not isinstance(baselines, dict) or set(baselines) != expected_baselines:
        findings.append(Finding("error", "E_BASELINE_CONTRACT", "Baseline contract must contain B0-B8 and P exactly.", str(contract_path)))
    expected_scenarios = {f"S{i:02d}" for i in range(1, 19)}
    scenarios = contract.get("scenarios")
    if not isinstance(scenarios, dict) or set(scenarios) != expected_scenarios:
        findings.append(Finding("error", "E_SCENARIO_CONTRACT", "Scenario contract must contain S01-S18 exactly.", str(contract_path)))
    b8 = baselines.get("B8", {}) if isinstance(baselines, dict) else {}
    required_b8_disabled = {
        "OptionalPayload", "FixedGroupIndex", "PayloadOwnerCheck",
        "DelayedPayloadReuse", "TransitionBudget", "HardSoftDescriptor"}
    if not required_b8_disabled.issubset(set(b8.get("disabled", []))):
        findings.append(Finding("error", "E_B8_CONTRACT", "B8 still contains SG-APT representation features.", str(contract_path)))

    parameters = frozen.get("parameters")
    if not isinstance(parameters, dict):
        findings.append(Finding("error", "E_FROZEN_PARAMETERS", "Frozen parameter table is missing.", str(frozen_path)))
        return
    required_parameters = {
        "groupsPerBlock", "payloadCapacityPercent", "payloadGenerationCount",
        "payloadRetireDelayFrames", "descriptorMaximumAge",
        "minimumResidenceFrames", "consecutiveStaticFrames",
        "staticTransitionBudget", "staticPositionThresholdMm",
        "dynamicPositionThresholdMm", "staticVelocityThresholdMm",
        "dynamicVelocityThresholdMm", "boundaryErrorThresholdMm",
        "collisionSafetyPaddingMm", "shadowErrorThresholdMm",
        "fullDynamicEnterRatio", "fullDynamicExitRatio",
        "fullDynamicDecisionFrames", "soakMaximumMemoryGrowthMiB"}
    missing = required_parameters - set(parameters)
    if missing:
        findings.append(Finding("error", "E_FROZEN_PARAMETERS", "Missing frozen parameters: " + ", ".join(sorted(missing)), str(frozen_path)))
    try:
        if float(parameters["dynamicPositionThresholdMm"]["value"]) <= float(parameters["staticPositionThresholdMm"]["value"]):
            raise ValueError("position hysteresis is invalid")
        if float(parameters["dynamicVelocityThresholdMm"]["value"]) <= float(parameters["staticVelocityThresholdMm"]["value"]):
            raise ValueError("velocity hysteresis is invalid")
        if float(parameters["fullDynamicEnterRatio"]["value"]) <= float(parameters["fullDynamicExitRatio"]["value"]):
            raise ValueError("fast-path hysteresis is invalid")
        if int(parameters["payloadRetireDelayFrames"]["value"]) < int(parameters["payloadGenerationCount"]["value"]):
            raise ValueError("payload retire delay is too short")
    except Exception as exc:
        findings.append(Finding("error", "E_FROZEN_HYSTERESIS", str(exc), str(frozen_path)))

    runtime = read(runtime_path)
    matrix = read(matrix_path)
    project = read(project_path)
    design = read(design_path)
    for item in parameters.values():
        if isinstance(item, dict) and item.get("env") and str(item["env"]) not in runtime and str(item["env"]) not in matrix:
            findings.append(Finding("error", "E_FROZEN_ENV_ROUTE", f"Frozen environment key is not connected: {item['env']}", str(frozen_path)))
    for marker in (
        "CANONICAL_SCENARIO_ROOT_RELATIVE",
        "NOVELTY01_FORMAL_CONTRACT.json",
        "novelty01_frozen_parameters.json"):
        if marker not in matrix:
            findings.append(Finding("error", "E_MATRIX_CONTRACT_ROUTE", f"Matrix contract marker is missing: {marker}", str(matrix_path)))
    for marker in (
        "Feature_FixedGroupIndex |",
        "Feature_HardSoftDescriptor |",
        "fullDynamicDecisionFrames",
        "proposalTimingHistory",
        "fullDynamicTimingHistory"):
        if marker not in runtime:
            findings.append(Finding("error", "E_RUNTIME_CONTRACT_ROUTE", f"Runtime contract marker is missing: {marker}", str(runtime_path)))
    if "HairSegmentGroupAdaptivePayloadRuntime.hlsl" not in project or "SegmentGroupAdaptivePayload.h" not in project:
        findings.append(Finding("error", "E_MSBUILD_REGISTRATION", "Novelty 01 Header/Shader is not registered in MSBuild.", str(project_path)))
    for marker in ("# 49. Version 69.3 実装統合情報",
                   "# 60. Version 69.3 Merge Gate",
                   "# 61. Version 69.4 完全実装追補",
                   "a0521e258acdbd9d71262172d6d2ba208c338eeb"):
        if marker not in design:
            findings.append(Finding("error", "E_DESIGN_TRACEABILITY", f"Design document marker is missing: {marker}", str(design_path)))

    # Distribution copies are not compiled and are tolerated in 69.4 because
    # this package intentionally postpones source-file compression. The runtime
    # source of truth remains apps/, include/, tools/final_stage/, benchmarks/,
    # tests/, docs/, and 設計書/.
    if (root / "overlay").exists():
        findings.append(Finding("warning", "W_DUPLICATE_OVERLAY",
                                "overlay/ remains as a distribution copy and must not be referenced by MSBuild.",
                                str(root / "overlay")))
    if (root / "replacement").exists():
        findings.append(Finding("warning", "W_DUPLICATE_REPLACEMENT",
                                "replacement/ remains as a distribution copy and must not be referenced by MSBuild.",
                                str(root / "replacement")))
    for relative in (
        "tools/apply_novelty01_complete.py",
        "tools/apply_novelty01_sgapt.py",
        "tools/verify_novelty01_sgapt.py"):
        if (root / relative).exists():
            findings.append(Finding("warning", "W_DISTRIBUTION_TOOL_IN_REPO",
                                    "Distribution-only apply tool remains; it is ignored by the runtime build.", relative))

    for python_path in (
        matrix_path,
        root / "tools/final_stage/hairgpu_novelty01_report.py",
        root / "tools/final_stage/hairgpu_novelty01_resource_audit.py",
        root / "tools/final_stage/hairgpu_novelty01_gate.py"):
        try:
            compile(read(python_path), str(python_path), "exec")
        except Exception as exc:
            findings.append(Finding("error", "E_PYTHON_SYNTAX", str(exc), str(python_path)))


def compile_header(root: Path, findings: list[Finding]) -> None:
    compiler = shutil.which("cl.exe") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        findings.append(Finding("error", "E_CPP_COMPILER", "No C++ compiler was found."))
        return
    with tempfile.TemporaryDirectory(prefix="n01_header_") as directory:
        source = Path(directory) / "test.cpp"
        output = Path(directory) / ("test.exe" if os.name == "nt" else "test")
        source.write_text(
            '#include "hairgpu/SegmentGroupAdaptivePayload.h"\n'
            'int main(){ hairgpu::novelty01::RuntimeConstants c{}; '
            'return c.groupCount == 0u ? 0 : 1; }\n', encoding="utf-8")
        if Path(compiler).name.lower() == "cl.exe":
            command = [compiler, "/nologo", "/std:c++17", "/W4", "/WX",
                       f"/I{root / 'include'}", str(source), f"/Fe:{output}"]
        else:
            command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                       f"-I{root / 'include'}", str(source), "-o", str(output)]
        result = run(command)
        if result.returncode != 0:
            findings.append(Finding("error", "E_CPP_HEADER_COMPILE", result.stdout.strip(), str(source)))


def compile_hlsl(
    root: Path,
    findings: list[Finding],
    require_fxc: bool,
) -> dict[str, object]:
    shader = root / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    fxc = find_fxc()
    manifest: dict[str, object] = {
        "schema": "hairgpu-novelty01-fxc-entrypoints-v1",
        "compiler": fxc or "",
        "executed": bool(fxc),
        "entryPointCount": len(ENTRY_POINTS),
        "failureCount": 0,
        "passed": False,
        "entries": [],
    }
    if not fxc:
        severity = "error" if require_fxc else "warning"
        findings.append(Finding(
            severity,
            "E_FXC_UNAVAILABLE" if require_fxc else "W_FXC_UNAVAILABLE",
            "FXC was not found; Windows Shader Model 5 compilation was not executed.",
            str(shader)))
        return manifest

    entries: list[dict[str, object]] = []
    failure_count = 0
    with tempfile.TemporaryDirectory(prefix="n01_fxc_") as directory:
        for entry, mode in ENTRY_POINTS.items():
            output = Path(directory) / f"{entry}.cso"
            command = [fxc, "/nologo", "/T", "cs_5_0", "/E", entry,
                       "/D", f"N01_BINDING_MODE={mode}", "/Fo", str(output), str(shader)]
            result = run(command)
            passed = result.returncode == 0 and output.is_file()
            entries.append({
                "entryPoint": entry,
                "bindingMode": mode,
                "passed": passed,
                "returnCode": result.returncode,
                "diagnostic": result.stdout.strip(),
            })
            if not passed:
                failure_count += 1
                findings.append(Finding(
                    "error", "E_HLSL_COMPILE",
                    f"{entry} (mode {mode}) failed:\n{result.stdout}", str(shader)))
    manifest["entries"] = entries
    manifest["failureCount"] = failure_count
    manifest["passed"] = failure_count == 0 and len(entries) == len(ENTRY_POINTS)
    return manifest


def validate_formal_csv(path: Path, findings: list[Finding]) -> None:
    if not path:
        return
    if not path.is_file():
        findings.append(Finding("error", "E_TELEMETRY_MISSING", "Formal telemetry CSV is missing.", str(path)))
        return
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        required = {
            "schema_version", "frame", "source_frame", "variant", "feature_mask",
            "proposal_ms_ewma", "full_dynamic_ms_ewma", "used_full_dynamic",
            "ground_truth_samples", "false_static_samples", "false_dynamic_samples", "position_rmse_mm",
            "position_max_mm", "follower_error_mm", "render_error_mm",
        } | {f"counter_{i}" for i in range(64)}
        missing = required - set(reader.fieldnames or ())
        if missing:
            findings.append(Finding("error", "E_TELEMETRY_COLUMNS",
                                    "Missing columns: " + ", ".join(sorted(missing)), str(path)))
        rows = list(reader)
        if not rows:
            findings.append(Finding("error", "E_TELEMETRY_EMPTY", "Formal telemetry has no rows.", str(path)))


def preflight(root: Path, require_fxc: bool, telemetry: Path | None) -> dict[str, object]:
    findings: list[Finding] = []
    all_text_parts: list[str] = []
    for relative in REQUIRED_FILES:
        path = root / relative
        if not path.is_file():
            findings.append(Finding("error", "E_REQUIRED_FILE", "Required source file is missing.", relative))
        elif relative != "tools/final_stage/hairgpu_novelty01_preflight.py" and not relative.startswith("tests/"):
            # Never allow the checker or its tests to satisfy their own markers.
            all_text_parts.append(read(path))
    all_text = "\n".join(all_text_parts)

    for relative in REQUIRED_FILES:
        path = root / relative
        if not path.is_file():
            continue
        text = read(path)
        if path.suffix.lower() in {".h", ".hpp", ".cpp", ".inl", ".hlsl"}:
            if not (balanced(text, "{", "}") and balanced(text, "(", ")") and balanced(text, "[", "]")):
                findings.append(Finding("error", "E_DELIMITER_BALANCE", "Unbalanced delimiters.", relative))
        if path.suffix.lower() in {".h", ".hpp", ".cpp", ".inl", ".hlsl", ".py"}:
            for expression in FORBIDDEN_PATTERNS:
                if re.search(expression, text, flags=re.IGNORECASE):
                    findings.append(Finding("error", "E_PLACEHOLDER", f"Forbidden placeholder matched: {expression}", relative))

    shader_text = read(root / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl") if (root / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl").is_file() else ""
    for entry, mode in ENTRY_POINTS.items():
        if not re.search(rf"\bvoid\s+{re.escape(entry)}\s*\(", shader_text):
            findings.append(Finding("error", "E_ENTRYPOINT", f"Missing shader entry point {entry} (mode {mode}).", "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"))

    for check_id, markers in CHECKLIST.items():
        missing = [marker for marker in markers if marker not in all_text]
        if missing:
            findings.append(Finding("error", "E_CHECKLIST_" + check_id.upper(),
                                    "Missing implementation markers: " + ", ".join(missing)))

    # The proposal must use a normal class declaration (no access-control macro),
    # preserve the original B0 solver for ground truth, and route frame calls to
    # the controlled dispatcher. B0 remains untouched; B1-B8/P/A1-A15 use their
    # explicitly frozen execution contracts.
    aggregator_path = root / "apps/dx11_viewer/src/GpuHairViewer.cpp"
    route_path = root / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
    aggregator = read(aggregator_path) if aggregator_path.is_file() else ""
    route_text = read(route_path) if route_path.is_file() else ""
    runtime_paths = (
        root / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",
        root / "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl",
    )
    runtime_text = "\n".join(read(path) for path in runtime_paths if path.is_file())
    if "#define private" in aggregator or "ExecuteExperimentSimulationTickLegacy" in aggregator:
        findings.append(Finding("error", "E_PRIVATE_MACRO_ROUTE", "Legacy macro injection remains in the viewer aggregator.", str(aggregator_path)))
    for marker in ("16_segment_group_adaptive_payload_complete.inl",
                   "17_novelty01_formal_evidence.inl"):
        if marker not in aggregator:
            findings.append(Finding("error", "E_VIEWER_ROUTE", f"Viewer include is missing: {marker}", str(aggregator_path)))
    if "ExecuteNovelty01ExperimentTick(" not in route_text:
        findings.append(Finding("error", "E_VIEWER_ROUTE", "Frame execution is not routed through ExecuteNovelty01ExperimentTick.", str(route_path)))
    for marker in ("StepSimulation(dt);", "ExperimentMethodId::P_IntegratedSegmentGroup"):
        if marker not in runtime_text:
            findings.append(Finding("error", "E_GROUND_TRUTH_ROUTE", f"Runtime marker is missing: {marker}"))

    scenario_root = root / "benchmarks/novelty01/sgapt_scenarios"
    for index in range(1, 19):
        code = f"S{index:02d}"
        scenario_path = scenario_root / f"{code}.json"
        if not scenario_path.is_file():
            findings.append(Finding("error", "E_DESIGN_SCENARIO",
                                    f"Dedicated design scenario is missing: {code}", str(scenario_path)))
            continue
        try:
            payload = json.loads(read(scenario_path))
            if payload.get("novelty01DesignScenario") != code:
                raise ValueError("novelty01DesignScenario mismatch")
            if payload.get("events") != []:
                raise ValueError("host timeline must not inject confounding events")
        except Exception as exc:
            findings.append(Finding("error", "E_DESIGN_SCENARIO",
                                    f"Invalid dedicated design scenario {code}: {exc}", str(scenario_path)))

    validate_formal_contract(root, findings)
    validate_shader_contract(root, findings)
    compile_header(root, findings)
    fxc_manifest = compile_hlsl(root, findings, require_fxc)
    if telemetry is not None:
        validate_formal_csv(telemetry, findings)

    errors = [asdict(item) for item in findings if item.severity == "error"]
    warnings = [asdict(item) for item in findings if item.severity == "warning"]
    return {
        "schema": SCHEMA,
        "baseMainCommit": BASE_MAIN_COMMIT,
        "sourceCommit": SOURCE_COMMIT,
        "root": str(root),
        "passed": not errors,
        "checklistCount": len(CHECKLIST),
        "shaderEntryPointCount": len(ENTRY_POINTS),
        "fxcCompilation": fxc_manifest,
        "errors": errors,
        "warnings": warnings,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True,
                        help="HairSystem repository root after applying the source overlay.")
    parser.add_argument("--require-fxc", action="store_true",
                        help="Fail when the Windows FXC compiler is unavailable.")
    parser.add_argument("--telemetry", type=Path,
                        help="Optional Novelty 01 formal telemetry CSV to validate.")
    parser.add_argument("--json", type=Path, help="Write the result JSON here.")
    parser.add_argument("--fxc-json", type=Path,
                        help="Write the per-entry-point FXC manifest here.")
    args = parser.parse_args()
    result = preflight(args.repo.resolve(), args.require_fxc,
                       args.telemetry.resolve() if args.telemetry else None)
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    print(encoded)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(encoded + "\n", encoding="utf-8")
    if args.fxc_json:
        args.fxc_json.parent.mkdir(parents=True, exist_ok=True)
        args.fxc_json.write_text(
            json.dumps(result["fxcCompilation"], ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
