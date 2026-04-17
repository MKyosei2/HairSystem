#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct HairCollisionSettings
    {
        float shellDistance = 0.0025f;
        float stiffness = 1.0f;
        bool affectRootPoints = false;
        bool affectAllPoints = true;
        uint32_t iterations = 1u;
        uint32_t sdfResolution = 28u;
        float sdfPadding = 0.04f;
    };

    struct HairCollisionReport
    {
        uint32_t affectedPointCount = 0u;
        float averagePenetration = 0.0f;
        float maxPenetration = 0.0f;
    };

    bool ApplyScalpMeshCollision(HairAsset& asset,
                                 const HairCollisionSettings& settings,
                                 HairCollisionReport* outReport = nullptr);

    bool ApplyScalpSdfCollision(HairAsset& asset,
                                const HairCollisionSettings& settings,
                                HairCollisionReport* outReport = nullptr);

    bool ExportScalpSdfSlicesCsv(const HairAsset& asset,
                                 const std::filesystem::path& filePath,
                                 uint32_t resolution,
                                 float padding,
                                 std::string& outError);

    bool BakeSdfGridFromScalpMesh(const ScalpMesh& scalpMesh,
                                  uint32_t resolution,
                                  float padding,
                                  SdfGridCollider& outGrid,
                                  std::string& outError);

    bool ExportSdfGridColliderCsv(const SdfGridCollider& grid,
                                  const std::filesystem::path& filePath,
                                  std::string& outError);
}
