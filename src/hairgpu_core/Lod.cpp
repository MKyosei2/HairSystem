#include "hairgpu/Lod.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

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

            const float scaled = Clamp(t, 0.0f, 1.0f) * static_cast<float>(strand.pointCount - 1);
            const uint32_t i0 = static_cast<uint32_t>(std::floor(scaled));
            const uint32_t i1 = std::min(i0 + 1u, strand.pointCount - 1u);
            const float localT = scaled - static_cast<float>(i0);
            return Lerp(asset.restPoints[strand.pointOffset + i0], asset.restPoints[strand.pointOffset + i1], localT);
        }

        float SampleStrandWidth(const HairAsset& asset, const StrandRange& strand, float t)
        {
            if (strand.pointCount == 0 || strand.pointOffset >= asset.pointWidths.size())
            {
                return 0.004f;
            }
            if (strand.pointCount == 1)
            {
                return asset.pointWidths[strand.pointOffset];
            }

            const float scaled = Clamp(t, 0.0f, 1.0f) * static_cast<float>(strand.pointCount - 1);
            const uint32_t i0 = static_cast<uint32_t>(std::floor(scaled));
            const uint32_t i1 = std::min(i0 + 1u, strand.pointCount - 1u);
            const float localT = scaled - static_cast<float>(i0);
            return Lerp(asset.pointWidths[strand.pointOffset + i0], asset.pointWidths[strand.pointOffset + i1], localT);
        }

        std::vector<uint32_t> EvenlySampleIndices(uint32_t count, uint32_t keepCount)
        {
            std::vector<uint32_t> indices;
            if (count == 0 || keepCount == 0)
            {
                return indices;
            }
            keepCount = std::min(keepCount, count);
            indices.reserve(keepCount);
            if (keepCount == 1)
            {
                indices.push_back(0u);
                return indices;
            }
            for (uint32_t i = 0; i < keepCount; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(keepCount - 1u);
                const uint32_t idx = static_cast<uint32_t>(std::round(t * static_cast<float>(count - 1u)));
                if (indices.empty() || indices.back() != idx)
                {
                    indices.push_back(idx);
                }
            }
            if (indices.size() < keepCount)
            {
                for (uint32_t idx = 0; idx < count && indices.size() < keepCount; ++idx)
                {
                    if (std::find(indices.begin(), indices.end(), idx) == indices.end())
                    {
                        indices.push_back(idx);
                    }
                }
                std::sort(indices.begin(), indices.end());
            }
            return indices;
        }

        void AppendResampledStrand(HairAsset& dst, const HairAsset& src, uint32_t srcStrandIndex, uint32_t newPointCount, uint32_t groupId, uint32_t flags, int32_t parentGuide)
        {
            const StrandRange& srcStrand = src.strands[srcStrandIndex];
            StrandRange range = {};
            range.pointOffset = static_cast<uint32_t>(dst.restPoints.size());
            range.pointCount = newPointCount;
            range.groupId = groupId;
            range.flags = flags;
            dst.strands.push_back(range);

            StrandMaterial material = (srcStrandIndex < src.materials.size()) ? src.materials[srcStrandIndex] : StrandMaterial{};
            dst.materials.push_back(material);
            RootBinding binding = (srcStrandIndex < src.rootBindings.size()) ? src.rootBindings[srcStrandIndex] : RootBinding{};
            dst.rootBindings.push_back(binding);
            FollowData follow = (srcStrandIndex < src.followData.size()) ? src.followData[srcStrandIndex] : FollowData{};
            follow.parentGuide = parentGuide;
            dst.followData.push_back(follow);

            for (uint32_t pointIndex = 0; pointIndex < newPointCount; ++pointIndex)
            {
                const float t = (newPointCount > 1) ? (static_cast<float>(pointIndex) / static_cast<float>(newPointCount - 1u)) : 0.0f;
                dst.restPoints.push_back(SampleStrandPoint(src, srcStrand, t));
                dst.pointWidths.push_back(SampleStrandWidth(src, srcStrand, t));
            }
        }
    }

    HairAsset BuildReducedHairAsset(const HairAsset& source, float strandFraction, float particleFraction)
    {
        if (source.strands.empty())
        {
            return source;
        }

        HairAsset lod = {};
        lod.scalpMesh = source.scalpMesh;
        const uint32_t sourceGuideCount = std::min(source.guideStrandCount, static_cast<uint32_t>(source.strands.size()));
        const uint32_t sourceFollowerCount = static_cast<uint32_t>(source.strands.size()) - sourceGuideCount;
        const uint32_t keepGuideCount = std::max(1u, static_cast<uint32_t>(std::round(static_cast<float>(sourceGuideCount) * Clamp(strandFraction, 0.0f, 1.0f))));
        const uint32_t keepFollowerCount = (sourceFollowerCount > 0u)
            ? static_cast<uint32_t>(std::round(static_cast<float>(sourceFollowerCount) * Clamp(strandFraction, 0.0f, 1.0f)))
            : 0u;
        const uint32_t newPointCount = std::max(4u, static_cast<uint32_t>(std::round(static_cast<float>(std::max(source.particlesPerStrand, 1u)) * Clamp(particleFraction, 0.0f, 1.0f))));

        lod.guideStrandCount = keepGuideCount;
        lod.particlesPerStrand = newPointCount;

        const std::vector<uint32_t> keptGuides = EvenlySampleIndices(sourceGuideCount, keepGuideCount);
        std::unordered_map<uint32_t, uint32_t> sourceGuideToNewGuide;
        for (uint32_t newGuideIndex = 0; newGuideIndex < keptGuides.size(); ++newGuideIndex)
        {
            sourceGuideToNewGuide[keptGuides[newGuideIndex]] = newGuideIndex;
            AppendResampledStrand(lod, source, keptGuides[newGuideIndex], newPointCount, newGuideIndex, static_cast<uint32_t>(StrandFlags::Guide), static_cast<int32_t>(newGuideIndex));
        }

        std::vector<uint32_t> candidateFollowers;
        candidateFollowers.reserve(sourceFollowerCount);
        for (uint32_t srcFollowerIndex = 0; srcFollowerIndex < sourceFollowerCount; ++srcFollowerIndex)
        {
            const uint32_t srcStrandIndex = sourceGuideCount + srcFollowerIndex;
            const FollowData follow = (srcStrandIndex < source.followData.size()) ? source.followData[srcStrandIndex] : FollowData{};
            if (follow.parentGuide >= 0 && sourceGuideToNewGuide.find(static_cast<uint32_t>(follow.parentGuide)) != sourceGuideToNewGuide.end())
            {
                candidateFollowers.push_back(srcStrandIndex);
            }
        }

        const uint32_t actualKeepFollowers = std::min<uint32_t>(keepFollowerCount, static_cast<uint32_t>(candidateFollowers.size()));
        const std::vector<uint32_t> followerPicks = EvenlySampleIndices(static_cast<uint32_t>(candidateFollowers.size()), actualKeepFollowers);
        for (uint32_t pick : followerPicks)
        {
            const uint32_t srcStrandIndex = candidateFollowers[pick];
            const FollowData follow = (srcStrandIndex < source.followData.size()) ? source.followData[srcStrandIndex] : FollowData{};
            const auto guideIt = sourceGuideToNewGuide.find(static_cast<uint32_t>(std::max(follow.parentGuide, 0)));
            const uint32_t parentGuide = (guideIt != sourceGuideToNewGuide.end()) ? guideIt->second : 0u;
            AppendResampledStrand(lod, source, srcStrandIndex, newPointCount, parentGuide, static_cast<uint32_t>(StrandFlags::None), static_cast<int32_t>(parentGuide));
        }

        lod.followerStrandCount = static_cast<uint32_t>(lod.strands.size()) - lod.guideStrandCount;
        return lod;
    }

    std::vector<HairLodAsset> BuildDefaultHairLodChain(const HairAsset& source)
    {
        std::vector<HairLodAsset> lods;
        lods.push_back({ source, { "LOD0 Full", 1.0f, 1.0f, 0.0f } });
        lods.push_back({ BuildReducedHairAsset(source, 0.55f, 0.80f), { "LOD1 Mid", 0.55f, 0.80f, 2.6f } });
        lods.push_back({ BuildReducedHairAsset(source, 0.25f, 0.55f), { "LOD2 Far", 0.25f, 0.55f, 3.6f } });
        return lods;
    }
}
