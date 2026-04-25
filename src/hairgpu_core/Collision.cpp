#include "hairgpu/Collision.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <fstream>
#include <vector>

#include "hairgpu/Geometry.h"
#include "hairgpu/Math.h"

namespace hairgpu
{
    namespace
    {
        struct SdfGrid
        {
            uint32_t resolutionX = 0u;
            uint32_t resolutionY = 0u;
            uint32_t resolutionZ = 0u;
            Vec3 boundsMin = {};
            Vec3 boundsMax = {};
            Vec3 cellSize = { 1.0f, 1.0f, 1.0f };
            std::vector<float> values;
        };

        Vec3 ComputeScalpCenter(const ScalpMesh& scalpMesh)
        {
            if (scalpMesh.positions.empty())
            {
                return {};
            }
            Vec3 center = {};
            for (const Vec3& p : scalpMesh.positions)
            {
                center += p;
            }
            return center / static_cast<float>(scalpMesh.positions.size());
        }

        Vec3 TriangleNormalFacingOutward(const ScalpMesh& scalpMesh, const TriangleIndices& tri, const Vec3& scalpCenter)
        {
            if (tri.a >= scalpMesh.positions.size() || tri.b >= scalpMesh.positions.size() || tri.c >= scalpMesh.positions.size())
            {
                return { 0.0f, 1.0f, 0.0f };
            }
            const Vec3 a = scalpMesh.positions[tri.a];
            const Vec3 b = scalpMesh.positions[tri.b];
            const Vec3 c = scalpMesh.positions[tri.c];
            Vec3 n = Normalize(Cross(b - a, c - a));
            if (Length(n) <= 1.0e-6f)
            {
                return { 0.0f, 1.0f, 0.0f };
            }
            const Vec3 centroid = (a + b + c) / 3.0f;
            if (Dot(n, centroid - scalpCenter) < 0.0f)
            {
                n = n * -1.0f;
            }
            return n;
        }

        Vec3 EvaluateSurfaceNormal(const ScalpMesh& scalpMesh, const TriangleIndices& tri, const Vec3& barycentric, const Vec3& scalpCenter)
        {
            if (tri.a < scalpMesh.normals.size() && tri.b < scalpMesh.normals.size() && tri.c < scalpMesh.normals.size())
            {
                Vec3 n = scalpMesh.normals[tri.a] * barycentric.x +
                         scalpMesh.normals[tri.b] * barycentric.y +
                         scalpMesh.normals[tri.c] * barycentric.z;
                if (Length(n) > 1.0e-6f)
                {
                    n = Normalize(n);
                    const Vec3 triNormal = TriangleNormalFacingOutward(scalpMesh, tri, scalpCenter);
                    if (Dot(n, triNormal) < 0.0f)
                    {
                        n = n * -1.0f;
                    }
                    return n;
                }
            }
            return TriangleNormalFacingOutward(scalpMesh, tri, scalpCenter);
        }

        ClosestTrianglePoint FindClosestPointOnScalp(const ScalpMesh& scalpMesh, const Vec3& point)
        {
            ClosestTrianglePoint best = {};
            for (uint32_t triIndex = 0u; triIndex < static_cast<uint32_t>(scalpMesh.triangles.size()); ++triIndex)
            {
                const TriangleIndices& tri = scalpMesh.triangles[triIndex];
                if (tri.a >= scalpMesh.positions.size() || tri.b >= scalpMesh.positions.size() || tri.c >= scalpMesh.positions.size())
                {
                    continue;
                }
                ClosestTrianglePoint candidate = ClosestPointOnTriangle(point,
                    scalpMesh.positions[tri.a], scalpMesh.positions[tri.b], scalpMesh.positions[tri.c]);
                if (candidate.distanceSq < best.distanceSq)
                {
                    best = candidate;
                    best.triangleIndex = triIndex;
                }
            }
            return best;
        }

        bool ValidateCollisionInput(const HairAsset& asset)
        {
            return !asset.strands.empty() &&
                   !asset.restPoints.empty() &&
                   !asset.scalpMesh.positions.empty() &&
                   !asset.scalpMesh.triangles.empty();
        }

        size_t GridIndex(const SdfGrid& grid, uint32_t x, uint32_t y, uint32_t z)
        {
            return (static_cast<size_t>(z) * grid.resolutionY + y) * grid.resolutionX + x;
        }

