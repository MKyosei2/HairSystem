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

    // Deterministic benchmark construction. This remains outside novelty 01.
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
            return positionNaNCount > 0u || velocityNaNCount > 0u || forcedStopCount > 0u;
        }
    };

    // Evidence-only contract. This intentionally does not change Segment Group
    // representation, state transitions, solver behavior, or ownership semantics.
    struct RuntimeMeasurementContract
    {
        bool requireReleaseBuild = true;
        bool requireX64Platform = true;
        bool requireRealMeasurement = true;
        bool requireVsyncDisabled = true;
        bool requireFixedTimeStep = true;
        bool requireReproducibilityHashes = true;
        bool requireExactSha256Format = true;
        bool requireSourceCommitMatch = true;
        bool requireHardwareIdentity = true;
        bool requireUniqueCaseIdentity = true;
        bool requireScenarioTimelineHash = true;
        bool requireAssetManifestHash = true;
        bool requireFrozenAsset = true;
        bool requirePairedBaseline = true;
        bool requireExactDisplayedStrandCount = true;
        bool requireFiniteFrameStatistics = true;
        bool requirePositiveFrameStatistics = true;
        bool requireOrderedTailPercentiles = true;
        bool requireMethodSettingsAcknowledgement = true;
        bool requireDeterministicReplay = true;
        bool requireNoFatalRuntimeFailure = true;
        bool requireThermalTelemetry = false;
        bool treatFrameBudgetOverrunAsError = false;
        uint32_t requiredWidth = 1920u;
        uint32_t requiredHeight = 1080u;
        uint32_t requiredDisplayedStrandCount = 100000u;
        uint32_t minWarmupFrames = 600u;
        uint32_t minMeasurementFrames = 6000u;
        uint32_t minIndependentSeeds = 5u;
        uint32_t minMatchedBaselineSeeds = 5u;
        float fixedTimeStepSeconds = 1.0f / 60.0f;
        float fixedTimeStepToleranceSeconds = 0.00001f;
        float frameBudgetMilliseconds = 16.67f;
        float maxFrameBudgetOverrunRate = 0.05f;
        float maxGpuTemperatureSpreadCelsius = 12.0f;
    };

    struct RuntimeMeasurementObservation
    {
        std::string caseId;
        std::string methodCode;
        std::string scenarioCode;
        std::string baselineMethodCode;
        std::string hardwareProfile;
        std::string buildConfiguration;
        std::string platform;
        bool realMeasurement = false;
        bool vsyncEnabled = false;
        bool fixedTimeStepEnabled = false;
        bool pairedBaselineAvailable = false;
        bool assetFrozen = false;
        bool methodSettingsAcknowledged = false;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t displayedStrandCount = 0u;
        uint32_t warmupFrames = 0u;
        uint32_t measurementFrames = 0u;
        uint32_t acceptedFrameSampleCount = 0u;
        uint32_t invalidFrameSampleCount = 0u;
        uint32_t independentSeedCount = 0u;
        uint32_t matchedBaselineSeedCount = 0u;
        uint32_t determinismMismatchCount = 0u;
        uint32_t fatalRuntimeFailureCount = 0u;
        float fixedTimeStepSeconds = 0.0f;
        float averageFrameMilliseconds = 0.0f;
        float p95FrameMilliseconds = 0.0f;
        float p99FrameMilliseconds = 0.0f;
        float p999FrameMilliseconds = 0.0f;
        float frameBudgetOverrunRate = 1.0f;
        float minimumGpuTemperatureCelsius = 0.0f;
        float maximumGpuTemperatureCelsius = 0.0f;

        std::string expectedCommitSha;
        std::string commitSha;
        std::string executableSha256;
        std::string shaderBundleSha256;
        std::string assetSha256;
        std::string settingsSha256;
        std::string appliedMethodSettingsSha256;
        std::string scenarioTimelineSha256;
        std::string assetManifestSha256;

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

    inline bool IsHexCharacter(char value)
    {
        return (value >= '0' && value <= '9') ||
               (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
    }

    inline bool IsExactHexDigest(const std::string& value, size_t expectedLength)
    {
        if (value.size() != expectedLength)
        {
            return false;
        }
        for (char character : value)
        {
            if (!IsHexCharacter(character))
            {
                return false;
            }
        }
        return true;
    }

    inline bool HasRequiredHash(const std::string& value, bool requireExactSha256Format = true)
    {
        if (requireExactSha256Format)
        {
            return IsExactHexDigest(value, 64u);
        }
        return value.size() >= 32u && value.find("REPLACE") == std::string::npos &&
               value.find("PENDING") == std::string::npos && value.find("UNKNOWN") == std::string::npos &&
               value.find("SYNTHETIC") == std::string::npos && value.find("TBD") == std::string::npos;
    }

    inline bool HasRequiredCommitSha(const std::string& value)
    {
        return IsExactHexDigest(value, 40u);
    }

    inline RuntimeMeasurementValidationReport ValidateRuntimeMeasurement(
        const RuntimeMeasurementObservation& observation,
        const RuntimeMeasurementContract& contract = {})
    {
        RuntimeMeasurementValidationReport report = {};

        if (contract.requireUniqueCaseIdentity &&
            (observation.caseId.empty() || observation.methodCode.empty() || observation.scenarioCode.empty()))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "A unique case id, method code, and scenario code are required.");
        }
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
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error, "VSync must be disabled.");
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
        if (contract.requireExactDisplayedStrandCount &&
            observation.displayedStrandCount != contract.requiredDisplayedStrandCount)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Displayed strand count does not match the 100,000-strand contract.");
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
        if (observation.acceptedFrameSampleCount < observation.measurementFrames)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Accepted frame sample count is below the declared measurement frame count.");
        }
        if (observation.invalidFrameSampleCount > 0u)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Frame telemetry contains missing, non-positive, or non-finite samples.");
        }
        if (observation.independentSeedCount < contract.minIndependentSeeds)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Independent seed coverage is below the minimum.");
        }
        if (contract.requirePairedBaseline &&
            (observation.baselineMethodCode.empty() || !observation.pairedBaselineAvailable ||
             observation.matchedBaselineSeedCount < contract.minMatchedBaselineSeeds))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "A matched baseline with the same scenario, seed, asset, and hardware is required.");
        }
        if (contract.requireFixedTimeStep &&
            (!std::isfinite(observation.fixedTimeStepSeconds) ||
             std::fabs(observation.fixedTimeStepSeconds - contract.fixedTimeStepSeconds) >
                 contract.fixedTimeStepToleranceSeconds))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Fixed time step differs from the declared benchmark value.");
        }

        const bool finiteStatistics =
            std::isfinite(observation.averageFrameMilliseconds) &&
            std::isfinite(observation.p95FrameMilliseconds) &&
            std::isfinite(observation.p99FrameMilliseconds) &&
            std::isfinite(observation.p999FrameMilliseconds) &&
            std::isfinite(observation.frameBudgetOverrunRate);
        if (contract.requireFiniteFrameStatistics && !finiteStatistics)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Frame-time statistics must be finite.");
        }
        if (contract.requirePositiveFrameStatistics && finiteStatistics &&
            (observation.averageFrameMilliseconds <= 0.0f ||
             observation.p95FrameMilliseconds <= 0.0f ||
             observation.p99FrameMilliseconds <= 0.0f ||
             observation.p999FrameMilliseconds <= 0.0f ||
             observation.frameBudgetOverrunRate < 0.0f ||
             observation.frameBudgetOverrunRate > 1.0f))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Frame-time statistics must be positive and rates must be in [0, 1].");
        }
        if (contract.requireOrderedTailPercentiles && finiteStatistics &&
            (observation.p95FrameMilliseconds > observation.p99FrameMilliseconds ||
             observation.p99FrameMilliseconds > observation.p999FrameMilliseconds))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Tail percentiles are not monotonically ordered.");
        }
        if (observation.frameBudgetOverrunRate > contract.maxFrameBudgetOverrunRate)
        {
            AddRuntimeMeasurementIssue(
                report,
                contract.treatFrameBudgetOverrunAsError ? ValidationSeverity::Error : ValidationSeverity::Warning,
                "Frame-budget overrun rate exceeds the strong 60 FPS target.");
        }

        if (contract.requireReproducibilityHashes &&
            (!HasRequiredCommitSha(observation.commitSha) ||
             !HasRequiredHash(observation.executableSha256, contract.requireExactSha256Format) ||
             !HasRequiredHash(observation.shaderBundleSha256, contract.requireExactSha256Format) ||
             !HasRequiredHash(observation.assetSha256, contract.requireExactSha256Format) ||
             !HasRequiredHash(observation.settingsSha256, contract.requireExactSha256Format)))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Commit, executable, shader, asset, and settings hashes are required and must use exact formats.");
        }
        if (contract.requireSourceCommitMatch &&
            (!HasRequiredCommitSha(observation.expectedCommitSha) || observation.commitSha != observation.expectedCommitSha))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Telemetry commit does not match the clean source commit declared by the suite.");
        }
        if (contract.requireMethodSettingsAcknowledgement &&
            (!observation.methodSettingsAcknowledged ||
             !HasRequiredHash(observation.appliedMethodSettingsSha256, contract.requireExactSha256Format)))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Runtime method settings were not acknowledged with an exact SHA-256.");
        }
        if (contract.requireScenarioTimelineHash &&
            !HasRequiredHash(observation.scenarioTimelineSha256, contract.requireExactSha256Format))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Scenario timeline SHA-256 is required.");
        }
        if (contract.requireAssetManifestHash &&
            !HasRequiredHash(observation.assetManifestSha256, contract.requireExactSha256Format))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Asset manifest SHA-256 is required.");
        }
        if (contract.requireFrozenAsset && !observation.assetFrozen)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "The benchmark asset must be frozen before paper evidence is captured.");
        }
        if (contract.requireHardwareIdentity &&
            (observation.cpuName.empty() || observation.gpuName.empty() ||
             observation.gpuDriver.empty() || observation.operatingSystem.empty() ||
             observation.hardwareProfile.empty()))
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "CPU, GPU, GPU driver, operating system, and hardware profile are required.");
        }
        if (contract.requireDeterministicReplay && observation.determinismMismatchCount > 0u)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Identical-seed deterministic replay produced a different canonical result hash.");
        }
        if (contract.requireNoFatalRuntimeFailure && observation.fatalRuntimeFailureCount > 0u)
        {
            AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                "Fatal runtime failures were recorded during the measurement window.");
        }
        if (contract.requireThermalTelemetry)
        {
            const float spread = observation.maximumGpuTemperatureCelsius - observation.minimumGpuTemperatureCelsius;
            if (!std::isfinite(spread) || spread < 0.0f || spread > contract.maxGpuTemperatureSpreadCelsius)
            {
                AddRuntimeMeasurementIssue(report, ValidationSeverity::Error,
                    "GPU temperature spread is missing or exceeds the thermal-stability contract.");
            }
        }
        return report;
    }

    HairAssetValidationReport ValidateHairAsset(const HairAsset& asset);
    std::string BuildHairAssetValidationSummary(
        const HairAssetValidationReport& report,
        bool includeIssueList = false,
        size_t maxIssues = 12);

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
