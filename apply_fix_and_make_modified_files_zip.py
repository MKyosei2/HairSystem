#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
HairSystem Novelty 61.21 guide-only simulation fix.

Run this script at the repository root of MKyosei2/HairSystem on the Novelty branch.
It modifies only the files listed in MODIFIED_FILES and then creates a ZIP containing
the full post-fix contents of those modified files only.
"""

from __future__ import annotations

from pathlib import Path
import argparse
import zipfile
import sys


MODIFIED_FILES = [
    "apps/dx11_viewer/shaders/HairSim.hlsl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl",
]


def replace_once(path: Path, old: str, new: str) -> str:
    text = path.read_text(encoding="utf-8")
    if new in text:
        return f"already patched: {path}"
    if old not in text:
        raise RuntimeError(f"Expected original block was not found in {path}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")
    return f"patched: {path}"


def patch_hair_sim(root: Path) -> str:
    path = root / "apps/dx11_viewer/shaders/HairSim.hlsl"
    old = """[numthreads(64, 1, 1)]
void IntegrateCS(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    uint totalParticles = TotalStrandCount * ParticlesPerStrand;
    if (particleIndex >= totalParticles)
    {
        return;
    }

    uint strandIndex = particleIndex / ParticlesPerStrand;
    uint localIndex = particleIndex % ParticlesPerStrand;
    float3 current = InputPositions[particleIndex].xyz;
    float3 previous = InputPreviousPositions[particleIndex].xyz;

    if (localIndex == 0)
    {
        float3 boundRoot = EvaluateBoundRoot(strandIndex);
        OutputPositions[particleIndex] = float4(boundRoot, 1.0f);
        OutputPreviousPositions[particleIndex] = float4(boundRoot, 1.0f);
        return;
    }

    if (strandIndex >= GuideStrandCount)
    {
        OutputPositions[particleIndex] = float4(current, 1.0f);
        OutputPreviousPositions[particleIndex] = float4(previous, 1.0f);
        return;
    }

    float localT = (ParticlesPerStrand > 1) ? (float(localIndex) / float(ParticlesPerStrand - 1)) : 0.0f;
    float shockScale = lerp(VelocityShockPropagation, 1.0f, localT);
    float3 velocity = (current - previous) * (1.0f - Damping) * shockScale;
    float windPulse = 0.65f + 0.35f * sin(ElapsedTime * 1.3f + particleIndex * 0.003f);
    float3 acceleration = float3(0.0f, -Gravity, 0.0f) + SafeNormalize(WindDirection) * WindStrength * windPulse;
    float3 next = current + velocity + acceleration * (DeltaTime * DeltaTime);

    if (localIndex == 1)
    {
        float3 boundRoot = EvaluateBoundRoot(strandIndex);
        float3 restRoot = RestPositions[strandIndex * ParticlesPerStrand].xyz;
        float3 restCurrent = RestPositions[particleIndex].xyz;
        float3 tetherDir = SafeNormalize(restCurrent - restRoot);
        float3 tetherTarget = boundRoot + tetherDir * SegmentLength;
        next = lerp(next, tetherTarget, RootStiffness);
    }

    float freezeFactor = ComputeBackFreezeFactor(strandIndex, localIndex);
    if (freezeFactor > 0.0f)
    {
        next = lerp(next, current, freezeFactor * FreezeBlend);
    }

    OutputPreviousPositions[particleIndex] = float4(current, 1.0f);
    OutputPositions[particleIndex] = float4(next, 1.0f);
}"""
    new = """[numthreads(64, 1, 1)]
