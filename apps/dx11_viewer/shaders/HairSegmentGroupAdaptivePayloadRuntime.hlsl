// HairSegmentGroupAdaptivePayloadRuntime.hlsl
// Novelty 01 complete SG-APT runtime.
// Shader Model 5.0 / DirectX 11.
//
// The file is compiled repeatedly with N01_BINDING_MODE so every pass remains
// within the eight-UAV limit of D3D11 compute shaders.
//
// 0: descriptor, block invalidation and state transition
// 1: deterministic two-level scan and stable compaction
// 2: payload remap, static transform, repair, cache and shadow validation
// 3: mixed-state dynamic solve and self-collision
// 4: root binding and local follower reconstruction
// 6: shared-point candidate generation and stable resolve
// 7: payload owner/epoch validation and lifecycle
// 8: guide/follower/render dirty-range propagation
// 9: formal ground-truth comparison and invariant verification
// 10: indirect dispatch argument generation and B4 migration baseline

#ifndef N01_BINDING_MODE
#define N01_BINDING_MODE 0
#endif

static const uint N01_INVALID = 0xffffffffu;
static const uint N01_STATE_STATIC_REUSE = 0u;
static const uint N01_STATE_STATIC_TRANSFORM = 1u;
static const uint N01_STATE_DYNAMIC = 2u;
static const uint N01_STATE_DEFERRED = 3u;
static const uint N01_STATE_REPAIR = 4u;
static const uint N01_STATE_FORCED_DYNAMIC = 5u;
static const uint N01_STATE_INVALID = 6u;

static const uint N01_DIRTY_ROOT = 1u << 0u;
static const uint N01_DIRTY_FORCE = 1u << 1u;
static const uint N01_DIRTY_COLLIDER = 1u << 2u;
static const uint N01_DIRTY_TOPOLOGY = 1u << 3u;
static const uint N01_DIRTY_EDIT = 1u << 4u;
static const uint N01_DIRTY_DEPENDENCY = 1u << 5u;
static const uint N01_DIRTY_AGE = 1u << 6u;
static const uint N01_DIRTY_BOUNDARY = 1u << 7u;
static const uint N01_DIRTY_WATCHDOG = 1u << 8u;
static const uint N01_DIRTY_PAYLOAD = 1u << 9u;
static const uint N01_DIRTY_NONFINITE = 1u << 10u;
static const uint N01_DIRTY_COLLISION = 1u << 11u;

static const uint N01_CORE_CACHE_VALID = 1u << 0u;
static const uint N01_CORE_PAYLOAD_VALID = 1u << 1u;
static const uint N01_CORE_FORCED_DYNAMIC = 1u << 2u;
static const uint N01_CORE_BASELINE_FALLBACK = 1u << 3u;
static const uint N01_CORE_SHADOW_SAMPLE = 1u << 4u;
static const uint N01_CORE_UNIFORM_REST = 1u << 5u;
static const uint N01_CORE_PAYLOAD_RETIRED = 1u << 6u;
static const uint N01_CORE_GROUND_TRUTH_SAMPLE = 1u << 7u;

static const uint N01_COUNTER_DYNAMIC = 0u;
static const uint N01_COUNTER_STATIC_REUSE = 1u;
static const uint N01_COUNTER_STATIC_TRANSFORM = 2u;
static const uint N01_COUNTER_REPAIR = 3u;
static const uint N01_COUNTER_INVALID = 4u;
static const uint N01_COUNTER_FALLBACK = 5u;
static const uint N01_COUNTER_TRANSITIONS = 6u;
static const uint N01_COUNTER_OSCILLATIONS = 7u;
static const uint N01_COUNTER_DIRTY_BLOCKS = 8u;
static const uint N01_COUNTER_DIRTY_GROUPS = 9u;
static const uint N01_COUNTER_CACHE_HITS = 10u;
static const uint N01_COUNTER_CACHE_MISSES = 11u;
static const uint N01_COUNTER_CACHE_INVALIDATIONS = 12u;
static const uint N01_COUNTER_PAYLOAD_ACTIVE = 13u;
static const uint N01_COUNTER_PAYLOAD_OVERFLOW = 14u;
static const uint N01_COUNTER_FALSE_STATIC = 15u;
static const uint N01_COUNTER_SHADOW_SAMPLES = 16u;
static const uint N01_COUNTER_SHARED_CONFLICTS = 17u;
static const uint N01_COUNTER_DIRTY_FOLLOWERS = 18u;
static const uint N01_COUNTER_DIRTY_RENDER_POINTS = 19u;
static const uint N01_COUNTER_WATCHDOG = 20u;
static const uint N01_COUNTER_REPAIR_SUCCESS = 21u;
static const uint N01_COUNTER_REPAIR_FAILURE = 22u;
static const uint N01_COUNTER_FULL_DYNAMIC = 23u;
static const uint N01_COUNTER_CANDIDATE_DYNAMIC = 24u;
static const uint N01_COUNTER_CANDIDATE_STATIC = 25u;
static const uint N01_COUNTER_CANDIDATE_REPAIR = 26u;
static const uint N01_COUNTER_SCAN_BLOCKS = 27u;
static const uint N01_COUNTER_READ_WORDS = 28u;
static const uint N01_COUNTER_WRITE_WORDS = 29u;
static const uint N01_COUNTER_DESCRIPTOR_VALID = 30u;
static const uint N01_COUNTER_FALSE_DYNAMIC = 31u;
static const uint N01_COUNTER_PAYLOAD_ALLOCATED = 32u;
static const uint N01_COUNTER_PAYLOAD_RETIRED = 33u;
static const uint N01_COUNTER_PAYLOAD_REUSED = 34u;
static const uint N01_COUNTER_PAYLOAD_OWNER_MISMATCH = 35u;
static const uint N01_COUNTER_PAYLOAD_VERSION_MISMATCH = 36u;
static const uint N01_COUNTER_DUPLICATE_PAYLOAD_OWNER = 37u;
static const uint N01_COUNTER_INVALID_PAYLOAD_INDEX = 38u;
static const uint N01_COUNTER_BLOCK_SKIPPED = 39u;
static const uint N01_COUNTER_GUIDE_DIRTY_RANGE = 40u;
static const uint N01_COUNTER_FOLLOWER_DIRTY_RANGE = 41u;
static const uint N01_COUNTER_RENDER_DIRTY_RANGE = 42u;
static const uint N01_COUNTER_FOLLOWER_REBUILT_POINTS = 43u;
static const uint N01_COUNTER_RENDER_REGENERATED_VERTICES = 44u;
static const uint N01_COUNTER_GROUND_TRUTH_SAMPLES = 45u;
static const uint N01_COUNTER_GROUND_TRUTH_FALSE_STATIC = 46u;
static const uint N01_COUNTER_GROUND_TRUTH_VIOLATIONS = 47u;
static const uint N01_COUNTER_DISPATCH_MISMATCH = 48u;
static const uint N01_COUNTER_SHARED_UNRESOLVED = 49u;
static const uint N01_COUNTER_DEPENDENCY_FALLBACK = 50u;
static const uint N01_COUNTER_RENDER_FALLBACK = 51u;
static const uint N01_COUNTER_TRANSITION_BYTES = 52u;
static const uint N01_COUNTER_GROUP_READ_BYTES = 53u;
static const uint N01_COUNTER_GROUP_WRITE_BYTES = 54u;
static const uint N01_COUNTER_PAYLOAD_READ_BYTES = 55u;
static const uint N01_COUNTER_PAYLOAD_WRITE_BYTES = 56u;
static const uint N01_COUNTER_STATE_COPY_BYTES = 57u;
static const uint N01_COUNTER_MEMORY_LEAK = 58u;
static const uint N01_COUNTER_NAN = 59u;
static const uint N01_COUNTER_INF = 60u;
static const uint N01_COUNTER_PENETRATION = 61u;
static const uint N01_COUNTER_DESCRIPTOR_VIOLATION = 62u;
static const uint N01_COUNTER_SAFE_STOP = 63u;

static const uint N01_FEATURE_OPTIONAL_PAYLOAD = 1u << 0u;
static const uint N01_FEATURE_FIXED_GROUP_INDEX = 1u << 1u;
static const uint N01_FEATURE_VERSION_CHECK = 1u << 2u;
static const uint N01_FEATURE_DELAYED_REUSE = 1u << 3u;
static const uint N01_FEATURE_HYSTERESIS = 1u << 4u;
static const uint N01_FEATURE_MINIMUM_RESIDENCE = 1u << 5u;
static const uint N01_FEATURE_STATIC_CACHE = 1u << 6u;
static const uint N01_FEATURE_INCREMENTAL_BLOCKS = 1u << 7u;
static const uint N01_FEATURE_STABLE_SHARED_POINT = 1u << 8u;
static const uint N01_FEATURE_FOLLOWER_DEPENDENCY = 1u << 9u;
static const uint N01_FEATURE_RENDER_DIRTY_RANGE = 1u << 10u;
static const uint N01_FEATURE_FAST_PATH = 1u << 11u;
static const uint N01_FEATURE_SHADOW_DYNAMIC = 1u << 12u;
static const uint N01_FEATURE_WATCHDOG = 1u << 13u;
static const uint N01_FEATURE_STABLE_COMPACTION = 1u << 14u;
static const uint N01_FEATURE_PAYLOAD_OWNER = 1u << 15u;
static const uint N01_FEATURE_GROUND_TRUTH = 1u << 16u;
static const uint N01_FEATURE_INDIRECT_DISPATCH = 1u << 17u;
static const uint N01_FEATURE_BASELINE_EQUIVALENT = 1u << 18u;
static const uint N01_FEATURE_SWEPT_SPATIAL = 1u << 19u;
static const uint N01_FEATURE_TRANSITION_BUDGET = 1u << 20u;
static const uint N01_FEATURE_HARD_SOFT_DESCRIPTOR = 1u << 21u;
static const uint N01_FEATURE_BYTE_TELEMETRY = 1u << 22u;
static const uint N01_FEATURE_FORMAL_TELEMETRY = 1u << 23u;

struct SegmentGroupCore
{
    uint strandId;
    uint localGroupIndex;
    uint startPointIndex;
    uint endPointIndex;

    uint parentGroupIndex;
    uint childGroupIndex;
    uint ownerGroupIndex;
    uint blockIndex;

    float restLength;
    float3 restDirection;

    uint state;
    uint previousState;
    uint dirtyFlags;
    uint flags;
};

struct CoreExtension
{
    uint payloadIndex;
    uint payloadVersion;
    uint rootTransformVersion;
    uint forceVersion;

    uint colliderVersion;
    uint topologyVersion;
    uint editVersion;
    uint dependencyVersion;

    uint lastSolvedFrame;
    uint lastValidatedFrame;
    uint lastTransitionFrame;
    uint minimumResidenceEndFrame;

    uint stableCandidateFrames;
    uint transitionCount;
    uint lastPayloadRetireFrame;
    uint payloadGeneration;

    float maximumObservedPositionErrorMm;
    float maximumObservedBoundaryErrorMm;
    float minimumObservedCollisionMarginMm;
    float predictedPositionErrorMm;
};

struct StaticCache
{
    float3 rootLocalEnd;
    float restLength;

    float3 localDirection;
    float boundaryErrorMm;

    float3 previousRootLocalEnd;
    float maximumErrorMm;

    uint sourceFrame;
    uint rootTransformVersion;
    uint topologyVersion;
    uint valid;
};

struct DynamicPayload
{
    float3 previousEndPosition;
    float distanceLambda;

    float3 velocity;
    float bendLambda;

    float constraintResidual;
    float collisionResidual;
    float predictedPositionErrorMm;
    float boundaryErrorMm;

    uint solverFlags;
    uint collisionFlags;
    uint allocationEpoch;
    uint retireEpoch;
};

struct PayloadOwner
{
    uint groupIndex;
    uint payloadVersion;
    uint generation;
    uint allocationEpoch;

    uint retireEpoch;
    uint safeReuseEpoch;
    uint state;
    uint reserved;
};

struct GuideDirtyRange
{
    uint segmentBegin;
    uint segmentEnd;
    uint flags;
    uint dependencyVersion;
};

struct FollowerDirtyRange
{
    uint followerStrandIndex;
    uint segmentBegin;
    uint segmentEnd;
    uint flags;

    uint sourceGuideIndex;
    uint dependencyVersion;
    uint guardBand;
    uint reserved;
};

struct RenderDirtyRange
{
    uint strandIndex;
    uint pointBegin;
    uint pointEnd;
    uint flags;
};

struct GroundTruthMetric
{
    float squaredPositionErrorMm2;
    float maximumPositionErrorMm;
    float segmentLengthErrorRatio;
    float boundaryErrorMm;

    float followerPositionErrorMm;
    float renderGeometryErrorMm;
    uint staticGroup;
    uint falseStatic;
};

struct ReuseDescriptor
{
    float positionErrorMm;
    float velocityDisplacementMm;
    float accelerationDisplacementMm;
    float constraintResidual;

    float rootTranslationDisplacementMm;
    float rootRotationDisplacementMm;
    float forceDisplacementMm;
    float colliderDisplacementMm;

    float collisionMarginMm;
    float boundaryErrorMm;
    float lengthErrorRatio;
    float shadowErrorMm;

    uint hardFlags;
    uint softFlags;
    uint ageFrames;
    uint candidateState;

    uint rootTransformVersion;
    uint forceVersion;
    uint colliderVersion;
    uint editVersion;
};

struct BlockDescriptor
{
    uint firstGroupIndex;
    uint groupCount;
    uint dirtyFlags;
    uint validFlags;

    float3 boundsMin;
    float maximumMotionMm;

    float3 boundsMax;
    float minimumCollisionMarginMm;

    float maximumResidual;
    float maximumBoundaryErrorMm;
    uint descriptorVersion;
    uint lastUpdatedFrame;
};

struct SharedPointCandidate
{
    float3 position;
    float positionErrorMm;

    float constraintResidual;
    float collisionResidual;
    uint sourceGroupIndex;
    uint priority;

    uint targetPointIndex;
    uint finite;
    uint valid;
    uint reserved;
};

struct TriangleData
{
    uint a;
    uint b;
    uint c;
    uint padding;
};

struct RootBindingData
{
    uint triangleIndex;
    float3 barycentric;
    float2 rootUv;
    float2 padding;
};

struct FollowData
{
    int parentGuide;
    float followWeight;
    float clumpWeight;
    float noiseWeight;
};

cbuffer Novelty01Constants : register(b0)
{
    uint gGroupCount;
    uint gBlockCount;
    uint gScanBlockCount;
    uint gGroupsPerBlock;

    uint gGuideCount;
    uint gFollowerCount;
    uint gParticlesPerStrand;
    uint gPayloadCapacity;

    uint gFrameIndex;
    uint gRootTransformVersion;
    uint gForceVersion;
    uint gColliderVersion;

    uint gTopologyVersion;
    uint gEditVersion;
    uint gDependencyVersion;
    uint gDescriptorMaximumAge;

    uint gMinimumResidenceFrames;
    uint gConsecutiveStaticFrames;
    uint gStaticTransitionBudget;
    uint gPayloadRetireDelayFrames;

    uint gPassKind;
    uint gSolverIteration;
    uint gSolverPhase;
    uint gRuntimeFlags;

    uint gFeatureMaskLow;
    uint gFeatureMaskHigh;
    uint gExperimentVariant;
    uint gPayloadGeneration;

    float gDeltaTime;
    float gStaticPositionThresholdMm;
    float gDynamicPositionThresholdMm;
    float gStaticVelocityThresholdMm;

    float gDynamicVelocityThresholdMm;
    float gLengthErrorThresholdRatio;
    float gBoundaryErrorThresholdMm;
    float gCollisionSafetyPaddingMm;

    float gShadowErrorThresholdMm;
    float gGravity;
    float gDamping;
    float gWindStrength;

    float3 gWindDirection;
    float gFloorY;

    float gStretchCompliance;
    float gBendCompliance;
    float gBendStiffness;
    float gSelfCollisionRadius;

    float gSelfCollisionStiffness;
    float gGridCellSize;
    uint gHashBucketCount;
    uint gEnableSelfCollision;

    float gRootStiffness;
    float gSupportCollisionRadius;
    float gSupportCollisionStiffness;
    float gTangentialFriction;

    float gLateralFriction;
    float gAngularBendRadians;
    float gAngularBendCompliance;
    float gAngularBendStiffness;

    float gAngularBendDamping;
    float gFullDynamicEnterRatio;
    float gFullDynamicExitRatio;
    uint gFullDynamicDecisionFrames;

    float gVelocityShockPropagation;
    float gFreezeBackThreshold;
    float gFreezeBlend;
    float gLooseConstraintStiffness;

    float gLooseConstraintMaxDistance;
    uint gSdfValueCount;
    uint gUseSdfCollider;
    uint gSolverIterations;

    float gScalpMotionBlend;
    float gElapsedTime;
    uint gBaselinePadding0;
    uint gBaselinePadding1;

    uint gScenarioId;
    uint gScenarioTick;
    uint gScenarioFlags;
    uint gScenarioTargetGroup;

    uint4 gSdfResolution;
    float4 gSdfBoundsMin;
    float4 gSdfBoundsMax;

    uint gSphereColliderCount;
    uint gCapsuleColliderCount;
    uint gPlaneColliderCount;
    uint gAabbColliderCount;

    float3 gSweptRootBoundsMin;
    float gSweptRootPadding0;

    float3 gSweptRootBoundsMax;
    float gSweptRootPadding1;

    float3 gSweptColliderBoundsMin;
    float gSweptColliderPadding0;

    float3 gSweptColliderBoundsMax;
    float gSweptColliderPadding1;

    float4 gSphereData[4];
    float4 gCapsuleData[8];
    float4 gPlaneData[4];
    float4 gAabbData[8];
};

bool N01FeatureEnabled(uint bit)
{
    return (gFeatureMaskLow & bit) != 0u;
}

bool N01Finite3(float3 value)
{
    return all(value == value) &&
        all(abs(value) < float3(1.0e10f, 1.0e10f, 1.0e10f));
}

float N01SafeLength(float3 value)
{
    return sqrt(max(dot(value, value), 1.0e-12f));
}

float3 N01SafeNormalize(float3 value, float3 fallback)
{
    const float lengthValue = N01SafeLength(value);
    return lengthValue > 1.0e-6f ? value / lengthValue : fallback;
}

bool N01AabbIntersects(float3 aMin, float3 aMax, float3 bMin, float3 bMax)
{
    return all(aMax >= bMin) && all(bMax >= aMin);
}

float N01DistanceToAabb(float3 p, float3 mn, float3 mx)
{
    const float3 q = max(max(mn - p, p - mx), 0.0f);
    return length(q);
}

float3 N01ResolveAabb(float3 p, float3 mn, float3 mx)
{
    if (all(p >= mn) && all(p <= mx))
    {
        const float3 d0 = p - mn;
        const float3 d1 = mx - p;
        float best = d0.x;
        uint axis = 0u;
        bool useMax = false;
        if (d1.x < best) { best = d1.x; axis = 0u; useMax = true; }
        if (d0.y < best) { best = d0.y; axis = 1u; useMax = false; }
        if (d1.y < best) { best = d1.y; axis = 1u; useMax = true; }
        if (d0.z < best) { best = d0.z; axis = 2u; useMax = false; }
        if (d1.z < best) { axis = 2u; useMax = true; }
        if (axis == 0u) p.x = useMax ? mx.x : mn.x;
        if (axis == 1u) p.y = useMax ? mx.y : mn.y;
        if (axis == 2u) p.z = useMax ? mx.z : mn.z;
    }
    return p;
}

float3 N01ResolveCapsule(float3 p, float4 aData, float4 bData)
{
    const float3 axis = bData.xyz - aData.xyz;
    const float axisLengthSquared = max(dot(axis, axis), 1.0e-8f);
    const float t = saturate(dot(p - aData.xyz, axis) / axisLengthSquared);
    const float3 center = lerp(aData.xyz, bData.xyz, t);
    const float radius = lerp(aData.w, bData.w, t);
    const float3 delta = p - center;
    const float distanceSquared = max(dot(delta, delta), 1.0e-10f);
    if (distanceSquared < radius * radius)
    {
        const float3 fallback = N01SafeNormalize(
            cross(axis, float3(0.0f, 1.0f, 0.0f)),
            float3(1.0f, 0.0f, 0.0f));
        const float3 normal =
            distanceSquared > 1.0e-9f
            ? delta * rsqrt(distanceSquared)
            : fallback;
        return center + normal * radius;
    }
    return p;
}

