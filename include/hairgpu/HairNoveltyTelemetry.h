#pragma once

#include <cstdint>
#include <limits>

namespace hairgpu
{

    // HAIRGPU_EXPERIMENT_RUNTIME_PATCH_V1
    enum HairNoveltyTelemetryAvailabilityBits : uint64_t
    {
        HairTelemetryAvailability_None = 0ull,
        HairTelemetryAvailability_FrameInterval = 1ull << 0u,
        HairTelemetryAvailability_CpuFrameWork = 1ull << 1u,
        HairTelemetryAvailability_GpuTiming = 1ull << 2u,
        HairTelemetryAvailability_GpuCounters = 1ull << 3u,
        HairTelemetryAvailability_Scenario = 1ull << 4u,
        HairTelemetryAvailability_ObservedSettings = 1ull << 5u,
        HairTelemetryAvailability_CollisionGroundTruth = 1ull << 6u,
        HairTelemetryAvailability_WakeGroundTruth = 1ull << 7u,
        HairTelemetryAvailability_RepairVerification = 1ull << 8u,
        HairTelemetryAvailability_BoundaryComparison = 1ull << 9u,
        HairTelemetryAvailability_DeterminismHash = 1ull << 10u,
        HairTelemetryAvailability_AdapterIdentity = 1ull << 11u
    };
    struct HairNoveltyFrameTelemetry
    {
        uint32_t frameIndex = 0u;
        uint32_t measuredFrameIndex = 0u;
        uint32_t topLevelNoveltyMask = 0u;

        // Delayed evidence identity and availability. sourceFrameIndex is the
        // simulation/render frame that produced this record, not the frame on
        // which the CPU resolved the asynchronous query.
        uint32_t sourceFrameIndex = 0u;
        uint64_t availabilityMask = HairTelemetryAvailability_None;
        uint64_t metricAvailabilityMask = HairTelemetryAvailability_None;
        uint32_t fatalFailure = 0u;
        uint32_t fatalFailureCode = 0u;
        uint32_t fatalRuntimeFailureCount = 0u;
        uint32_t scenarioAppliedEventCount = 0u;
        uint32_t scenarioAssertionFailureCount = 0u;
        uint32_t gpuTimingResolvedFrameCount = 0u;
        uint32_t gpuTimingValidFrameCount = 0u;
        uint32_t gpuTimingUnresolvedFrameCount = 0u;
        uint32_t counterReadbackResolvedFrameCount = 0u;
        uint32_t counterReadbackUnresolvedFrameCount = 0u;
        uint32_t unavailableMetricCount = 0u;
        uint32_t syntheticMetricCount = 0u;
        uint32_t fullCpuGroupUploadCount = 0u;
        uint32_t sameFrameReadbackCount = 0u;
        uint32_t nanStateCount = 0u;
        uint32_t infiniteStateCount = 0u;
        uint32_t determinismMismatchCount = 0u;
        float frameBudgetOverrunRate = 0.0f;

        uint32_t cpuReadbackCount = 0u;
        uint32_t overflowCount = 0u;
        uint32_t budgetDropCount = 0u;
        uint32_t fallbackCount = 0u;
        uint32_t deferCount = 0u;

        uint32_t microtypeDecodedCount = 0u;
        uint32_t safeDecodeCount = 0u;
        uint32_t certificateFailureCount = 0u;
        uint32_t falseSafeCount = 0u;
        uint32_t typedRepairRequestCount = 0u;
        uint32_t floatFallbackRequestCount = 0u;
        uint32_t rejectPayloadCount = 0u;

        // -----------------------------------------------------------------
        // 01: Segment-Group Certified Strand Data Type
        // -----------------------------------------------------------------

        // State population.
        uint32_t totalSegmentGroupCount = 0u;
        uint32_t staticCachedGroupCount = 0u;
        uint32_t staticTransformedGroupCount = 0u;
        uint32_t dynamicGroupCount = 0u;
        uint32_t deferredCorrectionGroupCount = 0u;
        uint32_t repairRequiredGroupCount = 0u;
        uint32_t fallbackGroupCount = 0u;

