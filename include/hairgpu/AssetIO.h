#pragma once

#include <filesystem>
#include <string>

#include "hairgpu/Types.h"

#include <vector>

namespace hairgpu
{
    bool LoadHairAssetFromCurvesCsv(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
    bool LoadHairAssetFromAlembic(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
    bool LoadHairAssetFromHairFile(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
    bool SaveHairAssetToAlembic(const std::filesystem::path& filePath, const HairAsset& asset, std::string& outError);
    bool SaveHairAssetToHairBinary(const std::filesystem::path& filePath, const HairAsset& asset, std::string& outError);
    bool LoadHairAssetFromHairBinary(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
}
