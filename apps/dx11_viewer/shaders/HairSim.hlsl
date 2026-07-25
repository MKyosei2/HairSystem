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
    uint DispatchBaseIndex;
    uint EnableCatastrophicRecovery;

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

    uint UseCompactedGuideDispatch;
    uint CompactedGuideCount;
    uint FollowerUpdateStride;
    uint FollowerUpdatePhase;
    uint UseCompactedFollowerDispatch;
    uint CompactedFollowerCount;
    uint UseCompactedGuidePointDispatch;
    uint CompactedGuidePointCount;
};

cbuffer BitonicConstants : register(b1)
{
    uint BitonicJ;
    uint BitonicK;
    uint BitonicEntryCount;
    uint BitonicPadding;
};

struct FollowData
{
    int ParentGuide;
    float FollowWeight;
    float ClumpWeight;
    float NoiseWeight;
};

struct TriangleData
{
    uint A;
    uint B;
    uint C;
    uint _Padding;
};

struct RootBindingData
{
    uint TriangleIndex;
    float3 Barycentric;
    float2 RootUV;
    float2 _Padding;
};

StructuredBuffer<float4> InputPositions : register(t0);
StructuredBuffer<float4> InputPreviousPositions : register(t1);
StructuredBuffer<float4> RestPositions : register(t2);
StructuredBuffer<FollowData> FollowStrands : register(t3);
StructuredBuffer<float4> ScalpPositions : register(t4);
StructuredBuffer<TriangleData> ScalpTriangles : register(t5);
StructuredBuffer<RootBindingData> RootBindings : register(t6);
StructuredBuffer<uint> CompactedFollowerIndices : register(t7);
StructuredBuffer<uint> FollowerTierAssignments : register(t8);
StructuredBuffer<uint> CompactedGuideIndices : register(t9);
StructuredBuffer<uint> GuideCompactionCountersInSim : register(t10);
StructuredBuffer<uint> FollowerCompactionCountersInSim : register(t11);
StructuredBuffer<float> SdfValues : register(t12);

RWStructuredBuffer<float4> OutputPositions : register(u0);
RWStructuredBuffer<float4> OutputPreviousPositions : register(u1);
RWStructuredBuffer<float> DistanceLambdas : register(u2);
RWStructuredBuffer<float> BendLambdas : register(u3);
RWStructuredBuffer<uint> GridCellCounts : register(u4);
RWStructuredBuffer<uint> GridCellIndices : register(u5);
RWStructuredBuffer<uint> GridHashHeads : register(u6);
RWStructuredBuffer<uint> GridParticleNext : register(u7);

float3 SafeNormalize(float3 v)
{
    float lenSq = dot(v, v);
    return (lenSq > 1.0e-8f) ? (v * rsqrt(lenSq)) : float3(0.0f, 0.0f, 0.0f);
}

float3 RotateVectorFromTo(float3 value, float3 fromDirection, float3 toDirection)
{
    float3 fromUnit = SafeNormalize(fromDirection);
    float3 toUnit = SafeNormalize(toDirection);
    float cosine = clamp(dot(fromUnit, toUnit), -1.0f, 1.0f);
    float3 axis = cross(fromUnit, toUnit);
    float sine = length(axis);

    if (sine <= 1.0e-5f)
    {
        if (cosine >= 0.0f)
        {
            return value;
        }

        float3 reference = (abs(fromUnit.x) < 0.8f)
            ? float3(1.0f, 0.0f, 0.0f)
            : float3(0.0f, 1.0f, 0.0f);
        float3 halfTurnAxis = SafeNormalize(cross(fromUnit, reference));
        return 2.0f * halfTurnAxis * dot(halfTurnAxis, value) - value;
    }

    axis /= sine;
    return value * cosine +
        cross(axis, value) * sine +
        axis * dot(axis, value) * (1.0f - cosine);
}


bool IsFinite3(float3 value)
{
    // Shader Model 5-safe finite test: NaN is unequal to itself and infinities
    // exceed the conservative simulation-domain limit.
    return all(value == value) && all(abs(value) < float3(1.0e10f, 1.0e10f, 1.0e10f));
}

uint TotalGuideParticles()
{
    return GuideStrandCount * ParticlesPerStrand;
}

uint ResolveGuideStrandIndex(uint dispatchIndex)
{
    if (UseCompactedGuideDispatch != 0u)
    {
        if (dispatchIndex >= CompactedGuideCount)
        {
            return 0xFFFFFFFFu;
        }
        return CompactedGuideIndices[dispatchIndex];
    }
    return dispatchIndex;
}

uint GetGuideParticleDispatchCount()
{
    if (UseCompactedGuidePointDispatch != 0u)
    {
        if (CompactedGuidePointCount != 0u)
        {
            return CompactedGuidePointCount;
        }
        return (ParticlesPerStrand > 1u) ? (GuideCompactionCountersInSim[0] * (ParticlesPerStrand - 1u)) : 0u;
    }
    return TotalGuideParticles();
}

uint ResolveGuideParticleIndex(uint dispatchIndex)
{
    if (UseCompactedGuidePointDispatch != 0u)
    {
        if (ParticlesPerStrand <= 1u)
        {
            return 0xFFFFFFFFu;
        }
        uint localStride = ParticlesPerStrand - 1u;
        uint guideDispatchIndex = dispatchIndex / localStride;
        uint localIndex = (dispatchIndex % localStride) + 1u;
        uint strandIndex = ResolveGuideStrandIndex(guideDispatchIndex);
        if (strandIndex == 0xFFFFFFFFu)
        {
            return 0xFFFFFFFFu;
        }
        return strandIndex * ParticlesPerStrand + localIndex;
    }
    return dispatchIndex;
}

uint GetCompactedFollowerCount()
{
    if (UseCompactedFollowerDispatch == 0u)
    {
        return FollowerStrandCount;
    }
    return (CompactedFollowerCount != 0u) ? CompactedFollowerCount : FollowerCompactionCountersInSim[0];
}

uint TotalGridCells()
{
    return max(GridDimX * GridDimY * GridDimZ, 1u);
}

int3 ComputeSignedGridCoord(float3 p)
{
    float3 local = (p - GridOrigin) / max(GridCellSize, 1.0e-5f);
    return int3((int)floor(local.x), (int)floor(local.y), (int)floor(local.z));
}

uint3 ClampGridCoord(int3 coord)
{
    uint3 result;
    result.x = min((uint)max(coord.x, 0), GridDimX - 1u);
    result.y = min((uint)max(coord.y, 0), GridDimY - 1u);
    result.z = min((uint)max(coord.z, 0), GridDimZ - 1u);
    return result;
}

uint3 ComputeGridCoord(float3 p)
{
    return ClampGridCoord(ComputeSignedGridCoord(p));
}

float ComputeCellSizeForLevel(uint level)
{
    return GridCellSize * (float)(1u << min(level, 8u));
}

int3 ComputeSignedGridCoordWithCellSize(float3 p, float cellSize)
{
    float3 local = (p - GridOrigin) / max(cellSize, 1.0e-5f);
    return int3((int)floor(local.x), (int)floor(local.y), (int)floor(local.z));
}

uint ComputeAdaptiveBroadphaseLevel(uint localIndex)
{
    if (ParticlesPerStrand <= 1u)
    {
        return 0u;
    }

    float t = float(localIndex) / float(ParticlesPerStrand - 1u);
    if (t > 0.72f)
    {
        return 2u;
    }
    if (t > 0.35f)
    {
        return 1u;
    }
    return 0u;
}

uint ComputeGridCellIndex(uint3 coord)
{
    return coord.x + coord.y * GridDimX + coord.z * GridDimX * GridDimY;
}

uint ComputeHashBucketIndexWithLevel(int3 coord, uint level)
{
    uint bucketCount = max(BroadphaseHashBucketCount, 1u);
    uint hx = asuint(coord.x * 73856093);
    uint hy = asuint(coord.y * 19349663);
    uint hz = asuint(coord.z * 83492791);
    uint hl = (level + 1u) * 2654435761u;
    return (hx ^ hy ^ hz ^ hl) % bucketCount;
}

uint ComputeHashBucketIndex(int3 coord)
{
    return ComputeHashBucketIndexWithLevel(coord, 0u);
}

bool ShouldSwapSortedPair(uint keyA, uint indexA, uint keyB, uint indexB, bool ascending)
{
    bool greater = (keyA > keyB) || (keyA == keyB && indexA > indexB);
    bool less = (keyA < keyB) || (keyA == keyB && indexA < indexB);
    return ascending ? greater : less;
}

float ComputeAngularTargetOffset(float prevLength, float nextLength)
{
    float avgSegment = 0.5f * (prevLength + nextLength);
    return avgSegment * sin(max(AngularBendRadians, 0.0f) * 0.5f);
}
float3 EvaluateBoundRoot(uint strandIndex)
{
    RootBindingData binding = RootBindings[strandIndex];
    TriangleData tri = ScalpTriangles[binding.TriangleIndex];
    float3 a = ScalpPositions[tri.A].xyz;
    float3 b = ScalpPositions[tri.B].xyz;
    float3 c = ScalpPositions[tri.C].xyz;
    return a * binding.Barycentric.x + b * binding.Barycentric.y + c * binding.Barycentric.z;
}

