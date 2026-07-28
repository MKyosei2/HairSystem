#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>

namespace hairgpu::novelty01
{
    static constexpr uint32_t kInvalidIndex = 0xffffffffu;
    static constexpr uint32_t kDefaultGroupsPerBlock = 32u;
    static constexpr uint32_t kScanThreadCount = 256u;
    static constexpr uint32_t kCounterCount = 64u;
    static constexpr uint32_t kDispatchArgumentCount = 8u;
    static constexpr uint32_t kPayloadGenerationCount = 4u;

    enum class RuntimeState : uint32_t
    {
        StaticReuse = 0u,
        StaticTransform = 1u,
        Dynamic = 2u,
        Deferred = 3u,
        Repair = 4u,
        ForcedDynamic = 5u,
        Invalid = 6u
    };

    enum DirtyBits : uint32_t
    {
        Dirty_None              = 0u,
        Dirty_RootTransform     = 1u << 0u,
        Dirty_Force             = 1u << 1u,
        Dirty_Collider          = 1u << 2u,
        Dirty_Topology          = 1u << 3u,
        Dirty_Edit              = 1u << 4u,
        Dirty_Dependency        = 1u << 5u,
        Dirty_DescriptorAge     = 1u << 6u,
        Dirty_Boundary          = 1u << 7u,
        Dirty_Watchdog          = 1u << 8u,
        Dirty_Payload           = 1u << 9u,
        Dirty_NonFinite         = 1u << 10u,
        Dirty_CollisionContact  = 1u << 11u,
        Dirty_SolverSettings    = 1u << 12u,
        Dirty_Lod               = 1u << 13u,
        Dirty_SharedPoint       = 1u << 14u,
        Dirty_RenderDependency  = 1u << 15u
    };

    enum CoreFlags : uint32_t
    {
        CoreFlag_None              = 0u,
        CoreFlag_CacheValid        = 1u << 0u,
        CoreFlag_PayloadValid      = 1u << 1u,
        CoreFlag_ForcedDynamic     = 1u << 2u,
        CoreFlag_BaselineFallback  = 1u << 3u,
        CoreFlag_ShadowSample      = 1u << 4u,
        CoreFlag_UniformRestLength = 1u << 5u,
        CoreFlag_PayloadRetired    = 1u << 6u,
        CoreFlag_GroundTruthSample = 1u << 7u
    };

    enum FeatureBits : uint64_t
    {
        Feature_OptionalPayload        = 1ull << 0u,
        Feature_FixedGroupIndex        = 1ull << 1u,
        Feature_VersionCheck           = 1ull << 2u,
        Feature_DelayedPayloadReuse    = 1ull << 3u,
        Feature_Hysteresis             = 1ull << 4u,
        Feature_MinimumResidence       = 1ull << 5u,
        Feature_StaticCache            = 1ull << 6u,
        Feature_IncrementalBlocks      = 1ull << 7u,
        Feature_StableSharedPoint      = 1ull << 8u,
        Feature_FollowerDependency     = 1ull << 9u,
        Feature_RenderDirtyRange       = 1ull << 10u,
        Feature_FullDynamicFastPath    = 1ull << 11u,
        Feature_ShadowDynamic          = 1ull << 12u,
        Feature_Watchdog               = 1ull << 13u,
        Feature_StableCompaction       = 1ull << 14u,
        Feature_PayloadOwnerCheck      = 1ull << 15u,
        Feature_GroundTruth            = 1ull << 16u,
        Feature_IndirectDispatch       = 1ull << 17u,
        Feature_BaselineEquivalentSolve= 1ull << 18u,
        Feature_SweptSpatialInvalidation = 1ull << 19u,
        Feature_TransitionBudget       = 1ull << 20u,
        Feature_HardSoftDescriptor     = 1ull << 21u,
        Feature_ByteTelemetry          = 1ull << 22u,
        Feature_FormalTelemetry        = 1ull << 23u
    };

