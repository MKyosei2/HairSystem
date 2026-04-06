#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/Lod.h"
#include "hairgpu/Types.h"

namespace hairgpu
{
    enum class DccInteropFlavor : uint32_t
    {
        BlenderCurves = 0u,
        MayaNurbsCurves = 1u,
        HoudiniCurves = 2u,
    };

    struct DccInteropExportSettings
    {
        DccInteropFlavor flavor = DccInteropFlavor::BlenderCurves;
        bool includeScalp = true;
        bool includeSkinning = true;
        bool includeRootBindings = true;
        bool includeLods = true;
        uint32_t lodCount = 4u;
        HairLodBuildSettings lodSettings = {};
    };

    std::string ToString(DccInteropFlavor flavor);
    bool ParseDccInteropFlavor(const std::string& text, DccInteropFlavor& outFlavor);

    bool SaveHairAssetToDccInteropJson(const std::filesystem::path& filePath,
                                       const HairAsset& asset,
                                       const DccInteropExportSettings& settings,
                                       std::string& outError);
}