float3 EvaluateBoundRootNormal(uint strandIndex)
{
    RootBindingData binding = RootBindings[strandIndex];
    TriangleData tri = ScalpTriangles[binding.TriangleIndex];
    float3 a = ScalpPositions[tri.A].xyz;
    float3 b = ScalpPositions[tri.B].xyz;
    float3 c = ScalpPositions[tri.C].xyz;
    float3 triangleNormal = SafeNormalize(cross(b - a, c - a));
    if (dot(triangleNormal, triangleNormal) <= 1.0e-8f)
    {
        triangleNormal = float3(0.0f, 1.0f, 0.0f);
    }
    return triangleNormal;
}

float3 ResolveCapsule(float3 p, float3 a, float ra, float3 b, float rb)
{
    float3 ab = b - a;
    float abLenSq = max(dot(ab, ab), 1.0e-8f);
    float t = saturate(dot(p - a, ab) / abLenSq);
    float3 center = lerp(a, b, t);
    float radius = lerp(ra, rb, t);
    float3 delta = p - center;
    float distSq = max(dot(delta, delta), 1.0e-8f);
    float distValue = sqrt(distSq);
    if (distValue < radius)
    {
        return center + delta / distValue * radius;
    }
    return p;
}

float3 ResolvePlane(float3 p, float4 plane)
{
    float signedDistance = dot(plane.xyz, p) + plane.w;
    if (signedDistance < 0.0f)
    {
        return p - plane.xyz * signedDistance;
    }
    return p;
}

float3 ResolveAabb(float3 p, float3 mn, float3 mx)
{
    if (all(p >= mn) && all(p <= mx))
    {
        float left = p.x - mn.x;
        float right = mx.x - p.x;
        float down = p.y - mn.y;
        float up = mx.y - p.y;
        float back = p.z - mn.z;
        float front = mx.z - p.z;

        float best = left;
        float3 normal = float3(-1.0f, 0.0f, 0.0f);

        if (right < best) { best = right; normal = float3(1.0f, 0.0f, 0.0f); }
        if (down < best) { best = down; normal = float3(0.0f, -1.0f, 0.0f); }
        if (up < best) { best = up; normal = float3(0.0f, 1.0f, 0.0f); }
        if (back < best) { best = back; normal = float3(0.0f, 0.0f, -1.0f); }
        if (front < best) { best = front; normal = float3(0.0f, 0.0f, 1.0f); }

        return p + normal * max(best, 1.0e-4f);
    }
    return p;
}

float3 ClosestPointOnSegment(float3 p, float3 a, float3 b)
{
    float3 ab = b - a;
    float denom = max(dot(ab, ab), 1.0e-8f);
    float t = saturate(dot(p - a, ab) / denom);
    return lerp(a, b, t);
}

float3 ComputeParticleTangent(uint strandIndex, uint localIndex)
{
    uint start = strandIndex * ParticlesPerStrand;
    if (ParticlesPerStrand <= 1)
    {
        return float3(0.0f, -1.0f, 0.0f);
    }

    if (localIndex == 0)
    {
        return SafeNormalize(OutputPositions[start + 1].xyz - OutputPositions[start].xyz);
    }

    if (localIndex + 1 >= ParticlesPerStrand)
    {
        return SafeNormalize(OutputPositions[start + localIndex].xyz - OutputPositions[start + localIndex - 1].xyz);
    }

    float3 prevP = OutputPositions[start + localIndex - 1].xyz;
    float3 nextP = OutputPositions[start + localIndex + 1].xyz;
    return SafeNormalize(nextP - prevP);
}

float ComputeBackFreezeFactor(uint strandIndex, uint localIndex)
{
    float3 rootRest = RestPositions[strandIndex * ParticlesPerStrand].xyz;
    float rootMask = saturate((-rootRest.z + FreezeBackThreshold) / 0.18f);
    float localT = (ParticlesPerStrand > 1) ? (float(localIndex) / float(ParticlesPerStrand - 1)) : 0.0f;
    float tailMask = saturate((localT - 0.25f) / 0.75f);
    return rootMask * tailMask;
}

float3 ApplySupportCollisionApprox(uint strandIndex, uint localIndex, float3 p)
{
    if (SupportCollisionRadius <= 0.0f || SupportCollisionStiffness <= 0.0f)
    {
        return p;
    }

    float3 correction = float3(0.0f, 0.0f, 0.0f);
    uint samples = 0u;
    uint lastLocal = max(ParticlesPerStrand - 1u, 1u);

    [unroll]
    for (int guideOffset = -2; guideOffset <= 2; ++guideOffset)
    {
        int otherGuide = int(strandIndex) + guideOffset;
        if (otherGuide < 0 || otherGuide >= int(GuideStrandCount) || otherGuide == int(strandIndex))
        {
            continue;
        }

        uint otherStart = uint(otherGuide) * ParticlesPerStrand;
        uint segmentLocal = min(localIndex, lastLocal - 1u);
        uint aIndex = otherStart + segmentLocal;
        uint bIndex = otherStart + min(segmentLocal + 1u, lastLocal);
        float3 a = OutputPositions[aIndex].xyz;
        float3 b = OutputPositions[bIndex].xyz;
        float3 closest = ClosestPointOnSegment(p, a, b);
        float3 delta = p - closest;
        float distSq = dot(delta, delta);
        float radius = SupportCollisionRadius;
        if (distSq > 1.0e-8f && distSq < radius * radius)
        {
            float distValue = sqrt(distSq);
            float penetration = radius - distValue;
            correction += (delta / distValue) * penetration;
            samples += 1u;
        }
    }

    if (samples > 0u)
    {
        p += (correction / float(samples)) * SupportCollisionStiffness;
    }
    return p;
}

float3 ApplyLooseGuideConstraints(uint strandIndex, uint localIndex, float3 p)
{
    if (LooseConstraintStiffness <= 0.0f || LooseConstraintMaxDistance <= 0.0f)
    {
        return p;
    }

    uint currentIndex = strandIndex * ParticlesPerStrand + localIndex;
    float3 restP = RestPositions[currentIndex].xyz;
    float3 correction = float3(0.0f, 0.0f, 0.0f);
    uint samples = 0u;
    float capture = max(LooseConstraintMaxDistance, SelfCollisionRadius * 1.5f);

    [unroll]
    for (int guideOffset = -2; guideOffset <= 2; ++guideOffset)
    {
        int otherGuide = int(strandIndex) + guideOffset;
        if (otherGuide < 0 || otherGuide >= int(GuideStrandCount) || otherGuide == int(strandIndex))
        {
            continue;
        }

        uint otherIndex = uint(otherGuide) * ParticlesPerStrand + localIndex;
        float3 q = OutputPositions[otherIndex].xyz;
        float3 restQ = RestPositions[otherIndex].xyz;

        float3 restDelta = restQ - restP;
        float restDist = length(restDelta);
        if (restDist > capture)
        {
            continue;
        }

        float3 delta = q - p;
        float distValue = length(delta);
        if (distValue <= 1.0e-6f)
        {
            continue;
        }

        float targetDist = min(max(restDist, SelfCollisionRadius * 0.95f), capture);
        float error = distValue - targetDist;
        if (abs(error) < 1.0e-4f)
        {
            continue;
        }

        float attenuation = saturate(1.0f - abs(error) / capture);
        correction += (delta / distValue) * error * attenuation;
        samples += 1u;
    }

    if (samples > 0u)
    {
        p += (correction / float(samples)) * LooseConstraintStiffness;
    }
    return p;
}

[numthreads(64, 1, 1)]
void RootBindCS(uint3 id : SV_DispatchThreadID)
{
    uint strandIndex = DispatchBaseIndex + id.x;
    if (strandIndex >= TotalStrandCount)
    {
        return;
    }

    // The authored root contains the scalp-normal lift used when the wig was
    // generated. Binding a static run to the raw barycentric scalp surface every
    // frame removed that lift and injected stress into the first segment.
    uint rootIndex = strandIndex * ParticlesPerStrand;
    float3 restRoot = RestPositions[rootIndex].xyz;
    float3 scalpRoot = EvaluateBoundRoot(strandIndex);
    float3 scalpNormal = EvaluateBoundRootNormal(strandIndex);

    // Preserve an authored scalp-normal clearance even when the animated scalp
    // becomes the binding source. The previous lerp ended exactly on the scalp
    // surface and the depth buffer hid the first hair segments.
    float authoredLift = clamp(
        dot(restRoot - scalpRoot, scalpNormal),
        0.0020f,
        0.0040f);
    float3 liftedScalpRoot =
        scalpRoot + scalpNormal * authoredLift;
    float3 boundRoot = lerp(
        restRoot,
        liftedScalpRoot,
        saturate(ScalpMotionBlend));

    if (!IsFinite3(boundRoot))
    {
        boundRoot = restRoot;
    }
    OutputPositions[rootIndex] = float4(boundRoot, 1.0f);
    OutputPreviousPositions[rootIndex] = float4(boundRoot, 1.0f);
}

