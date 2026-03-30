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
    };

    struct HairLodAsset
    {
        HairAsset asset;
        HairLodDescriptor descriptor;
    };

    HairAsset BuildReducedHairAsset(const HairAsset& source, float strandFraction, float particleFraction);
    std::vector<HairLodAsset> BuildDefaultHairLodChain(const HairAsset& source);
}
