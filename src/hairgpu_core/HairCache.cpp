#include "hairgpu/HairCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <sstream>

#include "hairgpu/FrameSampling.h"

#include "hairgpu/BinaryIO.h"
namespace hairgpu
{
    namespace
    {
        constexpr std::array<char, 8> kHairCacheMagic = { 'H','H','A','I','R','1','\0','\0' };
        constexpr uint32_t kHairCacheVersion = 1u;

    }

    bool InitializeHairCacheForAsset(const HairAsset& asset, float frameRate, HairCache& outCache, std::string& outError)
    {
        if (asset.strands.empty() || asset.restPoints.empty())
        {
            outError = "Asset has no strand data.";
            return false;
        }

        outCache = {};
        outCache.strandCount = static_cast<uint32_t>(asset.strands.size());
        outCache.pointCount = static_cast<uint32_t>(asset.restPoints.size());
        outCache.guideStrandCount = asset.guideStrandCount;
        outCache.followerStrandCount = asset.followerStrandCount;
        outCache.particlesPerStrand = asset.particlesPerStrand;
        outCache.frameRate = std::max(frameRate, 1.0f);
        outCache.strands = asset.strands;
        outCache.pointWidths = asset.pointWidths;
        outError.clear();
        return true;
    }

    bool AppendHairCacheFrame(HairCache& cache, const HairAsset& asset, const std::vector<Vec3>& strandPoints, std::string& outError)
    {
        if (cache.pointCount == 0u)
        {
            if (!InitializeHairCacheForAsset(asset, 30.0f, cache, outError))
            {
                return false;
            }
        }

        if (cache.strandCount != asset.strands.size() || cache.pointCount != asset.restPoints.size())
        {
            outError = "Hair cache layout does not match asset layout.";
            return false;
        }
        if (strandPoints.size() < cache.pointCount)
        {
            outError = "Input strand points are smaller than the cache layout.";
            return false;
        }

        cache.frames.insert(cache.frames.end(), strandPoints.begin(), strandPoints.begin() + cache.pointCount);
        outError.clear();
        return true;
    }

    bool EvaluateHairCache(const HairCache& cache, float timeSeconds, bool loop, std::vector<Vec3>& outPoints)
    {
        const uint32_t pointCount = cache.pointCount;
        const uint32_t frameCount = cache.FrameCount();
        if (pointCount == 0u || frameCount == 0u || cache.frameRate <= 0.0f)
        {
            outPoints.clear();
            return false;
        }

        FrameSample sample = {};
        if (!ComputeFrameSample(frameCount, cache.frameRate, timeSeconds, loop, sample))
        {
            outPoints.clear();
            return false;
        }

        outPoints.resize(pointCount);
        const size_t base0 = static_cast<size_t>(sample.frame0) * pointCount;
        const size_t base1 = static_cast<size_t>(sample.frame1) * pointCount;
        for (uint32_t i = 0u; i < pointCount; ++i)
        {
            outPoints[i] = Lerp(cache.frames[base0 + i], cache.frames[base1 + i], sample.t);
        }
        return true;
    }

    bool SaveHairCacheToFile(const std::filesystem::path& filePath, const HairCache& cache, std::string& outError)
    {
        if (cache.Empty())
        {
            outError = "Hair cache is empty.";
            return false;
        }

        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open output hair cache file.";
            return false;
        }

        const uint32_t frameCount = cache.FrameCount();
        out.write(kHairCacheMagic.data(), static_cast<std::streamsize>(kHairCacheMagic.size()));
        if (!WriteValue(out, kHairCacheVersion) ||
            !WriteValue(out, cache.strandCount) ||
            !WriteValue(out, cache.pointCount) ||
            !WriteValue(out, cache.guideStrandCount) ||
            !WriteValue(out, cache.followerStrandCount) ||
            !WriteValue(out, cache.particlesPerStrand) ||
            !WriteValue(out, cache.frameRate) ||
            !WriteValue(out, frameCount))
        {
            outError = "Failed to write hair cache header.";
            return false;
        }

        const uint32_t storedStrandCount = static_cast<uint32_t>(cache.strands.size());
        const uint32_t storedWidthCount = static_cast<uint32_t>(cache.pointWidths.size());
        if (!WriteValue(out, storedStrandCount) || !WriteValue(out, storedWidthCount))
        {
            outError = "Failed to write hair cache layout counts.";
            return false;
        }

