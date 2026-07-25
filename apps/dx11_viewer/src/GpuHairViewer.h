#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/GlobalHairBudget.h"
#include "hairgpu/HairNoveltyFlags.h"
#include "hairgpu/HairNoveltyTelemetry.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/BoneAnimation.h"
#include "hairgpu/HybridRuntime.h"
#include "hairgpu/RenderData.h"
#include "hairgpu/Types.h"

namespace hairgpu::viewer
{
    class GpuHairViewer
    {
    public:
        bool Initialize(HINSTANCE instance, int showCommand);
        int Run();
        int RunNativeSelfTests() const;
        int ExportCanonicalFrozenAssetSource(const std::filesystem::path& outputDirectory);
        int ValidateCanonicalFrozenAssetSource(const std::filesystem::path& sourceDirectory);
        bool ExportLocalDebugCapture(const std::filesystem::path& outputDirectory, const std::wstring& runName) const;
        bool FinalizeLocalDebugStreaming();
        bool CaptureBackBufferBmp(const std::filesystem::path& outputPath);

    private:
        static constexpr uint32_t kFixedGuideCount = 4000u;
        static constexpr uint32_t kFixedFollowersPerGuide = 24u;
        static constexpr uint32_t kFixedDisplayStrandCount = 100000u;
        static constexpr uint32_t kFixedParticlesPerStrand = 128u;
        static constexpr uint32_t kFixedSolverIterations = 6u;
        static constexpr float kFixedTimeStepSeconds = 1.0f / 60.0f;
        static constexpr size_t kMaximumUndoCount = 16u;
        static_assert(
            kFixedGuideCount * (kFixedFollowersPerGuide + 1u) == kFixedDisplayStrandCount,
            "The fixed workload must remain exactly 100,000 strands.");

        struct StrandEditSnapshot;

        struct GpuParticle
        {
            float x, y, z, w;
        };

        struct FollowGpuData
        {
            int32_t parentGuide = -1;
            float followWeight = 1.0f;
            float clumpWeight = 0.0f;
            float noiseWeight = 0.0f;
        };

        struct TriangleGpuData
        {
            uint32_t a = 0;
            uint32_t b = 0;
            uint32_t c = 0;
            uint32_t _padding = 0;
        };

        struct RootBindingGpuData
        {
            uint32_t triangleIndex = 0;
            float barycentric[3] = { 1.0f, 0.0f, 0.0f };
            float rootUv[2] = { 0.0f, 0.0f };
            float _padding[2] = { 0.0f, 0.0f };
        };

        enum class SegmentGroupRuntimeState : uint32_t
        {
            StaticCached = 0u,
            StaticTransformed = 1u,
            Dynamic = 2u,
            Deferred = 3u,
            Repair = 4u
        };

        enum SegmentGroupDirtyReasonBits : uint32_t
        {
            SegmentGroupDirty_None            = 0u,
            SegmentGroupDirty_ParentMoved     = 1u << 0u,
            SegmentGroupDirty_HeadMoved       = 1u << 1u,
            SegmentGroupDirty_ColliderEntered = 1u << 2u,
            SegmentGroupDirty_ColliderChanged = 1u << 3u,
            SegmentGroupDirty_NeighborChanged = 1u << 4u,
            SegmentGroupDirty_Edited          = 1u << 5u,
            SegmentGroupDirty_CacheExpired    = 1u << 6u,
            SegmentGroupDirty_BoundaryFailed  = 1u << 7u,
            SegmentGroupDirty_Watchdog        = 1u << 8u
        };

        struct alignas(16) SegmentGroupGpuData
        {
            uint32_t strandId = 0u;
            uint32_t groupId = 0u;
            uint32_t parentGroupId = std::numeric_limits<uint32_t>::max();
            uint32_t childGroupId = std::numeric_limits<uint32_t>::max();

            uint32_t startPointIndex = 0u;
            uint32_t endPointIndex = 0u;
            uint32_t ownerGroupId = 0u;
            uint32_t blockId = 0u;

            uint32_t state =
                static_cast<uint32_t>(SegmentGroupRuntimeState::Dynamic);
            uint32_t previousState =
                static_cast<uint32_t>(SegmentGroupRuntimeState::Dynamic);
            uint32_t dirtyReason = SegmentGroupDirty_CacheExpired;
            uint32_t flags = 0u;

            float restLength = 0.0f;
            float currentLength = 0.0f;
            float motionScore = 1.0f;
            float collisionRisk = 0.0f;

            float direction[3] = { 0.0f, 1.0f, 0.0f };
            float boundaryErrorMm = 0.0f;

            float previousDirection[3] = { 0.0f, 1.0f, 0.0f };
            float errorBoundMm = 0.0f;

            uint32_t lastSolvedFrame = 0u;
            uint32_t lastRepairFrame = 0u;
            uint32_t stateStableFrameCount = 0u;
            uint32_t reserved = 0u;
        };

        struct alignas(16) SegmentGroupBlockGpuData
        {
            uint32_t firstGroupIndex = 0u;
            uint32_t groupCount = 0u;
            uint32_t strandId = 0u;
            uint32_t stateMask = 0u;

            uint32_t dirtyMask = SegmentGroupDirty_CacheExpired;
            uint32_t dynamicGroupCount = 0u;
            uint32_t repairGroupCount = 0u;
            uint32_t reserved0 = 0u;

            float maxMotionScore = 1.0f;
            float maxCollisionRisk = 0.0f;
            float maxBoundaryErrorMm = 0.0f;
            float reserved1 = 0.0f;
        };

        struct alignas(16) SegmentGroupCertificateGpuData
        {
            uint32_t sourceFrame = 0u;
            uint32_t parentVersion = 0u;
            uint32_t colliderVersion = 0u;
            uint32_t editVersion = 0u;

            float maximumPositionErrorMm = 0.0f;
            float maximumDirectionErrorDeg = 0.0f;
            float maximumLengthErrorRatio = 0.0f;
            float maximumBoundaryErrorMm = 0.0f;

            uint32_t parentValid = 0u;
            uint32_t collisionValid = 0u;
            uint32_t boundaryValid = 0u;
            uint32_t editValid = 0u;

            uint32_t cacheValid = 0u;
            uint32_t reserved0 = 0u;
            uint32_t reserved1 = 0u;
            uint32_t reserved2 = 0u;
        };

        struct alignas(16) SegmentGroupRuntimeCounters
        {
            uint32_t totalGroupCount = 0u;
            uint32_t staticCachedCount = 0u;
            uint32_t staticTransformedCount = 0u;
            uint32_t dynamicCount = 0u;

            uint32_t deferredCount = 0u;
            uint32_t repairCount = 0u;
            uint32_t fallbackCount = 0u;
            uint32_t falseStaticCount = 0u;

            uint32_t dirtyBlockCount = 0u;
            uint32_t dirtyGroupCount = 0u;
            uint32_t cacheHitCount = 0u;
            uint32_t cacheMissCount = 0u;

            uint32_t cacheInvalidationCount = 0u;
            uint32_t watchdogCheckCount = 0u;
            uint32_t watchdogRepairCount = 0u;
            uint32_t reserved0 = 0u;

            float boundaryErrorSumMm = 0.0f;
            float boundaryErrorMaxMm = 0.0f;
            float directionCacheMemoryMb = 0.0f;
            float fullPositionMemoryMb = 0.0f;
        };

        static_assert(
            sizeof(SegmentGroupGpuData) % 16u == 0u,
            "SegmentGroupGpuData must stay 16-byte aligned");

        static_assert(
            sizeof(SegmentGroupBlockGpuData) % 16u == 0u,
            "SegmentGroupBlockGpuData must stay 16-byte aligned");

        static_assert(
            sizeof(SegmentGroupCertificateGpuData) % 16u == 0u,
            "SegmentGroupCertificateGpuData must stay 16-byte aligned");

        static_assert(
            sizeof(SegmentGroupRuntimeCounters) % 16u == 0u,
            "SegmentGroupRuntimeCounters must stay 16-byte aligned");

        struct DebugLineTemplateVertex
        {
            float endpointId = 0.0f;
        };