[numthreads(64, 1, 1)]
void IntegrateCS(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = DispatchBaseIndex + id.x;
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
        // RootBindCS is the single owner of the bound root for this step.
        // Leaving this lane untouched prevents IntegrateCS from writing the
        // same root a second time and preserves the value already placed in
        // the output buffer by RootBindCS.
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

    // The distance solver is the single owner of root-adjacent segment length.
    // Do not add a second tether correction for localIndex == 1 here.

    float freezeFactor = ComputeBackFreezeFactor(strandIndex, localIndex);
    if (freezeFactor > 0.0f)
    {
        next = lerp(next, current, freezeFactor * FreezeBlend);
    }

    OutputPreviousPositions[particleIndex] = float4(current, 1.0f);
    OutputPositions[particleIndex] = float4(next, 1.0f);
}

[numthreads(64, 1, 1)]
void ClearConstraintLambdasCS(uint3 id : SV_DispatchThreadID)
{
    uint constraintIndex = DispatchBaseIndex + id.x;
    uint distanceCount = GuideStrandCount *
        ((ParticlesPerStrand > 1u) ? (ParticlesPerStrand - 1u) : 0u);
    uint bendCount = GuideStrandCount *
        ((ParticlesPerStrand > 2u) ? (ParticlesPerStrand - 2u) : 0u);

    if (constraintIndex < distanceCount)
    {
        DistanceLambdas[constraintIndex] = 0.0f;
    }
    if (constraintIndex < bendCount)
    {
        BendLambdas[constraintIndex] = 0.0f;
    }
}

[numthreads(64, 1, 1)]
void DistanceCS(uint3 id : SV_DispatchThreadID)
{
    uint dispatchIndex = DispatchBaseIndex + id.x;
    uint strandIndex = ResolveGuideStrandIndex(dispatchIndex);
    if (strandIndex == 0xFFFFFFFFu || strandIndex >= GuideStrandCount)
    {
        return;
    }

    uint start = strandIndex * ParticlesPerStrand;
    float alpha = StretchCompliance / max(DeltaTime * DeltaTime, 1.0e-8f);

    [loop]
    for (uint i = 1u; i < ParticlesPerStrand; ++i)
    {
        uint prevIndex = start + i - 1u;
        uint currentIndex = start + i;
        uint lambdaIndex = strandIndex * (ParticlesPerStrand - 1u) + (i - 1u);

        float3 p0 = OutputPositions[prevIndex].xyz;
        float3 p1 = OutputPositions[currentIndex].xyz;
        float3 restP0 = RestPositions[prevIndex].xyz;
        float3 restP1 = RestPositions[currentIndex].xyz;
        float targetLength = max(length(restP1 - restP0), 1.0e-5f);
        float3 delta = p1 - p0;
        float currentLength = length(delta);
        float3 direction = (currentLength > 1.0e-6f)
            ? delta / currentLength
            : SafeNormalize(restP1 - restP0);
        currentLength = max(currentLength, 1.0e-6f);

        float lambda = DistanceLambdas[lambdaIndex];
        float constraint = currentLength - targetLength;
        float w0 = (i == 1u) ? 0.0f : 1.0f;
        float w1 = 1.0f;
        float dLambda = (-constraint - alpha * lambda) /
            max(w0 + w1 + alpha, 1.0e-6f);
        lambda += dLambda;
        DistanceLambdas[lambdaIndex] = lambda;

        if (w0 > 0.0f)
        {
            float3 correction0 = -direction * (w0 * dLambda);
            p0 += correction0;
            OutputPositions[prevIndex] = float4(p0, 1.0f);
            OutputPreviousPositions[prevIndex] = float4(
                OutputPreviousPositions[prevIndex].xyz + correction0,
                1.0f);
        }

        float3 correction1 = direction * (w1 * dLambda);
        p1 += correction1;
        OutputPositions[currentIndex] = float4(p1, 1.0f);
        OutputPreviousPositions[currentIndex] = float4(
            OutputPreviousPositions[currentIndex].xyz + correction1,
            1.0f);
    }
}

[numthreads(64, 1, 1)]
void StabilityGuardCS(uint3 id : SV_DispatchThreadID)
{
    uint dispatchIndex = DispatchBaseIndex + id.x;
    uint strandIndex = ResolveGuideStrandIndex(dispatchIndex);
    if (strandIndex == 0xFFFFFFFFu || strandIndex >= GuideStrandCount)
    {
        return;
    }

    uint start = strandIndex * ParticlesPerStrand;
    float3 root = OutputPositions[start].xyz;
    float3 restRoot = RestPositions[start].xyz;
    float3 rootTranslation = root - restRoot;
    bool catastrophic = !IsFinite3(root) || !IsFinite3(restRoot);
    float authoredLength = 0.0f;
    float maximumTranslatedRestDeviation = 0.0f;

    [loop]
    for (uint scanIndex = 1u; scanIndex < ParticlesPerStrand; ++scanIndex)
    {
        uint previousIndex = start + scanIndex - 1u;
        uint currentIndex = start + scanIndex;
        float3 restParent = RestPositions[previousIndex].xyz;
        float3 restChild = RestPositions[currentIndex].xyz;
        float targetLength = max(length(restChild - restParent), 1.0e-5f);
        authoredLength += targetLength;

        float3 parent = OutputPositions[previousIndex].xyz;
        float3 child = OutputPositions[currentIndex].xyz;
        float currentLength = length(child - parent);
        float translatedRestDeviation = length(child - (restChild + rootTranslation));
        maximumTranslatedRestDeviation = max(
            maximumTranslatedRestDeviation,
            translatedRestDeviation);

        catastrophic = catastrophic ||
            !IsFinite3(parent) ||
            !IsFinite3(child) ||
            !IsFinite3(OutputPreviousPositions[currentIndex].xyz) ||
            currentLength > targetLength * 1.12f;
    }

    // This envelope is intentionally much wider than normal secondary motion.
    // It catches only a guide that has rotated/folded into the slab-like failure.
    float recoveryEnvelope = max(0.30f, authoredLength * 1.05f);
    catastrophic = catastrophic ||
        maximumTranslatedRestDeviation > recoveryEnvelope;

    if (catastrophic && EnableCatastrophicRecovery != 0u)
    {
        [loop]
        for (uint recoveryIndex = 1u; recoveryIndex < ParticlesPerStrand; ++recoveryIndex)
        {
            uint pointIndex = start + recoveryIndex;
            float3 recovered = RestPositions[pointIndex].xyz + rootTranslation;
            OutputPositions[pointIndex] = float4(recovered, 1.0f);
            OutputPreviousPositions[pointIndex] = float4(recovered, 1.0f);
        }
        return;
    }

    // Collision is the final operation in each solver iteration and may restore
    // a small amount of extension. Clamp it once, root-to-tip, without changing
    // the authored rest length or injecting correction velocity.
    static const float kMaximumStretchRatio = 1.005f;
    [loop]
    for (uint clampIndex = 1u; clampIndex < ParticlesPerStrand; ++clampIndex)
    {
        uint previousIndex = start + clampIndex - 1u;
        uint currentIndex = start + clampIndex;
        float3 parent = OutputPositions[previousIndex].xyz;
        float3 child = OutputPositions[currentIndex].xyz;
        float3 restParent = RestPositions[previousIndex].xyz;
        float3 restChild = RestPositions[currentIndex].xyz;
        float targetLength = max(length(restChild - restParent), 1.0e-5f);
        float3 segment = child - parent;
        float currentLength = length(segment);

        if (!IsFinite3(child) || currentLength <= targetLength * kMaximumStretchRatio)
        {
            continue;
        }

        float3 direction = (currentLength > 1.0e-6f)
            ? segment / currentLength
            : SafeNormalize(restChild - restParent);
        float3 corrected = parent + direction * targetLength;
        float3 correction = corrected - child;
        OutputPositions[currentIndex] = float4(corrected, 1.0f);
        OutputPreviousPositions[currentIndex] = float4(
            OutputPreviousPositions[currentIndex].xyz + correction,
            1.0f);
    }
}

