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

    float SphereData[16];
    float CapsuleData[32];
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

RWStructuredBuffer<float4> OutputPositions : register(u0);
RWStructuredBuffer<float4> OutputPreviousPositions : register(u1);
RWStructuredBuffer<float> DistanceLambdas : register(u2);
RWStructuredBuffer<float> BendLambdas : register(u3);
RWStructuredBuffer<uint> GridCellCounts : register(u4);
RWStructuredBuffer<uint> GridCellIndices : register(u5);

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

uint3 ComputeGridCoord(float3 p)
{
    float3 local = (p - GridOrigin) / max(GridCellSize, 1.0e-5f);
    uint3 coord;
    coord.x = min((uint)clamp(floor(local.x), 0.0f, max((float)GridDimX - 1.0f, 0.0f)), GridDimX - 1u);
    coord.y = min((uint)clamp(floor(local.y), 0.0f, max((float)GridDimY - 1.0f, 0.0f)), GridDimY - 1u);
    coord.z = min((uint)clamp(floor(local.z), 0.0f, max((float)GridDimZ - 1.0f, 0.0f)), GridDimZ - 1u);
    return coord;
}

uint ComputeGridCellIndex(uint3 coord)
{
    return coord.x + coord.y * GridDimX + coord.z * GridDimX * GridDimY;
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
        float lenValue = max(length(delta), 1.0e-6f);
        float C = lenValue * BendStiffness;
        float dLambda = (-C - alpha * lambda) / (1.0f + alpha);
        lambda += dLambda;
        BendLambdas[lambdaIndex] = lambda;

        float3 correction = (delta / lenValue) * dLambda;
        b += correction;
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
    uint3 baseCoord = ComputeGridCoord(p);
    float3 push = float3(0.0f, 0.0f, 0.0f);
    float3 spring = float3(0.0f, 0.0f, 0.0f);
    uint pushSamples = 0u;
    uint springSamples = 0u;
    float contactRadius = SelfCollisionRadius;
    float contactSpringRadius = SelfCollisionRadius * 1.85f;
    float preferredSeparation = SelfCollisionRadius * 1.08f;

    [loop]
    for (int dz = -1; dz <= 1; ++dz)
    {
        int z = int(baseCoord.z) + dz;
        if (z < 0 || z >= int(GridDimZ))
        {
            continue;
        }
        [loop]
        for (int dy = -1; dy <= 1; ++dy)
        {
            int y = int(baseCoord.y) + dy;
            if (y < 0 || y >= int(GridDimY))
            {
                continue;
            }
            [loop]
            for (int dx = -1; dx <= 1; ++dx)
            {
                int x = int(baseCoord.x) + dx;
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
                    uint otherIndex = GridCellIndices[start + i];
                    if (otherIndex == 0xFFFFFFFFu || otherIndex == particleIndex)
                    {
                        continue;
                    }

                    uint otherStrand = otherIndex / ParticlesPerStrand;
                    uint otherLocal = otherIndex % ParticlesPerStrand;
                    if (otherStrand == strandIndex && abs(int(otherLocal) - int(localIndex)) <= 1)
                    {
                        continue;
                    }

                    float3 q = OutputPositions[otherIndex].xyz;
                    float3 delta = p - q;
                    float distSq = dot(delta, delta);
                    if (distSq <= 1.0e-8f)
                    {
                        continue;
                    }

                    float distValue = sqrt(distSq);
                    float3 dir = delta / distValue;
                    if (distValue < contactRadius)
                    {
                        float penetration = contactRadius - distValue;
                        push += dir * penetration;
                        pushSamples += 1u;
                    }
                    else if (distValue < contactSpringRadius)
                    {
                        float error = distValue - preferredSeparation;
                        float springWeight = saturate(1.0f - (distValue - contactRadius) / max(contactSpringRadius - contactRadius, 1.0e-5f));
                        spring += dir * error * springWeight;
                        springSamples += 1u;
                    }
                }
            }
        }
    }

    if (pushSamples > 0u)
    {
        p += (push / float(pushSamples)) * SelfCollisionStiffness;
    }

    if (springSamples > 0u)
    {
        p -= (spring / float(springSamples)) * (SelfCollisionStiffness * 0.18f);
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
    uint followerIndex = id.x;
    if (followerIndex >= FollowerStrandCount)
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
