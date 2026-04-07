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

struct GuideSignal
{
    float4 rootAndVelocity;
    float4 metrics;
};

StructuredBuffer<float4> InputPositions : register(t0);
StructuredBuffer<float4> InputPreviousPositions : register(t1);
StructuredBuffer<float4> RestPositions : register(t2);
RWStructuredBuffer<GuideSignal> GuideSignalsOut : register(u0);

float ComputeGuideContactProxy(float3 p)
{
    float bestScore = saturate((0.030f - (p.y - FloorY)) / 0.030f);
    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        if (i >= SphereColliderCount)
        {
            break;
        }

        float3 center = float3(SphereData[i * 4u + 0u], SphereData[i * 4u + 1u], SphereData[i * 4u + 2u]);
        float radius = SphereData[i * 4u + 3u];
        float surfaceDistance = length(p - center) - radius;
        bestScore = max(bestScore, saturate((0.040f - surfaceDistance) / 0.040f));
    }

    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        if (i >= CapsuleColliderCount)
        {
            break;
        }

        float3 a = float3(CapsuleData[i * 8u + 0u], CapsuleData[i * 8u + 1u], CapsuleData[i * 8u + 2u]);
        float ra = CapsuleData[i * 8u + 3u];
        float3 b = float3(CapsuleData[i * 8u + 4u], CapsuleData[i * 8u + 5u], CapsuleData[i * 8u + 6u]);
        float rb = CapsuleData[i * 8u + 7u];
        float3 ab = b - a;
        float denom = max(dot(ab, ab), 1.0e-8f);
        float t = saturate(dot(p - a, ab) / denom);
        float3 center = lerp(a, b, t);
        float radius = lerp(ra, rb, t);
        float surfaceDistance = length(p - center) - radius;
        bestScore = max(bestScore, saturate((0.045f - surfaceDistance) / 0.045f));
    }

    return bestScore;
}

[numthreads(64, 1, 1)]
void ExtractGuideSignalsCS(uint3 tid : SV_DispatchThreadID)
{
    uint guideIndex = tid.x;
    if (guideIndex >= GuideStrandCount)
    {
        return;
    }

    uint rootParticleIndex = guideIndex * ParticlesPerStrand;
    float3 currentRoot = InputPositions[rootParticleIndex].xyz;
    float3 previousRoot = InputPreviousPositions[rootParticleIndex].xyz;
    float3 restRoot = RestPositions[rootParticleIndex].xyz;

    float safeDt = max(DeltaTime, 1.0e-5f);
    float velocity = length(currentRoot - previousRoot) / safeDt;
    float displacement = length(currentRoot - restRoot);
    float contact = ComputeGuideContactProxy(currentRoot);
    float uncertainty = saturate((velocity / 0.30f) * 0.45f + (displacement / 0.030f) * 0.30f + contact * 0.25f);

    GuideSignal signal;
    signal.rootAndVelocity = float4(currentRoot, velocity);
    signal.metrics = float4(displacement, contact, uncertainty, 0.0f);
    GuideSignalsOut[guideIndex] = signal;
}