    static constexpr uint64_t kAllFeatures =
        Feature_OptionalPayload |
        Feature_FixedGroupIndex |
        Feature_VersionCheck |
        Feature_DelayedPayloadReuse |
        Feature_Hysteresis |
        Feature_MinimumResidence |
        Feature_StaticCache |
        Feature_IncrementalBlocks |
        Feature_StableSharedPoint |
        Feature_FollowerDependency |
        Feature_RenderDirtyRange |
        Feature_FullDynamicFastPath |
        Feature_ShadowDynamic |
        Feature_Watchdog |
        Feature_StableCompaction |
        Feature_PayloadOwnerCheck |
        Feature_GroundTruth |
        Feature_IndirectDispatch |
        Feature_BaselineEquivalentSolve |
        Feature_SweptSpatialInvalidation |
        Feature_TransitionBudget |
        Feature_HardSoftDescriptor |
        Feature_ByteTelemetry |
        Feature_FormalTelemetry;

    enum class ExperimentVariant : uint32_t
    {
        P = 0u,
        B1_OptimizedFullDynamic,
        B2_SleepingFlag,
        B3_ActiveList,
        B4_SeparateStaticDynamicBuffers,
        B5_FixedCoreSimplePayload,
        B6_NoBlockInvalidation,
        B7_GuideLocalOnly,
        B8_StrongestPractical,
        A1_NoOptionalPayload,
        A2_NoFixedGroupIndex,
        A3_NoVersionCheck,
        A4_NoDelayedFree,
        A5_NoHysteresis,
        A6_NoMinimumResidence,
        A7_NoStaticCache,
        A8_NoIncrementalBlockInvalidation,
        A9_NoStableSharedPoint,
        A10_NoFollowerDependency,
        A11_NoRenderDirtyRange,
        A12_NoFullDynamicFastPath,
        A13_NoShadowDynamic,
        A14_NoWatchdog,
        A15_NoStableCompaction
    };

    enum CounterIndex : uint32_t
    {
        Counter_Dynamic = 0u,
        Counter_StaticReuse = 1u,
        Counter_StaticTransform = 2u,
        Counter_Repair = 3u,
        Counter_Invalid = 4u,
        Counter_Fallback = 5u,
        Counter_Transitions = 6u,
        Counter_Oscillations = 7u,
        Counter_DirtyBlocks = 8u,
        Counter_DirtyGroups = 9u,
        Counter_CacheHits = 10u,
        Counter_CacheMisses = 11u,
        Counter_CacheInvalidations = 12u,
        Counter_PayloadActive = 13u,
        Counter_PayloadOverflow = 14u,
        Counter_FalseStatic = 15u,
        Counter_ShadowSamples = 16u,
        Counter_SharedPointConflicts = 17u,
        Counter_DirtyFollowers = 18u,
        Counter_DirtyRenderPoints = 19u,
        Counter_WatchdogChecks = 20u,
        Counter_RepairSuccess = 21u,
        Counter_RepairFailure = 22u,
        Counter_FullDynamicRequested = 23u,
        Counter_CandidateDynamic = 24u,
        Counter_CandidateStatic = 25u,
        Counter_CandidateRepair = 26u,
        Counter_ScanBlocks = 27u,
        Counter_ReadWords = 28u,
        Counter_WriteWords = 29u,
        Counter_DescriptorValid = 30u,
        Counter_FalseDynamic = 31u,

