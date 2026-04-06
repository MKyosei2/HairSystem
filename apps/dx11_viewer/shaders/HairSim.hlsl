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
StructuredBuffer<uint> CompactionCountersInSim : register(t8);

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

uint TotalGuideParticles()
{
    return GuideStrandCount * ParticlesPerStrand;
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
    uint strandIndex = id.x;
    if (strandIndex >= TotalStrandCount)
    {
        return;
    }

    uint rootIndex = strandIndex * ParticlesPerStrand;
    float3 boundRoot = EvaluateBoundRoot(strandIndex);
    OutputPositions[rootIndex] = float4(boundRoot, 1.0f);
    OutputPreviousPositions[rootIndex] = float4(boundRoot, 1.0f);
}

[numthreads(64, 1, 1)]
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
}

[numthreads(64, 1, 1)]
void DistanceCS(uint3 id : SV_DispatchThreadID)
{
    uint strandIndex = id.x;
    if (strandIndex >= GuideStrandCount)
    {
        return;
    }

    uint start = strandIndex * ParticlesPerStrand;
    float alpha = StretchCompliance / max(DeltaTime * DeltaTime, 1.0e-8f);
    float3 boundRoot = EvaluateBoundRoot(strandIndex);
    OutputPositions[start] = float4(boundRoot, 1.0f);
    OutputPreviousPositions[start] = float4(boundRoot, 1.0f);

    [loop]
    for (uint i = 1; i < ParticlesPerStrand; ++i)
    {
        uint prevIndex = start + i - 1;
        uint currentIndex = start + i;
        uint lambdaIndex = strandIndex * (ParticlesPerStrand - 1) + (i - 1);
        float lambda = DistanceLambdas[lambdaIndex];

        float3 p0 = OutputPositions[prevIndex].xyz;
        float3 p1 = OutputPositions[currentIndex].xyz;
        float3 delta = p1 - p0;
        float lengthValue = max(length(delta), 1.0e-6f);
        float3 dir = delta / lengthValue;
        float C = lengthValue - SegmentLength;
        float w0 = (i == 1) ? 0.0f : 1.0f;
        float w1 = 1.0f;
        float denom = w0 + w1 + alpha;
        float dLambda = (-C - alpha * lambda) / max(denom, 1.0e-6f);
        lambda += dLambda;
        DistanceLambdas[lambdaIndex] = lambda;

        if (w0 > 0.0f)
        {
            p0 -= dir * (w0 * dLambda);
            OutputPositions[prevIndex] = float4(p0, 1.0f);
        }
        p1 += dir * (w1 * dLambda);
        OutputPositions[currentIndex] = float4(p1, 1.0f);
    }
}

[numthreads(64, 1, 1)]
void BendCS(uint3 id : SV_DispatchThreadID)
{
    uint strandIndex = id.x;
    if (strandIndex >= GuideStrandCount || ParticlesPerStrand < 3)
    {
        return;
    }

    uint start = strandIndex * ParticlesPerStrand;
    float alpha = BendCompliance / max(DeltaTime * DeltaTime, 1.0e-8f);

    [loop]
    for (uint i = 1; i + 1 < ParticlesPerStrand; ++i)
    {
        uint prevIndex = start + i - 1;
        uint currentIndex = start + i;
        uint nextIndex = start + i + 1;
        uint lambdaIndex = strandIndex * (ParticlesPerStrand - 2) + (i - 1);
        float lambda = BendLambdas[lambdaIndex];

        float3 a = OutputPositions[prevIndex].xyz;
        float3 b = OutputPositions[currentIndex].xyz;
        float3 c = OutputPositions[nextIndex].xyz;
        float3 midpoint = 0.5f * (a + c);
        float3 delta = b - midpoint;
        float lenValue = length(delta);
        float3 restA = RestPositions[prevIndex].xyz;
        float3 restB = RestPositions[currentIndex].xyz;
        float3 restC = RestPositions[nextIndex].xyz;
        float3 restDelta = restB - 0.5f * (restA + restC);
        float3 direction = (lenValue > 1.0e-6f) ? (delta / lenValue) : SafeNormalize(restDelta + float3(0.0f, 1.0e-4f, 0.0f));

        float C = 0.0f;
        if (EnableRuntimeAngularBend != 0u)
        {
            float prevLength = length(b - a);
            float nextLength = length(c - b);
            float targetOffset = ComputeAngularTargetOffset(prevLength, nextLength);
            alpha = AngularBendCompliance / max(DeltaTime * DeltaTime, 1.0e-8f);
            C = (lenValue - targetOffset) * max(AngularBendStiffness, 1.0e-4f);
        }
        else
        {
            lenValue = max(lenValue, 1.0e-6f);
            C = lenValue * BendStiffness;
        }

        float dLambda = (-C - alpha * lambda) / (1.0f + alpha);
        lambda += dLambda;
        BendLambdas[lambdaIndex] = lambda;

        float3 correction = direction * dLambda;
        float blend = (EnableRuntimeAngularBend != 0u) ? saturate(1.0f - AngularBendDamping) : 1.0f;
        b = lerp(b, b + correction, blend);
        OutputPositions[currentIndex] = float4(b, 1.0f);
    }
}

