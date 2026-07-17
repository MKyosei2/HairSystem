#pragma once

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

namespace hairgpu
{
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
        HairTelemetryAvailability_AdapterIdentity = 1ull << 11u,

        // 01-Safe v3.1: these are intentionally independent. False-static
        // evidence must not be inferred from collision or boundary evidence.
        HairTelemetryAvailability_FalseStaticGroundTruth = 1ull << 12u,
        HairTelemetryAvailability_PerformanceEnvironment = 1ull << 13u,
        HairTelemetryAvailability_ResultIntegrity = 1ull << 14u
    };

    struct HairNoveltyFrameTelemetry
    {
        uint32_t frameIndex = 0u;
        uint32_t measuredFrameIndex = 0u;
        uint32_t topLevelNoveltyMask = 0u;

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

        // Number of measured frames carrying each formal evidence class.
        // These counters distinguish a measured zero from unavailable data.
        uint32_t collisionGroundTruthFrameCount = 0u;
        uint32_t wakeGroundTruthFrameCount = 0u;
        uint32_t repairVerificationFrameCount = 0u;
        uint32_t boundaryComparisonFrameCount = 0u;
        uint32_t falseStaticGroundTruthFrameCount = 0u;
        uint32_t performanceEnvironmentFrameCount = 0u;
        uint32_t resultIntegrityFrameCount = 0u;
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

        // 01: Segment-Group Certified Strand Data Type.
        uint32_t totalSegmentGroupCount = 0u;
        uint32_t staticCachedGroupCount = 0u;
        uint32_t staticTransformedGroupCount = 0u;
        uint32_t dynamicGroupCount = 0u;
        uint32_t deferredCorrectionGroupCount = 0u;
        uint32_t repairRequiredGroupCount = 0u;
        uint32_t fallbackGroupCount = 0u;

        uint32_t totalSegmentGroupBlockCount = 0u;
        uint32_t dirtyBlockCount = 0u;
        uint32_t dirtyGroupCount = 0u;

        uint32_t certificateValidGroupCount = 0u;
        uint32_t certificateInvalidGroupCount = 0u;
        uint32_t parentVersionMismatchCount = 0u;
        uint32_t colliderVersionMismatchCount = 0u;
        uint32_t editVersionMismatchCount = 0u;
        uint32_t topologyVersionMismatchCount = 0u;

        uint32_t staticReuseAttemptCount = 0u;
        uint32_t falseStaticCount = 0u;
        float falseStaticRate = std::numeric_limits<float>::quiet_NaN();

        uint32_t cacheHitCount = 0u;
        uint32_t cacheMissCount = 0u;
        uint32_t cacheInvalidationCount = 0u;

        uint32_t sharedPointConflictCount = 0u;
        uint32_t hemisphereParentResolveCount = 0u;
        uint32_t hemisphereChildResolveCount = 0u;

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
        float predictiveWakePrecision = std::numeric_limits<float>::quiet_NaN();
        float predictiveWakeRecall = std::numeric_limits<float>::quiet_NaN();

        uint32_t repairAttemptCount = 0u;
        uint32_t repairSuccessCount = 0u;
        uint32_t repairVerifiedCount = 0u;
        uint32_t repairUnverifiedCount = 0u;
        uint32_t repairFailureCount = 0u;
        uint32_t repairRecurrenceCount = 0u;
        uint32_t repairLatencyFrameSum = 0u;
        uint32_t repairLatencyFrameMax = 0u;
        float repairExecutionSuccessRate = std::numeric_limits<float>::quiet_NaN();
        float repairVerifiedSuccessRate = std::numeric_limits<float>::quiet_NaN();

        // Compatibility alias. New reports must use repairVerifiedSuccessRate.
        float repairSuccessRate = std::numeric_limits<float>::quiet_NaN();

        uint32_t stateTransitionCount = 0u;
        uint32_t stateOscillationCount = 0u;
        uint32_t staticLifetimeFrameSum = 0u;
        uint32_t staticLifetimeFrameMax = 0u;
        float stateOscillationRate = std::numeric_limits<float>::quiet_NaN();

        uint32_t collisionCandidateCount = 0u;
        uint32_t collisionMissCount = 0u;
        uint32_t penetrationEventCount = 0u;
        float collisionMissRate = std::numeric_limits<float>::quiet_NaN();
        float meanPenetrationMm = 0.0f;

        uint32_t dynamicCompactionInputCount = 0u;
        uint32_t dynamicCompactionOutputCount = 0u;
        uint32_t repairCompactionOutputCount = 0u;

        uint32_t watchdogCheckCount = 0u;
        uint32_t watchdogRepairCount = 0u;

        float staticDynamicBoundaryMeanErrorMm = 0.0f;
        float staticDynamicBoundaryMaxErrorMm = 0.0f;

        float directionCacheMemoryMb = 0.0f;
        float fullPositionMemoryMb = 0.0f;
        float directionCacheMemorySavingRate = std::numeric_limits<float>::quiet_NaN();

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

    // Formal evidence is recorded independently of the legacy frame exporter.
    // This preserves verified-repair and recurrence counters without inventing
    // proxy values and without changing the runtime representation.
    struct HairFormalEvidenceRow
    {
        uint32_t sourceFrameIndex = 0u;
        uint32_t measuredFrameIndex = 0u;
        uint64_t availabilityMask = 0ull;
        uint64_t metricAvailabilityMask = 0ull;
        uint32_t falseStaticCount = 0u;
        uint32_t staticCachedGroupCount = 0u;
        uint32_t staticTransformedGroupCount = 0u;
        uint32_t repairAttemptCount = 0u;
        uint32_t repairSuccessCount = 0u;
        uint32_t repairVerifiedCount = 0u;
        uint32_t repairRecurrenceCount = 0u;
    };

    class HairFormalEvidenceRecorder
    {
    public:
        void Record(const HairNoveltyFrameTelemetry& telemetry)
        {
            InitializeOutputPath();
            if (m_outputPath.empty())
            {
                return;
            }

            HairFormalEvidenceRow row = {};
            row.sourceFrameIndex = telemetry.sourceFrameIndex;
            row.measuredFrameIndex = telemetry.measuredFrameIndex;
            row.availabilityMask = telemetry.availabilityMask;
            row.metricAvailabilityMask = telemetry.metricAvailabilityMask;
            row.falseStaticCount = telemetry.falseStaticCount;
            row.staticCachedGroupCount = telemetry.staticCachedGroupCount;
            row.staticTransformedGroupCount = telemetry.staticTransformedGroupCount;
            row.repairAttemptCount = telemetry.repairAttemptCount;
            row.repairSuccessCount = telemetry.repairSuccessCount;
            row.repairVerifiedCount = telemetry.repairVerifiedCount;
            row.repairRecurrenceCount = telemetry.repairRecurrenceCount;

            // Finalization can be called more than once for one source frame.
            // Keep the last fully-populated value rather than double-counting.
            if (!m_rows.empty() && m_rows.back().sourceFrameIndex == row.sourceFrameIndex)
            {
                m_rows.back() = row;
            }
            else
            {
                m_rows.push_back(row);
            }
        }

        ~HairFormalEvidenceRecorder() noexcept
        {
            Flush();
        }

    private:
        void InitializeOutputPath()
        {
            if (m_initialized)
            {
                return;
            }
            m_initialized = true;

            const char* enabled = std::getenv("HAIRGPU_AUTO_TELEMETRY");
            const char* directory = std::getenv("HAIRGPU_TELEMETRY_DIR");
            const char* runName = std::getenv("HAIRGPU_TELEMETRY_RUN_NAME");
            if (!enabled || enabled[0] == '\0' || enabled[0] == '0' ||
                !directory || directory[0] == '\0' || !runName || runName[0] == '\0')
            {
                return;
            }
            m_outputPath = std::filesystem::path(directory) /
                (std::string(runName) + "_formal_evidence.jsonl");
            m_rows.reserve(8192u);
        }

        void Flush() noexcept
        {
            if (m_outputPath.empty() || m_rows.empty())
            {
                return;
            }
            try
            {
                std::filesystem::create_directories(m_outputPath.parent_path());
                const std::filesystem::path temporary = m_outputPath.string() + ".tmp";
                std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    return;
                }
                for (const HairFormalEvidenceRow& row : m_rows)
                {
                    out << "{\"sourceFrameIndex\":" << row.sourceFrameIndex
                        << ",\"measuredFrameIndex\":" << row.measuredFrameIndex
                        << ",\"availabilityMask\":" << row.availabilityMask
                        << ",\"metricAvailabilityMask\":" << row.metricAvailabilityMask
                        << ",\"falseStaticCount\":" << row.falseStaticCount
                        << ",\"staticCachedGroupCount\":" << row.staticCachedGroupCount
                        << ",\"staticTransformedGroupCount\":" << row.staticTransformedGroupCount
                        << ",\"repairAttemptCount\":" << row.repairAttemptCount
                        << ",\"repairSuccessCount\":" << row.repairSuccessCount
                        << ",\"repairVerifiedCount\":" << row.repairVerifiedCount
                        << ",\"repairRecurrenceCount\":" << row.repairRecurrenceCount
                        << "}\n";
                }
                out.flush();
                if (!out)
                {
                    return;
                }
                out.close();
                std::error_code error;
                std::filesystem::remove(m_outputPath, error);
                error.clear();
                std::filesystem::rename(temporary, m_outputPath, error);
                if (error)
                {
                    std::filesystem::remove(temporary, error);
                }
            }
            catch (...)
            {
                // Evidence export failure is detected by the Python validator.
            }
        }

        bool m_initialized = false;
        std::filesystem::path m_outputPath;
        std::vector<HairFormalEvidenceRow> m_rows;
    };

    inline HairFormalEvidenceRecorder& GetHairFormalEvidenceRecorder()
    {
        static HairFormalEvidenceRecorder recorder;
        return recorder;
    }

    inline bool HasTelemetryMetric(const HairNoveltyFrameTelemetry& telemetry, uint64_t bit)
    {
        return (telemetry.metricAvailabilityMask & bit) != 0ull;
    }

    inline float SafeTelemetryRate(uint32_t numerator, uint32_t denominator, bool available)
    {
        return available && denominator > 0u
            ? static_cast<float>(numerator) / static_cast<float>(denominator)
            : std::numeric_limits<float>::quiet_NaN();
    }

    inline void FinalizeNoveltyTelemetryRates(HairNoveltyFrameTelemetry& telemetry)
    {
        const uint32_t staticGroupCount =
            telemetry.staticCachedGroupCount + telemetry.staticTransformedGroupCount;

        telemetry.falseStaticRate = SafeTelemetryRate(
            telemetry.falseStaticCount,
            staticGroupCount,
            HasTelemetryMetric(telemetry, HairTelemetryAvailability_FalseStaticGroundTruth));

        telemetry.repairExecutionSuccessRate = SafeTelemetryRate(
            telemetry.repairSuccessCount,
            telemetry.repairAttemptCount,
            HasTelemetryMetric(telemetry, HairTelemetryAvailability_RepairVerification));

        telemetry.repairVerifiedSuccessRate = SafeTelemetryRate(
            telemetry.repairVerifiedCount,
            telemetry.repairAttemptCount,
            HasTelemetryMetric(telemetry, HairTelemetryAvailability_RepairVerification));
        telemetry.repairSuccessRate = telemetry.repairVerifiedSuccessRate;

        telemetry.predictiveWakePrecision = SafeTelemetryRate(
            telemetry.predictiveWakeTruePositiveCount,
            telemetry.predictiveWakeTruePositiveCount + telemetry.predictiveWakeFalsePositiveCount,
            HasTelemetryMetric(telemetry, HairTelemetryAvailability_WakeGroundTruth));

        telemetry.predictiveWakeRecall = SafeTelemetryRate(
            telemetry.predictiveWakeTruePositiveCount,
            telemetry.predictiveWakeTruePositiveCount + telemetry.predictiveWakeFalseNegativeCount,
            HasTelemetryMetric(telemetry, HairTelemetryAvailability_WakeGroundTruth));

        telemetry.collisionMissRate = SafeTelemetryRate(
            telemetry.collisionMissCount,
            telemetry.collisionCandidateCount,
            HasTelemetryMetric(telemetry, HairTelemetryAvailability_CollisionGroundTruth));

        telemetry.stateOscillationRate = SafeTelemetryRate(
            telemetry.stateOscillationCount,
            telemetry.stateTransitionCount,
            HasTelemetryMetric(telemetry, HairTelemetryAvailability_Scenario));

        telemetry.directionCacheMemorySavingRate = telemetry.fullPositionMemoryMb > 0.0f
            ? 1.0f - telemetry.directionCacheMemoryMb / telemetry.fullPositionMemoryMb
            : std::numeric_limits<float>::quiet_NaN();

        GetHairFormalEvidenceRecorder().Record(telemetry);
    }

    inline HairNoveltyFrameTelemetry MakeZeroNoveltyTelemetry()
    {
        return HairNoveltyFrameTelemetry{};
    }
}
