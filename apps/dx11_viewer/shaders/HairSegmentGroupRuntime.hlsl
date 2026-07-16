// HairSegmentGroupRuntime.hlsl
// Formal experiment kernels for Segment-Group Certified Strand Data Type.
// Shader Model: cs_5_0
//
// Register contract
//   t0: segment groups                 u0: writable segment groups / compact output
//   t1: segment-group blocks          u1: certificates / static compact output
//   t2: certificates                  u2: runtime counters / repair compact output
//   t3: current particle positions    u3: current particle positions / dispatch args
//   t4: previous particle positions   u4: previous particle positions / telemetry
//   t5: dynamic compacted indices     u5: wake/repair tracking
//   t6: static compacted indices      u6: verification records
//   t7: repair compacted indices
//
// The viewer binds only the resources used by an entry point. The duplicated
// logical roles are intentional so the file can expose small, independently
// measurable passes without a monolithic kernel.

#ifndef HAIRGPU_COMPACTION_BINDINGS
#define HAIRGPU_COMPACTION_BINDINGS 0
#endif

static const uint INVALID_GROUP_ID = 0xffffffffu;
static const uint GROUP_STATE_STATIC_CACHED = 0u;
static const uint GROUP_STATE_STATIC_TRANSFORMED = 1u;
static const uint GROUP_STATE_DYNAMIC = 2u;
static const uint GROUP_STATE_DEFERRED = 3u;
static const uint GROUP_STATE_REPAIR = 4u;

static const uint DIRTY_PARENT_MOVED = 1u << 0u;
static const uint DIRTY_HEAD_MOVED = 1u << 1u;
static const uint DIRTY_COLLIDER_ENTERED = 1u << 2u;
static const uint DIRTY_COLLIDER_CHANGED = 1u << 3u;
static const uint DIRTY_NEIGHBOR_CHANGED = 1u << 4u;
static const uint DIRTY_EDITED = 1u << 5u;
static const uint DIRTY_CACHE_EXPIRED = 1u << 6u;
static const uint DIRTY_BOUNDARY_FAILED = 1u << 7u;
static const uint DIRTY_WATCHDOG = 1u << 8u;

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

struct SegmentGroupBlockGpuData
{
    uint firstGroupIndex;
    uint groupCount;
    uint strandId;
    uint stateMask;

    uint dirtyMask;
    uint dynamicGroupCount;
    uint repairGroupCount;
    uint reserved0;

    float maxMotionScore;
    float maxCollisionRisk;
    float maxBoundaryErrorMm;
    float reserved1;
};

struct SegmentGroupCertificateGpuData
{
    uint sourceFrame;
    uint parentVersion;
    uint colliderVersion;
    uint editVersion;

    float maximumPositionErrorMm;
    float maximumDirectionErrorDeg;
    float maximumLengthErrorRatio;
    float maximumBoundaryErrorMm;

    uint parentValid;
    uint collisionValid;
    uint boundaryValid;
    uint editValid;

    uint cacheValid;
    uint reserved0;
    uint reserved1;
    uint reserved2;
};

struct WakeTelemetryRecord
{
    uint requested;
    uint groundTruthDynamic;
    uint requestFrame;
    uint wakeFrame;
};

struct RepairTrackingRecord
{
    uint attemptFrame;
    uint verifyAfterFrame;
    uint reasonMask;
    uint status; // 0 none, 1 pending, 2 verified, 3 failed
};

cbuffer SegmentGroupRuntimeConstants : register(b0)
{
    uint gGroupCount;
    uint gBlockCount;
    uint gFrameIndex;
    uint gGroupsPerBlock;

    uint gDynamicCompactCount;
    uint gStaticCompactCount;
    uint gRepairCompactCount;
    uint gRepairVerificationHoldFrames;

    float gDeltaTime;
    float gMotionWakeThreshold;
    float gCollisionWakeThreshold;
    float gStaticMotionThreshold;

    float gLengthErrorThreshold;
    float gBoundaryErrorThresholdMm;
    float gVelocityRecoveryBlend;
    float gPositionRecoveryBlend;

    float3 gRootTranslation;
    float gPadding0;
};

