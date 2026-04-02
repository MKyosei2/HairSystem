#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct HairCache
    {
        uint32_t strandCount = 0;
        uint32_t pointCount = 0;
        uint32_t guideStrandCount = 0;
        uint32_t followerStrandCount = 0;
        uint32_t particlesPerStrand = 0;
        float frameRate = 30.0f;
        std::vector<StrandRange> strands;
        std::vector<float> pointWidths;
        std::vector<Vec3> frames; // frame-major, pointCount entries per frame

        [[nodiscard]] uint32_t FrameCount() const
        {
            return (pointCount > 0u) ? static_cast<uint32_t>(frames.size() / pointCount) : 0u;
        }

        [[nodiscard]] bool Empty() const
        {
            return pointCount == 0u || strands.empty() || frames.empty();
        }
    };

    bool InitializeHairCacheForAsset(const HairAsset& asset, float frameRate, HairCache& outCache, std::string& outError);
    bool AppendHairCacheFrame(HairCache& cache, const HairAsset& asset, const std::vector<Vec3>& strandPoints, std::string& outError);
    bool EvaluateHairCache(const HairCache& cache, float timeSeconds, bool loop, std::vector<Vec3>& outPoints);
    bool SaveHairCacheToFile(const std::filesystem::path& filePath, const HairCache& cache, std::string& outError);
    bool LoadHairCacheFromFile(const std::filesystem::path& filePath, HairCache& outCache, std::string& outError);
    bool BuildHairAssetFrameFromCache(const HairCache& cache, float timeSeconds, bool loop, HairAsset& outAsset, std::string& outError);
    std::string BuildHairCacheSummary(const HairCache& cache);
}