void IntegrateCS(uint3 id : SV_DispatchThreadID)
{
    // Guide-only integration domain.
    //
    // The full Rapunzel preview asset is:
    //   6000 guides * (16 followers + 1 guide) * 128 particles = 13,056,000 particles.
    // Dispatching all particles here requires 204,000 thread groups at 64 threads/group,
    // which exceeds D3D11's 65,535 groups-per-dimension limit.
    //
    // Followers are reconstructed by FollowCS from guide motion, so this pass should only
    // integrate guide particles. Collision, self-collision, and constraint passes already
    // operate on guide-particle dispatch counts.
    uint dispatchIndex = id.x;
    uint guideParticleDispatchCount = GetGuideParticleDispatchCount();
    if (dispatchIndex >= guideParticleDispatchCount)
    {
        return;
    }

    uint particleIndex = ResolveGuideParticleIndex(dispatchIndex);
    if (particleIndex == 0xFFFFFFFFu)
    {
        return;
    }

    uint strandIndex = particleIndex / ParticlesPerStrand;
    uint localIndex = particleIndex % ParticlesPerStrand;
    if (strandIndex >= GuideStrandCount)
    {
        return;
    }

    float3 current = InputPositions[particleIndex].xyz;
    float3 previous = InputPreviousPositions[particleIndex].xyz;

    if (localIndex == 0)
    {
        float3 boundRoot = EvaluateBoundRoot(strandIndex);
        OutputPositions[particleIndex] = float4(boundRoot, 1.0f);
        OutputPreviousPositions[particleIndex] = float4(boundRoot, 1.0f);
        return;
    }

    float localT = (ParticlesPerStrand > 1) ? (float(localIndex) / float(ParticlesPerStrand - 1)) : 0.0f;
    float shockScale = lerp(VelocityShockPropagation, 1.0f, localT);
    float3 velocity = (current - previous) * (1.0f - Damping) * shockScale;
    float windPulse = 0.65f + 0.35f * sin(ElapsedTime * 1.3f + particleIndex * 0.003f);
    float3 acceleration = float3(0.0f, -Gravity, 0.0f) + SafeNormalize(WindDirection) * WindStrength * windPulse;
    float3 next = current + velocity + acceleration * (DeltaTime * DeltaTime);

    if (localIndex == 1)
    {
        float3 boundRoot = EvaluateBoundRoot(strandIndex);
        float3 restRoot = RestPositions[strandIndex * ParticlesPerStrand].xyz;
        float3 restCurrent = RestPositions[particleIndex].xyz;
        float3 tetherDir = SafeNormalize(restCurrent - restRoot);
        float3 tetherTarget = boundRoot + tetherDir * SegmentLength;
        next = lerp(next, tetherTarget, RootStiffness);
    }

    float freezeFactor = ComputeBackFreezeFactor(strandIndex, localIndex);
    if (freezeFactor > 0.0f)
    {
        next = lerp(next, current, freezeFactor * FreezeBlend);
    }

    OutputPreviousPositions[particleIndex] = float4(current, 1.0f);
    OutputPositions[particleIndex] = float4(next, 1.0f);
}"""
    return replace_once(path, old, new)


def patch_runtime_update(root: Path) -> str:
    path = root / "apps/dx11_viewer/src/GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl"
    old = """        m_context->CSSetShader(m_integrateCS.Get(), nullptr, 0);
        m_context->Dispatch(DispatchGroups(totalParticleCount, 64u), 1, 1);"""
    new = """        m_context->CSSetShader(m_integrateCS.Get(), nullptr, 0);
        // IntegrateCS is guide-only; followers are updated by FollowCS.
        // This keeps the 6000/16/128 asset while avoiding Dispatch(204000,1,1).
        m_context->Dispatch(guideParticleGroups, 1, 1);"""
    return replace_once(path, old, new)


def patch_compacted_guide_point_count(root: Path) -> str:
    path = root / "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl"
    old = """                    compactedGuidePointCount += m_asset.strands[guideIndex].pointCount;"""
    new = """                    const uint32_t pointCount = m_asset.strands[guideIndex].pointCount;
                    // Compacted guide-point dispatch in HairSim.hlsl excludes roots.
                    compactedGuidePointCount += (pointCount > 1u) ? (pointCount - 1u) : 0u;"""
    return replace_once(path, old, new)


def patch_dispatch_groups(root: Path) -> str:
    path = root / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl"
    old = """        UINT DispatchGroups(UINT itemCount, UINT groupSize)
        {
            return (itemCount + groupSize - 1u) / groupSize;
        }"""
    new = """        UINT DispatchGroups(UINT itemCount, UINT groupSize)
        {
            if (itemCount == 0u || groupSize == 0u)
            {
                return 0u;
            }

            const uint64_t groups =
                (static_cast<uint64_t>(itemCount) + static_cast<uint64_t>(groupSize) - 1ull) /
                static_cast<uint64_t>(groupSize);

            if (groups > D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION)
            {
                OutputDebugStringA(
                    "HairSystem: DispatchGroups exceeded the D3D11 per-dimension limit. "
                    "Use guide-only, active-set, indirect, 2D, or chunked dispatch for this pass.\\n");
                return 0u;
            }

            return static_cast<UINT>(groups);
        }"""
    return replace_once(path, old, new)


def create_modified_files_zip(root: Path, out_zip: Path) -> None:
    if out_zip.exists():
        out_zip.unlink()
    with zipfile.ZipFile(out_zip, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for rel in MODIFIED_FILES:
            path = root / rel
            if not path.exists():
                raise FileNotFoundError(path)
            z.write(path, rel)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=".", help="HairSystem repository root")
    parser.add_argument("--out-zip", default="HairSystem_Novelty_61_21_modified_files_only_FULL.zip")
    parser.add_argument("--no-zip", action="store_true")
    args = parser.parse_args()

    root = Path(args.repo_root).resolve()

    required = root / "apps/dx11_viewer/shaders/HairSim.hlsl"
    if not required.exists():
        print(f"ERROR: {required} not found. Run this at the HairSystem repository root.", file=sys.stderr)
        return 2

    messages = [
        patch_hair_sim(root),
        patch_runtime_update(root),
        patch_compacted_guide_point_count(root),
        patch_dispatch_groups(root),
    ]
    for msg in messages:
        print(msg)

    if not args.no_zip:
        out_zip = (root / args.out_zip).resolve()
        create_modified_files_zip(root, out_zip)
        print(f"created modified-files-only ZIP: {out_zip}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
