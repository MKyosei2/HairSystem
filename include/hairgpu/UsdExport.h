#pragma once

#include <filesystem>
#include <string>

#include "hairgpu/Cards.h"
#include "hairgpu/Types.h"

namespace hairgpu
{
    struct HairUsdExportSettings
    {
        bool includeScalp = true;
        bool includeCards = false;
        bool includeWidths = true;
        bool includeGuidePrimvar = true;
        bool includeGroupPrimvar = true;
        HairCardBuilderSettings cardSettings = {};
    };

    bool SaveHairAssetToUsda(const std::filesystem::path& filePath,
                             const HairAsset& asset,
                             const HairUsdExportSettings& settings,
                             std::string& outError);
}