        Counter_PayloadAllocated = 32u,
        Counter_PayloadRetired = 33u,
        Counter_PayloadReused = 34u,
        Counter_PayloadOwnerMismatch = 35u,
        Counter_PayloadVersionMismatch = 36u,
        Counter_DuplicatePayloadOwner = 37u,
        Counter_InvalidPayloadIndex = 38u,
        Counter_BlockSkipped = 39u,
        Counter_GuideDirtyRangeCount = 40u,
        Counter_FollowerDirtyRangeCount = 41u,
        Counter_RenderDirtyRangeCount = 42u,
        Counter_FollowerRebuiltPoints = 43u,
        Counter_RenderRegeneratedVertices = 44u,
        Counter_GroundTruthSamples = 45u,
        Counter_GroundTruthFalseStatic = 46u,
        Counter_GroundTruthViolations = 47u,
        Counter_DispatchMismatch = 48u,
        Counter_SharedPointUnresolved = 49u,
        Counter_DependencyFallback = 50u,
        Counter_RenderFallback = 51u,
        Counter_TransitionBytes = 52u,
        Counter_GroupReadBytes = 53u,
        Counter_GroupWriteBytes = 54u,
        Counter_PayloadReadBytes = 55u,
        Counter_PayloadWriteBytes = 56u,
        Counter_StateCopyBytes = 57u,
        Counter_MemoryLeakSuspect = 58u,
        Counter_NaN = 59u,
        Counter_Inf = 60u,
        Counter_CollisionPenetration = 61u,
        Counter_DescriptorViolation = 62u,
        Counter_SafeStop = 63u
    };

    struct alignas(16) SegmentGroupCore
    {
        uint32_t strandId = 0u;
        uint32_t localGroupIndex = 0u;
        uint32_t startPointIndex = 0u;
        uint32_t endPointIndex = 0u;

        uint32_t parentGroupIndex = kInvalidIndex;
        uint32_t childGroupIndex = kInvalidIndex;
        uint32_t ownerGroupIndex = kInvalidIndex;
        uint32_t blockIndex = 0u;

        float restLength = 0.0f;
        float restDirection[3] = { 0.0f, -1.0f, 0.0f };

        uint32_t state = static_cast<uint32_t>(RuntimeState::Dynamic);
        uint32_t previousState = static_cast<uint32_t>(RuntimeState::Dynamic);
        uint32_t dirtyFlags = Dirty_Topology | Dirty_DescriptorAge;
        uint32_t flags = CoreFlag_None;
    };

    struct alignas(16) CoreExtension
    {
        uint32_t payloadIndex = kInvalidIndex;
        uint32_t payloadVersion = 0u;
        uint32_t rootTransformVersion = 0u;
        uint32_t forceVersion = 0u;

        uint32_t colliderVersion = 0u;
        uint32_t topologyVersion = 0u;
        uint32_t editVersion = 0u;
        uint32_t dependencyVersion = 0u;

        uint32_t lastSolvedFrame = 0u;
        uint32_t lastValidatedFrame = 0u;
        uint32_t lastTransitionFrame = 0u;
        uint32_t minimumResidenceEndFrame = 0u;

        uint32_t stableCandidateFrames = 0u;
        uint32_t transitionCount = 0u;
        uint32_t lastPayloadRetireFrame = 0u;
        uint32_t payloadGeneration = 0u;

        float maximumObservedPositionErrorMm = 0.0f;
        float maximumObservedBoundaryErrorMm = 0.0f;
        float minimumObservedCollisionMarginMm = 1.0e9f;
        float predictedPositionErrorMm = 0.0f;
    };

    struct alignas(16) StaticCache
    {
        float rootLocalEnd[3] = { 0.0f, 0.0f, 0.0f };
        float restLength = 0.0f;

        float localDirection[3] = { 0.0f, -1.0f, 0.0f };
        float boundaryErrorMm = 0.0f;

        float previousRootLocalEnd[3] = { 0.0f, 0.0f, 0.0f };
        float maximumErrorMm = 0.0f;

        uint32_t sourceFrame = 0u;
        uint32_t rootTransformVersion = 0u;
        uint32_t topologyVersion = 0u;
        uint32_t valid = 0u;
    };

    struct alignas(16) DynamicPayload
    {
        float previousEndPosition[3] = { 0.0f, 0.0f, 0.0f };
        float distanceLambda = 0.0f;

        float velocity[3] = { 0.0f, 0.0f, 0.0f };
        float bendLambda = 0.0f;

        float constraintResidual = 0.0f;
        float collisionResidual = 0.0f;
        float predictedPositionErrorMm = 0.0f;
        float boundaryErrorMm = 0.0f;

        uint32_t solverFlags = 0u;
        uint32_t collisionFlags = 0u;
        uint32_t allocationEpoch = 0u;
        uint32_t retireEpoch = 0u;
    };