        struct GpuDebugLineEntry
        {
            uint32_t type = 0u;
            uint32_t indexA = 0u;
            uint32_t indexB = 0u;
            uint32_t axisOrFlags = 0u;
            float data0[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float data1[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        };

        struct DebugLineStats
        {
            uint32_t worldLineCount = 0u;
            uint32_t hairLineCount = 0u;
            uint32_t scalpLineCount = 0u;
            uint32_t rootMarkerCount = 0u;
            uint32_t totalLineCount = 0u;
        };

        struct ScalpVertex
        {
            uint32_t scalpIndex = 0u;
            float nx = 0.0f, ny = 1.0f, nz = 0.0f;
            float r = 0.76f, g = 0.62f, b = 0.52f;
        };

        struct HairMeshVertex
        {
            float x, y, z, w;
            float r, g, b, a;
        };

        struct BrickVertex
        {
            float px, py, pz;
            float nx, ny, nz;
        };

        struct BrickInstance
        {
            uint32_t particleIndex = 0;
            uint32_t strandIndex = 0;
        };

        enum class PerformanceEvidenceClass : uint32_t
        {
            InteractiveRealtime = 0u,
            DiagnosticDebugger = 1u,
            FormalThroughput = 2u
        };

        struct PerformanceSample
        {
            float elapsedSeconds = 0.0f;
            float frameMilliseconds = 0.0f;
            float frameIntervalMilliseconds = 0.0f;
            float cpuFrameWorkMilliseconds = 0.0f;
            float simulationMilliseconds = 0.0f;
            // Explicit cadence metrics. fps/frameMilliseconds remain compatibility
            // aliases for renderFps/renderFrameMilliseconds in existing exporters.
            float fps = 0.0f;
            float renderFps = 0.0f;
            float simulationHz = 0.0f;
            float renderFrameMilliseconds = 0.0f;
            float gpuFrameMilliseconds = 0.0f;
            float gpuSimulationMilliseconds = 0.0f;
            float gpuHairDrawMilliseconds = 0.0f;
            float gpuThroughputFps = 0.0f;
            uint32_t simulationTicksThisFrame = 0u;
            float droppedSimulationMilliseconds = 0.0f;
            uint32_t presentSyncInterval = 0u;
            uint32_t evidenceClass =
                static_cast<uint32_t>(PerformanceEvidenceClass::InteractiveRealtime);
            uint32_t guideCount = 0;
            uint32_t followerCount = 0;
            uint32_t totalStrandCount = 0;
            uint32_t totalParticleCount = 0;
            uint32_t activeSetClusterCount = 0;
            uint32_t activeSetSelectedClusterCount = 0;
            uint32_t activeSetSelectedGuideCount = 0;
            uint32_t activeSetSelectedStrandCount = 0;
            float activeSetHighestScore = 0.0f;
            float activeSetAverageSelectedScore = 0.0f;
            float activeSetEvaluationMilliseconds = 0.0f;
            float activeSetBudgetScale = 1.0f;
            uint32_t activeSetRecommendedSolverIterations = 0u;
            uint32_t fullTierCount = 0u;
            uint32_t reducedTierCount = 0u;
            uint32_t frozenTierCount = 0u;
            uint32_t cachedTierCount = 0u;
            uint32_t correctiveStrandCount = 0u;
            uint32_t correctivePointCount = 0u;
            float correctiveAveragePenetration = 0.0f;
            float correctiveMaxPenetration = 0.0f;
            float correctiveAverageLengthErrorBefore = 0.0f;
            float correctiveAverageLengthErrorAfter = 0.0f;
            uint32_t globalFrameIndex = 0u;
            uint32_t measuredFrameIndex = 0u;
            uint32_t sourceFrameIndex = 0u;
            uint64_t availabilityMask = 0u;
            uint64_t metricAvailabilityMask = 0u;
            uint32_t unavailableMetricCount = 0u;
            uint32_t syntheticMetricCount = 0u;
            uint32_t fullCpuGroupUploadCount = 0u;
            uint32_t sameFrameReadbackCount = 0u;
            uint32_t nanStateCount = 0u;
            uint32_t infiniteStateCount = 0u;
            uint32_t fatalFailure = 0u;
            uint32_t fatalFailureCode = 0u;
            uint32_t scenarioAppliedEventCount = 0u;
            uint32_t scenarioAssertionFailureCount = 0u;
            uint32_t collisionCandidateCount = 0u;
            uint32_t collisionMissCount = 0u;
            uint32_t penetrationEventCount = 0u;
            float meanPenetrationMm = 0.0f;
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
            float predictiveWakePrecision = 0.0f;
            float predictiveWakeRecall = 0.0f;
            uint32_t repairAttemptCount = 0u;
            uint32_t repairSuccessCount = 0u;
            uint32_t repairVerifiedCount = 0u;
            uint32_t repairUnverifiedCount = 0u;
            uint32_t repairFailureCount = 0u;
            uint32_t repairRecurrenceCount = 0u;
            uint32_t repairLatencyFrameSum = 0u;
            uint32_t repairLatencyFrameMax = 0u;
            float repairExecutionSuccessRate = 0.0f;
            float repairVerifiedSuccessRate = 0.0f;
            float repairRecurrenceRate = 0.0f;
            uint32_t stateTransitionCount = 0u;
            uint32_t stateOscillationCount = 0u;
            uint32_t determinismMismatchCount = 0u;
            uint64_t stateSequenceHash = 0ull;
            uint64_t compactionSequenceHash = 0ull;
            uint64_t finalPositionHash = 0ull;

            uint32_t topLevelNoveltyMask = 0u;
            uint32_t overflowCount = 0u;
            uint32_t budgetDropCount = 0u;
            uint32_t deferCount = 0u;
            uint32_t microtypeDecodedCount = 0u;
            uint32_t safeDecodeCount = 0u;
            uint32_t falseSafeCount = 0u;
            uint32_t totalSegmentGroupCount = 0u;
            uint32_t staticCachedGroupCount = 0u;
            uint32_t staticTransformedGroupCount = 0u;
            uint32_t dynamicGroupCount = 0u;
            uint32_t deferredCorrectionGroupCount = 0u;
            uint32_t repairRequiredGroupCount = 0u;
            uint32_t fallbackGroupCount = 0u;
            uint32_t falseStaticCount = 0u;
            float falseStaticRate = 0.0f;
            uint32_t cacheHitCount = 0u;
            uint32_t cacheMissCount = 0u;
            uint32_t cacheInvalidationCount = 0u;
            uint32_t watchdogCheckCount = 0u;
            uint32_t watchdogRepairCount = 0u;
            float staticDynamicBoundaryMeanErrorMm = 0.0f;
            float staticDynamicBoundaryMaxErrorMm = 0.0f;
            float directionCacheMemoryMb = 0.0f;
            float fullPositionMemoryMb = 0.0f;
            float directionCacheMemorySavingRate = 0.0f;
            uint32_t typedRepairRequestCount = 0u;
            uint32_t floatFallbackRequestCount = 0u;
            uint32_t rejectPayloadCount = 0u;
            uint32_t riskyStrandCount = 0u;
            uint32_t repairCommitCount = 0u;
            uint32_t repairRejectCount = 0u;
            uint32_t repairDeferCount = 0u;
            uint32_t islandCandidateCount = 0u;
            uint32_t physicsFallbackCount = 0u;
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
            uint32_t sharedPointConflictCount = 0u;
            uint32_t hemisphereParentResolveCount = 0u;
            uint32_t hemisphereChildResolveCount = 0u;
            uint32_t dynamicCompactionInputCount = 0u;
            uint32_t dynamicCompactionOutputCount = 0u;
            uint32_t repairCompactionOutputCount = 0u;
            float blockClassificationGpuMilliseconds = 0.0f;
            float groupClassificationGpuMilliseconds = 0.0f;
            float compactionGpuMilliseconds = 0.0f;
            float predictiveWakeGpuMilliseconds = 0.0f;
            float wakePropagationGpuMilliseconds = 0.0f;
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
            uint32_t warmupFrames = 0u;
            uint32_t measurementFrames = 0u;
            uint32_t seed = 0u;
            uint32_t certificateFailureCount = 0u;
            float certificateFailureRate = 0.0f;
            uint32_t fallbackCount = 0u;
            uint32_t modeSkipCount = 0u;
            uint32_t modeQSOnlyCount = 0u;
            uint32_t modeDynamicCount = 0u;
            uint32_t modeIslandCount = 0u;
            uint32_t modeRepairCount = 0u;
            uint32_t modeFloatFallbackCount = 0u;
            uint32_t islandCount = 0u;
            uint32_t islandMemberCount = 0u;
            uint32_t islandCommitCount = 0u;
            uint32_t islandRejectCount = 0u;
            uint32_t repairTaskCount = 0u;
            uint32_t repairCommittedCount = 0u;
            uint32_t repairRejectedCount = 0u;
            uint32_t repairDeferredCount = 0u;
            float repairSuccessRate = 0.0f;
            uint32_t cpuReadbackCount = 0u;
            float gpuHairMilliseconds = 0.0f;
            float vramMegabytes = 0.0f;
            float bandwidthMegabytesPerFrame = 0.0f;
            float meanDecodeErrorMm = 0.0f;
            float maxPenetrationMm = 0.0f;
        };

        // HAIRGPU_LOCAL_DEBUG_PER_SECOND_V3: one durable row is committed for
        // every completed wall-clock second.  State/counter fields are sampled from
        // the last frame in the bucket; timing fields are averaged over the bucket.
        struct LocalDebugSecondAccumulator
        {
            uint64_t secondIndex = 1ull;
            double wallTimeStartSeconds = 0.0;
            double durationSeconds = 0.0;
            uint64_t frameCount = 0ull;
            uint64_t simulationTickCount = 0ull;
            double frameMillisecondsSum = 0.0;
            double cpuWorkMillisecondsSum = 0.0;
            double simulationMillisecondsSum = 0.0;
            double gpuFrameMillisecondsSum = 0.0;
            double gpuSimulationMillisecondsSum = 0.0;
            double gpuHairDrawMillisecondsSum = 0.0;
            double totalHairGpuMillisecondsSum = 0.0;
            double gpuThroughputFpsSum = 0.0;
            uint64_t gpuFrameSampleCount = 0ull;
            uint64_t gpuSimulationSampleCount = 0ull;
            uint64_t gpuHairDrawSampleCount = 0ull;
            uint64_t totalHairGpuSampleCount = 0ull;
            uint64_t gpuThroughputSampleCount = 0ull;
            std::vector<float> frameMilliseconds;
            PerformanceSample lastSample = {};
            bool hasSample = false;
        };

        // HAIRGPU_EXPERIMENT_RUNTIME_PATCH_V2
        enum class ExperimentMethodId : uint32_t
        {
            B0_FullDynamic = 0u,
            B1_StrandSleep,
            B2_BlockSleep,
            B3_SegmentThresholdSleep,
            B4_SegmentHysteresisSleep,
            B5_SegmentCache,
            B6_SegmentGpuCompaction,
            B7_ActiveSetOnly,
            B8_ExternalStateArray,
            P_IntegratedSegmentGroup,
            Unknown
        };

        enum class ExperimentRunStatus : uint32_t
        {
            InProgress = 0u,
            CompleteValid,
            CompleteInvalid,
            Incomplete,
            Failed,
            Synthetic
        };

        enum class ExperimentScenarioId : uint32_t
        {
            S01_Idle = 0u, S02_Breathing, S03_Walk, S04_SuddenStop,
            S05_HeadRotation, S06_Teleport, S07_ColliderApproach,
            S08_StaticDynamicBoundaryContact, S09_StrongWind, S10_WindStop,
            S11_LocalEdit, S12_FalseStaticTrap, S13_RepairStorm,
            S14_AllDynamic, S15_AllStatic, S16_StateThrashing,
            S17_CloseCamera100k, S18_FarCamera100k, Unknown
        };

        enum class ScenarioEventType : uint32_t
        {
            SetWind = 0u, SetRootTranslation, SetRootRotation, TeleportRoot,
            SetColliderTransform, InjectLocalEdit, InjectRepairStorm,
            ForceAllDynamic, ForceAllStatic, SetCameraDistance, Unknown
        };

        struct ScenarioEvent
        {
            uint32_t frame = 0u;
            ScenarioEventType type = ScenarioEventType::Unknown;
            std::array<float, 4> value = { 0.0f, 0.0f, 0.0f, 0.0f };
            uint32_t targetIndex = 0u;
        };

        struct ScenarioTimeline
        {
            ExperimentScenarioId scenarioId = ExperimentScenarioId::Unknown;
            uint32_t durationFrames = 0u;
            float fixedTimeStepSeconds = kFixedTimeStepSeconds;
            bool seedDriven = true;
            std::wstring sourcePath;
            std::wstring sha256;
            std::vector<ScenarioEvent> events;
        };

        enum class MeasurementMode : uint32_t
        {
            Performance = 0u,
            Quality = 1u,
            Determinism = 2u,
            Robustness = 3u
        };

        enum class SegmentGroupStateStorage : uint32_t
        {
            IntegratedRecord = 0u,
            ExternalArray = 1u
        };

        struct ExperimentMethodFlags
        {
            bool enableNoNoveltyBaseline = false;
            bool enableMicrotype = false;
            bool enableBudgetedPhysics = false;
            bool enableCertificateRender = false;
            bool enablePerStrandEdit = false;
            bool enableCBMA = false;
            bool enablePhysicsIslands = false;
            bool enableRodRepair = false;
            bool enableGpuResident = false;
            bool enableSegmentGroupSleep = false;
            bool enableSegmentGroupIntegratedState = true;
            bool enableExternalStateArray = false;
            bool enableSharedPointOwnership = true;
            bool enableDirectionalResponsibility = true;
            bool enableStaticCertificate = true;
            bool enablePredictiveWake = true;
            bool enableVelocityRecovery = true;
            bool enableRepair = true;
            bool enableGpuCompaction = true;
            bool enableActiveSet = true;
            bool enableStrandSleep = false;
            bool enableBlockSleep = false;
            bool enableSleepHysteresis = true;
            bool enableStaticCache = true;
            SegmentGroupStateStorage stateStorage = SegmentGroupStateStorage::IntegratedRecord;
        };

        struct ExperimentCaseConfig
        {
            bool enabled = false;
            bool autoExitAfterCapture = false;
            bool fixedTimeStepEnabled = true;
            bool vsyncEnabled = false;
            bool strictValidationEnabled = true;
            bool realMeasurement = false;
            bool hardwareAdapterRequired = true;
            bool methodFlagsAcknowledged = false;
            ExperimentMethodId methodId = ExperimentMethodId::Unknown;
            ExperimentScenarioId scenarioId = ExperimentScenarioId::Unknown;
            ExperimentRunStatus runStatus = ExperimentRunStatus::InProgress;
            ScenarioTimeline timeline = {};
            bool timelineLoaded = false;
            std::wstring caseId;
            std::wstring variant;
            std::wstring scenario;
            std::wstring measurementModeName = L"performance";
            std::wstring caseJsonPath;
            std::wstring scenarioTimelinePath;
            std::wstring assetManifestPath;
            std::wstring formalAssetPath;
  std::wstring formalAssetSha256;
  std::wstring formalTopologySha256;
  std::wstring generatorSettingsSha256;
  std::wstring observedAssetPath;
  bool formalAssetLoaded = false;
            std::wstring settingsHash;
            std::wstring executableSha256;
            std::wstring commitSha;
            std::wstring buildConfiguration = L"Release";
            std::wstring platform = L"x64";
            std::wstring hardwareProfile;
            std::wstring scenarioTimelineSha256;
            std::wstring assetManifestSha256;
            std::wstring assetSha256;
            std::wstring requestedMethodSettingsSha256;
            std::wstring appliedMethodSettingsSha256;
            std::wstring observedMethodSettingsSha256;
            std::wstring shaderBundleSha256;
            std::wstring canonicalResultSha256;
            std::wstring observedGpuName;
            bool assetFrozen = false;
            uint32_t seed = 0u;
            uint32_t repetitionIndex = 0u;
            uint32_t warmupFrames = 0u;
            uint32_t measurementFrames = 0u;
            uint32_t requestedWidth = 1920u;
            uint32_t requestedHeight = 1080u;
            uint32_t observedRenderWidth = 0u;
            uint32_t observedRenderHeight = 0u;
            uint32_t observedDisplayStrandCount = 0u;
            uint32_t observedGuideCount = 0u;
            uint32_t observedParticlesPerGuide = 0u;
            uint32_t observedSolverIterations = 0u;
            bool observedVsyncEnabled = true;
            uint32_t observedPresentSyncInterval = 1u;
            uint32_t requestedGuideCount = kFixedGuideCount;
            uint32_t requestedDisplayStrandCount = kFixedDisplayStrandCount;
            uint32_t requestedParticlesPerGuide = kFixedParticlesPerStrand;
            uint32_t requestedSolverIterations = kFixedSolverIterations;
            uint32_t presentSyncInterval = 0u;
            uint32_t gpuTimestampDelayFrames = 4u;
            uint32_t scenarioAssertionMinAppliedEvents = 0u;
            uint32_t scenarioAssertionMinCollisionCandidates = 0u;
            uint32_t scenarioAssertionMinWakeRequests = 0u;
            uint32_t scenarioAssertionMinRepairs = 0u;
            uint32_t scenarioAssertionMinTransitions = 0u;
            float fixedTimeStepSeconds = 1.0f / 60.0f;
            MeasurementMode measurementMode = MeasurementMode::Performance;
            ExperimentMethodFlags requestedFlags = {};
            ExperimentMethodFlags appliedFlags = {};
            ExperimentMethodFlags observedFlags = {};

            // Compatibility mirrors retained for existing exporters.
            bool enableNoNoveltyBaseline = false;
            bool enableMicrotype = false;
            bool enableBudgetedPhysics = false;
            bool enableCertificateRender = false;
            bool enablePerStrandEdit = false;
            bool enableCBMA = false;
            bool enablePhysicsIslands = false;
            bool enableRodRepair = false;
            bool enableGpuResident = false;
        };

        struct Float4
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 0.0f;
        };

