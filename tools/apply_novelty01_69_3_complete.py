#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

SOURCE_BRANCH = "Novelty01"
SOURCE_COMMIT = "a0521e258acdbd9d71262172d6d2ba208c338eeb"
BASE_MAIN_COMMIT = "8d19bb546f3505fe25436d6ffa5b390a9b83897a"
DEFAULT_OUTPUT_BRANCH = "feature/novelty01-69-3-complete"


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
        raise PatchError(
            f"{path}: expected exactly one anchor, found {count}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def copy_replacement(package_root: Path, repo: Path) -> None:
    source_root = package_root / "replacement"
    if not source_root.is_dir():
        raise PatchError(f"replacement tree is missing: {source_root}")
    for source in source_root.rglob("*"):
        if not source.is_file():
            continue
        relative = source.relative_to(source_root)
        target = repo / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def patch_viewer_header(repo: Path) -> None:
    path = repo / "apps/dx11_viewer/src/GpuHairViewer.h"
    text = path.read_text(encoding="utf-8")

    if "#include <memory>" not in text:
        if text.count("#include <limits>\n") != 1:
            raise PatchError(f"{path}: <limits> include anchor changed")
        text = text.replace("#include <limits>\n", "#include <limits>\n#include <memory>\n", 1)
    if '#include "hairgpu/SegmentGroupAdaptivePayload.h"' not in text:
        anchor = '#include "hairgpu/RenderData.h"\n'
        if text.count(anchor) != 1:
            raise PatchError(f"{path}: RenderData include anchor changed")
        text = text.replace(
            anchor,
            anchor + '#include "hairgpu/SegmentGroupAdaptivePayload.h"\n', 1)
    if "~GpuHairViewer();" not in text:
        anchor = "        bool Initialize(HINSTANCE instance, int showCommand);\n"
        if text.count(anchor) != 1:
            raise PatchError(f"{path}: public declaration anchor changed")
        text = text.replace(anchor, anchor + "        ~GpuHairViewer();\n", 1)

    if "        struct Novelty01RuntimeState;\n" not in text:
        anchor = "        struct StrandEditSnapshot;\n"
        declarations = r'''

        // Novelty 01 formal SG-APT integration. These declarations are part of
        // the class contract; access-control macro injection is forbidden.
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
            raise PatchError(f"{path}: StrandEditSnapshot anchor changed")
        text = text.replace(anchor, anchor + declarations, 1)

    member = "        std::unique_ptr<Novelty01RuntimeState> m_novelty01Runtime;"
    if member not in text:
        class_end = text.rfind("    };\n}")
        if class_end < 0:
            raise PatchError(f"{path}: class closing anchor not found")
        text = text[:class_end] + member + "\n\n" + text[class_end:]

    path.write_text(text, encoding="utf-8", newline="\n")


def patch_telemetry_header(repo: Path) -> None:
    path = repo / "include/hairgpu/HairNoveltyTelemetry.h"
    text = path.read_text(encoding="utf-8")
    availability_marker = "HairTelemetryAvailability_Novelty01Sgapt"
    if availability_marker not in text:
        availability_anchor = (
            "        HairTelemetryAvailability_ResultIntegrity = 1ull << 14u\n")
        if availability_anchor not in text:
            raise PatchError(f"{path}: availability enum anchor changed")
        text = text.replace(
            availability_anchor,
            "        HairTelemetryAvailability_ResultIntegrity = 1ull << 14u,\n"
            "        HairTelemetryAvailability_Novelty01Sgapt = 1ull << 15u\n",
            1)
    if "        uint32_t n01ExperimentVariant = 0u;" in text:
        path.write_text(text, encoding="utf-8", newline="\n")
        return
    anchor = "        uint32_t delayedTelemetryReadCount = 0u;\n"
    if text.count(anchor) != 1:
        raise PatchError(f"{path}: delayedTelemetryReadCount anchor changed")
    fields = r'''

        // Novelty 01 SG-APT formal evidence. A zero is valid only when the
        // matching availability/sample count proves that the metric was read.
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
    path.write_text(text.replace(anchor, anchor + fields, 1), encoding="utf-8", newline="\n")


def patch_runtime_call_sites(repo: Path) -> None:
    parts = repo / "apps/dx11_viewer/src/GpuHairViewer_parts"
    for path in sorted(parts.glob("*.inl")):
        if path.name.startswith(("16_", "17_")):
            continue
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        output: list[str] = []
        changed = False
        for line in lines:
            updated = line
            if ("ExecuteExperimentSimulationTick(" in line and
                    "GpuHairViewer::ExecuteExperimentSimulationTick" not in line):
                updated = updated.replace(
                    "ExecuteExperimentSimulationTick(",
                    "ExecuteNovelty01ExperimentTick(")
            if ("StepSingleFrame();" in line and
                    "GpuHairViewer::StepSingleFrame" not in line):
                updated = updated.replace(
                    "StepSingleFrame();", "StepNovelty01SingleFrame();")
            changed |= updated != line
            output.append(updated)
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
    text = text[:match.end()] + "\n" + marker + "\n" + text[match.end():]
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
            last_other = max(context.rfind("m_scalpVS"), context.rfind("m_debugLineVS"))
            if last_ribbon > last_other:
                args = [match.group(i).strip() for i in range(1, 5)]
                replacement = (
                    "DrawNovelty01HairIndirect(\n"
                    f"                {args[0]},\n"
                    f"                {args[1]},\n"
                    f"                {args[2]},\n"
                    f"                {args[3]});")
                replacements.append((match.start(), match.end(), replacement))
        if not replacements:
            raise PatchError(f"{path}: no ribbon DrawInstanced call found")
        for start, end, replacement in reversed(replacements):
            text = text[:start] + replacement + text[end:]

    path.write_text(text, encoding="utf-8", newline="\n")


def patch_project(repo: Path) -> None:
    path = repo / "HairSystem.vcxproj"
    text = path.read_text(encoding="utf-8")
    shader = "HairSegmentGroupAdaptivePayloadRuntime.hlsl"

    # Register in both Debug and Release post-build loops.
    text = text.replace(
        "HairSegmentGroupRuntime.hlsl HairSim.hlsl",
        f"HairSegmentGroupRuntime.hlsl {shader} HairSim.hlsl")

    if 'include\\hairgpu\\SegmentGroupAdaptivePayload.h' not in text:
        anchor = '    <ClInclude Include="include\\hairgpu\\Types.h" />'
        if text.count(anchor) != 1:
            raise PatchError(f"{path}: Types.h project anchor changed")
        text = text.replace(
            anchor,
            '    <ClInclude Include="include\\hairgpu\\SegmentGroupAdaptivePayload.h" />\n' + anchor,
            1)

    if 'apps\\dx11_viewer\\shaders\\HairSegmentGroupAdaptivePayloadRuntime.hlsl' not in text:
        anchor = '    <None Include="apps\\dx11_viewer\\shaders\\HairSim.hlsl" />'
        if text.count(anchor) != 1:
            raise PatchError(f"{path}: HairSim project anchor changed")
        text = text.replace(
            anchor,
            '    <None Include="apps\\dx11_viewer\\shaders\\HairSegmentGroupAdaptivePayloadRuntime.hlsl" />\n' + anchor,
            1)

    # Fail before copy if either required formal shader is missing.
    required_anchor = (
        'if not exist "$(ProjectDir)apps\\dx11_viewer\\shaders\\HairSegmentGroupRuntime.hlsl" (')
    n01_check = (
        'if not exist "$(ProjectDir)apps\\dx11_viewer\\shaders\\HairSegmentGroupAdaptivePayloadRuntime.hlsl" (\n\n'
        '  echo ERROR: required Novelty 01 shader is missing: HairSegmentGroupAdaptivePayloadRuntime.hlsl\n\n'
        '  exit /b 1\n\n'
        ')\n\n')
    if "required Novelty 01 shader is missing" not in text:
        if text.count(required_anchor) != 2:
            raise PatchError(f"{path}: expected Debug and Release shader checks")
        text = text.replace(required_anchor, n01_check + required_anchor)

    if shader not in text:
        raise PatchError(f"{path}: Novelty 01 shader registration failed")
    path.write_text(text, encoding="utf-8", newline="\n")


def patch_design_document(package_root: Path, repo: Path) -> None:
    path = repo / "設計書/01_TopNovelty_NewStrandDataType.md"
    text = path.read_text(encoding="utf-8")
    replacements = {
        "| 対象ブランチ               | `main`                                     |":
            "| 対象ブランチ               | `Novelty01`                                |",
        "| 基準バージョン              | Version 67                                 |":
            "| 基準バージョン              | Version 69.3 integration                   |",
        "| 基準コミット               | `00fbf320b24315a343362be13712cb9ff04bb8ac` |":
            "| 基準コミット               | `a0521e258acdbd9d71262172d6d2ba208c338eeb` |",
        "| 現在の実装状況              | Segment Group試作あり。正式なSG-APTは未完成            |":
            "| 現在の実装状況              | Source正式統合済み。Windows Release x64／FXC／実機Formal検証前 |",
    }
    for old, new in replacements.items():
        if old in text:
            text = text.replace(old, new, 1)
        elif new not in text:
            raise PatchError(f"{path}: metadata anchor changed: {old}")

    marker = "# 49. Version 69.3 実装統合情報"
    if marker not in text:
        appendix = (
            package_root / "design_appendix/NOVELTY01_69_3_DESIGN_APPENDIX.md"
        ).read_text(encoding="utf-8")
        text = text.rstrip() + "\n" + appendix.lstrip("\n")
    path.write_text(text, encoding="utf-8", newline="\n")


def remove_duplicate_distribution_sources(repo: Path) -> None:
    overlay = repo / "overlay"
    if overlay.exists():
        shutil.rmtree(overlay)
    for relative in (
        "tools/apply_novelty01_complete.py",
        "tools/apply_novelty01_sgapt.py",
        "tools/verify_novelty01_sgapt.py",
    ):
        path = repo / relative
        if path.exists():
            path.unlink()


def restore_unrelated_novelty04(repo: Path) -> None:
    relative = "設計書/04_TopNovelty_PerStrandEditingSystem.md"
    result = run(
        repo, "git", "restore", "--source=origin/main", "--", relative,
        check=False)
    if result.returncode != 0:
        print(
            "WARNING: unrelated Novelty04 document could not be restored from "
            "origin/main. Review it manually before commit.\n" + result.stdout,
            file=sys.stderr)


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
            "proposalTimingHistory", "fullDynamicDecisionFrames",
            "Feature_HardSoftDescriptor |", "DrawInstancedIndirect",
            "HAIRGPU_N01_STATIC_POSITION_MM"],
        "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl": [
            "N01_BINDING_MODE == 13", "N01UpdateGuideRenderCacheCS",
            "N01UpdateFollowerRenderCacheCS"],
        "tools/final_stage/hairgpu_novelty01_matrix.py": [
            "CANONICAL_SCENARIO_ROOT_RELATIVE", "load_frozen_environment",
            "NOVELTY01_FORMAL_CONTRACT.json"],
        "HairSystem.vcxproj": [
            "HairSegmentGroupAdaptivePayloadRuntime.hlsl",
            "SegmentGroupAdaptivePayload.h"],
        "設計書/01_TopNovelty_NewStrandDataType.md": [
            "# 49. Version 69.3 実装統合情報",
            "# 60. Version 69.3 Merge Gate",
            SOURCE_COMMIT],
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
    if (repo / "overlay").exists():
        missing.append("overlay directory still exists")
    aggregator = repo / "apps/dx11_viewer/src/GpuHairViewer.cpp"
    if aggregator.is_file() and "#define private" in aggregator.read_text(encoding="utf-8"):
        missing.append("GpuHairViewer.cpp still contains #define private")
    if missing:
        raise PatchError("integration verification failed:\n  - " + "\n  - ".join(missing))



