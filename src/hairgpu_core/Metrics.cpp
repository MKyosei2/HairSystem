#include "hairgpu/Metrics.h"

#include <algorithm>
#include <cmath>
#include <vector>

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


Vec3 SampleStrandTangent(const HairAsset& asset, const StrandRange& strand, float t)
{
    const float eps = 1.0f / std::max<float>(static_cast<float>(std::max<uint32_t>(strand.pointCount, 2u) - 1u), 1.0f);
    const Vec3 a = SampleStrandPoint(asset, strand, Clamp(t - eps, 0.0f, 1.0f));
    const Vec3 b = SampleStrandPoint(asset, strand, Clamp(t + eps, 0.0f, 1.0f));
    return Normalize(b - a);
}

float SampleStrandCurvatureProxy(const HairAsset& asset, const StrandRange& strand, float t)
{
    const float eps = 1.0f / std::max<float>(static_cast<float>(std::max<uint32_t>(strand.pointCount, 3u) - 1u), 1.0f);
    const Vec3 ta = SampleStrandTangent(asset, strand, Clamp(t - eps, 0.0f, 1.0f));
    const Vec3 tb = SampleStrandTangent(asset, strand, Clamp(t + eps, 0.0f, 1.0f));
    return Length(tb - ta);
}

float ComputeGuideFamilySpread(const HairAsset& asset, uint32_t guideIndex)
{
    if (guideIndex >= asset.guideStrandCount || guideIndex >= asset.strands.size())
    {
        return 0.0f;
    }
    const StrandRange& guide = asset.strands[guideIndex];
    if (guide.pointCount == 0u)
    {
        return 0.0f;
    }
    const Vec3 guideTip = asset.restPoints[guide.pointOffset + guide.pointCount - 1u];
    double total = 0.0;
    uint32_t count = 0u;
    for (uint32_t strandIndex = asset.guideStrandCount; strandIndex < asset.strands.size(); ++strandIndex)
    {
        if (strandIndex >= asset.followData.size())
        {
            continue;
        }
        const FollowData& follow = asset.followData[strandIndex];
        if (follow.parentGuide != static_cast<int32_t>(guideIndex))
        {
            continue;
        }
        const StrandRange& follower = asset.strands[strandIndex];
        if (follower.pointCount == 0u)
        {
            continue;
        }
        const Vec3 followerTip = asset.restPoints[follower.pointOffset + follower.pointCount - 1u];
        total += Length(followerTip - guideTip);
        ++count;
    }
    return (count > 0u) ? static_cast<float>(total / static_cast<double>(count)) : 0.0f;
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



HairShapeComparisonReport CompareHairShape(const HairAsset& reference, const HairAsset& candidate, uint32_t samplesPerStrand)
{
    HairShapeComparisonReport report = {};
    const HairLodComparisonReport lodReport = CompareHairAssetsResampled(reference, candidate, samplesPerStrand);
    report.rmsPositionError = lodReport.rmsPositionError;

    const uint32_t strandPairs = std::min<uint32_t>(static_cast<uint32_t>(reference.strands.size()), static_cast<uint32_t>(candidate.strands.size()));
    if (strandPairs == 0u)
    {
        return report;
    }

    const HairAssetMetrics refMetrics = ComputeHairAssetMetrics(reference);
    const Vec3 diagonal = refMetrics.bounds.max - refMetrics.bounds.min;
    const float diagLength = std::max(Length(diagonal), 1.0e-4f);

    double silhouetteSq = 0.0;
    uint64_t silhouetteSamples = 0u;
    double rootDisp = 0.0;
    double tangentError = 0.0;
    double curvatureError = 0.0;
    uint64_t differentialSamples = 0u;
    double refFamilySpread = 0.0;
    double candFamilySpread = 0.0;
    uint32_t familySamples = 0u;
    for (uint32_t strandIndex = 0u; strandIndex < strandPairs; ++strandIndex)
    {
        const StrandRange& refStrand = reference.strands[strandIndex];
        const StrandRange& candStrand = candidate.strands[strandIndex];
        if (refStrand.pointCount > 0u && candStrand.pointCount > 0u)
        {
            rootDisp += static_cast<double>(Length(reference.restPoints[refStrand.pointOffset] - candidate.restPoints[candStrand.pointOffset]));
        }
        if (strandIndex < reference.guideStrandCount && strandIndex < candidate.guideStrandCount)
        {
            refFamilySpread += ComputeGuideFamilySpread(reference, strandIndex);
            candFamilySpread += ComputeGuideFamilySpread(candidate, strandIndex);
            ++familySamples;
        }
        for (uint32_t sampleIndex = 0u; sampleIndex < samplesPerStrand; ++sampleIndex)
        {
            const float t = (samplesPerStrand > 1u) ? static_cast<float>(sampleIndex) / static_cast<float>(samplesPerStrand - 1u) : 0.0f;
            const Vec3 a = SampleStrandPoint(reference, refStrand, t);
            const Vec3 b = SampleStrandPoint(candidate, candStrand, t);
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            silhouetteSq += static_cast<double>(dx * dx + dz * dz);
            ++silhouetteSamples;

            tangentError += Length(SampleStrandTangent(reference, refStrand, t) - SampleStrandTangent(candidate, candStrand, t));
            curvatureError += std::fabs(SampleStrandCurvatureProxy(reference, refStrand, t) - SampleStrandCurvatureProxy(candidate, candStrand, t));
            ++differentialSamples;
        }
    }
    report.silhouetteError = silhouetteSamples > 0u ? static_cast<float>(std::sqrt(silhouetteSq / static_cast<double>(silhouetteSamples))) : 0.0f;
    const float avgRootDisp = static_cast<float>(rootDisp / static_cast<double>(strandPairs));
    report.rootLocalityScore = Clamp(1.0f - avgRootDisp / (0.10f * diagLength + 1.0e-4f), 0.0f, 1.0f);
    report.averageTangentError = differentialSamples > 0u ? static_cast<float>(tangentError / static_cast<double>(differentialSamples)) : 0.0f;
    report.averageCurvatureError = differentialSamples > 0u ? static_cast<float>(curvatureError / static_cast<double>(differentialSamples)) : 0.0f;
    const float refSpread = familySamples > 0u ? static_cast<float>(refFamilySpread / static_cast<double>(familySamples)) : 0.0f;
    const float candSpread = familySamples > 0u ? static_cast<float>(candFamilySpread / static_cast<double>(familySamples)) : 0.0f;
    report.clumpAxisVariance = std::fabs(refSpread - candSpread);
    const float lengthTerm = lodReport.averageLengthDelta / (std::max(refMetrics.averageLength, 1.0e-4f) * 0.35f + 1.0e-4f);
    const float differentialPenalty = report.averageTangentError * 0.35f + report.averageCurvatureError * 0.85f + report.clumpAxisVariance * 3.0f;
    report.clumpPreservationScore = Clamp(1.0f - (lengthTerm + differentialPenalty), 0.0f, 1.0f);
    return report;
}


HairEditComparisonReport CompareHairEditRegion(const HairAsset& reference, const HairAsset& edited, uint32_t samplesPerStrand)
{
    HairEditComparisonReport report = {};
    report.shape = CompareHairShape(reference, edited, samplesPerStrand);
    const uint32_t strandPairs = std::min<uint32_t>(static_cast<uint32_t>(reference.strands.size()), static_cast<uint32_t>(edited.strands.size()));
    if (strandPairs == 0u)
    {
        return report;
    }

    std::vector<float> strandDelta(strandPairs, 0.0f);
    std::vector<float> strandTangentDelta(strandPairs, 0.0f);
    const HairAssetMetrics refMetrics = ComputeHairAssetMetrics(reference);
    const float avgLength = std::max(refMetrics.averageLength, 1.0e-4f);
    uint32_t changedCount = 0u;
    double changedDeltaSum = 0.0;
    double unchangedLeakageSum = 0.0;
    uint32_t unchangedCount = 0u;
    for (uint32_t strandIndex = 0u; strandIndex < strandPairs; ++strandIndex)
    {
        const StrandRange& refStrand = reference.strands[strandIndex];
        const StrandRange& editStrand = edited.strands[strandIndex];
        double sumSq = 0.0;
        double tangentAccum = 0.0;
        for (uint32_t sampleIndex = 0u; sampleIndex < samplesPerStrand; ++sampleIndex)
        {
            const float t = (samplesPerStrand > 1u) ? static_cast<float>(sampleIndex) / static_cast<float>(samplesPerStrand - 1u) : 0.0f;
            const Vec3 a = SampleStrandPoint(reference, refStrand, t);
            const Vec3 b = SampleStrandPoint(edited, editStrand, t);
            const float d = Length(a - b);
            sumSq += static_cast<double>(d * d);
            tangentAccum += Length(SampleStrandTangent(reference, refStrand, t) - SampleStrandTangent(edited, editStrand, t));
        }
        strandDelta[strandIndex] = static_cast<float>(std::sqrt(sumSq / static_cast<double>(samplesPerStrand)));
        strandTangentDelta[strandIndex] = static_cast<float>(tangentAccum / static_cast<double>(samplesPerStrand));
        if (strandDelta[strandIndex] > 0.01f * avgLength)
        {
            ++changedCount;
            changedDeltaSum += strandDelta[strandIndex];
        }
        else
        {
            unchangedLeakageSum += strandDelta[strandIndex];
            ++unchangedCount;
        }
    }
    report.editedStrandRatio = static_cast<float>(changedCount) / static_cast<float>(strandPairs);
    report.nonEditedLeakage = unchangedCount > 0u ? static_cast<float>(unchangedLeakageSum / static_cast<double>(unchangedCount)) : 0.0f;
    const float changedAverage = changedCount > 0u ? static_cast<float>(changedDeltaSum / static_cast<double>(changedCount)) : 0.0f;
    report.editLocalityScore = Clamp(1.0f - report.nonEditedLeakage / (changedAverage + 1.0e-4f), 0.0f, 1.0f);

    const uint32_t guidePairs = std::min<uint32_t>(reference.guideStrandCount, edited.guideStrandCount);
    double boundaryAccum = 0.0;
    double boundaryTangentAccum = 0.0;
    uint32_t boundaryCount = 0u;
    for (uint32_t i = 0u; i < guidePairs; ++i)
    {
        if (i >= reference.rootBindings.size())
        {
            continue;
        }
        const Vec2 uvi = reference.rootBindings[i].rootUv;
        for (uint32_t j = i + 1u; j < guidePairs; ++j)
        {
            if (j >= reference.rootBindings.size())
            {
                continue;
            }
            if (reference.strands[i].groupId != reference.strands[j].groupId)
            {
                continue;
            }
            const Vec2 uvj = reference.rootBindings[j].rootUv;
            const float du = uvi.x - uvj.x;
            const float dv = uvi.y - uvj.y;
            const float dist = std::sqrt(du * du + dv * dv);
            if (dist > 0.12f)
            {
                continue;
            }
            boundaryAccum += static_cast<double>(std::fabs(strandDelta[i] - strandDelta[j]));
            boundaryTangentAccum += static_cast<double>(std::fabs(strandTangentDelta[i] - strandTangentDelta[j]));
            ++boundaryCount;
        }
    }
    const float avgBoundaryJump = boundaryCount > 0u ? static_cast<float>(boundaryAccum / static_cast<double>(boundaryCount)) : 0.0f;
    report.averageBoundaryTangentJump = boundaryCount > 0u ? static_cast<float>(boundaryTangentAccum / static_cast<double>(boundaryCount)) : 0.0f;
    report.boundaryNaturalnessScore = Clamp(1.0f - (avgBoundaryJump / (0.08f * avgLength + 1.0e-4f) + report.averageBoundaryTangentJump * 0.75f), 0.0f, 1.0f);
    return report;
}


}