        struct alignas(16) SegmentGroupRuntimeConstants
        {
            uint32_t groupCount = 0u;
            uint32_t blockCount = 0u;
            uint32_t frameIndex = 0u;
            uint32_t groupsPerBlock = 64u;
            uint32_t dynamicCompactCount = 0u;
            uint32_t staticCompactCount = 0u;
            uint32_t repairCompactCount = 0u;
            uint32_t repairVerificationHoldFrames = 12u;
            float deltaTime = 1.0f / 60.0f;
            float motionWakeThreshold = 0.02f;
            float collisionWakeThreshold = 0.10f;
            float staticMotionThreshold = 0.005f;
            float lengthErrorThreshold = 0.02f;
            float boundaryErrorThresholdMm = 2.0f;
            float velocityRecoveryBlend = 0.75f;
            float positionRecoveryBlend = 0.35f;
            float rootTranslation[3] = { 0.0f, 0.0f, 0.0f };
            float padding0 = 0.0f;
        };

        struct alignas(16) SimConstantsHair
        {
            uint32_t totalStrandCount = 0;
            uint32_t guideStrandCount = 0;
            uint32_t followerStrandCount = 0;
            uint32_t particlesPerStrand = 0;

            uint32_t solverIterations = 0;
            uint32_t sphereColliderCount = 0;
            uint32_t capsuleColliderCount = 0;
            uint32_t enableSelfCollision = 0;
            uint32_t planeColliderCount = 0;
            uint32_t aabbColliderCount = 0;
            uint32_t useSdfCollider = 0;
            uint32_t sdfValueCount = 0;

            float deltaTime = 0.0f;
            float gravity = 9.81f;
            float damping = 0.03f;
            float segmentLength = 0.035f;

            float bendStiffness = 0.12f;
            float followStiffness = 0.92f;
            float windStrength = 0.0f;
            float floorY = -1.2f;

            float elapsedTime = 0.0f;
            float selfCollisionRadius = 0.012f;
            float selfCollisionStiffness = 0.35f;
            float scalpMotionBlend = 1.0f;

            float stretchCompliance = 2.0e-7f;
            float bendCompliance = 1.0e-5f;
            float rootStiffness = 0.18f;
            float velocityShockPropagation = 0.72f;

            float windDirection[3] = { 1.0f, 0.0f, 0.0f };
            float gridCellSize = 0.085f;

            uint32_t gridDimX = 18;
            uint32_t gridDimY = 18;
            uint32_t gridDimZ = 18;
            uint32_t gridCellCapacity = 24;

            float gridOrigin[3] = { -0.72f, 0.48f, -0.72f };
            uint32_t enableGridSelfCollision = 1;

            float looseConstraintStiffness = 0.12f;
            float looseConstraintMaxDistance = 0.085f;
            float supportCollisionRadius = 0.030f;
            float supportCollisionStiffness = 0.26f;

            float tangentialFriction = 0.06f;
            float lateralFriction = 0.78f;
            float freezeBackThreshold = -0.045f;
            float freezeBlend = 0.82f;

            uint32_t broadphaseMode = 0u;
            uint32_t broadphaseHashBucketCount = 32768u;
            uint32_t broadphasePadding0 = 0u;
            uint32_t broadphasePadding1 = 0u;

            uint32_t sortedPaddedEntryCount = 1u;
            uint32_t enableRuntimeAngularBend = 1u;
            uint32_t dispatchBaseIndex = 0u;
            uint32_t enableCatastrophicRecovery = 0u;

            float angularBendRadians = 0.18f;
            float angularBendCompliance = 5.0e-6f;
            float angularBendStiffness = 0.35f;
            float angularBendDamping = 0.08f;

            Float4 sphereData[4] = {};
            Float4 capsuleData[8] = {};
            Float4 planeData[4] = {};
            Float4 aabbData[8] = {};
            uint32_t sdfResolution[4] = {};
            Float4 sdfBoundsMin = {};
            Float4 sdfBoundsMax = {};

            uint32_t useCompactedGuideDispatch = 0u;
            uint32_t compactedGuideCount = 0u;
            uint32_t followerUpdateStride = 1u;
            uint32_t followerUpdatePhase = 0u;
            uint32_t useCompactedFollowerDispatch = 0u;
            uint32_t compactedFollowerCount = 0u;
            uint32_t useCompactedGuidePointDispatch = 0u;
            uint32_t compactedGuidePointCount = 0u;
        };

        struct alignas(16) SimConstantsActiveSet
        {
            uint32_t totalStrandCount = 0;
            uint32_t guideStrandCount = 0;
            uint32_t followerStrandCount = 0;
            uint32_t particlesPerStrand = 0;

            uint32_t solverIterations = 0;
            uint32_t sphereColliderCount = 0;
            uint32_t capsuleColliderCount = 0;
            uint32_t enableSelfCollision = 0;
            uint32_t planeColliderCount = 0;
            uint32_t aabbColliderCount = 0;
            uint32_t useSdfCollider = 0;
            uint32_t sdfValueCount = 0;

            float deltaTime = 0.0f;
            float gravity = 9.81f;
            float damping = 0.03f;
            float segmentLength = 0.035f;

            float bendStiffness = 0.12f;
            float followStiffness = 0.92f;
            float windStrength = 0.0f;
            float floorY = -1.2f;

            float elapsedTime = 0.0f;
            float selfCollisionRadius = 0.012f;
            float selfCollisionStiffness = 0.35f;
            float scalpMotionBlend = 1.0f;

            float stretchCompliance = 2.0e-7f;
            float bendCompliance = 1.0e-5f;
            float rootStiffness = 0.18f;
            float velocityShockPropagation = 0.72f;

            float windDirection[3] = { 1.0f, 0.0f, 0.0f };
            float gridCellSize = 0.085f;

            uint32_t gridDimX = 18;
            uint32_t gridDimY = 18;
            uint32_t gridDimZ = 18;
            uint32_t gridCellCapacity = 24;

            float gridOrigin[3] = { -0.72f, 0.48f, -0.72f };
            uint32_t enableGridSelfCollision = 1;

            float looseConstraintStiffness = 0.12f;
            float looseConstraintMaxDistance = 0.085f;
            float supportCollisionRadius = 0.030f;
            float supportCollisionStiffness = 0.26f;

            float tangentialFriction = 0.06f;
            float lateralFriction = 0.78f;
            float freezeBackThreshold = -0.045f;
            float freezeBlend = 0.82f;

            uint32_t broadphaseMode = 0u;
            uint32_t broadphaseHashBucketCount = 32768u;
            uint32_t broadphasePadding0 = 0u;
            uint32_t broadphasePadding1 = 0u;

            uint32_t sortedPaddedEntryCount = 1u;
            uint32_t enableRuntimeAngularBend = 1u;
            uint32_t runtimePadding0 = 0u;
            uint32_t runtimePadding1 = 0u;

            float angularBendRadians = 0.18f;
            float angularBendCompliance = 5.0e-6f;
            float angularBendStiffness = 0.35f;
            float angularBendDamping = 0.08f;

            Float4 sphereData[4] = {};
            Float4 capsuleData[8] = {};
            Float4 planeData[4] = {};
            Float4 aabbData[8] = {};
            uint32_t sdfResolution[4] = {};
            Float4 sdfBoundsMin = {};
            Float4 sdfBoundsMax = {};

            uint32_t followerUpdateStride = 1u;
            uint32_t followerUpdatePhase = 0u;
            uint32_t useCompactedFollowerDispatch = 0u;
            uint32_t compactedFollowerCount = 0u;
        };

        struct alignas(16) SimConstantsScalp
        {
            uint32_t totalStrandCount = 0;
            uint32_t guideStrandCount = 0;
            uint32_t followerStrandCount = 0;
            uint32_t particlesPerStrand = 0;

            uint32_t solverIterations = 0;
            uint32_t sphereColliderCount = 0;
            uint32_t capsuleColliderCount = 0;
            uint32_t enableSelfCollision = 0;
            uint32_t planeColliderCount = 0;
            uint32_t aabbColliderCount = 0;
            uint32_t useSdfCollider = 0;
            uint32_t sdfValueCount = 0;

            float deltaTime = 0.0f;
            float gravity = 9.81f;
            float damping = 0.03f;
            float segmentLength = 0.035f;

            float bendStiffness = 0.12f;
            float followStiffness = 0.92f;
            float windStrength = 0.0f;
            float floorY = -1.2f;

            float elapsedTime = 0.0f;
            float selfCollisionRadius = 0.012f;
            float selfCollisionStiffness = 0.35f;
            float scalpMotionBlend = 1.0f;

            float scalpCenter[4] = {};
            float neckPivot[4] = {};