StructuredBuffer<SegmentGroupGpuData> SegmentGroupsIn : register(t0);
StructuredBuffer<SegmentGroupBlockGpuData> SegmentBlocksIn : register(t1);
StructuredBuffer<SegmentGroupCertificateGpuData> CertificatesIn : register(t2);
StructuredBuffer<float4> CurrentPositionsIn : register(t3);
StructuredBuffer<float4> PreviousPositionsIn : register(t4);
StructuredBuffer<uint> DynamicGroupIndicesIn : register(t5);
StructuredBuffer<uint> StaticGroupIndicesIn : register(t6);
StructuredBuffer<uint> RepairGroupIndicesIn : register(t7);
StructuredBuffer<uint> CompactCountsIn : register(t8);

#if HAIRGPU_COMPACTION_BINDINGS
// Compaction/indirect-argument kernels use compact uint lists and raw args.
RWStructuredBuffer<uint> DynamicGroupIndicesOut : register(u0);
RWStructuredBuffer<uint> StaticGroupIndicesOut : register(u1);
RWStructuredBuffer<uint> RepairGroupIndicesOut : register(u2);
RWStructuredBuffer<uint> CompactCounters : register(u3);  // indices 0,1,2
RWByteAddressBuffer DynamicDispatchArgs : register(u4);   // uint3 at offset 0
RWByteAddressBuffer StaticDispatchArgs : register(u5);    // uint3 at offset 0
RWByteAddressBuffer RepairDispatchArgs : register(u6);    // uint3 at offset 0
#else
// Normal runtime kernels use typed records and particle buffers.
RWStructuredBuffer<SegmentGroupGpuData> SegmentGroupsOut : register(u0);
RWStructuredBuffer<SegmentGroupBlockGpuData> SegmentBlocksOut : register(u1);
RWStructuredBuffer<SegmentGroupCertificateGpuData> CertificatesOut : register(u2);
RWStructuredBuffer<float4> CurrentPositionsOut : register(u3);
RWStructuredBuffer<float4> PreviousPositionsOut : register(u4);
RWStructuredBuffer<WakeTelemetryRecord> WakeTelemetryOut : register(u5);
RWStructuredBuffer<RepairTrackingRecord> RepairTrackingOut : register(u6);
#endif
RWStructuredBuffer<uint> RuntimeCounters : register(u7);

bool Finite3(float3 value)
{
    return all(isfinite(value));
}

float SafeLength(float3 value)
{
    const float squaredLength = dot(value, value);
    return sqrt(max(squaredLength, 1.0e-12f));
}

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthValue = SafeLength(value);
    return lengthValue > 1.0e-6f ? value / lengthValue : fallback;
}

void CounterAdd(uint byteOffset, uint amount)
{
    uint originalValue;
    InterlockedAdd(RuntimeCounters[byteOffset / 4u], amount, originalValue);
}

#if !HAIRGPU_COMPACTION_BINDINGS
[numthreads(64, 1, 1)]
void SegmentGroupBlockClassifyCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint blockId = dispatchThreadId.x;
    if (blockId >= gBlockCount)
    {
        return;
    }

    SegmentGroupBlockGpuData block = SegmentBlocksOut[blockId];
    uint stateMask = 0u;
    uint dynamicCount = 0u;
    uint repairCount = 0u;
    float maxMotion = 0.0f;
    float maxCollision = 0.0f;
    float maxBoundary = 0.0f;

    [loop]
    for (uint localIndex = 0u; localIndex < block.groupCount; ++localIndex)
    {
        const uint groupId = block.firstGroupIndex + localIndex;
        if (groupId >= gGroupCount)
        {
            break;
        }
        const SegmentGroupGpuData group = SegmentGroupsIn[groupId];
        stateMask |= 1u << min(group.state, 31u);
        dynamicCount += group.state == GROUP_STATE_DYNAMIC ? 1u : 0u;
        repairCount += group.state == GROUP_STATE_REPAIR ? 1u : 0u;
        maxMotion = max(maxMotion, group.motionScore);
        maxCollision = max(maxCollision, group.collisionRisk);
        maxBoundary = max(maxBoundary, group.boundaryErrorMm);
    }

    block.stateMask = stateMask;
    block.dynamicGroupCount = dynamicCount;
    block.repairGroupCount = repairCount;
    block.maxMotionScore = maxMotion;
    block.maxCollisionRisk = maxCollision;
    block.maxBoundaryErrorMm = maxBoundary;
    SegmentBlocksOut[blockId] = block;
}

