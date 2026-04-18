#include "hairgpu/Wig.h"

#include <algorithm>

namespace hairgpu
{
    namespace
    {
        Vec3 ComputeScalpCenter(const ScalpMesh& scalpMesh)
        {
            if (scalpMesh.positions.empty())
            {
                return Vec3(0.0f, 1.6f, 0.0f);
            }

            Vec3 sum(0.0f, 0.0f, 0.0f);
            for (const Vec3& p : scalpMesh.positions)
            {
                sum += p;
            }
            return sum / static_cast<float>(scalpMesh.positions.size());
        }

        Vec3 ProjectOntoPlane(const Vec3& v, const Vec3& n)
        {
            return v - n * Dot(v, n);
        }

        Vec3 EvaluateBindingNormal(const ScalpMesh& scalpMesh, const RootBinding& binding)
        {
            if (scalpMesh.triangles.empty())
            {
                return Vec3(0.0f, 1.0f, 0.0f);
            }

            const TriangleIndices& tri = scalpMesh.triangles[std::min<size_t>(binding.triangleIndex, scalpMesh.triangles.size() - 1u)];
            if (tri.a < scalpMesh.normals.size() && tri.b < scalpMesh.normals.size() && tri.c < scalpMesh.normals.size())
            {
                const Vec3 n = scalpMesh.normals[tri.a] * binding.barycentric.x +
                               scalpMesh.normals[tri.b] * binding.barycentric.y +
                               scalpMesh.normals[tri.c] * binding.barycentric.z;
                const Vec3 normalized = Normalize(n);
                if (Length(normalized) > 1.0e-6f)
                {
                    return normalized;
                }
            }

            if (tri.a < scalpMesh.positions.size() && tri.b < scalpMesh.positions.size() && tri.c < scalpMesh.positions.size())
            {
                const Vec3 a = scalpMesh.positions[tri.a];
                const Vec3 b = scalpMesh.positions[tri.b];
                const Vec3 c = scalpMesh.positions[tri.c];
                const Vec3 faceNormal = Normalize(Cross(b - a, c - a));
                if (Length(faceNormal) > 1.0e-6f)
                {
                    return faceNormal;
                }
            }

            return Vec3(0.0f, 1.0f, 0.0f);
        }

        
        Vec3 ComputeCapFlowDirection(const Vec3& rootNormal, const Vec3& rootPosition, const Vec3& scalpCenter, const Vec3& fallbackDirection)
        {
            Vec3 projectedDown = ProjectOntoPlane(Vec3(0.0f, -1.0f, 0.0f), rootNormal);
            if (Length(projectedDown) <= 1.0e-6f)
            {
                projectedDown = fallbackDirection;
            }

            const Vec3 relative = rootPosition - scalpCenter;
            Vec3 radialAway = ProjectOntoPlane(relative, rootNormal);
            if (Length(radialAway) <= 1.0e-6f)
            {
                radialAway = fallbackDirection;
            }

            const float crownMask = Clamp(1.0f - Length(radialAway) / 0.12f, 0.0f, 1.0f);
            const float frontMask = Clamp(relative.z / 0.18f, 0.0f, 1.0f);
            Vec3 frontBias(0.0f, 0.0f, 0.0f);
            if (frontMask > 0.0f)
            {
                frontBias = Vec3(0.0f, 0.0f, 1.0f) * frontMask * 0.025f;
            }

            const float normalWeight = 0.76f + 0.14f * crownMask;
            const float downWeight = 0.18f;
            const float radialWeight = Lerp(0.12f, 0.05f, crownMask);
            Vec3 launch = rootNormal * normalWeight + Normalize(projectedDown) * downWeight + Normalize(radialAway) * radialWeight + frontBias * 0.25f;
            if (Length(launch) <= 1.0e-6f)
            {
                launch = fallbackDirection;
            }
            return Normalize(launch);
        }

    }