            float stretchCompliance = 2.0e-7f;
            float bendCompliance = 1.0e-5f;
            float rootStiffness = 0.18f;
            float velocityShockPropagation = 0.72f;

            float windDirection[3] = { 1.0f, 0.0f, 0.0f };
            float gridCellSize = 0.085f;

            uint32_t gridDimX = 18;
            uint32_t gridDimY = 18;
            uint32_t gridDimZ = 18;
            uint32_t gridCellCapacity = 24;

            float gridOrigin[3] = { -0.72f, 0.48f, -0.72f };
            uint32_t enableGridSelfCollision = 1;

            float looseConstraintStiffness = 0.12f;
            float looseConstraintMaxDistance = 0.085f;
            float supportCollisionRadius = 0.030f;
            float supportCollisionStiffness = 0.26f;

            float tangentialFriction = 0.06f;
            float lateralFriction = 0.78f;
            float freezeBackThreshold = -0.045f;
            float freezeBlend = 0.82f;

            uint32_t broadphaseMode = 0u;
            uint32_t broadphaseHashBucketCount = 32768u;
            uint32_t broadphasePadding0 = 0u;
            uint32_t broadphasePadding1 = 0u;

            uint32_t sortedPaddedEntryCount = 1u;
            uint32_t enableRuntimeAngularBend = 1u;
            uint32_t runtimePadding0 = 0u;
            uint32_t runtimePadding1 = 0u;

            float angularBendRadians = 0.18f;
            float angularBendCompliance = 5.0e-6f;
            float angularBendStiffness = 0.35f;
            float angularBendDamping = 0.08f;

            Float4 sphereData[4] = {};
            Float4 capsuleData[8] = {};
            Float4 planeData[4] = {};
            Float4 aabbData[8] = {};
            uint32_t sdfResolution[4] = {};
            Float4 sdfBoundsMin = {};
            Float4 sdfBoundsMax = {};
        };

        struct alignas(16) BitonicConstants
        {
            uint32_t j = 0u;
            uint32_t k = 0u;
            uint32_t entryCount = 0u;
            uint32_t _padding = 0u;
        };

        enum class RenderMode : uint32_t
        {
            ParticlePointSprite = 0,
            ParticleBillboard = 1,
            StrandRibbon = 2,
        };

        enum class InteractionMode : uint32_t
        {
            View = 0,
            Edit = 1,
        };

        enum class WorkspaceMode : uint32_t
        {
            Groom = 0,
            Sim = 1,
            Review = 2,
            Report = 3,
        };

        enum class GroomTool : uint32_t
        {
            Select = 0,
            Comb = 1,
            Grab = 2,
            Smooth = 3,
            Length = 4,
            Volume = 5,
            Part = 6,
            Freeze = 7,
            StrandFix = 8,
            Clump = 9,
            Curl = 10,
            Unclump = 11,
        };

        enum class BottomTrayTab : uint32_t
        {
            Layers = 0,
            History = 1,
            Metrics = 2,
        };

        enum class DebugColorMode : uint32_t
        {
            Natural = 0,
            GuideFollower = 1,
            WidthHeatmap = 2,
            Editability = 3,
        };

        enum class MaskOverlayMode : uint32_t
        {
            None = 0,
            Parting = 1,
            Region = 2,
            EditInfluence = 3,
            EditInfluenceBeforeAfter = 4,
            ActiveSetRiskSelection = 5,
            ActiveSetMotion = 6,
            ActiveSetContact = 7,
            ActiveSetUncertainty = 8,
            ActiveSetEdit = 9,
        };

        enum class CpuReadbackMode : uint32_t
        {
            Off = 0,
            OnDemandVisibility = 1,
            FullParticlesDebug = 2,
        };

        enum class StrandGizmoMode : uint32_t
        {
            TipTranslate = 0,
            WholeTranslate = 1,
            RootTranslate = 2,
        };

        enum class GizmoAxis : uint32_t
        {
            None = 0,
            X = 1,
            Y = 2,
            Z = 3,
        };

        struct alignas(16) RenderConstants
        {
            float viewProjection[16] = {};
            float cameraPosition[4] = {};
            float lightDirection[4] = { -0.4f, 0.9f, 0.2f, 0.0f };
            float widthScale = 1.0f;
            uint32_t debugColorMode = 0;
            uint32_t guideStrandCount = 0;
            uint32_t particlesPerStrand = 0;
            uint32_t totalStrandCount = 0;
            uint32_t tubeRingSides = 0;
            uint32_t renderFollowerStride = 1u;
            uint32_t renderGuideOnlyAboveTier = 4u;
            float renderReducedWidthBoost = 0.82f;
            float renderHiddenWidthThreshold = 0.0f;
            float renderStrandAlpha = 1.0f;
            float renderPadding0 = 0.0f;
        };

        static_assert(sizeof(SegmentGroupRuntimeConstants) == 80u, "SegmentGroupRuntimeConstants must match HairSegmentGroupRuntime.hlsl");
        static_assert(sizeof(SimConstantsHair) % 16u == 0u, "SimConstantsHair must stay 16-byte aligned");
        static_assert(sizeof(SimConstantsActiveSet) % 16u == 0u, "SimConstantsActiveSet must stay 16-byte aligned");
        static_assert(sizeof(SimConstantsScalp) % 16u == 0u, "SimConstantsScalp must stay 16-byte aligned");
        static_assert(sizeof(SimConstantsHair) == 704u, "SimConstantsHair must stay in sync with HairSim/HairIndirectArgs HLSL");
        static_assert(sizeof(SimConstantsActiveSet) == 688u, "SimConstantsActiveSet must stay in sync with HairActiveSet HLSL");
        static_assert(sizeof(SimConstantsScalp) == 704u, "SimConstantsScalp must stay in sync with ScalpSim HLSL");
        static_assert(sizeof(RenderConstants) == 144u, "RenderConstants must stay in sync with HLSL render constant buffers");

        struct StructuredBuffer
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        };

        enum class GpuPassId : uint32_t
        {
            BlockClassification = 0u,
            GroupClassification,
            Compaction,
            PredictiveWake,
            StaticTransform,
            DynamicSolve,
            VelocityRecovery,
            RepairSolve,
            BoundaryRepair,
            CacheUpdate,
            Watchdog,
            FollowerReconstruction,
            RenderPreparation,
            HairDraw,
            SelfCollisionBroadphase,
            SelfCollisionSolve,
            TelemetryReduction,
            Count
        };
        static constexpr size_t kGpuPassCount = static_cast<size_t>(GpuPassId::Count);

        struct GpuTimingFrame
        {
            Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
            Microsoft::WRL::ComPtr<ID3D11Query> frameStart;
            Microsoft::WRL::ComPtr<ID3D11Query> simStart;
            Microsoft::WRL::ComPtr<ID3D11Query> simEnd;
            Microsoft::WRL::ComPtr<ID3D11Query> hairDrawStart;
            Microsoft::WRL::ComPtr<ID3D11Query> hairDrawEnd;
            Microsoft::WRL::ComPtr<ID3D11Query> debugDrawStart;
            Microsoft::WRL::ComPtr<ID3D11Query> debugDrawEnd;
            Microsoft::WRL::ComPtr<ID3D11Query> frameEnd;
            Microsoft::WRL::ComPtr<ID3D11Query> hairPipelineStats;
            Microsoft::WRL::ComPtr<ID3D11Query> debugPipelineStats;
            std::array<Microsoft::WRL::ComPtr<ID3D11Query>, kGpuPassCount> passStart = {};
            std::array<Microsoft::WRL::ComPtr<ID3D11Query>, kGpuPassCount> passEnd = {};
            std::array<uint8_t, kGpuPassCount> passIssued = {};
            uint32_t sourceFrameIndex = 0u;
            uint32_t ageFrames = 0u;
            bool issued = false;
            bool resolved = false;
        };

        struct CounterReadbackFrame
        {
            Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
            uint32_t sourceFrameIndex = 0u;
            uint32_t ageFrames = 0u;
            bool issued = false;
            bool resolved = false;
        };