        if (storedStrandCount > 0u)
        {
            out.write(reinterpret_cast<const char*>(cache.strands.data()), static_cast<std::streamsize>(cache.strands.size() * sizeof(StrandRange)));
        }
        if (storedWidthCount > 0u)
        {
            out.write(reinterpret_cast<const char*>(cache.pointWidths.data()), static_cast<std::streamsize>(cache.pointWidths.size() * sizeof(float)));
        }
        out.write(reinterpret_cast<const char*>(cache.frames.data()), static_cast<std::streamsize>(cache.frames.size() * sizeof(Vec3)));
        if (!out)
        {
            outError = "Failed to write hair cache payload.";
            return false;
        }

        outError.clear();
        return true;
    }

    bool LoadHairCacheFromFile(const std::filesystem::path& filePath, HairCache& outCache, std::string& outError)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open hair cache file.";
            return false;
        }

        std::array<char, 8> magic = {};
        in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!in || magic != kHairCacheMagic)
        {
            outError = "Invalid hair cache magic.";
            return false;
        }

        uint32_t version = 0u;
        HairCache cache = {};
        uint32_t frameCount = 0u;
        uint32_t storedStrandCount = 0u;
        uint32_t storedWidthCount = 0u;
        if (!ReadValue(in, version) ||
            version != kHairCacheVersion ||
            !ReadValue(in, cache.strandCount) ||
            !ReadValue(in, cache.pointCount) ||
            !ReadValue(in, cache.guideStrandCount) ||
            !ReadValue(in, cache.followerStrandCount) ||
            !ReadValue(in, cache.particlesPerStrand) ||
            !ReadValue(in, cache.frameRate) ||
            !ReadValue(in, frameCount) ||
            !ReadValue(in, storedStrandCount) ||
            !ReadValue(in, storedWidthCount))
        {
            outError = "Hair cache header is truncated or unsupported.";
            return false;
        }

        if (cache.pointCount == 0u || frameCount == 0u || storedStrandCount == 0u)
        {
            outError = "Hair cache has an invalid layout.";
            return false;
        }

        cache.strands.resize(storedStrandCount);
        cache.pointWidths.resize(storedWidthCount);
        if (!cache.strands.empty())
        {
            in.read(reinterpret_cast<char*>(cache.strands.data()), static_cast<std::streamsize>(cache.strands.size() * sizeof(StrandRange)));
        }
        if (!cache.pointWidths.empty())
        {
            in.read(reinterpret_cast<char*>(cache.pointWidths.data()), static_cast<std::streamsize>(cache.pointWidths.size() * sizeof(float)));
        }
        if (!in)
        {
            outError = "Hair cache layout payload is truncated.";
            return false;
        }

        const uint64_t totalPoints = static_cast<uint64_t>(cache.pointCount) * frameCount;
        if (totalPoints > (1ull << 31))
        {
            outError = "Hair cache is too large.";
            return false;
        }
        cache.frames.resize(static_cast<size_t>(totalPoints));
        in.read(reinterpret_cast<char*>(cache.frames.data()), static_cast<std::streamsize>(cache.frames.size() * sizeof(Vec3)));
        if (!in)
        {
            outError = "Hair cache frame payload is truncated.";
            return false;
        }

        outCache = std::move(cache);
        outError.clear();
        return true;
    }

    bool BuildHairAssetFrameFromCache(const HairCache& cache, float timeSeconds, bool loop, HairAsset& outAsset, std::string& outError)
    {
        std::vector<Vec3> points;
        if (!EvaluateHairCache(cache, timeSeconds, loop, points))
        {
            outError = "Failed to evaluate hair cache frame.";
            return false;
        }

        HairAsset asset = {};
        asset.strands = cache.strands;
        asset.restPoints = std::move(points);
        asset.pointWidths = cache.pointWidths;
        asset.guideStrandCount = cache.guideStrandCount;
        asset.followerStrandCount = cache.followerStrandCount;
        asset.particlesPerStrand = cache.particlesPerStrand;
        outAsset = std::move(asset);
        outError.clear();
        return true;
    }

    std::string BuildHairCacheSummary(const HairCache& cache)
    {
        std::ostringstream out;
        out << "hairCache frames=" << cache.FrameCount()
            << " strands=" << cache.strandCount
            << " points=" << cache.pointCount
            << " guides=" << cache.guideStrandCount
            << " followers=" << cache.followerStrandCount
            << " pps=" << cache.particlesPerStrand
            << " fps=" << cache.frameRate;
        return out.str();
    }
}
