#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "hairgpu/Types.h"

namespace hairgpu
{
    inline Vec3 SampleStrandPoint(const HairAsset& asset, const StrandRange& strand, float t)
    {
        if (strand.pointCount == 0u || strand.pointOffset >= asset.restPoints.size())
        {
            return {};
        }
        if (strand.pointCount == 1u)
        {
            return asset.restPoints[strand.pointOffset];
        }

        const float scaled = Clamp(t, 0.0f, 1.0f) * static_cast<float>(strand.pointCount - 1u);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(scaled));
        const uint32_t i1 = std::min(i0 + 1u, strand.pointCount - 1u);
        const float localT = scaled - static_cast<float>(i0);
        return Lerp(asset.restPoints[strand.pointOffset + i0],
                    asset.restPoints[strand.pointOffset + i1],
                    localT);
    }

    inline float SampleStrandWidth(const HairAsset& asset,
                                   const StrandRange& strand,
                                   float t,
                                   float fallbackWidth = 0.004f)
    {
        if (strand.pointCount == 0u || strand.pointOffset >= asset.pointWidths.size())
        {
            return fallbackWidth;
        }
        if (strand.pointCount == 1u)
        {
            return asset.pointWidths[strand.pointOffset];
        }

        const float scaled = Clamp(t, 0.0f, 1.0f) * static_cast<float>(strand.pointCount - 1u);
        const uint32_t i0 = static_cast<uint32_t>(std::floor(scaled));
        const uint32_t i1 = std::min(i0 + 1u, strand.pointCount - 1u);
        const float localT = scaled - static_cast<float>(i0);
        return Lerp(asset.pointWidths[strand.pointOffset + i0],
                    asset.pointWidths[strand.pointOffset + i1],
                    localT);
    }

    inline Vec3 SampleStrandTangent(const HairAsset& asset, const StrandRange& strand, float t)
    {
        const float eps = 1.0f / std::max<float>(static_cast<float>(std::max<uint32_t>(strand.pointCount, 2u) - 1u), 1.0f);
        const Vec3 a = SampleStrandPoint(asset, strand, Clamp(t - eps, 0.0f, 1.0f));
        const Vec3 b = SampleStrandPoint(asset, strand, Clamp(t + eps, 0.0f, 1.0f));
        return Normalize(b - a);
    }

    inline float SampleStrandCurvatureProxy(const HairAsset& asset, const StrandRange& strand, float t)
    {
        const float eps = 1.0f / std::max<float>(static_cast<float>(std::max<uint32_t>(strand.pointCount, 3u) - 1u), 1.0f);
        const Vec3 ta = SampleStrandTangent(asset, strand, Clamp(t - eps, 0.0f, 1.0f));
        const Vec3 tb = SampleStrandTangent(asset, strand, Clamp(t + eps, 0.0f, 1.0f));
        return Length(tb - ta);
    }

    inline float ComputeStrandLength(const HairAsset& asset, const StrandRange& strand)
    {
        float length = 0.0f;
        if (strand.pointCount < 2u)
        {
            return length;
        }
        for (uint32_t i = 0u; i + 1u < strand.pointCount; ++i)
        {
            const uint32_t aIndex = strand.pointOffset + i;
            const uint32_t bIndex = strand.pointOffset + i + 1u;
            if (aIndex >= asset.restPoints.size() || bIndex >= asset.restPoints.size())
            {
                break;
            }
            length += Length(asset.restPoints[bIndex] - asset.restPoints[aIndex]);
        }
        return length;
    }
}
