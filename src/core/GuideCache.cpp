#include "hairgpu/GuideCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

namespace hairgpu
{
    namespace
    {
        constexpr std::array<char, 8> kGuideCacheMagic = { 'H','G','U','I','D','E','1','\0' };
        constexpr uint32_t kGuideCacheVersion = 1u;

        template <typename T>
        bool WriteValue(std::ofstream& out, const T& value)
        {
            out.write(reinterpret_cast<const char*>(&value), sizeof(T));
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadValue(std::ifstream& in, T& value)
        {
            in.read(reinterpret_cast<char*>(&value), sizeof(T));
            return static_cast<bool>(in);
        }
    }

    bool InitializeGuideCacheForAsset(const HairAsset& asset, float frameRate, GuideCache& outCache, std::string& outError)
    {
        if (asset.guideStrandCount == 0u || asset.particlesPerStrand == 0u)
        {
            outError = "Asset has no guide strands.";
            return false;
        }
        outCache = {};
        outCache.guideStrandCount = asset.guideStrandCount;
        outCache.particlesPerStrand = asset.particlesPerStrand;
        outCache.frameRate = std::max(frameRate, 1.0f);
        return true;
    }

    bool AppendGuideCacheFrame(GuideCache& cache, const HairAsset& asset, const std::vector<Vec3>& strandPoints, std::string& outError)
    {
        if (cache.guideStrandCount == 0u || cache.particlesPerStrand == 0u)
        {
            if (!InitializeGuideCacheForAsset(asset, 30.0f, cache, outError))
            {
                return false;
            }
        }
        if (cache.guideStrandCount != asset.guideStrandCount || cache.particlesPerStrand != asset.particlesPerStrand)
        {
            outError = "Guide cache layout does not match asset guide layout.";
            return false;
        }
        const uint32_t requiredPoints = asset.guideStrandCount * asset.particlesPerStrand;
        if (strandPoints.size() < requiredPoints)
        {
            outError = "Input strand point array is too small for the guide layout.";
            return false;
        }
        cache.frames.insert(cache.frames.end(), strandPoints.begin(), strandPoints.begin() + requiredPoints);
        return true;
    }

    bool AppendGuideCacheGuidePoints(GuideCache& cache, const std::vector<Vec3>& guidePoints, std::string& outError)
    {
        const uint32_t requiredPoints = cache.GuidePointCountPerFrame();
        if (requiredPoints == 0u)
        {
            outError = "Guide cache layout is not initialized.";
            return false;
        }
        if (guidePoints.size() != requiredPoints)
        {
            outError = "Guide point frame does not match the cache layout.";
            return false;
        }
        cache.frames.insert(cache.frames.end(), guidePoints.begin(), guidePoints.end());
        return true;
    }

    bool EvaluateGuideCache(const GuideCache& cache, float timeSeconds, bool loop, std::vector<Vec3>& outGuidePoints)
    {
        const uint32_t pointsPerFrame = cache.GuidePointCountPerFrame();
        const uint32_t frameCount = cache.FrameCount();
        if (pointsPerFrame == 0u || frameCount == 0u || cache.frameRate <= 0.0f)
        {
            outGuidePoints.clear();
            return false;
        }

        const float frameTime = 1.0f / cache.frameRate;
        const float totalDuration = frameTime * static_cast<float>(frameCount);
        float sampleTime = std::max(timeSeconds, 0.0f);
        if (loop && totalDuration > 1.0e-6f)
        {
            sampleTime = std::fmod(sampleTime, totalDuration);
            if (sampleTime < 0.0f)
            {
                sampleTime += totalDuration;
            }
        }
        else
        {
            sampleTime = std::min(sampleTime, std::max(totalDuration - frameTime, 0.0f));
        }

        const float frameIndexF = sampleTime / frameTime;
        const uint32_t frame0 = std::min<uint32_t>(static_cast<uint32_t>(std::floor(frameIndexF)), frameCount - 1u);
        const uint32_t frame1 = loop ? ((frame0 + 1u) % frameCount) : std::min<uint32_t>(frame0 + 1u, frameCount - 1u);
        const float t = std::clamp(frameIndexF - std::floor(frameIndexF), 0.0f, 1.0f);

        outGuidePoints.resize(pointsPerFrame);
        const size_t base0 = static_cast<size_t>(frame0) * pointsPerFrame;
        const size_t base1 = static_cast<size_t>(frame1) * pointsPerFrame;
        for (uint32_t i = 0; i < pointsPerFrame; ++i)
        {
            outGuidePoints[i] = Lerp(cache.frames[base0 + i], cache.frames[base1 + i], t);
        }
        return true;
    }