float N01CollisionMarginMm(float3 p)
{
    float margin = 1.0e9f;

    [loop]
    for (uint i = 0u; i < gSphereColliderCount; ++i)
    {
        const float distanceValue =
            length(p - gSphereData[i].xyz) - gSphereData[i].w;
        margin = min(margin, distanceValue * 1000.0f);
    }

    [loop]
    for (uint i = 0u; i < gCapsuleColliderCount; ++i)
    {
        const float4 aData = gCapsuleData[i * 2u + 0u];
        const float4 bData = gCapsuleData[i * 2u + 1u];
        const float3 axis = bData.xyz - aData.xyz;
        const float axisLengthSquared = max(dot(axis, axis), 1.0e-8f);
        const float t = saturate(dot(p - aData.xyz, axis) / axisLengthSquared);
        const float radius = lerp(aData.w, bData.w, t);
        const float3 center = lerp(aData.xyz, bData.xyz, t);
        margin = min(margin, (length(p - center) - radius) * 1000.0f);
    }

    [loop]
    for (uint i = 0u; i < gPlaneColliderCount; ++i)
    {
        margin = min(
            margin,
            (dot(p, gPlaneData[i].xyz) + gPlaneData[i].w) * 1000.0f);
    }

    [loop]
    for (uint i = 0u; i < gAabbColliderCount; ++i)
    {
        margin = min(
            margin,
            N01DistanceToAabb(
                p,
                gAabbData[i * 2u + 0u].xyz,
                gAabbData[i * 2u + 1u].xyz) * 1000.0f);
    }

    margin = min(margin, (p.y - gFloorY) * 1000.0f);
    return margin;
}

float3 N01ResolveExternalCollision(float3 p, out float residual)
{
    const float3 original = p;

    [loop]
    for (uint i = 0u; i < gSphereColliderCount; ++i)
    {
        const float3 delta = p - gSphereData[i].xyz;
        const float radius = gSphereData[i].w;
        const float distanceSquared = max(dot(delta, delta), 1.0e-10f);
        if (distanceSquared < radius * radius)
        {
            const float3 normal =
                distanceSquared > 1.0e-9f
                ? delta * rsqrt(distanceSquared)
                : float3(0.0f, 1.0f, 0.0f);
            p = gSphereData[i].xyz + normal * radius;
        }
    }

    [loop]
    for (uint i = 0u; i < gCapsuleColliderCount; ++i)
    {
        p = N01ResolveCapsule(
            p,
            gCapsuleData[i * 2u + 0u],
            gCapsuleData[i * 2u + 1u]);
    }

    [loop]
    for (uint i = 0u; i < gPlaneColliderCount; ++i)
    {
        const float signedDistance =
            dot(p, gPlaneData[i].xyz) + gPlaneData[i].w;
        if (signedDistance < 0.0f)
        {
            p -= gPlaneData[i].xyz * signedDistance;
        }
    }

    [loop]
    for (uint i = 0u; i < gAabbColliderCount; ++i)
    {
        p = N01ResolveAabb(
            p,
            gAabbData[i * 2u + 0u].xyz,
            gAabbData[i * 2u + 1u].xyz);
    }

    p.y = max(p.y, gFloorY);
    residual = length(p - original);
    return p;
}

void N01BuildFrame(
    float3 a,
    float3 b,
    float3 c,
    out float3 tangent,
    out float3 bitangent,
    out float3 normal)
{
    tangent = N01SafeNormalize(b - a, float3(1.0f, 0.0f, 0.0f));
    normal = N01SafeNormalize(
        cross(b - a, c - a),
        float3(0.0f, 1.0f, 0.0f));
    bitangent = N01SafeNormalize(
        cross(normal, tangent),
        float3(0.0f, 0.0f, 1.0f));
    tangent = N01SafeNormalize(cross(bitangent, normal), tangent);
}

float3 N01ToFrameLocal(
    float3 offset,
    float3 tangent,
    float3 bitangent,
    float3 normal)
{
    return float3(
        dot(offset, tangent),
        dot(offset, bitangent),
        dot(offset, normal));
}

float3 N01FromFrameLocal(
    float3 localValue,
    float3 tangent,
    float3 bitangent,
    float3 normal)
{
    return
        tangent * localValue.x +
        bitangent * localValue.y +
        normal * localValue.z;
}

#if N01_BINDING_MODE == 0

StructuredBuffer<float4> N01CurrentPositions : register(t0);
StructuredBuffer<float4> N01PreviousPositions : register(t1);
StructuredBuffer<DynamicPayload> N01OldPayloads : register(t2);
StructuredBuffer<uint4> N01CandidatePrefix : register(t8);
StructuredBuffer<uint4> N01CandidateBlockOffsets : register(t9);

RWStructuredBuffer<SegmentGroupCore> N01Cores : register(u0);
RWStructuredBuffer<CoreExtension> N01Extensions : register(u1);
RWStructuredBuffer<StaticCache> N01Caches : register(u2);
RWStructuredBuffer<ReuseDescriptor> N01Descriptors : register(u3);
RWStructuredBuffer<BlockDescriptor> N01Blocks : register(u4);
RWStructuredBuffer<uint4> N01Flags : register(u5);
RWStructuredBuffer<uint> N01Counters : register(u6);
RWStructuredBuffer<SharedPointCandidate> N01ScratchCandidates : register(u7);

void N01CounterAdd(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01Counters[index], value, ignored);
}

[numthreads(64, 1, 1)]
void N01InitializeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = (CoreExtension)0;
    extension.payloadIndex = N01_INVALID;
    extension.forceVersion = gForceVersion;
    extension.colliderVersion = gColliderVersion;
    extension.topologyVersion = gTopologyVersion;
    extension.editVersion = gEditVersion;
    extension.dependencyVersion = gDependencyVersion;
    extension.lastSolvedFrame = gFrameIndex;
    extension.rootTransformVersion = gRootTransformVersion;
    extension.forceVersion = gForceVersion;
    extension.colliderVersion = gColliderVersion;
    extension.topologyVersion = gTopologyVersion;
    extension.editVersion = gEditVersion;
    extension.dependencyVersion = gDependencyVersion;
    extension.minimumObservedCollisionMarginMm = 1.0e9f;

    StaticCache cache = (StaticCache)0;
    cache.restLength = core.restLength;
    cache.localDirection = core.restDirection;
    cache.valid = 0u;

    ReuseDescriptor descriptor = (ReuseDescriptor)0;
    descriptor.collisionMarginMm = 1.0e9f;
    descriptor.candidateState = N01_STATE_DYNAMIC;

    core.state = N01_STATE_DYNAMIC;
    core.previousState = N01_STATE_DYNAMIC;
    core.dirtyFlags = N01_DIRTY_TOPOLOGY | N01_DIRTY_AGE;
    core.flags &= ~(N01_CORE_CACHE_VALID | N01_CORE_PAYLOAD_VALID);

    N01Cores[groupIndex] = core;
    N01Extensions[groupIndex] = extension;
    N01Caches[groupIndex] = cache;
    N01Descriptors[groupIndex] = descriptor;
    N01Flags[groupIndex] = uint4(1u, 0u, 0u, 0u);
}

[numthreads(64, 1, 1)]
void N01BuildBlocksCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint blockIndex = dispatchThreadId.x;
    if (blockIndex >= gBlockCount)
    {
        return;
    }

    BlockDescriptor block = N01Blocks[blockIndex];
    if (N01FeatureEnabled(N01_FEATURE_INCREMENTAL_BLOCKS) &&
        block.validFlags != 0u && block.dirtyFlags == 0u)
    {
        N01CounterAdd(N01_COUNTER_BLOCK_SKIPPED, 1u);
        return;
    }

    float3 boundsMin = float3(1.0e9f, 1.0e9f, 1.0e9f);
    float3 boundsMax = float3(-1.0e9f, -1.0e9f, -1.0e9f);
    float maximumMotionMm = 0.0f;
    float minimumCollisionMarginMm = 1.0e9f;
    float maximumResidual = 0.0f;
    float maximumBoundary = 0.0f;
    bool hasDynamic = false;
    bool hasRepair = false;

    [loop]
    for (uint local = 0u; local < block.groupCount; ++local)
    {
        const uint groupIndex = block.firstGroupIndex + local;
        if (groupIndex >= gGroupCount)
        {
            break;
        }

        const SegmentGroupCore core = N01Cores[groupIndex];
        hasDynamic = hasDynamic ||
            core.state == N01_STATE_DYNAMIC ||
            core.state == N01_STATE_FORCED_DYNAMIC ||
            core.state == N01_STATE_DEFERRED;
        hasRepair = hasRepair ||
            core.state == N01_STATE_REPAIR ||
            core.state == N01_STATE_INVALID;
        const float3 start = N01CurrentPositions[core.startPointIndex].xyz;
        const float3 end = N01CurrentPositions[core.endPointIndex].xyz;
        const float3 previousEnd =
            N01PreviousPositions[core.endPointIndex].xyz;

        boundsMin = min(boundsMin, min(start, end));
        boundsMax = max(boundsMax, max(start, end));
        maximumMotionMm = max(
            maximumMotionMm,
            length(end - previousEnd) * 1000.0f);
        minimumCollisionMarginMm = min(
            minimumCollisionMarginMm,
            min(N01CollisionMarginMm(start), N01CollisionMarginMm(end)));

        const ReuseDescriptor descriptor = N01Descriptors[groupIndex];
        maximumResidual = max(
            maximumResidual,
            descriptor.constraintResidual);
        maximumBoundary = max(
            maximumBoundary,
            descriptor.boundaryErrorMm);
    }

    block.boundsMin = boundsMin;
    block.boundsMax = boundsMax;
    block.maximumMotionMm = maximumMotionMm;
    block.minimumCollisionMarginMm = minimumCollisionMarginMm;
    block.maximumResidual = maximumResidual;
    block.maximumBoundaryErrorMm = maximumBoundary;
    block.descriptorVersion = gTopologyVersion;
    block.lastUpdatedFrame = gFrameIndex;
    block.validFlags =
        1u |
        (hasDynamic ? 2u : 0u) |
        (hasRepair ? 4u : 0u);
    N01Blocks[blockIndex] = block;
}

[numthreads(64, 1, 1)]
void N01InvalidateBlocksCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint blockIndex = dispatchThreadId.x;
    if (blockIndex >= gBlockCount)
    {
        return;
    }

    BlockDescriptor block = N01Blocks[blockIndex];
    uint dirty = 0u;

    if ((block.validFlags & (2u | 4u)) != 0u)
    {
        dirty |= N01_DIRTY_AGE;
    }

    if ((gRuntimeFlags & (1u << 0u)) != 0u)
    {
        const bool sweptRootValid =
            all(gSweptRootBoundsMax >= gSweptRootBoundsMin);
        if (!N01FeatureEnabled(N01_FEATURE_SWEPT_SPATIAL) ||
            !sweptRootValid ||
            N01AabbIntersects(
                block.boundsMin,
                block.boundsMax,
                gSweptRootBoundsMin,
                gSweptRootBoundsMax))
        {
            dirty |= N01_DIRTY_ROOT;
        }
    }
    if ((gRuntimeFlags & (1u << 1u)) != 0u) dirty |= N01_DIRTY_FORCE;
    if ((gRuntimeFlags & (1u << 3u)) != 0u) dirty |= N01_DIRTY_TOPOLOGY;
    if ((gRuntimeFlags & (1u << 4u)) != 0u) dirty |= N01_DIRTY_EDIT;
    if ((gRuntimeFlags & (1u << 5u)) != 0u) dirty |= N01_DIRTY_DEPENDENCY;
    if ((gRuntimeFlags & (1u << 6u)) != 0u)
    {
        dirty |= N01_DIRTY_TOPOLOGY | N01_DIRTY_AGE;
    }

    if (block.descriptorVersion != gTopologyVersion)
    {
        dirty |= N01_DIRTY_TOPOLOGY;
    }

    if (gEditVersion != 0u &&
        (block.dirtyFlags & N01_DIRTY_EDIT) != 0u)
    {
        dirty |= N01_DIRTY_EDIT;
    }

    const bool sweptColliderValid =
        all(gSweptColliderBoundsMax >= gSweptColliderBoundsMin);
    if ((gRuntimeFlags & (1u << 2u)) != 0u)
    {
        if (!sweptColliderValid ||
            N01AabbIntersects(
                block.boundsMin,
                block.boundsMax,
                gSweptColliderBoundsMin,
                gSweptColliderBoundsMax))
        {
            dirty |= N01_DIRTY_COLLIDER;
        }
    }

    if (block.lastUpdatedFrame + gDescriptorMaximumAge < gFrameIndex)
    {
        dirty |= N01_DIRTY_AGE;
    }

    block.dirtyFlags |= dirty;
    N01Blocks[blockIndex] = block;

    if (dirty != 0u)
    {
        N01CounterAdd(N01_COUNTER_DIRTY_BLOCKS, 1u);
    }
}

[numthreads(64, 1, 1)]
void N01DescribeClassifyCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    StaticCache cache = N01Caches[groupIndex];
    ReuseDescriptor descriptor = N01Descriptors[groupIndex];
    const BlockDescriptor block = N01Blocks[core.blockIndex];

    if (N01FeatureEnabled(N01_FEATURE_INCREMENTAL_BLOCKS) &&
        block.validFlags != 0u && block.dirtyFlags == 0u)
    {
        uint4 cachedFlags = uint4(0u, 0u, 0u, 0u);
        if (core.state == N01_STATE_REPAIR ||
            core.state == N01_STATE_INVALID)
        {
            cachedFlags.z = 1u;
            cachedFlags.w = core.state == N01_STATE_INVALID ? 1u : 0u;
        }
        else if (core.state <= N01_STATE_STATIC_TRANSFORM)
        {
            cachedFlags.y = 1u;
            N01CounterAdd(N01_COUNTER_CACHE_HITS, 1u);
        }
        else
        {
            cachedFlags.x = 1u;
        }
        N01Flags[groupIndex] = cachedFlags;
        return;
    }

    const float3 start = N01CurrentPositions[core.startPointIndex].xyz;
    const float3 previousStart =
        N01PreviousPositions[core.startPointIndex].xyz;
    const float3 end = N01CurrentPositions[core.endPointIndex].xyz;
    const float3 previousEnd =
        N01PreviousPositions[core.endPointIndex].xyz;
    const float3 segment = end - start;
    const float currentLength = N01SafeLength(segment);
    const float lengthErrorRatio =
        abs(currentLength - core.restLength) /
        max(core.restLength, 1.0e-6f);
    const float positionErrorMm =
        length(end - previousEnd) * 1000.0f;
    const float velocityDisplacementMm = positionErrorMm;
    float accelerationDisplacementMm = 0.0f;
    const float rootTranslationDisplacementMm =
        length(start - previousStart) * 1000.0f;
    const float3 currentDirection =
        N01SafeNormalize(segment, core.restDirection);
    const float3 previousDirection =
        N01SafeNormalize(previousEnd - previousStart, core.restDirection);
    const float rootRotationRadians = acos(clamp(
        dot(currentDirection, previousDirection), -1.0f, 1.0f));
    const float rootRotationDisplacementMm =
        2.0f * max(core.restLength, 0.0f) *
        sin(rootRotationRadians * 0.5f) * 1000.0f;
    const float forceDisplacementMm =
        0.5f * abs(gWindStrength) * gDeltaTime * gDeltaTime * 1000.0f;

    const bool payloadIndexValid =
        extension.payloadIndex != N01_INVALID &&
        extension.payloadIndex < gPayloadCapacity;

    if (payloadIndexValid)
    {
        const DynamicPayload payload =
            N01OldPayloads[extension.payloadIndex];
        const float3 currentVelocity =
            (end - previousEnd) / max(gDeltaTime, 1.0e-6f);
        accelerationDisplacementMm =
            0.5f *
            length(currentVelocity - payload.velocity) *
            gDeltaTime * gDeltaTime *
            1000.0f;
    }

    const float collisionMarginMm =
        min(N01CollisionMarginMm(start), N01CollisionMarginMm(end));
    const float colliderDisplacementMm =
        (extension.colliderVersion != gColliderVersion)
        ? max(gCollisionSafetyPaddingMm - collisionMarginMm, 0.0f)
        : 0.0f;
    const uint ageFrames =
        gFrameIndex >= extension.lastValidatedFrame
        ? gFrameIndex - extension.lastValidatedFrame
        : gDescriptorMaximumAge + 1u;

    uint hardFlags = 0u;
    const bool finite =
        N01Finite3(start) &&
        N01Finite3(end) &&
        N01Finite3(previousEnd);

    if (!finite)
    {
        hardFlags |= N01_DIRTY_NONFINITE;
    }
    if (lengthErrorRatio > gLengthErrorThresholdRatio)
    {
        hardFlags |= N01_DIRTY_BOUNDARY;
    }
    if (descriptor.boundaryErrorMm > gBoundaryErrorThresholdMm)
    {
        hardFlags |= N01_DIRTY_BOUNDARY;
    }
    if (N01FeatureEnabled(N01_FEATURE_VERSION_CHECK) &&
        extension.topologyVersion != gTopologyVersion)
    {
        hardFlags |= N01_DIRTY_TOPOLOGY;
    }
    if (N01FeatureEnabled(N01_FEATURE_VERSION_CHECK) &&
        extension.editVersion != gEditVersion)
    {
        hardFlags |= N01_DIRTY_EDIT;
    }
    if (N01FeatureEnabled(N01_FEATURE_VERSION_CHECK) &&
        extension.dependencyVersion != gDependencyVersion)
    {
        hardFlags |= N01_DIRTY_DEPENDENCY;
    }
    if (N01FeatureEnabled(N01_FEATURE_VERSION_CHECK) &&
        extension.forceVersion != gForceVersion)
    {
        hardFlags |= N01_DIRTY_FORCE;
    }
    if (N01FeatureEnabled(N01_FEATURE_VERSION_CHECK) &&
        extension.colliderVersion != gColliderVersion &&
        collisionMarginMm <=
            gCollisionSafetyPaddingMm +
            velocityDisplacementMm +
            accelerationDisplacementMm)
    {
        hardFlags |= N01_DIRTY_COLLIDER;
    }
    if (ageFrames > gDescriptorMaximumAge)
    {
        hardFlags |= N01_DIRTY_AGE;
    }
    if (core.state == N01_STATE_DYNAMIC &&
        (core.flags & N01_CORE_PAYLOAD_VALID) != 0u &&
        !payloadIndexValid)
    {
        hardFlags |= N01_DIRTY_PAYLOAD;
    }

    uint candidateState = N01_STATE_DYNAMIC;
    uint softFlags = 0u;

    const bool repairRequired =
        (hardFlags &
            (N01_DIRTY_NONFINITE |
             N01_DIRTY_BOUNDARY |
             N01_DIRTY_PAYLOAD)) != 0u;

    const bool immediateDynamic =
        (hardFlags &
            (N01_DIRTY_TOPOLOGY |
             N01_DIRTY_EDIT |
             N01_DIRTY_DEPENDENCY |
             N01_DIRTY_FORCE |
             N01_DIRTY_COLLIDER |
             N01_DIRTY_WATCHDOG)) != 0u;

    const bool cacheValid =
        N01FeatureEnabled(N01_FEATURE_STATIC_CACHE) &&
        cache.valid != 0u &&
        (core.flags & N01_CORE_CACHE_VALID) != 0u;

    const bool stablePreconditions =
        positionErrorMm <= gStaticPositionThresholdMm &&
        velocityDisplacementMm <= gStaticVelocityThresholdMm &&
        accelerationDisplacementMm <= gStaticVelocityThresholdMm &&
        rootTranslationDisplacementMm <= gStaticPositionThresholdMm &&
        rootRotationDisplacementMm <= gStaticPositionThresholdMm &&
        forceDisplacementMm <= gStaticVelocityThresholdMm &&
        colliderDisplacementMm <= gStaticPositionThresholdMm &&
        lengthErrorRatio <= gLengthErrorThresholdRatio &&
        descriptor.boundaryErrorMm <= gBoundaryErrorThresholdMm &&
        collisionMarginMm >
            gCollisionSafetyPaddingMm +
            velocityDisplacementMm +
            accelerationDisplacementMm &&
        block.maximumMotionMm <= gDynamicPositionThresholdMm &&
        cacheValid &&
        ageFrames <= gDescriptorMaximumAge;

    extension.stableCandidateFrames = stablePreconditions
        ? min(extension.stableCandidateFrames + 1u, 0xfffffffeu)
        : 0u;

    const bool stableEnough =
        stablePreconditions &&
        extension.stableCandidateFrames >= gConsecutiveStaticFrames &&
        (!N01FeatureEnabled(N01_FEATURE_MINIMUM_RESIDENCE) ||
         gFrameIndex >= extension.minimumResidenceEndFrame);

    const bool dynamicThresholdExceeded =
        positionErrorMm >= gDynamicPositionThresholdMm ||
        velocityDisplacementMm >= gDynamicVelocityThresholdMm ||
        rootTranslationDisplacementMm >= gDynamicPositionThresholdMm ||
        rootRotationDisplacementMm >= gDynamicPositionThresholdMm ||
        forceDisplacementMm >= gDynamicVelocityThresholdMm ||
        colliderDisplacementMm >= gDynamicPositionThresholdMm ||
        collisionMarginMm <= gCollisionSafetyPaddingMm;

    if ((gRuntimeFlags & (1u << 7u)) != 0u)
    {
        candidateState = N01_STATE_FORCED_DYNAMIC;
    }
    else if ((gRuntimeFlags & (1u << 8u)) != 0u &&
             cacheValid &&
             !repairRequired &&
             !immediateDynamic)
    {
        candidateState =
            extension.rootTransformVersion != gRootTransformVersion
            ? N01_STATE_STATIC_TRANSFORM
            : N01_STATE_STATIC_REUSE;
    }
    else if (repairRequired)
    {
        candidateState = N01_STATE_REPAIR;
    }
    else if (immediateDynamic || dynamicThresholdExceeded)
    {
        candidateState = N01_STATE_FORCED_DYNAMIC;
    }
    else if (stableEnough &&
             gFrameIndex >= extension.minimumResidenceEndFrame)
    {
        candidateState =
            extension.rootTransformVersion != gRootTransformVersion
            ? N01_STATE_STATIC_TRANSFORM
            : N01_STATE_STATIC_REUSE;
    }
    else
    {
        candidateState = N01_STATE_DYNAMIC;
        softFlags |= N01_DIRTY_AGE;
    }

    if (N01FeatureEnabled(N01_FEATURE_HYSTERESIS) &&
        core.state <= N01_STATE_STATIC_TRANSFORM &&
        (gRuntimeFlags & (1u << 7u)) == 0u &&
        !repairRequired &&
        !immediateDynamic &&
        !dynamicThresholdExceeded &&
        cacheValid)
    {
        candidateState =
            extension.rootTransformVersion != gRootTransformVersion
            ? N01_STATE_STATIC_TRANSFORM
            : core.state;
    }

    if (gExperimentVariant == 4u)
    {
        if (!finite)
        {
            candidateState = N01_STATE_REPAIR;
        }
        else
        {
            candidateState =
                velocityDisplacementMm <= gStaticVelocityThresholdMm
                ? N01_STATE_STATIC_REUSE
                : N01_STATE_DYNAMIC;
        }
    }

    descriptor.positionErrorMm = positionErrorMm;
    descriptor.velocityDisplacementMm = velocityDisplacementMm;
    descriptor.accelerationDisplacementMm =
        accelerationDisplacementMm;
    descriptor.constraintResidual = lengthErrorRatio;
    descriptor.rootTranslationDisplacementMm =
        rootTranslationDisplacementMm;
    descriptor.rootRotationDisplacementMm =
        rootRotationDisplacementMm;
    descriptor.forceDisplacementMm = forceDisplacementMm;
    descriptor.colliderDisplacementMm = colliderDisplacementMm;
    descriptor.collisionMarginMm = collisionMarginMm;
    descriptor.boundaryErrorMm =
        max(descriptor.boundaryErrorMm, cache.boundaryErrorMm);
    descriptor.lengthErrorRatio = lengthErrorRatio;
    descriptor.hardFlags = hardFlags;
    descriptor.softFlags = softFlags;
    descriptor.ageFrames = ageFrames;
    descriptor.candidateState = candidateState;
    descriptor.rootTransformVersion = gRootTransformVersion;
    descriptor.forceVersion = gForceVersion;
    descriptor.colliderVersion = gColliderVersion;
    descriptor.editVersion = gEditVersion;

    if (candidateState <= N01_STATE_STATIC_TRANSFORM &&
        hardFlags == 0u)
    {
        extension.lastValidatedFrame = gFrameIndex;
    }

    core.dirtyFlags = hardFlags | softFlags | block.dirtyFlags;
    N01Cores[groupIndex] = core;
    N01Extensions[groupIndex] = extension;
    N01Descriptors[groupIndex] = descriptor;

    uint4 flags = uint4(0u, 0u, 0u, 0u);
    if (candidateState == N01_STATE_REPAIR ||
        candidateState == N01_STATE_INVALID)
    {
        flags.z = 1u;
        flags.w = candidateState == N01_STATE_INVALID ? 1u : 0u;
        N01CounterAdd(N01_COUNTER_CANDIDATE_REPAIR, 1u);
    }
    else if (candidateState <= N01_STATE_STATIC_TRANSFORM)
    {
        flags.y = 1u;
        N01CounterAdd(N01_COUNTER_CANDIDATE_STATIC, 1u);
    }
    else
    {
        flags.x = 1u;
        N01CounterAdd(N01_COUNTER_CANDIDATE_DYNAMIC, 1u);
    }
    N01Flags[groupIndex] = flags;

    if (core.dirtyFlags != 0u)
    {
        N01CounterAdd(N01_COUNTER_DIRTY_GROUPS, 1u);
    }
    N01CounterAdd(N01_COUNTER_GROUP_READ_BYTES, 272u);
    N01CounterAdd(N01_COUNTER_GROUP_WRITE_BYTES, 208u);
    N01CounterAdd(N01_COUNTER_DESCRIPTOR_VALID, 1u);
}

