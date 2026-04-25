#include "hairgpu/Binding.h"
#include "hairgpu/Geometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hairgpu
{


    bool GenerateRootBindingsFromScalp(HairAsset& asset, bool snapRootsToScalp, RootBindingQualityReport* outReport)
    {
        if (asset.strands.empty() || asset.restPoints.empty() || asset.scalpMesh.positions.empty() || asset.scalpMesh.triangles.empty())
        {
            return false;
        }

        RootBindingQualityReport report = {};
        asset.rootBindings.resize(asset.strands.size());

        for (size_t strandIndex = 0; strandIndex < asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0 || strand.pointOffset >= asset.restPoints.size())
            {
                ++report.invalidStrandCount;
                continue;
            }

            const Vec3 root = asset.restPoints[strand.pointOffset];
            ClosestTrianglePoint best = {};
            for (uint32_t triIndex = 0; triIndex < static_cast<uint32_t>(asset.scalpMesh.triangles.size()); ++triIndex)
            {
                const TriangleIndices& tri = asset.scalpMesh.triangles[triIndex];
                const ClosestTrianglePoint candidate = ClosestPointOnTriangle(root,
                    asset.scalpMesh.positions[tri.a],
                    asset.scalpMesh.positions[tri.b],
                    asset.scalpMesh.positions[tri.c]);
                if (candidate.distanceSq < best.distanceSq)
                {
                    best = candidate;
                    best.triangleIndex = triIndex;
                }
            }

            RootBinding binding = {};
            binding.triangleIndex = best.triangleIndex;
            binding.barycentric = best.barycentric;
            const TriangleIndices& tri = asset.scalpMesh.triangles[best.triangleIndex];
            const Vec2 uvA = (tri.a < asset.scalpMesh.uvs.size()) ? asset.scalpMesh.uvs[tri.a] : Vec2{0.5f, 0.5f};
            const Vec2 uvB = (tri.b < asset.scalpMesh.uvs.size()) ? asset.scalpMesh.uvs[tri.b] : Vec2{0.5f, 0.5f};
            const Vec2 uvC = (tri.c < asset.scalpMesh.uvs.size()) ? asset.scalpMesh.uvs[tri.c] : Vec2{0.5f, 0.5f};
            binding.rootUv = {
                uvA.x * binding.barycentric.x + uvB.x * binding.barycentric.y + uvC.x * binding.barycentric.z,
                uvA.y * binding.barycentric.x + uvB.y * binding.barycentric.y + uvC.y * binding.barycentric.z,
            };
            asset.rootBindings[strandIndex] = binding;

            const float dist = std::sqrt(best.distanceSq);
            report.averageRootDistance += dist;
            report.maxRootDistance = std::max(report.maxRootDistance, dist);

            if (snapRootsToScalp)
            {
                const Vec3 delta = best.position - root;
                for (uint32_t localIndex = 0; localIndex < strand.pointCount; ++localIndex)
                {
                    asset.restPoints[strand.pointOffset + localIndex] += delta;
                }
            }
        }

        if (!asset.strands.empty())
        {
            report.averageRootDistance /= static_cast<float>(asset.strands.size());
        }
        if (outReport != nullptr)
        {
            *outReport = report;
        }
        return true;
    }


    void NormalizeScalpSkinWeights(ScalpMesh& scalpMesh, bool fillMissingWeights)
    {
        scalpMesh.skinWeights.resize(scalpMesh.positions.size());
        for (SkinWeight4& weights : scalpMesh.skinWeights)
        {
            float sum = 0.0f;
            for (float value : weights.boneWeights)
            {
                sum += std::max(value, 0.0f);
            }
            if (sum <= 1.0e-6f)
            {
                if (fillMissingWeights)
                {
                    weights.boneIndices = { 0u, 0u, 0u, 0u };
                    weights.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
                }
                continue;
            }

            std::array<std::pair<float, uint32_t>, 4> packed = {};
            for (size_t i = 0; i < 4; ++i)
            {
                packed[i] = { std::max(weights.boneWeights[i], 0.0f), weights.boneIndices[i] };
            }
            std::sort(packed.begin(), packed.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            float renorm = 0.0f;
            for (size_t i = 0; i < 4; ++i)
            {
                weights.boneWeights[i] = packed[i].first;
                weights.boneIndices[i] = packed[i].second;
                renorm += weights.boneWeights[i];
            }
            renorm = std::max(renorm, 1.0e-6f);
            for (float& value : weights.boneWeights)
            {
                value /= renorm;
            }
        }
    }

    bool RepairHairAssetSkinning(HairAsset& asset, bool snapRootsToScalp, RootBindingQualityReport* outReport)
    {
        NormalizeScalpSkinWeights(asset.scalpMesh, true);
        return GenerateRootBindingsFromScalp(asset, snapRootsToScalp, outReport);
    }

    std::vector<Vec3> ComputeSkinnedScalpPositions(const ScalpMesh& scalpMesh, const std::vector<Mat4>& boneMatrices)
    {
        std::vector<Vec3> result;
        result.resize(scalpMesh.positions.size());
        for (size_t vertexIndex = 0; vertexIndex < scalpMesh.positions.size(); ++vertexIndex)
        {
            const Vec3 rest = scalpMesh.positions[vertexIndex];
            const SkinWeight4 weights = (vertexIndex < scalpMesh.skinWeights.size()) ? scalpMesh.skinWeights[vertexIndex] : SkinWeight4{};
            Vec3 blended = {};
            float remainingWeight = 0.0f;
            for (size_t influenceIndex = 0; influenceIndex < 4; ++influenceIndex)
            {
                const uint32_t boneIndex = weights.boneIndices[influenceIndex];
                const float boneWeight = weights.boneWeights[influenceIndex];
                if (boneWeight <= 0.0f)
                {
                    continue;
                }
                remainingWeight += boneWeight;
                if (boneIndex < boneMatrices.size())
                {
                    blended += TransformPoint(boneMatrices[boneIndex], rest) * boneWeight;
                }
                else
                {
                    blended += rest * boneWeight;
                }
            }
            if (remainingWeight <= 1.0e-6f)
            {
                blended = rest;
            }
            result[vertexIndex] = blended;
        }
        return result;
    }

    void RebuildRootRestPositionsFromScalpPositions(HairAsset& asset, const std::vector<Vec3>& scalpPositions, bool translateEntireStrands)
    {
        if (asset.strands.empty() || asset.rootBindings.size() < asset.strands.size() || asset.scalpMesh.triangles.empty())
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
            const RootBinding& binding = asset.rootBindings[strandIndex];
            if (binding.triangleIndex >= asset.scalpMesh.triangles.size())
            {
                continue;
            }
            const TriangleIndices& tri = asset.scalpMesh.triangles[binding.triangleIndex];
            if (tri.a >= scalpPositions.size() || tri.b >= scalpPositions.size() || tri.c >= scalpPositions.size())
            {
                continue;
            }
            const Vec3 root = scalpPositions[tri.a] * binding.barycentric.x
                + scalpPositions[tri.b] * binding.barycentric.y
                + scalpPositions[tri.c] * binding.barycentric.z;
            if (translateEntireStrands)
            {
                const Vec3 delta = root - asset.restPoints[strand.pointOffset];
                for (uint32_t localIndex = 0; localIndex < strand.pointCount; ++localIndex)
                {
                    asset.restPoints[strand.pointOffset + localIndex] += delta;
                }
            }
            else
            {
                asset.restPoints[strand.pointOffset] = root;
            }
        }
    }
}
