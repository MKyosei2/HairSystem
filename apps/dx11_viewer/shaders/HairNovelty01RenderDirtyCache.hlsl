// HairNovelty01RenderDirtyCache.hlsl
// Persistent render-position cache consumer for SG-APT RenderDirtyRange.
// Shader Model 5.0 / DirectX 11.

struct RenderDirtyRange
{
    uint strandIndex;
    uint pointBegin;
    uint pointEnd;
    uint flags;
};

cbuffer Novelty01RenderCacheConstants : register(b0)
{
    uint gGuideCount;
    uint gFollowerCount;
    uint gParticlesPerStrand;
    uint gTotalPointCount;
};

StructuredBuffer<float4> N01RenderSourcePositions : register(t0);
StructuredBuffer<RenderDirtyRange> N01RenderDirtyRanges : register(t1);
RWStructuredBuffer<float4> N01PersistentRenderPositions : register(u0);

[numthreads(256, 1, 1)]
void N01CopyGuideRenderCacheCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pointIndex = dispatchThreadId.x;
    const uint guidePointCount = gGuideCount * gParticlesPerStrand;
    if (pointIndex >= guidePointCount || pointIndex >= gTotalPointCount)
    {
        return;
    }
    N01PersistentRenderPositions[pointIndex] =
        N01RenderSourcePositions[pointIndex];
}

[numthreads(256, 1, 1)]
void N01CopyFollowerRenderCacheCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint totalFollowerPointCount =
        gFollowerCount * gParticlesPerStrand;
    const uint linearPointIndex = dispatchThreadId.x;
    if (linearPointIndex >= totalFollowerPointCount ||
        gParticlesPerStrand == 0u)
    {
        return;
    }

    const uint followerIndex =
        linearPointIndex / gParticlesPerStrand;
    const uint localPointIndex =
        linearPointIndex -
        followerIndex * gParticlesPerStrand;

    const RenderDirtyRange range = N01RenderDirtyRanges[followerIndex];
    const uint expectedStrandIndex = gGuideCount + followerIndex;
    if (range.strandIndex != expectedStrandIndex ||
        range.pointBegin == 0xffffffffu ||
        range.pointEnd < range.pointBegin ||
        localPointIndex < range.pointBegin ||
        localPointIndex > range.pointEnd)
    {
        return;
    }

    const uint pointIndex =
        expectedStrandIndex * gParticlesPerStrand + localPointIndex;
    if (pointIndex >= gTotalPointCount)
    {
        return;
    }

    N01PersistentRenderPositions[pointIndex] =
        N01RenderSourcePositions[pointIndex];
}