    struct alignas(16) PayloadOwner
    {
        uint32_t groupIndex = kInvalidIndex;
        uint32_t payloadVersion = 0u;
        uint32_t generation = 0u;
        uint32_t allocationEpoch = 0u;

        uint32_t retireEpoch = 0u;
        uint32_t safeReuseEpoch = 0u;
        uint32_t state = 0u; // 0 Free, 1 Active, 2 Retired, 3 SafeToReuse
        uint32_t reserved = 0u;
    };

    struct alignas(16) ReuseDescriptor
    {
        float positionErrorMm = 0.0f;
        float velocityDisplacementMm = 0.0f;
        float accelerationDisplacementMm = 0.0f;
        float constraintResidual = 0.0f;

        float rootTranslationDisplacementMm = 0.0f;
        float rootRotationDisplacementMm = 0.0f;
        float forceDisplacementMm = 0.0f;
        float colliderDisplacementMm = 0.0f;

        float collisionMarginMm = 1.0e9f;
        float boundaryErrorMm = 0.0f;
        float lengthErrorRatio = 0.0f;
        float shadowErrorMm = 0.0f;

        uint32_t hardFlags = 0u;
        uint32_t softFlags = 0u;
        uint32_t ageFrames = 0u;
        uint32_t candidateState = static_cast<uint32_t>(RuntimeState::Dynamic);

        uint32_t rootTransformVersion = 0u;
        uint32_t forceVersion = 0u;
        uint32_t colliderVersion = 0u;
        uint32_t editVersion = 0u;
    };

    struct alignas(16) BlockDescriptor
    {
        uint32_t firstGroupIndex = 0u;
        uint32_t groupCount = 0u;
        uint32_t dirtyFlags = Dirty_Topology;
        uint32_t validFlags = 0u;

        float boundsMin[3] = { 0.0f, 0.0f, 0.0f };
        float maximumMotionMm = 0.0f;

        float boundsMax[3] = { 0.0f, 0.0f, 0.0f };
        float minimumCollisionMarginMm = 1.0e9f;

        float maximumResidual = 0.0f;
        float maximumBoundaryErrorMm = 0.0f;
        uint32_t descriptorVersion = 0u;
        uint32_t lastUpdatedFrame = 0u;
    };

    struct alignas(16) SharedPointCandidate
    {
        float position[3] = { 0.0f, 0.0f, 0.0f };
        float positionErrorMm = 0.0f;

        float constraintResidual = 0.0f;
        float collisionResidual = 0.0f;
        uint32_t sourceGroupIndex = kInvalidIndex;
        uint32_t priority = 0u;

        uint32_t targetPointIndex = kInvalidIndex;
        uint32_t finite = 0u;
        uint32_t valid = 0u;
        uint32_t reserved = 0u;
    };

    struct alignas(16) ScanRecord
    {
        uint32_t dynamicValue = 0u;
        uint32_t staticValue = 0u;
        uint32_t repairValue = 0u;
        uint32_t invalidValue = 0u;
    };

    struct alignas(16) GuideDirtyRange
    {
        uint32_t segmentBegin = kInvalidIndex;
        uint32_t segmentEnd = 0u;
        uint32_t flags = 0u;
        uint32_t dependencyVersion = 0u;
    };

    struct alignas(16) FollowerDirtyRange
    {
        uint32_t followerStrandIndex = kInvalidIndex;
        uint32_t segmentBegin = kInvalidIndex;
        uint32_t segmentEnd = 0u;
        uint32_t flags = 0u;

        uint32_t sourceGuideIndex = kInvalidIndex;
        uint32_t dependencyVersion = 0u;
        uint32_t guardBand = 1u;
        uint32_t reserved = 0u;
    };

    struct alignas(16) RenderDirtyRange
    {
        uint32_t strandIndex = kInvalidIndex;
        uint32_t pointBegin = kInvalidIndex;
        uint32_t pointEnd = 0u;
        uint32_t flags = 0u;
    };