[numthreads(64, 1, 1)]
void N01FinalizeBlocksCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint blockIndex = dispatchThreadId.x;
    if (blockIndex >= gBlockCount)
    {
        return;
    }

    BlockDescriptor block = N01Blocks[blockIndex];
    block.dirtyFlags = 0u;
    block.validFlags |= 1u;
    N01Blocks[blockIndex] = block;
}

[numthreads(64, 1, 1)]
void N01ApplyTransitionsFinalFlagsCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    StaticCache cache = N01Caches[groupIndex];
    const ReuseDescriptor descriptor = N01Descriptors[groupIndex];

    uint requestedState = descriptor.candidateState;
    const uint scanBlock = groupIndex / 256u;
    const uint staticRank =
        N01CandidateBlockOffsets[scanBlock].y +
        N01CandidatePrefix[groupIndex].y;

    if (N01FeatureEnabled(N01_FEATURE_TRANSITION_BUDGET) &&
        requestedState <= N01_STATE_STATIC_TRANSFORM &&
        staticRank >= gStaticTransitionBudget &&
        core.state > N01_STATE_STATIC_TRANSFORM)
    {
        requestedState = N01_STATE_DEFERRED;
    }

    uint newState = requestedState;
    if (requestedState == N01_STATE_DEFERRED)
    {
        newState = N01_STATE_DYNAMIC;
    }

    const uint oldState = core.state;
    const uint priorTransitionFrame = extension.lastTransitionFrame;
    if (newState != oldState)
    {
        core.previousState = oldState;
        core.state = newState;
        extension.lastTransitionFrame = gFrameIndex;
        extension.minimumResidenceEndFrame =
            N01FeatureEnabled(N01_FEATURE_MINIMUM_RESIDENCE)
            ? gFrameIndex + gMinimumResidenceFrames
            : gFrameIndex;
        extension.transitionCount += 1u;
        if (newState > N01_STATE_STATIC_TRANSFORM)
        {
            extension.stableCandidateFrames = 0u;
        }
        N01CounterAdd(N01_COUNTER_TRANSITIONS, 1u);
        N01CounterAdd(N01_COUNTER_TRANSITION_BYTES, 32u);

        if (priorTransitionFrame > 0u &&
            gFrameIndex >= priorTransitionFrame &&
            gFrameIndex - priorTransitionFrame <= 2u)
        {
            N01CounterAdd(N01_COUNTER_OSCILLATIONS, 1u);
        }
    }

    if (newState <= N01_STATE_STATIC_TRANSFORM)
    {
        extension.payloadIndex = N01_INVALID;
        extension.lastPayloadRetireFrame = gFrameIndex;
        core.flags &= ~N01_CORE_PAYLOAD_VALID;
        if (cache.valid != 0u)
        {
            core.flags |= N01_CORE_CACHE_VALID;
        }
    }
    else if (newState == N01_STATE_FORCED_DYNAMIC)
    {
        core.flags |= N01_CORE_FORCED_DYNAMIC;
        core.state = N01_STATE_DYNAMIC;
    }

    extension.rootTransformVersion = gRootTransformVersion;
    extension.forceVersion = gForceVersion;
    extension.colliderVersion = gColliderVersion;
    extension.topologyVersion = gTopologyVersion;
    extension.editVersion = gEditVersion;
    extension.dependencyVersion = gDependencyVersion;

    if (core.state == N01_STATE_STATIC_TRANSFORM)
    {
        N01CounterAdd(N01_COUNTER_STATIC_TRANSFORM, 1u);
    }
    N01Cores[groupIndex] = core;
    N01Extensions[groupIndex] = extension;

    uint4 finalFlags = uint4(0u, 0u, 0u, 0u);
    if (core.state == N01_STATE_REPAIR ||
        core.state == N01_STATE_INVALID)
    {
        finalFlags.z = 1u;
        finalFlags.w = core.state == N01_STATE_INVALID ? 1u : 0u;
    }
    else if (core.state <= N01_STATE_STATIC_TRANSFORM)
    {
        finalFlags.y = 1u;
    }
    else
    {
        finalFlags.x = 1u;
    }

    N01Flags[groupIndex] = finalFlags;
}

#endif // mode 0

#if N01_BINDING_MODE == 1

StructuredBuffer<uint4> N01FlagsIn : register(t0);
StructuredBuffer<uint4> N01PrefixIn : register(t1);
StructuredBuffer<uint4> N01BlockOffsetsIn : register(t2);

RWStructuredBuffer<uint4> N01PrefixOut : register(u0);
RWStructuredBuffer<uint4> N01BlockCounts : register(u1);
RWStructuredBuffer<uint4> N01BlockOffsets : register(u2);
RWStructuredBuffer<uint> N01Counters : register(u3);
RWStructuredBuffer<uint> N01DynamicList : register(u4);
RWStructuredBuffer<uint> N01StaticList : register(u5);
RWStructuredBuffer<uint> N01RepairList : register(u6);
RWStructuredBuffer<uint> N01InvalidList : register(u7);

groupshared uint4 N01ScanValues[256];

[numthreads(256, 1, 1)]
void N01ScanLocalCS(
    uint3 groupId : SV_GroupID,
    uint groupIndex : SV_GroupIndex,
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    uint4 value = uint4(0u, 0u, 0u, 0u);
    if (index < gGroupCount)
    {
        value = N01FlagsIn[index];
    }

    N01ScanValues[groupIndex] = value;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint offset = 1u; offset < 256u; offset <<= 1u)
    {
        uint4 addValue = uint4(0u, 0u, 0u, 0u);
        if (groupIndex >= offset)
        {
            addValue = N01ScanValues[groupIndex - offset];
        }
        GroupMemoryBarrierWithGroupSync();
        N01ScanValues[groupIndex] += addValue;
        GroupMemoryBarrierWithGroupSync();
    }

    if (index < gGroupCount)
    {
        N01PrefixOut[index] =
            N01ScanValues[groupIndex] - value;
    }

    if (groupIndex == 255u)
    {
        N01BlockCounts[groupId.x] = N01ScanValues[groupIndex];
    }
}

[numthreads(1, 1, 1)]
void N01ScanBlocksCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint4 running = uint4(0u, 0u, 0u, 0u);
    [loop]
    for (uint block = 0u; block < gScanBlockCount; ++block)
    {
        N01BlockOffsets[block] = running;
        running += N01BlockCounts[block];
    }

    if (gPassKind == 0u)
    {
        N01Counters[N01_COUNTER_CANDIDATE_DYNAMIC] = running.x;
        N01Counters[N01_COUNTER_CANDIDATE_STATIC] = running.y;
        N01Counters[N01_COUNTER_CANDIDATE_REPAIR] = running.z;
    }
    else if (gPassKind == 2u)
    {
        N01Counters[N01_COUNTER_DIRTY_FOLLOWERS] = running.x;
        N01Counters[N01_COUNTER_DIRTY_RENDER_POINTS] =
            running.x * gParticlesPerStrand;
    }
    else
    {
        N01Counters[N01_COUNTER_DYNAMIC] = running.x;
        N01Counters[N01_COUNTER_STATIC_REUSE] = running.y;
        N01Counters[N01_COUNTER_REPAIR] = running.z;
        N01Counters[N01_COUNTER_INVALID] = running.w;
        N01Counters[N01_COUNTER_PAYLOAD_ACTIVE] =
            min(running.x, gPayloadCapacity);
        N01Counters[N01_COUNTER_PAYLOAD_OVERFLOW] =
            running.x > gPayloadCapacity
            ? running.x - gPayloadCapacity
            : 0u;
        N01Counters[N01_COUNTER_FULL_DYNAMIC] =
            running.x > gPayloadCapacity ? 1u : 0u;
    }

    N01Counters[N01_COUNTER_SCAN_BLOCKS] = gScanBlockCount;
}

[numthreads(256, 1, 1)]
void N01ScatterListsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= gGroupCount)
    {
        return;
    }

    const uint4 flags = N01FlagsIn[index];
    const uint4 prefix = N01PrefixIn[index];
    const uint4 blockOffset =
        N01BlockOffsetsIn[index / 256u];
    const uint4 outputIndex = prefix + blockOffset;

    if (flags.x != 0u)
    {
        N01DynamicList[outputIndex.x] = index;
    }
    if (flags.y != 0u)
    {
        N01StaticList[outputIndex.y] = index;
    }
    if (flags.z != 0u)
    {
        N01RepairList[outputIndex.z] = index;
    }
    if (flags.w != 0u)
    {
        N01InvalidList[outputIndex.w] = index;
    }
}

[numthreads(64, 1, 1)]
void N01BuildSleepingIdentityListCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    const uint4 flags = N01FlagsIn[groupIndex];
    N01DynamicList[groupIndex] = groupIndex;
    if (groupIndex == 0u)
    {
        N01Counters[N01_COUNTER_DYNAMIC] = gGroupCount;
        N01Counters[N01_COUNTER_PAYLOAD_ACTIVE] = gGroupCount;
    }

    if (flags.y != 0u)
    {
        uint slot;
        InterlockedAdd(N01Counters[N01_COUNTER_STATIC_REUSE], 1u, slot);
        N01StaticList[slot] = groupIndex;
    }
    if (flags.z != 0u)
    {
        uint slot;
        InterlockedAdd(N01Counters[N01_COUNTER_REPAIR], 1u, slot);
        N01RepairList[slot] = groupIndex;
    }
    if (flags.w != 0u)
    {
        uint slot;
        InterlockedAdd(N01Counters[N01_COUNTER_INVALID], 1u, slot);
        N01InvalidList[slot] = groupIndex;
    }
}

#endif // mode 1

#if N01_BINDING_MODE == 2

StructuredBuffer<uint> N01DynamicListIn : register(t0);
StructuredBuffer<uint> N01StaticListIn : register(t1);
StructuredBuffer<uint> N01RepairListIn : register(t2);
StructuredBuffer<float4> N01RestPositions : register(t4);
StructuredBuffer<float4> N01RestScalp : register(t5);
StructuredBuffer<float4> N01CurrentScalp : register(t6);
StructuredBuffer<float4> N01PreviousScalp : register(t7);
StructuredBuffer<TriangleData> N01ScalpTriangles : register(t8);
StructuredBuffer<RootBindingData> N01RootBindings : register(t9);
StructuredBuffer<DynamicPayload> N01OldPayloads : register(t10);

RWStructuredBuffer<SegmentGroupCore> N01Cores : register(u0);
RWStructuredBuffer<CoreExtension> N01Extensions : register(u1);
RWStructuredBuffer<StaticCache> N01Caches : register(u2);
RWStructuredBuffer<ReuseDescriptor> N01Descriptors : register(u3);
RWStructuredBuffer<DynamicPayload> N01NewPayloads : register(u4);
RWStructuredBuffer<float4> N01Positions : register(u5);
RWStructuredBuffer<float4> N01PreviousPositions : register(u6);
RWStructuredBuffer<uint> N01Counters : register(u7);

void N01CounterAdd(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01Counters[index], value, ignored);
}

void N01GetRootFrame(
    uint strandIndex,
    bool previousFrame,
    out float3 root,
    out float3 tangent,
    out float3 bitangent,
    out float3 normal)
{
    const RootBindingData binding = N01RootBindings[strandIndex];
    const TriangleData triIndices =
        N01ScalpTriangles[binding.triangleIndex];

    const float3 a =
        previousFrame
        ? N01PreviousScalp[triIndices.a].xyz
        : N01CurrentScalp[triIndices.a].xyz;
    const float3 b =
        previousFrame
        ? N01PreviousScalp[triIndices.b].xyz
        : N01CurrentScalp[triIndices.b].xyz;
    const float3 c =
        previousFrame
        ? N01PreviousScalp[triIndices.c].xyz
        : N01CurrentScalp[triIndices.c].xyz;

    root =
        a * binding.barycentric.x +
        b * binding.barycentric.y +
        c * binding.barycentric.z;
    N01BuildFrame(a, b, c, tangent, bitangent, normal);
}

[numthreads(64, 1, 1)]
void N01CarryNonOptionalPayloadsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount ||
        N01FeatureEnabled(N01_FEATURE_OPTIONAL_PAYLOAD) ||
        groupIndex >= gPayloadCapacity)
    {
        return;
    }

    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    DynamicPayload payload = (DynamicPayload)0;
    const bool oldPayloadValid =
        extension.payloadIndex != N01_INVALID &&
        extension.payloadIndex < gPayloadCapacity;
    if (oldPayloadValid)
    {
        payload = N01OldPayloads[extension.payloadIndex];
        N01CounterAdd(N01_COUNTER_PAYLOAD_READ_BYTES, 64u);
    }
    else
    {
        const float3 currentEnd = N01Positions[core.endPointIndex].xyz;
        const float3 previousEnd = N01PreviousPositions[core.endPointIndex].xyz;
        payload.previousEndPosition = N01Finite3(previousEnd) ? previousEnd : currentEnd;
        payload.velocity = (currentEnd - payload.previousEndPosition) /
            max(gDeltaTime, 1.0e-6f);
        payload.allocationEpoch = gFrameIndex;
        extension.payloadVersion += 1u;
    }

    N01NewPayloads[groupIndex] = payload;
    extension.payloadIndex = groupIndex;
    extension.payloadGeneration = gPayloadGeneration;
    core.flags |= N01_CORE_PAYLOAD_VALID;
    N01Cores[groupIndex] = core;
    N01Extensions[groupIndex] = extension;
    N01CounterAdd(N01_COUNTER_PAYLOAD_WRITE_BYTES, 64u);
}

[numthreads(64, 1, 1)]
void N01RemapPayloadCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount =
        N01Counters[N01_COUNTER_DYNAMIC];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupIndex = N01DynamicListIn[compactedIndex];
    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    const uint targetPayloadIndex =
        N01FeatureEnabled(N01_FEATURE_OPTIONAL_PAYLOAD)
        ? compactedIndex
        : groupIndex;

    if (targetPayloadIndex >= gPayloadCapacity)
    {
        extension.payloadIndex = N01_INVALID;
        core.flags &=
            ~N01_CORE_PAYLOAD_VALID;
        core.flags |= N01_CORE_BASELINE_FALLBACK;
        N01Cores[groupIndex] = core;
        N01Extensions[groupIndex] = extension;
        N01CounterAdd(N01_COUNTER_FALLBACK, 1u);
        return;
    }

    DynamicPayload payload = (DynamicPayload)0;
    const bool oldPayloadValid =
        extension.payloadIndex != N01_INVALID &&
        extension.payloadIndex < gPayloadCapacity &&
        (core.flags & N01_CORE_PAYLOAD_VALID) != 0u;

    const float3 currentEnd =
        N01Positions[core.endPointIndex].xyz;
    const float3 previousEnd =
        N01PreviousPositions[core.endPointIndex].xyz;

    if (oldPayloadValid)
    {
        payload = N01OldPayloads[extension.payloadIndex];
        N01CounterAdd(N01_COUNTER_PAYLOAD_READ_BYTES, 64u);
    }
    else
    {
        payload.previousEndPosition =
            N01Finite3(previousEnd) ? previousEnd : currentEnd;
        payload.velocity =
            (currentEnd - payload.previousEndPosition) /
            max(gDeltaTime, 1.0e-6f);
        payload.distanceLambda = 0.0f;
        payload.bendLambda = 0.0f;
        payload.allocationEpoch = gFrameIndex;
        extension.payloadVersion += 1u;
    }

    payload.retireEpoch = 0u;
    N01NewPayloads[targetPayloadIndex] = payload;
    N01CounterAdd(N01_COUNTER_PAYLOAD_WRITE_BYTES, 64u);
    extension.payloadIndex = targetPayloadIndex;
    extension.payloadGeneration = gPayloadGeneration;
    core.flags |= N01_CORE_PAYLOAD_VALID;
    core.flags &= ~N01_CORE_BASELINE_FALLBACK;
    N01Cores[groupIndex] = core;
    N01Extensions[groupIndex] = extension;
}

