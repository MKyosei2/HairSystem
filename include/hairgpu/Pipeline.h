#pragma once

#include <filesystem>
#include <string>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct ScaffoldProjectSettings
    {
        float guideCacheFrameRate = 30.0f;
        uint32_t guideCacheFrameCount = 60u;
        bool saveHairBinary = true;
        bool saveValidationReport = true;
        bool saveMetricsJson = true;
        bool saveManifestJson = true;
        bool saveLods = true;
        bool saveCards = true;
        bool saveUsda = true;
        bool saveGuideCache = true;
        bool saveScalpObj = true;
    };

    struct ScaffoldProjectOutputs
    {
        std::filesystem::path normalizedHairAssetPath;
        std::filesystem::path validationReportPath;
        std::filesystem::path metricsJsonPath;
        std::filesystem::path manifestJsonPath;
        std::filesystem::path lodDirectory;
        std::filesystem::path cardsObjPath;
        std::filesystem::path usdaPath;
        std::filesystem::path guideCachePath;
        std::filesystem::path scalpObjPath;
    };

    bool WriteHairAssetMetricsJson(const std::filesystem::path& outputPath,
                                   const HairAsset& asset,
                                   std::string& outError);

    bool ScaffoldHairProject(const HairAsset& asset,
                             const std::filesystem::path& outputDirectory,
                             const ScaffoldProjectSettings& settings,
                             ScaffoldProjectOutputs& outOutputs,
                             std::string& outError);
}