        bool CreateWindowAndDevice(HINSTANCE instance, int showCommand);
        bool CreateSimulationResources();
        bool CreateRenderResources();
        bool CreateHairOitResources(UINT width, UINT height);
        bool LoadShaders();
        bool CreateUiControls();
        bool RebuildAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand);
        void LayoutUiControls();
        void ApplyWorkspaceMode(WorkspaceMode workspace);
        void ApplyGroomToolPreset(GroomTool tool);
        const wchar_t* GetWorkspaceDisplayName() const;
        const wchar_t* GetGroomToolDisplayName() const;
        const wchar_t* GetBottomTrayDisplayName() const;
        bool IsGroomWorkspace() const;
        bool IsSimWorkspace() const;
        bool IsReviewWorkspace() const;
        bool IsReportWorkspace() const;
        void SyncControlsFromState();
        void SyncStateFromControls();
        void RefreshAssetStatsText();
        void UpdateStatsLabel();
        void ApplyPreset(int presetIndex);
        void ResetSimulation();
        void StepSingleFrame();
        void ExecuteExperimentSimulationTick(float dt);
        void ApplyScenarioForTick(uint32_t tickIndex, float dt);
        bool LoadScenarioTimeline();
        bool ValidateScenarioTimeline() const;
        void ApplyScenarioTimelineFrame(uint32_t frameIndex);
        void StepFullDynamic(float dt);
        void StepStrandSleep(float dt);
        void StepBlockSleep(float dt);
        void StepSegmentSleep(float dt);
        void StepActiveSetOnly(float dt);
        void StepExternalStateArray(float dt);
        void StepIntegratedSegmentGroup(float dt);
        uint64_t ComputeColliderStateHash() const;
        void StepSimulation(float dt);
        void ResetNoveltyTelemetryForFrame();
        bool UpdateSegmentGroupRuntimeConstants(float dt);
        bool BuildSegmentGroupRuntimeTopology();
        bool CreateSegmentGroupRuntimeResources();
        bool CreateSegmentGroupIndirectArgsResources();
        void ResetSegmentGroupRuntimeResources();
        void DetectSegmentGroupDirtyEvents();
        void ClassifySegmentGroupBlocks();
        void ClassifyDirtySegmentGroups();
        void CompactDynamicSegmentGroups();
        void CompactStaticSegmentGroups();
        void BuildSegmentGroupDispatchArguments();
        void ExecutePredictiveWake(float dt);
        void TransformStaticSegmentGroups();
        void SolveDynamicSegmentGroups(float dt);
        void ExecuteVelocityRecovery(float dt);
        void SolveRepairSegmentGroups(float dt);
        void RepairSegmentGroupBoundaries();
        void UpdateSegmentGroupCaches();
        void ExecuteSegmentGroupWatchdog();
        void ResolveSegmentGroupTelemetry();
        void EvaluateMicrotypeCertificates();
        void ExecuteBudgetedPhysics();
        void ExecuteCertificateAwareRendering();
        void ExecutePerStrandEditing();
        void ExecuteGpuResidentRuntime();
        void Render();
        void Resize(UINT width, UINT height);
        void UpdateCamera();
        void UpdateWindowTitle();
        const wchar_t* GetPerformanceEvidenceClassDisplayName() const;
        const char* GetPerformanceEvidenceClassCode() const;
        void Shutdown();
        void UpdateAnimatedScalp(float elapsedSeconds);
        void RebuildActiveSetFoundation();
        void SyncActiveSetGpuBuffers();
        void FillHairSimConstants(SimConstantsHair& constants, float dt) const;
        void FillActiveSetSimConstants(SimConstantsActiveSet& constants, float dt) const;
        void FillScalpSimConstants(SimConstantsScalp& constants, float dt) const;
        void FillRenderConstants(RenderConstants& constants, const Mat4& viewProjection) const;
        void UpdateActiveSetRuntimeFromParticles(const std::vector<GpuParticle>& particles);
        std::vector<hairgpu::Vec3> BuildGuideRootPositionsFromParticles(const std::vector<GpuParticle>& particles) const;
        std::vector<float> BuildGuideContactScoresFromParticles(const std::vector<GpuParticle>& particles) const;
        void BuildHairMeshVertices(const std::vector<GpuParticle>& particles, float aspectRatio);
        void RecomputeAssetBounds();
        void AutoFrameCameraToAsset(bool resetAngles);
        void QueueVisibilityRecovery();
        bool ShouldRunVisibilityRecoveryThisFrame() const;
        bool RunVisibilityRecoveryPass(float aspectRatio, bool allowCameraRecovery, std::vector<GpuParticle>* outParticles);
        bool RestoreGpuParticleStateFromInitialPose(bool resetCamera);
        uint32_t EstimateVisibleParticleCount(const std::vector<GpuParticle>& particles, float aspectRatio) const;
        void RebuildGpuDebugLineEntries();
        bool EnsureHairMeshVertexCapacity(size_t vertexCount);
        bool EnsureGpuDebugLineCapacity(size_t lineCount);
        void UpdateRenderStatistics();
        bool CreateGpuTimingQueries();
        void ResolveGpuTimingFrame(GpuTimingFrame& frame);
        void BeginGpuPassTiming(GpuPassId pass);
        void EndGpuPassTiming(GpuPassId pass);
        void QueueCounterReadback(uint32_t sourceFrameIndex);
        void ResolveCounterReadback(CounterReadbackFrame& frame);
        void DrainGpuTimingAndCounterReadbacks();
        void PatchSampleFromResolvedGpuTiming(uint32_t sourceFrameIndex);
        bool SaveSettingsToFile(const std::wstring& filePath) const;
        bool LoadSettingsFromFile(const std::wstring& filePath);
        bool ExportCurrentFrameCsv(const std::wstring& filePath);
        bool ExportPerformanceCsv(const std::wstring& filePath) const;
        bool ExportRealtimeCadenceCsv(const std::wstring& filePath) const;
        bool ExportPerSecondTelemetryCsv(const std::wstring& filePath) const;
        bool ExportPerformanceJsonl(const std::wstring& filePath) const;
        bool EnsureLocalDebugStreamingInitialized() const;
        bool AppendLocalDebugSample(const PerformanceSample& sample) const;
        bool AppendLocalDebugPerSecondSample(const PerformanceSample& sample) const;
        bool FlushLocalDebugPerSecondBucket(bool completeSecond, bool durable) const;
        void FlushLocalDebugStreaming(bool durable) const;
        void WriteLocalDebugRunState(const char* status, const char* exitMode) const;
        bool ExportExperimentSettingsJson(const std::wstring& filePath) const;
        bool ExportActiveSetPlanJson(const std::wstring& filePath) const;
        bool ExportRuntimeReport(const std::wstring& filePath) const;
        bool LoadExperimentCaseFromEnvironment();
        static ExperimentMethodId ParseExperimentMethodId(const std::wstring& value);
        static ExperimentScenarioId ParseExperimentScenarioId(const std::wstring& value);
        static ScenarioEventType ParseScenarioEventType(const std::string& value);
        static bool ExperimentMethodFlagsEqual(const ExperimentMethodFlags& lhs, const ExperimentMethodFlags& rhs);
        bool ValidateExperimentMethodFlags() const;
        ExperimentRunStatus ValidateCompletedExperiment() const;
        void SetFatalExperimentFailure(uint32_t errorCode, const std::wstring& message);
        static const char* ExperimentRunStatusToString(ExperimentRunStatus status);
        void ApplyExperimentConfigBeforeAssetCreation();
        bool IsExperimentModeEnabled() const;
        bool ShouldCaptureCurrentFrame() const;
        bool ShouldAutoExitAfterCurrentFrame() const;
        PerformanceSample BuildPerformanceSample(float frameSeconds, float instantFps) const;
        bool ReadBackCurrentParticlePositions(std::vector<GpuParticle>& outParticles);
        bool PickStrandAtPoint(POINT clientPoint);
        bool ApplySelectedStrandOffset(const hairgpu::Vec3& rootOffset, const hairgpu::Vec3& tipOffset);
        bool BeginStrandGizmoDrag(POINT clientPoint);
        void UpdateStrandGizmoDrag(POINT clientPoint);
        void EndStrandGizmoDrag(bool cancelDrag);
        bool IsBrushDrivenTool() const;
        bool ComputeBrushCenterUvFromScreenPoint(POINT clientPoint, hairgpu::Vec2& outUv, int32_t* outGuideIndex = nullptr, float* outScreenDistancePx = nullptr) const;
        hairgpu::Vec3 ComputeWorldDeltaFromScreenDelta(float dxPixels, float dyPixels) const;
        bool BeginViewportBrushStroke(POINT clientPoint);
        void UpdateViewportBrushStroke(POINT clientPoint);
        void EndViewportBrushStroke(bool cancelStroke);
        bool ApplyViewportBrushStroke(POINT clientPoint, bool commitStroke);
        hairgpu::EditLayer BuildViewportBrushLayer(const hairgpu::Vec2& centerUv,
                                                   const hairgpu::Vec2& startUv,
                                                   const hairgpu::Vec2& currentUv,
                                                   const POINT& currentPoint,
                                                   uint32_t* outAffectedGuideCount = nullptr) const;
        bool IsStrandLocked(uint32_t strandIndex) const;
        void RecordViewportHistory(const std::wstring& entry);
        std::wstring BuildHistorySummaryText() const;
        hairgpu::Vec3 GetSelectedStrandAnchorPoint(bool root) const;
        bool ProjectWorldToScreen(const hairgpu::Vec3& world, float& outX, float& outY) const;
        void EnforceFixedWorkload();
        void CommitEditedRestState();
        void CommitEditedPointIndices(const std::vector<uint32_t>& pointIndices);
        std::vector<uint32_t> ExpandGuideFamilies(const std::vector<uint32_t>& guideIndices) const;
        void PushStrandEditUndoSnapshot();
        void PushStrandEditUndoSnapshot(const std::vector<uint32_t>& guideIndices);
        void ExtendLastStrandEditUndoSnapshot(const std::vector<uint32_t>& guideIndices);
        void FinalizeLastStrandEditUndoSnapshot();
        void ApplyStrandEditSnapshot(const StrandEditSnapshot& snapshot, bool applyAfter);
        bool UndoStrandEdit();
        bool RedoStrandEdit();
        bool SaveStrandEditSessionToPath(const std::filesystem::path& path) const;
        bool LoadStrandEditSessionFromPath(const std::filesystem::path& path);
        bool LoadHairAssetFromPath(const std::wstring& filePath);
        bool LoadScalpAnimationFromPath(const std::wstring& filePath, bool showMessageOnFailure);
        bool BrowseAndSetEditStackPath();
        bool ClearEditStackAndRestoreAsset();
        bool ApplyEditStackToCurrentAsset(bool showMessageOnFailure);
        bool ApplyEditStackToAsset(hairgpu::HairAsset& asset, std::wstring* outStatusText = nullptr);
        bool ApplyInlineEditStackFromUi(bool showMessageOnFailure);
        bool SaveInlineEditStackToPath(const std::wstring& filePath, bool applyAfterSave);
        void ResetInlineEditStackText();
        std::wstring GetInlineEditStackText() const;
        std::filesystem::path GetInlineEditStackTempPath() const;
        bool BrowseAndSetMaskPath(bool partingMask);
        void RefreshActiveSetTuningLabels();
        void RefreshQuickEditFormLabels();
        void SyncQuickEditFormControlsFromState();
        void RefreshInlineLayerListFromText(int preferredSelection = -1);
        bool TryBuildSelectedInlineLayerPreview(std::vector<float>& outGuideInfluence, uint32_t& outActiveGuideCount, float& outMaxInfluence) const;
        bool BuildEditVisualizationCategories(std::vector<uint32_t>& outCategories) const;
        bool IsEditInfluenceOverlayMode() const;
        bool IsBoundaryBeforeAfterOverlayMode() const;
        bool IsActiveSetRiskOverlayMode() const;
        const wchar_t* GetMaskOverlayModeDisplayName() const;
        float GetActiveSetOverlayScoreValue(const hairgpu::ActiveSetClusterScore& score) const;
        int GetSelectedInlineLayerIndex() const;
        void SetSelectedInlineLayerIndex(int layerIndex);
        void LoadQuickEditFormFromInlineText(int preferredSelection = -1);
        bool PushQuickEditFormToInlineText(bool replaceSelectedLayer, bool applyAfterPush);
        bool MoveSelectedInlineLayer(int direction, bool applyAfterMove);
        bool ToggleSelectedInlineLayerEnabled(bool applyAfterToggle);
        bool DuplicateSelectedInlineLayer(bool applyAfterDuplicate);
        bool DeleteSelectedInlineLayer(bool applyAfterDelete);
        hairgpu::EditLayer BuildQuickEditLayerFromUi() const;

        void ApplySampleMaskOverrides();
        void RefreshMaskPreviewControls();
        void ReleaseMaskPreviewBitmaps();
        void ShowMaskPreviewPopup(bool partingMask);
        void UpdateScalpOverlayVertexBuffer();
        void EvaluateScalpCoverageTint(float u, float v, float& r, float& g, float& b) const;
        RECT GetRenderViewportRect() const;
        hairgpu::Vec3 ApplySampleScalpSkinning(const hairgpu::Vec3& restPosition, const hairgpu::SkinWeight4& weights, float elapsedSeconds) const;

        bool CompileShaderFromFile(const wchar_t* filePath, const char* entryPoint, const char* target, ID3DBlob** blobOut, std::wstring& errorText);
        bool CompileSegmentGroupShaderFromFile(const char* entryPoint, bool compactionBindings, ID3DBlob** blobOut, std::wstring& errorText);
        StructuredBuffer CreateStructuredBuffer(UINT elementSize, UINT elementCount, const void* initialData, bool unorderedAccess);

        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        HWND m_hwnd = nullptr;
        UINT m_width = 1600;
        UINT m_height = 900;
        UINT m_pendingResizeWidth = 0u;
        UINT m_pendingResizeHeight = 0u;
        bool m_resizePending = false;
        bool m_vsyncEnabled = true;
        UINT m_presentSyncInterval = 1u;
        UINT m_lastObservedPresentSyncInterval = 1u;
        HRESULT m_lastPresentResult = S_OK;
        std::filesystem::path m_pendingVisualCapturePath;
        bool m_visualCapturePending = false;
        bool m_visualCaptureSucceeded = false;
        bool m_running = true;
        bool m_paused = false;
        bool m_dragging = false;
        POINT m_mouseDownPoint = {};
        bool m_syncingControls = false;
        bool m_enableSelfCollision = true;
        bool m_enableScalpMotion = false;
        bool m_showColliderDebug = false;
        bool m_showScalpWireframe = false;
        bool m_showScalpMesh = true;
        bool m_showGridBounds = false;
        bool m_showFloorGuide = false;
        bool m_showGuideSkeleton = false;
        bool m_showFollowerSkeleton = false;
        bool m_showRootMarkers = false;
        bool m_showFollowLinks = false;
        bool m_showTestMesh = false; // legacy CPU test mesh path disabled in runtime UI
        POINT m_lastMouse = {};
        struct SparsePointEdit
        {
            uint32_t pointIndex = 0u;
            hairgpu::Vec3 before = {};
            hairgpu::Vec3 after = {};
        };