[numthreads(64, 1, 1)]
void BendCS(uint3 id : SV_DispatchThreadID)
{
    uint dispatchIndex = DispatchBaseIndex + id.x;
    uint strandIndex = ResolveGuideStrandIndex(dispatchIndex);
    if (strandIndex == 0xFFFFFFFFu ||
        strandIndex >= GuideStrandCount ||
        ParticlesPerStrand < 3u)
    {
        return;
    }

    uint start = strandIndex * ParticlesPerStrand;
    float compliance = (EnableRuntimeAngularBend != 0u)
        ? AngularBendCompliance
        : BendCompliance;
    float stiffness = (EnableRuntimeAngularBend != 0u)
        ? saturate(AngularBendStiffness)
        : saturate(BendStiffness);
    float damping = (EnableRuntimeAngularBend != 0u)
        ? saturate(AngularBendDamping)
        : 0.0f;
    float alpha = compliance / max(DeltaTime * DeltaTime, 1.0e-8f);

    // Rotationally invariant rest-chord constraint. The previous implementation
    // constrained only the magnitude of b-midpoint(a,c), not its direction; over
    // many frames that allowed a whole guide to rotate/fold while keeping length.
    [loop]
    for (uint i = 1u; i + 1u < ParticlesPerStrand; ++i)
    {
        uint aIndex = start + i - 1u;
        uint cIndex = start + i + 1u;
        uint lambdaIndex = strandIndex * (ParticlesPerStrand - 2u) + (i - 1u);

        float3 a = OutputPositions[aIndex].xyz;
        float3 c = OutputPositions[cIndex].xyz;
        float3 restA = RestPositions[aIndex].xyz;
        float3 restC = RestPositions[cIndex].xyz;
        float targetChordLength = max(length(restC - restA), 1.0e-5f);
        float3 chord = c - a;
        float currentChordLength = length(chord);
        float3 direction = (currentChordLength > 1.0e-6f)
            ? chord / currentChordLength
            : SafeNormalize(restC - restA);
        currentChordLength = max(currentChordLength, 1.0e-6f);

        float lambda = BendLambdas[lambdaIndex];
        float constraint = currentChordLength - targetChordLength;
        float wA = (i == 1u) ? 0.0f : 1.0f;
        float wC = 1.0f;
        float dLambda = (-constraint - alpha * lambda) /
            max(wA + wC + alpha, 1.0e-6f);
        lambda += dLambda;
        BendLambdas[lambdaIndex] = lambda;

        float correctionGain = stiffness * (1.0f - 0.50f * damping);
        if (wA > 0.0f)
        {
            float3 correctionA = -direction * (wA * dLambda * correctionGain);
            a += correctionA;
            OutputPositions[aIndex] = float4(a, 1.0f);
            OutputPreviousPositions[aIndex] = float4(
                OutputPreviousPositions[aIndex].xyz + correctionA,
                1.0f);
        }

        float3 correctionC = direction * (wC * dLambda * correctionGain);
        c += correctionC;
        OutputPositions[cIndex] = float4(c, 1.0f);
        OutputPreviousPositions[cIndex] = float4(
            OutputPreviousPositions[cIndex].xyz + correctionC,
            1.0f);
    }
}

uint ComputeSdfIndex(uint x, uint y, uint z)
{
    return (z * SdfResolution.y + y) * SdfResolution.x + x;
}

float SampleSdfNearest(int x, int y, int z)
{
    x = clamp(x, 0, int(SdfResolution.x) - 1);
    y = clamp(y, 0, int(SdfResolution.y) - 1);
    z = clamp(z, 0, int(SdfResolution.z) - 1);
    return SdfValues[ComputeSdfIndex((uint)x, (uint)y, (uint)z)];
}

float SampleSdfTrilinear(float3 p)
{
    if (UseSdfCollider == 0u || SdfValueCount == 0u || SdfResolution.x < 2u || SdfResolution.y < 2u || SdfResolution.z < 2u)
    {
        return 1.0e9f;
    }
    float3 cellSize = (SdfBoundsMax.xyz - SdfBoundsMin.xyz) / max(float3(SdfResolution.xyz - 1u), float3(1.0f, 1.0f, 1.0f));
    float gx = clamp((p.x - SdfBoundsMin.x) / max(cellSize.x, 1.0e-6f), 0.0f, float(SdfResolution.x - 1u));
    float gy = clamp((p.y - SdfBoundsMin.y) / max(cellSize.y, 1.0e-6f), 0.0f, float(SdfResolution.y - 1u));
    float gz = clamp((p.z - SdfBoundsMin.z) / max(cellSize.z, 1.0e-6f), 0.0f, float(SdfResolution.z - 1u));
    int x0 = int(floor(gx)); int y0 = int(floor(gy)); int z0 = int(floor(gz));
    int x1 = min(x0 + 1, int(SdfResolution.x) - 1);
    int y1 = min(y0 + 1, int(SdfResolution.y) - 1);
    int z1 = min(z0 + 1, int(SdfResolution.z) - 1);
    float tx = gx - float(x0); float ty = gy - float(y0); float tz = gz - float(z0);
    float c000 = SampleSdfNearest(x0, y0, z0);
    float c100 = SampleSdfNearest(x1, y0, z0);
    float c010 = SampleSdfNearest(x0, y1, z0);
    float c110 = SampleSdfNearest(x1, y1, z0);
    float c001 = SampleSdfNearest(x0, y0, z1);
    float c101 = SampleSdfNearest(x1, y0, z1);
    float c011 = SampleSdfNearest(x0, y1, z1);
    float c111 = SampleSdfNearest(x1, y1, z1);
    float c00 = lerp(c000, c100, tx);
    float c10 = lerp(c010, c110, tx);
    float c01 = lerp(c001, c101, tx);
    float c11 = lerp(c011, c111, tx);
    float c0 = lerp(c00, c10, ty);
    float c1 = lerp(c01, c11, ty);
    return lerp(c0, c1, tz);
}

