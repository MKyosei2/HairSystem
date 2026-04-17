#pragma once

#include <cstdint>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct AdaptiveSimulationSettings
    {
        float fullDistance = 1.6f;
        float reducedDistance = 2.8f;
        float silhouetteBoost = 0.55f;
        float frontPriority = 0.35f;
        float frozenBackZ = -0.06f;
        float cachedBackZ = -0.18f;
        float cachedDistance = 1.8f;
        float visibleFrontZ = 0.02f;
    };

    struct AdaptiveSimulationStats
    {
        uint32_t fullCount = 0;
        uint32_t reducedCount = 0;
        uint32_t frozenCount = 0;
        uint32_t cachedCount = 0;
    };

    std::vector<SimTier> BuildAdaptiveSimulationTiers(
        const HairAsset& asset,
        const std::vector<Vec3>& strandRoots,
        const Vec3& cameraPosition,
        const Vec3& cameraForward,
        const AdaptiveSimulationSettings& settings,
        bool cacheAvailable,
        AdaptiveSimulationStats* outStats = nullptr);
}
