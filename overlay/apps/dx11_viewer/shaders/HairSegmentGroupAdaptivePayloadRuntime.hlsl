// HairSegmentGroupAdaptivePayloadRuntime.hlsl
// SG-APT safety-first optional dynamic payload runtime.
// Shader Model: cs_5_0
//
// This file is intentionally separate from HairSegmentGroupRuntime.hlsl.
// It reuses the current 112-byte SegmentGroupGpuData ABI and stores the
// optional-payload index in the low 24 bits of flags. The high bits record
// validity and explicit baseline fallback.
//
// Register contract:
//   b1: SgaptPayloadConstants
//   t0: current positions
//   t1: previous positions
//   t2: compacted dynamic group indices
//   t3: compact counters (dynamic count at index 0)
//   u0: segment groups
//   u1: dynamic payload pool
//   u2: payload pool counters
//   u3: previous positions (pre-solve restore only)

static const uint SGAPT_FLAG_PAYLOAD_VALID = 0x80000000u;
static const uint SGAPT_FLAG_PAYLOAD_FALLBACK = 0x40000000u;
static const uint SGAPT_PAYLOAD_INDEX_MASK = 0x00ffffffu;

struct SegmentGroupGpuData
{
    uint strandId;
    uint groupId;
    uint parentGroupId;
    uint childGroupId;

    uint startPointIndex;
    uint endPointIndex;
    uint ownerGroupId;
    uint blockId;

    uint state;
    uint previousState;
    uint dirtyReason;
    uint flags;

    float restLength;
    float currentLength;
    float motionScore;
    float collisionRisk;

    float3 direction;
    float boundaryErrorMm;

    float3 previousDirection;
    float errorBoundMm;

    uint lastSolvedFrame;
    uint lastRepairFrame;
    uint stateStableFrameCount;
    uint reserved;
};

struct SegmentDynamicPayloadGpuData
{
    float3 previousEndPosition;
    uint allocationEpoch;

    float3 velocity;
    uint payloadVersion;
};

cbuffer SgaptPayloadConstants : register(b1)
{
    uint gSgaptGroupCount;
    uint gSgaptPayloadCapacity;
    uint gSgaptFrameIndex;
    uint gSgaptDynamicState;

    float gSgaptDeltaTime;
    uint gSgaptInvalidPayloadIndex;
    uint gSgaptPayloadIndexMask;
    uint gSgaptFlags;
};

StructuredBuffer<float4> CurrentPositionsIn : register(t0);
StructuredBuffer<float4> PreviousPositionsIn : register(t1);
StructuredBuffer<uint> DynamicGroupIndicesIn : register(t2);
StructuredBuffer<uint> CompactCountsIn : register(t3);

RWStructuredBuffer<SegmentGroupGpuData> SegmentGroupsOut : register(u0);
RWStructuredBuffer<SegmentDynamicPayloadGpuData> DynamicPayloadsOut : register(u1);
RWStructuredBuffer<uint> PayloadPoolCounters : register(u2);
RWStructuredBuffer<float4> PreviousPositionsOut : register(u3);

bool SgaptHasPayload(SegmentGroupGpuData group)
{
    return (group.flags & SGAPT_FLAG_PAYLOAD_VALID) != 0u;
}

bool SgaptUsesFallback(SegmentGroupGpuData group)
{
    return (group.flags & SGAPT_FLAG_PAYLOAD_FALLBACK) != 0u;
}

uint SgaptPayloadIndex(SegmentGroupGpuData group)
{
    return group.flags & gSgaptPayloadIndexMask;
}

void SgaptSetPayload(inout SegmentGroupGpuData group, uint payloadIndex)
{
    group.flags =
        (group.flags & ~(SGAPT_FLAG_PAYLOAD_VALID |
                         SGAPT_FLAG_PAYLOAD_FALLBACK |
                         gSgaptPayloadIndexMask)) |
        SGAPT_FLAG_PAYLOAD_VALID |
        (payloadIndex & gSgaptPayloadIndexMask);
}

void SgaptSetFallback(inout SegmentGroupGpuData group)
{
    group.flags =
        (group.flags & ~(SGAPT_FLAG_PAYLOAD_VALID |
                         gSgaptPayloadIndexMask)) |
        SGAPT_FLAG_PAYLOAD_FALLBACK;
}

void SgaptClearPayloadState(inout SegmentGroupGpuData group)
{
    group.flags &=
        ~(SGAPT_FLAG_PAYLOAD_VALID |
          SGAPT_FLAG_PAYLOAD_FALLBACK |
          gSgaptPayloadIndexMask);
}

float3 SgaptSafeVelocity(float3 currentValue, float3 previousValue)
{
    const float safeDt = max(gSgaptDeltaTime, 1.0e-6f);
    const float3 value = (currentValue - previousValue) / safeDt;
    return all(isfinite(value)) ? value : float3(0.0f, 0.0f, 0.0f);
}