float3 EstimateSdfGradient(float3 p)
{
    float3 cellSize = (SdfBoundsMax.xyz - SdfBoundsMin.xyz) / max(float3(SdfResolution.xyz - 1u), float3(1.0f, 1.0f, 1.0f));
    float3 eps = max(cellSize, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
    float dx = SampleSdfTrilinear(p + float3(eps.x, 0.0f, 0.0f)) - SampleSdfTrilinear(p - float3(eps.x, 0.0f, 0.0f));
    float dy = SampleSdfTrilinear(p + float3(0.0f, eps.y, 0.0f)) - SampleSdfTrilinear(p - float3(0.0f, eps.y, 0.0f));
    float dz = SampleSdfTrilinear(p + float3(0.0f, 0.0f, eps.z)) - SampleSdfTrilinear(p - float3(0.0f, 0.0f, eps.z));
    float3 g = SafeNormalize(float3(dx, dy, dz));
    return (dot(g, g) > 1.0e-8f) ? g : float3(0.0f, 1.0f, 0.0f);
}

float3 ResolveSdfCollider(float3 p, inout bool touchedSurface)
{
    float sdf = SampleSdfTrilinear(p);
    float shell = max(SupportCollisionRadius * 0.6f, 0.0025f);
    if (sdf < shell)
    {
        float3 n = EstimateSdfGradient(p);
        p += n * (shell - sdf);
        touchedSurface = true;
    }
    return p;
}


float3 StableCollisionFallback(uint strandIndex, uint localPointIndex, float3 axis)
{
    const float seed = frac(sin((float(strandIndex) + 1.0f) * 12.9898f + (float(localPointIndex) + 1.0f) * 78.233f) * 43758.5453f);
    float3 reference = (abs(axis.y) < 0.85f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 radial = SafeNormalize(cross(axis, reference));
    float3 binormal = SafeNormalize(cross(axis, radial));
    float angle = seed * 6.28318530718f;
    float3 result = radial * cos(angle) + binormal * sin(angle);
    return (dot(result, result) > 1.0e-8f) ? result : float3(0.0f, 1.0f, 0.0f);
}

float3 ResolveFollowerExternalCollisions(
    uint strandIndex,
    uint localPointIndex,
    float3 p,
    out float3 collisionNormal,
    out bool touchedSurface)
{
    collisionNormal = float3(0.0f, 0.0f, 0.0f);
    touchedSurface = false;
    const float shell = max(SelfCollisionRadius * 0.38f, 0.0018f);

    [loop]
    for (uint sphereIndex = 0u; sphereIndex < SphereColliderCount; ++sphereIndex)
    {
        const float4 sphere = SphereData[sphereIndex];
        const float radius = sphere.w + shell;
        float3 delta = p - sphere.xyz;
        float distSq = dot(delta, delta);
        if (distSq < radius * radius)
        {
            float3 normal = (distSq > 1.0e-10f)
                ? delta * rsqrt(distSq)
                : StableCollisionFallback(strandIndex, localPointIndex, float3(0.0f, 1.0f, 0.0f));
            p = sphere.xyz + normal * radius;
            collisionNormal += normal;
            touchedSurface = true;
        }
    }

    [loop]
    for (uint capsuleIndex = 0u; capsuleIndex < CapsuleColliderCount; ++capsuleIndex)
    {
        const float4 capsuleA = CapsuleData[capsuleIndex * 2u + 0u];
        const float4 capsuleB = CapsuleData[capsuleIndex * 2u + 1u];
        const float3 axis = capsuleB.xyz - capsuleA.xyz;
        const float axisLengthSq = max(dot(axis, axis), 1.0e-8f);
        const float axisT = saturate(dot(p - capsuleA.xyz, axis) / axisLengthSq);
        const float3 center = lerp(capsuleA.xyz, capsuleB.xyz, axisT);
        const float radius = lerp(capsuleA.w, capsuleB.w, axisT) + shell;
        float3 delta = p - center;
        float distSq = dot(delta, delta);
        if (distSq < radius * radius)
        {
            const float3 axisDirection = axis * rsqrt(axisLengthSq);
            float3 normal = (distSq > 1.0e-10f)
                ? delta * rsqrt(distSq)
                : StableCollisionFallback(strandIndex, localPointIndex, axisDirection);
            p = center + normal * radius;
            collisionNormal += normal;
            touchedSurface = true;
        }
    }

    [loop]
    for (uint planeIndex = 0u; planeIndex < PlaneColliderCount; ++planeIndex)
    {
        float3 normal = SafeNormalize(PlaneData[planeIndex].xyz);
        if (dot(normal, normal) <= 1.0e-8f)
        {
            continue;
        }
        const float signedDistance = dot(normal, p) + PlaneData[planeIndex].w;
        if (signedDistance < shell)
        {
            p += normal * (shell - signedDistance);
            collisionNormal += normal;
            touchedSurface = true;
        }
    }

    [loop]
    for (uint aabbIndex = 0u; aabbIndex < AabbColliderCount; ++aabbIndex)
    {
        const float3 before = p;
        p = ResolveAabb(p, AabbData[aabbIndex * 2u + 0u].xyz, AabbData[aabbIndex * 2u + 1u].xyz);
        const float3 correction = p - before;
        if (dot(correction, correction) > 1.0e-10f)
        {
            collisionNormal += SafeNormalize(correction);
            touchedSurface = true;
        }
    }

    if (p.y < FloorY + shell)
    {
        p.y = FloorY + shell;
        collisionNormal += float3(0.0f, 1.0f, 0.0f);
        touchedSurface = true;
    }

    if (UseSdfCollider != 0u && SdfValueCount > 0u)
    {
        const float sdf = SampleSdfTrilinear(p);
        const float sdfShell = max(shell, 0.0032f);
        if (sdf < sdfShell)
        {
            const float3 normal = EstimateSdfGradient(p);
            p += normal * (sdfShell - sdf);
            collisionNormal += normal;
            touchedSurface = true;
        }
    }

    // Near the root, preserve a small outward shell from the bound scalp
    // triangle. This closes the gap where follower reconstruction previously
    // bypassed CollisionCS and could cut through the forehead/crown.
    const float strandT = (ParticlesPerStrand > 1u)
        ? float(localPointIndex) / float(ParticlesPerStrand - 1u)
        : 0.0f;
    if (strandT < 0.12f)
    {
        const float3 scalpPoint = EvaluateBoundRoot(strandIndex);
        const float3 scalpNormal = EvaluateBoundRootNormal(strandIndex);
        const float rootDistance = length(p - scalpPoint);
        if (rootDistance < 0.30f)
        {
            const float requiredClearance = lerp(0.0030f, 0.0012f, smoothstep(0.01f, 0.12f, strandT));
            const float signedClearance = dot(p - scalpPoint, scalpNormal);
            if (signedClearance < requiredClearance)
            {
                p += scalpNormal * (requiredClearance - signedClearance);
                collisionNormal += scalpNormal;
                touchedSurface = true;
            }
        }
    }

    collisionNormal = SafeNormalize(collisionNormal);
    return p;
}

[numthreads(64, 1, 1)]
void CollisionCS(uint3 id : SV_DispatchThreadID)
{
    uint dispatchIndex = DispatchBaseIndex + id.x;
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
    if (localIndex == 0)
    {
        return;
    }

    float3 p = OutputPositions[particleIndex].xyz;
    float3 previousPosition = OutputPreviousPositions[particleIndex].xyz;
    bool touchedSurface = false;

    [loop]
    for (uint sphereIndex = 0; sphereIndex < SphereColliderCount; ++sphereIndex)
    {
        float4 sphere = SphereData[sphereIndex];
        float3 center = sphere.xyz;
        float radius = sphere.w;
        float3 toPoint = p - center;
        float distSq = max(dot(toPoint, toPoint), 1.0e-8f);
        float distValue = sqrt(distSq);
        if (distValue < radius)
        {
            p = center + (toPoint / distValue) * radius;
            touchedSurface = true;
        }
    }

    [loop]
    for (uint capsuleIndex = 0; capsuleIndex < CapsuleColliderCount; ++capsuleIndex)
    {
        float4 capsuleA = CapsuleData[capsuleIndex * 2u + 0u];
        float4 capsuleB = CapsuleData[capsuleIndex * 2u + 1u];
        float3 a = capsuleA.xyz;
        float ra = capsuleA.w;
        float3 b = capsuleB.xyz;
        float rb = capsuleB.w;
        float3 resolved = ResolveCapsule(p, a, ra, b, rb);
        touchedSurface = touchedSurface || (length(resolved - p) > 1.0e-6f);
        p = resolved;
    }

    [loop]
    for (uint planeIndex = 0; planeIndex < PlaneColliderCount; ++planeIndex)
    {
        float3 resolved = ResolvePlane(p, PlaneData[planeIndex]);
        touchedSurface = touchedSurface || (length(resolved - p) > 1.0e-6f);
        p = resolved;
    }

    [loop]
    for (uint aabbIndex = 0; aabbIndex < AabbColliderCount; ++aabbIndex)
    {
        float3 mn = AabbData[aabbIndex * 2u + 0u].xyz;
        float3 mx = AabbData[aabbIndex * 2u + 1u].xyz;
        float3 resolved = ResolveAabb(p, mn, mx);
        touchedSurface = touchedSurface || (length(resolved - p) > 1.0e-6f);
        p = resolved;
    }

    if (p.y < FloorY)
    {
        p.y = FloorY;
        touchedSurface = true;
    }

    if (UseSdfCollider != 0u && SdfValueCount > 0u)
    {
        p = ResolveSdfCollider(p, touchedSurface);
    }

    p = ApplySupportCollisionApprox(strandIndex, localIndex, p);

    if (touchedSurface)
    {
        float3 tangent = ComputeParticleTangent(strandIndex, localIndex);
        float3 velocity = p - previousPosition;
        float tangentialSpeed = dot(velocity, tangent);
        float3 tangentialVelocity = tangent * tangentialSpeed;
        float3 lateralVelocity = velocity - tangentialVelocity;
        tangentialVelocity *= max(1.0f - TangentialFriction, 0.0f);
        lateralVelocity *= max(1.0f - LateralFriction, 0.0f);
        float freezeFactor = ComputeBackFreezeFactor(strandIndex, localIndex);
        float3 adjustedVelocity = tangentialVelocity + lateralVelocity;
        adjustedVelocity = lerp(adjustedVelocity, tangentialVelocity * 0.25f, freezeFactor * FreezeBlend * 0.5f);
        previousPosition = p - adjustedVelocity;
        OutputPreviousPositions[particleIndex] = float4(previousPosition, 1.0f);
    }

    OutputPositions[particleIndex] = float4(p, 1.0f);
}

[numthreads(64, 1, 1)]
void ClearGridCS(uint3 id : SV_DispatchThreadID)
{
    uint cellIndex = DispatchBaseIndex + id.x;
    uint totalCells = TotalGridCells();
    if (cellIndex >= totalCells)
    {
        return;
    }

    GridCellCounts[cellIndex] = 0u;
    uint start = cellIndex * GridCellCapacity;
    [loop]
    for (uint i = 0; i < GridCellCapacity; ++i)
    {
        GridCellIndices[start + i] = 0xFFFFFFFFu;
    }
}

[numthreads(64, 1, 1)]
void BuildGridCS(uint3 id : SV_DispatchThreadID)
{
    uint dispatchIndex = DispatchBaseIndex + id.x;
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

    uint localIndex = particleIndex % ParticlesPerStrand;
    if (localIndex == 0)
    {
        return;
    }

    float3 p = OutputPositions[particleIndex].xyz;
    uint3 coord = ComputeGridCoord(p);
    uint cellIndex = ComputeGridCellIndex(coord);
    uint slot = 0u;
    InterlockedAdd(GridCellCounts[cellIndex], 1u, slot);
    if (slot < GridCellCapacity)
    {
        GridCellIndices[cellIndex * GridCellCapacity + slot] = particleIndex;
    }
}

[numthreads(64, 1, 1)]
void ClearHashGridCS(uint3 id : SV_DispatchThreadID)
{
    uint index = DispatchBaseIndex + id.x;
    uint totalGuideParticles = TotalGuideParticles();
    uint bucketCount = max(BroadphaseHashBucketCount, 1u);
    if (index < bucketCount)
    {
        GridHashHeads[index] = 0xFFFFFFFFu;
    }
    if (index < totalGuideParticles)
    {
        GridParticleNext[index] = 0xFFFFFFFFu;
    }
}

[numthreads(64, 1, 1)]
void BuildHashGridCS(uint3 id : SV_DispatchThreadID)
{
    uint dispatchIndex = DispatchBaseIndex + id.x;
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

    uint localIndex = particleIndex % ParticlesPerStrand;
    if (localIndex == 0)
    {
        GridParticleNext[particleIndex] = 0xFFFFFFFFu;
        return;
    }

    uint level = (BroadphaseMode == 2u) ? ComputeAdaptiveBroadphaseLevel(localIndex) : 0u;
    float cellSize = (BroadphaseMode == 2u) ? ComputeCellSizeForLevel(level) : GridCellSize;
    int3 coord = ComputeSignedGridCoordWithCellSize(OutputPositions[particleIndex].xyz, cellSize);
    uint bucketIndex = ComputeHashBucketIndexWithLevel(coord, level);
    uint previousHead = 0xFFFFFFFFu;
    InterlockedExchange(GridHashHeads[bucketIndex], particleIndex, previousHead);
    GridParticleNext[particleIndex] = previousHead;
}

[numthreads(64, 1, 1)]
void ClearSortedRangesCS(uint3 id : SV_DispatchThreadID)
{
    uint cellIndex = DispatchBaseIndex + id.x;
    uint totalCells = TotalGridCells();
    if (cellIndex >= totalCells)
    {
        return;
    }

    GridHashHeads[cellIndex] = 0xFFFFFFFFu;
    GridParticleNext[cellIndex] = 0xFFFFFFFFu;
}

[numthreads(64, 1, 1)]
void BuildSortedEntriesCS(uint3 id : SV_DispatchThreadID)
{
    uint entryIndex = DispatchBaseIndex + id.x;
    if (entryIndex >= max(SortedPaddedEntryCount, 1u))
    {
        return;
    }

    uint activeGuideParticles = GetGuideParticleDispatchCount();
    if (entryIndex >= activeGuideParticles)
    {
        GridCellCounts[entryIndex] = 0xFFFFFFFFu;
        GridCellIndices[entryIndex] = 0xFFFFFFFFu;
        return;
    }

    uint particleIndex = ResolveGuideParticleIndex(entryIndex);
    if (particleIndex == 0xFFFFFFFFu)
    {
        GridCellCounts[entryIndex] = 0xFFFFFFFFu;
        GridCellIndices[entryIndex] = 0xFFFFFFFFu;
        return;
    }

    uint cellIndex = ComputeGridCellIndex(ComputeGridCoord(OutputPositions[particleIndex].xyz));
    GridCellCounts[entryIndex] = cellIndex;
    GridCellIndices[entryIndex] = particleIndex;
}

[numthreads(256, 1, 1)]
void BitonicSortCS(uint3 id : SV_DispatchThreadID)
{
    uint index = id.x;
    if (index >= BitonicEntryCount)
    {
        return;
    }

    uint partner = index ^ BitonicJ;
    if (partner <= index || partner >= BitonicEntryCount)
    {
        return;
    }

    uint keyA = GridCellCounts[index];
    uint keyB = GridCellCounts[partner];
    uint particleA = GridCellIndices[index];
    uint particleB = GridCellIndices[partner];
    bool ascending = ((index & BitonicK) == 0u);
    if (ShouldSwapSortedPair(keyA, particleA, keyB, particleB, ascending))
    {
        GridCellCounts[index] = keyB;
        GridCellCounts[partner] = keyA;
        GridCellIndices[index] = particleB;
        GridCellIndices[partner] = particleA;
    }
}

[numthreads(64, 1, 1)]
void BuildSortedRangesCS(uint3 id : SV_DispatchThreadID)
{
    uint entryIndex = DispatchBaseIndex + id.x;
    uint entryCount = max(SortedPaddedEntryCount, 1u);
    if (entryIndex >= entryCount)
    {
        return;
    }

    uint key = GridCellCounts[entryIndex];
    uint particleIndex = GridCellIndices[entryIndex];
    if (key == 0xFFFFFFFFu || particleIndex == 0xFFFFFFFFu || key >= TotalGridCells())
    {
        return;
    }

    uint prevKey = (entryIndex > 0u) ? GridCellCounts[entryIndex - 1u] : 0xFFFFFFFFu;
    if (entryIndex == 0u || prevKey != key)
    {
        GridHashHeads[key] = entryIndex;
    }

    uint nextKey = (entryIndex + 1u < entryCount) ? GridCellCounts[entryIndex + 1u] : 0xFFFFFFFFu;
    if (entryIndex + 1u >= entryCount || nextKey != key)
    {
        GridParticleNext[key] = entryIndex + 1u;
    }
}

float3 ComputeLegacySelfCollision(uint particleIndex, uint strandIndex, uint localIndex)
{
    float3 p = OutputPositions[particleIndex].xyz;
    float3 push = float3(0.0f, 0.0f, 0.0f);
    uint samples = 0u;
    int guideIndex = int(strandIndex);
    int local = int(localIndex);

    [unroll]
    for (int guideOffset = -2; guideOffset <= 2; ++guideOffset)
    {
        int otherGuide = guideIndex + guideOffset;
        if (otherGuide < 0 || otherGuide >= int(GuideStrandCount) || otherGuide == guideIndex)
        {
            continue;
        }

        [unroll]
        for (int localOffset = -1; localOffset <= 1; ++localOffset)
        {
            int otherLocal = local + localOffset;
            if (otherLocal <= 0 || otherLocal >= int(ParticlesPerStrand))
            {
                continue;
            }

            uint otherIndex = uint(otherGuide) * ParticlesPerStrand + uint(otherLocal);
            float3 q = OutputPositions[otherIndex].xyz;
            float3 delta = p - q;
            float distSq = dot(delta, delta);
            if (distSq > 1.0e-8f && distSq < SelfCollisionRadius * SelfCollisionRadius)
            {
                float distValue = sqrt(distSq);
                float penetration = SelfCollisionRadius - distValue;
                push += (delta / distValue) * penetration;
                samples += 1u;
            }
        }
    }

    if (samples > 0u)
    {
        p += (push / float(samples)) * SelfCollisionStiffness;
    }
    return p;
}

bool ShouldCullSelfCollisionPair(uint strandIndex, uint localIndex, uint otherStrand, uint otherLocal)
{
    if (otherStrand == strandIndex)
    {
        int localDelta = abs(int(otherLocal) - int(localIndex));
        return localDelta <= 2;
    }

    if (localIndex <= 2u && otherLocal <= 2u)
    {
        RootBindingData a = RootBindings[strandIndex];
        RootBindingData b = RootBindings[otherStrand];
        if (a.TriangleIndex == b.TriangleIndex)
        {
            return true;
        }
    }

    return false;
}

void AccumulateSelfCollisionResponse(
    uint particleIndex,
    uint strandIndex,
    uint localIndex,
    float3 p,
    uint otherIndex,
    float contactRadius,
    float contactSpringRadius,
    float preferredSeparation,
    inout float3 push,
    inout float3 spring,
    inout uint pushSamples,
    inout uint springSamples)
{
    if (otherIndex == 0xFFFFFFFFu || otherIndex == particleIndex)
    {
        return;
    }

    uint otherStrand = otherIndex / ParticlesPerStrand;
    uint otherLocal = otherIndex % ParticlesPerStrand;
    if (ShouldCullSelfCollisionPair(strandIndex, localIndex, otherStrand, otherLocal))
    {
        return;
    }

    float3 q = OutputPositions[otherIndex].xyz;
    float3 delta = p - q;
    float distSq = dot(delta, delta);
    if (distSq <= 1.0e-8f)
    {
        return;
    }

    float distValue = sqrt(distSq);
    if (distValue >= contactSpringRadius)
    {
        return;
    }

    float3 dir = delta / distValue;
    float kernel = saturate(1.0f - distValue / max(contactSpringRadius, 1.0e-5f));
    float densityWeight = kernel * kernel;
    push += dir * densityWeight;
    pushSamples += 1u;

    if (distValue < preferredSeparation)
    {
        float penetration = preferredSeparation - distValue;
        spring += dir * penetration;
        springSamples += 1u;
    }
}

[numthreads(64, 1, 1)]
void SelfCollisionCS(uint3 id : SV_DispatchThreadID)
{
    if (EnableSelfCollision == 0)
    {
        return;
    }

    uint dispatchIndex = DispatchBaseIndex + id.x;
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
    if (localIndex == 0)
    {
        return;
    }

    if (EnableGridSelfCollision == 0)
    {
        float3 legacyP = ComputeLegacySelfCollision(particleIndex, strandIndex, localIndex);
        legacyP = ApplyLooseGuideConstraints(strandIndex, localIndex, legacyP);
        // SupportCollisionApprox is owned by CollisionCS and must not be
        // applied a second time from the self-collision pass.
        OutputPositions[particleIndex] = float4(legacyP, 1.0f);
        return;
    }

    float3 p = OutputPositions[particleIndex].xyz;
    int3 baseCoord = ComputeSignedGridCoord(p);
    float3 push = float3(0.0f, 0.0f, 0.0f);
    float3 spring = float3(0.0f, 0.0f, 0.0f);
    uint pushSamples = 0u;
    uint springSamples = 0u;
    float contactRadius = SelfCollisionRadius;
    float contactSpringRadius = SelfCollisionRadius * 1.85f;
    float preferredSeparation = SelfCollisionRadius * 1.08f;

    if (BroadphaseMode == 0u)
    {
        [loop]
        for (int dz = -1; dz <= 1; ++dz)
        {
            int z = baseCoord.z + dz;
            if (z < 0 || z >= int(GridDimZ))
            {
                continue;
            }
            [loop]
            for (int dy = -1; dy <= 1; ++dy)
            {
                int y = baseCoord.y + dy;
                if (y < 0 || y >= int(GridDimY))
                {
                    continue;
                }
                [loop]
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int x = baseCoord.x + dx;
                    if (x < 0 || x >= int(GridDimX))
                    {
                        continue;
                    }

                    uint neighborCell = ComputeGridCellIndex(uint3(uint(x), uint(y), uint(z)));
                    uint count = min(GridCellCounts[neighborCell], GridCellCapacity);
                    uint start = neighborCell * GridCellCapacity;
                    [loop]
                    for (uint i = 0u; i < count; ++i)
                    {
                        AccumulateSelfCollisionResponse(
                            particleIndex,
                            strandIndex,
                            localIndex,
                            p,
                            GridCellIndices[start + i],
                            contactRadius,
                            contactSpringRadius,
                            preferredSeparation,
                            push,
                            spring,
                            pushSamples,
                            springSamples);
                    }
                }
            }
        }
    }
    else if (BroadphaseMode == 3u)
    {
        [loop]
        for (int dz = -1; dz <= 1; ++dz)
        {
            int z = baseCoord.z + dz;
            if (z < 0 || z >= int(GridDimZ))
            {
                continue;
            }
            [loop]
            for (int dy = -1; dy <= 1; ++dy)
            {
                int y = baseCoord.y + dy;
                if (y < 0 || y >= int(GridDimY))
                {
                    continue;
                }
                [loop]
                for (int dx = -1; dx <= 1; ++dx)
                {
                    int x = baseCoord.x + dx;
                    if (x < 0 || x >= int(GridDimX))
                    {
                        continue;
                    }

                    uint neighborCell = ComputeGridCellIndex(uint3(uint(x), uint(y), uint(z)));
                    uint start = GridHashHeads[neighborCell];
                    uint end = GridParticleNext[neighborCell];
                    if (start == 0xFFFFFFFFu || end == 0xFFFFFFFFu || end <= start)
                    {
                        continue;
                    }

                    [loop]
                    for (uint i = start; i < end; ++i)
                    {
                        AccumulateSelfCollisionResponse(
                            particleIndex,
                            strandIndex,
                            localIndex,
                            p,
                            GridCellIndices[i],
                            contactRadius,
                            contactSpringRadius,
                            preferredSeparation,
                            push,
                            spring,
                            pushSamples,
                            springSamples);
                    }
                }
            }
        }
    }
    else
    {
        uint queryLevelBegin = 0u;
        uint queryLevelEnd = (BroadphaseMode == 2u) ? 2u : 0u;
        [loop]
        for (uint queryLevel = queryLevelBegin; queryLevel <= queryLevelEnd; ++queryLevel)
        {
            float queryCellSize = (BroadphaseMode == 2u) ? ComputeCellSizeForLevel(queryLevel) : GridCellSize;
            int3 queryBaseCoord = (BroadphaseMode == 2u) ? ComputeSignedGridCoordWithCellSize(p, queryCellSize) : baseCoord;
            [loop]
            for (int dz = -1; dz <= 1; ++dz)
            {
                [loop]
                for (int dy = -1; dy <= 1; ++dy)
                {
                    [loop]
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        int3 neighborCoord = int3(queryBaseCoord.x + dx, queryBaseCoord.y + dy, queryBaseCoord.z + dz);
                        uint bucketIndex = ComputeHashBucketIndexWithLevel(neighborCoord, queryLevel);
                        uint otherIndex = GridHashHeads[bucketIndex];
                        uint chainGuard = 0u;
                        [loop]
                        while (otherIndex != 0xFFFFFFFFu && chainGuard < TotalGuideParticles())
                        {
                            uint otherLocal = otherIndex % ParticlesPerStrand;
                            uint otherLevel = (BroadphaseMode == 2u) ? ComputeAdaptiveBroadphaseLevel(otherLocal) : 0u;
                            float otherCellSize = (BroadphaseMode == 2u) ? ComputeCellSizeForLevel(otherLevel) : GridCellSize;
                            int3 otherCoord = ComputeSignedGridCoordWithCellSize(OutputPositions[otherIndex].xyz, otherCellSize);
                            if (otherLevel == queryLevel && all(otherCoord == neighborCoord))
                            {
                                AccumulateSelfCollisionResponse(
                                    particleIndex,
                                    strandIndex,
                                    localIndex,
                                    p,
                                    otherIndex,
                                    contactRadius,
                                    contactSpringRadius,
                                    preferredSeparation,
                                    push,
                                    spring,
                                    pushSamples,
                                    springSamples);
                            }
                            otherIndex = GridParticleNext[otherIndex];
                            chainGuard += 1u;
                        }
                    }
                }
            }
        }
    }

    if (pushSamples > 0u)
    {
        float densityGain = lerp(0.35f, 1.25f, saturate(float(pushSamples) / 8.0f));
        p += (push / float(pushSamples)) * (SelfCollisionStiffness * densityGain);
    }

    if (springSamples > 0u)
    {
        p += (spring / float(springSamples)) * (SelfCollisionStiffness * 0.22f);
    }

    p = ApplyLooseGuideConstraints(strandIndex, localIndex, p);

    // CollisionCS owns support collision and IntegrateCS owns freeze damping.
    // Reapplying either term here would make the result iteration-count
    // dependent and would over-constrain the same particle twice.
    OutputPositions[particleIndex] = float4(p, 1.0f);
}

