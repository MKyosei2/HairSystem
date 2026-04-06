cbuffer SimConstants : register(b0)
{
    uint TotalStrandCount;
    uint GuideStrandCount;
    uint FollowerStrandCount;
    uint ParticlesPerStrand;

    uint SolverIterations;
    uint SphereColliderCount;
    uint CapsuleColliderCount;
    uint EnableSelfCollision;

    float DeltaTime;
    float Gravity;
    float Damping;
    float SegmentLength;

    float BendStiffness;
    float FollowStiffness;
    float WindStrength;
    float FloorY;

    float ElapsedTime;
    float SelfCollisionRadius;
    float SelfCollisionStiffness;
    float ScalpMotionBlend;

    float StretchCompliance;
    float BendCompliance;
    float RootStiffness;
    float VelocityShockPropagation;

    float3 WindDirection;
    float GridCellSize;

    uint GridDimX;
    uint GridDimY;
    uint GridDimZ;
    uint GridCellCapacity;

    float3 GridOrigin;
    uint EnableGridSelfCollision;

    float LooseConstraintStiffness;
    float LooseConstraintMaxDistance;
    float SupportCollisionRadius;
    float SupportCollisionStiffness;

    float TangentialFriction;
    float LateralFriction;
    float FreezeBackThreshold;
    float FreezeBlend;

    uint BroadphaseMode;
    uint BroadphaseHashBucketCount;
    uint BroadphasePadding0;
    uint BroadphasePadding1;

    uint SortedPaddedEntryCount;
    uint EnableRuntimeAngularBend;
    uint RuntimePadding0;
    uint RuntimePadding1;

    float AngularBendRadians;
    float AngularBendCompliance;
    float AngularBendStiffness;
    float AngularBendDamping;

    float SphereData[16];
    float CapsuleData[32];

    uint FollowerUpdateStride;
    uint FollowerUpdatePhase;
    uint UseCompactedFollowerDispatch;
    uint CompactedFollowerCount;
};

cbuffer RenderConstants : register(b0)
{
    float4x4 ViewProjection;
    float4 CameraPosition;
    float4 LightDirection;
    float WidthScale;
    uint DebugColorMode;
    uint GuideStrandCountRender;
    uint ParticlesPerStrandRender;
    uint RenderStride;
    uint RenderPhase;
    uint TotalStrandCountRender;
    uint StrandTemplateIndexCount;
    uint _RenderPadding1;
};


StructuredBuffer<uint> FollowerTierAssignments : register(t0);
StructuredBuffer<uint> CompactionCountersIn : register(t1);
RWStructuredBuffer<uint> CompactedFollowerIndices : register(u0);
RWStructuredBuffer<uint> CompactionCounters : register(u1);
RWByteAddressBuffer IndirectArgs : register(u6);

static const uint SIM_TIER_FULL = 0u;
static const uint SIM_TIER_REDUCED = 1u;
static const uint SIM_TIER_FROZEN = 2u;
static const uint SIM_TIER_CACHED = 3u;

[numthreads(64,1,1)]
void CompactFollowerDispatchCS(uint3 tid : SV_DispatchThreadID)
{
    uint followerIndex = tid.x;
    if (followerIndex >= FollowerStrandCount)
    {
        return;
    }

    uint tier = FollowerTierAssignments[followerIndex];
    bool active = false;
    if (tier == SIM_TIER_FULL)
    {
        active = true;
    }
    else if (tier == SIM_TIER_REDUCED)
    {
        uint stride = max(FollowerUpdateStride, 2u);
        active = ((followerIndex + FollowerUpdatePhase) % stride) == 0u;
    }
    else if (tier == SIM_TIER_CACHED)
    {
        uint stride = max(FollowerUpdateStride, 4u);
        active = ((followerIndex + FollowerUpdatePhase) % stride) == 0u;
    }

    if (!active)
    {
        return;
    }

    uint outIndex = 0u;
    InterlockedAdd(CompactionCounters[0], 1u, outIndex);
    CompactedFollowerIndices[outIndex] = followerIndex;
}


[numthreads(1,1,1)]
void BuildFollowDispatchArgsCS(uint3 tid : SV_DispatchThreadID)
{
    uint activeFollowerCount = UseCompactedFollowerDispatch != 0u
        ? CompactionCountersIn[0]
        : ((FollowerStrandCount <= FollowerUpdatePhase)
            ? 0u
            : ((FollowerStrandCount - 1u - FollowerUpdatePhase) / max(FollowerUpdateStride, 1u) + 1u));
    uint groupsX = (activeFollowerCount + 63u) / 64u;
    IndirectArgs.Store(0u, groupsX);
    IndirectArgs.Store(4u, 1u);
    IndirectArgs.Store(8u, 1u);
}

[numthreads(1,1,1)]
void BuildDrawArgsCS(uint3 tid : SV_DispatchThreadID)
{
    uint followerCount = (TotalStrandCountRender > GuideStrandCountRender)
        ? (TotalStrandCountRender - GuideStrandCountRender)
        : 0u;
    uint stride = max(RenderStride, 1u);
    uint renderedFollowerCount = (followerCount <= RenderPhase)
        ? 0u
        : ((followerCount - 1u - RenderPhase) / stride + 1u);
    uint instanceCount = GuideStrandCountRender + renderedFollowerCount;

    IndirectArgs.Store(0u, StrandTemplateIndexCount);
    IndirectArgs.Store(4u, instanceCount);
    IndirectArgs.Store(8u, 0u);
    IndirectArgs.Store(12u, 0u);
    IndirectArgs.Store(16u, 0u);
}