def export_modified_source_zip(repo: Path, output_path: Path) -> None:
    """Export complete post-apply contents of every changed source/config file.

    This is intentionally evaluated after integration so large existing files
    such as GpuHairViewer.h are exported in their exact patched 69.3 form.
    Deleted paths are recorded in a manifest instead of being recreated.
    """
    def git_paths(*arguments: str) -> list[str]:
        output = run(repo, "git", *arguments).stdout
        return [line.strip() for line in output.splitlines() if line.strip()]

    changed = set(git_paths(
        "diff", "--name-only", "--diff-filter=ACMRTUXB"))
    changed.update(git_paths(
        "diff", "--cached", "--name-only", "--diff-filter=ACMRTUXB"))
    changed.update(git_paths(
        "ls-files", "--others", "--exclude-standard"))

    deleted = set(git_paths(
        "diff", "--name-only", "--diff-filter=D"))
    deleted.update(git_paths(
        "diff", "--cached", "--name-only", "--diff-filter=D"))

    output_path = output_path.resolve()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        output_path.unlink()
    with zipfile.ZipFile(
            output_path, "w", compression=zipfile.ZIP_DEFLATED,
            compresslevel=9) as archive:
        for relative in sorted(changed):
            candidate = repo / relative
            if candidate.is_file():
                archive.write(candidate, Path(relative).as_posix())
        archive.writestr(
            "DELETED_FILES.txt",
            "\n".join(sorted(deleted)) + ("\n" if deleted else ""))
    print(f"Exported complete modified-file snapshot: {output_path}")


