#pragma once

#include <cstdint>

namespace hairgpu
{
    inline constexpr const char* kThesisEditEvaluationSchema =
        "hairgpu-thesis-edit-eval-v2";

    struct ThesisEditEpisodeSummary
    {
        uint64_t episodeId = 0u;
        uint32_t frameIndex = 0u;
        uint64_t physicalEditHash = 0ull;
        uint32_t requestedGuideCount = 0u;
        float requestedTipOffsetMm = 0.0f;
        uint32_t changedPointCount = 0u;
        uint32_t touchedGuideCount = 0u;
        uint32_t touchedFollowerCount = 0u;
        uint32_t directGroupCount = 0u;
        uint32_t directBlockCount = 0u;
        uint32_t totalGroupCount = 0u;
        uint32_t totalBlockCount = 0u;
        uint32_t localInvalidationQueued = 0u;
        uint32_t forcedFallbackRequested = 0u;
        double directGroupRatio = 0.0;
        double directBlockRatio = 0.0;
    };

    inline double ThesisSafeRatio(uint64_t numerator, uint64_t denominator)
    {
        return denominator == 0u ? 0.0 :
            static_cast<double>(numerator) / static_cast<double>(denominator);
    }
}