        struct StrandEditSnapshot
        {
            std::vector<SparsePointEdit> points;
            std::wstring inlineEditStackTextBefore;
            std::wstring inlineEditStackTextAfter;
            std::vector<uint8_t> lockedStrandsBefore;
            std::vector<uint8_t> lockedStrandsAfter;
            int32_t selectedStrandIndexBefore = -1;
            int32_t selectedStrandIndexAfter = -1;
            int32_t selectedInlineLayerIndexBefore = -1;
            int32_t selectedInlineLayerIndexAfter = -1;
        };

        int32_t m_selectedStrandIndex = -1;
        float m_lastSelectedStrandScreenDistance = 0.0f;
        StrandGizmoMode m_strandGizmoMode = StrandGizmoMode::TipTranslate;
        GizmoAxis m_activeGizmoAxis = GizmoAxis::None;
        bool m_gizmoDragging = false;
        POINT m_gizmoDragStartPoint = {};
        StrandEditSnapshot m_gizmoDragSnapshot;
        std::vector<StrandEditSnapshot> m_strandUndoStack;
        std::vector<StrandEditSnapshot> m_strandRedoStack;
        bool m_brushStrokeActive = false;
        POINT m_brushStrokeStartPoint = {};
        POINT m_brushStrokeLastPoint = {};
        hairgpu::Vec2 m_brushStrokeStartUv = { 0.5f, 0.5f };
        hairgpu::Vec2 m_brushStrokeCurrentUv = { 0.5f, 0.5f };
        hairgpu::EditStack m_brushStrokeBaseStack = {};
        uint32_t m_brushStrokeAffectedGuideCount = 0u;
        bool m_brushStrokeSnapshotActive = false;
        std::vector<uint8_t> m_lockedStrands;
        std::vector<std::wstring> m_viewportHistory;
        uint32_t m_viewportStrokeSerial = 0u;
        hairgpu::BoneAnimationClip m_scalpAnimationClip;
        std::vector<hairgpu::Mat4> m_currentScalpBoneMatrices;
        std::wstring m_lastScalpAnimationSource;

        float m_cameraYaw = 0.1f;
        float m_cameraPitch = 0.35f;
        float m_cameraDistance = 2.5f;
        hairgpu::Vec3 m_cameraTarget = { 0.0f, 1.2f, 0.0f };
        hairgpu::Vec3 m_cameraPosition = { 0.0f, 0.0f, 0.0f };
        hairgpu::Vec3 m_scalpCenter = { 0.0f, 1.60f, 0.0f };
        hairgpu::Vec3 m_neckPivot = { 0.0f, 1.42f, -0.03f };
        hairgpu::Vec3 m_assetBoundsMin = { -0.5f, 1.0f, -0.5f };
        hairgpu::Vec3 m_assetBoundsMax = { 0.5f, 2.0f, 0.5f };
        hairgpu::Vec3 m_assetBoundsCenter = { 0.0f, 1.5f, 0.0f };
        float m_assetBoundsRadius = 0.75f;
        float m_widthScale = 0.56f;
        float m_selfCollisionRadius = 0.012f;
        float m_selfCollisionStiffness = 0.35f;
        bool m_enableGridSelfCollision = true;
        uint32_t m_selfCollisionBroadphaseMode = 1u; // Spatial hash runtime default.
        bool m_enableRuntimeAngularBend = true;
        float m_runtimeAngularBendRadians = 0.18f;
        float m_runtimeAngularBendCompliance = 5.0e-6f;
        float m_runtimeAngularBendStiffness = 0.35f;
        float m_runtimeAngularBendDamping = 0.08f;
        uint32_t m_sortedBroadphasePaddedCount = 1u;
        uint32_t m_builderGuideCount = kFixedGuideCount;
        uint32_t m_builderFollowersPerGuide = kFixedFollowersPerGuide;
        uint32_t m_builderParticlesPerStrand = kFixedParticlesPerStrand;
        std::wstring m_activePresetName = L"Rapunzel Preview";
        std::wstring m_lastAssetSource = L"Sample Hybrid";
        std::wstring m_lastImportStatus = L"Sample asset active";
        enum class MaskSourceMode : uint32_t
        {
            Auto = 0,
            ExternalFiles = 1,
            Procedural = 2,
        };

        std::wstring m_partingMaskPath;
        std::wstring m_regionMaskPath;
        std::wstring m_editStackPath;
        std::wstring m_lastEditStackStatus = L"No edit stack";
        hairgpu::RuntimeEditState m_runtimeEditState;
        hairgpu::RegionSelectMode m_quickEditMode = hairgpu::RegionSelectMode::Ellipse;
        bool m_quickEditEnabled = true;
        std::wstring m_quickEditLayerName = L"quick_form";
        float m_quickEditFeather = 0.20f;
        float m_quickEditLengthScale = 1.00f;
        float m_quickEditLayerWeight = 1.00f;
        float m_quickEditCenterU = 0.50f;
        float m_quickEditCenterV = 0.50f;
        float m_quickEditRadiusU = 0.18f;
        float m_quickEditRadiusV = 0.18f;
        float m_quickEditInnerRadius = 0.00f;
        float m_quickEditAngleCenterRadians = 0.00f;
        float m_quickEditAngleHalfExtentRadians = 3.1415926f;
        float m_quickEditRootOffsetX = 0.00f;
        float m_quickEditRootOffsetY = 0.00f;
        float m_quickEditRootOffsetZ = 0.00f;
        float m_quickEditTipOffsetX = 0.02f;
        float m_quickEditTipOffsetY = 0.00f;
        float m_quickEditTipOffsetZ = -0.08f;
        float m_quickEditSideOffset = 0.00f;
        float m_quickEditYawRadians = 0.00f;
        float m_quickEditWidthScale = 1.00f;
        float m_quickEditFollowerInfluence = 1.00f;
        float m_quickEditBoundarySmoothing = 0.42f;
        float m_quickEditBoundaryRadiusUv = 0.10f;
        uint32_t m_quickEditGroupId = UINT32_MAX;
        bool m_quickEditGuidesOnly = true;
        int m_selectedInlineLayerIndex = 0;
        hairgpu::HairAsset m_baseAsset;
        MaskSourceMode m_maskSourceMode = MaskSourceMode::Auto;
        MaskOverlayMode m_maskOverlayMode = MaskOverlayMode::None;
        float m_scalpCoverageStrength = 1.30f;
        float m_scalpPartLineWidth = 0.012f;
        float m_scalpTintDarkness = 0.88f;
        float m_scalpHairBlend = 0.96f;
        // Non-invasive cadence metrics. Render cadence uses QPC start-to-start
        // intervals; GPU throughput uses delayed timestamp queries and never waits.
        float m_smoothedFps = 0.0f; // compatibility alias for m_renderFps
        float m_renderFps = 0.0f;
        float m_simulationHz = 0.0f;
        float m_gpuThroughputFps = 0.0f;
        float m_lastFrameSeconds = 0.0f;
        float m_lastFrameIntervalMilliseconds = 0.0f;
        static constexpr size_t kRealtimeFrameHistoryCapacity = 600u;
        std::array<float, kRealtimeFrameHistoryCapacity> m_recentFrameIntervalsMilliseconds = {};
        size_t m_recentFrameIntervalCount = 0u;
        size_t m_recentFrameIntervalWriteIndex = 0u;
        float m_recentFrameAverageMilliseconds = 0.0f;
        float m_sessionEffectiveFps = 0.0f;
        double m_sessionWallSeconds = 0.0;
        uint64_t m_sessionCompletedFrameCount = 0ull;
        float m_recentEffectiveFps = 0.0f;
        float m_recentOnePercentLowFps = 0.0f;
        float m_recentFrameP95Milliseconds = 0.0f;
        float m_recentFrameWorstMilliseconds = 0.0f;
        uint32_t m_recentFrameStallCount100Milliseconds = 0u;
        uint64_t m_observedStallCount100Milliseconds = 0ull;
        float m_lastCpuFrameWorkMilliseconds = 0.0f;
        float m_lastSimulationStepMilliseconds = 0.0f;
        float m_droppedSimulationMilliseconds = 0.0f;
        float m_statsUpdateAccumulator = 0.0f;
        uint32_t m_simulationTicksThisFrame = 0u;
        bool m_renderCadenceWindowValid = false;
        bool m_localDebugCaptureActive = false;
        uint32_t m_localDebugWarmupFrames = 120u;
        uint32_t m_localDebugCaptureFrames = 1800u;
        mutable bool m_localDebugStreamingInitialized = false;
        mutable bool m_localDebugStreamingFailed = false;
        mutable uint64_t m_localDebugStreamSampleCount = 0ull;
        mutable uint64_t m_localDebugMeasurementSampleCount = 0ull;
        mutable LocalDebugSecondAccumulator m_localDebugSecondAccumulator = {};
        mutable std::filesystem::path m_localDebugRunDirectory;
        mutable std::filesystem::path m_localDebugPartialCsvPath;
        mutable std::filesystem::path m_localDebugPartialJsonlPath;
        mutable std::filesystem::path m_localDebugPartialPerSecondPath;
        mutable std::filesystem::path m_localDebugFinalCsvPath;
        mutable std::filesystem::path m_localDebugFinalJsonlPath;
        mutable std::filesystem::path m_localDebugFinalPerSecondPath;
        mutable std::ofstream m_localDebugCsvStream;
        mutable std::ofstream m_localDebugJsonlStream;
        mutable std::ofstream m_localDebugPerSecondStream;
        hairgpu::HairAsset m_asset;
        hairgpu::GpuSimulationConfig m_simConfig;
        hairgpu::ColliderSet m_colliders;
        std::vector<GpuParticle> m_initialParticles;
        std::vector<FollowGpuData> m_followGpuData;
        hairgpu::HairSegmentRenderData m_renderData;
        std::vector<uint32_t> m_editVisualizationCategories;
        std::vector<GpuParticle> m_restScalpParticles;
        std::vector<GpuParticle> m_animatedScalpParticles;
        std::vector<TriangleGpuData> m_scalpTriangles;
        std::vector<RootBindingGpuData> m_rootBindingGpuData;
        std::vector<float> m_zeroDistanceLambdas;
        std::vector<float> m_zeroBendLambdas;
        std::vector<HairMeshVertex> m_hairMeshVertices;
        std::vector<GpuDebugLineEntry> m_gpuDebugLineEntries;
        std::vector<GpuParticle> m_debugReadbackParticles;
        bool m_enableActiveSetRuntime = false;
        hairgpu::ActiveSetClusterSettings m_activeSetClusterSettings;
        hairgpu::ActiveSetScoringSettings m_activeSetScoringSettings;
        hairgpu::ActiveSetBudget m_activeSetBudget;
        std::vector<hairgpu::ActiveSetCluster> m_activeSetClusters;
        hairgpu::ActiveSetPlan m_activeSetPlan;
        std::vector<hairgpu::Vec3> m_activeSetPreviousGuideRoots;
        std::vector<float> m_activeSetGuideContactScores;
        std::vector<float> m_activeSetGuideUncertainty;
        std::vector<SegmentGroupGpuData> m_segmentGroupInitialData;
        std::vector<SegmentGroupBlockGpuData> m_segmentGroupBlockInitialData;
        std::vector<SegmentGroupCertificateGpuData>
            m_segmentGroupCertificateInitialData;
        uint32_t m_segmentGroupCount = 0u;
        uint32_t m_segmentGroupBlockCount = 0u;
        uint32_t m_segmentGroupsPerBlock = 8u;
        uint32_t m_headTransformVersion = 1u;
        uint32_t m_scalpVersion = 1u;
        uint32_t m_colliderVersion = 1u;
        uint32_t m_editVersion = 1u;
        uint32_t m_simulationResetVersion = 1u;
        uint32_t m_previousSphereColliderCount = 0u;
        uint32_t m_previousCapsuleColliderCount = 0u;
        uint32_t m_previousPlaneColliderCount = 0u;
        uint32_t m_previousAabbColliderCount = 0u;
        bool m_segmentGroupRuntimeReady = false;
        bool m_segmentGroupTopologyDirty = true;
        bool m_forceAllSegmentGroupsDirty = true;
        uint32_t m_segmentGroupWatchdogIntervalFrames = 64u;
        std::wstring m_activeSetLastStatus = L"Not evaluated";
        float m_activeSetEvaluationMilliseconds = 0.0f;
        hairgpu::SparseCorrectiveSettings m_sparseCorrectiveSettings;
        hairgpu::SparseCorrectiveReport m_sparseCorrectiveReport;
        hairgpu::HairNoveltyFeatureFlags m_noveltyFlags = {};
        hairgpu::GlobalHairFrameBudget m_globalHairBudget = {};
        hairgpu::HairNoveltyFrameTelemetry m_noveltyTelemetry = {};
        std::vector<PerformanceSample> m_performanceSamples;
        ExperimentCaseConfig m_experimentCase;
        uint32_t m_globalFrameIndex = 0u;
        uint32_t m_measuredFrameIndex = 0u;
        Microsoft::WRL::ComPtr<ID3D11Device> m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
        Microsoft::WRL::ComPtr<IDXGISwapChain> m_swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_depthTexture;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_dsv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_hairOitAccumTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_hairOitAccumRtv;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_hairOitAccumSrv;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_hairOitRevealTexture;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_hairOitRevealRtv;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_hairOitRevealSrv;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_particleVS;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_particlePointGS;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_particleBillboardGS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_particlePS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_ribbonVS;
        Microsoft::WRL::ComPtr<ID3D11GeometryShader> m_ribbonGS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ribbonPS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ribbonOitPS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_hairOitCompositeVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_hairOitCompositePS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_debugLineVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_debugLinePS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_scalpVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_scalpPS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> m_hairMeshVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> m_hairMeshPS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_rootBindCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_integrateCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearConstraintLambdasCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_distanceCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_stabilityGuardCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_bendCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_collisionCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearGridCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildGridCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearHashGridCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildHashGridCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearSortedRangesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildSortedEntriesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_bitonicSortCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildSortedRangesCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_selfCollisionCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_followCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_compactFollowerDispatchCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildFollowDispatchArgsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_compactGuideDispatchCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildGuideDispatchArgsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_buildGuidePointDispatchArgsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupBlockClassifyCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupClassifyCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupCompactDynamicCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupCompactStaticCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupBuildDispatchArgsCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupPredictiveWakeCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupStaticTransformCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupDynamicSolveCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupVelocityRecoveryCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupRepairSolveCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupBoundaryRepairCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupCacheUpdateCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupWatchdogCS;
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_segmentGroupTelemetryReduceCS;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_segmentInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_debugInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_scalpInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> m_hairMeshInputLayout;

