#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

EXPECTED_MAIN_SHA = "00fbf320b24315a343362be13712cb9ff04bb8ac"
DEFAULT_BRANCH = "feature/novelty01-sgapt-core"


def run(repo: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        list(args),
        cwd=repo,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if check and result.returncode != 0:
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(args)}\n{result.stdout}")
    return result


def replace_once(path: Path, old: str, new: str, marker: str) -> None:
    text = path.read_text(encoding="utf-8")
    if marker in text:
        return
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{path}: expected exactly one patch anchor, found {count}.\n"
            "Main may have changed; do not apply a guessed patch."
        )
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def copy_overlay(package_root: Path, repo: Path) -> None:
    overlay = package_root / "overlay"
    if not overlay.is_dir():
        raise RuntimeError(f"overlay directory not found: {overlay}")
    for source in overlay.rglob("*"):
        if not source.is_file():
            continue
        relative = source.relative_to(overlay)
        target = repo / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def patch_header(repo: Path) -> None:
    path = repo / "apps/dx11_viewer/src/GpuHairViewer.h"

    replace_once(
        path,
        '#include "hairgpu/HairNoveltyTelemetry.h"\n',
        '#include "hairgpu/HairNoveltyTelemetry.h"\n'
        '#include "hairgpu/SegmentGroupAdaptivePayload.h"\n',
        '#include "hairgpu/SegmentGroupAdaptivePayload.h"',
    )

    replace_once(
        path,
        '        void ResolveSegmentGroupTelemetry();\n',
        '        void ResolveSegmentGroupTelemetry();\n'
        '        bool CreateSgaptPayloadResources();\n'
        '        void ResetSgaptPayloadResources();\n'
        '        bool UpdateSgaptPayloadConstants(float dt);\n'
        '        void ExecuteSgaptPayloadTransitions();\n'
        '        void ExecuteSgaptPayloadPreSolve();\n'
        '        void ExecuteSgaptPayloadPostSolve();\n',
        '        bool CreateSgaptPayloadResources();',
    )

    replace_once(
        path,
        '        uint32_t m_segmentGroupBlockCount = 0u;\n',
        '        uint32_t m_segmentGroupBlockCount = 0u;\n'
        '        uint32_t m_sgaptPayloadCapacity = 0u;\n',
        '        uint32_t m_sgaptPayloadCapacity = 0u;',
    )

    replace_once(
        path,
        '        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupTelemetryReduceCS;\n',
        '        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupTelemetryReduceCS;\n'
        '        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_sgaptPayloadTransitionCS;\n'
        '        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_sgaptPayloadPreSolveCS;\n'
        '        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_sgaptPayloadPostSolveCS;\n',
        '        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_sgaptPayloadTransitionCS;',
    )

    replace_once(
        path,
        '        StructuredBuffer m_segmentGroupRuntimeCountersBuffer;\n',
        '        StructuredBuffer m_segmentGroupRuntimeCountersBuffer;\n'
        '        StructuredBuffer m_sgaptDynamicPayloadBuffer;\n'
        '        StructuredBuffer m_sgaptPayloadPoolCountersBuffer;\n',
        '        StructuredBuffer m_sgaptDynamicPayloadBuffer;',
    )

    replace_once(
        path,
        '        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentGroupRuntimeConstants;\n',
        '        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentGroupRuntimeConstants;\n'
        '        Microsoft::WRL::ComPtr<ID3D11Buffer> m_sgaptPayloadConstants;\n',
        '        Microsoft::WRL::ComPtr<ID3D11Buffer> m_sgaptPayloadConstants;',
    )


def patch_resources(repo: Path) -> None:
    path = repo / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
    replace_once(
        path,
        '        if (!CreateSegmentGroupIndirectArgsResources())\n',
        '        if (!CreateSgaptPayloadResources())\n'
        '        {\n'
        '            return false;\n'
        '        }\n\n'
        '        if (!CreateSegmentGroupIndirectArgsResources())\n',
        '        if (!CreateSgaptPayloadResources())',
    )