[numthreads(64, 1, 1)]
void N01ApplyStaticCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint staticCount =
        N01Counters[N01_COUNTER_STATIC_REUSE];
    if (compactedIndex >= staticCount)
    {
        return;
    }

    const uint groupIndex = N01StaticListIn[compactedIndex];
    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    const StaticCache cache = N01Caches[groupIndex];

    if (cache.valid == 0u ||
        (core.flags & N01_CORE_CACHE_VALID) == 0u)
    {
        core.previousState = core.state;
        core.state = N01_STATE_REPAIR;
        core.dirtyFlags |= N01_DIRTY_PAYLOAD;
        N01Cores[groupIndex] = core;
        N01CounterAdd(N01_COUNTER_CACHE_MISSES, 1u);
        return;
    }

    float3 currentRoot;
    float3 currentTangent;
    float3 currentBitangent;
    float3 currentNormal;
    N01GetRootFrame(
        core.strandId,
        false,
        currentRoot,
        currentTangent,
        currentBitangent,
        currentNormal);

    float3 previousRoot;
    float3 previousTangent;
    float3 previousBitangent;
    float3 previousNormal;
    N01GetRootFrame(
        core.strandId,
        true,
        previousRoot,
        previousTangent,
        previousBitangent,
        previousNormal);

    const float3 currentEnd =
        currentRoot +
        N01FromFrameLocal(
            cache.rootLocalEnd,
            currentTangent,
            currentBitangent,
            currentNormal);

    const float3 previousEnd =
        previousRoot +
        N01FromFrameLocal(
            cache.previousRootLocalEnd,
            previousTangent,
            previousBitangent,
            previousNormal);

    if (!N01Finite3(currentEnd) ||
        !N01Finite3(previousEnd))
    {
        core.previousState = core.state;
        core.state = N01_STATE_REPAIR;
        core.dirtyFlags |= N01_DIRTY_NONFINITE;
        N01Cores[groupIndex] = core;
        N01CounterAdd(N01_COUNTER_CACHE_MISSES, 1u);
        return;
    }

    N01Positions[core.endPointIndex] =
        float4(currentEnd, 1.0f);
    N01PreviousPositions[core.endPointIndex] =
        float4(previousEnd, 1.0f);

    core.state =
        extension.rootTransformVersion != gRootTransformVersion
        ? N01_STATE_STATIC_TRANSFORM
        : N01_STATE_STATIC_REUSE;
    core.dirtyFlags = 0u;
    extension.lastValidatedFrame = gFrameIndex;
    extension.rootTransformVersion = gRootTransformVersion;
    N01Cores[groupIndex] = core;
    N01Extensions[groupIndex] = extension;
    N01CounterAdd(N01_COUNTER_CACHE_HITS, 1u);
}

[numthreads(64, 1, 1)]
void N01RepairCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint repairCount =
        N01Counters[N01_COUNTER_REPAIR];
    if (compactedIndex >= repairCount)
    {
        return;
    }

    const uint groupIndex = N01RepairListIn[compactedIndex];
    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    const StaticCache cache = N01Caches[groupIndex];

    const float3 start =
        N01Positions[core.startPointIndex].xyz;
    const float3 oldEnd =
        N01Positions[core.endPointIndex].xyz;
    const float3 fallbackDirection =
        cache.valid != 0u
        ? cache.localDirection
        : core.restDirection;
    const float3 direction =
        N01SafeNormalize(oldEnd - start, fallbackDirection);

    float collisionResidual = 0.0f;
    float3 repaired =
        start + direction * max(core.restLength, 1.0e-6f);
    repaired =
        N01ResolveExternalCollision(
            repaired,
            collisionResidual);

    if (N01Finite3(start) && N01Finite3(repaired))
    {
        N01Positions[core.endPointIndex] =
            float4(repaired, 1.0f);
        N01PreviousPositions[core.endPointIndex] =
            float4(repaired, 1.0f);
        core.previousState = core.state;
        core.state = N01_STATE_DYNAMIC;
        core.flags |= N01_CORE_FORCED_DYNAMIC;
        core.dirtyFlags = N01_DIRTY_BOUNDARY;
        extension.minimumResidenceEndFrame =
            gFrameIndex + gMinimumResidenceFrames;
        extension.lastTransitionFrame = gFrameIndex;
        N01CounterAdd(N01_COUNTER_REPAIR_SUCCESS, 1u);
    }
    else
    {
        core.previousState = core.state;
        core.state = N01_STATE_INVALID;
        core.flags |= N01_CORE_BASELINE_FALLBACK;
        N01CounterAdd(N01_COUNTER_REPAIR_FAILURE, 1u);
        N01CounterAdd(N01_COUNTER_FALLBACK, 1u);
    }

    N01Cores[groupIndex] = core;
    N01Extensions[groupIndex] = extension;
}

[numthreads(64, 1, 1)]
void N01UpdateCacheCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    StaticCache cache = N01Caches[groupIndex];
    ReuseDescriptor descriptor = N01Descriptors[groupIndex];

    if ((core.state == N01_STATE_STATIC_REUSE ||
         core.state == N01_STATE_STATIC_TRANSFORM) &&
        cache.valid != 0u &&
        core.dirtyFlags == 0u)
    {
        return;
    }

    if (core.state == N01_STATE_INVALID)
    {
        cache.valid = 0u;
        core.flags &= ~N01_CORE_CACHE_VALID;
        N01Caches[groupIndex] = cache;
        N01Cores[groupIndex] = core;
        return;
    }

    const float3 start =
        N01Positions[core.startPointIndex].xyz;
    const float3 end =
        N01Positions[core.endPointIndex].xyz;
    if (!N01Finite3(start) || !N01Finite3(end))
    {
        cache.valid = 0u;
        core.flags &= ~N01_CORE_CACHE_VALID;
        core.state = N01_STATE_REPAIR;
        core.dirtyFlags |= N01_DIRTY_NONFINITE;
        N01Caches[groupIndex] = cache;
        N01Cores[groupIndex] = core;
        return;
    }

    float3 root;
    float3 tangent;
    float3 bitangent;
    float3 normal;
    N01GetRootFrame(
        core.strandId,
        false,
        root,
        tangent,
        bitangent,
        normal);

    const float3 localEnd =
        N01ToFrameLocal(
            end - root,
            tangent,
            bitangent,
            normal);
    const float3 localDirection =
        N01SafeNormalize(end - start, core.restDirection);

    cache.previousRootLocalEnd =
        cache.valid != 0u
        ? cache.rootLocalEnd
        : localEnd;
    cache.rootLocalEnd = localEnd;
    cache.localDirection = localDirection;
    cache.restLength = core.restLength;
    cache.boundaryErrorMm = descriptor.boundaryErrorMm;
    cache.maximumErrorMm = max(
        cache.maximumErrorMm,
        descriptor.positionErrorMm);
    cache.sourceFrame = gFrameIndex;
    cache.rootTransformVersion = gRootTransformVersion;
    cache.valid = 1u;

    core.flags |= N01_CORE_CACHE_VALID;
    extension.lastSolvedFrame = gFrameIndex;
    extension.lastValidatedFrame = gFrameIndex;
    extension.maximumObservedPositionErrorMm = max(
        extension.maximumObservedPositionErrorMm,
        descriptor.positionErrorMm);
    extension.maximumObservedBoundaryErrorMm = max(
        extension.maximumObservedBoundaryErrorMm,
        descriptor.boundaryErrorMm);
    extension.minimumObservedCollisionMarginMm = min(
        extension.minimumObservedCollisionMarginMm,
        descriptor.collisionMarginMm);

    N01Caches[groupIndex] = cache;
    N01Cores[groupIndex] = core;
    N01Extensions[groupIndex] = extension;
}

[numthreads(64, 1, 1)]
void N01ShadowValidateCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint staticCount =
        N01Counters[N01_COUNTER_STATIC_REUSE];
    if (compactedIndex >= staticCount)
    {
        return;
    }

    const uint groupIndex = N01StaticListIn[compactedIndex];
    if ((groupIndex & 63u) != (gFrameIndex & 63u))
    {
        return;
    }

    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    ReuseDescriptor descriptor = N01Descriptors[groupIndex];

    const float3 current =
        N01Positions[core.endPointIndex].xyz;
    const float3 previous =
        N01PreviousPositions[core.endPointIndex].xyz;
    const float3 velocity =
        (current - previous) /
        max(gDeltaTime, 1.0e-6f);
    const float phase =
        float((groupIndex * 1664525u + 1013904223u) & 1023u) /
        1023.0f;
    const float3 acceleration =
        float3(0.0f, -gGravity, 0.0f) +
        N01SafeNormalize(
            gWindDirection,
            float3(1.0f, 0.0f, 0.0f)) *
        gWindStrength *
        (0.5f + phase);

    float3 predicted =
        current +
        velocity * gDeltaTime +
        acceleration * gDeltaTime * gDeltaTime;
    const float3 start =
        N01Positions[core.startPointIndex].xyz;
    predicted =
        start +
        N01SafeNormalize(
            predicted - start,
            core.restDirection) *
        max(core.restLength, 1.0e-6f);
    float shadowCollisionResidual = 0.0f;
    predicted =
        N01ResolveExternalCollision(
            predicted,
            shadowCollisionResidual);
    const float shadowErrorMm =
        length(predicted - current) * 1000.0f;

    descriptor.shadowErrorMm = shadowErrorMm;
    extension.predictedPositionErrorMm = shadowErrorMm;
    core.flags |= N01_CORE_SHADOW_SAMPLE;
    N01CounterAdd(N01_COUNTER_SHADOW_SAMPLES, 1u);

    if (shadowErrorMm > gShadowErrorThresholdMm)
    {
        core.previousState = core.state;
        core.state = N01_STATE_DYNAMIC;
        core.flags |= N01_CORE_FORCED_DYNAMIC;
        core.dirtyFlags |= N01_DIRTY_WATCHDOG;
        extension.minimumResidenceEndFrame =
            gFrameIndex + gMinimumResidenceFrames;
        N01CounterAdd(N01_COUNTER_FALSE_STATIC, 1u);
    }

    N01Descriptors[groupIndex] = descriptor;
    N01Extensions[groupIndex] = extension;
    N01Cores[groupIndex] = core;
}

#endif // mode 2

#if N01_BINDING_MODE == 3

StructuredBuffer<uint> N01DynamicListIn : register(t0);
StructuredBuffer<GuideDirtyRange> N01GuideDirtyRangesIn : register(t1);
StructuredBuffer<float4> N01RestPositions : register(t2);
StructuredBuffer<float> N01SdfValues : register(t3);

RWStructuredBuffer<float4> N01Positions : register(u0);
RWStructuredBuffer<float4> N01PreviousPositions : register(u1);
RWStructuredBuffer<SegmentGroupCore> N01Cores : register(u2);
RWStructuredBuffer<CoreExtension> N01Extensions : register(u3);
RWStructuredBuffer<DynamicPayload> N01Payloads : register(u4);
RWStructuredBuffer<uint> N01HashHeads : register(u5);
RWStructuredBuffer<uint> N01ParticleNext : register(u6);
RWStructuredBuffer<uint> N01Counters : register(u7);

void N01CounterAdd(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01Counters[index], value, ignored);
}

uint N01Hash(float3 p)
{
    const int3 cell = int3(floor(p / max(gGridCellSize, 1.0e-5f)));
    const uint hx = asuint(cell.x * 73856093);
    const uint hy = asuint(cell.y * 19349663);
    const uint hz = asuint(cell.z * 83492791);
    return (hx ^ hy ^ hz) % max(gHashBucketCount, 1u);
}

bool N01GroupIsDynamic(uint groupIndex)
{
    if (groupIndex == N01_INVALID || groupIndex >= gGroupCount)
    {
        return false;
    }
    const uint state = N01Cores[groupIndex].state;
    return state == N01_STATE_DYNAMIC ||
        state == N01_STATE_FORCED_DYNAMIC ||
        state == N01_STATE_REPAIR;
}

uint N01DynamicPayloadIndex(uint compactedIndex, uint groupIndex)
{
    return N01FeatureEnabled(N01_FEATURE_OPTIONAL_PAYLOAD)
        ? compactedIndex
        : groupIndex;
}

bool N01GuideRangeValid(uint guideIndex, out uint segmentBegin, out uint segmentEnd)
{
    segmentBegin = 0u;
    segmentEnd = 0u;
    if (guideIndex >= gGuideCount)
    {
        return false;
    }
    const GuideDirtyRange range = N01GuideDirtyRangesIn[guideIndex];
    if (range.segmentBegin == N01_INVALID || range.segmentEnd < range.segmentBegin)
    {
        return false;
    }
    segmentBegin = min(range.segmentBegin, gParticlesPerStrand - 1u);
    segmentEnd = min(range.segmentEnd, gParticlesPerStrand - 2u);
    return segmentBegin <= segmentEnd;
}

uint N01GroupIndexFromGuideSegment(uint guideIndex, uint localSegment)
{
    return guideIndex * (gParticlesPerStrand - 1u) + localSegment;
}

float N01ComputeBackFreezeFactor(uint guideIndex, uint localIndex)
{
    const float3 rootRest = N01RestPositions[guideIndex * gParticlesPerStrand].xyz;
    const float rootMask = saturate((-rootRest.z + gFreezeBackThreshold) / 0.18f);
    const float localT = (gParticlesPerStrand > 1u)
        ? float(localIndex) / float(gParticlesPerStrand - 1u)
        : 0.0f;
    const float tailMask = saturate((localT - 0.25f) / 0.75f);
    return rootMask * tailMask;
}

uint N01SdfIndex(uint x, uint y, uint z)
{
    return (z * gSdfResolution.y + y) * gSdfResolution.x + x;
}

float N01SampleSdfNearest(int x, int y, int z)
{
    x = clamp(x, 0, int(gSdfResolution.x) - 1);
    y = clamp(y, 0, int(gSdfResolution.y) - 1);
    z = clamp(z, 0, int(gSdfResolution.z) - 1);
    return N01SdfValues[N01SdfIndex((uint)x, (uint)y, (uint)z)];
}

float N01SampleSdfTrilinear(float3 p)
{
    if (gUseSdfCollider == 0u || gSdfValueCount == 0u ||
        gSdfResolution.x < 2u || gSdfResolution.y < 2u || gSdfResolution.z < 2u)
    {
        return 1.0e9f;
    }
    const float3 cellSize = (gSdfBoundsMax.xyz - gSdfBoundsMin.xyz) /
        max(float3(gSdfResolution.xyz - 1u), float3(1.0f, 1.0f, 1.0f));
    const float3 grid = clamp(
        (p - gSdfBoundsMin.xyz) / max(cellSize, float3(1.0e-6f, 1.0e-6f, 1.0e-6f)),
        float3(0.0f, 0.0f, 0.0f),
        float3(gSdfResolution.xyz - 1u));
    const int3 i0 = int3(floor(grid));
    const int3 i1 = min(i0 + 1, int3(gSdfResolution.xyz) - 1);
    const float3 t = grid - float3(i0);
    const float c000 = N01SampleSdfNearest(i0.x, i0.y, i0.z);
    const float c100 = N01SampleSdfNearest(i1.x, i0.y, i0.z);
    const float c010 = N01SampleSdfNearest(i0.x, i1.y, i0.z);
    const float c110 = N01SampleSdfNearest(i1.x, i1.y, i0.z);
    const float c001 = N01SampleSdfNearest(i0.x, i0.y, i1.z);
    const float c101 = N01SampleSdfNearest(i1.x, i0.y, i1.z);
    const float c011 = N01SampleSdfNearest(i0.x, i1.y, i1.z);
    const float c111 = N01SampleSdfNearest(i1.x, i1.y, i1.z);
    const float c00 = lerp(c000, c100, t.x);
    const float c10 = lerp(c010, c110, t.x);
    const float c01 = lerp(c001, c101, t.x);
    const float c11 = lerp(c011, c111, t.x);
    return lerp(lerp(c00, c10, t.y), lerp(c01, c11, t.y), t.z);
}

float3 N01EstimateSdfGradient(float3 p)
{
    const float3 cellSize = (gSdfBoundsMax.xyz - gSdfBoundsMin.xyz) /
        max(float3(gSdfResolution.xyz - 1u), float3(1.0f, 1.0f, 1.0f));
    const float3 e = max(cellSize, float3(1.0e-4f, 1.0e-4f, 1.0e-4f));
    return N01SafeNormalize(float3(
        N01SampleSdfTrilinear(p + float3(e.x, 0.0f, 0.0f)) - N01SampleSdfTrilinear(p - float3(e.x, 0.0f, 0.0f)),
        N01SampleSdfTrilinear(p + float3(0.0f, e.y, 0.0f)) - N01SampleSdfTrilinear(p - float3(0.0f, e.y, 0.0f)),
        N01SampleSdfTrilinear(p + float3(0.0f, 0.0f, e.z)) - N01SampleSdfTrilinear(p - float3(0.0f, 0.0f, e.z))),
        float3(0.0f, 1.0f, 0.0f));
}

float3 N01ResolveExternalCollisionMode3(float3 p, out float residual)
{
    p = N01ResolveExternalCollision(p, residual);
    if (gUseSdfCollider != 0u && gSdfValueCount > 0u)
    {
        const float shell = max(gSupportCollisionRadius * 0.6f, 0.0025f);
        const float sdf = N01SampleSdfTrilinear(p);
        if (sdf < shell)
        {
            const float3 before = p;
            p += N01EstimateSdfGradient(p) * (shell - sdf);
            residual = max(residual, length(p - before));
        }
    }
    return p;
}

float3 N01ApplySupportCollision(uint guideIndex, uint localIndex, float3 p)
{
    if (gSupportCollisionRadius <= 0.0f || gSupportCollisionStiffness <= 0.0f)
    {
        return p;
    }
    float3 correction = float3(0.0f, 0.0f, 0.0f);
    uint samples = 0u;
    [unroll]
    for (int offset = -2; offset <= 2; ++offset)
    {
        const int otherGuide = int(guideIndex) + offset;
        if (otherGuide < 0 || otherGuide >= int(gGuideCount) || otherGuide == int(guideIndex))
        {
            continue;
        }
        const uint otherStart = uint(otherGuide) * gParticlesPerStrand;
        const uint segmentLocal = min(localIndex, gParticlesPerStrand - 2u);
        const float3 a = N01Positions[otherStart + segmentLocal].xyz;
        const float3 b = N01Positions[otherStart + segmentLocal + 1u].xyz;
        const float3 ab = b - a;
        const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1.0e-8f));
        const float3 closest = lerp(a, b, t);
        const float3 delta = p - closest;
        const float d2 = dot(delta, delta);
        if (d2 > 1.0e-8f && d2 < gSupportCollisionRadius * gSupportCollisionRadius)
        {
            const float d = sqrt(d2);
            correction += delta / d * (gSupportCollisionRadius - d);
            samples += 1u;
        }
    }
    return samples > 0u
        ? p + correction / float(samples) * gSupportCollisionStiffness
        : p;
}