        StructuredBuffer m_positions[2];
        StructuredBuffer m_previousPositions[2];
        StructuredBuffer m_restPositions;
        StructuredBuffer m_pointWidths;
        StructuredBuffer m_followBuffer;
        StructuredBuffer m_renderStrandTiersBuffer;
        // Rendering LOD/simulation tier and edit-visualization category are
        // intentionally separate. Reusing one buffer for both caused category
        // values 3/4 to be interpreted as hidden LOD tiers, producing large
        // missing bands in the wig.
        StructuredBuffer m_editVisualizationCategoryBuffer;
        StructuredBuffer m_guideTierAssignmentsBuffer;
        StructuredBuffer m_followerTierAssignmentsBuffer;
        StructuredBuffer m_compactedFollowerIndicesBuffer;
        StructuredBuffer m_compactedFollowerCounterBuffer;
        StructuredBuffer m_compactedGuideIndicesBuffer;
        StructuredBuffer m_compactedGuideCounterBuffer;
        StructuredBuffer m_scalpPositions;
        StructuredBuffer m_scalpTrianglesBuffer;
        StructuredBuffer m_rootBindingsBuffer;
        StructuredBuffer m_sdfValuesBuffer;
        StructuredBuffer m_distanceLambdaBuffer;
        StructuredBuffer m_bendLambdaBuffer;
        StructuredBuffer m_gridCellCountsBuffer;
        StructuredBuffer m_gridCellIndicesBuffer;
        StructuredBuffer m_gridHashHeadsBuffer;
        StructuredBuffer m_gridParticleNextBuffer;
        StructuredBuffer m_sortedKeysBuffer;
        StructuredBuffer m_sortedParticleIndicesBuffer;
        StructuredBuffer m_sortedCellStartsBuffer;
        StructuredBuffer m_sortedCellEndsBuffer;
        StructuredBuffer m_segmentGroupBuffer;
        StructuredBuffer m_segmentGroupBlockBuffer;
        StructuredBuffer m_segmentGroupCertificateBuffer;
        StructuredBuffer m_dynamicSegmentGroupIndicesBuffer;
        StructuredBuffer m_staticTransformedGroupIndicesBuffer;
        StructuredBuffer m_repairSegmentGroupIndicesBuffer;
        StructuredBuffer m_externalSegmentGroupStateBuffer;
        StructuredBuffer m_wakeRequestBuffer;
        StructuredBuffer m_wakeGroundTruthBuffer;
        StructuredBuffer m_wakeTelemetryBuffer;
        StructuredBuffer m_repairTrackingBuffer;
        StructuredBuffer m_repairVerificationBuffer;
        StructuredBuffer m_referencePositions[2];
        StructuredBuffer m_referencePreviousPositions[2];
        StructuredBuffer m_segmentGroupCounterBuffer;
        StructuredBuffer m_segmentGroupRuntimeAtomicCountersBuffer;
        StructuredBuffer m_segmentGroupRuntimeCountersBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_strandTubeVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_strandTubeIndexBuffer;
        uint32_t m_strandTubeIndexCount = 0;
        uint32_t m_strandTubeRingSides = 0;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_hairMeshVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_debugLineTemplateVertexBuffer;
        StructuredBuffer m_debugLineEntriesBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_followDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_followDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_guideDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_guideDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_guidePointDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_guidePointDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentGroupDynamicDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_segmentGroupDynamicDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentGroupStaticDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_segmentGroupStaticDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentGroupRepairDispatchArgsBuffer;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_segmentGroupRepairDispatchArgsUav;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_scalpVertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_scalpIndexBuffer;
        uint32_t m_scalpIndexCount = 0u;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_segmentGroupRuntimeConstants;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_simConstantsHair;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_simConstantsActiveSet;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_simConstantsScalp;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_bitonicConstants;
        Microsoft::WRL::ComPtr<ID3D11Buffer> m_renderConstants;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_hairOitBlendState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_overlayDepthState;
        // Transparent hair ribbons test depth but do not write it.
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_hairDepthState;
        size_t m_hairMeshVertexCapacity = 0;
        size_t m_debugLineEntryCapacity = 0;
        bool m_gpuDebugLinesDirty = true;
        DebugLineStats m_debugLineStats = {};
        uint32_t m_lastCurrentHairDrawCalls = 0u;
        uint64_t m_lastCurrentHairVertexCount = 0u;
        uint64_t m_lastCurrentHairPrimitiveCount = 0u;
        uint32_t m_lastCurrentHairInstanceCount = 0u;
        uint32_t m_lastTotalDrawCalls = 0u;
        float m_lastGpuFrameMilliseconds = 0.0f;
        float m_lastGpuSimulationMilliseconds = 0.0f;
        float m_lastGpuHairDrawMilliseconds = 0.0f;
        float m_lastGpuDebugDrawMilliseconds = 0.0f;
        uint32_t m_lastEstimatedVisibleParticles = 0u;
        uint32_t m_lastReadbackParticleCount = 0u;
        bool m_lastVisibilitySuspect = false;
        bool m_lastUsedCpuVisibilityFallback = false;
        bool m_lastVisibilityReadbackValid = true;
        bool m_lastVisibilityUsedInitialPoseFallback = false;
        uint32_t m_visibilityReadbackFailureCount = 0u;
        uint32_t m_visibilityRecoveryCount = 0u;
        CpuReadbackMode m_cpuReadbackMode = CpuReadbackMode::Off;
        bool m_visibilityRecoveryPending = true;
        uint32_t m_consecutiveInvisibleFrames = 0u;
        uint32_t m_framesSinceVisibilityCheck = 0u;
        uint32_t m_runtimeFollowerUpdatePhase = 0u;
        D3D11_QUERY_DATA_PIPELINE_STATISTICS m_lastHairPipelineStats = {};
        D3D11_QUERY_DATA_PIPELINE_STATISTICS m_lastDebugPipelineStats = {};
        std::array<GpuTimingFrame, 8> m_gpuTimingFrames = {};
        std::array<CounterReadbackFrame, 8> m_counterReadbackFrames = {};
        uint32_t m_counterReadbackWriteIndex = 0u;
        uint32_t m_gpuTimingWriteIndex = 0u;
        int32_t m_activeGpuTimingFrameIndex = -1;
        uint32_t m_simulationTickIndex = 0u;
        ExperimentRunStatus m_experimentRunStatus = ExperimentRunStatus::InProgress;
        std::wstring m_experimentFailureMessage;
        uint32_t m_experimentFailureCode = 0u;
        D3D_DRIVER_TYPE m_observedDriverType = D3D_DRIVER_TYPE_UNKNOWN;
        std::wstring m_observedGpuName;
        uint32_t m_observedGpuVendorId = 0u;
        uint32_t m_observedGpuDeviceId = 0u;
        uint64_t m_observedDedicatedVideoMemory = 0ull;
        uint32_t m_actualRenderWidth = 0u;
        uint32_t m_actualRenderHeight = 0u;
        uint32_t m_fullCpuGroupUploadCount = 0u;
        uint32_t m_sameFrameReadbackCount = 0u;
        uint32_t m_nanStateCount = 0u;
        uint32_t m_infiniteStateCount = 0u;
        uint32_t m_syntheticMetricCount = 0u;
        uint32_t m_unavailableMetricCount = 0u;
        uint32_t m_gpuTimingRequiredFrameCount = 0u;
        uint32_t m_gpuTimingValidFrameCount = 0u;
        std::wstring m_shaderBundleSha256;
        std::wstring m_canonicalResultSha256;
        uint32_t m_scenarioAppliedEventCount = 0u;
        uint32_t m_scenarioAssertionFailureCount = 0u;
        uint32_t m_scenarioCollisionCandidateCount = 0u;
        uint32_t m_scenarioWakeRequestCount = 0u;
        uint32_t m_scenarioRepairAttemptCount = 0u;
        uint32_t m_scenarioEditInvalidationCount = 0u;
        uint32_t m_scenarioStateTransitionCount = 0u;
        uint64_t m_lastColliderStateHash = 0ull;
        Vec3 m_scenarioBaseSphereCenter = { 0.0f, 1.66f, 0.0f };
        bool m_scenarioBaseCaptured = false;
        uint32_t m_gpuTimingResolvedCount = 0u;
        uint32_t m_gpuTimingValidCount = 0u;
        uint32_t m_gpuTimingUnresolvedCount = 0u;
        uint32_t m_counterReadbackResolvedCount = 0u;
        uint32_t m_counterReadbackUnresolvedCount = 0u;