    struct alignas(16) FollowerDependencyEntry
    {
        uint32_t followerStrandIndex = 0u;
        uint32_t sourceGuideIndex = 0u;
        uint32_t sourceSegmentBegin = 0u;
        uint32_t sourceSegmentEnd = 0u;

        uint32_t targetSegmentBegin = 0u;
        uint32_t targetSegmentEnd = 0u;
        uint32_t guardBand = 1u;
        uint32_t dependencyVersion = 0u;
    };

    struct alignas(16) GroundTruthMetric
    {
        float squaredPositionErrorMm2 = 0.0f;
        float maximumPositionErrorMm = 0.0f;
        float segmentLengthErrorRatio = 0.0f;
        float boundaryErrorMm = 0.0f;

        float followerPositionErrorMm = 0.0f;
        float renderGeometryErrorMm = 0.0f;
        uint32_t staticGroup = 0u;
        uint32_t falseStatic = 0u;
    };

    struct alignas(16) DispatchArguments
    {
        uint32_t x = 0u;
        uint32_t y = 1u;
        uint32_t z = 1u;
        uint32_t reserved = 0u;
    };

    struct alignas(16) RuntimeConstants
    {
        uint32_t groupCount = 0u;
        uint32_t blockCount = 0u;
        uint32_t scanBlockCount = 0u;
        uint32_t groupsPerBlock = kDefaultGroupsPerBlock;

        uint32_t guideCount = 0u;
        uint32_t followerCount = 0u;
        uint32_t particlesPerStrand = 0u;
        uint32_t payloadCapacity = 0u;

        uint32_t frameIndex = 0u;
        uint32_t rootTransformVersion = 0u;
        uint32_t forceVersion = 0u;
        uint32_t colliderVersion = 0u;

        uint32_t topologyVersion = 0u;
        uint32_t editVersion = 0u;
        uint32_t dependencyVersion = 0u;
        uint32_t descriptorMaximumAge = 64u;

        uint32_t minimumResidenceFrames = 12u;
        uint32_t consecutiveStaticFrames = 8u;
        uint32_t staticTransitionBudget = 32768u;
        uint32_t payloadRetireDelayFrames = kPayloadGenerationCount;

        uint32_t passKind = 0u;
        uint32_t solverIteration = 0u;
        uint32_t solverPhase = 0u;
        uint32_t runtimeFlags = 0u;

        uint32_t featureMaskLow = static_cast<uint32_t>(kAllFeatures);
        uint32_t featureMaskHigh = static_cast<uint32_t>(kAllFeatures >> 32u);
        uint32_t experimentVariant = static_cast<uint32_t>(ExperimentVariant::P);
        uint32_t payloadGeneration = 0u;

        float deltaTime = 1.0f / 60.0f;
        float staticPositionThresholdMm = 0.10f;
        float dynamicPositionThresholdMm = 0.20f;
        float staticVelocityThresholdMm = 0.10f;

        float dynamicVelocityThresholdMm = 0.20f;
        float lengthErrorThresholdRatio = 0.02f;
        float boundaryErrorThresholdMm = 2.0f;
        float collisionSafetyPaddingMm = 2.0f;

        float shadowErrorThresholdMm = 0.50f;
        float gravity = 9.81f;
        float damping = 0.03f;
        float windStrength = 0.0f;

        float windDirection[3] = { 1.0f, 0.0f, 0.0f };
        float floorY = -1.2f;

        float stretchCompliance = 2.0e-7f;
        float bendCompliance = 1.0e-5f;
        float bendStiffness = 0.35f;
        float selfCollisionRadius = 0.012f;

        float selfCollisionStiffness = 0.35f;
        float gridCellSize = 0.085f;
        uint32_t hashBucketCount = 32768u;
        uint32_t enableSelfCollision = 1u;

        float rootStiffness = 1.0f;
        float supportCollisionRadius = 0.0f;
        float supportCollisionStiffness = 0.0f;
        float tangentialFriction = 0.0f;

        float lateralFriction = 0.0f;
        float angularBendRadians = 0.0f;
        float angularBendCompliance = 0.0f;
        float angularBendStiffness = 0.0f;