def patch_simulation(repo: Path) -> None:
    path = repo / "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl"

    shader_anchor = (
        '        segmentGroupShadersReady &= loadSegmentGroupShader('
        '"SegmentGroupTelemetryReduceCS", m_segmentGroupTelemetryReduceCS);\n'
    )
    shader_replacement = shader_anchor + (
        '\n'
        '        auto loadSgaptPayloadShader =\n'
        '            [&](const char* entryPoint, ComPtr<ID3D11ComputeShader>& destination) -> bool\n'
        '        {\n'
        '            ComPtr<ID3DBlob> blob;\n'
        '            std::wstring localError;\n'
        '            if (!CompileShaderFromFile(\n'
        '                    L"HairSegmentGroupAdaptivePayloadRuntime.hlsl",\n'
        '                    entryPoint,\n'
        '                    "cs_5_0",\n'
        '                    &blob,\n'
        '                    localError) ||\n'
        '                FAILED(m_device->CreateComputeShader(\n'
        '                    blob->GetBufferPointer(),\n'
        '                    blob->GetBufferSize(),\n'
        '                    nullptr,\n'
        '                    &destination)))\n'
        '            {\n'
        '                errorText = localError.empty()\n'
        '                    ? std::wstring(L"Failed to create SG-APT payload shader.")\n'
        '                    : localError;\n'
        '                destination.Reset();\n'
        '                return false;\n'
        '            }\n'
        '            return true;\n'
        '        };\n'
        '        segmentGroupShadersReady &= loadSgaptPayloadShader(\n'
        '            "SgaptPayloadTransitionCS", m_sgaptPayloadTransitionCS);\n'
        '        segmentGroupShadersReady &= loadSgaptPayloadShader(\n'
        '            "SgaptPayloadPreSolveCS", m_sgaptPayloadPreSolveCS);\n'
        '        segmentGroupShadersReady &= loadSgaptPayloadShader(\n'
        '            "SgaptPayloadPostSolveCS", m_sgaptPayloadPostSolveCS);\n'
    )
    replace_once(
        path,
        shader_anchor,
        shader_replacement,
        '            "SgaptPayloadTransitionCS", m_sgaptPayloadTransitionCS);',
    )

    reset_anchor = (
        '        m_forceAllSegmentGroupsDirty = true;\n'
        '    }\n\n'
        '    void GpuHairViewer::DetectSegmentGroupDirtyEvents()\n'
    )
    reset_replacement = (
        '        ResetSgaptPayloadResources();\n'
        '        m_forceAllSegmentGroupsDirty = true;\n'
        '    }\n\n'
        '    void GpuHairViewer::DetectSegmentGroupDirtyEvents()\n'
    )
    replace_once(
        path,
        reset_anchor,
        reset_replacement,
        '        ResetSgaptPayloadResources();\n        m_forceAllSegmentGroupsDirty = true;',
    )

    replace_once(
        path,
        '        if (!UpdateSegmentGroupRuntimeConstants(dt)) return;\n'
        '        DetectSegmentGroupDirtyEvents();\n',
        '        if (!UpdateSegmentGroupRuntimeConstants(dt)) return;\n'
        '        if (!UpdateSgaptPayloadConstants(dt)) return;\n'
        '        DetectSegmentGroupDirtyEvents();\n',
        '        if (!UpdateSgaptPayloadConstants(dt)) return;',
    )

    pre_anchor = (
        '        BeginGpuPassTiming(GpuPassId::Compaction); '
        'CompactDynamicSegmentGroups(); BuildSegmentGroupDispatchArguments(); '
        'EndGpuPassTiming(GpuPassId::Compaction);\n'
        '        BeginGpuPassTiming(GpuPassId::StaticTransform); '
        'TransformStaticSegmentGroups(); '
        'EndGpuPassTiming(GpuPassId::StaticTransform);\n'
        '        BeginGpuPassTiming(GpuPassId::DynamicSolve); '
        'SolveDynamicSegmentGroups(dt); '
        'EndGpuPassTiming(GpuPassId::DynamicSolve);\n'
    )
    pre_replacement = (
        '        ExecuteSgaptPayloadTransitions();\n'
        '        BeginGpuPassTiming(GpuPassId::Compaction); '
        'CompactDynamicSegmentGroups(); BuildSegmentGroupDispatchArguments(); '
        'EndGpuPassTiming(GpuPassId::Compaction);\n'
        '        BeginGpuPassTiming(GpuPassId::StaticTransform); '
        'TransformStaticSegmentGroups(); '
        'EndGpuPassTiming(GpuPassId::StaticTransform);\n'
        '        ExecuteSgaptPayloadPreSolve();\n'
        '        BeginGpuPassTiming(GpuPassId::DynamicSolve); '
        'SolveDynamicSegmentGroups(dt); '
        'EndGpuPassTiming(GpuPassId::DynamicSolve);\n'
    )
    # The sequence exists in both StepSegmentSleep and StepIntegratedSegmentGroup.
    # Patch only the final occurrence, which is the proposal method.
    text = path.read_text(encoding="utf-8")
    if '        ExecuteSgaptPayloadTransitions();\n' not in text:
        index = text.rfind(pre_anchor)
        if index < 0:
            raise RuntimeError(f"{path}: proposal pipeline anchor not found.")
        text = text[:index] + pre_replacement + text[index + len(pre_anchor):]
        path.write_text(text, encoding="utf-8", newline="\n")

    post_anchor = (
        '        if (m_experimentCase.appliedFlags.enableVelocityRecovery)\n'
        '        {\n'
        '            BeginGpuPassTiming(GpuPassId::VelocityRecovery); '
        'ExecuteVelocityRecovery(dt); '
        'EndGpuPassTiming(GpuPassId::VelocityRecovery);\n'
        '        }\n'
        '        if (m_experimentCase.appliedFlags.enableRepair)\n'
    )
    post_replacement = (
        '        if (m_experimentCase.appliedFlags.enableVelocityRecovery)\n'
        '        {\n'
        '            BeginGpuPassTiming(GpuPassId::VelocityRecovery); '
        'ExecuteVelocityRecovery(dt); '
        'EndGpuPassTiming(GpuPassId::VelocityRecovery);\n'
        '        }\n'
        '        ExecuteSgaptPayloadPostSolve();\n'
        '        if (m_experimentCase.appliedFlags.enableRepair)\n'
    )
    replace_once(
        path,
        post_anchor,
        post_replacement,
        '        ExecuteSgaptPayloadPostSolve();',
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create a branch from Main and apply the Novelty 01 SG-APT core overlay."
    )
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--branch", default=DEFAULT_BRANCH)
    parser.add_argument("--no-fetch", action="store_true")
    parser.add_argument("--reuse-branch", action="store_true")
    args = parser.parse_args()

    package_root = Path(__file__).resolve().parents[1]
    repo = args.repo.resolve()

    try:
        if not (repo / ".git").exists():
            raise RuntimeError(f"not a Git repository: {repo}")

        # Only tracked changes block the operation. The package itself lives
        # outside the repository and is copied after branch creation.
        if run(repo, "git", "diff", "--quiet", check=False).returncode != 0:
            raise RuntimeError("tracked working-tree changes exist; commit or stash them first.")
        if run(repo, "git", "diff", "--cached", "--quiet", check=False).returncode != 0:
            raise RuntimeError("staged changes exist; commit or unstage them first.")

        if not args.no_fetch:
            print(run(repo, "git", "fetch", "origin", "main").stdout, end="")

        print(run(repo, "git", "switch", "main").stdout, end="")
        if not args.no_fetch:
            print(run(repo, "git", "pull", "--ff-only", "origin", "main").stdout, end="")

        main_sha = run(repo, "git", "rev-parse", "HEAD").stdout.strip()
        print(f"Main HEAD: {main_sha}")
        if main_sha != EXPECTED_MAIN_SHA:
            print(
                "WARNING: Main is newer/different than Version 67. "
                "The applier will continue only if every exact anchor still matches.",
                file=sys.stderr,
            )

        branch_exists = (
            run(
                repo,
                "git",
                "show-ref",
                "--verify",
                "--quiet",
                f"refs/heads/{args.branch}",
                check=False,
            ).returncode
            == 0
        )
        if branch_exists:
            if not args.reuse_branch:
                raise RuntimeError(
                    f"local branch already exists: {args.branch}\n"
                    "Use --reuse-branch only when you intentionally want to continue it."
                )
            print(run(repo, "git", "switch", args.branch).stdout, end="")
        else:
            print(run(repo, "git", "switch", "-c", args.branch).stdout, end="")

        copy_overlay(package_root, repo)
        patch_header(repo)
        patch_resources(repo)
        patch_simulation(repo)

        print(run(repo, "git", "diff", "--check").stdout, end="")

        verifier = package_root / "tools/verify_novelty01_sgapt.py"
        verify = subprocess.run(
            [sys.executable, str(verifier), "--repo", str(repo)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        print(verify.stdout, end="")
        if verify.returncode != 0:
            raise RuntimeError("static verification failed.")

        print("\nApplied files:")
        print(run(repo, "git", "status", "--short").stdout, end="")
        print("\nNext commands:")
        print("  1. Build Release x64 in Visual Studio 2022")
        print("  2. Run the shader self-test")
        print("  3. Run B0/B2/B5/P smoke cases before long formal tests")
        print("  4. git add <reviewed files>")
        print('  5. git commit -m "feat(novelty01): add SG-APT optional dynamic payload core"')
        print(f"  6. git push -u origin {args.branch}")
        return 0

    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
