#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "hairgpu/Math.h"

namespace hairgpu
{
    enum class StrandFlags : uint32_t
    {
        None = 0,
        Guide = 1 << 0,
        Frozen = 1 << 1,
    };

    inline StrandFlags operator|(StrandFlags a, StrandFlags b)
    {
        return static_cast<StrandFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    struct StrandRange
    {
        uint32_t pointOffset = 0;
        uint32_t pointCount = 0;
        uint32_t groupId = 0;
        uint32_t flags = 0;
    };

    struct StrandMaterial
    {
        float rootWidth = 0.006f;
        float tipWidth = 0.0015f;
        float stiffness = 0.65f;
        float damping = 0.04f;
    };

    struct RootBinding
    {
        uint32_t triangleIndex = 0;
        Vec3 barycentric = { 1.0f, 0.0f, 0.0f };
        Vec2 rootUv = { 0.0f, 0.0f };
    };

    struct FollowData
    {
        int32_t parentGuide = -1;
        float followWeight = 1.0f;
        float clumpWeight = 0.0f;
        float noiseWeight = 0.0f;
    };

    struct TriangleIndices
    {
        uint32_t a = 0;
        uint32_t b = 0;
        uint32_t c = 0;
    };

    struct SkinWeight4
    {
        std::array<uint32_t, 4> boneIndices = { 0u, 0u, 0u, 0u };
        std::array<float, 4> boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
    };

    struct ScalpMesh
    {
        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<Vec2> uvs;
        std::vector<TriangleIndices> triangles;
        std::vector<SkinWeight4> skinWeights;
        uint32_t boneCount = 0;
    };

    struct HairAsset
    {
        std::vector<StrandRange> strands;
        std::vector<Vec3> restPoints;
        std::vector<float> pointWidths;
        std::vector<StrandMaterial> materials;
        std::vector<RootBinding> rootBindings;
        std::vector<FollowData> followData;

        ScalpMesh scalpMesh;

        uint32_t guideStrandCount = 0;
        uint32_t followerStrandCount = 0;
        uint32_t particlesPerStrand = 0;
    };



    enum class SimTier : uint32_t
    {
        Full = 0,
        Reduced = 1,
        Frozen = 2,
        Cached = 3,
    };

    struct WigParameters
    {
        float overallLength = 0.92f;
        float partingOffset = 0.0f;
        float partWidth = 0.030f;
        float scalpConform = 0.85f;
        float rootLift = 0.030f;
        float silhouetteWidth = 0.090f;
        float sideVolume = 0.070f;
        float backVolume = 0.055f;
        float lowerRingLengthBias = 0.28f;
        float strandNoise = 0.020f;
        float followerSpread = 0.010f;
        float tipInward = 0.020f;
    };

    struct GpuSimulationConfig
    {
        uint32_t solverIterations = 6;
        float fixedTimeStep = 1.0f / 120.0f;
        float gravity = 9.81f;
        float globalDamping = 0.03f;
        float segmentLength = 0.035f;
        float bendStiffness = 0.12f;
        float followStiffness = 0.92f;
        float windStrength = 0.0f;
        Vec3 windDirection = { 1.0f, 0.0f, 0.0f };
        float floorY = -1.2f;
        float selfCollisionRadius = 0.012f;
        float selfCollisionStiffness = 0.35f;
        float stretchCompliance = 2.0e-7f;
        float bendCompliance = 1.0e-5f;
        float rootStiffness = 0.18f;
        float velocityShockPropagation = 0.72f;

        // Rapunzel-style secondary controls layered on top of the base solver.
        float looseConstraintStiffness = 0.12f;
        float looseConstraintMaxDistance = 0.085f;
        float supportCollisionRadius = 0.030f;
        float supportCollisionStiffness = 0.26f;
        float tangentialFriction = 0.06f;
        float lateralFriction = 0.78f;
        float freezeBackThreshold = -0.045f;
        float freezeBlend = 0.82f;

        // Dense uniform-grid broadphase for GPU self collision.
        uint32_t selfCollisionGridDimX = 18;
        uint32_t selfCollisionGridDimY = 18;
        uint32_t selfCollisionGridDimZ = 18;
        uint32_t selfCollisionCellCapacity = 24;
        Vec3 selfCollisionGridOrigin = { -0.72f, 0.48f, -0.72f };
        float selfCollisionCellSize = 0.085f;

        // Broadphase backend selection. DenseGrid keeps the original fixed-volume layout,
        // while SpatialHash supports sparse coordinates and larger motion ranges.
        uint32_t selfCollisionBroadphaseMode = 1u;
        uint32_t selfCollisionHashBucketCount = 32768u;
    };

    enum class ColliderType : uint32_t
    {
        Sphere = 0,
        Capsule = 1,
    };

    struct SphereCollider
    {
        Vec3 center = { 0.0f, 0.0f, 0.0f };
        float radius = 0.0f;
    };

    struct CapsuleCollider
    {
        Vec3 pointA = { 0.0f, 0.0f, 0.0f };
        float radiusA = 0.0f;
        Vec3 pointB = { 0.0f, 0.0f, 0.0f };
        float radiusB = 0.0f;
    };

    struct ColliderSet
    {
        std::vector<SphereCollider> spheres;
        std::vector<CapsuleCollider> capsules;
    };

    ScalpMesh BuildSampleScalpMesh(uint32_t rows, uint32_t columns, const Vec3& center, float radiusX, float radiusY, float radiusZ);
    Vec3 EvaluateRootBindingPosition(const ScalpMesh& scalpMesh, const RootBinding& binding);
    void RebuildRootRestPositionsFromScalp(HairAsset& asset);

    WigParameters DefaultWigParameters();
    HairAsset BuildStaticWigHairAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand, float segmentLength, const WigParameters& parameters);
    HairAsset BuildSampleGuideHairAsset(uint32_t strandCount, uint32_t particlesPerStrand, float segmentLength);
    HairAsset BuildSampleHybridHairAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand, float segmentLength);

    enum class SampleMaskSourceMode : uint32_t
    {
        Auto = 0,
        ExternalFiles = 1,
        Procedural = 2,
    };

    void SetSampleMaskOverridePaths(const char* partingMaskPath, const char* regionMaskPath);
    void ClearSampleMaskOverridePaths();
    void SetSampleMaskSourceMode(SampleMaskSourceMode mode);
    SampleMaskSourceMode GetSampleMaskSourceMode();
}