float3 N01ApplyLooseGuideConstraints(uint guideIndex, uint localIndex, float3 p)
{
    if (gLooseConstraintStiffness <= 0.0f || gLooseConstraintMaxDistance <= 0.0f)
    {
        return p;
    }
    const uint pointIndex = guideIndex * gParticlesPerStrand + localIndex;
    const float3 restP = N01RestPositions[pointIndex].xyz;
    float3 correction = float3(0.0f, 0.0f, 0.0f);
    uint samples = 0u;
    const float capture = max(gLooseConstraintMaxDistance, gSelfCollisionRadius * 1.5f);
    [unroll]
    for (int offset = -2; offset <= 2; ++offset)
    {
        const int otherGuide = int(guideIndex) + offset;
        if (otherGuide < 0 || otherGuide >= int(gGuideCount) || otherGuide == int(guideIndex))
        {
            continue;
        }
        const uint otherIndex = uint(otherGuide) * gParticlesPerStrand + localIndex;
        const float3 q = N01Positions[otherIndex].xyz;
        const float3 restQ = N01RestPositions[otherIndex].xyz;
        const float restDistance = length(restQ - restP);
        if (restDistance > capture)
        {
            continue;
        }
        const float3 delta = q - p;
        const float distanceValue = length(delta);
        if (distanceValue <= 1.0e-6f)
        {
            continue;
        }
        const float target = min(max(restDistance, gSelfCollisionRadius * 0.95f), capture);
        const float error = distanceValue - target;
        if (abs(error) >= 1.0e-4f)
        {
            correction += delta / distanceValue * error * saturate(1.0f - abs(error) / capture);
            samples += 1u;
        }
    }
    return samples > 0u
        ? p + correction / float(samples) * gLooseConstraintStiffness
        : p;
}


[numthreads(64, 1, 1)]
void N01ClearDynamicGuideLambdasCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint guideIndex = dispatchThreadId.x;
    uint segmentBegin, segmentEnd;
    if (!N01GuideRangeValid(guideIndex, segmentBegin, segmentEnd))
    {
        return;
    }
    [loop]
    for (uint segment = segmentBegin; segment <= segmentEnd; ++segment)
    {
        const uint groupIndex = N01GroupIndexFromGuideSegment(guideIndex, segment);
        if (!N01GroupIsDynamic(groupIndex))
        {
            continue;
        }
        const CoreExtension extension = N01Extensions[groupIndex];
        if (extension.payloadIndex < gPayloadCapacity)
        {
            DynamicPayload payload = N01Payloads[extension.payloadIndex];
            payload.distanceLambda = 0.0f;
            payload.bendLambda = 0.0f;
            payload.constraintResidual = 0.0f;
            payload.collisionResidual = 0.0f;
            N01Payloads[extension.payloadIndex] = payload;
        }
    }
}

[numthreads(64, 1, 1)]
void N01IntegrateDynamicGuidesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint guideIndex = dispatchThreadId.x;
    uint segmentBegin, segmentEnd;
    if (!N01GuideRangeValid(guideIndex, segmentBegin, segmentEnd))
    {
        return;
    }
    const uint strandStart = guideIndex * gParticlesPerStrand;
    [loop]
    for (uint localIndex = segmentBegin + 1u; localIndex <= segmentEnd + 1u; ++localIndex)
    {
        const uint groupIndex = N01GroupIndexFromGuideSegment(guideIndex, localIndex - 1u);
        if (!N01GroupIsDynamic(groupIndex))
        {
            continue;
        }
        const uint pointIndex = strandStart + localIndex;
        const float3 current = N01Positions[pointIndex].xyz;
        const float3 previous = N01PreviousPositions[pointIndex].xyz;
        const float localT = float(localIndex) / max(float(gParticlesPerStrand - 1u), 1.0f);
        const float shockScale = lerp(gVelocityShockPropagation, 1.0f, localT);
        const float3 velocity = (current - previous) * (1.0f - gDamping) * shockScale;
        const float windPulse = 0.65f + 0.35f * sin(gElapsedTime * 1.3f + float(pointIndex) * 0.003f);
        const float3 acceleration = float3(0.0f, -gGravity, 0.0f) +
            N01SafeNormalize(gWindDirection, float3(1.0f, 0.0f, 0.0f)) * gWindStrength * windPulse;
        float3 next = current + velocity + acceleration * (gDeltaTime * gDeltaTime);
        const float freezeFactor = N01ComputeBackFreezeFactor(guideIndex, localIndex);
        if (freezeFactor > 0.0f)
        {
            next = lerp(next, current, freezeFactor * gFreezeBlend);
        }
        if (!N01Finite3(next))
        {
            SegmentGroupCore core = N01Cores[groupIndex];
            core.previousState = core.state;
            core.state = N01_STATE_REPAIR;
            core.dirtyFlags |= N01_DIRTY_NONFINITE;
            N01Cores[groupIndex] = core;
            continue;
        }
        N01PreviousPositions[pointIndex] = float4(current, 1.0f);
        N01Positions[pointIndex] = float4(next, 1.0f);
    }
}

[numthreads(64, 1, 1)]
void N01DistanceDynamicGuidesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint guideIndex = dispatchThreadId.x;
    uint segmentBegin, segmentEnd;
    if (!N01GuideRangeValid(guideIndex, segmentBegin, segmentEnd))
    {
        return;
    }
    const uint strandStart = guideIndex * gParticlesPerStrand;
    const float alpha = gStretchCompliance / max(gDeltaTime * gDeltaTime, 1.0e-8f);
    [loop]
    for (uint localIndex = max(segmentBegin + 1u, 1u); localIndex <= segmentEnd + 1u; ++localIndex)
    {
        const uint groupIndex = N01GroupIndexFromGuideSegment(guideIndex, localIndex - 1u);
        if (!N01GroupIsDynamic(groupIndex))
        {
            continue;
        }
        const uint previousIndex = strandStart + localIndex - 1u;
        const uint currentIndex = strandStart + localIndex;
        float3 p0 = N01Positions[previousIndex].xyz;
        float3 p1 = N01Positions[currentIndex].xyz;
        const float3 rest0 = N01RestPositions[previousIndex].xyz;
        const float3 rest1 = N01RestPositions[currentIndex].xyz;
        const float targetLength = max(length(rest1 - rest0), 1.0e-5f);
        const float3 delta = p1 - p0;
        const float currentLength = max(length(delta), 1.0e-6f);
        const float3 direction = currentLength > 1.0e-6f
            ? delta / currentLength
            : N01SafeNormalize(rest1 - rest0, float3(0.0f, -1.0f, 0.0f));
        const CoreExtension extension = N01Extensions[groupIndex];
        DynamicPayload payload = (DynamicPayload)0;
        if (extension.payloadIndex < gPayloadCapacity)
        {
            payload = N01Payloads[extension.payloadIndex];
        }
        const float constraint = currentLength - targetLength;
        const float w0 = localIndex == 1u ? 0.0f : 1.0f;
        const float w1 = 1.0f;
        const float deltaLambda = (-constraint - alpha * payload.distanceLambda) /
            max(w0 + w1 + alpha, 1.0e-6f);
        payload.distanceLambda += deltaLambda;
        payload.constraintResidual = max(payload.constraintResidual, abs(constraint));
        if (w0 > 0.0f)
        {
            const float3 correction0 = -direction * (w0 * deltaLambda);
            p0 += correction0;
            N01Positions[previousIndex] = float4(p0, 1.0f);
            N01PreviousPositions[previousIndex] = float4(N01PreviousPositions[previousIndex].xyz + correction0, 1.0f);
        }
        const float3 correction1 = direction * (w1 * deltaLambda);
        p1 += correction1;
        N01Positions[currentIndex] = float4(p1, 1.0f);
        N01PreviousPositions[currentIndex] = float4(N01PreviousPositions[currentIndex].xyz + correction1, 1.0f);
        if (extension.payloadIndex < gPayloadCapacity)
        {
            N01Payloads[extension.payloadIndex] = payload;
        }
    }
}

[numthreads(64, 1, 1)]
void N01BendDynamicGuidesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint guideIndex = dispatchThreadId.x;
    uint segmentBegin, segmentEnd;
    if (!N01GuideRangeValid(guideIndex, segmentBegin, segmentEnd) || gParticlesPerStrand < 3u)
    {
        return;
    }
    const uint strandStart = guideIndex * gParticlesPerStrand;
    const float compliance = gAngularBendCompliance > 0.0f ? gAngularBendCompliance : gBendCompliance;
    const float stiffness = gAngularBendStiffness > 0.0f ? saturate(gAngularBendStiffness) : saturate(gBendStiffness);
    const float damping = saturate(gAngularBendDamping);
    const float alpha = compliance / max(gDeltaTime * gDeltaTime, 1.0e-8f);
    const uint beginPoint = max(segmentBegin, 1u);
    const uint endPoint = min(segmentEnd + 1u, gParticlesPerStrand - 2u);
    [loop]
    for (uint i = beginPoint; i <= endPoint; ++i)
    {
        const uint leftGroup = N01GroupIndexFromGuideSegment(guideIndex, i - 1u);
        const uint rightGroup = N01GroupIndexFromGuideSegment(guideIndex, i);
        if (!N01GroupIsDynamic(leftGroup) && !N01GroupIsDynamic(rightGroup))
        {
            continue;
        }
        const uint aIndex = strandStart + i - 1u;
        const uint cIndex = strandStart + i + 1u;
        float3 a = N01Positions[aIndex].xyz;
        float3 c = N01Positions[cIndex].xyz;
        const float3 restA = N01RestPositions[aIndex].xyz;
        const float3 restC = N01RestPositions[cIndex].xyz;
        const float targetChord = max(length(restC - restA), 1.0e-5f);
        const float3 chord = c - a;
        const float currentChord = max(length(chord), 1.0e-6f);
        const float3 direction = currentChord > 1.0e-6f
            ? chord / currentChord
            : N01SafeNormalize(restC - restA, float3(0.0f, -1.0f, 0.0f));
        const uint payloadGroup = N01GroupIsDynamic(leftGroup) ? leftGroup : rightGroup;
        const CoreExtension extension = N01Extensions[payloadGroup];
        DynamicPayload payload = (DynamicPayload)0;
        if (extension.payloadIndex < gPayloadCapacity)
        {
            payload = N01Payloads[extension.payloadIndex];
        }
        const float constraint = currentChord - targetChord;
        const float wA = i == 1u ? 0.0f : 1.0f;
        const float wC = 1.0f;
        const float deltaLambda = (-constraint - alpha * payload.bendLambda) /
            max(wA + wC + alpha, 1.0e-6f);
        payload.bendLambda += deltaLambda;
        payload.constraintResidual = max(payload.constraintResidual, abs(constraint));
        const float gain = stiffness * (1.0f - 0.50f * damping);
        if (wA > 0.0f)
        {
            const float3 correctionA = -direction * (wA * deltaLambda * gain);
            a += correctionA;
            N01Positions[aIndex] = float4(a, 1.0f);
            N01PreviousPositions[aIndex] = float4(N01PreviousPositions[aIndex].xyz + correctionA, 1.0f);
        }
        const float3 correctionC = direction * (wC * deltaLambda * gain);
        c += correctionC;
        N01Positions[cIndex] = float4(c, 1.0f);
        N01PreviousPositions[cIndex] = float4(N01PreviousPositions[cIndex].xyz + correctionC, 1.0f);
        if (extension.payloadIndex < gPayloadCapacity)
        {
            N01Payloads[extension.payloadIndex] = payload;
        }
    }
}

[numthreads(64, 1, 1)]
void N01CollisionDynamicGuidesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint guideIndex = dispatchThreadId.x;
    uint segmentBegin, segmentEnd;
    if (!N01GuideRangeValid(guideIndex, segmentBegin, segmentEnd))
    {
        return;
    }
    const uint strandStart = guideIndex * gParticlesPerStrand;
    [loop]
    for (uint localIndex = segmentBegin + 1u; localIndex <= segmentEnd + 1u; ++localIndex)
    {
        const uint groupIndex = N01GroupIndexFromGuideSegment(guideIndex, localIndex - 1u);
        if (!N01GroupIsDynamic(groupIndex))
        {
            continue;
        }
        const uint pointIndex = strandStart + localIndex;
        const float3 before = N01Positions[pointIndex].xyz;
        const float3 previous = N01PreviousPositions[pointIndex].xyz;
        float collisionResidual = 0.0f;
        float3 after = N01ResolveExternalCollisionMode3(before, collisionResidual);
        after = N01ApplySupportCollision(guideIndex, localIndex, after);
        if (!N01Finite3(after))
        {
            continue;
        }
        if (collisionResidual > 0.0f)
        {
            float3 tangent;
            if (localIndex + 1u < gParticlesPerStrand)
            {
                tangent = N01SafeNormalize(N01Positions[pointIndex + 1u].xyz - N01Positions[pointIndex - 1u].xyz, float3(0.0f, -1.0f, 0.0f));
            }
            else
            {
                tangent = N01SafeNormalize(after - N01Positions[pointIndex - 1u].xyz, float3(0.0f, -1.0f, 0.0f));
            }
            const float3 velocity = after - previous;
            float3 tangential = tangent * dot(velocity, tangent);
            float3 lateral = velocity - tangential;
            tangential *= max(1.0f - gTangentialFriction, 0.0f);
            lateral *= max(1.0f - gLateralFriction, 0.0f);
            const float freeze = N01ComputeBackFreezeFactor(guideIndex, localIndex);
            const float3 adjusted = lerp(tangential + lateral, tangential * 0.25f, freeze * gFreezeBlend * 0.5f);
            N01PreviousPositions[pointIndex] = float4(after - adjusted, 1.0f);
        }
        N01Positions[pointIndex] = float4(after, 1.0f);
        const CoreExtension extension = N01Extensions[groupIndex];
        if (extension.payloadIndex < gPayloadCapacity)
        {
            DynamicPayload payload = N01Payloads[extension.payloadIndex];
            payload.collisionResidual = collisionResidual;
            payload.collisionFlags |= collisionResidual > 0.0f ? 1u : 0u;
            N01Payloads[extension.payloadIndex] = payload;
        }
    }
}

[numthreads(64, 1, 1)]
void N01StabilityGuardDynamicGuidesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint guideIndex = dispatchThreadId.x;
    uint segmentBegin, segmentEnd;
    if (!N01GuideRangeValid(guideIndex, segmentBegin, segmentEnd))
    {
        return;
    }
    const uint strandStart = guideIndex * gParticlesPerStrand;
    [loop]
    for (uint localIndex = max(segmentBegin + 1u, 1u); localIndex <= segmentEnd + 1u; ++localIndex)
    {
        const uint groupIndex = N01GroupIndexFromGuideSegment(guideIndex, localIndex - 1u);
        if (!N01GroupIsDynamic(groupIndex))
        {
            continue;
        }
        const uint previousIndex = strandStart + localIndex - 1u;
        const uint currentIndex = strandStart + localIndex;
        const float3 parent = N01Positions[previousIndex].xyz;
        const float3 child = N01Positions[currentIndex].xyz;
        const float3 restParent = N01RestPositions[previousIndex].xyz;
        const float3 restChild = N01RestPositions[currentIndex].xyz;
        const float targetLength = max(length(restChild - restParent), 1.0e-5f);
        const float3 segment = child - parent;
        const float currentLength = length(segment);
        if (!N01Finite3(child))
        {
            SegmentGroupCore core = N01Cores[groupIndex];
            core.previousState = core.state;
            core.state = N01_STATE_REPAIR;
            core.dirtyFlags |= N01_DIRTY_NONFINITE;
            N01Cores[groupIndex] = core;
            continue;
        }
        if (currentLength > targetLength * 1.005f)
        {
            const float3 direction = currentLength > 1.0e-6f
                ? segment / currentLength
                : N01SafeNormalize(restChild - restParent, float3(0.0f, -1.0f, 0.0f));
            const float3 corrected = parent + direction * targetLength;
            const float3 correction = corrected - child;
            N01Positions[currentIndex] = float4(corrected, 1.0f);
            N01PreviousPositions[currentIndex] = float4(N01PreviousPositions[currentIndex].xyz + correction, 1.0f);
        }
    }
}

[numthreads(64, 1, 1)]
void N01IntegrateDynamicCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount =
        N01Counters[N01_COUNTER_DYNAMIC];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupIndex = N01DynamicListIn[compactedIndex];
    SegmentGroupCore core = N01Cores[groupIndex];
    if (core.state != N01_STATE_DYNAMIC &&
        core.state != N01_STATE_FORCED_DYNAMIC &&
        core.state != N01_STATE_REPAIR)
    {
        return;
    }
    CoreExtension extension = N01Extensions[groupIndex];
    const uint payloadIndex = extension.payloadIndex;
    const bool payloadValid = payloadIndex < gPayloadCapacity;
    DynamicPayload payload = (DynamicPayload)0;
    if (payloadValid)
    {
        payload = N01Payloads[payloadIndex];
    }

    const uint pointIndex = core.endPointIndex;
    const float3 current = N01Positions[pointIndex].xyz;
    const float3 previous = N01PreviousPositions[pointIndex].xyz;
    float3 velocity =
        (current - previous) /
        max(gDeltaTime, 1.0e-6f);

    if (N01Finite3(payload.velocity))
    {
        velocity = lerp(velocity, payload.velocity, 0.35f);
    }

    const float phase =
        float((groupIndex * 747796405u + 2891336453u) & 1023u) /
        1023.0f;
    const float3 windDirection =
        N01SafeNormalize(
            gWindDirection,
            float3(1.0f, 0.0f, 0.0f));
    const float3 acceleration =
        float3(0.0f, -gGravity, 0.0f) +
        windDirection *
        gWindStrength *
        (0.55f + 0.45f * sin(phase * 6.2831853f + gFrameIndex * 0.071f));

    const float3 predicted =
        current +
        velocity * gDeltaTime * max(1.0f - gDamping, 0.0f) +
        acceleration * gDeltaTime * gDeltaTime;

    if (!N01Finite3(predicted))
    {
        core.previousState = core.state;
        core.state = N01_STATE_REPAIR;
        core.dirtyFlags |= N01_DIRTY_NONFINITE;
        N01Cores[groupIndex] = core;
        return;
    }

    N01PreviousPositions[pointIndex] = float4(current, 1.0f);
    N01Positions[pointIndex] = float4(predicted, 1.0f);

    payload.previousEndPosition = current;
    payload.velocity = velocity;
    payload.predictedPositionErrorMm =
        length(predicted - current) * 1000.0f;
    payload.allocationEpoch =
        payload.allocationEpoch == 0u
        ? gFrameIndex
        : payload.allocationEpoch;

    extension.lastSolvedFrame = gFrameIndex;
    if (payloadValid)
    {
        N01Payloads[payloadIndex] = payload;
    }
    else
    {
        core.flags |= N01_CORE_BASELINE_FALLBACK;
        core.flags &= ~N01_CORE_PAYLOAD_VALID;
        extension.payloadIndex = N01_INVALID;
    }
    N01Extensions[groupIndex] = extension;
    N01Cores[groupIndex] = core;
}

[numthreads(64, 1, 1)]
void N01DistanceConstraintCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount =
        N01Counters[N01_COUNTER_DYNAMIC];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupIndex = N01DynamicListIn[compactedIndex];
    SegmentGroupCore core = N01Cores[groupIndex];
    if (core.state != N01_STATE_DYNAMIC &&
        core.state != N01_STATE_FORCED_DYNAMIC &&
        core.state != N01_STATE_REPAIR)
    {
        return;
    }
    const CoreExtension extension = N01Extensions[groupIndex];
    const uint payloadIndex = extension.payloadIndex;
    if ((core.localGroupIndex & 1u) != (gSolverPhase & 1u))
    {
        return;
    }

    const uint startIndex = core.startPointIndex;
    const uint endIndex = core.endPointIndex;
    float3 start = N01Positions[startIndex].xyz;
    float3 end = N01Positions[endIndex].xyz;
    const float3 delta = end - start;
    const float currentLength = N01SafeLength(delta);
    const float3 direction =
        N01SafeNormalize(delta, core.restDirection);
    const float constraint =
        currentLength - max(core.restLength, 1.0e-6f);

    const bool payloadValid = payloadIndex < gPayloadCapacity;
    DynamicPayload payload = (DynamicPayload)0;
    if (payloadValid)
    {
        payload = N01Payloads[payloadIndex];
    }
    const float alpha =
        gStretchCompliance /
        max(gDeltaTime * gDeltaTime, 1.0e-8f);
    const bool startDynamic =
        core.parentGroupIndex != N01_INVALID &&
        N01GroupIsDynamic(core.parentGroupIndex);
    const float wStart = startDynamic ? 1.0f : 0.0f;
    const float wEnd = 1.0f;
    const float deltaLambda =
        (-constraint - alpha * payload.distanceLambda) /
        max(wStart + wEnd + alpha, 1.0e-6f);
    payload.distanceLambda += deltaLambda;
    payload.constraintResidual = abs(constraint);

    if (wStart > 0.0f)
    {
        start -= direction * (wStart * deltaLambda);
        N01Positions[startIndex] = float4(start, 1.0f);
    }
    end += direction * (wEnd * deltaLambda);
    N01Positions[endIndex] = float4(end, 1.0f);
    if (payloadValid)
    {
        N01Payloads[payloadIndex] = payload;
    }
}

