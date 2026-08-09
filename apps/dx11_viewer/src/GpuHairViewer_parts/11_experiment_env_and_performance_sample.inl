// GpuHairViewer.cpp ordered part 11: experiment_env_and_performance_sample
// This file is included by ../GpuHairViewer.cpp; do not compile it directly.
// HAIRGPU_PRENOVELTY_GATE_24FPS: B0/B6/B8 evidence uses the 24 FPS baseline gate;
// the proposal P method retains the independent 60 FPS target in the suite profile.

sample.topLevelNoveltyMask = m_noveltyTelemetry.topLevelNoveltyMask;
sample.overflowCount = m_noveltyTelemetry.overflowCount;
sample.budgetDropCount = m_noveltyTelemetry.budgetDropCount;
sample.deferCount = m_noveltyTelemetry.deferCount;
sample.microtypeDecodedCount = m_noveltyTelemetry.microtypeDecodedCount;
sample.safeDecodeCount = m_noveltyTelemetry.safeDecodeCount;
sample.falseSafeCount = m_noveltyTelemetry.falseSafeCount;
sample.totalSegmentGroupCount = m_noveltyTelemetry.totalSegmentGroupCount;
sample.staticCachedGroupCount = m_noveltyTelemetry.staticCachedGroupCount;
sample.staticTransformedGroupCount = m_noveltyTelemetry.staticTransformedGroupCount;
sample.dynamicGroupCount = m_noveltyTelemetry.dynamicGroupCount;
sample.deferredCorrectionGroupCount = m_noveltyTelemetry.deferredCorrectionGroupCount;
sample.repairRequiredGroupCount = m_noveltyTelemetry.repairRequiredGroupCount;
sample.fallbackGroupCount = m_noveltyTelemetry.fallbackGroupCount;
sample.falseStaticCount = m_noveltyTelemetry.falseStaticCount;
sample.falseStaticRate = m_noveltyTelemetry.falseStaticRate;
sample.cacheHitCount = m_noveltyTelemetry.cacheHitCount;
sample.cacheMissCount = m_noveltyTelemetry.cacheMissCount;
sample.cacheInvalidationCount = m_noveltyTelemetry.cacheInvalidationCount;
sample.watchdogCheckCount = m_noveltyTelemetry.watchdogCheckCount;
sample.watchdogRepairCount = m_noveltyTelemetry.watchdogRepairCount;
sample.staticDynamicBoundaryMeanErrorMm = m_noveltyTelemetry.staticDynamicBoundaryMeanErrorMm;
sample.staticDynamicBoundaryMaxErrorMm = m_noveltyTelemetry.staticDynamicBoundaryMaxErrorMm;
sample.directionCacheMemoryMb = m_noveltyTelemetry.directionCacheMemoryMb;
sample.fullPositionMemoryMb = m_noveltyTelemetry.fullPositionMemoryMb;
sample.directionCacheMemorySavingRate = m_noveltyTelemetry.directionCacheMemorySavingRate;
sample.totalSegmentGroupBlockCount = m_noveltyTelemetry.totalSegmentGroupBlockCount;
sample.dirtyBlockCount = m_noveltyTelemetry.dirtyBlockCount;
sample.dirtyGroupCount = m_noveltyTelemetry.dirtyGroupCount;
sample.certificateValidGroupCount = m_noveltyTelemetry.certificateValidGroupCount;
sample.certificateInvalidGroupCount = m_noveltyTelemetry.certificateInvalidGroupCount;
sample.parentVersionMismatchCount = m_noveltyTelemetry.parentVersionMismatchCount;
sample.colliderVersionMismatchCount = m_noveltyTelemetry.colliderVersionMismatchCount;
sample.editVersionMismatchCount = m_noveltyTelemetry.editVersionMismatchCount;
sample.topologyVersionMismatchCount = m_noveltyTelemetry.topologyVersionMismatchCount;
sample.staticReuseAttemptCount = m_noveltyTelemetry.staticReuseAttemptCount;
sample.sharedPointConflictCount = m_noveltyTelemetry.sharedPointConflictCount;
sample.hemisphereParentResolveCount = m_noveltyTelemetry.hemisphereParentResolveCount;
sample.hemisphereChildResolveCount = m_noveltyTelemetry.hemisphereChildResolveCount;
sample.dynamicCompactionInputCount = m_noveltyTelemetry.dynamicCompactionInputCount;
sample.dynamicCompactionOutputCount = m_noveltyTelemetry.dynamicCompactionOutputCount;
sample.repairCompactionOutputCount = m_noveltyTelemetry.repairCompactionOutputCount;
sample.blockClassificationGpuMilliseconds = m_noveltyTelemetry.blockClassificationGpuMilliseconds;
sample.groupClassificationGpuMilliseconds = m_noveltyTelemetry.groupClassificationGpuMilliseconds;
sample.compactionGpuMilliseconds = m_noveltyTelemetry.compactionGpuMilliseconds;
sample.staticTransformGpuMilliseconds = m_noveltyTelemetry.staticTransformGpuMilliseconds;
sample.dynamicSolveGpuMilliseconds = m_noveltyTelemetry.dynamicSolveGpuMilliseconds;
sample.boundaryRepairGpuMilliseconds = m_noveltyTelemetry.boundaryRepairGpuMilliseconds;
sample.watchdogGpuMilliseconds = m_noveltyTelemetry.watchdogGpuMilliseconds;
sample.telemetryReductionGpuMilliseconds = m_noveltyTelemetry.telemetryReductionGpuMilliseconds;
sample.typedRepairRequestCount = m_noveltyTelemetry.typedRepairRequestCount;
sample.floatFallbackRequestCount = m_noveltyTelemetry.floatFallbackRequestCount;
sample.rejectPayloadCount = m_noveltyTelemetry.rejectPayloadCount;
sample.riskyStrandCount = m_noveltyTelemetry.riskyStrandCount;
sample.repairCommitCount = m_noveltyTelemetry.repairCommitCount;
sample.repairRejectCount = m_noveltyTelemetry.repairRejectCount;
sample.repairDeferCount = m_noveltyTelemetry.repairDeferCount;
sample.islandCandidateCount = m_noveltyTelemetry.islandCandidateCount;
sample.physicsFallbackCount = m_noveltyTelemetry.physicsFallbackCount;
sample.renderCertificateCount = m_noveltyTelemetry.renderCertificateCount;
sample.renderSkipCount = m_noveltyTelemetry.renderSkipCount;
sample.renderCoarseOpacityCount = m_noveltyTelemetry.renderCoarseOpacityCount;
sample.renderStrandRasterCount = m_noveltyTelemetry.renderStrandRasterCount;
sample.renderHighQualityShadowCount = m_noveltyTelemetry.renderHighQualityShadowCount;
sample.renderHighQualitySpecularCount = m_noveltyTelemetry.renderHighQualitySpecularCount;
sample.renderFullQualityCount = m_noveltyTelemetry.renderFullQualityCount;
sample.renderDroppedFullQualityCount = m_noveltyTelemetry.renderDroppedFullQualityCount;
sample.p95ShadowErrorBound = m_noveltyTelemetry.p95ShadowErrorBound;
sample.p95SpecularErrorBound = m_noveltyTelemetry.p95SpecularErrorBound;
sample.temporalFlickerScore = m_noveltyTelemetry.temporalFlickerScore;
sample.editOperationCount = m_noveltyTelemetry.editOperationCount;
sample.editRepairTaskCount = m_noveltyTelemetry.editRepairTaskCount;
sample.editIslandEscalationCount = m_noveltyTelemetry.editIslandEscalationCount;
sample.editCommitCount = m_noveltyTelemetry.editCommitCount;
sample.editRejectCount = m_noveltyTelemetry.editRejectCount;
sample.editPreviewFullQualityCount = m_noveltyTelemetry.editPreviewFullQualityCount;
sample.editLatencyMs = m_noveltyTelemetry.editLatencyMs;
sample.gpuResidentTaskCount = m_noveltyTelemetry.gpuResidentTaskCount;
sample.riskyCompactionCount = m_noveltyTelemetry.riskyCompactionCount;
sample.dispatchIndirectCount = m_noveltyTelemetry.dispatchIndirectCount;
sample.drawIndirectCount = m_noveltyTelemetry.drawIndirectCount;
sample.delayedTelemetryReadCount = m_noveltyTelemetry.delayedTelemetryReadCount;
sample.penetrationEventCount = m_noveltyTelemetry.penetrationEventCount;
sample.meanPenetrationMm = m_noveltyTelemetry.meanPenetrationMm;
sample.maxPenetrationMm = m_noveltyTelemetry.maxPenetrationMm;
sample.predictiveWakeEvaluatedCount = m_noveltyTelemetry.predictiveWakeEvaluatedCount;
sample.predictiveWakeTruePositiveCount = m_noveltyTelemetry.predictiveWakeTruePositiveCount;
sample.predictiveWakeFalsePositiveCount = m_noveltyTelemetry.predictiveWakeFalsePositiveCount;
sample.predictiveWakeFalseNegativeCount = m_noveltyTelemetry.predictiveWakeFalseNegativeCount;
sample.wakeDelayFrameSum = m_noveltyTelemetry.wakeDelayFrameSum;
sample.wakeDelayFrameMax = m_noveltyTelemetry.wakeDelayFrameMax;
sample.wakePositionErrorMeanMm = m_noveltyTelemetry.wakePositionErrorMeanMm;
sample.wakePositionErrorMaxMm = m_noveltyTelemetry.wakePositionErrorMaxMm;
sample.wakeVelocityErrorMean = m_noveltyTelemetry.wakeVelocityErrorMean;
sample.wakeVelocityErrorMax = m_noveltyTelemetry.wakeVelocityErrorMax;
sample.predictiveWakePrecision = m_noveltyTelemetry.predictiveWakePrecision;
sample.predictiveWakeRecall = m_noveltyTelemetry.predictiveWakeRecall;
sample.repairVerifiedCount = m_noveltyTelemetry.repairVerifiedCount;
sample.repairUnverifiedCount = m_noveltyTelemetry.repairUnverifiedCount;
sample.repairFailureCount = m_noveltyTelemetry.repairFailureCount;
sample.repairRecurrenceCount = m_noveltyTelemetry.repairRecurrenceCount;
sample.repairLatencyFrameSum = m_noveltyTelemetry.repairLatencyFrameSum;
sample.repairLatencyFrameMax = m_noveltyTelemetry.repairLatencyFrameMax;
sample.repairExecutionSuccessRate = m_noveltyTelemetry.repairExecutionSuccessRate;
sample.repairVerifiedSuccessRate = m_noveltyTelemetry.repairVerifiedSuccessRate;
sample.repairRecurrenceRate = (m_noveltyTelemetry.repairAttemptCount > 0u)
    ? static_cast<float>(m_noveltyTelemetry.repairRecurrenceCount) / static_cast<float>(m_noveltyTelemetry.repairAttemptCount)
    : std::numeric_limits<float>::quiet_NaN();
