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

    float3 ScalpCenter;
    float ScalpPadding0;
    float3 NeckPivot;
    float ScalpPadding1;

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
};

struct SkinWeightData
{
    uint4 BoneIndices;
    float4 BoneWeights;
};

StructuredBuffer<float4> RestScalpPositions : register(t0);
StructuredBuffer<SkinWeightData> ScalpSkinWeights : register(t1);
RWStructuredBuffer<float4> OutputScalpPositions : register(u0);

float3 RotateX(float3 p, float radians)
{
    const float s = sin(radians);
    const float c = cos(radians);
    return float3(p.x, p.y * c + p.z * s, -p.y * s + p.z * c);
}

float3 RotateY(float3 p, float radians)
{
    const float s = sin(radians);
    const float c = cos(radians);
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

float3 ApplyNeckTransform(float3 p, float neckYaw, float neckPitch, float bob)
{
    float3 local = p - NeckPivot;
    local = RotateX(local, neckPitch);
    local = RotateY(local, neckYaw);
    return local + NeckPivot + float3(0.0f, bob * 0.5f, 0.0f);
}

float3 ApplyHeadTransform(float3 p, float headYaw, float headPitch, float3 headPivotWorld)
{
    float3 local = p - headPivotWorld;
    local = RotateX(local, headPitch);
    local = RotateY(local, headYaw);
    return local + headPivotWorld;
}

[numthreads(64, 1, 1)]
void UpdateScalpCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    const float motionBlend = ScalpMotionBlend;
    const float neckYaw = sin(ElapsedTime * 0.83f) * 0.07f * motionBlend;
    const float neckPitch = sin(ElapsedTime * 1.07f) * 0.035f * motionBlend;
    const float headYaw = sin(ElapsedTime * 0.91f) * 0.14f * motionBlend;
    const float headPitch = sin(ElapsedTime * 1.39f) * 0.06f * motionBlend;
    const float bob = sin(ElapsedTime * 1.15f) * 0.012f * motionBlend;

    uint restCount = 0u;
    RestScalpPositions.GetDimensions(restCount);
    if (index >= restCount)
    {
        return;
    }

    const float3 headPivotWorld = ApplyNeckTransform(ScalpCenter, neckYaw, neckPitch, bob);

    const float3 rest = RestScalpPositions[index].xyz;
    const SkinWeightData skin = ScalpSkinWeights[index];
    float3 blended = float3(0.0f, 0.0f, 0.0f);
    const float3 neckPoint = ApplyNeckTransform(rest, neckYaw, neckPitch, bob);
    const float3 headPoint = ApplyHeadTransform(neckPoint, headYaw, headPitch, headPivotWorld);
    [unroll]
    for (uint weightIndex = 0u; weightIndex < 4u; ++weightIndex)
    {
        const float weight = skin.BoneWeights[weightIndex];
        if (weight <= 0.0f)
        {
            continue;
        }
        const uint boneIndex = skin.BoneIndices[weightIndex];
        blended += ((boneIndex == 0u) ? neckPoint : headPoint) * weight;
    }

    OutputScalpPositions[index] = float4(blended, 1.0f);
}
