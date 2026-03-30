#include "hairgpu/Binding.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hairgpu
{
    namespace
    {
        struct ClosestTrianglePoint
        {
            Vec3 position = {};
            Vec3 barycentric = { 1.0f, 0.0f, 0.0f };
            float distanceSq = std::numeric_limits<float>::max();
            uint32_t triangleIndex = 0;
        };

        ClosestTrianglePoint ClosestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
        {
            const Vec3 ab = b - a;
            const Vec3 ac = c - a;
            const Vec3 ap = p - a;
            const float d1 = Dot(ab, ap);
            const float d2 = Dot(ac, ap);
            if (d1 <= 0.0f && d2 <= 0.0f)
            {
                return { a, {1.0f, 0.0f, 0.0f}, Dot(p - a, p - a), 0u };
            }

            const Vec3 bp = p - b;
            const float d3 = Dot(ab, bp);
            const float d4 = Dot(ac, bp);
            if (d3 >= 0.0f && d4 <= d3)
            {
                return { b, {0.0f, 1.0f, 0.0f}, Dot(p - b, p - b), 0u };
            }

            const float vc = d1 * d4 - d3 * d2;
            if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
            {
                const float v = d1 / std::max(d1 - d3, 1.0e-8f);
                const Vec3 q = a + ab * v;
                return { q, {1.0f - v, v, 0.0f}, Dot(p - q, p - q), 0u };
            }

            const Vec3 cp = p - c;
            const float d5 = Dot(ab, cp);
            const float d6 = Dot(ac, cp);
            if (d6 >= 0.0f && d5 <= d6)
            {
                return { c, {0.0f, 0.0f, 1.0f}, Dot(p - c, p - c), 0u };
            }

            const float vb = d5 * d2 - d1 * d6;
            if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
            {
                const float w = d2 / std::max(d2 - d6, 1.0e-8f);
                const Vec3 q = a + ac * w;
                return { q, {1.0f - w, 0.0f, w}, Dot(p - q, p - q), 0u };
            }

            const float va = d3 * d6 - d5 * d4;
            if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
            {
                const Vec3 bc = c - b;
                const float w = (d4 - d3) / std::max((d4 - d3) + (d5 - d6), 1.0e-8f);
                const Vec3 q = b + bc * w;
                return { q, {0.0f, 1.0f - w, w}, Dot(p - q, p - q), 0u };
            }

            const float denom = 1.0f / std::max(va + vb + vc, 1.0e-8f);
            const float v = vb * denom;
            const float w = vc * denom;
            const float u = 1.0f - v - w;
            const Vec3 q = a * u + b * v + c * w;
            return { q, {u, v, w}, Dot(p - q, p - q), 0u };
        }
    }

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