// Allocates a persistent sidecar when a group first becomes dynamic.
// Retired indices are deliberately not returned to the pool during the run.
// Once capacity is exhausted, the existing full-position path remains active
// and the group is explicitly marked as fallback.
[numthreads(64, 1, 1)]
void SgaptPayloadTransitionCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupId = dispatchThreadId.x;
    if (groupId >= gSgaptGroupCount)
    {
        return;
    }

    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    const bool isDynamic = group.state == gSgaptDynamicState;
    const bool hasPayload = SgaptHasPayload(group);
    const bool fallback = SgaptUsesFallback(group);

    if (isDynamic)
    {
        if (!hasPayload && !fallback)
        {
            uint payloadIndex;
            InterlockedAdd(PayloadPoolCounters[0u], 1u, payloadIndex);

            if (payloadIndex < gSgaptPayloadCapacity &&
                payloadIndex < gSgaptInvalidPayloadIndex)
            {
                const float3 currentEnd =
                    CurrentPositionsIn[group.endPointIndex].xyz;
                const float3 previousEnd =
                    PreviousPositionsIn[group.endPointIndex].xyz;

                SegmentDynamicPayloadGpuData payload;
                payload.previousEndPosition =
                    all(isfinite(previousEnd)) ? previousEnd : currentEnd;
                payload.allocationEpoch = gSgaptFrameIndex;
                payload.velocity =
                    SgaptSafeVelocity(currentEnd, payload.previousEndPosition);
                payload.payloadVersion = group.reserved + 1u;
                DynamicPayloadsOut[payloadIndex] = payload;

                SgaptSetPayload(group, payloadIndex);
                group.reserved = payload.payloadVersion;

                uint ignored;
                InterlockedAdd(PayloadPoolCounters[1u], 1u, ignored);
                InterlockedAdd(PayloadPoolCounters[4u], 1u, ignored);
                InterlockedAdd(PayloadPoolCounters[7u], 1u, ignored);
            }
            else
            {
                SgaptSetFallback(group);
                uint ignored;
                InterlockedAdd(PayloadPoolCounters[2u], 1u, ignored);
                InterlockedAdd(PayloadPoolCounters[5u], 1u, ignored);
            }

            SegmentGroupsOut[groupId] = group;
        }
        return;
    }

    // A retired slot is kept quarantined until ResetSimulation. This prevents
    // same-frame reuse and stale GPU references without requiring a CPU fence.
    if (hasPayload)
    {
        const uint payloadIndex = SgaptPayloadIndex(group);
        if (payloadIndex < gSgaptPayloadCapacity)
        {
            SegmentDynamicPayloadGpuData payload =
                DynamicPayloadsOut[payloadIndex];
            payload.allocationEpoch = 0x80000000u | gSgaptFrameIndex;
            DynamicPayloadsOut[payloadIndex] = payload;

            uint ignored;
            InterlockedAdd(PayloadPoolCounters[1u], 0xffffffffu, ignored);
            InterlockedAdd(PayloadPoolCounters[3u], 1u, ignored);
            InterlockedAdd(PayloadPoolCounters[7u], 1u, ignored);
        }

        SgaptClearPayloadState(group);
        SegmentGroupsOut[groupId] = group;
    }
    else if (fallback)
    {
        // Allow a new allocation attempt only after the group has left dynamic.
        SgaptClearPayloadState(group);
        SegmentGroupsOut[groupId] = group;
    }
}

// Restores the dynamic path's previous position from the optional payload.
// A fallback group is intentionally untouched and continues to use the
// baseline PreviousPositions buffer.
[numthreads(64, 1, 1)]
void SgaptPayloadPreSolveCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount = CompactCountsIn[0u];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupId = DynamicGroupIndicesIn[compactedIndex];
    if (groupId >= gSgaptGroupCount)
    {
        return;
    }

    const SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    if (!SgaptHasPayload(group))
    {
        return;
    }

    const uint payloadIndex = SgaptPayloadIndex(group);
    if (payloadIndex >= gSgaptPayloadCapacity)
    {
        return;
    }

    const SegmentDynamicPayloadGpuData payload =
        DynamicPayloadsOut[payloadIndex];
    const float4 oldPrevious =
        PreviousPositionsOut[group.endPointIndex];

    PreviousPositionsOut[group.endPointIndex] =
        float4(payload.previousEndPosition, oldPrevious.w);

    uint ignored;
    InterlockedAdd(PayloadPoolCounters[6u], 1u, ignored);
}

// Captures the final dynamic result after solve and velocity recovery.
[numthreads(64, 1, 1)]
void SgaptPayloadPostSolveCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount = CompactCountsIn[0u];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupId = DynamicGroupIndicesIn[compactedIndex];
    if (groupId >= gSgaptGroupCount)
    {
        return;
    }

    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    if (!SgaptHasPayload(group))
    {
        return;
    }

    const uint payloadIndex = SgaptPayloadIndex(group);
    if (payloadIndex >= gSgaptPayloadCapacity)
    {
        SgaptSetFallback(group);
        SegmentGroupsOut[groupId] = group;
        return;
    }

    const float3 currentEnd =
        CurrentPositionsIn[group.endPointIndex].xyz;
    const float3 previousEnd =
        PreviousPositionsIn[group.endPointIndex].xyz;

    SegmentDynamicPayloadGpuData payload =
        DynamicPayloadsOut[payloadIndex];
    payload.previousEndPosition =
        all(isfinite(previousEnd)) ? previousEnd : currentEnd;
    payload.velocity =
        SgaptSafeVelocity(currentEnd, payload.previousEndPosition);
    payload.payloadVersion = group.reserved;
    DynamicPayloadsOut[payloadIndex] = payload;

    uint ignored;
    InterlockedAdd(PayloadPoolCounters[7u], 1u, ignored);
}