sample.determinismMismatchCount = m_noveltyTelemetry.determinismMismatchCount;
sample.stateSequenceHash = m_noveltyTelemetry.stateSequenceHash;
sample.compactionSequenceHash = m_noveltyTelemetry.compactionSequenceHash;
sample.finalPositionHash = m_noveltyTelemetry.finalPositionHash;
sample.wakePropagationGpuMilliseconds = m_noveltyTelemetry.wakePropagationGpuMilliseconds;
sample.followerReconstructionGpuMilliseconds = m_noveltyTelemetry.followerReconstructionGpuMilliseconds;
sample.renderPreparationGpuMilliseconds = m_noveltyTelemetry.renderPreparationGpuMilliseconds;
sample.hairDrawGpuMilliseconds = m_noveltyTelemetry.hairDrawGpuMilliseconds;
sample.totalHairGpuMilliseconds = m_noveltyTelemetry.totalHairGpuMilliseconds;
// HAIRGPU_F5_LOCAL_CAPTURE_RANGE_V2: F5 capture uses the dedicated local
// debugger counters populated from --local-capture-warmup/--local-capture-frames.
// The ordinary interactive ExperimentCaseConfig intentionally remains disabled and
// therefore has zero warm-up/measurement values.
if (m_localDebugCaptureActive &&
    m_localDebugStreamSampleCount <
        static_cast<uint64_t>(m_localDebugWarmupFrames) +
        static_cast<uint64_t>(m_localDebugCaptureFrames))
{
    PerformanceSample localDebugSample = sample;
    localDebugSample.warmupFrames = m_localDebugWarmupFrames;
    localDebugSample.measurementFrames = m_localDebugCaptureFrames;
    localDebugSample.measuredFrameIndex =
        m_globalFrameIndex >= m_localDebugWarmupFrames
            ? static_cast<uint32_t>(m_localDebugMeasurementSampleCount)
            : 0u;
    if (!AppendLocalDebugSample(localDebugSample))
    {
        m_localDebugStreamingFailed = true;
    }
}
return sample;
    }

    bool GpuHairViewer::ExportPerformanceCsv(const std::wstring& filePath) const
    {
        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out)
        {
            return false;
        }

        // Formal CSV and JSONL are two serializations of the same PerformanceSample.
        // Use one canonical numeric format so parity never depends on tolerance or
        // post-hoc reconstruction of defaultfloat output.
        out << std::fixed << std::setprecision(6);

        out << "sampleIndex,elapsedSeconds,frameMilliseconds,simulationMilliseconds,fps,guideCount,followerCount,totalStrandCount,totalParticleCount,activeSetClusterCount,activeSetSelectedClusterCount,activeSetSelectedGuideCount,activeSetSelectedStrandCount,activeSetHighestScore,activeSetAverageSelectedScore,activeSetEvaluationMilliseconds,activeSetBudgetScale,activeSetRecommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctiveStrandCount,correctivePointCount,correctiveAveragePenetration,correctiveMaxPenetration,correctiveAverageLengthErrorBefore,correctiveAverageLengthErrorAfter,globalFrameIndex,measuredFrameIndex,warmupFrames,measurementFrames,caseId,variant,scenario,seed,settingsHash,enableNoNoveltyBaseline,enableMicrotype,enableBudgetedPhysics,enableCertificateRender,enablePerStrandEdit,enableCBMA,enablePhysicsIslands,enableRodRepair,enableGpuResident,certificateFailureCount,certificateFailureRate,fallbackCount,modeSkipCount,modeQSOnlyCount,modeDynamicCount,modeIslandCount,modeRepairCount,modeFloatFallbackCount,islandCount,islandMemberCount,islandCommitCount,islandRejectCount,repairTaskCount,repairCommittedCount,repairRejectedCount,repairDeferredCount,repairSuccessRate,cpuReadbackCount,gpuHairMilliseconds,vramMegabytes,bandwidthMegabytesPerFrame,meanDecodeErrorMm,maxPenetrationMm,phase,noveltyMappingVersion,topLevelNoveltyMask,overflowCount,budgetDropCount,deferCount,microtypeDecodedCount,safeDecodeCount,falseSafeCount,totalSegmentGroupCount,staticCachedGroupCount,staticTransformedGroupCount,dynamicGroupCount,deferredCorrectionGroupCount,repairRequiredGroupCount,fallbackGroupCount,falseStaticCount,falseStaticRate,cacheHitCount,cacheMissCount,cacheInvalidationCount,watchdogCheckCount,watchdogRepairCount,staticDynamicBoundaryMeanErrorMm,staticDynamicBoundaryMaxErrorMm,directionCacheMemoryMb,fullPositionMemoryMb,directionCacheMemorySavingRate,typedRepairRequestCount,floatFallbackRequestCount,rejectPayloadCount,riskyStrandCount,repairCommitCount,repairRejectCount,repairDeferCount,islandCandidateCount,physicsFallbackCount,renderCertificateCount,renderSkipCount,renderCoarseOpacityCount,renderStrandRasterCount,renderHighQualityShadowCount,renderHighQualitySpecularCount,renderFullQualityCount,renderDroppedFullQualityCount,p95ShadowErrorBound,p95SpecularErrorBound,temporalFlickerScore,editOperationCount,editRepairTaskCount,editIslandEscalationCount,editCommitCount,editRejectCount,editPreviewFullQualityCount,editLatencyMs,gpuResidentTaskCount,riskyCompactionCount,dispatchIndirectCount,drawIndirectCount,delayedTelemetryReadCount,totalSegmentGroupBlockCount,dirtyBlockCount,dirtyGroupCount,certificateValidGroupCount,certificateInvalidGroupCount,parentVersionMismatchCount,colliderVersionMismatchCount,editVersionMismatchCount,topologyVersionMismatchCount,staticReuseAttemptCount,sharedPointConflictCount,hemisphereParentResolveCount,hemisphereChildResolveCount,dynamicCompactionInputCount,dynamicCompactionOutputCount,repairCompactionOutputCount,blockClassificationGpuMilliseconds,groupClassificationGpuMilliseconds,compactionGpuMilliseconds,staticTransformGpuMilliseconds,dynamicSolveGpuMilliseconds,boundaryRepairGpuMilliseconds,watchdogGpuMilliseconds,telemetryReductionGpuMilliseconds,microtypeCertificateEnabled,microtypeDirtyPropagationEnabled,microtypeBlockHierarchyEnabled,microtypeBoundaryRepairEnabled,microtypeWatchdogEnabled,microtypeHemisphereOwnershipEnabled,microtypeIndirectDispatchEnabled,microtypeGroupsPerBlock,microtypeWatchdogInterval,frameIntervalMilliseconds,cpuFrameWorkMilliseconds,sourceFrameIndex,availabilityMask,metricAvailabilityMask,unavailableMetricCount,syntheticMetricCount,sameFrameReadbackCount,nanStateCount,infiniteStateCount,fatalFailure,fatalFailureCode,scenarioAppliedEventCount,scenarioAssertionFailureCount,collisionCandidateCount,collisionMissCount,wakeRequestGroupCount,repairAttemptCount,repairSuccessCount,stateTransitionCount,stateOscillationCount,penetrationEventCount,meanPenetrationMm,predictiveWakeEvaluatedCount,predictiveWakeTruePositiveCount,predictiveWakeFalsePositiveCount,predictiveWakeFalseNegativeCount,wakeDelayFrameSum,wakeDelayFrameMax,wakePositionErrorMeanMm,wakePositionErrorMaxMm,wakeVelocityErrorMean,wakeVelocityErrorMax,predictiveWakePrecision,predictiveWakeRecall,repairVerifiedCount,repairUnverifiedCount,repairFailureCount,repairRecurrenceCount,repairLatencyFrameSum,repairLatencyFrameMax,repairExecutionSuccessRate,repairVerifiedSuccessRate,repairRecurrenceRate,determinismMismatchCount,stateSequenceHash,compactionSequenceHash,finalPositionHash,wakePropagationGpuMilliseconds,followerReconstructionGpuMilliseconds,renderPreparationGpuMilliseconds,hairDrawGpuMilliseconds,totalHairGpuMilliseconds\n";
        const std::string caseId = CsvEscapeUtf8(WideToUtf8(m_experimentCase.caseId));
        const std::string variant = CsvEscapeUtf8(WideToUtf8(m_experimentCase.variant));
        const std::string scenario = CsvEscapeUtf8(WideToUtf8(m_experimentCase.scenario));
        const std::string settingsHash = CsvEscapeUtf8(WideToUtf8(m_experimentCase.settingsHash));
        for (size_t i = 0; i < m_performanceSamples.size(); ++i)
        {
            const PerformanceSample& s = m_performanceSamples[i];
            out << i << ","
                << s.elapsedSeconds << ","
                << s.frameMilliseconds << ","
                << s.simulationMilliseconds << ","
                << s.fps << ","
                << s.guideCount << ","
                << s.followerCount << ","
                << s.totalStrandCount << ","
                << s.totalParticleCount << ","
                << s.activeSetClusterCount << ","
                << s.activeSetSelectedClusterCount << ","
                << s.activeSetSelectedGuideCount << ","
                << s.activeSetSelectedStrandCount << ","
                << s.activeSetHighestScore << ","
                << s.activeSetAverageSelectedScore << ","
                << s.activeSetEvaluationMilliseconds << ","
                << s.activeSetBudgetScale << ","
                << s.activeSetRecommendedSolverIterations << ","
                << s.fullTierCount << ","
                << s.reducedTierCount << ","
                << s.frozenTierCount << ","
                << s.cachedTierCount << ","
                << s.correctiveStrandCount << ","
                << s.correctivePointCount << ","
                << s.correctiveAveragePenetration << ","
                << s.correctiveMaxPenetration << ","
                << s.correctiveAverageLengthErrorBefore << ","
                << s.correctiveAverageLengthErrorAfter << ","
                << s.globalFrameIndex << ","
                << s.measuredFrameIndex << ","
                << s.warmupFrames << ","
                << s.measurementFrames << ","
                << caseId << ","
                << variant << ","
                << scenario << ","
                << s.seed << ","
                << settingsHash << ","
                << (m_experimentCase.enableNoNoveltyBaseline ? 1 : 0) << ","
                << (m_experimentCase.enableMicrotype ? 1 : 0) << ","
                << (m_experimentCase.enableBudgetedPhysics ? 1 : 0) << ","
                << (m_experimentCase.enableCertificateRender ? 1 : 0) << ","
                << (m_experimentCase.enablePerStrandEdit ? 1 : 0) << ","
                << (m_experimentCase.enableCBMA ? 1 : 0) << ","
                << (m_experimentCase.enablePhysicsIslands ? 1 : 0) << ","
                << (m_experimentCase.enableRodRepair ? 1 : 0) << ","
                << (m_experimentCase.enableGpuResident ? 1 : 0) << ","
                << s.certificateFailureCount << ","
                << s.certificateFailureRate << ","
                << s.fallbackCount << ","
                << s.modeSkipCount << ","
                << s.modeQSOnlyCount << ","
                << s.modeDynamicCount << ","
                << s.modeIslandCount << ","
                << s.modeRepairCount << ","
                << s.modeFloatFallbackCount << ","
                << s.islandCount << ","
                << s.islandMemberCount << ","
                << s.islandCommitCount << ","
                << s.islandRejectCount << ","
                << s.repairTaskCount << ","
                << s.repairCommittedCount << ","
                << s.repairRejectedCount << ","
                << s.repairDeferredCount << ","
                << s.repairSuccessRate << ","
                << s.cpuReadbackCount << ","
                << s.gpuHairMilliseconds << ","
                << s.vramMegabytes << ","
                << s.bandwidthMegabytesPerFrame << ","
                << s.meanDecodeErrorMm << ","
                << s.maxPenetrationMm
                << "," << "phase1_pre_novelty_final_adjustment"
                << "," << "final5_v1"
                << "," << s.topLevelNoveltyMask
                << "," << s.overflowCount
                << "," << s.budgetDropCount
                << "," << s.deferCount
                << "," << s.microtypeDecodedCount
                << "," << s.safeDecodeCount
                << "," << s.falseSafeCount
                << "," << s.totalSegmentGroupCount
                << "," << s.staticCachedGroupCount
                << "," << s.staticTransformedGroupCount
                << "," << s.dynamicGroupCount
                << "," << s.deferredCorrectionGroupCount
                << "," << s.repairRequiredGroupCount
                << "," << s.fallbackGroupCount
                << "," << s.falseStaticCount
                << "," << s.falseStaticRate
                << "," << s.cacheHitCount
                << "," << s.cacheMissCount
                << "," << s.cacheInvalidationCount
                << "," << s.watchdogCheckCount
                << "," << s.watchdogRepairCount
                << "," << s.staticDynamicBoundaryMeanErrorMm
                << "," << s.staticDynamicBoundaryMaxErrorMm
                << "," << s.directionCacheMemoryMb
                << "," << s.fullPositionMemoryMb
                << "," << s.directionCacheMemorySavingRate
                << "," << s.typedRepairRequestCount
                << "," << s.floatFallbackRequestCount
                << "," << s.rejectPayloadCount
                << "," << s.riskyStrandCount
                << "," << s.repairCommitCount
                << "," << s.repairRejectCount
                << "," << s.repairDeferCount
                << "," << s.islandCandidateCount
                << "," << s.physicsFallbackCount
                << "," << s.renderCertificateCount
                << "," << s.renderSkipCount
                << "," << s.renderCoarseOpacityCount
                << "," << s.renderStrandRasterCount
                << "," << s.renderHighQualityShadowCount
                << "," << s.renderHighQualitySpecularCount
                << "," << s.renderFullQualityCount
                << "," << s.renderDroppedFullQualityCount
                << "," << s.p95ShadowErrorBound
                << "," << s.p95SpecularErrorBound
                << "," << s.temporalFlickerScore
                << "," << s.editOperationCount
                << "," << s.editRepairTaskCount
                << "," << s.editIslandEscalationCount
                << "," << s.editCommitCount
                << "," << s.editRejectCount
                << "," << s.editPreviewFullQualityCount
                << "," << s.editLatencyMs
                << "," << s.gpuResidentTaskCount
                << "," << s.riskyCompactionCount
                << "," << s.dispatchIndirectCount
                << "," << s.drawIndirectCount
                << "," << s.delayedTelemetryReadCount
                << "," << s.totalSegmentGroupBlockCount
                << "," << s.dirtyBlockCount
                << "," << s.dirtyGroupCount
                << "," << s.certificateValidGroupCount
                << "," << s.certificateInvalidGroupCount
                << "," << s.parentVersionMismatchCount
                << "," << s.colliderVersionMismatchCount
                << "," << s.editVersionMismatchCount
                << "," << s.topologyVersionMismatchCount
                << "," << s.staticReuseAttemptCount
                << "," << s.sharedPointConflictCount
                << "," << s.hemisphereParentResolveCount
                << "," << s.hemisphereChildResolveCount
                << "," << s.dynamicCompactionInputCount
                << "," << s.dynamicCompactionOutputCount
                << "," << s.repairCompactionOutputCount
                << "," << s.blockClassificationGpuMilliseconds
                << "," << s.groupClassificationGpuMilliseconds
                << "," << s.compactionGpuMilliseconds
                << "," << s.staticTransformGpuMilliseconds
                << "," << s.dynamicSolveGpuMilliseconds
                << "," << s.boundaryRepairGpuMilliseconds
                << "," << s.watchdogGpuMilliseconds
                << "," << s.telemetryReductionGpuMilliseconds
                << "," << m_noveltyFlags.enableMicrotypeCertificate
                << "," << m_noveltyFlags.enableMicrotypeDirtyPropagation
                << "," << m_noveltyFlags.enableMicrotypeBlockHierarchy
                << "," << m_noveltyFlags.enableMicrotypeBoundaryRepair
                << "," << m_noveltyFlags.enableMicrotypeWatchdog
                << "," << m_noveltyFlags.enableMicrotypeHemisphereOwnership
                << "," << m_noveltyFlags.enableMicrotypeIndirectDispatch
                << "," << m_segmentGroupsPerBlock
                << "," << m_segmentGroupWatchdogIntervalFrames
                << "," << s.frameIntervalMilliseconds
                << "," << s.cpuFrameWorkMilliseconds
                << "," << s.sourceFrameIndex
                << "," << s.availabilityMask
                << "," << s.metricAvailabilityMask
                << "," << s.unavailableMetricCount
                << "," << s.syntheticMetricCount
                << "," << s.sameFrameReadbackCount
                << "," << s.nanStateCount
                << "," << s.infiniteStateCount
                << "," << s.fatalFailure
                << "," << s.fatalFailureCode
                << "," << s.scenarioAppliedEventCount
                << "," << s.scenarioAssertionFailureCount
                << "," << s.collisionCandidateCount
                << "," << s.collisionMissCount
                << "," << s.wakeRequestGroupCount
                << "," << s.repairAttemptCount
                << "," << s.repairSuccessCount
                << "," << s.stateTransitionCount
                << "," << s.stateOscillationCount
                << "," << s.penetrationEventCount
                << "," << s.meanPenetrationMm
                << "," << s.predictiveWakeEvaluatedCount
                << "," << s.predictiveWakeTruePositiveCount
                << "," << s.predictiveWakeFalsePositiveCount
                << "," << s.predictiveWakeFalseNegativeCount
                << "," << s.wakeDelayFrameSum
                << "," << s.wakeDelayFrameMax
                << "," << s.wakePositionErrorMeanMm
                << "," << s.wakePositionErrorMaxMm
                << "," << s.wakeVelocityErrorMean
                << "," << s.wakeVelocityErrorMax
                << "," << s.predictiveWakePrecision
                << "," << s.predictiveWakeRecall
                << "," << s.repairVerifiedCount
                << "," << s.repairUnverifiedCount
                << "," << s.repairFailureCount
                << "," << s.repairRecurrenceCount
                << "," << s.repairLatencyFrameSum
                << "," << s.repairLatencyFrameMax
                << "," << s.repairExecutionSuccessRate
                << "," << s.repairVerifiedSuccessRate
                << "," << s.repairRecurrenceRate
                << "," << s.determinismMismatchCount
                << "," << s.stateSequenceHash
                << "," << s.compactionSequenceHash
                << "," << s.finalPositionHash
                << "," << s.wakePropagationGpuMilliseconds
                << "," << s.followerReconstructionGpuMilliseconds
                << "," << s.renderPreparationGpuMilliseconds
                << "," << s.hairDrawGpuMilliseconds
                << "," << s.totalHairGpuMilliseconds
                << "\n";
        }
        return true;
    }

    bool GpuHairViewer::ExportPerformanceJsonl(const std::wstring& filePath) const
    {
        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out) return false;
        const std::string caseId = JsonEscape(WideToUtf8(m_experimentCase.caseId));
        const std::string variant = JsonEscape(WideToUtf8(m_experimentCase.variant));
        const std::string scenario = JsonEscape(WideToUtf8(m_experimentCase.scenario));
        const std::string settingsHash = JsonEscape(WideToUtf8(m_experimentCase.settingsHash));
        out << std::fixed << std::setprecision(6);
        auto writeNullable = [&out](float value)
        {
            if (std::isfinite(value)) out << value;
            else out << "null";
        };
        for (const PerformanceSample& sample : m_performanceSamples)
        {
            out << "{";
            out << "\"caseId\":\"" << caseId << "\",";
            out << "\"case_id\":\"" << caseId << "\",";
            out << "\"variant\":\"" << variant << "\",";
            out << "\"appliedMethodCode\":\"" << variant << "\",";
            out << "\"scenario\":\"" << scenario << "\",";
            out << "\"seed\":" << sample.seed << ",";
            out << "\"settingsSha256\":\"" << settingsHash << "\",";
            out << "\"sourceFrameIndex\":" << sample.sourceFrameIndex << ",";
            out << "\"globalFrameIndex\":" << sample.globalFrameIndex << ",";
            out << "\"measuredFrameIndex\":" << sample.measuredFrameIndex << ",";
            out << "\"frameMilliseconds\":" << sample.frameMilliseconds << ",";
            out << "\"simulationMilliseconds\":" << sample.simulationMilliseconds << ",";
            out << "\"gpuHairMilliseconds\":" << sample.gpuHairMilliseconds << ",";
            out << "\"gpuTimingResolved\":" << ((sample.availabilityMask & hairgpu::HairTelemetryAvailability_GpuTiming) != 0ull ? "true" : "false") << ",";
            out << "\"availabilityMask\":" << sample.availabilityMask << ",";
            out << "\"metricAvailabilityMask\":" << sample.metricAvailabilityMask << ",";
            out << "\"unavailableMetricCount\":" << sample.unavailableMetricCount << ",";
            out << "\"syntheticMetricCount\":" << sample.syntheticMetricCount << ",";
            out << "\"fatalFailure\":" << sample.fatalFailure << ",";
            out << "\"fatalFailureCode\":" << sample.fatalFailureCode << ",";
            out << "\"guideCount\":" << sample.guideCount << ",";
            out << "\"followerCount\":" << sample.followerCount << ",";
            out << "\"totalStrandCount\":" << sample.totalStrandCount << ",";
            out << "\"totalParticleCount\":" << sample.totalParticleCount << ",";
            out << "\"totalSegmentGroupCount\":" << sample.totalSegmentGroupCount << ",";
            out << "\"staticCachedGroupCount\":" << sample.staticCachedGroupCount << ",";
            out << "\"staticTransformedGroupCount\":" << sample.staticTransformedGroupCount << ",";
            out << "\"dynamicGroupCount\":" << sample.dynamicGroupCount << ",";
            out << "\"repairRequiredGroupCount\":" << sample.repairRequiredGroupCount << ",";
            out << "\"falseStaticCount\":";
            if ((sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_FalseStaticGroundTruth) != 0ull)
                out << sample.falseStaticCount;
            else out << "null";
            out << ",";
            out << "\"falseStaticRate\":"; writeNullable(sample.falseStaticRate); out << ",";
            out << "\"collisionCandidateCount\":" << sample.collisionCandidateCount << ",";
            out << "\"collisionMissCount\":";
            if ((sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_CollisionGroundTruth) != 0ull)
                out << sample.collisionMissCount;
            else out << "null";
            out << ",";
            out << "\"wakeRequestGroupCount\":" << sample.wakeRequestGroupCount << ",";
            out << "\"repairAttemptCount\":" << sample.repairAttemptCount << ",";
            out << "\"repairSuccessCount\":";
            if ((sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_RepairVerification) != 0ull)
                out << sample.repairSuccessCount;
            else out << "null";
            out << ",";
            out << "\"repairSuccessRate\":"; writeNullable(sample.repairSuccessRate); out << ",";
            out << "\"stateTransitionCount\":" << sample.stateTransitionCount << ",";
            out << "\"stateOscillationCount\":" << sample.stateOscillationCount << ",";
            out << "\"fullCpuGroupUploadCount\":" << sample.fullCpuGroupUploadCount << ",";
            out << "\"sameFrameReadbackCount\":" << sample.sameFrameReadbackCount << ",";
            out << "\"nanStateCount\":" << sample.nanStateCount << ",";
            out << "\"infiniteStateCount\":" << sample.infiniteStateCount << ",";
            out << "\"blockClassificationGpuMilliseconds\":" << sample.blockClassificationGpuMilliseconds << ",";
            out << "\"groupClassificationGpuMilliseconds\":" << sample.groupClassificationGpuMilliseconds << ",";
            out << "\"compactionGpuMilliseconds\":" << sample.compactionGpuMilliseconds << ",";
            out << "\"predictiveWakeGpuMilliseconds\":" << sample.predictiveWakeGpuMilliseconds << ",";
            out << "\"staticTransformGpuMilliseconds\":" << sample.staticTransformGpuMilliseconds << ",";
            out << "\"dynamicSolveGpuMilliseconds\":" << sample.dynamicSolveGpuMilliseconds << ",";
            out << "\"velocityRecoveryGpuMilliseconds\":" << sample.velocityRecoveryGpuMilliseconds << ",";
            out << "\"repairSolveGpuMilliseconds\":" << sample.repairSolveGpuMilliseconds << ",";
            out << "\"boundaryRepairGpuMilliseconds\":" << sample.boundaryRepairGpuMilliseconds << ",";
            out << "\"cacheUpdateGpuMilliseconds\":" << sample.cacheUpdateGpuMilliseconds << ",";
            out << "\"watchdogGpuMilliseconds\":" << sample.watchdogGpuMilliseconds << ",";
            out << "\"penetrationEventCount\":" << sample.penetrationEventCount << ",";
            out << "\"meanPenetrationMm\":" << sample.meanPenetrationMm << ",";
            out << "\"predictiveWakeEvaluatedCount\":" << sample.predictiveWakeEvaluatedCount << ",";
            out << "\"predictiveWakeTruePositiveCount\":" << sample.predictiveWakeTruePositiveCount << ",";
            out << "\"predictiveWakeFalsePositiveCount\":" << sample.predictiveWakeFalsePositiveCount << ",";
            out << "\"predictiveWakeFalseNegativeCount\":" << sample.predictiveWakeFalseNegativeCount << ",";
            out << "\"wakeDelayFrameSum\":" << sample.wakeDelayFrameSum << ",";
            out << "\"wakeDelayFrameMax\":" << sample.wakeDelayFrameMax << ",";
            out << "\"wakePositionErrorMeanMm\":" << sample.wakePositionErrorMeanMm << ",";
            out << "\"wakePositionErrorMaxMm\":" << sample.wakePositionErrorMaxMm << ",";
            out << "\"wakeVelocityErrorMean\":" << sample.wakeVelocityErrorMean << ",";
            out << "\"wakeVelocityErrorMax\":" << sample.wakeVelocityErrorMax << ",";
            out << "\"predictiveWakePrecision\":"; writeNullable(sample.predictiveWakePrecision); out << ",";
            out << "\"predictiveWakeRecall\":"; writeNullable(sample.predictiveWakeRecall); out << ",";
            out << "\"repairVerifiedCount\":" << sample.repairVerifiedCount << ",";
            out << "\"repairUnverifiedCount\":" << sample.repairUnverifiedCount << ",";
            out << "\"repairFailureCount\":" << sample.repairFailureCount << ",";
            out << "\"repairRecurrenceCount\":" << sample.repairRecurrenceCount << ",";
            out << "\"repairLatencyFrameSum\":" << sample.repairLatencyFrameSum << ",";
            out << "\"repairLatencyFrameMax\":" << sample.repairLatencyFrameMax << ",";
            out << "\"repairExecutionSuccessRate\":"; writeNullable(sample.repairExecutionSuccessRate); out << ",";
            out << "\"repairVerifiedSuccessRate\":"; writeNullable(sample.repairVerifiedSuccessRate); out << ",";
            out << "\"repairRecurrenceRate\":"; writeNullable(sample.repairRecurrenceRate); out << ",";
            out << "\"determinismMismatchCount\":" << sample.determinismMismatchCount << ",";
            out << "\"stateSequenceHash\":" << sample.stateSequenceHash << ",";
            out << "\"compactionSequenceHash\":" << sample.compactionSequenceHash << ",";
            out << "\"finalPositionHash\":" << sample.finalPositionHash << ",";
            out << "\"wakePropagationGpuMilliseconds\":" << sample.wakePropagationGpuMilliseconds << ",";
            out << "\"followerReconstructionGpuMilliseconds\":" << sample.followerReconstructionGpuMilliseconds << ",";
            out << "\"renderPreparationGpuMilliseconds\":" << sample.renderPreparationGpuMilliseconds << ",";
            out << "\"hairDrawGpuMilliseconds\":" << sample.hairDrawGpuMilliseconds << ",";
            out << "\"totalHairGpuMilliseconds\":" << sample.totalHairGpuMilliseconds << ",";
            out << "\"telemetryReductionGpuMilliseconds\":" << sample.telemetryReductionGpuMilliseconds;
            out << "}\n";
        }
        return true;
    }

    bool GpuHairViewer::ExportExperimentSettingsJson(const std::wstring& filePath) const
    {
        std::ofstream out{ std::filesystem::path(filePath) };
        if (!out) return false;
        const ExperimentRunStatus status = ValidateCompletedExperiment();
        const bool realMeasurement = status == ExperimentRunStatus::CompleteValid;
        const ExperimentMethodFlags& requested = m_experimentCase.requestedFlags;
        const ExperimentMethodFlags& applied = m_experimentCase.appliedFlags;
        out << std::fixed << std::setprecision(9);
        out << "{\n";
        out << "  \"caseId\": \"" << JsonEscape(WideToUtf8(m_experimentCase.caseId)) << "\",\n";
        out << "  \"case_id\": \"" << JsonEscape(WideToUtf8(m_experimentCase.caseId)) << "\",\n";
        out << "  \"variant\": \"" << JsonEscape(WideToUtf8(m_experimentCase.variant)) << "\",\n";
        out << "  \"requestedMethodCode\": \"" << JsonEscape(WideToUtf8(m_experimentCase.variant)) << "\",\n";
        out << "  \"appliedMethodCode\": \"" << JsonEscape(WideToUtf8(m_experimentCase.variant)) << "\",\n";
        out << "  \"scenario\": \"" << JsonEscape(WideToUtf8(m_experimentCase.scenario)) << "\",\n";
        out << "  \"seed\": " << m_experimentCase.seed << ",\n";
        out << "  \"repetitionIndex\": " << m_experimentCase.repetitionIndex << ",\n";
        out << "  \"warmupFrames\": " << m_experimentCase.warmupFrames << ",\n";
        out << "  \"measurementFrames\": " << m_experimentCase.measurementFrames << ",\n";
        out << "  \"fixedTimeStepSeconds\": " << m_experimentCase.fixedTimeStepSeconds << ",\n";
        out << "  \"settingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.settingsHash)) << "\",\n";
        out << "  \"commitSha\": \"" << JsonEscape(WideToUtf8(m_experimentCase.commitSha)) << "\",\n";
        out << "  \"executableSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.executableSha256)) << "\",\n";
        out << "  \"shaderBundleSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.shaderBundleSha256)) << "\",\n";
        out << "  \"assetSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.assetSha256)) << "\",\n";
        out << "  \"assetManifestSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.assetManifestSha256)) << "\",\n";
        out << "  \"scenarioTimelineSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.scenarioTimelineSha256)) << "\",\n";
        out << "  \"requestedMethodSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.requestedMethodSettingsSha256)) << "\",\n";
        out << "  \"appliedMethodSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.appliedMethodSettingsSha256)) << "\",\n";
        out << "  \"observedMethodSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.observedMethodSettingsSha256)) << "\",\n";
        out << "  \"methodSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.appliedMethodSettingsSha256)) << "\",\n";
        out << "  \"methodFlagsMatched\": " << (m_experimentCase.methodFlagsAcknowledged ? "true" : "false") << ",\n";
        out << "  \"realMeasurement\": " << (realMeasurement ? "true" : "false") << ",\n";
        const ExperimentMethodFlags& observedFlags = m_experimentCase.observedFlags;
        auto writeFlags = [&out](const char* name, const ExperimentMethodFlags& flags, bool comma)
        {
            out << "  \"" << name << "\": {\n";
            out << "    \"enableNoNoveltyBaseline\": " << (flags.enableNoNoveltyBaseline ? "true" : "false") << ",\n";
            out << "    \"enableMicrotype\": " << (flags.enableMicrotype ? "true" : "false") << ",\n";
            out << "    \"enableBudgetedPhysics\": " << (flags.enableBudgetedPhysics ? "true" : "false") << ",\n";
            out << "    \"enableCertificateRender\": " << (flags.enableCertificateRender ? "true" : "false") << ",\n";
            out << "    \"enablePerStrandEdit\": " << (flags.enablePerStrandEdit ? "true" : "false") << ",\n";
            out << "    \"enableCBMA\": " << (flags.enableCBMA ? "true" : "false") << ",\n";
            out << "    \"enablePhysicsIslands\": " << (flags.enablePhysicsIslands ? "true" : "false") << ",\n";
            out << "    \"enableRodRepair\": " << (flags.enableRodRepair ? "true" : "false") << ",\n";
            out << "    \"enableGpuResident\": " << (flags.enableGpuResident ? "true" : "false") << ",\n";
            out << "    \"enableSegmentGroupSleep\": " << (flags.enableSegmentGroupSleep ? "true" : "false") << ",\n";
            out << "    \"enableSegmentGroupIntegratedState\": " << (flags.enableSegmentGroupIntegratedState ? "true" : "false") << ",\n";
            out << "    \"enableExternalStateArray\": " << (flags.enableExternalStateArray ? "true" : "false") << ",\n";
            out << "    \"enableSharedPointOwnership\": " << (flags.enableSharedPointOwnership ? "true" : "false") << ",\n";
            out << "    \"enableDirectionalResponsibility\": " << (flags.enableDirectionalResponsibility ? "true" : "false") << ",\n";
            out << "    \"enableStaticCertificate\": " << (flags.enableStaticCertificate ? "true" : "false") << ",\n";
            out << "    \"enablePredictiveWake\": " << (flags.enablePredictiveWake ? "true" : "false") << ",\n";
            out << "    \"enableVelocityRecovery\": " << (flags.enableVelocityRecovery ? "true" : "false") << ",\n";
            out << "    \"enableRepair\": " << (flags.enableRepair ? "true" : "false") << ",\n";
            out << "    \"enableGpuCompaction\": " << (flags.enableGpuCompaction ? "true" : "false") << ",\n";
            out << "    \"enableActiveSet\": " << (flags.enableActiveSet ? "true" : "false") << ",\n";
            out << "    \"enableStrandSleep\": " << (flags.enableStrandSleep ? "true" : "false") << ",\n";
            out << "    \"enableBlockSleep\": " << (flags.enableBlockSleep ? "true" : "false") << ",\n";
            out << "    \"enableSleepHysteresis\": " << (flags.enableSleepHysteresis ? "true" : "false") << ",\n";
            out << "    \"enableStaticCache\": " << (flags.enableStaticCache ? "true" : "false") << ",\n";
            out << "    \"stateStorage\": \""
                << (flags.stateStorage == SegmentGroupStateStorage::IntegratedRecord ? "IntegratedRecord" : "ExternalArray")
                << "\"\n";
            out << "  }" << (comma ? "," : "") << "\n";
        };
        writeFlags("requested", requested, true);
        writeFlags("applied", applied, true);
        writeFlags("observedFlags", observedFlags, true);
        out << "  \"observed\": {\n";
        out << "    \"adapterType\": \"" << (m_observedDriverType == D3D_DRIVER_TYPE_HARDWARE ? "hardware" : "non_hardware") << "\",\n";
        out << "    \"gpuName\": \"" << JsonEscape(WideToUtf8(m_observedGpuName)) << "\",\n";
        out << "    \"gpuVendorId\": " << m_observedGpuVendorId << ",\n";
        out << "    \"gpuDeviceId\": " << m_observedGpuDeviceId << ",\n";
        out << "    \"dedicatedVideoMemoryBytes\": " << m_observedDedicatedVideoMemory << ",\n";
        out << "    \"renderWidth\": " << m_actualRenderWidth << ",\n";
        out << "    \"renderHeight\": " << m_actualRenderHeight << ",\n";
        out << "    \"displayStrandCount\": " << m_asset.strands.size() << ",\n";
        out << "    \"guideCount\": " << m_asset.guideStrandCount << ",\n";
        out << "    \"particlesPerGuide\": " << m_asset.particlesPerStrand << ",\n";
        out << "    \"solverIterations\": " << m_simConfig.solverIterations << ",\n";
        out << "    \"vsync\": " << (m_vsyncEnabled ? "true" : "false") << ",\n";
        out << "    \"presentSyncInterval\": " << m_lastObservedPresentSyncInterval << "\n";
        out << "  }\n";
        out << "}\n";
        return true;
    }

    bool GpuHairViewer::ExportActiveSetPlanJson(const std::wstring& filePath) const
    {
        std::string error;
        return hairgpu::WriteActiveSetPlanJson(std::filesystem::path(filePath), m_asset, m_activeSetPlan, error);
    }

    const char* GpuHairViewer::ExperimentRunStatusToString(ExperimentRunStatus status)
    {
        switch (status)
        {
        case ExperimentRunStatus::InProgress: return "in_progress";
        case ExperimentRunStatus::CompleteValid: return "complete_valid";
        case ExperimentRunStatus::CompleteInvalid: return "complete_invalid";
        case ExperimentRunStatus::Incomplete: return "incomplete";
        case ExperimentRunStatus::Failed: return "failed";
        case ExperimentRunStatus::Synthetic: return "synthetic";
        default: return "failed";
        }
    }

    void GpuHairViewer::SetFatalExperimentFailure(uint32_t errorCode, const std::wstring& message)
    {
        if (m_experimentFailureCode == 0u)
        {
            m_experimentFailureCode = errorCode;
            m_experimentFailureMessage = message;
        }
        m_experimentRunStatus = ExperimentRunStatus::Failed;
        m_experimentCase.runStatus = ExperimentRunStatus::Failed;
        m_noveltyTelemetry.fatalFailure = 1u;
        m_noveltyTelemetry.fatalFailureCode = errorCode;
        ++m_noveltyTelemetry.fatalRuntimeFailureCount;
    }

    GpuHairViewer::ExperimentRunStatus GpuHairViewer::ValidateCompletedExperiment() const
    {
        if (!m_experimentCase.enabled) return ExperimentRunStatus::Incomplete;
        if (m_experimentRunStatus == ExperimentRunStatus::Failed ||
            m_experimentFailureCode != 0u || m_noveltyTelemetry.fatalRuntimeFailureCount != 0u)
            return ExperimentRunStatus::Failed;
        if (m_syntheticMetricCount != 0u || m_noveltyTelemetry.syntheticMetricCount != 0u)
            return ExperimentRunStatus::Synthetic;
        if (m_performanceSamples.size() != static_cast<size_t>(m_experimentCase.measurementFrames))
            return ExperimentRunStatus::Incomplete;
        const float timingCoverage = m_performanceSamples.empty() ? 0.0f :
            static_cast<float>(m_gpuTimingValidCount) / static_cast<float>(m_performanceSamples.size());
        const bool scenarioAssertionsPass =
            m_scenarioAppliedEventCount >= m_experimentCase.scenarioAssertionMinAppliedEvents &&
            m_scenarioCollisionCandidateCount >= m_experimentCase.scenarioAssertionMinCollisionCandidates &&
            m_scenarioWakeRequestCount >= m_experimentCase.scenarioAssertionMinWakeRequests &&
            m_scenarioRepairAttemptCount >= m_experimentCase.scenarioAssertionMinRepairs &&
            m_scenarioStateTransitionCount >= m_experimentCase.scenarioAssertionMinTransitions;
        const bool formalIdentityValid =
            !m_experimentCase.caseId.empty() &&
            m_experimentCase.methodId != ExperimentMethodId::Unknown &&
            m_experimentCase.scenarioId != ExperimentScenarioId::Unknown &&
            m_experimentCase.methodFlagsAcknowledged &&
            m_experimentCase.timelineLoaded &&
            m_experimentCase.assetFrozen &&
            m_experimentCase.formalAssetLoaded &&
            !m_experimentCase.formalAssetPath.empty() &&
            !m_experimentCase.observedAssetPath.empty() &&
            !m_experimentCase.formalAssetSha256.empty() &&
            !m_experimentCase.formalTopologySha256.empty() &&
            !m_experimentCase.generatorSettingsSha256.empty() &&
            !m_experimentCase.settingsHash.empty() &&
            !m_experimentCase.executableSha256.empty() &&
            !m_experimentCase.shaderBundleSha256.empty() &&
            !m_experimentCase.assetSha256.empty() &&
            !m_experimentCase.scenarioTimelineSha256.empty() &&
            !m_experimentCase.requestedMethodSettingsSha256.empty() &&
            m_experimentCase.requestedMethodSettingsSha256 == m_experimentCase.appliedMethodSettingsSha256 &&
            m_experimentCase.appliedMethodSettingsSha256 == m_experimentCase.observedMethodSettingsSha256;
        const bool observedSettingsValid =
            m_observedDriverType == D3D_DRIVER_TYPE_HARDWARE &&
            m_actualRenderWidth == m_experimentCase.requestedWidth &&
            m_actualRenderHeight == m_experimentCase.requestedHeight &&
            m_asset.strands.size() == static_cast<size_t>(m_experimentCase.requestedDisplayStrandCount) &&
            m_asset.guideStrandCount == m_experimentCase.requestedGuideCount &&
            m_asset.particlesPerStrand == m_experimentCase.requestedParticlesPerGuide &&
            m_simConfig.solverIterations == m_experimentCase.requestedSolverIterations &&
            !m_vsyncEnabled && m_lastObservedPresentSyncInterval == 0u;
        const bool runtimeQualityValid =
            timingCoverage >= 0.99f &&
            m_gpuTimingUnresolvedCount == 0u &&
            m_nanStateCount == 0u && m_infiniteStateCount == 0u &&
            m_sameFrameReadbackCount == 0u && scenarioAssertionsPass;
        return formalIdentityValid && observedSettingsValid && runtimeQualityValid
            ? ExperimentRunStatus::CompleteValid
            : ExperimentRunStatus::CompleteInvalid;
    }

    bool GpuHairViewer::ExportRuntimeReport(const std::wstring& filePath) const
    {
        const std::filesystem::path summaryPath(filePath);
        const std::filesystem::path baseDir = summaryPath.parent_path();
        std::wstring runStem = summaryPath.stem().wstring();
        const std::wstring suffix = L"_summary";
        if (runStem.size() >= suffix.size() && runStem.substr(runStem.size() - suffix.size()) == suffix)
            runStem.resize(runStem.size() - suffix.size());
        const std::filesystem::path frameCsvPath = baseDir / (runStem + L"_frames.csv");
        const std::filesystem::path frameJsonlPath = baseDir / (runStem + L"_frames.jsonl");
        const std::filesystem::path settingsPath = baseDir / (runStem + L"_settings.json");
        const bool frameCsvOk = ExportPerformanceCsv(frameCsvPath.wstring());
        const bool frameJsonlOk = ExportPerformanceJsonl(frameJsonlPath.wstring());
        const bool settingsOk = ExportExperimentSettingsJson(settingsPath.wstring());

        std::vector<float> frameMilliseconds;
        std::vector<float> simulationMilliseconds;
        std::vector<float> gpuMilliseconds;
        frameMilliseconds.reserve(m_performanceSamples.size());
        simulationMilliseconds.reserve(m_performanceSamples.size());
        gpuMilliseconds.reserve(m_performanceSamples.size());
        uint64_t collisionCandidates = 0u, collisionMisses = 0u;
        uint64_t wakeTruePositive = 0u, wakeFalsePositive = 0u, wakeFalseNegative = 0u;
        uint64_t repairAttempts = 0u, repairSuccesses = 0u, repairVerified = 0u, repairRecurrence = 0u;
        uint64_t falseStatic = 0u, staticGroups = 0u;
        uint64_t stateTransitions = 0u, stateOscillations = 0u, determinismMismatches = 0u;
        uint64_t unavailableMetrics = 0u, syntheticMetrics = 0u;
        uint32_t maximumWakeDelay = 0u;
        float maximumBoundaryErrorMm = 0.0f;
        uint64_t stateSequenceHash = 0ull, compactionSequenceHash = 0ull, finalPositionHash = 0ull;
        bool collisionGroundTruthAvailable = !m_performanceSamples.empty();
        bool wakeGroundTruthAvailable = !m_performanceSamples.empty();
        bool repairVerificationAvailable = !m_performanceSamples.empty();
        bool boundaryComparisonAvailable = !m_performanceSamples.empty();
        bool determinismAvailable = !m_performanceSamples.empty();
        bool falseStaticGroundTruthAvailable = !m_performanceSamples.empty();
        for (const PerformanceSample& sample : m_performanceSamples)
        {
            frameMilliseconds.push_back(sample.frameMilliseconds);
            simulationMilliseconds.push_back(sample.simulationMilliseconds);
            if ((sample.availabilityMask & hairgpu::HairTelemetryAvailability_GpuTiming) != 0ull)
                gpuMilliseconds.push_back(sample.gpuHairMilliseconds);
            collisionCandidates += sample.collisionCandidateCount;
            collisionMisses += sample.collisionMissCount;
            wakeTruePositive += sample.predictiveWakeTruePositiveCount;
            wakeFalsePositive += sample.predictiveWakeFalsePositiveCount;
            wakeFalseNegative += sample.predictiveWakeFalseNegativeCount;
            maximumWakeDelay = std::max(maximumWakeDelay, sample.wakeDelayFrameMax);
            repairAttempts += sample.repairAttemptCount;
            repairSuccesses += sample.repairSuccessCount;
            repairVerified += sample.repairVerifiedCount;
            repairRecurrence += sample.repairRecurrenceCount;
            falseStatic += sample.falseStaticCount;
            staticGroups += sample.staticCachedGroupCount + sample.staticTransformedGroupCount;
            stateTransitions += sample.stateTransitionCount;
            stateOscillations += sample.stateOscillationCount;
            determinismMismatches += sample.determinismMismatchCount;
            maximumBoundaryErrorMm = std::max(maximumBoundaryErrorMm, sample.staticDynamicBoundaryMaxErrorMm);
            stateSequenceHash = sample.stateSequenceHash;
            compactionSequenceHash = sample.compactionSequenceHash;
            finalPositionHash = sample.finalPositionHash;
            unavailableMetrics += sample.unavailableMetricCount;
            syntheticMetrics += sample.syntheticMetricCount;
            collisionGroundTruthAvailable = collisionGroundTruthAvailable &&
                (sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_CollisionGroundTruth) != 0ull;
            wakeGroundTruthAvailable = wakeGroundTruthAvailable &&
                (sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_WakeGroundTruth) != 0ull;
            repairVerificationAvailable = repairVerificationAvailable &&
                (sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_RepairVerification) != 0ull;
            boundaryComparisonAvailable = boundaryComparisonAvailable &&
                (sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_BoundaryComparison) != 0ull;
            determinismAvailable = determinismAvailable &&
                (sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_DeterminismHash) != 0ull;
            falseStaticGroundTruthAvailable = falseStaticGroundTruthAvailable &&
                (sample.metricAvailabilityMask & hairgpu::HairTelemetryAvailability_FalseStaticGroundTruth) != 0ull;
        }
        const ExperimentRunStatus status = ValidateCompletedExperiment();
        const bool realMeasurement = status == ExperimentRunStatus::CompleteValid;
        const float timingCoverage = m_performanceSamples.empty() ? 0.0f :
            static_cast<float>(m_gpuTimingValidCount) / static_cast<float>(m_performanceSamples.size());
        const auto nullableRate = [](uint64_t numerator, uint64_t denominator) -> float
        {
            return denominator > 0u ? static_cast<float>(numerator) / static_cast<float>(denominator)
                : std::numeric_limits<float>::quiet_NaN();
        };
        auto writeNullable = [](std::ostream& stream, float value)
        {
            if (std::isfinite(value)) stream << value;
            else stream << "null";
        };

        std::ofstream out(summaryPath);
        if (!out) return false;
        out << std::fixed << std::setprecision(9);
        out << "{\n";
        out << "  \"status\": \"" << ExperimentRunStatusToString(status) << "\",\n";
        out << "  \"validationStatus\": \"" << ExperimentRunStatusToString(status) << "\",\n";
        out << "  \"realMeasurement\": " << (realMeasurement ? "true" : "false") << ",\n";
        out << "  \"caseId\": \"" << JsonEscape(WideToUtf8(m_experimentCase.caseId)) << "\",\n";
        out << "  \"case_id\": \"" << JsonEscape(WideToUtf8(m_experimentCase.caseId)) << "\",\n";
        out << "  \"variant\": \"" << JsonEscape(WideToUtf8(m_experimentCase.variant)) << "\",\n";
        out << "  \"requestedMethodCode\": \"" << JsonEscape(WideToUtf8(m_experimentCase.variant)) << "\",\n";
        out << "  \"appliedMethodCode\": \"" << JsonEscape(WideToUtf8(m_experimentCase.variant)) << "\",\n";
        out << "  \"observedMethodCode\": \"" << JsonEscape(WideToUtf8(m_experimentCase.variant)) << "\",\n";
        out << "  \"scenario\": \"" << JsonEscape(WideToUtf8(m_experimentCase.scenario)) << "\",\n";
        out << "  \"seed\": " << m_experimentCase.seed << ",\n";
        out << "  \"repetitionIndex\": " << m_experimentCase.repetitionIndex << ",\n";
        out << "  \"warmup_frames\": " << m_experimentCase.warmupFrames << ",\n";
        out << "  \"measurement_frames\": " << m_experimentCase.measurementFrames << ",\n";
        out << "  \"sample_count\": " << m_performanceSamples.size() << ",\n";
        out << "  \"settingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.settingsHash)) << "\",\n";
        out << "  \"commitSha\": \"" << JsonEscape(WideToUtf8(m_experimentCase.commitSha)) << "\",\n";
        out << "  \"executableSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.executableSha256)) << "\",\n";
        out << "  \"shaderBundleSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.shaderBundleSha256)) << "\",\n";
        out << "  \"assetSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.assetSha256)) << "\",\n";
        out << "  \"formalAssetPath\": \"" << JsonEscape(WideToUtf8(m_experimentCase.formalAssetPath)) << "\",\n";
        out << "  \"observedAssetPath\": \"" << JsonEscape(WideToUtf8(m_experimentCase.observedAssetPath)) << "\",\n";
        out << "  \"formalAssetSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.formalAssetSha256)) << "\",\n";
        out << "  \"formalTopologySha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.formalTopologySha256)) << "\",\n";
        out << "  \"generatorSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.generatorSettingsSha256)) << "\",\n";
        out << "  \"formalAssetLoaded\": " << (m_experimentCase.formalAssetLoaded ? "true" : "false") << ",\n";
        out << "  \"assetManifestSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.assetManifestSha256)) << "\",\n";
        out << "  \"scenarioTimelineSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.scenarioTimelineSha256)) << "\",\n";
        out << "  \"requestedMethodSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.requestedMethodSettingsSha256)) << "\",\n";
        out << "  \"appliedMethodSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.appliedMethodSettingsSha256)) << "\",\n";
        out << "  \"observedMethodSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.observedMethodSettingsSha256)) << "\",\n";
        out << "  \"methodSettingsSha256\": \"" << JsonEscape(WideToUtf8(m_experimentCase.appliedMethodSettingsSha256)) << "\",\n";
        out << "  \"methodFlagsMatched\": " << (m_experimentCase.methodFlagsAcknowledged ? "true" : "false") << ",\n";
        out << "  \"assetFrozen\": " << (m_experimentCase.assetFrozen ? "true" : "false") << ",\n";
        out << "  \"actualAdapterType\": \"" << (m_observedDriverType == D3D_DRIVER_TYPE_HARDWARE ? "hardware" : "non_hardware") << "\",\n";
        out << "  \"gpuName\": \"" << JsonEscape(WideToUtf8(m_observedGpuName)) << "\",\n";
        out << "  \"actualRenderWidth\": " << m_actualRenderWidth << ",\n";
        out << "  \"actualRenderHeight\": " << m_actualRenderHeight << ",\n";
        out << "  \"actualDisplayStrandCount\": " << m_asset.strands.size() << ",\n";
        out << "  \"gpuTimingResolvedFrameCount\": " << m_gpuTimingValidCount << ",\n";
        out << "  \"gpuTimingUnresolvedFrameCount\": " << m_gpuTimingUnresolvedCount << ",\n";
        out << "  \"gpuTimingCoverage\": " << timingCoverage << ",\n";
        out << "  \"fatalRuntimeFailureCount\": " << m_noveltyTelemetry.fatalRuntimeFailureCount << ",\n";
        out << "  \"fatalFailureCode\": " << m_experimentFailureCode << ",\n";
        out << "  \"fatalFailureMessage\": \"" << JsonEscape(WideToUtf8(m_experimentFailureMessage)) << "\",\n";
        out << "  \"unavailableMetricCount\": " << unavailableMetrics + m_unavailableMetricCount << ",\n";
        out << "  \"syntheticMetricCount\": " << syntheticMetrics + m_syntheticMetricCount << ",\n";
        out << "  \"fullCpuGroupUploadCount\": " << m_fullCpuGroupUploadCount << ",\n";
        out << "  \"sameFrameReadbackCount\": " << m_sameFrameReadbackCount << ",\n";
        out << "  \"nanStateCount\": " << m_nanStateCount << ",\n";
        out << "  \"infiniteStateCount\": " << m_infiniteStateCount << ",\n";
        out << "  \"visualValidationPassed\": "
            << ((m_nanStateCount == 0u && m_infiniteStateCount == 0u) ? "true" : "false")
            << ",\n";
        out << "  \"scenarioAppliedEventCount\": " << m_scenarioAppliedEventCount << ",\n";
        out << "  \"scenarioAssertionFailureCount\": " << m_scenarioAssertionFailureCount << ",\n";
        out << "  \"collisionCandidateCount\": " << collisionCandidates << ",\n";
        out << "  \"collisionMissCount\": ";
        if (collisionGroundTruthAvailable) out << collisionMisses; else out << "null";
        out << ",\n";
        out << "  \"collisionMissRate\": ";
        writeNullable(out, collisionGroundTruthAvailable
            ? nullableRate(collisionMisses, collisionCandidates)
            : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"wakeRequestGroupCount\": " << m_scenarioWakeRequestCount << ",\n";
        out << "  \"predictiveWakePrecision\": ";
        writeNullable(out, wakeGroundTruthAvailable
            ? nullableRate(wakeTruePositive, wakeTruePositive + wakeFalsePositive)
            : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"predictiveWakeRecall\": ";
        writeNullable(out, wakeGroundTruthAvailable
            ? nullableRate(wakeTruePositive, wakeTruePositive + wakeFalseNegative)
            : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"wakeDelayFrameMax\": " << maximumWakeDelay << ",\n";
        out << "  \"repairAttemptCount\": " << repairAttempts << ",\n";
        out << "  \"repairSuccessCount\": ";
        if (repairVerificationAvailable) out << repairSuccesses; else out << "null";
        out << ",\n";
        out << "  \"repairSuccessRate\": ";
        writeNullable(out, repairVerificationAvailable
            ? nullableRate(repairSuccesses, repairAttempts)
            : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"repairExecutionSuccessRate\": ";
        writeNullable(out, repairVerificationAvailable
            ? nullableRate(repairSuccesses, repairAttempts)
            : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"repairVerifiedSuccessRate\": ";
        writeNullable(out, repairVerificationAvailable
            ? nullableRate(repairVerified, repairAttempts)
            : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"repairRecurrenceRate\": ";
        writeNullable(out, repairVerificationAvailable
            ? nullableRate(repairRecurrence, repairAttempts)
            : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"repairVerifiedCount\": " << repairVerified << ",\n";
        out << "  \"repairRecurrenceCount\": " << repairRecurrence << ",\n";
        out << "  \"falseStaticCount\": ";
        if (falseStaticGroundTruthAvailable) out << falseStatic; else out << "null";
        out << ",\n";
        out << "  \"falseStaticRate\": ";
        writeNullable(out, falseStaticGroundTruthAvailable
            ? nullableRate(falseStatic, staticGroups)
            : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"stateTransitionCount\": " << stateTransitions << ",\n";
        out << "  \"stateOscillationCount\": " << stateOscillations << ",\n";
        out << "  \"stateOscillationRate\": ";
        writeNullable(out, nullableRate(stateOscillations, stateTransitions));
        out << ",\n";
        out << "  \"staticDynamicBoundaryMaxErrorMm\": ";
        writeNullable(out, boundaryComparisonAvailable ? maximumBoundaryErrorMm : std::numeric_limits<float>::quiet_NaN());
        out << ",\n";
        out << "  \"determinismMismatchCount\": " << determinismMismatches << ",\n";
        out << "  \"stateSequenceHash\": " << (determinismAvailable ? stateSequenceHash : 0ull) << ",\n";
        out << "  \"compactionSequenceHash\": " << (determinismAvailable ? compactionSequenceHash : 0ull) << ",\n";
        out << "  \"finalPositionHash\": " << (determinismAvailable ? finalPositionHash : 0ull) << ",\n";
        out << "  \"avg_frame_ms\": " << ComputeAverage(frameMilliseconds) << ",\n";
        out << "  \"p50_frame_ms\": " << ComputePercentile(frameMilliseconds, 50.0f) << ",\n";
        out << "  \"p95_frame_ms\": " << ComputePercentile(frameMilliseconds, 95.0f) << ",\n";
        out << "  \"p99_frame_ms\": " << ComputePercentile(frameMilliseconds, 99.0f) << ",\n";
        out << "  \"p999_frame_ms\": " << ComputePercentile(frameMilliseconds, 99.9f) << ",\n";
        out << "  \"worst_frame_ms\": " << (frameMilliseconds.empty() ? 0.0f : *std::max_element(frameMilliseconds.begin(), frameMilliseconds.end())) << ",\n";
        out << "  \"avg_simulation_ms\": " << ComputeAverage(simulationMilliseconds) << ",\n";
        out << "  \"avg_gpu_hair_ms\": " << ComputeAverage(gpuMilliseconds) << ",\n";
        out << "  \"exports\": {\n";
        out << "    \"frameCsv\": " << (frameCsvOk ? "true" : "false") << ",\n";
        out << "    \"frameJsonl\": " << (frameJsonlOk ? "true" : "false") << ",\n";
        out << "    \"settingsJson\": " << (settingsOk ? "true" : "false") << "\n";
        out << "  },\n";
        out << "  \"canonicalResultSha256\": \"\",\n";
        out << "  \"validationReasons\": []\n";
        out << "}\n";
        return true;
    }

    bool GpuHairViewer::ExportLocalDebugCapture(
        const std::filesystem::path& outputDirectory,
        const std::wstring& runName) const
    {
        std::error_code errorCode;
        std::filesystem::create_directories(outputDirectory, errorCode);
        if (errorCode || runName.empty())
        {
            return false;
        }

        const std::filesystem::path stem = outputDirectory / runName;
        const std::filesystem::path streamedFrameCsv = stem.wstring() + L"_frames.csv";
        const std::filesystem::path streamedFrameJsonl = stem.wstring() + L"_frames.jsonl";
        const std::filesystem::path streamedPerSecond = stem.wstring() + L"_per_second.csv";
        // FinalizeLocalDebugStreaming already promoted the crash-safe streams.  Do
        // not overwrite them with an in-memory exporter that may contain a different
        // capture range during an ordinary F5 launch.
        const bool frameCsvOk = std::filesystem::is_regular_file(streamedFrameCsv)
            || ExportPerformanceCsv(streamedFrameCsv.wstring());
        const bool frameJsonlOk = std::filesystem::is_regular_file(streamedFrameJsonl)
            || ExportPerformanceJsonl(streamedFrameJsonl.wstring());
        const bool perSecondOk = std::filesystem::is_regular_file(streamedPerSecond)
            || ExportPerSecondTelemetryCsv(streamedPerSecond.wstring());
        const bool settingsOk = ExportExperimentSettingsJson((stem.wstring() + L"_settings.json"));
        const bool summaryOk = ExportRuntimeReport((stem.wstring() + L"_summary.json"));
        return frameCsvOk && frameJsonlOk && perSecondOk && settingsOk && summaryOk;
    }

    bool GpuHairViewer::ProjectWorldToScreen(const Vec3& world, float& outX, float& outY) const
    {
        const RECT renderRect = GetRenderViewportRect();
        const float renderWidth = static_cast<float>(std::max<LONG>(renderRect.right - renderRect.left, 1));
        const float renderHeight = static_cast<float>(std::max<LONG>(renderRect.bottom - renderRect.top, 1));
        const float aspectRatio = std::max(renderWidth / std::max(renderHeight, 1.0f), 0.6f);
        Vec3 ndc;
        if (!ProjectWorldToNdc(world, m_cameraPosition, m_cameraTarget, aspectRatio, ndc))
        {
            return false;
        }
        outX = renderRect.left + (ndc.x * 0.5f + 0.5f) * renderWidth;
        outY = renderRect.top + (-ndc.y * 0.5f + 0.5f) * renderHeight;
        return true;
    }


    bool GpuHairViewer::IsBrushDrivenTool() const
    {
        if (!IsGroomWorkspace())
        {
            return false;
        }
        switch (m_groomTool)
        {
        case GroomTool::Comb:
        case GroomTool::Grab:
        case GroomTool::Smooth:
        case GroomTool::Length:
        case GroomTool::Volume:
        case GroomTool::Part:
        case GroomTool::Freeze:
            return true;
        default:
            return false;
        }
    }

    bool GpuHairViewer::IsStrandLocked(uint32_t strandIndex) const
    {
        if (strandIndex >= m_lockedStrands.size())
        {
            return false;
        }
        return m_lockedStrands[strandIndex] != 0u;
    }

    bool GpuHairViewer::ComputeBrushCenterUvFromScreenPoint(POINT clientPoint, Vec2& outUv, int32_t* outGuideIndex, float* outScreenDistancePx) const
    {
        const uint32_t guideCount = std::min<uint32_t>(m_asset.guideStrandCount, static_cast<uint32_t>(m_asset.strands.size()));
        if (guideCount == 0u || m_asset.rootBindings.size() < guideCount)
        {
            return false;
        }

        std::vector<GpuParticle> particles;
        const bool haveParticles = const_cast<GpuHairViewer*>(this)->ReadBackCurrentParticlePositions(particles) && particles.size() >= m_asset.restPoints.size();

        float bestDistSq = 70.0f * 70.0f;
        int32_t bestGuide = -1;
        Vec2 bestUv = { 0.5f, 0.5f };
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            const StrandRange& strand = m_asset.strands[guideIndex];
            if (strand.pointCount == 0u)
            {
                continue;
            }
            Vec3 root = m_asset.restPoints[strand.pointOffset];
            if (haveParticles)
            {
                const GpuParticle& p = particles[strand.pointOffset];
                root = Vec3(p.x, p.y, p.z);
            }
            float sx = 0.0f;
            float sy = 0.0f;
            if (!ProjectWorldToScreen(root, sx, sy))
            {
                continue;
            }
            const float dx = static_cast<float>(clientPoint.x) - sx;
            const float dy = static_cast<float>(clientPoint.y) - sy;
            const float distSq = dx * dx + dy * dy;
            if (distSq < bestDistSq)
            {
                bestDistSq = distSq;
                bestGuide = static_cast<int32_t>(guideIndex);
                bestUv = m_asset.rootBindings[guideIndex].rootUv;
            }
        }

        if (bestGuide < 0)
        {
            return false;
        }
        outUv = bestUv;
        if (outGuideIndex)
        {
            *outGuideIndex = bestGuide;
        }
        if (outScreenDistancePx)
        {
            *outScreenDistancePx = std::sqrt(bestDistSq);
        }
        return true;
    }

    Vec3 GpuHairViewer::ComputeWorldDeltaFromScreenDelta(float dxPixels, float dyPixels) const
    {
        const Vec3 forward = Normalize(m_cameraTarget - m_cameraPosition);
        const Vec3 safeUp = (std::fabs(forward.y) > 0.98f) ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f);
        const Vec3 right = Normalize(Cross(forward, safeUp));
        const Vec3 up = Normalize(Cross(right, forward));
        const float scale = std::max(0.00045f, m_cameraDistance * 0.00085f);
        return right * (dxPixels * scale) + up * (-dyPixels * scale);
    }

    hairgpu::EditLayer GpuHairViewer::BuildViewportBrushLayer(const Vec2& centerUv,
        const Vec2& startUv,
        const Vec2& currentUv,
        const POINT& currentPoint,
        uint32_t* outAffectedGuideCount) const
    {
        hairgpu::EditLayer layer = {};
        ++const_cast<GpuHairViewer*>(this)->m_viewportStrokeSerial;
        std::ostringstream name;
        switch (m_groomTool)
        {
        case GroomTool::Comb: name << "comb_stroke_"; break;
        case GroomTool::Grab: name << "grab_stroke_"; break;
        case GroomTool::Smooth: name << "smooth_stroke_"; break;
        case GroomTool::Length: name << "length_stroke_"; break;
        case GroomTool::Volume: name << "volume_stroke_"; break;
        case GroomTool::Part: name << "part_stroke_"; break;
        case GroomTool::Freeze: name << "freeze_mask_"; break;
        default: name << "stroke_"; break;
        }
        name << m_viewportStrokeSerial;
        layer.name = name.str();
        layer.enabled = true;
        layer.layerWeight = hairgpu::Clamp(m_quickEditLayerWeight, 0.0f, 1.5f);
        layer.region.mode = m_quickEditMode;
        layer.region.centerUv = centerUv;
        layer.region.radiusUv = { hairgpu::Clamp(m_quickEditRadiusU, 0.01f, 0.60f), hairgpu::Clamp(m_quickEditRadiusV, 0.01f, 0.60f) };
        layer.region.innerRadius = hairgpu::Clamp(m_quickEditInnerRadius, 0.0f, std::min(layer.region.radiusUv.x, layer.region.radiusUv.y) * 0.95f);
        layer.region.angleCenterRadians = std::atan2(currentUv.y - startUv.y, currentUv.x - startUv.x);
        layer.region.angleHalfExtentRadians = hairgpu::Clamp(m_quickEditAngleHalfExtentRadians, 0.05f, 3.1415926f);
        layer.region.feather = hairgpu::Clamp(m_quickEditFeather, 0.0f, 1.0f);
        layer.region.groupId = m_quickEditGroupId;
        layer.region.guidesOnly = true;
        layer.edit.lengthScale = hairgpu::Clamp(m_quickEditLengthScale, 0.6f, 1.4f);
        layer.edit.widthScale = hairgpu::Clamp(m_quickEditWidthScale, 0.5f, 1.8f);
        layer.edit.followerInfluence = hairgpu::Clamp(m_quickEditFollowerInfluence, 0.0f, 1.5f);

        const float dxPixels = static_cast<float>(currentPoint.x - m_brushStrokeStartPoint.x);
        const float dyPixels = static_cast<float>(currentPoint.y - m_brushStrokeStartPoint.y);
        const float pixelDistance = std::sqrt(dxPixels * dxPixels + dyPixels * dyPixels);
        const float signedMagnitude = hairgpu::Clamp(pixelDistance / 180.0f, 0.0f, 1.0f) * std::max(0.15f, layer.layerWeight);
        const float verticalSign = (dyPixels <= 0.0f) ? 1.0f : -1.0f;
        const Vec3 worldDelta = ComputeWorldDeltaFromScreenDelta(dxPixels, dyPixels) * std::max(0.25f, layer.layerWeight);
        const float angle = std::atan2(-dyPixels, dxPixels);
