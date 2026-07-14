#pragma once

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
