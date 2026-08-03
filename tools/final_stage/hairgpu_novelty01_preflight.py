from __future__ import annotations

"""Fail-closed source/runtime preflight for Novelty 01 SG-APT.

This checker validates repository identity, layout, source contracts, C++/HLSL
compilation, and optional formal telemetry. It intentionally does not equate a
source/build pass with a successful GPU experiment.
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
from dataclasses import asdict, dataclass
from pathlib import Path

SOURCE_BASELINE_COMMIT = "4f4c6210d7be40a0b5d522789d2684ade86d3de9"
SCHEMA = "novelty01-preflight-v5"

REQUIRED_FILES = (
    ".github/workflows/windows-build-test.yml",
    ".github/workflows/novelty01-formal-gate.yml",
    "include/hairgpu/SegmentGroupAdaptivePayload.h",
    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl",
    "apps/dx11_viewer/shaders/HairNovelty01RenderDirtyCache.hlsl",
    "apps/dx11_viewer/src/GpuHairViewer.h",
    "apps/dx11_viewer/src/GpuHairViewer.cpp",
    "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl",
    "tools/final_stage/hairgpu_novelty01_preflight.py",
    "tools/final_stage/hairgpu_final_run.py",
    "tools/final_stage/hairgpu_novelty01_matrix.py",
    "tools/final_stage/hairgpu_novelty01_readiness_smoke.py",
    "tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
    "tests/final_stage/test_novelty01_complete_contract.py",
    "tests/final_stage/test_novelty01_runtime_contract.py",
    "tests/final_stage/test_novelty01_formal_asset_binding_contract.py",
)

OBSOLETE_PATHS = (
    "overlay",
    "replacement",
    "tools/apply_novelty01_69_3_complete.py",
    "tools/apply_novelty01_69_4_complete.py",
    "tools/apply_novelty01_complete.py",
    "tools/apply_novelty01_sgapt.py",
    "tools/final_stage/apply_formal_asset_binding_patch.py",
    "patches/hairgpu_final_run_formal_asset_binding.patch",
    "files",
    "tools/repair_novelty01_premerge.py",
    "tools/verify_novelty01_sgapt.py",
)

RENDER_CACHE_ENTRY_POINTS = (
    "N01CopyGuideRenderCacheCS",
    "N01CopyFollowerRenderCacheCS",
)

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
}

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
    "baseline_equivalent_solver": (
        "Feature_BaselineEquivalentSolve",
        "N01IntegrateDynamicGuidesCS",
        "N01DistanceDynamicGuidesCS",
        "N01BendDynamicGuidesCS",
        "N01CollisionDynamicGuidesCS",
        "RunNovelty01GroundTruth",
        "StepSimulation(dt)",
    ),
    "follower_dependency_precomputed": ("followerOffsets", "followerEntries", "dependencyVersion"),
    "follower_local_reconstruct": ("N01FollowerLocalCS", "FollowerDirtyRange"),
    "render_dirty_range": (
        "RenderDirtyRange",
        "Counter_RenderRegeneratedVertices",
        "N01CopyFollowerRenderCacheCS",
        "runtime->renderDirtyRanges.srv.Get()",
        "linearPointIndex / gParticlesPerStrand",
        "N01DispatchGroups(followerPointCount, 256u)",
        "D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION",
    ),
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
    "design_scenarios": (
        "ApplyNovelty01DesignScenarioForTick",
        "N01InjectDesignScenarioCS",
        "S17 Payload pool stress",
        "S18 Version/descriptor fault injection",
    ),
    "main_foundation": ("02a_required_runtime_foundation.inl",),
    "formal_asset_binding": (
        "HAIRGPU_FORMAL_ASSET_PATH",
        "HAIRGPU_FORMAL_ASSET_SHA256",
        "HAIRGPU_FORMAL_TOPOLOGY_SHA256",
        "HAIRGPU_GENERATOR_SETTINGS_SHA256",
    ),
    "formal_header_integration": (
        "struct Novelty01RuntimeState;",
        "struct Novelty01RenderCacheState;",
        "void ExecuteNovelty01ExperimentTick(float dt);",
        "void RenderLegacy();",
    ),
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
    try:
        return subprocess.run(
            command,
            cwd=cwd,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except OSError as exc:
        return subprocess.CompletedProcess(command, 127, stdout=str(exc))


def git_output(root: Path, *arguments: str) -> tuple[int, str]:
    result = run(["git", "-C", str(root), *arguments])
    return result.returncode, result.stdout.strip()


def collect_git_identity(root: Path, findings: list[Finding], require_clean: bool) -> dict[str, object]:
    identity: dict[str, object] = {
        "available": False,
        "branch": "",
        "head": "",
        "mergeBase": "",
        "mainRef": "",
        "worktreeDirty": False,
        "sourceBaselineCommit": SOURCE_BASELINE_COMMIT,
        "sourceBaselineIsAncestor": False,
    }

    code, inside = git_output(root, "rev-parse", "--is-inside-work-tree")
    if code != 0 or inside.lower() != "true":
        findings.append(Finding("error", "E_GIT_REPOSITORY", "Repository is not a Git worktree.", str(root)))
        return identity

    identity["available"] = True
    _, branch = git_output(root, "branch", "--show-current")
    code, head = git_output(root, "rev-parse", "HEAD")
    if code != 0 or not re.fullmatch(r"[0-9a-fA-F]{40}", head):
        findings.append(Finding("error", "E_GIT_HEAD", "Could not resolve a 40-character HEAD commit.", str(root)))
        return identity
    identity["branch"] = branch
    identity["head"] = head.lower()

    if branch not in {"Novelty01", "main"}:
        findings.append(Finding("error", "E_GIT_BRANCH", f"Novelty01 or main is required; current branch is {branch!r}.", str(root)))

    ancestor = run(["git", "-C", str(root), "merge-base", "--is-ancestor", SOURCE_BASELINE_COMMIT, "HEAD"])
    identity["sourceBaselineIsAncestor"] = ancestor.returncode == 0
    if ancestor.returncode != 0:
        findings.append(Finding(
            "error",
            "E_GIT_SOURCE_BASELINE",
            f"HEAD is not descended from required Novelty01 69.9 baseline {SOURCE_BASELINE_COMMIT}.",
            str(root),
        ))

    for candidate in ("origin/main", "main"):
        code, _ = git_output(root, "rev-parse", "--verify", candidate)
        if code != 0:
            continue
        code, merge_base = git_output(root, "merge-base", "HEAD", candidate)
        if code == 0 and re.fullmatch(r"[0-9a-fA-F]{40}", merge_base):
            identity["mainRef"] = candidate
            identity["mergeBase"] = merge_base.lower()
            break

    code, status = git_output(root, "status", "--porcelain", "--untracked-files=all")
    dirty = code != 0 or bool(status.strip())
    identity["worktreeDirty"] = dirty
    if require_clean and dirty:
        findings.append(Finding("error", "E_GIT_DIRTY", "Formal verification requires a clean worktree.", str(root)))
    elif dirty:
        findings.append(Finding("warning", "W_GIT_DIRTY", "Worktree is dirty; evidence must record the final committed HEAD before publication.", str(root)))

    return identity


def _program_files_x86() -> Path | None:
    value = os.environ.get("ProgramFiles(x86)") or os.environ.get("ProgramFiles")
    return Path(value) if value else None


def find_vswhere() -> Path | None:
    explicit = os.environ.get("VSWHERE")
    if explicit and Path(explicit).is_file():
        return Path(explicit)
    root = _program_files_x86()
    if root is None:
        return None
    candidate = root / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    return candidate if candidate.is_file() else None


def find_vsdevcmd() -> Path | None:
    vswhere = find_vswhere()
    if vswhere is None:
        return None
    query = run([
        str(vswhere), "-latest", "-products", "*",
        "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-property", "installationPath",
    ])
    if query.returncode != 0 or not query.stdout.strip():
        return None
    installation = Path(query.stdout.strip().splitlines()[-1].strip())
    candidate = installation / "Common7" / "Tools" / "VsDevCmd.bat"
    return candidate if candidate.is_file() else None


def find_fxc() -> str | None:
    explicit = os.environ.get("FXC")
    if explicit and Path(explicit).is_file():
        return str(Path(explicit))
    direct = shutil.which("fxc.exe") or shutil.which("fxc")
    if direct:
        return direct
    if os.name != "nt":
        return None
    root = _program_files_x86()
    if root is None:
        return None
    kits = root / "Windows Kits" / "10" / "bin"
    if not kits.is_dir():
        return None
    candidates = sorted(kits.glob("*/x64/fxc.exe"), key=lambda item: item.parent.parent.name, reverse=True)
    return str(candidates[0]) if candidates else None


def validate_repository_layout(root: Path, findings: list[Finding]) -> None:
    for relative in OBSOLETE_PATHS:
        path = root / relative
        if path.exists():
            findings.append(Finding(
                "error", "E_OBSOLETE_DUPLICATE_TREE",
                "Obsolete overlay/replacement/application artifact must be removed.", relative,
            ))

    cpp_path = root / "apps/dx11_viewer/src/GpuHairViewer.cpp"
    header_path = root / "apps/dx11_viewer/src/GpuHairViewer.h"
    if cpp_path.is_file():
        cpp = read(cpp_path)
        for forbidden in ("#define private", "#undef private"):
            if forbidden in cpp:
                findings.append(Finding(
                    "error", "E_ACCESS_MACRO_REWRITE",
                    f"GpuHairViewer.cpp must not rewrite access control with {forbidden}.", str(cpp_path),
                ))
        if '#include "GpuHairViewer.h"' not in cpp:
            findings.append(Finding("error", "E_VIEWER_HEADER_INCLUDE", "GpuHairViewer.cpp does not include the real header.", str(cpp_path)))

    if header_path.is_file():
        header = read(header_path)
        required = (
            "struct Novelty01RuntimeState;",
            "struct Novelty01RenderCacheState;",
            "void ExecuteNovelty01ExperimentTick(float dt);",
            "void StepNovelty01SingleFrame();",
            "void RenderLegacy();",
            "void ShutdownNovelty01Routed();",
        )
        for marker in required:
            if marker not in header:
                findings.append(Finding("error", "E_FORMAL_HEADER_DECLARATION", f"Missing formal header declaration: {marker}", str(header_path)))

    for workflow in (
        root / ".github/workflows/windows-build-test.yml",
        root / ".github/workflows/novelty01-formal-gate.yml",
    ):
        if not workflow.is_file():
            continue
        text = read(workflow)
        for forbidden in (
            "path.write_text(text",
            "apply_viewer_dedup.py",
            "chatgpt/stable-benchmark-wig",
            "chatgpt/inextensible-stretch-guard",
            "replacement/",
            "overlay/",
        ):
            if forbidden in text:
                findings.append(Finding("error", "E_WORKFLOW_RUNTIME_PATCH", f"Workflow contains forbidden runtime patch/dependency marker: {forbidden}", str(workflow)))
        if "actions/checkout@v4" not in text or "fetch-depth: 0" not in text:
            findings.append(Finding("error", "E_WORKFLOW_CHECKOUT", "Workflow must use a full clean checkout.", str(workflow)))


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
        findings.append(Finding("error", "E_ENTRYPOINT_DUPLICATE", "Duplicate shader entry points: " + ", ".join(duplicate_entries), str(shader_path)))

    actual_set = set(actual_entries)
    mapped_set = set(ENTRY_POINTS)
    if actual_set != mapped_set:
        findings.append(Finding(
            "error", "E_ENTRYPOINT_MAP",
            "Entry-point map mismatch. Missing from map: " + ", ".join(sorted(actual_set - mapped_set))
            + "; stale in map: " + ", ".join(sorted(mapped_set - actual_set)), str(shader_path),
        ))

    loaded_set = set(re.findall(r'load\("(N01[A-Za-z0-9_]+CS)"', runtime))
    if loaded_set != actual_set:
        findings.append(Finding(
            "error", "E_ENTRYPOINT_LOAD",
            "Runtime shader-load mismatch. Missing loads: " + ", ".join(sorted(actual_set - loaded_set))
            + "; unknown loads: " + ", ".join(sorted(loaded_set - actual_set)), str(runtime_path),
        ))

    for mode in range(13):
        match = re.search(rf"#if N01_BINDING_MODE == {mode}\n(.*?)(?=\n#endif // mode {mode})", shader, flags=re.DOTALL)
        if not match:
            findings.append(Finding("error", "E_BINDING_MODE", f"Missing binding mode {mode}.", str(shader_path)))
            continue
        body = match.group(1)
        uav_registers = [int(value) for value in re.findall(r"register\(u(\d+)\)", body)]
        srv_registers = [int(value) for value in re.findall(r"register\(t(\d+)\)", body)]
        if len(uav_registers) != len(set(uav_registers)):
            findings.append(Finding("error", "E_UAV_REGISTER_DUPLICATE", f"Binding mode {mode} contains duplicate UAV registers.", str(shader_path)))
        if len(srv_registers) != len(set(srv_registers)):
            findings.append(Finding("error", "E_SRV_REGISTER_DUPLICATE", f"Binding mode {mode} contains duplicate SRV registers.", str(shader_path)))
        if uav_registers and max(uav_registers) > 7:
            findings.append(Finding("error", "E_D3D11_UAV_LIMIT", f"Binding mode {mode} exceeds the D3D11 eight-UAV limit.", str(shader_path)))

    cpp_match = re.search(r"struct\s+alignas\(16\)\s+RuntimeConstants\s*\{(.*?)\n\s*\};", header, flags=re.DOTALL)
    hlsl_match = re.search(r"cbuffer\s+Novelty01Constants\s*:\s*register\(b0\)\s*\{(.*?)\n\};", shader, flags=re.DOTALL)
    if not cpp_match or not hlsl_match:
        findings.append(Finding("error", "E_CONSTANT_BUFFER_DECLARATION", "RuntimeConstants or Novelty01Constants is missing.", str(shader_path)))
    else:
        cpp_names: list[str] = []
        for raw in cpp_match.group(1).splitlines():
            declaration = re.match(r"(?:uint32_t|float)\s+([A-Za-z_][A-Za-z0-9_]*)(?:\[[^\]]+\])?(?:\[[^\]]+\])?\s*(?:=.*)?;", raw.strip())
            if declaration:
                cpp_names.append(declaration.group(1))
        hlsl_names: list[str] = []
        for raw in hlsl_match.group(1).splitlines():
            declaration = re.match(r"(?:uint|float|float2|float3|float4|uint4)\s+(g[A-Za-z_][A-Za-z0-9_]*)(?:\[[^\]]+\])?\s*;", raw.strip())
            if declaration:
                name = declaration.group(1)[1:]
                hlsl_names.append(name[:1].lower() + name[1:])
        if cpp_names != hlsl_names:
            mismatch = next((index for index, pair in enumerate(zip(cpp_names, hlsl_names)) if pair[0] != pair[1]), min(len(cpp_names), len(hlsl_names)))
            findings.append(Finding("error", "E_CONSTANT_BUFFER_LAYOUT", f"C++/HLSL constant-buffer field order differs at index {mismatch}.", str(shader_path)))

    counter_match = re.search(r"enum\s+CounterIndex\s*:\s*uint32_t\s*\{(.*?)\n\s*\};", header, flags=re.DOTALL)
    if not counter_match:
        findings.append(Finding("error", "E_COUNTER_ENUM", "CounterIndex is missing.", str(header_path)))
    else:
        counter_values = [int(value) for _, value in re.findall(r"(Counter_[A-Za-z0-9_]+)\s*=\s*([0-9]+)u", counter_match.group(1))]
        if counter_values != list(range(64)):
            findings.append(Finding("error", "E_COUNTER_LAYOUT", "CounterIndex must contain exactly the contiguous range 0..63.", str(header_path)))

    missing_features = sorted({token for token in re.findall(r"\bFeature_[A-Za-z0-9_]+\b", shader) if token not in header})
    if missing_features:
        findings.append(Finding("error", "E_FEATURE_CONTRACT", "Shader uses undeclared C++ feature bits: " + ", ".join(missing_features), str(shader_path)))


def validate_render_cache_contract(root: Path, findings: list[Finding]) -> None:
    shader_path = root / "apps/dx11_viewer/shaders/HairNovelty01RenderDirtyCache.hlsl"
    consumer_path = root / "apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl"
    if not shader_path.is_file() or not consumer_path.is_file():
        return
    shader = read(shader_path)
    consumer = read(consumer_path)
    actual_entries = set(re.findall(r"\bvoid\s+(N01[A-Za-z0-9_]+CS)\s*\(", shader))
    expected_entries = set(RENDER_CACHE_ENTRY_POINTS)
    if actual_entries != expected_entries:
        findings.append(Finding("error", "E_RENDER_CACHE_ENTRYPOINT_MAP", "Render-cache entry-point mismatch.", str(shader_path)))
    for marker in ("[numthreads(256, 1, 1)]", "SV_DispatchThreadID", "linearPointIndex / gParticlesPerStrand", "gFollowerCount * gParticlesPerStrand"):
        if marker not in shader:
            findings.append(Finding("error", "E_RENDER_CACHE_FLATTENED_DISPATCH", f"Render-cache shader marker is missing: {marker}", str(shader_path)))
    for marker in ("followerPointCount64", "N01DispatchGroups(followerPointCount, 256u)", "D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION", "followerDispatchGroups"):
        if marker not in consumer:
            findings.append(Finding("error", "E_RENDER_CACHE_DISPATCH_GUARD", f"Render-cache consumer marker is missing: {marker}", str(consumer_path)))
    for marker in ("m_context->Dispatch(\n            m_asset.followerStrandCount", "const uint followerIndex = groupId.x;"):
        if marker in consumer or marker in shader:
            findings.append(Finding("error", "E_RENDER_CACHE_96000_GROUP_DISPATCH", "The 96,000-follower one-group-per-follower path is forbidden.", str(consumer_path)))
    groups = (96000 * 128 + 256 - 1) // 256
    if groups != 48000 or groups > 65535:
        findings.append(Finding("error", "E_RENDER_CACHE_FORMAL_DISPATCH_LIMIT", "Formal 100k render-cache dispatch is invalid.", str(consumer_path)))


def validate_formal_runner_contract(root: Path, findings: list[Finding]) -> None:
    runner_path = root / "tools/final_stage/hairgpu_final_run.py"
    if not runner_path.is_file():
        return
    source = read(runner_path)
    for marker in (
        'asset_manifest.get("_resolvedMembers", {})',
        '"HAIRGPU_FORMAL_ASSET_PATH": formal_asset_path',
        '"HAIRGPU_FORMAL_ASSET_SHA256": formal_asset_sha256',
        '"HAIRGPU_FORMAL_TOPOLOGY_SHA256": formal_topology_sha256',
        '"HAIRGPU_GENERATOR_SETTINGS_SHA256": generator_settings_sha256',
        "SHA256_RE.fullmatch",
    ):
        if marker not in source:
            findings.append(Finding("error", "E_FORMAL_RUNNER_ASSET_BINDING", f"Direct formal-runner binding is missing: {marker}", str(runner_path)))
    if "apply_formal_asset_binding_patch" in source:
        findings.append(Finding("error", "E_FORMAL_RUNNER_RUNTIME_PATCH", "The formal runner must not patch itself at runtime.", str(runner_path)))


def compile_header(root: Path, findings: list[Finding]) -> None:
    direct_compiler = (
        shutil.which("cl.exe")
        or shutil.which("clang++")
        or shutil.which("g++")
    )

    with tempfile.TemporaryDirectory(prefix="n01_header_") as directory:
        working_directory = Path(directory)
        source = working_directory / "test.cpp"
        output = working_directory / (
            "test.exe" if os.name == "nt" else "test"
        )
        source.write_text(
            '#include "hairgpu/SegmentGroupAdaptivePayload.h"\n'
            "int main(){ "
            "hairgpu::novelty01::RuntimeConstants c{}; "
            "return c.groupCount == 0u ? 0 : 1; "
            "}\n",
            encoding="utf-8",
        )

        if (
            direct_compiler
            and Path(direct_compiler).name.lower() == "cl.exe"
        ):
            command = [
                direct_compiler,
                "/nologo",
                "/std:c++17",
                "/W4",
                "/WX",
                f"/I{root / 'include'}",
                str(source),
                f"/Fe:{output}",
            ]
            result = run(command)
        elif direct_compiler:
            command = [
                direct_compiler,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                f"-I{root / 'include'}",
                str(source),
                "-o",
                str(output),
            ]
            result = run(command)
        else:
            vswhere = find_vswhere()
            if vswhere is None:
                findings.append(
                    Finding(
                        "error",
                        "E_CPP_COMPILER",
                        "No direct C++ compiler or vswhere.exe was found.",
                    )
                )
                return

            query = run([
                str(vswhere),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.Component.MSBuild",
                "-find",
                r"MSBuild\**\Bin\MSBuild.exe",
            ])
            msbuild_candidates = [
                Path(line.strip().strip('"'))
                for line in query.stdout.splitlines()
                if line.strip()
            ]
            msbuild = next(
                (
                    candidate
                    for candidate in msbuild_candidates
                    if candidate.is_file()
                ),
                None,
            )
            if query.returncode != 0 or msbuild is None:
                findings.append(
                    Finding(
                        "error",
                        "E_CPP_COMPILER",
                        "MSBuild.exe could not be resolved through vswhere.\n"
                        + query.stdout.strip(),
                    )
                )
                return

            include_path = (
                str(root / "include")
                .replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
            )
            project = working_directory / "HeaderProbe.vcxproj"
            project.write_text(
                """<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build"
         xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>{A6653D5D-A5C8-4F38-92CC-7B95B6A41131}</ProjectGuid>
    <Keyword>Win32Proj</Keyword>
    <RootNamespace>N01HeaderProbe</RootNamespace>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup
      Condition="'$(Configuration)|$(Platform)'=='Release|x64'"
      Label="Configuration">
    <ConfigurationType>Application</ConfigurationType>
    <UseDebugLibraries>false</UseDebugLibraries>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ItemDefinitionGroup
      Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <AdditionalIncludeDirectories>"""
                + include_path
                + """;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
    <Link>
      <SubSystem>Console</SubSystem>
    </Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="test.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