        float SampleGridNearest(const SdfGrid& grid, int x, int y, int z)
        {
            x = std::max(0, std::min(x, static_cast<int>(grid.resolutionX) - 1));
            y = std::max(0, std::min(y, static_cast<int>(grid.resolutionY) - 1));
            z = std::max(0, std::min(z, static_cast<int>(grid.resolutionZ) - 1));
            return grid.values[GridIndex(grid, static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z))];
        }

        Vec3 GridPosition(const SdfGrid& grid, uint32_t x, uint32_t y, uint32_t z)
        {
            return {
                grid.boundsMin.x + static_cast<float>(x) * grid.cellSize.x,
                grid.boundsMin.y + static_cast<float>(y) * grid.cellSize.y,
                grid.boundsMin.z + static_cast<float>(z) * grid.cellSize.z
            };
        }

        SdfGrid BuildSdfGridFromScalpMesh(const ScalpMesh& scalpMesh, uint32_t requestedResolution, float padding)
        {
            SdfGrid grid = {};
            const uint32_t resolution = std::max<uint32_t>(requestedResolution, 8u);
            grid.resolutionX = resolution;
            grid.resolutionY = resolution;
            grid.resolutionZ = resolution;

            Vec3 boundsMin(FLT_MAX, FLT_MAX, FLT_MAX);
            Vec3 boundsMax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (const Vec3& p : scalpMesh.positions)
            {
                ExpandBounds(boundsMin, boundsMax, p);
            }
            const float pad = std::max(padding, 0.001f);
            boundsMin = boundsMin - Vec3(pad, pad, pad);
            boundsMax = boundsMax + Vec3(pad, pad, pad);
            grid.boundsMin = boundsMin;
            grid.boundsMax = boundsMax;
            grid.cellSize = {
                (boundsMax.x - boundsMin.x) / std::max(1u, resolution - 1u),
                (boundsMax.y - boundsMin.y) / std::max(1u, resolution - 1u),
                (boundsMax.z - boundsMin.z) / std::max(1u, resolution - 1u)
            };
            grid.values.resize(static_cast<size_t>(resolution) * resolution * resolution, 0.0f);

            const Vec3 scalpCenter = ComputeScalpCenter(scalpMesh);
            for (uint32_t z = 0u; z < resolution; ++z)
            {
                for (uint32_t y = 0u; y < resolution; ++y)
                {
                    for (uint32_t x = 0u; x < resolution; ++x)
                    {
                        const Vec3 samplePos = GridPosition(grid, x, y, z);
                        const ClosestTrianglePoint closest = FindClosestPointOnScalp(scalpMesh, samplePos);
                        const TriangleIndices& tri = scalpMesh.triangles[closest.triangleIndex];
                        const Vec3 normal = EvaluateSurfaceNormal(scalpMesh, tri, closest.barycentric, scalpCenter);
                        const float dist = std::sqrt(std::max(closest.distanceSq, 0.0f));
                        const float sign = (Dot(samplePos - closest.position, normal) >= 0.0f) ? 1.0f : -1.0f;
                        grid.values[GridIndex(grid, x, y, z)] = dist * sign;
                    }
                }
            }
            return grid;
        }

        SdfGrid BuildScalpSdfGrid(const HairAsset& asset, uint32_t requestedResolution, float padding)
        {
            return BuildSdfGridFromScalpMesh(asset.scalpMesh, requestedResolution, padding);
        }

        float SampleGridTrilinear(const SdfGrid& grid, const Vec3& point)
        {
            const float gx = (point.x - grid.boundsMin.x) / std::max(grid.cellSize.x, 1.0e-6f);
            const float gy = (point.y - grid.boundsMin.y) / std::max(grid.cellSize.y, 1.0e-6f);
            const float gz = (point.z - grid.boundsMin.z) / std::max(grid.cellSize.z, 1.0e-6f);

            const int x0 = static_cast<int>(std::floor(gx));
            const int y0 = static_cast<int>(std::floor(gy));
            const int z0 = static_cast<int>(std::floor(gz));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;
            const int z1 = z0 + 1;
            const float tx = gx - std::floor(gx);
            const float ty = gy - std::floor(gy);
            const float tz = gz - std::floor(gz);

            const float c000 = SampleGridNearest(grid, x0, y0, z0);
            const float c100 = SampleGridNearest(grid, x1, y0, z0);
            const float c010 = SampleGridNearest(grid, x0, y1, z0);
            const float c110 = SampleGridNearest(grid, x1, y1, z0);
            const float c001 = SampleGridNearest(grid, x0, y0, z1);
            const float c101 = SampleGridNearest(grid, x1, y0, z1);
            const float c011 = SampleGridNearest(grid, x0, y1, z1);
            const float c111 = SampleGridNearest(grid, x1, y1, z1);

            return TriLerp(c000, c100,
                           c010, c110,
                           c001, c101,
                           c011, c111,
                           tx, ty, tz);
        }