[numthreads(64, 1, 1)]
void N01BendConstraintCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount =
        N01Counters[N01_COUNTER_DYNAMIC];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupIndex = N01DynamicListIn[compactedIndex];
    const SegmentGroupCore core = N01Cores[groupIndex];
    if (core.state != N01_STATE_DYNAMIC &&
        core.state != N01_STATE_FORCED_DYNAMIC &&
        core.state != N01_STATE_REPAIR)
    {
        return;
    }
    const CoreExtension extension = N01Extensions[groupIndex];
    const uint payloadIndex = extension.payloadIndex;
    if (core.childGroupIndex == N01_INVALID ||
        (core.localGroupIndex % 3u) != (gSolverPhase % 3u))
    {
        return;
    }

    const SegmentGroupCore child =
        N01Cores[core.childGroupIndex];
    const uint aIndex = core.startPointIndex;
    const uint cIndex = child.endPointIndex;
    float3 a = N01Positions[aIndex].xyz;
    float3 c = N01Positions[cIndex].xyz;
    const float3 restA = N01RestPositions[aIndex].xyz;
    const float3 restC = N01RestPositions[cIndex].xyz;
    const float targetChord =
        max(length(restC - restA), 1.0e-6f);
    const float3 chord = c - a;
    const float currentChord = N01SafeLength(chord);
    const float3 direction =
        N01SafeNormalize(chord, restC - restA);
    const float constraint = currentChord - targetChord;

    const bool payloadValid = payloadIndex < gPayloadCapacity;
    DynamicPayload payload = (DynamicPayload)0;
    if (payloadValid)
    {
        payload = N01Payloads[payloadIndex];
    }
    const float alpha =
        gBendCompliance /
        max(gDeltaTime * gDeltaTime, 1.0e-8f);
    const bool aDynamic =
        core.parentGroupIndex != N01_INVALID &&
        N01GroupIsDynamic(core.parentGroupIndex);
    const bool cDynamic =
        N01GroupIsDynamic(core.childGroupIndex);
    const float wA = aDynamic ? 1.0f : 0.0f;
    const float wC = cDynamic ? 1.0f : 0.0f;
    const float deltaLambda =
        (-constraint - alpha * payload.bendLambda) /
        max(wA + wC + alpha, 1.0e-6f);
    payload.bendLambda += deltaLambda;
    payload.constraintResidual = max(
        payload.constraintResidual,
        abs(constraint));

    const float gain = saturate(gBendStiffness);
    if (wA > 0.0f)
    {
        a -= direction * (wA * deltaLambda * gain);
        N01Positions[aIndex] = float4(a, 1.0f);
    }
    if (wC > 0.0f)
    {
        c += direction * (wC * deltaLambda * gain);
        N01Positions[cIndex] = float4(c, 1.0f);
    }

    if (payloadValid)
    {
        N01Payloads[payloadIndex] = payload;
    }
}

[numthreads(64, 1, 1)]
void N01CollisionCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount =
        N01Counters[N01_COUNTER_DYNAMIC];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupIndex = N01DynamicListIn[compactedIndex];
    const SegmentGroupCore core = N01Cores[groupIndex];
    if (core.state != N01_STATE_DYNAMIC &&
        core.state != N01_STATE_FORCED_DYNAMIC &&
        core.state != N01_STATE_REPAIR)
    {
        return;
    }
    const CoreExtension extension = N01Extensions[groupIndex];
    const uint payloadIndex = extension.payloadIndex;
    const bool payloadValid = payloadIndex < gPayloadCapacity;
    DynamicPayload payload = (DynamicPayload)0;
    if (payloadValid)
    {
        payload = N01Payloads[payloadIndex];
    }

    const uint pointIndex = core.endPointIndex;
    const float3 before = N01Positions[pointIndex].xyz;
    float residual = 0.0f;
    const float3 after =
        N01ResolveExternalCollisionMode3(before, residual);

    if (N01Finite3(after))
    {
        N01Positions[pointIndex] = float4(after, 1.0f);
        if (residual > 0.0f)
        {
            const float3 previous =
                N01PreviousPositions[pointIndex].xyz;
            const float3 velocity = after - previous;
            N01PreviousPositions[pointIndex] =
                float4(after - velocity * 0.35f, 1.0f);
            payload.collisionFlags |= 1u;
        }
        payload.collisionResidual = residual;
        if (payloadValid)
        {
            N01Payloads[payloadIndex] = payload;
        }
    }
}

[numthreads(64, 1, 1)]
void N01ClearHashCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    const uint guidePointCount =
        gGuideCount * gParticlesPerStrand;
    if (index < gHashBucketCount)
    {
        N01HashHeads[index] = N01_INVALID;
    }
    if (index < guidePointCount)
    {
        N01ParticleNext[index] = N01_INVALID;
    }
}

[numthreads(64, 1, 1)]
void N01BuildHashCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint pointIndex = dispatchThreadId.x;
    const uint guidePointCount =
        gGuideCount * gParticlesPerStrand;
    if (pointIndex >= guidePointCount ||
        (pointIndex % gParticlesPerStrand) == 0u)
    {
        return;
    }

    const float3 p = N01Positions[pointIndex].xyz;
    if (!N01Finite3(p))
    {
        return;
    }

    const uint bucket = N01Hash(p);
    uint previousHead;
    InterlockedExchange(
        N01HashHeads[bucket],
        pointIndex,
        previousHead);
    N01ParticleNext[pointIndex] = previousHead;
}

[numthreads(64, 1, 1)]
void N01SelfCollisionCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (gEnableSelfCollision == 0u)
    {
        return;
    }

    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount =
        N01Counters[N01_COUNTER_DYNAMIC];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupIndex = N01DynamicListIn[compactedIndex];
    const SegmentGroupCore core = N01Cores[groupIndex];
    if (core.state != N01_STATE_DYNAMIC &&
        core.state != N01_STATE_FORCED_DYNAMIC &&
        core.state != N01_STATE_REPAIR)
    {
        return;
    }
    const uint pointIndex = core.endPointIndex;
    const uint strandIndex =
        pointIndex / gParticlesPerStrand;
    const uint localIndex =
        pointIndex % gParticlesPerStrand;

    float3 p = N01Positions[pointIndex].xyz;
    const int3 baseCell =
        int3(floor(p / max(gGridCellSize, 1.0e-5f)));
    float3 correction = float3(0.0f, 0.0f, 0.0f);
    uint samples = 0u;
    const float radius = max(gSelfCollisionRadius, 1.0e-5f);

    [loop]
    for (int z = -1; z <= 1; ++z)
    {
        [loop]
        for (int y = -1; y <= 1; ++y)
        {
            [loop]
            for (int x = -1; x <= 1; ++x)
            {
                const int3 cell = baseCell + int3(x, y, z);
                const uint hx = asuint(cell.x * 73856093);
                const uint hy = asuint(cell.y * 19349663);
                const uint hz = asuint(cell.z * 83492791);
                const uint bucket =
                    (hx ^ hy ^ hz) %
                    max(gHashBucketCount, 1u);

                uint other = N01HashHeads[bucket];
                uint guard = 0u;
                [loop]
                while (other != N01_INVALID && guard < 128u)
                {
                    const uint otherStrand =
                        other / gParticlesPerStrand;
                    const uint otherLocal =
                        other % gParticlesPerStrand;
                    const bool adjacentSameStrand =
                        otherStrand == strandIndex &&
                        abs(int(otherLocal) - int(localIndex)) <= 2;

                    if (other != pointIndex &&
                        !adjacentSameStrand)
                    {
                        const float3 q =
                            N01Positions[other].xyz;
                        const float3 delta = p - q;
                        const float distanceSquared =
                            dot(delta, delta);
                        if (distanceSquared > 1.0e-10f &&
                            distanceSquared < radius * radius)
                        {
                            const float distanceValue =
                                sqrt(distanceSquared);
                            correction +=
                                delta / distanceValue *
                                (radius - distanceValue);
                            samples += 1u;
                        }
                    }

                    other = N01ParticleNext[other];
                    guard += 1u;
                }
            }
        }
    }

    if (samples > 0u)
    {
        p +=
            correction /
            float(samples) *
            saturate(gSelfCollisionStiffness);
    }
    p = N01ApplyLooseGuideConstraints(strandIndex, localIndex, p);
    N01Positions[pointIndex] = float4(p, 1.0f);
}

[numthreads(64, 1, 1)]
void N01VelocityRecoveryCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint compactedIndex = dispatchThreadId.x;
    const uint dynamicCount =
        N01Counters[N01_COUNTER_DYNAMIC];
    if (compactedIndex >= dynamicCount)
    {
        return;
    }

    const uint groupIndex = N01DynamicListIn[compactedIndex];
    const SegmentGroupCore core = N01Cores[groupIndex];
    if (core.state != N01_STATE_DYNAMIC &&
        core.state != N01_STATE_FORCED_DYNAMIC &&
        core.state != N01_STATE_REPAIR)
    {
        return;
    }
    const CoreExtension extension = N01Extensions[groupIndex];
    const uint payloadIndex = extension.payloadIndex;
    const bool payloadValid = payloadIndex < gPayloadCapacity;
    DynamicPayload payload = (DynamicPayload)0;
    if (payloadValid)
    {
        payload = N01Payloads[payloadIndex];
    }
    const uint pointIndex = core.endPointIndex;
    const float3 current = N01Positions[pointIndex].xyz;
    const float3 previous = N01PreviousPositions[pointIndex].xyz;
    const float3 recoveredVelocity =
        (current - previous) /
        max(gDeltaTime, 1.0e-6f);

    payload.velocity =
        N01Finite3(recoveredVelocity)
        ? recoveredVelocity
        : float3(0.0f, 0.0f, 0.0f);
    payload.previousEndPosition = previous;
    payload.boundaryErrorMm =
        abs(length(current - N01Positions[core.startPointIndex].xyz) -
            core.restLength) *
        1000.0f;
    if (payloadValid)
    {
        N01Payloads[payloadIndex] = payload;
    }
}

[numthreads(64, 1, 1)]
void N01WatchdogCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    SegmentGroupCore core = N01Cores[groupIndex];
    CoreExtension extension = N01Extensions[groupIndex];
    const float3 start =
        N01Positions[core.startPointIndex].xyz;
    const float3 end =
        N01Positions[core.endPointIndex].xyz;
    const float lengthError =
        abs(length(end - start) - core.restLength) /
        max(core.restLength, 1.0e-6f);

    N01CounterAdd(N01_COUNTER_WATCHDOG, 1u);

    if (!N01Finite3(start) ||
        !N01Finite3(end) ||
        lengthError > gLengthErrorThresholdRatio * 2.0f)
    {
        core.previousState = core.state;
        core.state = N01_STATE_REPAIR;
        core.dirtyFlags |=
            !N01Finite3(start) || !N01Finite3(end)
            ? N01_DIRTY_NONFINITE
            : N01_DIRTY_WATCHDOG;
        core.flags |= N01_CORE_FORCED_DYNAMIC;
        extension.minimumResidenceEndFrame =
            gFrameIndex + gMinimumResidenceFrames;
        N01Cores[groupIndex] = core;
        N01Extensions[groupIndex] = extension;
    }
}

#endif // mode 3

#if N01_BINDING_MODE == 4

StructuredBuffer<uint> N01FollowerList : register(t0);
StructuredBuffer<FollowData> N01FollowData : register(t1);
StructuredBuffer<float4> N01RestPositions : register(t2);
StructuredBuffer<float4> N01CurrentScalp : register(t3);
StructuredBuffer<float4> N01PreviousScalp : register(t4);
StructuredBuffer<TriangleData> N01ScalpTriangles : register(t5);
StructuredBuffer<RootBindingData> N01RootBindings : register(t6);
StructuredBuffer<FollowerDirtyRange> N01FollowerDirtyRanges : register(t7);

RWStructuredBuffer<float4> N01Positions : register(u0);
RWStructuredBuffer<float4> N01PreviousPositions : register(u1);
RWStructuredBuffer<uint> N01Counters : register(u2);

void N01CounterAdd(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01Counters[index], value, ignored);
}

float3 N01EvaluateRoot(uint strandIndex, bool previousFrame)
{
    const RootBindingData binding =
        N01RootBindings[strandIndex];
    const TriangleData triIndices =
        N01ScalpTriangles[binding.triangleIndex];
    const float3 a =
        previousFrame
        ? N01PreviousScalp[triIndices.a].xyz
        : N01CurrentScalp[triIndices.a].xyz;
    const float3 b =
        previousFrame
        ? N01PreviousScalp[triIndices.b].xyz
        : N01CurrentScalp[triIndices.b].xyz;
    const float3 c =
        previousFrame
        ? N01PreviousScalp[triIndices.c].xyz
        : N01CurrentScalp[triIndices.c].xyz;
    return
        a * binding.barycentric.x +
        b * binding.barycentric.y +
        c * binding.barycentric.z;
}

float3 N01EvaluateRootNormal(uint strandIndex, bool previousFrame)
{
    const RootBindingData binding = N01RootBindings[strandIndex];
    const TriangleData triIndices = N01ScalpTriangles[binding.triangleIndex];
    const float3 a = previousFrame
        ? N01PreviousScalp[triIndices.a].xyz
        : N01CurrentScalp[triIndices.a].xyz;
    const float3 b = previousFrame
        ? N01PreviousScalp[triIndices.b].xyz
        : N01CurrentScalp[triIndices.b].xyz;
    const float3 c = previousFrame
        ? N01PreviousScalp[triIndices.c].xyz
        : N01CurrentScalp[triIndices.c].xyz;
    return N01SafeNormalize(cross(b - a, c - a), float3(0.0f, 1.0f, 0.0f));
}

float3 N01EvaluateLiftedRoot(uint strandIndex, bool previousFrame)
{
    const uint rootPoint = strandIndex * gParticlesPerStrand;
    const float3 restRoot = N01RestPositions[rootPoint].xyz;
    const float3 scalpRoot = N01EvaluateRoot(strandIndex, previousFrame);
    const float3 normal = N01EvaluateRootNormal(strandIndex, previousFrame);
    const float authoredLift = clamp(dot(restRoot - scalpRoot, normal), 0.0020f, 0.0040f);
    const float3 lifted = scalpRoot + normal * authoredLift;
    const float3 result = lerp(restRoot, lifted, saturate(gScalpMotionBlend));
    return N01Finite3(result) ? result : restRoot;
}

[numthreads(64, 1, 1)]
void N01RootBindCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    const bool followerPass = gPassKind != 0u;
    const uint followerDirtyCount =
        N01Counters[N01_COUNTER_DIRTY_FOLLOWERS];
    const uint total = followerPass
        ? followerDirtyCount
        : gGuideCount;
    if (index >= total)
    {
        return;
    }

    const uint strandIndex = followerPass
        ? N01FollowerList[index]
        : index;

    const uint rootPoint =
        strandIndex * gParticlesPerStrand;
    const float3 currentRoot =
        N01EvaluateLiftedRoot(strandIndex, false);

    N01Positions[rootPoint] =
        float4(currentRoot, 1.0f);
    N01PreviousPositions[rootPoint] =
        float4(currentRoot, 1.0f);
}

[numthreads(256, 1, 1)]
void N01FollowerLocalCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint followerDirtyCount =
        N01Counters[N01_COUNTER_DIRTY_FOLLOWERS];
    const uint pointDispatchIndex = dispatchThreadId.x;
    const uint totalPointCount =
        followerDirtyCount * gParticlesPerStrand;
    if (pointDispatchIndex >= totalPointCount)
    {
        return;
    }

    const uint dirtyFollowerIndex =
        pointDispatchIndex / gParticlesPerStrand;
    const uint localPointIndex =
        pointDispatchIndex % gParticlesPerStrand;
    if (localPointIndex == 0u)
    {
        return;
    }

    const uint strandIndex =
        N01FollowerList[dirtyFollowerIndex];
    const uint followerIndex =
        strandIndex >= gGuideCount
        ? strandIndex - gGuideCount
        : N01_INVALID;
    if (followerIndex == N01_INVALID ||
        followerIndex >= gFollowerCount)
    {
        return;
    }

    const FollowerDirtyRange dirtyRange =
        N01FollowerDirtyRanges[followerIndex];
    if (dirtyRange.dependencyVersion != gDependencyVersion ||
        dirtyRange.segmentBegin == N01_INVALID)
    {
        return;
    }
    const uint pointBegin = dirtyRange.segmentBegin;
    const uint pointEnd =
        min(dirtyRange.segmentEnd + 1u, gParticlesPerStrand - 1u);
    if (localPointIndex < pointBegin ||
        localPointIndex > pointEnd)
    {
        return;
    }

    const FollowData follow = N01FollowData[strandIndex];
    if (follow.parentGuide < 0 ||
        uint(follow.parentGuide) >= gGuideCount)
    {
        return;
    }

    const uint parentGuide = uint(follow.parentGuide);
    const uint strandStart =
        strandIndex * gParticlesPerStrand;
    const uint guideStart =
        parentGuide * gParticlesPerStrand;
    const uint pointIndex =
        strandStart + localPointIndex;
    const uint guidePointIndex =
        guideStart + localPointIndex;

    const float3 followerRoot =
        N01Positions[strandStart].xyz;
    const float3 previousFollowerRoot =
        N01PreviousPositions[strandStart].xyz;
    const float3 guideRoot =
        N01Positions[guideStart].xyz;
    const float3 previousGuideRoot =
        N01PreviousPositions[guideStart].xyz;

    const float3 followerRestRoot =
        N01RestPositions[strandStart].xyz;
    const float3 guideRestRoot =
        N01RestPositions[guideStart].xyz;
    const float3 followerRestLocal =
        N01RestPositions[pointIndex].xyz -
        followerRestRoot;
    const float3 guideRestLocal =
        N01RestPositions[guidePointIndex].xyz -
        guideRestRoot;

    const float3 guideLocal =
        N01Positions[guidePointIndex].xyz -
        guideRoot;
    const float3 previousGuideLocal =
        N01PreviousPositions[guidePointIndex].xyz -
        previousGuideRoot;

    const float followWeight =
        saturate(follow.followWeight);
    const float t =
        float(localPointIndex) /
        max(float(gParticlesPerStrand - 1u), 1.0f);
    const float clump =
        lerp(
            1.0f,
            lerp(0.42f, 0.88f, saturate(follow.clumpWeight)),
            smoothstep(0.48f, 0.92f, t));

    float3 currentLocal =
        followerRestLocal +
        (guideLocal - guideRestLocal) *
        followWeight;
    float3 previousLocal =
        followerRestLocal +
        (previousGuideLocal - guideRestLocal) *
        followWeight;
    currentLocal = lerp(guideLocal, currentLocal, clump);
    previousLocal =
        lerp(previousGuideLocal, previousLocal, clump);

    const float seed =
        frac(
            sin((float(strandIndex) + 1.0f) * 12.9898f) *
            43758.5453f);
    const float phase =
        float(gFrameIndex) *
        gDeltaTime *
        lerp(1.15f, 2.05f, seed) +
        seed * 6.2831853f;
    const float noiseAmplitude =
        min(
            (0.00035f + follow.noiseWeight * 0.0022f) *
            smoothstep(0.18f, 1.0f, t),
            max(
                length(followerRestLocal - guideRestLocal) *
                    0.10f,
                0.00018f));
    const float3 noise =
        float3(
            sin(phase),
            0.35f * sin(phase * 0.73f + 1.2f),
            cos(phase * 1.11f)) *
        noiseAmplitude;

    const float3 target =
        followerRoot + currentLocal + noise;
    const float3 previousTarget =
        previousFollowerRoot + previousLocal;

    float3 current =
        N01Positions[pointIndex].xyz;
    float3 previous =
        N01PreviousPositions[pointIndex].xyz;
    if (!N01Finite3(current) ||
        !N01Finite3(previous))
    {
        current = target;
        previous = previousTarget;
    }

    float3 velocity = current - previous;
    const float maximumVelocity =
        lerp(0.0035f, 0.0200f, t);
    const float velocityLength = length(velocity);
    if (velocityLength > maximumVelocity &&
        velocityLength > 1.0e-7f)
    {
        velocity *=
            maximumVelocity / velocityLength;
    }

    const float damping =
        lerp(0.28f, 0.80f, t);
    const float response =
        lerp(0.74f, 0.16f, t);
    float3 predicted =
        current +
        velocity * damping +
        (target - current) * response;

    float residual = 0.0f;
    predicted =
        N01ResolveExternalCollision(
            predicted,
            residual);

    if (!N01Finite3(predicted))
    {
        predicted = target;
        current = previousTarget;
    }

    N01Positions[pointIndex] =
        float4(predicted, 1.0f);
    N01PreviousPositions[pointIndex] =
        float4(current, 1.0f);
}

