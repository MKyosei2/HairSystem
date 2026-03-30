#include "hairgpu/Metrics.h"

#include <algorithm>
#include <cmath>

#include "hairgpu/Math.h"

namespace hairgpu
{
    namespace
    {
        Vec3 SampleStrandPoint(const HairAsset& asset, const StrandRange& strand, float t)
        {
            if (strand.pointCount == 0 || strand.pointOffset >= asset.restPoints.size())
            {
                return {};
            }
            if (strand.pointCount == 1)
            {
                return asset.restPoints[strand.pointOffset];
            }
            const float scaled = Clamp(t, 0.0f, 1.0f) * static_cast<float>(strand.pointCount - 1u);
            const uint32_t i0 = static_cast<uint32_t>(std::floor(scaled));
            const uint32_t i1 = std::min(i0 + 1u, strand.pointCount - 1u);
            const float localT = scaled - static_cast<float>(i0);
            const Vec3& p0 = asset.restPoints[strand.pointOffset + i0];
            const Vec3& p1 = asset.restPoints[strand.pointOffset + i1];
            return Lerp(p0, p1, localT);
        }

        float ComputeStrandLength(const HairAsset& asset, const StrandRange& strand)
        {
            float length = 0.0f;
            if (strand.pointCount < 2u)
            {
                return length;
            }
            for (uint32_t i = 0; i + 1u < strand.pointCount; ++i)
            {
                const Vec3& a = asset.restPoints[strand.pointOffset + i];
                const Vec3& b = asset.restPoints[strand.pointOffset + i + 1u];
                length += Length(b - a);
            }
            return length;
        }
    }

    HairAssetMetrics ComputeHairAssetMetrics(const HairAsset& asset)
    {
        HairAssetMetrics metrics = {};
        metrics.strandCount = static_cast<uint32_t>(asset.strands.size());
        metrics.guideCount = asset.guideStrandCount;
        metrics.followerCount = asset.followerStrandCount;
        metrics.pointCount = static_cast<uint32_t>(asset.restPoints.size());
        metrics.particlesPerStrand = asset.particlesPerStrand;
        if (!asset.restPoints.empty())
        {
            metrics.bounds.min = asset.restPoints.front();
            metrics.bounds.max = asset.restPoints.front();
        }

        float totalWidth = 0.0f;
        for (size_t i = 0; i < asset.pointWidths.size(); ++i)
        {
            totalWidth += asset.pointWidths[i];
        }
        metrics.averageWidth = asset.pointWidths.empty() ? 0.0f : (totalWidth / static_cast<float>(asset.pointWidths.size()));

        for (const Vec3& p : asset.restPoints)
        {
            metrics.bounds.min.x = std::min(metrics.bounds.min.x, p.x);
            metrics.bounds.min.y = std::min(metrics.bounds.min.y, p.y);
            metrics.bounds.min.z = std::min(metrics.bounds.min.z, p.z);
            metrics.bounds.max.x = std::max(metrics.bounds.max.x, p.x);
            metrics.bounds.max.y = std::max(metrics.bounds.max.y, p.y);
            metrics.bounds.max.z = std::max(metrics.bounds.max.z, p.z);
        }

        for (const StrandRange& strand : asset.strands)
        {
            const float length = ComputeStrandLength(asset, strand);
            metrics.totalLength += length;
            metrics.maxLength = std::max(metrics.maxLength, length);
        }
        metrics.averageLength = asset.strands.empty() ? 0.0f : (metrics.totalLength / static_cast<float>(asset.strands.size()));
        metrics.averageFollowerPerGuide = (asset.guideStrandCount > 0u)
            ? (static_cast<float>(asset.followerStrandCount) / static_cast<float>(asset.guideStrandCount))
            : 0.0f;
        return metrics;
    }

    HairLodComparisonReport CompareHairAssetsResampled(const HairAsset& reference, const HairAsset& candidate, uint32_t samplesPerStrand)
    {
        HairLodComparisonReport report = {};
        samplesPerStrand = std::max<uint32_t>(2u, samplesPerStrand);
        report.strandRetentionRatio = reference.strands.empty() ? 1.0f : (static_cast<float>(candidate.strands.size()) / static_cast<float>(reference.strands.size()));
        report.particleRetentionRatio = reference.restPoints.empty() ? 1.0f : (static_cast<float>(candidate.restPoints.size()) / static_cast<float>(reference.restPoints.size()));

        const uint32_t strandPairs = std::min<uint32_t>(static_cast<uint32_t>(reference.strands.size()), static_cast<uint32_t>(candidate.strands.size()));
        if (strandPairs == 0u)
        {
            return report;
        }

        double sumSq = 0.0;
        uint64_t sampleCount = 0u;
        for (uint32_t strandIndex = 0; strandIndex < strandPairs; ++strandIndex)
        {
            const StrandRange& refStrand = reference.strands[strandIndex];
            const StrandRange& lodStrand = candidate.strands[strandIndex];
            const float refLength = ComputeStrandLength(reference, refStrand);
            const float lodLength = ComputeStrandLength(candidate, lodStrand);
            const float lengthDelta = std::fabs(refLength - lodLength);
            report.averageLengthDelta += lengthDelta;
            report.maxLengthDelta = std::max(report.maxLengthDelta, lengthDelta);

            for (uint32_t sampleIndex = 0; sampleIndex < samplesPerStrand; ++sampleIndex)
            {
                const float t = (samplesPerStrand > 1u) ? static_cast<float>(sampleIndex) / static_cast<float>(samplesPerStrand - 1u) : 0.0f;
                const Vec3 refPos = SampleStrandPoint(reference, refStrand, t);
                const Vec3 lodPos = SampleStrandPoint(candidate, lodStrand, t);
                const float d = Length(refPos - lodPos);
                sumSq += static_cast<double>(d) * static_cast<double>(d);
                report.maxPositionError = std::max(report.maxPositionError, d);
                ++sampleCount;
            }
        }

        report.averageLengthDelta /= static_cast<float>(strandPairs);
        report.rmsPositionError = (sampleCount > 0u) ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(sampleCount))) : 0.0f;
        return report;
    }
}
