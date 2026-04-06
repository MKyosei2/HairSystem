#pragma once

#include <filesystem>
#include <string>

#include "hairgpu/Types.h"

#include <vector>

namespace hairgpu
{
    struct AlembicRoundTripReport
    {
        bool usedAlembicSdk = false;
        uint32_t sourceStrandCount = 0u;
        uint32_t roundTripStrandCount = 0u;
        uint32_t sourcePointCount = 0u;
        uint32_t roundTripPointCount = 0u;
        float rmsRestPointError = 0.0f;
        float maxRestPointError = 0.0f;
        float rmsWidthError = 0.0f;
        float maxWidthError = 0.0f;
        bool scalpRoundTripOk = false;
    };

    struct HairAlembicSettings
    {
        bool includeScalp = true;
        bool includeGuidePrimvar = true;
        bool includeGroupPrimvar = true;
        bool includeWidths = true;
    };
}


namespace hairgpu
{
    bool LoadHairAssetFromCurvesCsv(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
    bool LoadHairAssetFromUsda(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
    bool LoadHairAssetFromAlembic(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
    bool LoadHairAssetFromHairFile(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
    bool SaveHairAssetToAlembic(const std::filesystem::path& filePath, const HairAsset& asset, std::string& outError);
    bool SaveHairAssetToAlembic(const std::filesystem::path& filePath, const HairAsset& asset, const HairAlembicSettings& settings, std::string& outError);
    bool SaveHairAssetToHairBinary(const std::filesystem::path& filePath, const HairAsset& asset, std::string& outError);
    bool RoundTripHairAssetThroughAlembic(const HairAsset& asset,
                                        const std::filesystem::path& filePath,
                                        const HairAlembicSettings& settings,
                                        AlembicRoundTripReport& outReport,
                                        std::string& outError);
    bool LoadHairAssetFromHairBinary(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
}
