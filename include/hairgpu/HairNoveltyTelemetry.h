#pragma once

#include <cstdint>

namespace hairgpu
{
    struct HairNoveltyFrameTelemetry
    {
        uint32_t frameIndex = 0u;
        uint32_t measuredFrameIndex = 0u;
        uint32_t topLevelNoveltyMask = 0u;

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

        // GPU pass timings.
        float blockClassificationGpuMilliseconds = 0.0f;
        float groupClassificationGpuMilliseconds = 0.0f;
        float compactionGpuMilliseconds = 0.0f;
        float staticTransformGpuMilliseconds = 0.0f;
        float boundaryRepairGpuMilliseconds = 0.0f;
        float watchdogGpuMilliseconds = 0.0f;
        float telemetryReductionGpuMilliseconds = 0.0f;

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

    inline HairNoveltyFrameTelemetry MakeZeroNoveltyTelemetry()
    {
        return HairNoveltyFrameTelemetry{};
    }
}