    bool LoadGuideCacheFromFile(const std::filesystem::path& filePath, GuideCache& outCache, std::string& outError)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open guide cache file.";
            return false;
        }

        std::array<char, 8> magic = {};
        in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!in || magic != kGuideCacheMagic)
        {
            outError = "Invalid guide cache magic.";
            return false;
        }

        uint32_t version = 0u;
        if (!ReadValue(in, version) || version != kGuideCacheVersion)
        {
            outError = "Unsupported guide cache version.";
            return false;
        }

        GuideCache cache = {};
        uint32_t frameCount = 0u;
        if (!ReadValue(in, cache.guideStrandCount) ||
            !ReadValue(in, cache.particlesPerStrand) ||
            !ReadValue(in, cache.frameRate) ||
            !ReadValue(in, frameCount))
        {
            outError = "Guide cache header is truncated.";
            return false;
        }
        if (cache.guideStrandCount == 0u || cache.particlesPerStrand == 0u || frameCount == 0u)
        {
            outError = "Guide cache has an invalid layout.";
            return false;
        }

        const uint64_t totalPoints = static_cast<uint64_t>(cache.guideStrandCount) * cache.particlesPerStrand * frameCount;
        if (totalPoints > (1ull << 30))
        {
            outError = "Guide cache is too large.";
            return false;
        }
        cache.frames.resize(static_cast<size_t>(totalPoints));
        in.read(reinterpret_cast<char*>(cache.frames.data()), static_cast<std::streamsize>(cache.frames.size() * sizeof(Vec3)));
        if (!in)
        {
            outError = "Guide cache frame data is truncated.";
            return false;
        }

        outCache = std::move(cache);
        return true;
    }

    bool SaveGuideCacheToFile(const std::filesystem::path& filePath, const GuideCache& cache, std::string& outError)
    {
        if (cache.Empty())
        {
            outError = "Guide cache is empty.";
            return false;
        }

        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open output guide cache file.";
            return false;
        }

        const uint32_t frameCount = cache.FrameCount();
        out.write(kGuideCacheMagic.data(), static_cast<std::streamsize>(kGuideCacheMagic.size()));
        if (!WriteValue(out, kGuideCacheVersion) ||
            !WriteValue(out, cache.guideStrandCount) ||
            !WriteValue(out, cache.particlesPerStrand) ||
            !WriteValue(out, cache.frameRate) ||
            !WriteValue(out, frameCount))
        {
            outError = "Failed to write guide cache header.";
            return false;
        }

        out.write(reinterpret_cast<const char*>(cache.frames.data()), static_cast<std::streamsize>(cache.frames.size() * sizeof(Vec3)));
        if (!out)
        {
            outError = "Failed to write guide cache frame data.";
            return false;
        }
        return true;
    }

    std::string BuildGuideCacheSummary(const GuideCache& cache)
    {
        std::ostringstream out;
        out << "guideCache frames=" << cache.FrameCount()
            << " guides=" << cache.guideStrandCount
            << " pps=" << cache.particlesPerStrand
            << " fps=" << cache.frameRate;
        return out.str();
    }
}