#endif // mode 4

#if N01_BINDING_MODE == 5

StructuredBuffer<SegmentGroupCore> N01CoresIn : register(t0);
StructuredBuffer<FollowData> N01FollowDataIn : register(t1);

RWStructuredBuffer<uint4> N01FollowerFlags : register(u0);
RWStructuredBuffer<uint> N01Counters : register(u1);

[numthreads(64, 1, 1)]
void N01BuildFollowerFlagsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint followerIndex = dispatchThreadId.x;
    if (followerIndex >= gFollowerCount)
    {
        return;
    }

    const uint strandIndex = gGuideCount + followerIndex;
    const FollowData follow = N01FollowDataIn[strandIndex];
    uint dirty = 0u;

    if (follow.parentGuide < 0 ||
        uint(follow.parentGuide) >= gGuideCount)
    {
        dirty = 1u;
    }
    else if ((gRuntimeFlags &
             ((1u << 0u) | (1u << 3u) | (1u << 4u) |
              (1u << 5u) | (1u << 6u))) != 0u)
    {
        dirty = 1u;
    }
    else
    {
        const uint firstGroup =
            uint(follow.parentGuide) *
            max(gParticlesPerStrand - 1u, 1u);
        const uint groupCount =
            max(gParticlesPerStrand - 1u, 1u);

        [loop]
        for (uint localGroup = 0u;
             localGroup < groupCount;
             ++localGroup)
        {
            const uint groupIndex = firstGroup + localGroup;
            if (groupIndex >= gGroupCount)
            {
                dirty = 1u;
                break;
            }
            const SegmentGroupCore core = N01CoresIn[groupIndex];
            const bool requiresUpdate =
                core.state != N01_STATE_STATIC_REUSE ||
                core.dirtyFlags != 0u ||
                (core.flags & N01_CORE_BASELINE_FALLBACK) != 0u;
            if (requiresUpdate)
            {
                dirty = 1u;
                break;
            }
        }
    }

    N01FollowerFlags[followerIndex] =
        uint4(dirty, 0u, 0u, 0u);
}

#endif // mode 5

#if N01_BINDING_MODE == 6

StructuredBuffer<SegmentGroupCore> N01CoresIn : register(t0);
StructuredBuffer<CoreExtension> N01ExtensionsIn : register(t1);
StructuredBuffer<ReuseDescriptor> N01DescriptorsIn : register(t2);

RWStructuredBuffer<float4> N01Positions : register(u0);
RWStructuredBuffer<float4> N01PreviousPositions : register(u1);
RWStructuredBuffer<SharedPointCandidate> N01Candidates : register(u2);
RWStructuredBuffer<uint> N01Counters : register(u3);

void N01CounterAdd(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01Counters[index], value, ignored);
}

uint N01PriorityForState(
    uint state,
    uint dirtyFlags,
    float collisionResidual)
{
    if (state == N01_STATE_INVALID)
    {
        return 1000u;
    }
    if ((dirtyFlags & N01_DIRTY_COLLISION) != 0u ||
        collisionResidual > 0.0f)
    {
        return 900u;
    }
    if ((dirtyFlags & N01_DIRTY_EDIT) != 0u)
    {
        return 800u;
    }
    if (state == N01_STATE_FORCED_DYNAMIC)
    {
        return 700u;
    }
    if ((dirtyFlags & N01_DIRTY_WATCHDOG) != 0u)
    {
        return 650u;
    }
    if (state == N01_STATE_DYNAMIC)
    {
        return 600u;
    }
    if (state == N01_STATE_REPAIR)
    {
        return 500u;
    }
    if (state == N01_STATE_STATIC_TRANSFORM)
    {
        return 300u;
    }
    return 200u;
}

[numthreads(64, 1, 1)]
void N01BuildSharedCandidatesCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    const SegmentGroupCore core = N01CoresIn[groupIndex];
    const ReuseDescriptor descriptor =
        N01DescriptorsIn[groupIndex];
    const float3 sharedPosition =
        N01Positions[core.endPointIndex].xyz;

    SharedPointCandidate owner = (SharedPointCandidate)0;
    owner.position = sharedPosition;
    owner.positionErrorMm = descriptor.positionErrorMm;
    owner.constraintResidual =
        descriptor.constraintResidual;
    owner.collisionResidual =
        max(
            0.0f,
            gCollisionSafetyPaddingMm -
            descriptor.collisionMarginMm);
    owner.sourceGroupIndex = groupIndex;
    owner.priority =
        N01PriorityForState(
            core.state,
            core.dirtyFlags,
            owner.collisionResidual);
    owner.targetPointIndex = core.endPointIndex;
    owner.finite = N01Finite3(owner.position) ? 1u : 0u;
    owner.valid = 1u;
    N01Candidates[groupIndex * 2u + 0u] = owner;

    SharedPointCandidate childStart = (SharedPointCandidate)0;
    childStart.position =
        N01Positions[core.startPointIndex].xyz;
    childStart.positionErrorMm = descriptor.positionErrorMm;
    childStart.constraintResidual = descriptor.constraintResidual;
    childStart.collisionResidual =
        max(
            0.0f,
            gCollisionSafetyPaddingMm -
            descriptor.collisionMarginMm);
    childStart.sourceGroupIndex = groupIndex;
    childStart.priority =
        N01PriorityForState(
            core.state,
            core.dirtyFlags,
            childStart.collisionResidual);
    childStart.targetPointIndex = core.startPointIndex;
    childStart.finite = N01Finite3(childStart.position) ? 1u : 0u;
    childStart.valid =
        core.parentGroupIndex != N01_INVALID ? 1u : 0u;
    N01Candidates[groupIndex * 2u + 1u] = childStart;
}

bool N01CandidateBetter(
    SharedPointCandidate a,
    SharedPointCandidate b)
{
    if (a.valid != b.valid)
    {
        return a.valid > b.valid;
    }
    if (a.finite != b.finite)
    {
        return a.finite > b.finite;
    }
    if (a.priority != b.priority)
    {
        return a.priority > b.priority;
    }
    if (a.positionErrorMm != b.positionErrorMm)
    {
        return a.positionErrorMm < b.positionErrorMm;
    }
    if (a.constraintResidual != b.constraintResidual)
    {
        return a.constraintResidual < b.constraintResidual;
    }
    if (a.collisionResidual != b.collisionResidual)
    {
        return a.collisionResidual < b.collisionResidual;
    }
    return a.sourceGroupIndex < b.sourceGroupIndex;
}

[numthreads(64, 1, 1)]
void N01ResolveSharedPointsCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    const SegmentGroupCore core = N01CoresIn[groupIndex];
    SharedPointCandidate owner =
        N01Candidates[groupIndex * 2u + 0u];
    SharedPointCandidate child = (SharedPointCandidate)0;
    if (core.childGroupIndex != N01_INVALID &&
        core.childGroupIndex < gGroupCount)
    {
        child =
            N01Candidates[core.childGroupIndex * 2u + 1u];
    }

    SharedPointCandidate winner = owner;
    if (N01FeatureEnabled(N01_FEATURE_STABLE_SHARED_POINT) &&
        N01CandidateBetter(child, owner))
    {
        winner = child;
    }

    if (owner.valid != 0u &&
        child.valid != 0u &&
        length(owner.position - child.position) * 1000.0f >
            gBoundaryErrorThresholdMm)
    {
        N01CounterAdd(N01_COUNTER_SHARED_CONFLICTS, 1u);
    }

    if (winner.valid != 0u &&
        winner.finite != 0u &&
        winner.targetPointIndex != N01_INVALID)
    {
        const float4 previous =
            N01PreviousPositions[winner.targetPointIndex];
        N01Positions[winner.targetPointIndex] =
            float4(winner.position, 1.0f);
        if (!N01Finite3(previous.xyz))
        {
            N01PreviousPositions[winner.targetPointIndex] =
                float4(winner.position, 1.0f);
        }
    }
}

#endif // mode 6

#if N01_BINDING_MODE == 7

StructuredBuffer<SegmentGroupCore> N01LifecycleCoresIn : register(t0);
StructuredBuffer<CoreExtension> N01LifecycleExtensionsIn : register(t1);
StructuredBuffer<PayloadOwner> N01OldPayloadOwners : register(t2);
StructuredBuffer<uint> N01LifecycleDynamicList : register(t3);
StructuredBuffer<uint> N01LifecycleCountersIn : register(t4);

RWStructuredBuffer<SegmentGroupCore> N01LifecycleCoresOut : register(u0);
RWStructuredBuffer<CoreExtension> N01LifecycleExtensionsOut : register(u1);
RWStructuredBuffer<PayloadOwner> N01NewPayloadOwners : register(u2);
RWStructuredBuffer<uint> N01LifecycleCounters : register(u3);

void N01LifecycleCounter(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01LifecycleCounters[index], value, ignored);
}

[numthreads(64, 1, 1)]
void N01ValidatePayloadOwnersCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount ||
        !N01FeatureEnabled(N01_FEATURE_PAYLOAD_OWNER))
    {
        return;
    }

    SegmentGroupCore core = N01LifecycleCoresOut[groupIndex];
    CoreExtension extension = N01LifecycleExtensionsOut[groupIndex];
    const bool expectsPayload =
        core.state == N01_STATE_DYNAMIC ||
        core.state == N01_STATE_FORCED_DYNAMIC;

    if (!expectsPayload)
    {
        if ((core.flags & N01_CORE_PAYLOAD_VALID) != 0u ||
            extension.payloadIndex != N01_INVALID)
        {
            core.flags &= ~N01_CORE_PAYLOAD_VALID;
            extension.payloadIndex = N01_INVALID;
            core.dirtyFlags |= N01_DIRTY_PAYLOAD;
            N01LifecycleCounter(N01_COUNTER_DESCRIPTOR_VIOLATION, 1u);
        }
        N01LifecycleCoresOut[groupIndex] = core;
        N01LifecycleExtensionsOut[groupIndex] = extension;
        return;
    }

    if ((core.flags & N01_CORE_PAYLOAD_VALID) == 0u)
    {
        return;
    }

    if (extension.payloadIndex >= gPayloadCapacity)
    {
        core.flags &= ~N01_CORE_PAYLOAD_VALID;
        core.flags |= N01_CORE_BASELINE_FALLBACK;
        core.dirtyFlags |= N01_DIRTY_PAYLOAD;
        extension.payloadIndex = N01_INVALID;
        N01LifecycleCounter(N01_COUNTER_INVALID_PAYLOAD_INDEX, 1u);
        N01LifecycleCounter(N01_COUNTER_FALLBACK, 1u);
        N01LifecycleCoresOut[groupIndex] = core;
        N01LifecycleExtensionsOut[groupIndex] = extension;
        return;
    }

    const PayloadOwner owner =
        N01OldPayloadOwners[extension.payloadIndex];
    const bool ownerMismatch =
        owner.groupIndex != groupIndex ||
        owner.generation != extension.payloadGeneration ||
        owner.state != 1u;
    const bool versionMismatch =
        owner.payloadVersion != extension.payloadVersion;

    if (ownerMismatch || versionMismatch)
    {
        core.flags &= ~N01_CORE_PAYLOAD_VALID;
        core.flags |= N01_CORE_FORCED_DYNAMIC;
        core.dirtyFlags |= N01_DIRTY_PAYLOAD;
        extension.payloadIndex = N01_INVALID;
        if (ownerMismatch)
        {
            N01LifecycleCounter(N01_COUNTER_PAYLOAD_OWNER_MISMATCH, 1u);
        }
        if (versionMismatch)
        {
            N01LifecycleCounter(N01_COUNTER_PAYLOAD_VERSION_MISMATCH, 1u);
        }
        N01LifecycleCoresOut[groupIndex] = core;
        N01LifecycleExtensionsOut[groupIndex] = extension;
    }
}

[numthreads(64, 1, 1)]
void N01WritePayloadOwnersCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    SegmentGroupCore core = N01LifecycleCoresOut[groupIndex];
    CoreExtension extension =
        N01LifecycleExtensionsOut[groupIndex];

    const bool dynamicState =
        core.state == N01_STATE_DYNAMIC ||
        core.state == N01_STATE_FORCED_DYNAMIC;
    const bool payloadValid =
        (core.flags & N01_CORE_PAYLOAD_VALID) != 0u &&
        extension.payloadIndex != N01_INVALID &&
        extension.payloadIndex < gPayloadCapacity;

    if (!dynamicState || !payloadValid)
    {
        return;
    }

    const uint targetPayloadIndex = extension.payloadIndex;

    PayloadOwner newOwner = (PayloadOwner)0;
    newOwner.groupIndex = groupIndex;
    newOwner.payloadVersion = extension.payloadVersion;
    newOwner.generation = extension.payloadGeneration;
    newOwner.allocationEpoch = gFrameIndex;
    newOwner.retireEpoch = 0u;
    newOwner.safeReuseEpoch =
        N01FeatureEnabled(N01_FEATURE_DELAYED_REUSE)
        ? gFrameIndex + gPayloadRetireDelayFrames
        : gFrameIndex;
    newOwner.state = 1u;

    const PayloadOwner oldSlot =
        N01OldPayloadOwners[targetPayloadIndex];
    if (oldSlot.state == 1u &&
        oldSlot.groupIndex != groupIndex &&
        gFrameIndex < oldSlot.safeReuseEpoch)
    {
        core.flags |= N01_CORE_BASELINE_FALLBACK;
        core.flags &= ~N01_CORE_PAYLOAD_VALID;
        core.dirtyFlags |= N01_DIRTY_PAYLOAD;
        extension.payloadIndex = N01_INVALID;
        N01LifecycleCounter(
            N01_COUNTER_DUPLICATE_PAYLOAD_OWNER,
            1u);
        N01LifecycleCounter(N01_COUNTER_FALLBACK, 1u);
    }
    else
    {
        if (oldSlot.state == 3u ||
            (oldSlot.state == 2u &&
             gFrameIndex >= oldSlot.safeReuseEpoch))
        {
            N01LifecycleCounter(
                N01_COUNTER_PAYLOAD_REUSED,
                1u);
        }
        else
        {
            N01LifecycleCounter(
                N01_COUNTER_PAYLOAD_ALLOCATED,
                1u);
        }

        core.flags |= N01_CORE_PAYLOAD_VALID;
        core.flags &= ~N01_CORE_BASELINE_FALLBACK;
        N01NewPayloadOwners[targetPayloadIndex] = newOwner;
    }

    N01LifecycleCoresOut[groupIndex] = core;
    N01LifecycleExtensionsOut[groupIndex] = extension;
}

[numthreads(64, 1, 1)]
void N01RetirePayloadOwnersCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint payloadIndex = dispatchThreadId.x;
    if (payloadIndex >= gPayloadCapacity)
    {
        return;
    }

    PayloadOwner owner = N01OldPayloadOwners[payloadIndex];
    if (!N01FeatureEnabled(N01_FEATURE_OPTIONAL_PAYLOAD) &&
        owner.groupIndex < gGroupCount)
    {
        SegmentGroupCore core = N01LifecycleCoresOut[owner.groupIndex];
        CoreExtension extension = N01LifecycleExtensionsOut[owner.groupIndex];
        owner.generation = gPayloadGeneration;
        owner.state = 1u;
        owner.retireEpoch = 0u;
        owner.safeReuseEpoch = gFrameIndex + gPayloadRetireDelayFrames;
        extension.payloadIndex = payloadIndex;
        extension.payloadGeneration = gPayloadGeneration;
        core.flags |= N01_CORE_PAYLOAD_VALID;
        N01LifecycleCoresOut[owner.groupIndex] = core;
        N01LifecycleExtensionsOut[owner.groupIndex] = extension;
        N01NewPayloadOwners[payloadIndex] = owner;
        return;
    }
    if (owner.state == 0u)
    {
        N01NewPayloadOwners[payloadIndex] = owner;
        return;
    }

    bool remainsActive = false;
    if (owner.groupIndex < gGroupCount)
    {
        const SegmentGroupCore core =
            N01LifecycleCoresOut[owner.groupIndex];
        const CoreExtension extension =
            N01LifecycleExtensionsOut[owner.groupIndex];
        remainsActive =
            (core.flags & N01_CORE_PAYLOAD_VALID) != 0u &&
            extension.payloadIndex == payloadIndex &&
            extension.payloadGeneration == owner.generation &&
            extension.payloadVersion == owner.payloadVersion &&
            core.state == N01_STATE_DYNAMIC;
    }

    if (!remainsActive && owner.state == 1u)
    {
        owner.state = 2u;
        owner.retireEpoch = gFrameIndex;
        owner.safeReuseEpoch =
            N01FeatureEnabled(N01_FEATURE_DELAYED_REUSE)
            ? gFrameIndex + gPayloadRetireDelayFrames
            : gFrameIndex;
        N01LifecycleCounter(N01_COUNTER_PAYLOAD_RETIRED, 1u);
    }
    else if (owner.state == 2u &&
             gFrameIndex >= owner.safeReuseEpoch)
    {
        owner.state = 3u;
    }

    N01NewPayloadOwners[payloadIndex] = owner;
}

#endif // mode 7

#if N01_BINDING_MODE == 8

StructuredBuffer<SegmentGroupCore> N01RangeCores : register(t0);
StructuredBuffer<FollowData> N01RangeFollowData : register(t1);
StructuredBuffer<GuideDirtyRange> N01GuideRangesIn : register(t2);
StructuredBuffer<uint> N01RangeCountersIn : register(t3);

RWStructuredBuffer<GuideDirtyRange> N01GuideRangesOut : register(u0);
RWStructuredBuffer<FollowerDirtyRange> N01FollowerRangesOut : register(u1);
RWStructuredBuffer<RenderDirtyRange> N01RenderRangesOut : register(u2);
RWStructuredBuffer<uint4> N01RangeFollowerFlags : register(u3);
RWStructuredBuffer<uint> N01RangeCounters : register(u4);

void N01RangeCounter(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01RangeCounters[index], value, ignored);
}

[numthreads(64, 1, 1)]
void N01ClearGuideDirtyRangesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint guideIndex = dispatchThreadId.x;
    if (guideIndex >= gGuideCount)
    {
        return;
    }

    GuideDirtyRange range;
    range.segmentBegin = N01_INVALID;
    range.segmentEnd = 0u;
    range.flags = 0u;
    range.dependencyVersion = gDependencyVersion;
    N01GuideRangesOut[guideIndex] = range;
}

[numthreads(64, 1, 1)]
void N01BuildGuideDirtyRangesCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    const SegmentGroupCore core = N01RangeCores[groupIndex];
    const bool changed =
        !N01FeatureEnabled(N01_FEATURE_INCREMENTAL_BLOCKS) ||
        core.state == N01_STATE_DYNAMIC ||
        core.state == N01_STATE_REPAIR ||
        core.state == N01_STATE_FORCED_DYNAMIC ||
        core.state == N01_STATE_INVALID ||
        core.state == N01_STATE_STATIC_TRANSFORM ||
        core.dirtyFlags != 0u;

    if (!changed || core.strandId >= gGuideCount)
    {
        return;
    }

    uint ignored;
    InterlockedMin(
        N01GuideRangesOut[core.strandId].segmentBegin,
        core.localGroupIndex,
        ignored);
    InterlockedMax(
        N01GuideRangesOut[core.strandId].segmentEnd,
        core.localGroupIndex,
        ignored);
    InterlockedOr(
        N01GuideRangesOut[core.strandId].flags,
        core.dirtyFlags | 1u,
        ignored);
}