[numthreads(64, 1, 1)]
void SegmentGroupClassifyCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupId = dispatchThreadId.x;
    if (groupId >= gGroupCount)
    {
        return;
    }

    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    SegmentGroupCertificateGpuData certificate = CertificatesOut[groupId];
    const float3 currentStart = CurrentPositionsIn[group.startPointIndex].xyz;
    const float3 currentEnd = CurrentPositionsIn[group.endPointIndex].xyz;
    const float3 segment = currentEnd - currentStart;
    const float currentLength = SafeLength(segment);
    const float lengthError = abs(currentLength - group.restLength) / max(group.restLength, 1.0e-6f);
    const bool dirty = group.dirtyReason != 0u;
    const bool finite = Finite3(currentStart) && Finite3(currentEnd);
    const bool certificateValid =
        !dirty && finite &&
        certificate.parentValid != 0u &&
        certificate.collisionValid != 0u &&
        certificate.boundaryValid != 0u &&
        certificate.editValid != 0u &&
        certificate.cacheValid != 0u &&
        lengthError <= gLengthErrorThreshold &&
        group.motionScore <= gStaticMotionThreshold &&
        group.collisionRisk <= gCollisionWakeThreshold;

    group.previousState = group.state;
    if (!finite || lengthError > gLengthErrorThreshold || group.boundaryErrorMm > gBoundaryErrorThresholdMm)
    {
        group.state = GROUP_STATE_REPAIR;
    }
    else if (certificateValid)
    {
        group.state = GROUP_STATE_STATIC_CACHED;
        group.stateStableFrameCount += 1u;
    }
    else
    {
        group.state = GROUP_STATE_DYNAMIC;
        group.stateStableFrameCount = 0u;
    }
    group.currentLength = currentLength;
    group.direction = SafeNormalize(segment, group.direction);
    SegmentGroupsOut[groupId] = group;

    certificate.sourceFrame = gFrameIndex;
    certificate.maximumLengthErrorRatio = lengthError;
    certificate.maximumBoundaryErrorMm = group.boundaryErrorMm;
    certificate.cacheValid = certificateValid ? 1u : 0u;
    CertificatesOut[groupId] = certificate;
}

[numthreads(64, 1, 1)]
void SegmentGroupPredictiveWakeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupId = dispatchThreadId.x;
    if (groupId >= gGroupCount)
    {
        return;
    }

    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    const bool neighborDynamic =
        (group.parentGroupId != INVALID_GROUP_ID && SegmentGroupsOut[group.parentGroupId].state == GROUP_STATE_DYNAMIC) ||
        (group.childGroupId != INVALID_GROUP_ID && SegmentGroupsOut[group.childGroupId].state == GROUP_STATE_DYNAMIC);
    const bool versionOrEditDirty = (group.dirtyReason &
        (DIRTY_PARENT_MOVED | DIRTY_HEAD_MOVED | DIRTY_COLLIDER_ENTERED |
         DIRTY_COLLIDER_CHANGED | DIRTY_NEIGHBOR_CHANGED | DIRTY_EDITED)) != 0u;
    const bool shouldWake =
        group.motionScore >= gMotionWakeThreshold ||
        group.collisionRisk >= gCollisionWakeThreshold ||
        neighborDynamic || versionOrEditDirty;

    WakeTelemetryRecord telemetry = WakeTelemetryOut[groupId];
    telemetry.groundTruthDynamic = group.state == GROUP_STATE_DYNAMIC || group.state == GROUP_STATE_REPAIR ? 1u : 0u;
    if (shouldWake && group.state <= GROUP_STATE_STATIC_TRANSFORMED)
    {
        group.previousState = group.state;
        group.state = GROUP_STATE_DYNAMIC;
        group.dirtyReason |= DIRTY_NEIGHBOR_CHANGED;
        telemetry.requested = 1u;
        telemetry.requestFrame = gFrameIndex;
        telemetry.wakeFrame = gFrameIndex;
        SegmentGroupsOut[groupId] = group;
    }
    WakeTelemetryOut[groupId] = telemetry;
}