        Vec3 EstimateGridGradient(const SdfGrid& grid, const Vec3& point)
        {
            const Vec3 dx = { std::max(grid.cellSize.x, 1.0e-4f), 0.0f, 0.0f };
            const Vec3 dy = { 0.0f, std::max(grid.cellSize.y, 1.0e-4f), 0.0f };
            const Vec3 dz = { 0.0f, 0.0f, std::max(grid.cellSize.z, 1.0e-4f) };
            Vec3 g = {
                SampleGridTrilinear(grid, point + dx) - SampleGridTrilinear(grid, point - dx),
                SampleGridTrilinear(grid, point + dy) - SampleGridTrilinear(grid, point - dy),
                SampleGridTrilinear(grid, point + dz) - SampleGridTrilinear(grid, point - dz)
            };
            return Normalize(g);
        }

        void FinalizeReport(HairCollisionReport* outReport, HairCollisionReport report)
        {
            if (outReport != nullptr)
            {
                if (report.affectedPointCount > 0u)
                {
                    report.averagePenetration /= static_cast<float>(report.affectedPointCount);
                }
                *outReport = report;
            }
        }
    }

    bool ApplyScalpMeshCollision(HairAsset& asset,
                                 const HairCollisionSettings& settings,
                                 HairCollisionReport* outReport)
    {
        if (!ValidateCollisionInput(asset))
        {
            return false;
        }

        const Vec3 scalpCenter = ComputeScalpCenter(asset.scalpMesh);
        HairCollisionReport report = {};
        const float shell = std::max(settings.shellDistance, 0.0f);
        const float stiffness = Clamp(settings.stiffness, 0.0f, 1.0f);

        for (uint32_t iteration = 0u; iteration < std::max<uint32_t>(1u, settings.iterations); ++iteration)
        {
            for (size_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                const StrandRange& strand = asset.strands[strandIndex];
                if (strand.pointCount == 0u)
                {
                    continue;
                }
                const uint32_t startPoint = settings.affectAllPoints ? 0u : (strand.pointCount > 1u ? strand.pointCount - 1u : 0u);
                for (uint32_t localIndex = startPoint; localIndex < strand.pointCount; ++localIndex)
                {
                    if (!settings.affectRootPoints && localIndex == 0u)
                    {
                        continue;
                    }
                    Vec3& point = asset.restPoints[strand.pointOffset + localIndex];
                    const ClosestTrianglePoint closest = FindClosestPointOnScalp(asset.scalpMesh, point);
                    const TriangleIndices& tri = asset.scalpMesh.triangles[closest.triangleIndex];
                    const Vec3 normal = EvaluateSurfaceNormal(asset.scalpMesh, tri, closest.barycentric, scalpCenter);
                    const float signedDistance = Dot(point - closest.position, normal);
                    if (signedDistance < shell)
                    {
                        const float penetration = shell - signedDistance;
                        point += normal * (penetration * stiffness);
                        ++report.affectedPointCount;
                        report.averagePenetration += penetration;
                        report.maxPenetration = std::max(report.maxPenetration, penetration);
                    }
                }
            }
        }

        FinalizeReport(outReport, report);
        return true;
    }

    bool ApplyScalpSdfCollision(HairAsset& asset,
                                const HairCollisionSettings& settings,
                                HairCollisionReport* outReport)
    {
        if (!ValidateCollisionInput(asset))
        {
            return false;
        }

        const SdfGrid grid = BuildScalpSdfGrid(asset,
                                              std::max<uint32_t>(settings.sdfResolution, 8u),
                                              std::max(settings.sdfPadding, settings.shellDistance + 0.01f));
        HairCollisionReport report = {};
        const float shell = std::max(settings.shellDistance, 0.0f);
        const float stiffness = Clamp(settings.stiffness, 0.0f, 1.0f);

        for (uint32_t iteration = 0u; iteration < std::max<uint32_t>(1u, settings.iterations); ++iteration)
        {
            for (size_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                const StrandRange& strand = asset.strands[strandIndex];
                if (strand.pointCount == 0u)
                {
                    continue;
                }
                const uint32_t startPoint = settings.affectAllPoints ? 0u : (strand.pointCount > 1u ? strand.pointCount - 1u : 0u);
                for (uint32_t localIndex = startPoint; localIndex < strand.pointCount; ++localIndex)
                {
                    if (!settings.affectRootPoints && localIndex == 0u)
                    {
                        continue;
                    }
                    Vec3& point = asset.restPoints[strand.pointOffset + localIndex];
                    const float sdf = SampleGridTrilinear(grid, point);
                    if (sdf < shell)
                    {
                        Vec3 gradient = EstimateGridGradient(grid, point);
                        if (Length(gradient) <= 1.0e-6f)
                        {
                            gradient = { 0.0f, 1.0f, 0.0f };
                        }
                        const float penetration = shell - sdf;
                        point += gradient * (penetration * stiffness);
                        ++report.affectedPointCount;
                        report.averagePenetration += penetration;
                        report.maxPenetration = std::max(report.maxPenetration, penetration);
                    }
                }
            }
        }

        FinalizeReport(outReport, report);
        return true;
    }

    bool ExportScalpSdfSlicesCsv(const HairAsset& asset,
                                 const std::filesystem::path& filePath,
                                 uint32_t resolution,
                                 float padding,
                                 std::string& outError)
    {
        if (!ValidateCollisionInput(asset))
        {
            outError = "Asset does not contain a valid scalp mesh.";
            return false;
        }

        const SdfGrid grid = BuildScalpSdfGrid(asset, resolution, padding);
        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to open output SDF CSV: " + filePath.string();
            return false;
        }

        out << "x,y,z,worldX,worldY,worldZ,sdf\n";
        for (uint32_t z = 0u; z < grid.resolutionZ; ++z)
        {
            for (uint32_t y = 0u; y < grid.resolutionY; ++y)
            {
                for (uint32_t x = 0u; x < grid.resolutionX; ++x)
                {
                    const Vec3 p = GridPosition(grid, x, y, z);
                    out << x << ',' << y << ',' << z << ','
                        << p.x << ',' << p.y << ',' << p.z << ','
                        << grid.values[GridIndex(grid, x, y, z)] << '\n';
                }
            }
        }

        outError.clear();
        return true;
    }

    bool BakeSdfGridFromScalpMesh(const ScalpMesh& scalpMesh,
                                  uint32_t resolution,
                                  float padding,
                                  SdfGridCollider& outGrid,
                                  std::string& outError)
    {
        if (scalpMesh.positions.empty() || scalpMesh.triangles.empty())
        {
            outError = "Scalp mesh does not contain any triangle data.";
            return false;
        }
        const SdfGrid grid = BuildSdfGridFromScalpMesh(scalpMesh, resolution, padding);
        outGrid = {};
        outGrid.resolutionX = grid.resolutionX;
        outGrid.resolutionY = grid.resolutionY;
        outGrid.resolutionZ = grid.resolutionZ;
        outGrid.boundsMin = grid.boundsMin;
        outGrid.boundsMax = grid.boundsMax;
        outGrid.values = grid.values;
        outError.clear();
        return true;
    }

    bool ExportSdfGridColliderCsv(const SdfGridCollider& grid,
                                  const std::filesystem::path& filePath,
                                  std::string& outError)
    {
        const size_t expectedCount = static_cast<size_t>(grid.resolutionX) * grid.resolutionY * grid.resolutionZ;
        if (grid.resolutionX < 2u || grid.resolutionY < 2u || grid.resolutionZ < 2u || grid.values.size() != expectedCount)
        {
            outError = "SDF grid collider is invalid or incomplete.";
            return false;
        }

        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to open output SDF collider CSV: " + filePath.string();
            return false;
        }

        out << "resolution," << grid.resolutionX << ',' << grid.resolutionY << ',' << grid.resolutionZ << "\n";
        out << "boundsMin," << grid.boundsMin.x << ',' << grid.boundsMin.y << ',' << grid.boundsMin.z << "\n";
        out << "boundsMax," << grid.boundsMax.x << ',' << grid.boundsMax.y << ',' << grid.boundsMax.z << "\n";
        out << "values";
        for (float value : grid.values)
        {
            out << ',' << value;
        }
        out << "\n";

        outError.clear();
        return true;
    }

    SignedDistanceSample QuerySignedDistanceToSphere(const SphereCollider& sphere, const Vec3& p)
    {
        const Vec3 delta = p - sphere.center;
        const float len = Length(delta);
        SignedDistanceSample sample = {};
        sample.signedDistance = len - sphere.radius;
        sample.normal = (len > 1.0e-6f) ? (delta / len) : Vec3(0.0f, 1.0f, 0.0f);
        sample.colliderType = 0u;
        return sample;
    }

    SignedDistanceSample QuerySignedDistanceToCapsule(const CapsuleCollider& capsule, const Vec3& p)
    {
        const CapsuleProjection projection = ProjectToCapsule(capsule, p);
        SignedDistanceSample sample = {};
        sample.signedDistance = projection.distance - projection.radius;
        sample.normal = projection.normal;
        sample.colliderType = 1u;
        return sample;
    }

    SignedDistanceSample QuerySignedDistanceToPlane(const PlaneCollider& plane, const Vec3& p)
    {
        const Vec3 n = (Length(plane.normal) > 1.0e-6f) ? Normalize(plane.normal) : Vec3(0.0f, 1.0f, 0.0f);
        SignedDistanceSample sample = {};
        sample.signedDistance = Dot(p - plane.point, n);
        sample.normal = n;
        sample.colliderType = 2u;
        return sample;
    }

    SignedDistanceSample QuerySignedDistanceToAabb(const AabbCollider& box, const Vec3& p)
    {
        const Vec3 clamped{
            Clamp(p.x, box.minBounds.x, box.maxBounds.x),
            Clamp(p.y, box.minBounds.y, box.maxBounds.y),
            Clamp(p.z, box.minBounds.z, box.maxBounds.z)
        };
        const Vec3 delta = p - clamped;
        const float outsideLen = Length(delta);
        SignedDistanceSample sample = {};
        sample.colliderType = 3u;
        if (outsideLen > 1.0e-6f)
        {
            sample.signedDistance = outsideLen;
            sample.normal = delta / outsideLen;
            return sample;
        }

        const float dxMin = std::fabs(p.x - box.minBounds.x);
        const float dxMax = std::fabs(box.maxBounds.x - p.x);
        const float dyMin = std::fabs(p.y - box.minBounds.y);
        const float dyMax = std::fabs(box.maxBounds.y - p.y);
        const float dzMin = std::fabs(p.z - box.minBounds.z);
        const float dzMax = std::fabs(box.maxBounds.z - p.z);
        float minDist = dxMin;
        sample.normal = Vec3(-1.0f, 0.0f, 0.0f);
        if (dxMax < minDist) { minDist = dxMax; sample.normal = Vec3(1.0f, 0.0f, 0.0f); }
        if (dyMin < minDist) { minDist = dyMin; sample.normal = Vec3(0.0f, -1.0f, 0.0f); }
        if (dyMax < minDist) { minDist = dyMax; sample.normal = Vec3(0.0f, 1.0f, 0.0f); }
        if (dzMin < minDist) { minDist = dzMin; sample.normal = Vec3(0.0f, 0.0f, -1.0f); }
        if (dzMax < minDist) { minDist = dzMax; sample.normal = Vec3(0.0f, 0.0f, 1.0f); }
        sample.signedDistance = -minDist;
        return sample;
    }

    SignedDistanceSample QuerySignedDistanceToSdfGrid(const SdfGridCollider& grid, const Vec3& p)
    {
        SignedDistanceSample sample = {};
        sample.colliderType = 4u;
        if (grid.resolutionX == 0u || grid.resolutionY == 0u || grid.resolutionZ == 0u || grid.values.empty())
        {
            sample.signedDistance = FLT_MAX;
            return sample;
        }

        const Vec3 extents = grid.boundsMax - grid.boundsMin;
        const Vec3 safeExtents(std::max(extents.x, 1.0e-6f), std::max(extents.y, 1.0e-6f), std::max(extents.z, 1.0e-6f));
        const Vec3 uvw = {
            Clamp((p.x - grid.boundsMin.x) / safeExtents.x, 0.0f, 1.0f),
            Clamp((p.y - grid.boundsMin.y) / safeExtents.y, 0.0f, 1.0f),
            Clamp((p.z - grid.boundsMin.z) / safeExtents.z, 0.0f, 1.0f)
        };
        const float gx = uvw.x * static_cast<float>(std::max(grid.resolutionX - 1u, 1u));
        const float gy = uvw.y * static_cast<float>(std::max(grid.resolutionY - 1u, 1u));
        const float gz = uvw.z * static_cast<float>(std::max(grid.resolutionZ - 1u, 1u));
        const uint32_t x0 = std::min<uint32_t>(static_cast<uint32_t>(std::floor(gx)), grid.resolutionX - 1u);
        const uint32_t y0 = std::min<uint32_t>(static_cast<uint32_t>(std::floor(gy)), grid.resolutionY - 1u);
        const uint32_t z0 = std::min<uint32_t>(static_cast<uint32_t>(std::floor(gz)), grid.resolutionZ - 1u);
        const uint32_t x1 = std::min<uint32_t>(x0 + 1u, grid.resolutionX - 1u);
        const uint32_t y1 = std::min<uint32_t>(y0 + 1u, grid.resolutionY - 1u);
        const uint32_t z1 = std::min<uint32_t>(z0 + 1u, grid.resolutionZ - 1u);
        const float tx = gx - static_cast<float>(x0);
        const float ty = gy - static_cast<float>(y0);
        const float tz = gz - static_cast<float>(z0);
        auto idx = [&](uint32_t x, uint32_t y, uint32_t z) -> size_t { return (static_cast<size_t>(z) * grid.resolutionY + y) * grid.resolutionX + x; };
        const float c000 = grid.values[idx(x0,y0,z0)];
        const float c100 = grid.values[idx(x1,y0,z0)];
        const float c010 = grid.values[idx(x0,y1,z0)];
        const float c110 = grid.values[idx(x1,y1,z0)];
        const float c001 = grid.values[idx(x0,y0,z1)];
        const float c101 = grid.values[idx(x1,y0,z1)];
        const float c011 = grid.values[idx(x0,y1,z1)];
        const float c111 = grid.values[idx(x1,y1,z1)];
        sample.signedDistance = TriLerp(c000, c100,
                                        c010, c110,
                                        c001, c101,
                                        c011, c111,
                                        tx, ty, tz);
        sample.normal = Vec3(0.0f, 1.0f, 0.0f);
        return sample;
    }

    SignedDistanceSample QueryClosestColliderDistance(const ColliderSet& colliders, const Vec3& p)
    {
        SignedDistanceSample best = {};
        best.signedDistance = FLT_MAX;

        auto consider = [&](SignedDistanceSample s, uint32_t index)
        {
            s.colliderIndex = index;
            if (s.signedDistance < best.signedDistance)
            {
                best = s;
            }
        };

        for (uint32_t i = 0u; i < static_cast<uint32_t>(colliders.spheres.size()); ++i)
            consider(QuerySignedDistanceToSphere(colliders.spheres[i], p), i);
        for (uint32_t i = 0u; i < static_cast<uint32_t>(colliders.capsules.size()); ++i)
            consider(QuerySignedDistanceToCapsule(colliders.capsules[i], p), i);
        for (uint32_t i = 0u; i < static_cast<uint32_t>(colliders.planes.size()); ++i)
            consider(QuerySignedDistanceToPlane(colliders.planes[i], p), i);
        for (uint32_t i = 0u; i < static_cast<uint32_t>(colliders.aabbs.size()); ++i)
            consider(QuerySignedDistanceToAabb(colliders.aabbs[i], p), i);
        for (uint32_t i = 0u; i < static_cast<uint32_t>(colliders.sdfGrids.size()); ++i)
            consider(QuerySignedDistanceToSdfGrid(colliders.sdfGrids[i], p), i);

        return best;
    }

}