[numthreads(64, 1, 1)]
void N01PropagateFollowerRenderRangesCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint followerIndex = dispatchThreadId.x;
    if (followerIndex >= gFollowerCount)
    {
        return;
    }

    const uint strandIndex = gGuideCount + followerIndex;
    const FollowData follow = N01RangeFollowData[strandIndex];
    FollowerDirtyRange followerRange = (FollowerDirtyRange)0;
    RenderDirtyRange renderRange = (RenderDirtyRange)0;
    uint4 activeFlag = uint4(0u, 0u, 0u, 0u);

    followerRange.followerStrandIndex = strandIndex;
    followerRange.sourceGuideIndex =
        follow.parentGuide >= 0
        ? uint(follow.parentGuide)
        : N01_INVALID;
    followerRange.segmentBegin = N01_INVALID;
    followerRange.segmentEnd = 0u;
    followerRange.dependencyVersion = gDependencyVersion;
    followerRange.guardBand = 1u;

    renderRange.strandIndex = strandIndex;
    renderRange.pointBegin = N01_INVALID;
    renderRange.pointEnd = 0u;

    const bool dependencyEnabled =
        N01FeatureEnabled(N01_FEATURE_FOLLOWER_DEPENDENCY);
    const bool dependencyValid =
        dependencyEnabled &&
        follow.parentGuide >= 0 &&
        uint(follow.parentGuide) < gGuideCount;

    if (!dependencyValid)
    {
        followerRange.segmentBegin = 0u;
        followerRange.segmentEnd =
            max(gParticlesPerStrand, 1u) - 2u;
        followerRange.flags = N01_DIRTY_DEPENDENCY;
        renderRange.pointBegin = 0u;
        renderRange.pointEnd =
            max(gParticlesPerStrand, 1u) - 1u;
        renderRange.flags = N01_DIRTY_DEPENDENCY;
        activeFlag.x = 1u;
        N01RangeCounter(N01_COUNTER_DEPENDENCY_FALLBACK, 1u);
    }
    else
    {
        const GuideDirtyRange guideRange =
            N01GuideRangesIn[uint(follow.parentGuide)];
        if (guideRange.segmentBegin != N01_INVALID &&
            guideRange.dependencyVersion == gDependencyVersion)
        {
            const uint maximumSegment =
                max(gParticlesPerStrand, 1u) - 2u;
            const uint begin =
                guideRange.segmentBegin > followerRange.guardBand
                ? guideRange.segmentBegin - followerRange.guardBand
                : 0u;
            const uint end = min(
                guideRange.segmentEnd + followerRange.guardBand,
                maximumSegment);
            followerRange.segmentBegin = begin;
            followerRange.segmentEnd = end;
            followerRange.flags = guideRange.flags;
            renderRange.pointBegin = begin;
            renderRange.pointEnd =
                min(end + 1u, gParticlesPerStrand - 1u);
            renderRange.flags = guideRange.flags;
            activeFlag.x = 1u;

            const uint dirtyPoints =
                renderRange.pointEnd >= renderRange.pointBegin
                ? renderRange.pointEnd - renderRange.pointBegin + 1u
                : 0u;
            N01RangeCounter(N01_COUNTER_FOLLOWER_REBUILT_POINTS, dirtyPoints);
            N01RangeCounter(
                N01_COUNTER_RENDER_REGENERATED_VERTICES,
                dirtyPoints * 2u);
        }
    }

    N01FollowerRangesOut[followerIndex] = followerRange;
    N01RenderRangesOut[followerIndex] = renderRange;
    N01RangeFollowerFlags[followerIndex] = activeFlag;
    if (activeFlag.x != 0u)
    {
        N01RangeCounter(N01_COUNTER_FOLLOWER_DIRTY_RANGE, 1u);
        N01RangeCounter(N01_COUNTER_RENDER_DIRTY_RANGE, 1u);
    }
}

[numthreads(64, 1, 1)]
void N01FinalizeGuideDirtyRangeCountersCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint guideIndex = dispatchThreadId.x;
    if (guideIndex >= gGuideCount)
    {
        return;
    }

    const GuideDirtyRange range = N01GuideRangesIn[guideIndex];
    if (range.segmentBegin != N01_INVALID)
    {
        N01RangeCounter(N01_COUNTER_GUIDE_DIRTY_RANGE, 1u);
    }
}

#endif // mode 8

#if N01_BINDING_MODE == 9

StructuredBuffer<float4> N01ProposalPositions : register(t0);
StructuredBuffer<float4> N01GroundTruthPositions : register(t1);
StructuredBuffer<float4> N01FormalRestPositions : register(t2);
StructuredBuffer<CoreExtension> N01FormalExtensions : register(t4);
StructuredBuffer<PayloadOwner> N01FormalOwners : register(t5);

RWStructuredBuffer<GroundTruthMetric> N01GroundTruthMetrics : register(u0);
RWStructuredBuffer<SegmentGroupCore> N01FormalCoresOut : register(u1);
RWStructuredBuffer<uint> N01FormalCounters : register(u2);

void N01FormalCounter(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01FormalCounters[index], value, ignored);
}

[numthreads(64, 1, 1)]
void N01CompareGroundTruthCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    SegmentGroupCore core = N01FormalCoresOut[groupIndex];
    const float3 proposalStart =
        N01ProposalPositions[core.startPointIndex].xyz;
    const float3 proposalEnd =
        N01ProposalPositions[core.endPointIndex].xyz;
    const float3 truthStart =
        N01GroundTruthPositions[core.startPointIndex].xyz;
    const float3 truthEnd =
        N01GroundTruthPositions[core.endPointIndex].xyz;
    const float3 restStart =
        N01FormalRestPositions[core.startPointIndex].xyz;
    const float3 restEnd =
        N01FormalRestPositions[core.endPointIndex].xyz;

    GroundTruthMetric metric = (GroundTruthMetric)0;
    const float positionErrorMm =
        length(proposalEnd - truthEnd) * 1000.0f;
    metric.squaredPositionErrorMm2 =
        positionErrorMm * positionErrorMm;
    metric.maximumPositionErrorMm =
        max(
            positionErrorMm,
            length(proposalStart - truthStart) * 1000.0f);
    const float restLength =
        max(length(restEnd - restStart), 1.0e-6f);
    metric.segmentLengthErrorRatio =
        abs(length(proposalEnd - proposalStart) - restLength) /
        restLength;
    metric.boundaryErrorMm =
        length(
            (proposalEnd - proposalStart) -
            (truthEnd - truthStart)) *
        1000.0f;
    metric.staticGroup =
        core.state <= N01_STATE_STATIC_TRANSFORM ? 1u : 0u;
    metric.falseStatic =
        metric.staticGroup != 0u &&
        metric.maximumPositionErrorMm > gShadowErrorThresholdMm
        ? 1u
        : 0u;

    if (!N01Finite3(proposalStart) ||
        !N01Finite3(proposalEnd))
    {
        core.state = N01_STATE_INVALID;
        core.dirtyFlags |= N01_DIRTY_NONFINITE;
        N01FormalCounter(N01_COUNTER_NAN, 1u);
        N01FormalCounter(N01_COUNTER_GROUND_TRUTH_VIOLATIONS, 1u);
    }

    if (metric.falseStatic != 0u)
    {
        core.flags |= N01_CORE_FORCED_DYNAMIC;
        core.dirtyFlags |= N01_DIRTY_WATCHDOG;
        N01FormalCounter(N01_COUNTER_GROUND_TRUTH_FALSE_STATIC, 1u);
    }

    const bool proposalDynamic =
        core.state == N01_STATE_DYNAMIC ||
        core.state == N01_STATE_FORCED_DYNAMIC ||
        core.state == N01_STATE_REPAIR;
    const bool groundTruthWouldPermitStatic =
        metric.maximumPositionErrorMm <= gStaticPositionThresholdMm &&
        metric.segmentLengthErrorRatio <= gLengthErrorThresholdRatio &&
        metric.boundaryErrorMm <= gBoundaryErrorThresholdMm;
    if (proposalDynamic && groundTruthWouldPermitStatic)
    {
        N01FormalCounter(N01_COUNTER_FALSE_DYNAMIC, 1u);
    }

    core.flags |= N01_CORE_GROUND_TRUTH_SAMPLE;
    N01GroundTruthMetrics[groupIndex] = metric;
    N01FormalCoresOut[groupIndex] = core;
    N01FormalCounter(N01_COUNTER_GROUND_TRUTH_SAMPLES, 1u);
}

[numthreads(64, 1, 1)]
void N01VerifyInvariantsCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    SegmentGroupCore core = N01FormalCoresOut[groupIndex];
    const CoreExtension extension = N01FormalExtensions[groupIndex];
    bool violation = false;

    const bool isStatic =
        core.state <= N01_STATE_STATIC_TRANSFORM;
    const bool isDynamic =
        core.state == N01_STATE_DYNAMIC ||
        core.state == N01_STATE_FORCED_DYNAMIC;

    if (isStatic &&
        ((core.flags & N01_CORE_PAYLOAD_VALID) != 0u ||
         extension.payloadIndex != N01_INVALID))
    {
        violation = true;
    }

    if (isDynamic &&
        (core.flags & N01_CORE_BASELINE_FALLBACK) == 0u)
    {
        if ((core.flags & N01_CORE_PAYLOAD_VALID) == 0u ||
            extension.payloadIndex >= gPayloadCapacity)
        {
            violation = true;
            N01FormalCounter(N01_COUNTER_INVALID_PAYLOAD_INDEX, 1u);
        }
        else if (N01FeatureEnabled(N01_FEATURE_PAYLOAD_OWNER))
        {
            const PayloadOwner owner =
                N01FormalOwners[extension.payloadIndex];
            if (owner.groupIndex != groupIndex ||
                owner.payloadVersion != extension.payloadVersion ||
                owner.generation != extension.payloadGeneration ||
                owner.state != 1u)
            {
                violation = true;
                N01FormalCounter(N01_COUNTER_PAYLOAD_OWNER_MISMATCH, 1u);
            }
        }
    }

    if (violation)
    {
        core.state = N01_STATE_REPAIR;
        core.dirtyFlags |= N01_DIRTY_PAYLOAD;
        core.flags |= N01_CORE_FORCED_DYNAMIC;
        N01FormalCounter(N01_COUNTER_DESCRIPTOR_VIOLATION, 1u);
        N01FormalCoresOut[groupIndex] = core;
    }
}

#endif // mode 9

#if N01_BINDING_MODE == 10

StructuredBuffer<uint> N01ArgumentCounters : register(t0);
StructuredBuffer<uint> N01MigrationDynamicList : register(t1);
StructuredBuffer<uint> N01MigrationStaticList : register(t2);
StructuredBuffer<SegmentGroupCore> N01MigrationCores : register(t3);

RWByteAddressBuffer N01DispatchArguments : register(u0);
RWStructuredBuffer<SegmentGroupCore> N01MigratingDynamicCores : register(u1);
RWStructuredBuffer<SegmentGroupCore> N01MigratingStaticCores : register(u2);
RWStructuredBuffer<uint> N01ArgumentCountersOut : register(u3);

uint N01DispatchGroupCount(uint itemCount, uint threads)
{
    return itemCount == 0u
        ? 0u
        : (itemCount + threads - 1u) / threads;
}

[numthreads(1, 1, 1)]
void N01BuildIndirectArgumentsCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint dynamicCount =
        N01ArgumentCounters[N01_COUNTER_DYNAMIC];
    const uint staticCount =
        N01ArgumentCounters[N01_COUNTER_STATIC_REUSE];
    const uint repairCount =
        N01ArgumentCounters[N01_COUNTER_REPAIR];
    const uint followerCount =
        N01ArgumentCounters[N01_COUNTER_DIRTY_FOLLOWERS];

    N01DispatchArguments.Store3(
        0u,
        uint3(N01DispatchGroupCount(dynamicCount, 64u), 1u, 1u));
    N01DispatchArguments.Store3(
        12u,
        uint3(N01DispatchGroupCount(staticCount, 64u), 1u, 1u));
    N01DispatchArguments.Store3(
        24u,
        uint3(N01DispatchGroupCount(repairCount, 64u), 1u, 1u));
    N01DispatchArguments.Store3(
        36u,
        uint3(N01DispatchGroupCount(followerCount, 64u), 1u, 1u));
    N01DispatchArguments.Store3(
        48u,
        uint3(
            N01DispatchGroupCount(
                followerCount * gParticlesPerStrand,
                256u),
            1u,
            1u));
    N01DispatchArguments.Store3(
        60u,
        uint3(N01DispatchGroupCount(gGroupCount, 64u), 1u, 1u));
}

[numthreads(64, 1, 1)]
void N01MigrateCoreBuffersCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    const uint dynamicCount =
        N01ArgumentCounters[N01_COUNTER_DYNAMIC];
    const uint staticCount =
        N01ArgumentCounters[N01_COUNTER_STATIC_REUSE];

    if (index < dynamicCount)
    {
        const uint groupIndex =
            N01MigrationDynamicList[index];
        N01MigratingDynamicCores[index] =
            N01MigrationCores[groupIndex];
    }

    if (index < staticCount)
    {
        const uint groupIndex =
            N01MigrationStaticList[index];
        N01MigratingStaticCores[index] =
            N01MigrationCores[groupIndex];
    }

    if (index == 0u)
    {
        N01ArgumentCountersOut[N01_COUNTER_STATE_COPY_BYTES] =
            (dynamicCount + staticCount) * 64u;
    }
}

#endif // mode 10
#if N01_BINDING_MODE == 11

StructuredBuffer<uint4> N01AtomicFlags : register(t0);
RWStructuredBuffer<uint> N01AtomicCounters : register(u0);
RWStructuredBuffer<uint> N01AtomicDynamicList : register(u1);
RWStructuredBuffer<uint> N01AtomicStaticList : register(u2);
RWStructuredBuffer<uint> N01AtomicRepairList : register(u3);
RWStructuredBuffer<uint> N01AtomicInvalidList : register(u4);

[numthreads(1, 1, 1)]
void N01ClearAtomicCompactionCountersCS(
    uint3 dispatchThreadId : SV_DispatchThreadID)
{
    N01AtomicCounters[N01_COUNTER_DYNAMIC] = 0u;
    N01AtomicCounters[N01_COUNTER_STATIC_REUSE] = 0u;
    N01AtomicCounters[N01_COUNTER_REPAIR] = 0u;
    N01AtomicCounters[N01_COUNTER_INVALID] = 0u;
}

[numthreads(64, 1, 1)]
void N01AtomicCompactCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount)
    {
        return;
    }

    const uint4 flags = N01AtomicFlags[groupIndex];
    uint slot = 0u;
    if (flags.x != 0u)
    {
        InterlockedAdd(
            N01AtomicCounters[N01_COUNTER_DYNAMIC],
            1u,
            slot);
        N01AtomicDynamicList[slot] = groupIndex;
    }
    if (flags.y != 0u)
    {
        InterlockedAdd(
            N01AtomicCounters[N01_COUNTER_STATIC_REUSE],
            1u,
            slot);
        N01AtomicStaticList[slot] = groupIndex;
    }
    if (flags.z != 0u)
    {
        InterlockedAdd(
            N01AtomicCounters[N01_COUNTER_REPAIR],
            1u,
            slot);
        N01AtomicRepairList[slot] = groupIndex;
    }
    if (flags.w != 0u)
    {
        InterlockedAdd(
            N01AtomicCounters[N01_COUNTER_INVALID],
            1u,
            slot);
        N01AtomicInvalidList[slot] = groupIndex;
    }
}

#endif // mode 11

#if N01_BINDING_MODE == 12

RWStructuredBuffer<float4> N01ScenarioPositions : register(u0);
RWStructuredBuffer<float4> N01ScenarioPreviousPositions : register(u1);
RWStructuredBuffer<SegmentGroupCore> N01ScenarioCores : register(u2);
RWStructuredBuffer<CoreExtension> N01ScenarioExtensions : register(u3);
RWStructuredBuffer<ReuseDescriptor> N01ScenarioDescriptors : register(u4);
RWStructuredBuffer<uint> N01ScenarioCounters : register(u5);

void N01ScenarioCounter(uint index, uint value)
{
    uint ignored;
    InterlockedAdd(N01ScenarioCounters[index], value, ignored);
}

void N01ForceScenarioDynamic(
    uint groupIndex,
    uint dirtyFlags,
    float3 displacement)
{
    if (groupIndex >= gGroupCount)
    {
        return;
    }
    SegmentGroupCore core = N01ScenarioCores[groupIndex];
    CoreExtension extension = N01ScenarioExtensions[groupIndex];
    ReuseDescriptor descriptor = N01ScenarioDescriptors[groupIndex];
    const uint pointIndex = core.endPointIndex;
    const float3 oldPosition = N01ScenarioPositions[pointIndex].xyz;
    const float3 newPosition = oldPosition + displacement;
    if (N01Finite3(newPosition))
    {
        N01ScenarioPreviousPositions[pointIndex] = float4(oldPosition, 1.0f);
        N01ScenarioPositions[pointIndex] = float4(newPosition, 1.0f);
    }
    core.previousState = core.state;
    core.state = N01_STATE_FORCED_DYNAMIC;
    core.flags |= N01_CORE_FORCED_DYNAMIC;
    core.dirtyFlags |= dirtyFlags;
    extension.minimumResidenceEndFrame = gFrameIndex + gMinimumResidenceFrames;
    extension.lastTransitionFrame = gFrameIndex;
    descriptor.hardFlags |= dirtyFlags;
    descriptor.candidateState = N01_STATE_FORCED_DYNAMIC;
    N01ScenarioCores[groupIndex] = core;
    N01ScenarioExtensions[groupIndex] = extension;
    N01ScenarioDescriptors[groupIndex] = descriptor;
}

[numthreads(64, 1, 1)]
void N01InjectDesignScenarioCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint groupIndex = dispatchThreadId.x;
    if (groupIndex >= gGroupCount || gScenarioFlags == 0u)
    {
        return;
    }

    if ((gScenarioFlags & 1u) != 0u && groupIndex == gScenarioTargetGroup)
    {
        N01ForceScenarioDynamic(groupIndex, N01_DIRTY_EDIT, float3(0.018f, 0.0f, 0.0f));
    }

    if ((gScenarioFlags & 2u) != 0u &&
        groupIndex >= gScenarioTargetGroup &&
        groupIndex < min(gScenarioTargetGroup + 32u, gGroupCount))
    {
        const float phase = float(groupIndex - gScenarioTargetGroup) * 0.21f + float(gScenarioTick) * 0.11f;
        N01ForceScenarioDynamic(
            groupIndex,
            N01_DIRTY_EDIT,
            float3(0.0015f * sin(phase), 0.0008f * cos(phase), 0.0f));
    }

    if ((gScenarioFlags & 4u) != 0u)
    {
        if (groupIndex == gScenarioTargetGroup)
        {
            N01ForceScenarioDynamic(groupIndex, N01_DIRTY_BOUNDARY, float3(0.004f, 0.0f, 0.0f));
        }
        else if (groupIndex == gScenarioTargetGroup + 1u)
        {
            SegmentGroupCore core = N01ScenarioCores[groupIndex];
            core.previousState = core.state;
            core.state = N01_STATE_STATIC_REUSE;
            core.dirtyFlags |= N01_DIRTY_BOUNDARY;
            N01ScenarioCores[groupIndex] = core;
        }
    }

    if ((gScenarioFlags & 8u) != 0u && (groupIndex & 255u) == 0u)
    {
        SegmentGroupCore core = N01ScenarioCores[groupIndex];
        core.previousState = core.state;
        core.state = ((gScenarioTick / 8u) & 1u) != 0u
            ? N01_STATE_FORCED_DYNAMIC
            : N01_STATE_STATIC_REUSE;
        core.dirtyFlags |= N01_DIRTY_WATCHDOG;
        N01ScenarioCores[groupIndex] = core;
    }

    if ((gScenarioFlags & 16u) != 0u)
    {
        const SegmentGroupCore core = N01ScenarioCores[groupIndex];
        if ((core.strandId % 997u) < 4u &&
            core.localGroupIndex >= 40u && core.localGroupIndex <= 72u)
        {
            N01ForceScenarioDynamic(groupIndex, N01_DIRTY_DEPENDENCY, float3(0.0f, 0.0f, 0.0006f));
        }
    }

    if ((gScenarioFlags & 32u) != 0u && groupIndex == gScenarioTargetGroup)
    {
        SegmentGroupCore core = N01ScenarioCores[groupIndex];
        CoreExtension extension = N01ScenarioExtensions[groupIndex];
        ReuseDescriptor descriptor = N01ScenarioDescriptors[groupIndex];
        core.state = N01_STATE_STATIC_REUSE;
        core.flags |= N01_CORE_PAYLOAD_VALID;
        core.dirtyFlags |= N01_DIRTY_PAYLOAD | N01_DIRTY_TOPOLOGY;
        extension.payloadIndex = gPayloadCapacity + 17u;
        extension.topologyVersion = gTopologyVersion ^ 0x5a5a5a5au;
        descriptor.hardFlags |= N01_DIRTY_PAYLOAD | N01_DIRTY_TOPOLOGY;
        descriptor.candidateState = N01_STATE_REPAIR;
        N01ScenarioCores[groupIndex] = core;
        N01ScenarioExtensions[groupIndex] = extension;
        N01ScenarioDescriptors[groupIndex] = descriptor;
        N01ScenarioCounter(N01_COUNTER_DESCRIPTOR_VIOLATION, 1u);
    }
}

#endif // mode 12
