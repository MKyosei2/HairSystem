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
    uint PlaneColliderCount;
    uint AabbColliderCount;
    uint UseSdfCollider;
    uint SdfValueCount;

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

    float4 SphereData[4];
    float4 CapsuleData[8];
    float4 PlaneData[4];
    float4 AabbData[8];
    uint4 SdfResolution;
    float4 SdfBoundsMin;
    float4 SdfBoundsMax;

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

float SignedDistanceToPlane(float3 p, float4 plane)
{
    return dot(plane.xyz, p) + plane.w;
}

float DistanceToAabbSurface(float3 p, float3 mn, float3 mx)
{
    float3 clamped = clamp(p, mn, mx);
    float outside = length(p - clamped);
    float3 margin = min(p - mn, mx - p);
    float inside = min(margin.x, min(margin.y, margin.z));
    return (outside > 0.0f) ? outside : -inside;
}

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

        float4 sphere = SphereData[i];
        float3 center = sphere.xyz;
        float radius = sphere.w;
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

        float4 capsuleA = CapsuleData[i * 2u + 0u];
        float4 capsuleB = CapsuleData[i * 2u + 1u];
        float3 a = capsuleA.xyz;
        float ra = capsuleA.w;
        float3 b = capsuleB.xyz;
        float rb = capsuleB.w;
        float3 ab = b - a;
        float denom = max(dot(ab, ab), 1.0e-8f);
        float t = saturate(dot(p - a, ab) / denom);
        float3 center = lerp(a, b, t);
        float radius = lerp(ra, rb, t);
        float surfaceDistance = length(p - center) - radius;
        bestScore = max(bestScore, saturate((0.045f - surfaceDistance) / 0.045f));
    }

    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        if (i >= PlaneColliderCount)
        {
            break;
        }
        float signedDistance = SignedDistanceToPlane(p, PlaneData[i]);
        bestScore = max(bestScore, saturate((0.040f - signedDistance) / 0.040f));
    }

    [unroll]
    for (uint i = 0u; i < 4u; ++i)
    {
        if (i >= AabbColliderCount)
        {
            break;
        }
        float3 mn = AabbData[i * 2u + 0u].xyz;
        float3 mx = AabbData[i * 2u + 1u].xyz;
        float surfaceDistance = DistanceToAabbSurface(p, mn, mx);
        bestScore = max(bestScore, saturate((0.040f - surfaceDistance) / 0.040f));
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