#endif

#if HAIRGPU_COMPACTION_BINDINGS
[numthreads(64, 1, 1)]
void SegmentGroupCompactDynamicCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupId = dispatchThreadId.x;
    if (groupId >= gGroupCount)
    {
        return;
    }

    const SegmentGroupGpuData group = SegmentGroupsIn[groupId];
    uint outputIndex;
    if (group.state == GROUP_STATE_DYNAMIC)
    {
        InterlockedAdd(CompactCounters[0u], 1u, outputIndex);
        DynamicGroupIndicesOut[outputIndex] = groupId;
    }
    else if (group.state == GROUP_STATE_STATIC_CACHED || group.state == GROUP_STATE_STATIC_TRANSFORMED)
    {
        InterlockedAdd(CompactCounters[1u], 1u, outputIndex);
        StaticGroupIndicesOut[outputIndex] = groupId;
    }
    else if (group.state == GROUP_STATE_REPAIR)
    {
        InterlockedAdd(CompactCounters[2u], 1u, outputIndex);
        RepairGroupIndicesOut[outputIndex] = groupId;
    }
}

[numthreads(64, 1, 1)]
void SegmentGroupCompactStaticCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupId = dispatchThreadId.x;
    if (groupId >= gGroupCount)
    {
        return;
    }
    const SegmentGroupGpuData group = SegmentGroupsIn[groupId];
    if (group.state == GROUP_STATE_STATIC_CACHED || group.state == GROUP_STATE_STATIC_TRANSFORMED)
    {
        uint outputIndex;
        InterlockedAdd(CompactCounters[1u], 1u, outputIndex);
        StaticGroupIndicesOut[outputIndex] = groupId;
    }
}

[numthreads(1, 1, 1)]
void SegmentGroupBuildDispatchArgsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint dynamicCount = CompactCounters[0u];
    const uint staticCount = CompactCounters[1u];
    const uint repairCount = CompactCounters[2u];

    DynamicDispatchArgs.Store(0u, (dynamicCount + 63u) / 64u);
    DynamicDispatchArgs.Store(4u, 1u);
    DynamicDispatchArgs.Store(8u, 1u);
    StaticDispatchArgs.Store(0u, (staticCount + 63u) / 64u);
    StaticDispatchArgs.Store(4u, 1u);
    StaticDispatchArgs.Store(8u, 1u);
    RepairDispatchArgs.Store(0u, (repairCount + 63u) / 64u);
    RepairDispatchArgs.Store(4u, 1u);
    RepairDispatchArgs.Store(8u, 1u);
}

#endif

#if !HAIRGPU_COMPACTION_BINDINGS
[numthreads(64, 1, 1)]
void SegmentGroupStaticTransformCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    if (compactedIndex >= CompactCountsIn[1u])
    {
        return;
    }
    const uint groupId = StaticGroupIndicesIn[compactedIndex];
    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    const uint pointIndex = group.endPointIndex;
    float4 current = CurrentPositionsOut[pointIndex];
    current.xyz += gRootTranslation;
    CurrentPositionsOut[pointIndex] = current;
    group.previousState = group.state;
    group.state = GROUP_STATE_STATIC_TRANSFORMED;
    SegmentGroupsOut[groupId] = group;
}