[numthreads(256, 1, 1)]
void FollowCS(uint3 id : SV_DispatchThreadID)
{
    uint followerPointDispatchIndex = DispatchBaseIndex + id.x;
    uint totalFollowerPointCount = FollowerStrandCount * ParticlesPerStrand;
    if (followerPointDispatchIndex >= totalFollowerPointCount)
    {
        return;
    }

    uint followerIndex = followerPointDispatchIndex / ParticlesPerStrand;
    uint localPointIndex = followerPointDispatchIndex % ParticlesPerStrand;
    if (followerIndex >= FollowerStrandCount)
    {
        return;
    }

    uint strandIndex = GuideStrandCount + followerIndex;
    FollowData follow = FollowStrands[strandIndex];
    if (follow.ParentGuide < 0 || uint(follow.ParentGuide) >= GuideStrandCount)
    {
        return;
    }

    // RootBindCS owns roots for guides and followers.
    if (localPointIndex == 0u)
    {
        return;
    }

    uint strandStart = strandIndex * ParticlesPerStrand;
    uint strandPointIndex = strandStart + localPointIndex;
    uint parentGuide = uint(follow.ParentGuide);
    uint parentStart = parentGuide * ParticlesPerStrand;
    uint parentPointIndex = parentStart + localPointIndex;

    float3 guideRoot = OutputPositions[parentStart].xyz;
    float3 previousGuideRoot = OutputPreviousPositions[parentStart].xyz;
    float3 guideRestRoot = RestPositions[parentStart].xyz;
    float3 followerRoot = OutputPositions[strandStart].xyz;
    float3 previousFollowerRoot = OutputPreviousPositions[strandStart].xyz;
    float3 followerRestRoot = RestPositions[strandStart].xyz;

    float3 parentPosition = OutputPositions[parentPointIndex].xyz;
    float3 previousParentPosition = OutputPreviousPositions[parentPointIndex].xyz;
    float3 parentRest = RestPositions[parentPointIndex].xyz;
    float3 followerRest = RestPositions[strandPointIndex].xyz;

    float3 guideCurrentLocal = parentPosition - guideRoot;
    float3 guidePreviousLocal = previousParentPosition - previousGuideRoot;
    float3 guideRestLocal = parentRest - guideRestRoot;
    float3 followerRestLocal = followerRest - followerRestRoot;

    // Build a continuous guide-relative target. This remains free of the
    // rest-tangent/current-tangent half-turn singularity that caused explosions.
    float followWeight = saturate(follow.FollowWeight);
    float3 currentFollowerLocal =
        followerRestLocal +
        (guideCurrentLocal - guideRestLocal) * followWeight;
    float3 previousFollowerLocal =
        followerRestLocal +
        (guidePreviousLocal - guideRestLocal) * followWeight;

    float t = (ParticlesPerStrand > 1u)
        ? float(localPointIndex) / float(ParticlesPerStrand - 1u)
        : 0.0f;

    // Keep authored root coverage and allow gradually stronger clumping only
    // toward the tip. Followers therefore do not collapse into one guide sheet.
    float authoredClumpGain = lerp(
        0.42f,
        0.88f,
        saturate(follow.ClumpWeight));
    float rootCoverageBlend = smoothstep(0.48f, 0.92f, t);
    float followerRetention = lerp(
        1.0f,
        authoredClumpGain,
        rootCoverageBlend);
    currentFollowerLocal = lerp(
        guideCurrentLocal,
        currentFollowerLocal,
        followerRetention);
    previousFollowerLocal = lerp(
        guidePreviousLocal,
        previousFollowerLocal,
        followerRetention);

    // Strand-coherent micro motion. One follower receives one phase, while the
    // amplitude increases smoothly toward the tip. This avoids point-to-point
    // glitter and gives neighbouring followers independent secondary motion.
    float followerSeed = frac(sin((float(followerIndex) + 1.0f) * 12.9898f) * 43758.5453f);
    float strandPhase = ElapsedTime * lerp(1.15f, 2.05f, followerSeed) + followerSeed * 6.2831853f;
    float previousStrandPhase = (ElapsedTime - DeltaTime) * lerp(1.15f, 2.05f, followerSeed) + followerSeed * 6.2831853f;
    float noiseAmplitude = min(
        (0.00035f + follow.NoiseWeight * 0.0022f) * smoothstep(0.18f, 1.0f, t),
        max(length(followerRestLocal - guideRestLocal) * 0.10f, 0.00018f));
    float3 currentNoise = float3(
        sin(strandPhase),
        0.35f * sin(strandPhase * 0.73f + 1.2f),
        cos(strandPhase * 1.11f)) * noiseAmplitude;
    float3 previousNoise = float3(
        sin(previousStrandPhase),
        0.35f * sin(previousStrandPhase * 0.73f + 1.2f),
        cos(previousStrandPhase * 1.11f)) * noiseAmplitude;

    float3 target = followerRoot + currentFollowerLocal + currentNoise;
    float3 previousTarget = previousFollowerRoot + previousFollowerLocal + previousNoise;
    float3 translatedRest = followerRoot + followerRestLocal;
    float3 previousTranslatedRest = previousFollowerRoot + followerRestLocal;

    float authoredDistanceFromRoot = max(
        length(followerRestLocal),
        length(guideRestLocal));
    float maximumFollowerRadius = max(
        0.08f,
        authoredDistanceFromRoot * 1.30f + 0.030f);

    bool targetInvalid =
        !IsFinite3(guideRoot) ||
        !IsFinite3(parentPosition) ||
        !IsFinite3(target) ||
        length(target - followerRoot) > maximumFollowerRadius;
    bool previousTargetInvalid =
        !IsFinite3(previousGuideRoot) ||
        !IsFinite3(previousParentPosition) ||
        !IsFinite3(previousTarget) ||
        length(previousTarget - previousFollowerRoot) > maximumFollowerRadius;
    if (targetInvalid)
    {
        target = translatedRest;
    }
    if (previousTargetInvalid)
    {
        previousTarget = previousTranslatedRest;
    }

    float3 currentPosition = OutputPositions[strandPointIndex].xyz;
    float3 previousPosition = OutputPreviousPositions[strandPointIndex].xyz;
    bool stateInvalid =
        !IsFinite3(currentPosition) ||
        !IsFinite3(previousPosition) ||
        length(currentPosition - followerRoot) > maximumFollowerRadius * 1.10f ||
        length(previousPosition - previousFollowerRoot) > maximumFollowerRadius * 1.10f;
    if (stateInvalid)
    {
        currentPosition = target;
        previousPosition = previousTarget;
    }

    // Lightweight per-follower secondary physics. Guide deformation is a moving
    // target rather than a direct position overwrite. Each follower keeps bounded
    // temporal inertia, making the 24 followers around one guide visibly distinct.
    float3 secondaryVelocity = currentPosition - previousPosition;
    float maximumVelocity = lerp(0.0035f, 0.0200f, t);
    float velocityLength = length(secondaryVelocity);
    if (velocityLength > maximumVelocity && velocityLength > 1.0e-7f)
    {
        secondaryVelocity *= maximumVelocity / velocityLength;
    }

    float damping = lerp(0.28f, 0.80f, t) * lerp(0.94f, 1.03f, followerSeed);
    float targetResponse = lerp(0.74f, 0.16f, t) * lerp(0.86f, 1.10f, followerSeed);

    // Drive secondary motion in the local strand frame instead of using the
    // same world-space wobble for every follower. Authored radial placement,
    // phase and damping now produce coherent but visibly independent layers.
    uint parentPreviousLocal = (localPointIndex > 0u) ? (localPointIndex - 1u) : localPointIndex;
    uint parentNextLocal = min(localPointIndex + 1u, ParticlesPerStrand - 1u);
    float3 guideTangent = SafeNormalize(
        OutputPositions[parentStart + parentNextLocal].xyz -
        OutputPositions[parentStart + parentPreviousLocal].xyz);
    if (dot(guideTangent, guideTangent) <= 1.0e-8f)
    {
        guideTangent = float3(0.0f, -1.0f, 0.0f);
    }
    float3 authoredRadial = followerRestLocal - guideRestLocal;
    authoredRadial -= guideTangent * dot(authoredRadial, guideTangent);
    if (dot(authoredRadial, authoredRadial) <= 1.0e-8f)
    {
        authoredRadial = StableCollisionFallback(strandIndex, localPointIndex, guideTangent);
    }
    authoredRadial = SafeNormalize(authoredRadial);
    float3 authoredBinormal = SafeNormalize(cross(guideTangent, authoredRadial));
    if (dot(authoredBinormal, authoredBinormal) <= 1.0e-8f)
    {
        authoredBinormal = StableCollisionFallback(strandIndex + 17u, localPointIndex, guideTangent);
    }

    float airStrength = lerp(0.90f, 5.20f, smoothstep(0.10f, 1.0f, t)) *
        lerp(0.62f, 1.25f, saturate(follow.NoiseWeight + followerSeed * 0.35f));
    float3 lateralAir =
        authoredRadial * sin(strandPhase * 0.67f + followerSeed * 2.1f) +
        authoredBinormal * cos(strandPhase * 0.89f + 1.7f);
    float3 gravitySlip = float3(0.0f, -1.0f, 0.0f) * lerp(0.30f, 2.10f, t);
    float3 secondaryAcceleration = lateralAir * airStrength + gravitySlip;

    float safeDt = max(DeltaTime, 1.0e-5f);

    // Keep the guide-relative target untouched and project only the final
    // prediction. A single analytic follower collision pass is enough for
    // sphere/capsule/plane/SDF shells and avoids tripling collider work across
    // 12.288 million follower points.
    float3 predicted =
        currentPosition +
        secondaryVelocity * damping +
        (target - currentPosition) * targetResponse +
        secondaryAcceleration * (safeDt * safeDt);

    float maximumSecondaryLag = lerp(0.0015f, 0.0220f, t) * lerp(0.85f, 1.15f, followerSeed);
    float3 lag = predicted - target;
    float lagLength = length(lag);
    if (lagLength > maximumSecondaryLag && lagLength > 1.0e-7f)
    {
        predicted = target + lag * (maximumSecondaryLag / lagLength);
    }

    float3 collisionNormal = float3(0.0f, 0.0f, 0.0f);
    bool touchedSurface = false;
    predicted = ResolveFollowerExternalCollisions(
        strandIndex,
        localPointIndex,
        predicted,
        collisionNormal,
        touchedSurface);

    if (!IsFinite3(predicted) ||
        length(predicted - followerRoot) > maximumFollowerRadius)
    {
        predicted = target;
        currentPosition = previousTarget;
        touchedSurface = false;
    }

    // Remove inward normal velocity and retain damped tangential sliding after
    // collision. This prevents the spring target from repeatedly pulling a
    // follower back through the head on the next frame.
    if (touchedSurface && dot(collisionNormal, collisionNormal) > 1.0e-8f)
    {
        float3 resolvedVelocity = predicted - currentPosition;
        float normalSpeed = dot(resolvedVelocity, collisionNormal);
        if (normalSpeed < 0.0f)
        {
            resolvedVelocity -= collisionNormal * normalSpeed;
        }
        resolvedVelocity *= 0.72f;
        currentPosition = predicted - resolvedVelocity;
    }

    // Preserve Verlet history instead of replacing it with the guide's previous
    // target. This is the state that produces follower-specific inertia.
    OutputPositions[strandPointIndex] = float4(predicted, 1.0f);
    OutputPreviousPositions[strandPointIndex] = float4(currentPosition, 1.0f);
}


