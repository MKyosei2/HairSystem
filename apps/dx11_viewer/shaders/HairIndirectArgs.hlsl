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

    uint FollowerUpdateStride;
    uint FollowerUpdatePhase;
    uint SimulationLodLevel;
    uint _SimPadding0;

    float SphereData[16];
    float CapsuleData[32];
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

RWByteAddressBuffer IndirectArgs : register(u6);

[numthreads(1,1,1)]
void BuildFollowDispatchArgsCS(uint3 tid : SV_DispatchThreadID)
{
    uint updateStride = max(FollowerUpdateStride, 1u);
    uint activeFollowerCount = (FollowerStrandCount <= FollowerUpdatePhase)
        ? 0u
        : ((FollowerStrandCount - 1u - FollowerUpdatePhase) / updateStride + 1u);
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
