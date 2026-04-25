#include "hairgpu/Adaptive.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace hairgpu
{
    std::vector<SimTier> BuildAdaptiveSimulationTiers(
        const HairAsset& asset,
        const std::vector<Vec3>& strandRoots,
        const Vec3& cameraPosition,
        const Vec3& cameraForward,
        const AdaptiveSimulationSettings& settings,
        bool cacheAvailable,
        AdaptiveSimulationStats* outStats)
    {
        std::vector<SimTier> tiers(asset.strands.size(), SimTier::Full);
        AdaptiveSimulationStats stats = {};

        Vec3 boundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
        Vec3 boundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        for (uint32_t i = 0; i < asset.guideStrandCount && i < asset.strands.size(); ++i)
        {
            const StrandRange& strand = asset.strands[i];
            if (strand.pointCount == 0u || strand.pointOffset >= asset.restPoints.size())
            {
                continue;
            }
            const Vec3 p = asset.restPoints[strand.pointOffset];
            ExpandBounds(boundsMin, boundsMax, p);
        }
        if (boundsMin.x > boundsMax.x)
        {
            boundsMin = {-0.1f, 1.4f, -0.1f};
            boundsMax = {0.1f, 1.8f, 0.1f};
        }
        const Vec3 center = (boundsMin + boundsMax) * 0.5f;
        const float halfWidth = std::max((boundsMax.x - boundsMin.x) * 0.5f, 1.0e-3f);
        const Vec3 camF = Normalize(cameraForward);

        for (uint32_t strandIndex = 0; strandIndex < static_cast<uint32_t>(asset.strands.size()); ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            const Vec3 restRoot = (strand.pointCount > 0u && strand.pointOffset < asset.restPoints.size())
                ? asset.restPoints[strand.pointOffset]
                : Vec3{};
            const Vec3 worldRoot = (strandIndex < strandRoots.size()) ? strandRoots[strandIndex] : restRoot;

            SimTier tier = SimTier::Reduced;
            if (strandIndex >= asset.guideStrandCount)
            {
                int32_t parentGuide = (strandIndex < asset.followData.size()) ? asset.followData[strandIndex].parentGuide : -1;
                if (parentGuide >= 0 && static_cast<size_t>(parentGuide) < tiers.size())
                {
                    tier = tiers[static_cast<size_t>(parentGuide)];
                    if (tier == SimTier::Frozen)
                    {
                        tier = SimTier::Reduced;
                    }
                }
                tiers[strandIndex] = tier;
                continue;
            }

            const float distanceToCamera = Length(worldRoot - cameraPosition);
            const float distanceFactor = DistanceFactor(worldRoot, cameraPosition, settings.fullDistance, settings.reducedDistance, 1.0e-3f);
            const float frontFactor = FrontFactor(worldRoot, cameraPosition, camF);
            const float silhouetteFactor = SilhouetteFactorX(restRoot, center, halfWidth, 1.0e-3f);
            const float visibleFront = restRoot.z >= settings.visibleFrontZ ? 1.0f : 0.0f;
            const float importance = std::max(distanceFactor, silhouetteFactor * settings.silhouetteBoost) * (1.0f - settings.frontPriority)
                                   + std::max(frontFactor, visibleFront) * settings.frontPriority;

            if (cacheAvailable && restRoot.z <= settings.cachedBackZ && distanceToCamera >= settings.cachedDistance)
            {
                tier = SimTier::Cached;
            }
            else if (restRoot.z <= settings.frozenBackZ && importance < 0.42f)
            {
                tier = SimTier::Frozen;
            }
            else if (importance >= 0.62f)
            {
                tier = SimTier::Full;
            }
            else
            {
                tier = SimTier::Reduced;
            }

            tiers[strandIndex] = tier;
        }

        for (SimTier tier : tiers)
        {
            switch (tier)
            {
            case SimTier::Full: ++stats.fullCount; break;
            case SimTier::Reduced: ++stats.reducedCount; break;
            case SimTier::Frozen: ++stats.frozenCount; break;
            case SimTier::Cached: ++stats.cachedCount; break;
            default: break;
            }
        }
        if (outStats)
        {
            *outStats = stats;
        }
        return tiers;
    }
}
