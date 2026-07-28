#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

BASE_COMMIT = "8d19bb546f3505fe25436d6ffa5b390a9b83897a"
DEFAULT_BRANCH = "feature/novelty01-complete"


class PatchError(RuntimeError):
    pass


def run(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(args), cwd=repo, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if check and result.returncode != 0:
        raise PatchError(f"command failed: {' '.join(args)}\n{result.stdout}")
    return result


def replace_once(path: Path, old: str, new: str, marker: str | None = None) -> None:
    text = path.read_text(encoding="utf-8")
    if marker and marker in text:
        return
    count = text.count(old)
    if count != 1:
        raise PatchError(f"{path}: expected one anchor, found {count}: {old[:100]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def copy_overlay(package_root: Path, repo: Path) -> None:
    overlay = package_root / "overlay"
    for source in overlay.rglob("*"):
        if not source.is_file():
            continue
        relative = source.relative_to(overlay)
        target = repo / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def patch_viewer_header(repo: Path) -> None:
    path = repo / "apps/dx11_viewer/src/GpuHairViewer.h"
    text = path.read_text(encoding="utf-8")

    if "#include <memory>" not in text:
        text = text.replace("#include <limits>\n", "#include <limits>\n#include <memory>\n", 1)
    if '#include "hairgpu/SegmentGroupAdaptivePayload.h"' not in text:
        text = text.replace(
            '#include "hairgpu/RenderData.h"\n',
            '#include "hairgpu/RenderData.h"\n#include "hairgpu/SegmentGroupAdaptivePayload.h"\n',
            1)
    if "~GpuHairViewer();" not in text:
        anchor = "        bool Initialize(HINSTANCE instance, int showCommand);\n"
        if text.count(anchor) != 1:
            raise PatchError(f"{path}: public declaration anchor changed")
        text = text.replace(anchor, anchor + "        ~GpuHairViewer();\n", 1)

    declaration_marker = "        struct Novelty01RuntimeState;\n"
    if declaration_marker not in text:
        anchor = "        struct StrandEditSnapshot;\n"
        declarations = r'''
        struct Novelty01RuntimeState;

        void ResetNovelty01RuntimeIfNeeded();
        bool EnsureNovelty01Runtime(float dt);
        void ApplyNovelty01DesignScenarioForTick(float dt);
        void ExecuteNovelty01ExperimentTick(float dt);
        void StepNovelty01SingleFrame();
        void ExecuteNovelty01Complete(float dt);
        void RunNovelty01PartialSimulation(float dt);
        void ExecuteNovelty01PreClassificationValidation();
        void ExecuteNovelty01PostCompactionLifecycle();
        void ExecuteNovelty01DirtyRangePropagation();
        void ExecuteNovelty01BuildIndirectArguments();
        void ExecuteNovelty01B4MigrationBaseline();
        bool RunNovelty01GroundTruth(float dt);
        void CompareNovelty01GroundTruth();
        void ValidateNovelty01FormalInvariants();
        void ShutdownNovelty01Routed();
        void WriteNovelty01FormalTelemetry();
        void ExecuteNovelty01RenderCacheUpdate();
        ID3D11ShaderResourceView* GetNovelty01RenderPositionSrv() const;
        bool DrawNovelty01HairIndirect(
            UINT vertexCountPerInstance,
            UINT instanceCount,
            UINT startVertexLocation,
            UINT startInstanceLocation);
'''
        if text.count(anchor) != 1:
            raise PatchError(f"{path}: private declaration anchor changed")
        text = text.replace(anchor, anchor + declarations, 1)

    member_marker = "        std::unique_ptr<Novelty01RuntimeState> m_novelty01Runtime;"
    if member_marker not in text:
        class_end = text.rfind("    };\n}")
        if class_end < 0:
            raise PatchError(f"{path}: class closing anchor not found")
        text = (
            text[:class_end] +
            "        std::unique_ptr<Novelty01RuntimeState> m_novelty01Runtime;\n\n" +
            text[class_end:]
        )

    path.write_text(text, encoding="utf-8", newline="\n")


def patch_telemetry_header(repo: Path) -> None:
    path = repo / "include/hairgpu/HairNoveltyTelemetry.h"
    text = path.read_text(encoding="utf-8")
    marker = "        uint32_t n01ExperimentVariant = 0u;"
    if marker in text:
        return
    anchor = "        uint32_t delayedTelemetryReadCount = 0u;\n"
    if text.count(anchor) != 1:
        raise PatchError(f"{path}: telemetry extension anchor changed")
    fields = r'''

        // Novelty 01 SG-APT v4 formal payload/range evidence.
        uint32_t n01ExperimentVariant = 0u;
        uint64_t n01FeatureMask = 0ull;
        uint32_t n01PayloadActiveCount = 0u;
        uint32_t n01PayloadAllocatedCount = 0u;
        uint32_t n01PayloadRetiredCount = 0u;
        uint32_t n01PayloadReusedCount = 0u;
        uint32_t n01PayloadOverflowCount = 0u;
        uint32_t n01PayloadOwnerMismatchCount = 0u;
        uint32_t n01PayloadVersionMismatchCount = 0u;
        uint32_t n01DuplicatePayloadOwnerCount = 0u;
        uint32_t n01InvalidPayloadIndexCount = 0u;
        uint32_t n01BlockSkipCount = 0u;
        uint32_t n01GuideDirtyRangeCount = 0u;
        uint32_t n01FollowerDirtyRangeCount = 0u;
        uint32_t n01RenderDirtyRangeCount = 0u;
        uint32_t n01FollowerRebuiltPointCount = 0u;
        uint32_t n01RenderRegeneratedVertexCount = 0u;
        uint32_t n01GroundTruthSampleCount = 0u;
        uint32_t n01FalseDynamicCount = 0u;
        uint32_t n01DescriptorViolationCount = 0u;
        uint32_t n01GroupReadBytes = 0u;
        uint32_t n01GroupWriteBytes = 0u;
        uint32_t n01PayloadReadBytes = 0u;
        uint32_t n01PayloadWriteBytes = 0u;
        uint32_t n01StateCopyBytes = 0u;
        uint32_t n01SafeStopCount = 0u;
'''
    text = text.replace(anchor, anchor + fields, 1)
    path.write_text(text, encoding="utf-8", newline="\n")


def patch_runtime_call_sites(repo: Path) -> None:
    parts = repo / "apps/dx11_viewer/src/GpuHairViewer_parts"
    for path in sorted(parts.glob("*.inl")):
        if path.name.startswith(("16_", "17_")):
            continue
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        changed = False
        output: list[str] = []
        for line in lines:
            new_line = line
            if ("ExecuteExperimentSimulationTick(" in line and
                    "GpuHairViewer::ExecuteExperimentSimulationTick" not in line):
                new_line = new_line.replace(
                    "ExecuteExperimentSimulationTick(",
                    "ExecuteNovelty01ExperimentTick(")
            if ("StepSingleFrame();" in line and
                    "GpuHairViewer::StepSingleFrame" not in line):
                new_line = new_line.replace(
                    "StepSingleFrame();", "StepNovelty01SingleFrame();")
            if new_line != line:
                changed = True
            output.append(new_line)
        if changed:
            path.write_text("".join(output), encoding="utf-8", newline="\n")


def patch_shutdown(repo: Path) -> None:
    path = repo / "apps/dx11_viewer/src/GpuHairViewer_parts/15_shutdown_and_tail.inl"
    text = path.read_text(encoding="utf-8")
    marker = "        m_novelty01Runtime.reset(); // N01 formal shutdown"
    if marker in text:
        return
    match = re.search(r"void\s+GpuHairViewer::Shutdown\s*\(\s*\)\s*\{", text)
    if not match:
        raise PatchError(f"{path}: Shutdown definition not found")
    insert_at = match.end()
    text = text[:insert_at] + "\n" + marker + "\n" + text[insert_at:]
    path.write_text(text, encoding="utf-8", newline="\n")


def patch_renderer(repo: Path) -> None:
    path = repo / "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl"
    text = path.read_text(encoding="utf-8")

    if "GetNovelty01RenderPositionSrv()" not in text:
        array_match = re.search(
            r"(ID3D11ShaderResourceView\s*\*\s*hairSrvs\s*\[\s*\]\s*=\s*\{)(.*?)(\};)",
            text, flags=re.DOTALL)
        if not array_match:
            raise PatchError(f"{path}: hair SRV array not found")
        body = array_match.group(2)
        old = "m_positions[m_currentBufferIndex].srv.Get()"
        if old not in body:
            raise PatchError(f"{path}: hair position SRV anchor not found")
        body = body.replace(old, "GetNovelty01RenderPositionSrv()", 1)
        text = text[:array_match.start(2)] + body + text[array_match.end(2):]

    if "DrawNovelty01HairIndirect(" not in text:
        pattern = re.compile(
            r"m_context->DrawInstanced\(\s*([^,;]+),\s*([^,;]+),\s*([^,;]+),\s*([^\);]+)\s*\);",
            flags=re.DOTALL)
        replacements: list[tuple[int, int, str]] = []
        for match in pattern.finditer(text):
            context = text[max(0, match.start() - 2200):match.start()]
            last_ribbon = max(context.rfind("m_ribbonVS"), context.rfind("m_ribbonOitPS"))
            last_scalp = max(context.rfind("m_scalpVS"), context.rfind("m_debugLineVS"))
            if last_ribbon > last_scalp:
                args = [match.group(i).strip() for i in range(1, 5)]
                replacement = (
                    "DrawNovelty01HairIndirect(\n"
                    f"                {args[0]},\n"
                    f"                {args[1]},\n"
                    f"                {args[2]},\n"
                    f"                {args[3]});")
                replacements.append((match.start(), match.end(), replacement))
        if not replacements:
            raise PatchError(
                f"{path}: no ribbon hair DrawInstanced call was identified; "
                "refusing to patch a guessed draw call")
        for start, end, replacement in reversed(replacements):
            text = text[:start] + replacement + text[end:]

    path.write_text(text, encoding="utf-8", newline="\n")


def patch_project(repo: Path) -> None:
    path = repo / "HairSystem.vcxproj"
    text = path.read_text(encoding="utf-8")
    shader = "HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    if shader not in text:
        text = text.replace(
            "HairSegmentGroupRuntime.hlsl HairSim.hlsl",
            f"HairSegmentGroupRuntime.hlsl {shader} HairSim.hlsl")
        text = text.replace(
            '    <ClInclude Include="include\\hairgpu\\Types.h" />',
            '    <ClInclude Include="include\\hairgpu\\SegmentGroupAdaptivePayload.h" />\n'
            '    <ClInclude Include="include\\hairgpu\\Types.h" />',
            1)
        text = text.replace(
            '    <None Include="apps\\dx11_viewer\\shaders\\HairSim.hlsl" />',
            '    <None Include="apps\\dx11_viewer\\shaders\\HairSegmentGroupAdaptivePayloadRuntime.hlsl" />\n'
            '    <None Include="apps\\dx11_viewer\\shaders\\HairSim.hlsl" />',
            1)
    if shader not in text:
        raise PatchError(f"{path}: failed to register Novelty 01 shader")
    path.write_text(text, encoding="utf-8", newline="\n")


def verify_integrated(repo: Path) -> None:
    required = {
        "apps/dx11_viewer/src/GpuHairViewer.h": [
            "~GpuHairViewer();", "struct Novelty01RuntimeState;",
            "m_novelty01Runtime", "GetNovelty01RenderPositionSrv",
            "DrawNovelty01HairIndirect"],
        "include/hairgpu/HairNoveltyTelemetry.h": [
            "n01PayloadActiveCount", "n01RenderDirtyRangeCount",
            "n01GroundTruthSampleCount", "n01StateCopyBytes"],
        "apps/dx11_viewer/src/GpuHairViewer.cpp": [
            "16_segment_group_adaptive_payload_complete.inl",
            "17_novelty01_formal_evidence.inl"],
        "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl": [
            "GetNovelty01RenderPositionSrv()", "DrawNovelty01HairIndirect("],
        "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl": [
            "ExecuteNovelty01RenderCacheUpdate", "DrawInstancedIndirect",
            "renderPositionCache", "N01UpdateFollowerRenderCacheCS"],
        "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl": [
            "N01_BINDING_MODE == 13", "N01UpdateGuideRenderCacheCS",
            "N01UpdateFollowerRenderCacheCS"],
        "HairSystem.vcxproj": ["HairSegmentGroupAdaptivePayloadRuntime.hlsl"],
    }
    missing: list[str] = []
    for relative, markers in required.items():
        path = repo / relative
        if not path.is_file():
            missing.append(f"missing file: {relative}")
            continue
        text = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in text:
                missing.append(f"{relative}: missing {marker}")
    if missing:
        raise PatchError("integration verification failed:\n  - " + "\n  - ".join(missing))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--branch", default=DEFAULT_BRANCH)
    parser.add_argument("--no-fetch", action="store_true")
    parser.add_argument("--reuse-branch", action="store_true")
    parser.add_argument("--skip-branch", action="store_true")
    args = parser.parse_args()
    package_root = Path(__file__).resolve().parents[1]
    repo = args.repo.resolve()

    try:
        if not (repo / ".git").exists():
            raise PatchError(f"not a Git repository: {repo}")
        status = run(repo, "git", "status", "--porcelain").stdout.strip()
        if status:
            raise PatchError(
                "working tree is not clean; commit, remove, or stash tracked and "
                "untracked changes first:\n" + status)

        if not args.skip_branch:
            if not args.no_fetch:
                print(run(repo, "git", "fetch", "origin", "main").stdout, end="")
            print(run(repo, "git", "switch", "main").stdout, end="")
            if not args.no_fetch:
                print(run(repo, "git", "pull", "--ff-only", "origin", "main").stdout, end="")
            head = run(repo, "git", "rev-parse", "HEAD").stdout.strip()
            print(f"Main HEAD: {head}")
            if head != BASE_COMMIT:
                print(
                    "WARNING: Main differs from the verified Version-68 base. "
                    "Exact anchors will still be required; guessed edits are disabled.",
                    file=sys.stderr)
            exists = run(
                repo, "git", "show-ref", "--verify", "--quiet",
                f"refs/heads/{args.branch}", check=False).returncode == 0
            if exists:
                if not args.reuse_branch:
                    raise PatchError(
                        f"branch already exists: {args.branch}; use --reuse-branch intentionally")
                print(run(repo, "git", "switch", args.branch).stdout, end="")
            else:
                print(run(repo, "git", "switch", "-c", args.branch).stdout, end="")

        copy_overlay(package_root, repo)
        patch_viewer_header(repo)
        patch_telemetry_header(repo)
        patch_runtime_call_sites(repo)
        patch_shutdown(repo)
        patch_renderer(repo)
        patch_project(repo)
        verify_integrated(repo)

        print(run(repo, "git", "diff", "--check").stdout, end="")
        preflight = repo / "tools/final_stage/hairgpu_novelty01_preflight.py"
        result = subprocess.run(
            [sys.executable, str(preflight), "--repo", str(repo)],
            cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(result.stdout, end="")
        if result.returncode != 0:
            raise PatchError("Novelty 01 preflight failed")

        print("\nModified/added files:")
        print(run(repo, "git", "status", "--short").stdout, end="")
        print("\nWindows verification still required:")
        print("  msbuild HairSystem.vcxproj /p:Configuration=Release /p:Platform=x64 /p:HairGpuFormalSelfTest=true")
        print("  python tools/final_stage/hairgpu_novelty01_preflight.py --repo . --require-fxc")
        return 0
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