""",
                encoding="utf-8",
            )
            command = [
                str(msbuild),
                str(project),
                "/nologo",
                "/m:1",
                "/verbosity:minimal",
                "/p:Configuration=Release",
                "/p:Platform=x64",
            ]
            result = run(command, cwd=working_directory)

        if result.returncode != 0:
            findings.append(
                Finding(
                    "error",
                    "E_CPP_HEADER_COMPILE",
                    result.stdout.strip(),
                    str(source),
                )
            )


def compile_hlsl(root: Path, findings: list[Finding], require_fxc: bool) -> None:
    main_shader = root / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    render_shader = root / "apps/dx11_viewer/shaders/HairNovelty01RenderDirtyCache.hlsl"
    fxc = find_fxc()
    if not fxc:
        severity = "error" if require_fxc else "warning"
        findings.append(Finding(severity, "E_FXC_UNAVAILABLE" if require_fxc else "W_FXC_UNAVAILABLE", "FXC was not found; Shader Model 5 compilation was not executed.", str(main_shader)))
        return
    with tempfile.TemporaryDirectory(prefix="n01_fxc_") as directory:
        for entry, mode in ENTRY_POINTS.items():
            output = Path(directory) / f"{entry}.cso"
            result = run([fxc, "/nologo", "/T", "cs_5_0", "/E", entry, "/D", f"N01_BINDING_MODE={mode}", "/Fo", str(output), str(main_shader)])
            if result.returncode != 0:
                findings.append(Finding("error", "E_HLSL_COMPILE", f"{entry} (mode {mode}) failed:\n{result.stdout}", str(main_shader)))
        for entry in RENDER_CACHE_ENTRY_POINTS:
            output = Path(directory) / f"{entry}.cso"
            result = run([fxc, "/nologo", "/T", "cs_5_0", "/E", entry, "/Fo", str(output), str(render_shader)])
            if result.returncode != 0:
                findings.append(Finding("error", "E_RENDER_CACHE_HLSL_COMPILE", f"{entry} failed:\n{result.stdout}", str(render_shader)))


def validate_formal_csv(path: Path, findings: list[Finding]) -> None:
    if not path.is_file():
        findings.append(Finding("error", "E_TELEMETRY_MISSING", "Formal telemetry CSV is missing.", str(path)))
        return
    with path.open(newline="", encoding="utf-8-sig") as handle:
        reader = csv.DictReader(handle)
        required = {
            "schema_version", "frame", "source_frame", "variant", "feature_mask",
            "proposal_ms_ewma", "full_dynamic_ms_ewma", "used_full_dynamic",
            "ground_truth_samples", "false_static_samples", "false_dynamic_samples",
            "position_rmse_mm", "position_max_mm", "follower_error_mm", "render_error_mm",
        } | {f"counter_{i}" for i in range(64)}
        missing = required - set(reader.fieldnames or ())
        if missing:
            findings.append(Finding("error", "E_TELEMETRY_COLUMNS", "Missing columns: " + ", ".join(sorted(missing)), str(path)))
        if not list(reader):
            findings.append(Finding("error", "E_TELEMETRY_EMPTY", "Formal telemetry has no rows.", str(path)))


def preflight(
    root: Path,
    require_fxc: bool,
    telemetry: Path | None,
    require_clean_git: bool = False,
) -> dict[str, object]:
    findings: list[Finding] = []
    identity = collect_git_identity(root, findings, require_clean_git)
    all_text_parts: list[str] = []

    for relative in REQUIRED_FILES:
        path = root / relative
        if not path.is_file():
            findings.append(Finding("error", "E_REQUIRED_FILE", "Required source file is missing.", relative))
        elif relative != "tools/final_stage/hairgpu_novelty01_preflight.py" and not relative.startswith("tests/"):
            all_text_parts.append(read(path))

    validate_repository_layout(root, findings)
    all_text = "\n".join(all_text_parts)

    for relative in REQUIRED_FILES:
        path = root / relative
        if not path.is_file():
            continue
        text = read(path)
        if path.suffix.lower() in {".h", ".hpp", ".cpp", ".inl", ".hlsl"}:
            if not (balanced(text, "{", "}") and balanced(text, "(", ")") and balanced(text, "[", "]")):
                findings.append(Finding("error", "E_DELIMITER_BALANCE", "Unbalanced delimiters.", relative))
        for expression in FORBIDDEN_PATTERNS:
            if re.search(expression, text, flags=re.IGNORECASE):
                findings.append(Finding("error", "E_PLACEHOLDER", f"Forbidden placeholder matched: {expression}", relative))

    shader_path = root / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    shader_text = read(shader_path) if shader_path.is_file() else ""
    for entry, mode in ENTRY_POINTS.items():
        if not re.search(rf"\bvoid\s+{re.escape(entry)}\s*\(", shader_text):
            findings.append(Finding("error", "E_ENTRYPOINT", f"Missing shader entry point {entry} (mode {mode}).", str(shader_path)))

    for check_id, markers in CHECKLIST.items():
        missing = [marker for marker in markers if marker not in all_text]
        if missing:
            findings.append(Finding("error", "E_CHECKLIST_" + check_id.upper(), "Missing implementation markers: " + ", ".join(missing)))

    aggregator_path = root / "apps/dx11_viewer/src/GpuHairViewer.cpp"
    aggregator = read(aggregator_path) if aggregator_path.is_file() else ""
    for marker in (
        "ExecuteExperimentSimulationTickLegacy", "ExecuteNovelty01ExperimentTick",
        "16_segment_group_adaptive_payload_complete.inl", "17_novelty01_formal_evidence.inl",
        "18_novelty01_render_dirty_range_consumer.inl", "02a_required_runtime_foundation.inl",
    ):
        if marker not in aggregator:
            findings.append(Finding("error", "E_VIEWER_ROUTE", f"Viewer route marker is missing: {marker}", str(aggregator_path)))

    validate_shader_contract(root, findings)
    validate_render_cache_contract(root, findings)
    validate_formal_runner_contract(root, findings)
    compile_header(root, findings)
    compile_hlsl(root, findings, require_fxc)
    if telemetry is not None:
        validate_formal_csv(telemetry, findings)

    errors = [asdict(item) for item in findings if item.severity == "error"]
    warnings = [asdict(item) for item in findings if item.severity == "warning"]
    return {
        "schema": SCHEMA,
        "sourceBaselineCommit": SOURCE_BASELINE_COMMIT,
        "baseCommit": identity.get("mergeBase", ""),
        "noveltySourceCommit": identity.get("head", ""),
        "git": identity,
        "root": str(root),
        "passed": not errors,
        "checklistCount": len(CHECKLIST),
        "shaderEntryPointCount": len(ENTRY_POINTS),
        "renderCacheShaderEntryPointCount": len(RENDER_CACHE_ENTRY_POINTS),
        "formalFollowerRenderDispatchGroups": 48000,
        "errors": errors,
        "warnings": warnings,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--require-fxc", action="store_true")
    parser.add_argument("--require-clean-git", action="store_true")
    parser.add_argument("--telemetry", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()
    result = preflight(
        args.repo.resolve(),
        args.require_fxc,
        args.telemetry.resolve() if args.telemetry else None,
        args.require_clean_git,
    )
    encoded = json.dumps(result, ensure_ascii=False, indent=2)
    print(encoded)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(encoded + "\n", encoding="utf-8")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