        // Dirty hierarchy.
        uint32_t totalSegmentGroupBlockCount = 0u;
        uint32_t dirtyBlockCount = 0u;
        uint32_t dirtyGroupCount = 0u;

        // Cache certificate results.
        uint32_t certificateValidGroupCount = 0u;
        uint32_t certificateInvalidGroupCount = 0u;

        uint32_t parentVersionMismatchCount = 0u;
        uint32_t colliderVersionMismatchCount = 0u;
        uint32_t editVersionMismatchCount = 0u;
        uint32_t topologyVersionMismatchCount = 0u;

        // Static reuse quality.
        uint32_t staticReuseAttemptCount = 0u;
        uint32_t falseStaticCount = 0u;
        float falseStaticRate = 0.0f;

        uint32_t cacheHitCount = 0u;
        uint32_t cacheMissCount = 0u;
        uint32_t cacheInvalidationCount = 0u;

        // Shared endpoint and hemisphere ownership.
        uint32_t sharedPointConflictCount = 0u;
        uint32_t hemisphereParentResolveCount = 0u;
        uint32_t hemisphereChildResolveCount = 0u;

        // Predictive wake and recovery quality.
        uint32_t wakeRequestGroupCount = 0u;
        uint32_t predictiveWakeEvaluatedCount = 0u;
        uint32_t predictiveWakeTruePositiveCount = 0u;
        uint32_t predictiveWakeFalsePositiveCount = 0u;
        uint32_t predictiveWakeFalseNegativeCount = 0u;
        uint32_t wakeDelayFrameSum = 0u;
        uint32_t wakeDelayFrameMax = 0u;
        float wakePositionErrorMeanMm = 0.0f;
        float wakePositionErrorMaxMm = 0.0f;
        float wakeVelocityErrorMean = 0.0f;
        float wakeVelocityErrorMax = 0.0f;

        // Repair quality.
        uint32_t repairAttemptCount = 0u;
        uint32_t repairSuccessCount = 0u;
        uint32_t repairVerifiedCount = 0u;
        uint32_t repairUnverifiedCount = 0u;
        uint32_t repairFailureCount = 0u;
        uint32_t repairRecurrenceCount = 0u;
        uint32_t repairLatencyFrameSum = 0u;
        uint32_t repairLatencyFrameMax = 0u;
        float repairSuccessRate = 0.0f;

        // State stability.
        uint32_t stateTransitionCount = 0u;
        uint32_t stateOscillationCount = 0u;
        uint32_t staticLifetimeFrameSum = 0u;
        uint32_t staticLifetimeFrameMax = 0u;

        // Collision-ground-truth checks.
        uint32_t collisionCandidateCount = 0u;
        uint32_t collisionMissCount = 0u;
        uint32_t penetrationEventCount = 0u;
        float collisionMissRate = 0.0f;
        float meanPenetrationMm = 0.0f;

        // GPU compaction.
        uint32_t dynamicCompactionInputCount = 0u;
        uint32_t dynamicCompactionOutputCount = 0u;
        uint32_t repairCompactionOutputCount = 0u;

        // Watchdog.
        uint32_t watchdogCheckCount = 0u;
        uint32_t watchdogRepairCount = 0u;

        // Static/Dynamic boundary.
        float staticDynamicBoundaryMeanErrorMm = 0.0f;
        float staticDynamicBoundaryMaxErrorMm = 0.0f;

        // Memory.
        float directionCacheMemoryMb = 0.0f;
        float fullPositionMemoryMb = 0.0f;
        float directionCacheMemorySavingRate = 0.0f;

        // GPU pass timings. All fields must come from delayed timestamp-query
        // readback and must not force same-frame CPU/GPU synchronization.
        float blockClassificationGpuMilliseconds = 0.0f;
        float groupClassificationGpuMilliseconds = 0.0f;
        float predictiveWakeGpuMilliseconds = 0.0f;
        float wakePropagationGpuMilliseconds = 0.0f;
        float compactionGpuMilliseconds = 0.0f;
        float staticTransformGpuMilliseconds = 0.0f;
        float dynamicSolveGpuMilliseconds = 0.0f;
        float velocityRecoveryGpuMilliseconds = 0.0f;
        float repairSolveGpuMilliseconds = 0.0f;
        float boundaryRepairGpuMilliseconds = 0.0f;
        float cacheUpdateGpuMilliseconds = 0.0f;
        float watchdogGpuMilliseconds = 0.0f;
        float followerReconstructionGpuMilliseconds = 0.0f;
        float renderPreparationGpuMilliseconds = 0.0f;
        float hairDrawGpuMilliseconds = 0.0f;
        float telemetryReductionGpuMilliseconds = 0.0f;
        float totalHairGpuMilliseconds = 0.0f;