        float angularBendDamping = 0.0f;
        float fullDynamicEnterRatio = 0.70f;
        float fullDynamicExitRatio = 0.55f;
        uint32_t fullDynamicDecisionFrames = 8u;

        float velocityShockPropagation = 0.72f;
        float freezeBackThreshold = -0.045f;
        float freezeBlend = 0.82f;
        float looseConstraintStiffness = 0.12f;

        float looseConstraintMaxDistance = 0.085f;
        uint32_t sdfValueCount = 0u;
        uint32_t useSdfCollider = 0u;
        uint32_t solverIterations = 6u;

        float scalpMotionBlend = 1.0f;
        float elapsedTime = 0.0f;
        uint32_t baselinePadding0 = 0u;
        uint32_t baselinePadding1 = 0u;

        uint32_t scenarioId = 0u;
        uint32_t scenarioTick = 0u;
        uint32_t scenarioFlags = 0u;
        uint32_t scenarioTargetGroup = kInvalidIndex;

        uint32_t sdfResolution[4] = {};
        float sdfBoundsMin[4] = {};
        float sdfBoundsMax[4] = {};

        uint32_t sphereColliderCount = 0u;
        uint32_t capsuleColliderCount = 0u;
        uint32_t planeColliderCount = 0u;
        uint32_t aabbColliderCount = 0u;

        float sweptRootBoundsMin[3] = { 1.0e9f, 1.0e9f, 1.0e9f };
        float sweptRootPadding0 = 0.0f;

        float sweptRootBoundsMax[3] = { -1.0e9f, -1.0e9f, -1.0e9f };
        float sweptRootPadding1 = 0.0f;

        float sweptColliderBoundsMin[3] = { 1.0e9f, 1.0e9f, 1.0e9f };
        float sweptColliderPadding0 = 0.0f;

        float sweptColliderBoundsMax[3] = { -1.0e9f, -1.0e9f, -1.0e9f };
        float sweptColliderPadding1 = 0.0f;

        float sphereData[4][4] = {};
        float capsuleData[8][4] = {};
        float planeData[4][4] = {};
        float aabbData[8][4] = {};
    };

    struct RuntimeCounterSnapshot
    {
        std::array<uint32_t, kCounterCount> values = {};

        uint32_t Get(CounterIndex index) const
        {
            return values[static_cast<size_t>(index)];
        }
    };

    struct FormalAggregate
    {
        uint64_t sampleCount = 0u;
        uint64_t falseStaticCount = 0u;
        double squaredPositionErrorMm2 = 0.0;
        double maximumPositionErrorMm = 0.0;
        double segmentLengthErrorRatio = 0.0;
        double boundaryErrorMm = 0.0;
        double followerPositionErrorMm = 0.0;
        double renderGeometryErrorMm = 0.0;

        double PositionRmseMm() const
        {
            return sampleCount > 0u
                ? std::sqrt(squaredPositionErrorMm2 /
                    static_cast<double>(sampleCount))
                : 0.0;
        }
    };

    static_assert(sizeof(SegmentGroupCore) == 64u);
    static_assert(sizeof(CoreExtension) == 80u);
    static_assert(sizeof(StaticCache) == 64u);
    static_assert(sizeof(DynamicPayload) == 64u);
    static_assert(sizeof(PayloadOwner) == 32u);
    static_assert(sizeof(ReuseDescriptor) == 80u);
    static_assert(sizeof(BlockDescriptor) == 64u);
    static_assert(sizeof(SharedPointCandidate) == 48u);
    static_assert(sizeof(ScanRecord) == 16u);
    static_assert(sizeof(GuideDirtyRange) == 16u);
    static_assert(sizeof(FollowerDirtyRange) == 32u);
    static_assert(sizeof(RenderDirtyRange) == 16u);
    static_assert(sizeof(FollowerDependencyEntry) == 32u);
    static_assert(sizeof(GroundTruthMetric) == 32u);
    static_assert(sizeof(DispatchArguments) == 16u);
    static_assert(sizeof(RuntimeConstants) % 16u == 0u);
}
