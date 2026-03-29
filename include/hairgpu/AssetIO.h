#pragma once

#include <filesystem>
#include <string>

#include "hairgpu/Types.h"

namespace hairgpu
{
    bool LoadHairAssetFromCurvesCsv(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
    bool LoadHairAssetFromAlembic(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError);
}