[numthreads(64, 1, 1)]
void SegmentGroupDynamicSolveCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    if (compactedIndex >= CompactCountsIn[0u])
    {
        return;
    }
    const uint groupId = DynamicGroupIndicesIn[compactedIndex];
    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    const uint startIndex = group.startPointIndex;
    const uint endIndex = group.endPointIndex;
    const float3 start = CurrentPositionsOut[startIndex].xyz;
    float4 endPosition = CurrentPositionsOut[endIndex];
    const float3 oldEnd = endPosition.xyz;
    const float3 direction = SafeNormalize(endPosition.xyz - start, group.direction);
    const float3 targetEnd = start + direction * group.restLength;
    endPosition.xyz = lerp(endPosition.xyz, targetEnd, saturate(gPositionRecoveryBlend));
    CurrentPositionsOut[endIndex] = endPosition;
    PreviousPositionsOut[endIndex] = float4(oldEnd, endPosition.w);
    group.previousDirection = group.direction;
    group.direction = SafeNormalize(endPosition.xyz - start, group.direction);
    group.currentLength = SafeLength(endPosition.xyz - start);
    group.lastSolvedFrame = gFrameIndex;
    group.dirtyReason = 0u;
    SegmentGroupsOut[groupId] = group;
}

[numthreads(64, 1, 1)]
void SegmentGroupVelocityRecoveryCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    if (compactedIndex >= CompactCountsIn[0u])
    {
        return;
    }
    const uint groupId = DynamicGroupIndicesIn[compactedIndex];
    const SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    const uint pointIndex = group.endPointIndex;
    const float4 current = CurrentPositionsOut[pointIndex];
    float4 previous = PreviousPositionsOut[pointIndex];
    const float3 recoveredPrevious = current.xyz - group.direction * max(group.motionScore, 0.0f) * gDeltaTime;
    previous.xyz = lerp(previous.xyz, recoveredPrevious, saturate(gVelocityRecoveryBlend));
    PreviousPositionsOut[pointIndex] = previous;
}

[numthreads(64, 1, 1)]
void SegmentGroupRepairSolveCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    if (compactedIndex >= CompactCountsIn[2u])
    {
        return;
    }
    const uint groupId = RepairGroupIndicesIn[compactedIndex];
    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    const float4 startPosition = CurrentPositionsOut[group.startPointIndex];
    float4 endPosition = CurrentPositionsOut[group.endPointIndex];
    const bool finiteInput = Finite3(startPosition.xyz) && Finite3(endPosition.xyz);
    const float3 fallbackDirection = Finite3(group.previousDirection) ? group.previousDirection : float3(0.0f, 1.0f, 0.0f);
    const float3 repairedDirection = SafeNormalize(endPosition.xyz - startPosition.xyz, fallbackDirection);
    endPosition.xyz = startPosition.xyz + repairedDirection * max(group.restLength, 1.0e-6f);
    CurrentPositionsOut[group.endPointIndex] = endPosition;
    PreviousPositionsOut[group.endPointIndex] = endPosition;

    RepairTrackingRecord tracking;
    tracking.attemptFrame = gFrameIndex;
    tracking.verifyAfterFrame = gFrameIndex + max(gRepairVerificationHoldFrames, 1u);
    tracking.reasonMask = group.dirtyReason;
    tracking.status = finiteInput ? 1u : 3u;
    RepairTrackingOut[groupId] = tracking;

    group.previousState = group.state;
    group.state = finiteInput ? GROUP_STATE_DYNAMIC : GROUP_STATE_REPAIR;
    group.lastRepairFrame = gFrameIndex;
    group.direction = repairedDirection;
    group.currentLength = group.restLength;
    SegmentGroupsOut[groupId] = group;
}