def prepare_branch(repo: Path, branch: str, no_fetch: bool, reuse_branch: bool) -> None:
    if not no_fetch:
        print(run(repo, "git", "fetch", "origin", SOURCE_BRANCH, "main").stdout, end="")
    print(run(repo, "git", "switch", SOURCE_BRANCH).stdout, end="")
    if not no_fetch:
        print(run(repo, "git", "pull", "--ff-only", "origin", SOURCE_BRANCH).stdout, end="")
    head = run(repo, "git", "rev-parse", "HEAD").stdout.strip()
    print(f"{SOURCE_BRANCH} HEAD: {head}")
    if head != SOURCE_COMMIT:
        print(
            "WARNING: Novelty01 differs from the verified 69.2 source. Exact "
            "anchors are still required and guessed edits remain disabled.",
            file=sys.stderr)
    exists = run(
        repo, "git", "show-ref", "--verify", "--quiet",
        f"refs/heads/{branch}", check=False).returncode == 0
    if exists:
        if not reuse_branch:
            raise PatchError(
                f"branch already exists: {branch}; use --reuse-branch intentionally")
        print(run(repo, "git", "switch", branch).stdout, end="")
    else:
        print(run(repo, "git", "switch", "-c", branch).stdout, end="")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply the complete Novelty 01 69.3 integration to a clean HairSystem checkout.")
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--branch", default=DEFAULT_OUTPUT_BRANCH)
    parser.add_argument("--no-fetch", action="store_true")
    parser.add_argument("--reuse-branch", action="store_true")
    parser.add_argument("--skip-branch", action="store_true")
    parser.add_argument(
        "--export-modified-zip", type=Path,
        help="After applying, export complete contents of all modified files to this ZIP.")
    args = parser.parse_args()

    package_root = Path(__file__).resolve().parents[1]
    repo = args.repo.resolve()
    try:
        if not (repo / ".git").exists():
            raise PatchError(f"not a Git repository: {repo}")
        status = run(repo, "git", "status", "--porcelain").stdout.strip()
        if status:
            raise PatchError(
                "working tree is not clean; commit, remove, or stash all changes first:\n" + status)

        if not args.skip_branch:
            prepare_branch(repo, args.branch, args.no_fetch, args.reuse_branch)

        copy_replacement(package_root, repo)
        patch_viewer_header(repo)
        patch_telemetry_header(repo)
        patch_runtime_call_sites(repo)
        patch_shutdown(repo)
        patch_renderer(repo)
        patch_project(repo)
        patch_design_document(package_root, repo)
        remove_duplicate_distribution_sources(repo)
        restore_unrelated_novelty04(repo)
        verify_integrated(repo)

        print(run(repo, "git", "diff", "--check").stdout, end="")
        preflight = repo / "tools/final_stage/hairgpu_novelty01_preflight.py"
        result = subprocess.run(
            [sys.executable, str(preflight), "--repo", str(repo)],
            cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(result.stdout, end="")
        if result.returncode != 0:
            raise PatchError("Novelty 01 source preflight failed")

        print("\nModified/added/deleted files:")
        print(run(repo, "git", "status", "--short").stdout, end="")
        if args.export_modified_zip is not None:
            export_modified_source_zip(repo, args.export_modified_zip)
        print("\nMandatory Windows verification:")
        print("  tools\\final_stage\\Run_Novelty01_Complete_Verification.bat")
        print("\nDo not merge until Release x64, FXC, smoke, determinism, soak, and GTX1660 evidence are recorded.")
        return 0
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
