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

    struct SignedDistanceSample
    {
        float signedDistance = 0.0f;
        Vec3 normal = { 0.0f, 1.0f, 0.0f };
        uint32_t colliderType = UINT32_MAX;
        uint32_t colliderIndex = UINT32_MAX;
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

    SignedDistanceSample QuerySignedDistanceToSphere(const SphereCollider& sphere, const Vec3& p);
    SignedDistanceSample QuerySignedDistanceToCapsule(const CapsuleCollider& capsule, const Vec3& p);
    SignedDistanceSample QuerySignedDistanceToPlane(const PlaneCollider& plane, const Vec3& p);
    SignedDistanceSample QuerySignedDistanceToAabb(const AabbCollider& box, const Vec3& p);
    SignedDistanceSample QuerySignedDistanceToSdfGrid(const SdfGridCollider& grid, const Vec3& p);
    SignedDistanceSample QueryClosestColliderDistance(const ColliderSet& colliders, const Vec3& p);
}
