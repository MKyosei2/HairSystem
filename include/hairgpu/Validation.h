#pragma once

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

    HairAssetValidationReport ValidateHairAsset(const HairAsset& asset);
    std::string BuildHairAssetValidationSummary(const HairAssetValidationReport& report, bool includeIssueList = false, size_t maxIssues = 12);
}