        RenderMode m_renderMode = RenderMode::StrandRibbon;
        InteractionMode m_interactionMode = InteractionMode::Edit;
        WorkspaceMode m_workspaceMode = WorkspaceMode::Groom;
        GroomTool m_groomTool = GroomTool::Comb;
        BottomTrayTab m_bottomTrayTab = BottomTrayTab::Layers;
        DebugColorMode m_debugColorMode = DebugColorMode::Natural;
        bool m_showInspectorPane = true;
        bool m_showBottomTray = false;
        bool m_showAdvancedInspector = false;
        bool m_fastGroomViewport = true;

        LARGE_INTEGER m_qpcFrequency = {};
        LARGE_INTEGER m_lastTick = {};
        float m_accumulator = 0.0f;
        float m_elapsedSeconds = 0.0f;
        uint32_t m_currentBufferIndex = 0;

        HWND m_uiPanel = nullptr;
        HWND m_btnPlayPause = nullptr;
        HWND m_btnReset = nullptr;
        HWND m_btnStep = nullptr;
        HWND m_btnApplyAsset = nullptr;
        HWND m_btnImportAsset = nullptr;
        HWND m_btnSaveSettings = nullptr;
        HWND m_btnLoadSettings = nullptr;
        HWND m_btnExportCsv = nullptr;
        HWND m_btnExportPerfCsv = nullptr;
        HWND m_btnBrowseEditStack = nullptr;
        HWND m_btnClearEditStack = nullptr;
        HWND m_btnApplyInlineEditStack = nullptr;
        HWND m_btnSaveInlineEditStack = nullptr;
        HWND m_btnResetInlineEditStack = nullptr;
        HWND m_labelQuickEditForm = nullptr;
        HWND m_labelLayerList = nullptr;
        HWND m_listInlineLayers = nullptr;
        HWND m_labelQuickEditName = nullptr;
        HWND m_editQuickEditName = nullptr;
        HWND m_btnQuickEditMoveUp = nullptr;
        HWND m_btnQuickEditMoveDown = nullptr;
        HWND m_checkQuickEditEnabled = nullptr;
        HWND m_btnQuickEditDelete = nullptr;
        HWND m_btnQuickEditDuplicate = nullptr;
        HWND m_comboQuickEditMode = nullptr;
        HWND m_trackQuickEditFeather = nullptr;
        HWND m_trackQuickEditLengthScale = nullptr;
        HWND m_trackQuickEditLayerWeight = nullptr;
        HWND m_trackQuickEditCenterU = nullptr;
        HWND m_trackQuickEditCenterV = nullptr;
        HWND m_trackQuickEditRadiusU = nullptr;
        HWND m_trackQuickEditRadiusV = nullptr;
        HWND m_trackQuickEditInnerRadius = nullptr;
        HWND m_trackQuickEditAngleCenter = nullptr;
        HWND m_trackQuickEditAngleHalfExtent = nullptr;
        HWND m_trackQuickEditRootOffsetX = nullptr;
        HWND m_trackQuickEditRootOffsetY = nullptr;
        HWND m_trackQuickEditRootOffsetZ = nullptr;
        HWND m_trackQuickEditTipOffsetX = nullptr;
        HWND m_trackQuickEditTipOffsetY = nullptr;
        HWND m_trackQuickEditTipOffsetZ = nullptr;
        HWND m_trackQuickEditSideOffset = nullptr;
        HWND m_trackQuickEditYaw = nullptr;
        HWND m_trackQuickEditWidthScale = nullptr;
        HWND m_trackQuickEditFollowerInfluence = nullptr;
        HWND m_trackQuickEditBoundarySmoothing = nullptr;
        HWND m_trackQuickEditBoundaryRadiusUv = nullptr;
        HWND m_editQuickEditGroupId = nullptr;
        HWND m_checkQuickEditGuidesOnly = nullptr;
        HWND m_btnQuickEditReplace = nullptr;
        HWND m_btnQuickEditAppend = nullptr;

        HWND m_btnBrowsePartingMask = nullptr;
        HWND m_btnBrowseRegionMask = nullptr;
        HWND m_comboMaskSource = nullptr;
        HWND m_comboMaskOverlay = nullptr;
        HWND m_previewPartingMask = nullptr;
        HWND m_previewRegionMask = nullptr;
        HWND m_comboPreset = nullptr;
        HWND m_comboRenderMode = nullptr;
        HWND m_comboInteractionMode = nullptr;
        HWND m_comboDebugColor = nullptr;
        HWND m_checkSelfCollision = nullptr;
        HWND m_checkGrid = nullptr;
        HWND m_checkScalpMotion = nullptr;
        HWND m_checkShowColliders = nullptr;
        HWND m_checkShowScalp = nullptr;
        HWND m_checkShowScalpMesh = nullptr;
        HWND m_checkShowGridBounds = nullptr;
        HWND m_checkShowFloor = nullptr;
        HWND m_checkShowGuides = nullptr;
        HWND m_checkShowFollowers = nullptr;
        HWND m_checkShowRoots = nullptr;
        HWND m_checkShowLinks = nullptr;
        HWND m_editGuides = nullptr;
        HWND m_editFollowers = nullptr;
        HWND m_editParticles = nullptr;
        HWND m_trackWidth = nullptr;
        HWND m_trackGravity = nullptr;
        HWND m_trackDamping = nullptr;
        HWND m_trackWind = nullptr;
        HWND m_trackIterations = nullptr;
        HWND m_trackFollow = nullptr;
        HWND m_trackSelfRadius = nullptr;
        HWND m_trackSelfStiffness = nullptr;
        HWND m_trackGridCellSize = nullptr;
        HWND m_trackStretchCompliance = nullptr;
        HWND m_trackBendCompliance = nullptr;
        HWND m_labelStats = nullptr;
        HWND m_labelAssetStats = nullptr;
        HWND m_labelEditWorkflow = nullptr;
        HWND m_labelEditLegend = nullptr;
        HWND m_labelEditStack = nullptr;
        HWND m_labelInlineEditStack = nullptr;
        HWND m_editInlineEditStack = nullptr;
        HWND m_labelQuickEditFeather = nullptr;
        HWND m_labelQuickEditLengthScale = nullptr;
        HWND m_labelQuickEditLayerWeight = nullptr;
        HWND m_labelQuickEditCenterU = nullptr;
        HWND m_labelQuickEditCenterV = nullptr;
        HWND m_labelQuickEditRadiusU = nullptr;
        HWND m_labelQuickEditRadiusV = nullptr;
        HWND m_labelQuickEditInnerRadius = nullptr;
        HWND m_labelQuickEditAngleCenter = nullptr;
        HWND m_labelQuickEditAngleHalfExtent = nullptr;
        HWND m_labelQuickEditRootOffsetX = nullptr;
        HWND m_labelQuickEditRootOffsetY = nullptr;
        HWND m_labelQuickEditRootOffsetZ = nullptr;
        HWND m_labelQuickEditTipOffsetX = nullptr;
        HWND m_labelQuickEditTipOffsetY = nullptr;
        HWND m_labelQuickEditTipOffsetZ = nullptr;
        HWND m_labelQuickEditSideOffset = nullptr;
        HWND m_labelQuickEditYaw = nullptr;
        HWND m_labelQuickEditWidthScale = nullptr;
        HWND m_labelQuickEditFollowerInfluence = nullptr;
        HWND m_labelQuickEditBoundarySmoothing = nullptr;
        HWND m_labelQuickEditBoundaryRadiusUv = nullptr;
        HWND m_labelQuickEditGroupId = nullptr;
        HWND m_labelQuickEditVisualization = nullptr;

        HWND m_labelPartingMask = nullptr;
        HWND m_labelRegionMask = nullptr;
        HWND m_labelMaskSource = nullptr;
        HWND m_labelMaskOverlay = nullptr;
        HWND m_labelScalpCoverageStrength = nullptr;
        HWND m_trackScalpCoverageStrength = nullptr;
        HWND m_labelScalpPartWidth = nullptr;
        HWND m_trackScalpPartWidth = nullptr;
        HWND m_labelScalpDarkness = nullptr;
        HWND m_trackScalpDarkness = nullptr;
        HWND m_labelScalpHairBlend = nullptr;
        HWND m_trackScalpHairBlend = nullptr;
        HBITMAP m_partingPreviewBitmap = nullptr;
        HBITMAP m_regionPreviewBitmap = nullptr;
        HWND m_labelWidth = nullptr;
        HWND m_labelGravity = nullptr;
        HWND m_labelDamping = nullptr;
        HWND m_labelWind = nullptr;
        HWND m_labelIterations = nullptr;
        HWND m_labelFollow = nullptr;
        HWND m_labelSelfRadius = nullptr;
        HWND m_labelSelfStiffness = nullptr;
        HWND m_labelGridCellSize = nullptr;
        HWND m_labelStretchCompliance = nullptr;
        HWND m_labelBendCompliance = nullptr;
        HWND m_labelActiveSetTuning = nullptr;
        HWND m_checkActiveSetAdaptiveBudget = nullptr;
        HWND m_checkActiveSetHardClamp = nullptr;
        HWND m_labelActiveSetMaxClusters = nullptr;
        HWND m_trackActiveSetMaxClusters = nullptr;
        HWND m_labelActiveSetMaxGuides = nullptr;
        HWND m_trackActiveSetMaxGuides = nullptr;
        HWND m_labelActiveSetThreshold = nullptr;
        HWND m_trackActiveSetThreshold = nullptr;
        HWND m_labelActiveSetTargetFrame = nullptr;
        HWND m_trackActiveSetTargetFrame = nullptr;
        HWND m_labelActiveSetFixedCost = nullptr;
        HWND m_trackActiveSetFixedCost = nullptr;
        HWND m_labelActiveSetClusterCost = nullptr;
        HWND m_trackActiveSetClusterCost = nullptr;
        HWND m_labelActiveSetScoreWeights = nullptr;
        HWND m_labelActiveSetMotionWeight = nullptr;
        HWND m_trackActiveSetMotionWeight = nullptr;
        HWND m_labelActiveSetContactWeight = nullptr;
        HWND m_trackActiveSetContactWeight = nullptr;
        HWND m_labelActiveSetUncertaintyWeight = nullptr;
        HWND m_trackActiveSetUncertaintyWeight = nullptr;
        HWND m_labelActiveSetEditWeight = nullptr;
        HWND m_trackActiveSetEditWeight = nullptr;
        HWND m_btnExportActiveSetJson = nullptr;
        HWND m_btnExportRuntimeReport = nullptr;

        HWND m_btnWorkspaceGroom = nullptr;
        HWND m_btnWorkspaceSim = nullptr;
        HWND m_btnWorkspaceReview = nullptr;
        HWND m_btnWorkspaceReport = nullptr;
        HWND m_btnToolSelect = nullptr;
        HWND m_btnToolComb = nullptr;
        HWND m_btnToolGrab = nullptr;
        HWND m_btnToolSmooth = nullptr;
        HWND m_btnToolLength = nullptr;
        HWND m_btnToolVolume = nullptr;
        HWND m_btnToolPart = nullptr;
        HWND m_btnToolFreeze = nullptr;
        HWND m_btnToolStrandFix = nullptr;
        HWND m_btnUndoEdit = nullptr;
        HWND m_btnRedoEdit = nullptr;
        HWND m_btnTrayLayers = nullptr;
        HWND m_btnTrayHistory = nullptr;
        HWND m_btnTrayMetrics = nullptr;
        HWND m_btnToggleInspector = nullptr;
        HWND m_btnToggleTray = nullptr;
        HWND m_btnToggleAdvanced = nullptr;
        HWND m_btnFastViewport = nullptr;
        HWND m_labelWorkspaceTitle = nullptr;
        HWND m_labelInspectorTitle = nullptr;
        HWND m_labelTrayTitle = nullptr;
        HWND m_labelToolHint = nullptr;
        HWND m_labelSessionStatus = nullptr;
    };
}
