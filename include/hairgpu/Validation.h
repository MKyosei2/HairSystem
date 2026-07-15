#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    enum class ValidationSeverity : uint32_t
    {
        Info = 0,
        Warning = 1,
        Error = 2,
    };

    struct ValidationIssue
    {
        ValidationSeverity severity = ValidationSeverity::Info;
        std::string message;
    };

    struct HairAssetValidationReport
    {
        bool valid = true;
        uint32_t infoCount = 0;
        uint32_t warningCount = 0;
        uint32_t errorCount = 0;
        float minSegmentLength = 0.0f;
        float averageSegmentLength = 0.0f;
        float maxSegmentLength = 0.0f;
        uint32_t zeroLengthSegmentCount = 0;
        uint32_t nonFinitePointCount = 0;
        uint32_t nonPositiveWidthCount = 0;
        uint32_t invalidRootBindingCount = 0;
        uint32_t invalidFollowReferenceCount = 0;
        std::vector<ValidationIssue> issues;
    };

    // Pre-01 support: deterministic large-scale benchmark construction.
    // "totalStrandCount" means guides + followers.
    struct HairBenchmarkAssetSettings
    {
        uint32_t totalStrandCount = 100000u;
        uint32_t particlesPerStrand = 16u;
        uint32_t guideStrandCount = 10000u;
        uint32_t followersPerGuide = 9u;
        uint32_t groupsPerBlock = 8u;
        float segmentLength = 0.035f;
        uint32_t deterministicSeed = 1u;
        bool requireExactStrandCount = true;
        bool requireUniformPointCount = true;
    };

    struct SegmentGroupValidationReport
    {
        bool valid = true;
        uint32_t infoCount = 0u;
        uint32_t warningCount = 0u;
        uint32_t errorCount = 0u;

        uint32_t expectedGroupCount = 0u;
        uint32_t actualGroupCount = 0u;
        uint32_t expectedBlockCount = 0u;
        uint32_t actualBlockCount = 0u;

        uint32_t invalidStrandRangeCount = 0u;
        uint32_t invalidBlockRangeCount = 0u;
        uint32_t overlappingBlockCount = 0u;
        uint32_t uncoveredGroupCount = 0u;
        uint32_t nonUniformPointCountStrandCount = 0u;
        uint32_t topologyHashMismatchCount = 0u;
        uint32_t zeroLengthSegmentCount = 0u;
        uint32_t nonFinitePointCount = 0u;

        uint64_t expectedTopologyHash = 0u;
        uint64_t actualTopologyHash = 0u;

        std::vector<ValidationIssue> issues;
    };

    struct RuntimeSafetyCounters
    {
        uint32_t positionNaNCount = 0u;
        uint32_t velocityNaNCount = 0u;
        uint32_t broadphaseOverflowCount = 0u;
        uint32_t collisionOverflowCount = 0u;
        uint32_t indirectOverflowCount = 0u;
        uint32_t consecutiveSlowFrameCount = 0u;
        uint32_t consecutiveFailureFrameCount = 0u;
        uint32_t automaticFallbackCount = 0u;
        uint32_t forcedStopCount = 0u;

        bool HasFatalFailure() const
        {
            return positionNaNCount > 0u ||
                   velocityNaNCount > 0u ||
                   forcedStopCount > 0u;
        }
    };

    // Non-novelty evaluation contract used to reject invalid benchmark evidence.
    // This is intentionally independent from the Segment Group algorithm itself.
    struct RuntimeMeasurementContract
    {
        bool requireReleaseBuild = true;
        bool requireX64Platform = true;
        bool requireRealMeasurement = true;
        bool requireVsyncDisabled = true;
        bool requireFixedTimeStep = true;
        bool requireReproducibilityHashes = true;
        bool requireHardwareIdentity = true;
        uint32_t requiredWidth = 1920u;
        uint32_t requiredHeight = 1080u;
        uint32_t minWarmupFrames = 600u;
        uint32_t minMeasurementFrames = 6000u;
        uint32_t minIndependentSeeds = 5u;
        float fixedTimeStepSeconds = 1.0f / 60.0f;
        float fixedTimeStepToleranceSeconds = 0.00001f;
        float frameBudgetMilliseconds = 16.67f;
    };

    struct RuntimeMeasurementObservation
    {
        std::string buildConfiguration;
        std::string platform;
        bool realMeasurement = false;
        bool vsyncEnabled = false;
        bool fixedTimeStepEnabled = false;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t warmupFrames = 0u;
        uint32_t measurementFrames = 0u;
        uint32_t independentSeedCount = 0u;
        float fixedTimeStepSeconds = 0.0f;

        std::string commitSha;
        std::string executableSha256;
        std::string shaderBundleSha256;
        std::string assetSha256;
        std::string settingsSha256;

        std::string cpuName;
        std::string gpuName;
        std::string gpuDriver;
        std::string operatingSystem;
    };

    struct RuntimeMeasurementValidationReport
    {
        bool valid = true;
        uint32_t warningCount = 0u;
        uint32_t errorCount = 0u;
        std::vector<ValidationIssue> issues;
    };

    inline void AddRuntimeMeasurementIssue(
        RuntimeMeasurementValidationReport& report,
        ValidationSeverity severity,
        const std::string& message)
    {
        report.issues.push_back({ severity, message });
        if (severity == ValidationSeverity::Error)
        {
            ++report.errorCount;
            report.valid = false;
        }
        else if (severity == ValidationSeverity::Warning)
        {
            ++report.warningCount;
        }
    }

    inline RuntimeMeasurementValidationReport ValidateRuntimeMeasurement(
        const RuntimeMeasurementObservation& observation,
        const RuntimeMeasurementContract& contract = {})
    {
        RuntimeMeasurementValidationReport report = {};

        if (contract.requireReleaseBuild && observation.buildConfiguration != "Release")
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Performance evidence must be captured from a Release build.");
        }
        if (contract.requireX64Platform && observation.platform != "x64")
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Performance evidence must be captured from the x64 target.");
        }
        if (contract.requireRealMeasurement && !observation.realMeasurement)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Build-only, pending, estimated, or synthetic FPS data cannot be used as real evidence.");
        }
        if (contract.requireVsyncDisabled && observation.vsyncEnabled)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "VSync must be disabled for benchmark capture.");
        }
        if (contract.requireFixedTimeStep && !observation.fixedTimeStepEnabled)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "A deterministic fixed time step is required.");
        }
        if (observation.width != contract.requiredWidth || observation.height != contract.requiredHeight)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Resolution does not match the benchmark contract.");
        }
        if (observation.warmupFrames < contract.minWarmupFrames)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Warm-up frame count is below the minimum.");
        }
        if (observation.measurementFrames < contract.minMeasurementFrames)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Measured frame count is below the minimum.");
        }
        if (observation.independentSeedCount < contract.minIndependentSeeds)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Independent seed coverage is below the minimum.");
        }
        if (contract.requireFixedTimeStep &&
            (!std::isfinite(observation.fixedTimeStepSeconds) ||
             std::fabs(observation.fixedTimeStepSeconds - contract.fixedTimeStepSeconds) >
                 contract.fixedTimeStepToleranceSeconds))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Fixed time step differs from the declared benchmark value.");
        }

        if (contract.requireReproducibilityHashes)
        {
            if (observation.commitSha.empty() ||
                observation.executableSha256.empty() ||
                observation.shaderBundleSha256.empty() ||
                observation.assetSha256.empty() ||
                observation.settingsSha256.empty())
            {
                AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                    "Commit, executable, shader, asset, and settings hashes are required.");
            }
        }

        if (contract.requireHardwareIdentity &&
            (observation.cpuName.empty() || observation.gpuName.empty() ||
             observation.gpuDriver.empty() || observation.operatingSystem.empty()))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "CPU, GPU, GPU driver, and operating-system identities are required.");
        }

        return report;
    }

    HairAssetValidationReport ValidateHairAsset(const HairAsset& asset);
    std::string BuildHairAssetValidationSummary(
        const HairAssetValidationReport& report,
        bool includeIssueList = false,
        size_t maxIssues = 12);

    // Rebuilds only the generic topology cache. This does not implement novelty 01.
    bool RebuildSegmentGroupTopology(
        HairAsset& asset,
        uint32_t groupsPerBlock,
        std::string* outError = nullptr);

    SegmentGroupValidationReport ValidateSegmentGroupTopology(
        const HairAsset& asset,
        uint32_t expectedParticlesPerStrand = 0u,
        uint32_t expectedTotalStrandCount = 0u);

    std::string BuildSegmentGroupValidationSummary(
        const SegmentGroupValidationReport& report,
        bool includeIssueList = false,
        size_t maxIssues = 16);

    HairAsset BuildBenchmarkHairAsset(
        const HairBenchmarkAssetSettings& settings,
        SegmentGroupValidationReport* outTopologyReport = nullptr,
        std::string* outError = nullptr);
}