        uint32_t riskyStrandCount = 0u;
        uint32_t repairTaskCount = 0u;
        uint32_t repairCommitCount = 0u;
        uint32_t repairRejectCount = 0u;
        uint32_t repairDeferCount = 0u;
        uint32_t islandCandidateCount = 0u;
        uint32_t islandCount = 0u;
        uint32_t islandMemberCount = 0u;
        uint32_t islandCommitCount = 0u;
        uint32_t islandRejectCount = 0u;
        uint32_t physicsFallbackCount = 0u;
        float maxPenetrationMm = 0.0f;

        uint32_t renderCertificateCount = 0u;
        uint32_t renderSkipCount = 0u;
        uint32_t renderCoarseOpacityCount = 0u;
        uint32_t renderStrandRasterCount = 0u;
        uint32_t renderHighQualityShadowCount = 0u;
        uint32_t renderHighQualitySpecularCount = 0u;
        uint32_t renderFullQualityCount = 0u;
        uint32_t renderDroppedFullQualityCount = 0u;
        float p95ShadowErrorBound = 0.0f;
        float p95SpecularErrorBound = 0.0f;
        float temporalFlickerScore = 0.0f;

        uint32_t editOperationCount = 0u;
        uint32_t editRepairTaskCount = 0u;
        uint32_t editIslandEscalationCount = 0u;
        uint32_t editCommitCount = 0u;
        uint32_t editRejectCount = 0u;
        uint32_t editPreviewFullQualityCount = 0u;
        float editLatencyMs = 0.0f;

        uint32_t gpuResidentTaskCount = 0u;
        uint32_t riskyCompactionCount = 0u;
        uint32_t dispatchIndirectCount = 0u;
        uint32_t drawIndirectCount = 0u;
        uint32_t delayedTelemetryReadCount = 0u;
    };

    inline void FinalizeNoveltyTelemetryRates(HairNoveltyFrameTelemetry& telemetry)
    {
        const float unavailable = std::numeric_limits<float>::quiet_NaN();
        const uint32_t staticGroupCount =
            telemetry.staticCachedGroupCount + telemetry.staticTransformedGroupCount;
        telemetry.falseStaticRate =
            (telemetry.metricAvailabilityMask & HairTelemetryAvailability_CollisionGroundTruth) != 0ull && staticGroupCount > 0u
            ? static_cast<float>(telemetry.falseStaticCount) / static_cast<float>(staticGroupCount)
            : unavailable;

        telemetry.repairSuccessRate =
            (telemetry.metricAvailabilityMask & HairTelemetryAvailability_RepairVerification) != 0ull &&
            telemetry.repairAttemptCount > 0u
            ? static_cast<float>(telemetry.repairVerifiedCount) /
                static_cast<float>(telemetry.repairAttemptCount)
            : unavailable;

        telemetry.collisionMissRate =
            (telemetry.metricAvailabilityMask & HairTelemetryAvailability_CollisionGroundTruth) != 0ull &&
            telemetry.collisionCandidateCount > 0u
            ? static_cast<float>(telemetry.collisionMissCount) /
                static_cast<float>(telemetry.collisionCandidateCount)
            : unavailable;

        telemetry.directionCacheMemorySavingRate = telemetry.fullPositionMemoryMb > 0.0f
            ? 1.0f - telemetry.directionCacheMemoryMb / telemetry.fullPositionMemoryMb
            : 0.0f;
    }

    inline HairNoveltyFrameTelemetry MakeZeroNoveltyTelemetry()
    {
        return HairNoveltyFrameTelemetry{};
    }
}
