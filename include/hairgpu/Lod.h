#pragma once

#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct HairLodDescriptor
    {
        std::string name;
        float strandFraction = 1.0f;
        float particleFraction = 1.0f;
        float switchDistance = 0.0f;
        float widthScale = 1.0f;
    };

    struct HairLodBuildSettings
    {
        uint32_t levelCount = 4u;
        bool preserveAllGuidesUntilLastLevel = true;
        bool favorSilhouetteRoots = true;
        float nearSwitchDistance = 0.0f;
        float midSwitchDistance = 2.5f;
        float farSwitchDistance = 4.0f;
        float extremeSwitchDistance = 6.0f;
    };

    struct HairLodAsset
    {
        HairAsset asset;
        HairLodDescriptor descriptor;
    };

    HairAsset BuildReducedHairAsset(const HairAsset& source, float strandFraction, float particleFraction);
    std::vector<HairLodAsset> BuildDefaultHairLodChain(const HairAsset& source);
    std::vector<HairLodAsset> BuildAdaptiveHairLodChain(const HairAsset& source,
                                                        const HairLodBuildSettings& settings = {});
}
