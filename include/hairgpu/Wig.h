#pragma once

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct WigStyleSettings
    {
        float strength = 1.0f;
        float capRegion = 0.34f;
        float shellOffset = 0.004f;
        float flowBlend = 0.78f;
        float outwardBlend = 0.18f;
        float restoreOriginalTowardsTip = 0.62f;
    };

    bool StyleAssetToScalpWig(HairAsset& asset, const WigStyleSettings& settings = {});
}
