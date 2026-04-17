#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct GuideCache
    {
        uint32_t guideStrandCount = 0;
        uint32_t particlesPerStrand = 0;
        float frameRate = 30.0f;
        std::vector<Vec3> frames; // frame-major, guideCount * particlesPerStrand entries per frame

        [[nodiscard]] uint32_t GuidePointCountPerFrame() const
        {
            return guideStrandCount * particlesPerStrand;
        }

        [[nodiscard]] uint32_t FrameCount() const
        {
            const uint32_t pointsPerFrame = GuidePointCountPerFrame();
            return (pointsPerFrame > 0u) ? static_cast<uint32_t>(frames.size() / pointsPerFrame) : 0u;
        }

        [[nodiscard]] bool Empty() const
        {
            return guideStrandCount == 0u || particlesPerStrand == 0u || frames.empty();
        }
    };

    bool InitializeGuideCacheForAsset(const HairAsset& asset, float frameRate, GuideCache& outCache, std::string& outError);
    bool AppendGuideCacheFrame(GuideCache& cache, const HairAsset& asset, const std::vector<Vec3>& strandPoints, std::string& outError);
    bool AppendGuideCacheGuidePoints(GuideCache& cache, const std::vector<Vec3>& guidePoints, std::string& outError);
    bool EvaluateGuideCache(const GuideCache& cache, float timeSeconds, bool loop, std::vector<Vec3>& outGuidePoints);
    bool LoadGuideCacheFromFile(const std::filesystem::path& filePath, GuideCache& outCache, std::string& outError);
    bool SaveGuideCacheToFile(const std::filesystem::path& filePath, const GuideCache& cache, std::string& outError);
    std::string BuildGuideCacheSummary(const GuideCache& cache);
}