[numthreads(64, 1, 1)]
void SegmentGroupBoundaryRepairCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    if (compactedIndex >= CompactCountsIn[2u])
    {
        return;
    }
    const uint groupId = RepairGroupIndicesIn[compactedIndex];
    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    if (group.parentGroupId != INVALID_GROUP_ID)
    {
        const SegmentGroupGpuData parent = SegmentGroupsOut[group.parentGroupId];
        const float3 parentEnd = CurrentPositionsOut[parent.endPointIndex].xyz;
        float4 startPosition = CurrentPositionsOut[group.startPointIndex];
        startPosition.xyz = parentEnd;
        CurrentPositionsOut[group.startPointIndex] = startPosition;
        group.boundaryErrorMm = 0.0f;
        group.dirtyReason &= ~DIRTY_BOUNDARY_FAILED;
        SegmentGroupsOut[groupId] = group;
    }
}

[numthreads(64, 1, 1)]
void SegmentGroupCacheUpdateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupId = dispatchThreadId.x;
    if (groupId >= gGroupCount)
    {
        return;
    }
    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    SegmentGroupCertificateGpuData certificate = CertificatesOut[groupId];
    if (group.state == GROUP_STATE_STATIC_CACHED || group.state == GROUP_STATE_STATIC_TRANSFORMED)
    {
        certificate.sourceFrame = gFrameIndex;
        certificate.cacheValid = 1u;
        certificate.maximumLengthErrorRatio =
            abs(group.currentLength - group.restLength) / max(group.restLength, 1.0e-6f);
        certificate.maximumBoundaryErrorMm = group.boundaryErrorMm;
        CertificatesOut[groupId] = certificate;
    }
}

[numthreads(64, 1, 1)]
void SegmentGroupWatchdogCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupId = dispatchThreadId.x;
    if (groupId >= gGroupCount)
    {
        return;
    }
    SegmentGroupGpuData group = SegmentGroupsOut[groupId];
    const float3 start = CurrentPositionsOut[group.startPointIndex].xyz;
    const float3 end = CurrentPositionsOut[group.endPointIndex].xyz;
    const float lengthValue = SafeLength(end - start);
    const float lengthError = abs(lengthValue - group.restLength) / max(group.restLength, 1.0e-6f);
    if (!Finite3(start) || !Finite3(end) || lengthError > gLengthErrorThreshold || group.boundaryErrorMm > gBoundaryErrorThresholdMm)
    {
        group.previousState = group.state;
        group.state = GROUP_STATE_REPAIR;
        group.dirtyReason |= DIRTY_WATCHDOG;
        SegmentGroupsOut[groupId] = group;
    }
}

[numthreads(64, 1, 1)]
void SegmentGroupTelemetryReduceCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupId = dispatchThreadId.x;
    if (groupId >= gGroupCount)
    {
        return;
    }
    const SegmentGroupGpuData group = SegmentGroupsIn[groupId];
    // RuntimeCounters byte offsets are deliberately stable and documented:
    // 0 total, 4 static cached, 8 static transformed, 12 dynamic,
    // 16 deferred, 20 repair, 24 invalid/finite failure.
    CounterAdd(0u, 1u);
    if (group.state == GROUP_STATE_STATIC_CACHED) CounterAdd(4u, 1u);
    else if (group.state == GROUP_STATE_STATIC_TRANSFORMED) CounterAdd(8u, 1u);
    else if (group.state == GROUP_STATE_DYNAMIC) CounterAdd(12u, 1u);
    else if (group.state == GROUP_STATE_DEFERRED) CounterAdd(16u, 1u);
    else if (group.state == GROUP_STATE_REPAIR) CounterAdd(20u, 1u);

    const float3 start = CurrentPositionsIn[group.startPointIndex].xyz;
    const float3 end = CurrentPositionsIn[group.endPointIndex].xyz;
    if (!Finite3(start) || !Finite3(end)) CounterAdd(24u, 1u);
}
#endif