    bool StyleAssetToScalpWig(HairAsset& asset, const WigStyleSettings& settings)
    {
        if (asset.strands.empty() || asset.restPoints.empty() || asset.rootBindings.size() != asset.strands.size() || asset.scalpMesh.positions.empty())
        {
            return false;
        }

        const Vec3 scalpCenter = ComputeScalpCenter(asset.scalpMesh);
        const float clampedStrength = Clamp(settings.strength, 0.0f, 1.0f);
        const float capRegion = Clamp(settings.capRegion, 0.05f, 0.65f);
        const float shellOffset = std::max(settings.shellOffset, 0.0f);
        const float flowBlend = Clamp(settings.flowBlend, 0.0f, 1.0f);
        const float outwardBlend = Clamp(settings.outwardBlend, 0.0f, 1.0f);
        const float tipRestore = Clamp(settings.restoreOriginalTowardsTip, 0.0f, 1.0f);

        for (size_t strandIndex = 0; strandIndex < asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount < 2u)
            {
                continue;
            }

            const uint32_t offset = strand.pointOffset;
            if (offset + strand.pointCount > asset.restPoints.size())
            {
                continue;
            }

            std::vector<Vec3> originalPoints(strand.pointCount);
            for (uint32_t i = 0; i < strand.pointCount; ++i)
            {
                originalPoints[i] = asset.restPoints[offset + i];
            }

            const RootBinding& binding = asset.rootBindings[strandIndex];
            const Vec3 root = EvaluateRootBindingPosition(asset.scalpMesh, binding);
            const Vec3 rootNormal = EvaluateBindingNormal(asset.scalpMesh, binding);
            Vec3 originalFirstDir = Normalize(originalPoints[1] - originalPoints[0]);
            if (Length(originalFirstDir) <= 1.0e-6f)
            {
                originalFirstDir = Vec3(0.0f, -1.0f, 0.0f);
            }

            const Vec3 capFlow = ComputeCapFlowDirection(rootNormal, root, scalpCenter, originalFirstDir);
            const Vec3 styledRoot = root + rootNormal * shellOffset;
            asset.restPoints[offset] = Lerp(originalPoints[0], styledRoot, clampedStrength);

            Vec3 chainPoint = asset.restPoints[offset];
            const Vec3 rootRadial = ProjectOntoPlane(root - scalpCenter, rootNormal);
            Vec3 launchDir = Normalize(capFlow * flowBlend + rootRadial * (outwardBlend * 0.55f) + rootNormal * 0.34f);
            Vec3 prevDir = Normalize(Lerp(originalFirstDir, launchDir, clampedStrength));
            if (Length(prevDir) <= 1.0e-6f)
            {
                prevDir = capFlow;
            }

            for (uint32_t i = 1; i < strand.pointCount; ++i)
            {
                const float prevT = static_cast<float>(i - 1u) / static_cast<float>(strand.pointCount - 1u);
                const float t = static_cast<float>(i) / static_cast<float>(strand.pointCount - 1u);
                const float capWeight = Clamp((capRegion - prevT) / std::max(capRegion, 1.0e-5f), 0.0f, 1.0f) * clampedStrength;
                const Vec3 originalPrev = originalPoints[i - 1u];
                const Vec3 originalCurrent = originalPoints[i];
                const Vec3 originalDir = Normalize(originalCurrent - originalPrev);
                const float segmentLength = std::max(Length(originalCurrent - originalPrev), 1.0e-4f);

                const float crownRootBlend = Clamp((0.22f - prevT) / 0.22f, 0.0f, 1.0f);
                Vec3 preferredDir = Normalize(capFlow * flowBlend + rootRadial * (outwardBlend * Lerp(0.45f, 1.0f, prevT)) + rootNormal * Lerp(0.34f, 0.18f, prevT));
                if (Length(preferredDir) <= 1.0e-6f)
                {
                    preferredDir = capFlow;
                }
                Vec3 stepDir = Normalize(Lerp(originalDir, preferredDir, capWeight));
                if (Length(stepDir) <= 1.0e-6f)
                {
                    stepDir = prevDir;
                }

                const float rootStandBlend = Clamp((0.18f - prevT) / 0.18f, 0.0f, 1.0f) * clampedStrength;
                Vec3 rootStandDir = Normalize(rootNormal * 0.82f + Vec3(0.0f, -1.0f, 0.0f) * 0.18f);
                if (Length(rootStandDir) > 1.0e-6f)
                {
                    stepDir = Normalize(Lerp(stepDir, rootStandDir, rootStandBlend));
                }

                Vec3 shapedPoint = chainPoint + stepDir * segmentLength;
                shapedPoint += rootNormal * (shellOffset * 0.35f * capWeight);

                const Vec3 originalTarget = styledRoot + (originalCurrent - originalPoints[0]);
                const float restoreWeight = Clamp((t - capRegion * 0.35f) / std::max(1.0f - capRegion * 0.35f, 1.0e-5f), 0.0f, 1.0f) * tipRestore;
                shapedPoint = Lerp(shapedPoint, originalTarget, restoreWeight);

                const float localBlend = clampedStrength * Clamp((0.30f - t) / 0.30f, 0.0f, 1.0f);
                asset.restPoints[offset + i] = Lerp(originalCurrent, shapedPoint, localBlend);
                chainPoint = asset.restPoints[offset + i];
                prevDir = stepDir;
            }
        }

        return true;
    }
}
