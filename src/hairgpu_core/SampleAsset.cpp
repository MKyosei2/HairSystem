#include "hairgpu/Types.h"

#include <algorithm>
#include <cmath>

namespace hairgpu
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTau = 6.28318530717958647692f;

        float Fract(float value)
        {
            return value - std::floor(value);
        }

        float Hash01(uint32_t value)
        {
            return Fract(std::sin(static_cast<float>(value) * 12.9898f + 78.233f) * 43758.5453f);
        }

        float SmoothStep01(float t)
        {
            t = Clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        Vec3 ComputeRootDirection(const Vec3& root, const Vec3& scalpCenter)
        {
            const Vec3 outward = Normalize(Vec3(root.x - scalpCenter.x, 0.0f, root.z - scalpCenter.z));
            const Vec3 downward(0.0f, -1.0f, 0.0f);
            return Normalize(Lerp(downward, outward, 0.18f));
        }

        Vec3 ComputeWigSurfaceDirection(const Vec3& root, const Vec3& scalpCenter)
        {
            Vec3 normal = Normalize(root - scalpCenter);
            if (Length(normal) <= 1.0e-6f)
            {
                normal = Vec3(0.0f, 1.0f, 0.0f);
            }

            const Vec3 downward(0.0f, -1.0f, 0.0f);
            Vec3 tangent = downward - normal * Dot(downward, normal);
            if (Length(tangent) <= 1.0e-6f)
            {
                tangent = Vec3(normal.z, 0.0f, -normal.x);
            }
            tangent = Normalize(tangent);

            Vec3 outward = Normalize(Vec3(root.x - scalpCenter.x, 0.0f, root.z - scalpCenter.z));
            if (Length(outward) <= 1.0e-6f)
            {
                outward = Vec3(1.0f, 0.0f, 0.0f);
            }
            return Normalize(Lerp(tangent, outward, 0.15f));
        }

        Vec3 ComputeGuideOffsetFrameX(const RootBinding& binding, uint32_t seed)
        {
            const float angle = binding.rootUv.x * kTau + Hash01(seed + 11u) * 0.65f;
            return Vec3(std::cos(angle), 0.0f, std::sin(angle));
        }

        Vec3 ComputeGuideOffsetFrameZ(const Vec3& frameX)
        {
            return Normalize(Vec3(-frameX.z, 0.0f, frameX.x));
        }

        void AppendStrand(
            HairAsset& asset,
            const RootBinding& binding,
            const Vec3& root,
            const Vec3& initialDirection,
            const StrandMaterial& material,
            const FollowData& follow,
            uint32_t pointCount,
            float segmentLength,
            uint32_t groupId,
            uint32_t flags,
            uint32_t randomSeed,
            const Vec3& rootOffset,
            float sideWaveAmplitude,
            float noiseScale,
            const Vec3& scalpCenter)
        {
            StrandRange range;
            range.pointOffset = static_cast<uint32_t>(asset.restPoints.size());
            range.pointCount = pointCount;
            range.groupId = groupId;
            range.flags = flags;
            asset.strands.push_back(range);
            asset.materials.push_back(material);
            asset.rootBindings.push_back(binding);
            asset.followData.push_back(follow);

            const Vec3 sideX = Normalize(ComputeGuideOffsetFrameX(binding, randomSeed));
            const Vec3 sideZ = ComputeGuideOffsetFrameZ(sideX);
            const Vec3 shellDirection = ComputeWigSurfaceDirection(root, scalpCenter);
            const Vec3 hangDirection = Normalize(Lerp(initialDirection, Vec3(0.0f, -1.0f, 0.0f), 0.82f));
            Vec3 current = root + rootOffset * 0.25f;
            const float shellRadius = std::max(Length(root - scalpCenter), 0.001f);

            for (uint32_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
            {
                const float t = static_cast<float>(pointIndex) / static_cast<float>(pointCount - 1);
                if (pointIndex > 0u)
                {
                    const float shellBlend = 1.0f - SmoothStep01((t - 0.02f) / 0.30f);
                    const Vec3 stepDirection = Normalize(Lerp(hangDirection, shellDirection, shellBlend));
                    current += stepDirection * segmentLength;

                    if (shellBlend > 0.0f)
                    {
                        Vec3 radial = Normalize(current - scalpCenter);
                        if (Length(radial) <= 1.0e-6f)
                        {
                            radial = Normalize(root - scalpCenter);
                        }
                        const float shellOffset = 0.014f + 0.008f * (1.0f - shellBlend);
                        const Vec3 shellTarget = scalpCenter + radial * (shellRadius + shellOffset);
                        current = Lerp(current, shellTarget, 0.72f * shellBlend);
                    }

                    const float strandNoise = Hash01(randomSeed * 37u + pointIndex * 13u);
                    const float swayA = std::sin(t * 3.4f + strandNoise * kTau);
                    const float swayB = std::cos(t * 2.3f + Hash01(randomSeed * 17u + pointIndex * 5u) * kTau);
                    const float sideScale = sideWaveAmplitude * (0.04f + 0.18f * (1.0f - shellBlend) + 0.38f * t);
                    current += sideX * (swayA * sideScale * noiseScale);
                    current += sideZ * (swayB * sideScale * 0.22f * noiseScale);
                    current += rootOffset * (0.10f + 0.55f * t);

                    const float hang = std::max(t - 0.28f, 0.0f);
                    current.y -= hang * hang * (segmentLength * 0.55f);
                }

                asset.restPoints.push_back(current);
                asset.pointWidths.push_back(Lerp(material.rootWidth, material.tipWidth, t));
            }
        }
    }

    ScalpMesh BuildSampleScalpMesh(uint32_t rows, uint32_t columns, const Vec3& center, float radiusX, float radiusY, float radiusZ)
    {
        ScalpMesh mesh;
        rows = std::max(rows, 2u);
        columns = std::max(columns, 3u);

        mesh.positions.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.normals.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.uvs.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.triangles.reserve(static_cast<size_t>(rows) * columns * 2u);
        mesh.skinWeights.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.boneCount = 2;

        for (uint32_t row = 0; row <= rows; ++row)
        {
            const float v = static_cast<float>(row) / static_cast<float>(rows);
            const float phi = (0.10f + 0.44f * v) * kPi;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            for (uint32_t col = 0; col <= columns; ++col)
            {
                const float u = static_cast<float>(col) / static_cast<float>(columns);
                const float theta = u * kTau;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);

                Vec3 normal(cosTheta * sinPhi, cosPhi, sinTheta * sinPhi);
                Vec3 position(
                    center.x + normal.x * radiusX,
                    center.y + normal.y * radiusY,
                    center.z + normal.z * radiusZ);

                mesh.positions.push_back(position);
                mesh.normals.push_back(Normalize(Vec3(normal.x / radiusX, normal.y / radiusY, normal.z / radiusZ)));
                mesh.uvs.push_back({ u, v });
                SkinWeight4 weights;
                const float headWeight = Lerp(0.18f, 1.0f, std::pow(v, 0.7f));
                weights.boneIndices = { 0u, 1u, 0u, 0u };
                weights.boneWeights = { 1.0f - headWeight, headWeight, 0.0f, 0.0f };
                mesh.skinWeights.push_back(weights);
            }
        }

        const uint32_t stride = columns + 1;
        for (uint32_t row = 0; row < rows; ++row)
        {
            for (uint32_t col = 0; col < columns; ++col)
            {
                const uint32_t i0 = row * stride + col;
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = i0 + stride;
                const uint32_t i3 = i2 + 1;
                mesh.triangles.push_back({ i0, i2, i1 });
                mesh.triangles.push_back({ i1, i2, i3 });
            }
        }

        return mesh;
    }

    Vec3 EvaluateRootBindingPosition(const ScalpMesh& scalpMesh, const RootBinding& binding)
    {
        if (binding.triangleIndex >= scalpMesh.triangles.size())
        {
            return {};
        }

        const TriangleIndices& tri = scalpMesh.triangles[binding.triangleIndex];
        const Vec3& a = scalpMesh.positions[tri.a];
        const Vec3& b = scalpMesh.positions[tri.b];
        const Vec3& c = scalpMesh.positions[tri.c];
        return a * binding.barycentric.x + b * binding.barycentric.y + c * binding.barycentric.z;
    }

    void RebuildRootRestPositionsFromScalp(HairAsset& asset)
    {
        if (asset.scalpMesh.positions.empty() || asset.strands.empty())
        {
            return;
        }

        for (size_t strandIndex = 0; strandIndex < asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0 || strand.pointOffset >= asset.restPoints.size())
            {
                continue;
            }

            const Vec3 reboundRoot = EvaluateRootBindingPosition(asset.scalpMesh, asset.rootBindings[strandIndex]);
            Vec3 originalRoot = asset.restPoints[strand.pointOffset];
            Vec3 delta = reboundRoot - originalRoot;
            for (uint32_t i = 0; i < strand.pointCount; ++i)
            {
                asset.restPoints[strand.pointOffset + i] += delta;
            }
        }
    }

    HairAsset BuildSampleGuideHairAsset(uint32_t strandCount, uint32_t particlesPerStrand, float segmentLength)
    {
        return BuildSampleHybridHairAsset(strandCount, 0, particlesPerStrand, segmentLength);
    }

    HairAsset BuildSampleHybridHairAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand, float segmentLength)
    {
        HairAsset asset;
        asset.guideStrandCount = guideCount;
        asset.followerStrandCount = guideCount * followersPerGuide;
        asset.particlesPerStrand = particlesPerStrand;
        asset.scalpMesh = BuildSampleScalpMesh(24, 40, Vec3(0.0f, 1.60f, 0.0f), 0.23f, 0.19f, 0.18f);

        const uint32_t totalStrands = asset.guideStrandCount + asset.followerStrandCount;
        asset.strands.reserve(totalStrands);
        asset.materials.reserve(totalStrands);
        asset.rootBindings.reserve(totalStrands);
        asset.followData.reserve(totalStrands);
        asset.restPoints.reserve(static_cast<size_t>(totalStrands) * particlesPerStrand);
        asset.pointWidths.reserve(static_cast<size_t>(totalStrands) * particlesPerStrand);

        const Vec3 scalpCenter(0.0f, 1.60f, 0.0f);
        const uint32_t rows = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(guideCount))));
        const uint32_t cols = rows;

        for (uint32_t guideIndex = 0; guideIndex < guideCount; ++guideIndex)
        {
            const uint32_t row = guideIndex / cols;
            const uint32_t col = guideIndex % cols;
            float u = (static_cast<float>(col) + 0.5f) / static_cast<float>(cols);
            float v = (static_cast<float>(row) + 0.5f) / static_cast<float>(rows);
            float x = (u - 0.5f) * 2.0f;
            float z = (v - 0.5f) * 2.0f;
            const float radial = x * x + z * z;
            if (radial > 1.0f)
            {
                const float inv = 1.0f / std::sqrt(radial);
                x *= inv;
                z *= inv;
            }

            const float jitterX = (Hash01(guideIndex * 7 + 1) - 0.5f) * 0.010f;
            const float jitterZ = (Hash01(guideIndex * 7 + 2) - 0.5f) * 0.010f;
            const float rootY = scalpCenter.y + 0.10f * (1.0f - radial) + 0.03f;
            const Vec3 root(
                scalpCenter.x + x * 0.22f + jitterX,
                rootY,
                scalpCenter.z + z * 0.17f + jitterZ);

            const Vec3 direction = ComputeRootDirection(root, scalpCenter);
            StrandMaterial material;
            material.rootWidth = 0.0100f;
            material.tipWidth = 0.0014f;
            material.stiffness = 0.82f;
            material.damping = 0.026f;

            RootBinding binding;
            binding.triangleIndex = guideIndex % static_cast<uint32_t>(asset.scalpMesh.triangles.size());
            binding.barycentric = { 0.55f, 0.25f, 0.20f };
            binding.rootUv = { u, v };

            FollowData guideFollow;
            guideFollow.parentGuide = static_cast<int32_t>(guideIndex);
            guideFollow.followWeight = 1.0f;
            guideFollow.clumpWeight = 0.0f;
            guideFollow.noiseWeight = 0.0f;

            AppendStrand(
                asset,
                binding,
                root,
                direction,
                material,
                guideFollow,
                particlesPerStrand,
                segmentLength,
                0,
                static_cast<uint32_t>(StrandFlags::Guide),
                guideIndex + 1u,
                Vec3(0.0f, 0.0f, 0.0f),
                0.0105f,
                1.0f,
                scalpCenter);

            const Vec3 sideX = Normalize(ComputeGuideOffsetFrameX(binding, guideIndex + 101u));
            const Vec3 sideZ = ComputeGuideOffsetFrameZ(sideX);
            for (uint32_t followerIndex = 0; followerIndex < followersPerGuide; ++followerIndex)
            {
                const uint32_t seed = guideIndex * 97u + followerIndex * 17u + 13u;
                const float ring = 0.004f + 0.012f * (0.25f + Hash01(seed + 5u));
                const float angle = Hash01(seed + 9u) * kTau;
                const Vec3 rootOffset = sideX * (std::cos(angle) * ring) + sideZ * (std::sin(angle) * ring);

                StrandMaterial followerMaterial = material;
                followerMaterial.rootWidth *= 0.88f;
                followerMaterial.tipWidth *= 0.82f;

                RootBinding followerBinding = binding;
                followerBinding.rootUv.x = std::fmod(binding.rootUv.x + (Hash01(seed + 21u) - 0.5f) * 0.02f + 1.0f, 1.0f);
                followerBinding.rootUv.y = std::max(0.0f, std::min(binding.rootUv.y + (Hash01(seed + 22u) - 0.5f) * 0.02f, 1.0f));

                FollowData followerFollow;
                followerFollow.parentGuide = static_cast<int32_t>(guideIndex);
                followerFollow.followWeight = 0.95f + Hash01(seed + 7u) * 0.04f;
                followerFollow.clumpWeight = 0.78f + Hash01(seed + 19u) * 0.18f;
                followerFollow.noiseWeight = 0.12f + Hash01(seed + 23u) * 0.20f;

                AppendStrand(
                    asset,
                    followerBinding,
                    root,
                    direction,
                    followerMaterial,
                    followerFollow,
                    particlesPerStrand,
                    segmentLength,
                    1,
                    static_cast<uint32_t>(StrandFlags::None),
                    seed,
                    rootOffset,
                    0.006f,
                    0.75f,
                    scalpCenter);
            }
        }

        RebuildRootRestPositionsFromScalp(asset);
        return asset;
    }
}