[numthreads(64, 1, 1)]
void CollisionCS(uint3 id : SV_DispatchThreadID)
{
    uint particleIndex = id.x;
    uint totalGuideParticles = TotalGuideParticles();
    if (particleIndex >= totalGuideParticles)
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
    for (uint i = 0; i < SphereColliderCount; ++i)
    {
        float3 center = float3(SphereData[i * 4 + 0], SphereData[i * 4 + 1], SphereData[i * 4 + 2]);
        float radius = SphereData[i * 4 + 3];
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
    for (uint i = 0; i < CapsuleColliderCount; ++i)
    {
        float3 a = float3(CapsuleData[i * 8 + 0], CapsuleData[i * 8 + 1], CapsuleData[i * 8 + 2]);
        float ra = CapsuleData[i * 8 + 3];
        float3 b = float3(CapsuleData[i * 8 + 4], CapsuleData[i * 8 + 5], CapsuleData[i * 8 + 6]);
        float rb = CapsuleData[i * 8 + 7];
        float3 resolved = ResolveCapsule(p, a, ra, b, rb);
        touchedSurface = touchedSurface || (length(resolved - p) > 1.0e-6f);
        p = resolved;
    }

    if (p.y < FloorY)
    {
        p.y = FloorY;
        touchedSurface = true;
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
    uint cellIndex = id.x;
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
    uint particleIndex = id.x;
    uint totalGuideParticles = TotalGuideParticles();
    if (particleIndex >= totalGuideParticles)
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
    uint index = id.x;
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
    uint particleIndex = id.x;
    uint totalGuideParticles = TotalGuideParticles();
    if (particleIndex >= totalGuideParticles)
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
    uint cellIndex = id.x;
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
    uint entryIndex = id.x;
    if (entryIndex >= max(SortedPaddedEntryCount, 1u))
    {
        return;
    }

    uint totalGuideParticles = TotalGuideParticles();
    if (entryIndex >= totalGuideParticles)
    {
        GridCellCounts[entryIndex] = 0xFFFFFFFFu;
        GridCellIndices[entryIndex] = 0xFFFFFFFFu;
        return;
    }

    uint localIndex = entryIndex % ParticlesPerStrand;
    if (localIndex == 0u)
    {
        GridCellCounts[entryIndex] = 0xFFFFFFFFu;
        GridCellIndices[entryIndex] = 0xFFFFFFFFu;
        return;
    }

    uint cellIndex = ComputeGridCellIndex(ComputeGridCoord(OutputPositions[entryIndex].xyz));
    GridCellCounts[entryIndex] = cellIndex;
    GridCellIndices[entryIndex] = entryIndex;
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
    uint entryIndex = id.x;
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

    uint particleIndex = id.x;
    uint totalGuideParticles = TotalGuideParticles();
    if (particleIndex >= totalGuideParticles)
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
        legacyP = ApplySupportCollisionApprox(strandIndex, localIndex, legacyP);
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
        uint queryLevelBegin = (BroadphaseMode == 2u) ? 0u : 0u;
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
                        while (otherIndex != 0xFFFFFFFFu && chainGuard < totalGuideParticles)
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
    p = ApplySupportCollisionApprox(strandIndex, localIndex, p);

    float freezeFactor = ComputeBackFreezeFactor(strandIndex, localIndex);
    if (freezeFactor > 0.0f)
    {
        float3 current = OutputPositions[particleIndex].xyz;
        p = lerp(p, current, freezeFactor * FreezeBlend * 0.35f);
    }

    OutputPositions[particleIndex] = float4(p, 1.0f);
}

[numthreads(64, 1, 1)]
void FollowCS(uint3 id : SV_DispatchThreadID)
{
    uint followerDispatchIndex = id.x;
    uint followerIndex = followerDispatchIndex;
    if (UseCompactedFollowerDispatch != 0u)
    {
        if (followerDispatchIndex >= CompactionCountersInSim[0])
        {
            return;
        }
        followerIndex = CompactedFollowerIndices[followerDispatchIndex];
    }
    else if (followerIndex >= FollowerStrandCount)
    {
        return;
    }

    uint strandIndex = GuideStrandCount + followerIndex;
    FollowData follow = FollowStrands[strandIndex];
    if (follow.ParentGuide < 0)
    {
        return;
    }

    uint parentStart = uint(follow.ParentGuide) * ParticlesPerStrand;
    uint strandStart = strandIndex * ParticlesPerStrand;
    float3 boundFollowerRoot = EvaluateBoundRoot(strandIndex);

    [loop]
    for (uint i = 0; i < ParticlesPerStrand; ++i)
    {
        uint parentPointIndex = parentStart + i;
        uint strandPointIndex = strandStart + i;

        float3 parentPos = OutputPositions[parentPointIndex].xyz;
        float3 parentRest = RestPositions[parentPointIndex].xyz;
        float3 followerRest = RestPositions[strandPointIndex].xyz;
        float3 restOffset = followerRest - parentRest;

        float t = (ParticlesPerStrand > 1) ? (float(i) / float(ParticlesPerStrand - 1)) : 0.0f;
        float noisePhase = ElapsedTime * (1.7f + 0.6f * follow.NoiseWeight) + strandIndex * 0.173f + i * 0.217f;
        float noiseAmp = follow.NoiseWeight * (0.0015f + t * 0.0065f);
        float3 noise = float3(sin(noisePhase), 0.0f, cos(noisePhase * 1.17f)) * noiseAmp;

        float followGain = saturate(lerp(1.0f, follow.FollowWeight, FollowStiffness));
        float clumpGain = lerp(0.25f, 1.0f, follow.ClumpWeight);
        float curveGain = 0.35f + 0.65f * t;
        float3 parentDelta = parentPos - parentRest;
        float3 target = parentPos + restOffset * clumpGain + parentDelta * curveGain * 0.12f + noise;
        float3 current = InputPositions[strandPointIndex].xyz;
        float3 blended = lerp(current, target, followGain);

        if (i == 0)
        {
            blended = boundFollowerRoot;
        }

        OutputPreviousPositions[strandPointIndex] = float4(InputPositions[strandPointIndex].xyz, 1.0f);
        OutputPositions[strandPointIndex] = float4(blended, 1.0f);
    }
}
