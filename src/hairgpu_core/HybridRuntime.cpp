#include "hairgpu/HybridRuntime.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

#include "hairgpu/AssetIO.h"
#include "hairgpu/GuideCache.h"
#include "hairgpu/HairCache.h"
#include "hairgpu/ReducedModel.h"

namespace hairgpu
{
    namespace
    {
        constexpr float kPi = 3.1415926535f;

        struct PenetrationResult
        {
            Vec3 correctedPoint = { 0.0f, 0.0f, 0.0f };
            float penetration = 0.0f;
            bool corrected = false;
        };

        float ComputeAverageScalar(const std::vector<float>& values);
        float ComputeMaxScalar(const std::vector<float>& values);

        Vec3 ClosestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p)
        {
            const Vec3 ab = b - a;
            const float denom = std::max(Dot(ab, ab), 1.0e-8f);
            const float t = Clamp(Dot(p - a, ab) / denom, 0.0f, 1.0f);
            return a + ab * t;
        }

        float StrandSegmentRestLength(const HairAsset& asset, uint32_t strandIndex, uint32_t pointIndex)
        {
            if (strandIndex >= asset.strands.size())
            {
                return 0.0f;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            if (pointIndex == 0u || pointIndex >= strand.pointCount)
            {
                return 0.0f;
            }
            const uint32_t prevIndex = strand.pointOffset + pointIndex - 1u;
            const uint32_t currIndex = strand.pointOffset + pointIndex;
            if (currIndex >= asset.restPoints.size() || prevIndex >= asset.restPoints.size())
            {
                return 0.0f;
            }
            return Length(asset.restPoints[currIndex] - asset.restPoints[prevIndex]);
        }

        PenetrationResult ResolveAgainstFloor(const Vec3& point, float floorY, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const float floorLimit = floorY + shellDistance;
            if (point.y < floorLimit)
            {
                result.penetration = floorLimit - point.y;
                result.correctedPoint.y = floorLimit;
                result.corrected = true;
            }
            return result;
        }

        PenetrationResult ResolveAgainstSphere(const Vec3& point, const SphereCollider& sphere, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const Vec3 delta = point - sphere.center;
            const float distance = Length(delta);
            const float targetDistance = sphere.radius + shellDistance;
            if (distance < targetDistance)
            {
                const Vec3 normal = (distance > 1.0e-6f) ? (delta / distance) : Vec3(0.0f, 1.0f, 0.0f);
                result.penetration = targetDistance - distance;
                result.correctedPoint = sphere.center + normal * targetDistance;
                result.corrected = true;
            }
            return result;
        }

        PenetrationResult ResolveAgainstCapsule(const Vec3& point, const CapsuleCollider& capsule, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const Vec3 closest = ClosestPointOnSegment(capsule.pointA, capsule.pointB, point);
            const Vec3 axis = capsule.pointB - capsule.pointA;
            const float axisDenom = std::max(Dot(axis, axis), 1.0e-8f);
            const float t = Clamp(Dot(closest - capsule.pointA, axis) / axisDenom, 0.0f, 1.0f);
            const float capsuleRadius = Lerp(capsule.radiusA, capsule.radiusB, t) + shellDistance;
            const Vec3 delta = point - closest;
            const float distance = Length(delta);
            if (distance < capsuleRadius)
            {
                const Vec3 normal = (distance > 1.0e-6f) ? (delta / distance) : Vec3(0.0f, 1.0f, 0.0f);
                result.penetration = capsuleRadius - distance;
                result.correctedPoint = closest + normal * capsuleRadius;
                result.corrected = true;
            }
            return result;
        }

        bool HasAnyCollider(const ColliderSet& colliders)
        {
            return !colliders.spheres.empty() ||
                   !colliders.capsules.empty() ||
                   !colliders.planes.empty() ||
                   !colliders.aabbs.empty() ||
                   !colliders.sdfGrids.empty();
        }

        float SignedDistanceToSphere(const Vec3& point, const SphereCollider& sphere)
        {
            return Length(point - sphere.center) - sphere.radius;
        }

        float SignedDistanceToCapsule(const Vec3& point, const CapsuleCollider& capsule)
        {
            const Vec3 closest = ClosestPointOnSegment(capsule.pointA, capsule.pointB, point);
            const Vec3 axis = capsule.pointB - capsule.pointA;
            const float axisDenom = std::max(Dot(axis, axis), 1.0e-8f);
            const float t = Clamp(Dot(closest - capsule.pointA, axis) / axisDenom, 0.0f, 1.0f);
            const float radius = Lerp(capsule.radiusA, capsule.radiusB, t);
            return Length(point - closest) - radius;
        }

        float SignedDistanceToPlane(const Vec3& point, const PlaneCollider& plane)
        {
            const Vec3 normal = Normalize(plane.normal);
            const Vec3 safeNormal = (Length(normal) > 1.0e-6f) ? normal : Vec3(0.0f, 1.0f, 0.0f);
            return Dot(point - plane.point, safeNormal);
        }

        Vec3 ClosestPointOnAabb(const Vec3& point, const AabbCollider& box)
        {
            return Vec3(
                Clamp(point.x, std::min(box.minBounds.x, box.maxBounds.x), std::max(box.minBounds.x, box.maxBounds.x)),
                Clamp(point.y, std::min(box.minBounds.y, box.maxBounds.y), std::max(box.minBounds.y, box.maxBounds.y)),
                Clamp(point.z, std::min(box.minBounds.z, box.maxBounds.z), std::max(box.minBounds.z, box.maxBounds.z)));
        }

        float SignedDistanceToAabb(const Vec3& point, const AabbCollider& box)
        {
            const Vec3 minB(
                std::min(box.minBounds.x, box.maxBounds.x),
                std::min(box.minBounds.y, box.maxBounds.y),
                std::min(box.minBounds.z, box.maxBounds.z));
            const Vec3 maxB(
                std::max(box.minBounds.x, box.maxBounds.x),
                std::max(box.minBounds.y, box.maxBounds.y),
                std::max(box.minBounds.z, box.maxBounds.z));
            const Vec3 center = (minB + maxB) * 0.5f;
            const Vec3 halfExtents = (maxB - minB) * 0.5f;
            const Vec3 local = point - center;
            const Vec3 q(
                std::fabs(local.x) - halfExtents.x,
                std::fabs(local.y) - halfExtents.y,
                std::fabs(local.z) - halfExtents.z);
            const Vec3 outside(
                std::max(q.x, 0.0f),
                std::max(q.y, 0.0f),
                std::max(q.z, 0.0f));
            const float outsideDistance = Length(outside);
            const float insideDistance = std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
            return outsideDistance + insideDistance;
        }

        PenetrationResult ResolveAgainstPlane(const Vec3& point, const PlaneCollider& plane, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const Vec3 normal = Normalize(plane.normal);
            const Vec3 safeNormal = (Length(normal) > 1.0e-6f) ? normal : Vec3(0.0f, 1.0f, 0.0f);
            const float signedDistance = Dot(point - plane.point, safeNormal);
            if (signedDistance < shellDistance)
            {
                result.penetration = shellDistance - signedDistance;
                result.correctedPoint = point + safeNormal * result.penetration;
                result.corrected = true;
            }
            return result;
        }

        PenetrationResult ResolveAgainstAabb(const Vec3& point, const AabbCollider& box, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const float signedDistance = SignedDistanceToAabb(point, box);
            if (signedDistance >= shellDistance)
            {
                return result;
            }

            Vec3 closest = ClosestPointOnAabb(point, box);
            Vec3 delta = point - closest;
            Vec3 normal = Normalize(delta);
            if (Length(normal) <= 1.0e-6f)
            {
                const Vec3 minB(
                    std::min(box.minBounds.x, box.maxBounds.x),
                    std::min(box.minBounds.y, box.maxBounds.y),
                    std::min(box.minBounds.z, box.maxBounds.z));
                const Vec3 maxB(
                    std::max(box.minBounds.x, box.maxBounds.x),
                    std::max(box.minBounds.y, box.maxBounds.y),
                    std::max(box.minBounds.z, box.maxBounds.z));
                const float dx = std::min(std::fabs(point.x - minB.x), std::fabs(maxB.x - point.x));
                const float dy = std::min(std::fabs(point.y - minB.y), std::fabs(maxB.y - point.y));
                const float dz = std::min(std::fabs(point.z - minB.z), std::fabs(maxB.z - point.z));
                if (dx <= dy && dx <= dz)
                {
                    normal = Vec3((point.x - (minB.x + maxB.x) * 0.5f) >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
                }
                else if (dy <= dz)
                {
                    normal = Vec3(0.0f, (point.y - (minB.y + maxB.y) * 0.5f) >= 0.0f ? 1.0f : -1.0f, 0.0f);
                }
                else
                {
                    normal = Vec3(0.0f, 0.0f, (point.z - (minB.z + maxB.z) * 0.5f) >= 0.0f ? 1.0f : -1.0f);
                }
            }

            result.penetration = shellDistance - signedDistance;
            result.correctedPoint = point + normal * result.penetration;
            result.corrected = true;
            return result;
        }

        bool IsValidSdfGrid(const SdfGridCollider& grid)
        {
            return grid.resolutionX >= 2u &&
                   grid.resolutionY >= 2u &&
                   grid.resolutionZ >= 2u &&
                   grid.values.size() == static_cast<size_t>(grid.resolutionX) * grid.resolutionY * grid.resolutionZ;
        }

        size_t SdfGridIndex(const SdfGridCollider& grid, uint32_t x, uint32_t y, uint32_t z)
        {
            return (static_cast<size_t>(z) * grid.resolutionY + y) * grid.resolutionX + x;
        }

        Vec3 SdfGridCellSize(const SdfGridCollider& grid)
        {
            return Vec3(
                (grid.boundsMax.x - grid.boundsMin.x) / std::max(1u, grid.resolutionX - 1u),
                (grid.boundsMax.y - grid.boundsMin.y) / std::max(1u, grid.resolutionY - 1u),
                (grid.boundsMax.z - grid.boundsMin.z) / std::max(1u, grid.resolutionZ - 1u));
        }

        float SampleSdfGrid(const SdfGridCollider& grid, const Vec3& point)
        {
            if (!IsValidSdfGrid(grid))
            {
                return std::numeric_limits<float>::max();
            }

            const Vec3 cellSize = SdfGridCellSize(grid);
            const float gx = Clamp((point.x - grid.boundsMin.x) / std::max(cellSize.x, 1.0e-6f), 0.0f, static_cast<float>(grid.resolutionX - 1u));
            const float gy = Clamp((point.y - grid.boundsMin.y) / std::max(cellSize.y, 1.0e-6f), 0.0f, static_cast<float>(grid.resolutionY - 1u));
            const float gz = Clamp((point.z - grid.boundsMin.z) / std::max(cellSize.z, 1.0e-6f), 0.0f, static_cast<float>(grid.resolutionZ - 1u));

            const uint32_t x0 = static_cast<uint32_t>(std::floor(gx));
            const uint32_t y0 = static_cast<uint32_t>(std::floor(gy));
            const uint32_t z0 = static_cast<uint32_t>(std::floor(gz));
            const uint32_t x1 = std::min(x0 + 1u, grid.resolutionX - 1u);
            const uint32_t y1 = std::min(y0 + 1u, grid.resolutionY - 1u);
            const uint32_t z1 = std::min(z0 + 1u, grid.resolutionZ - 1u);
            const float tx = gx - static_cast<float>(x0);
            const float ty = gy - static_cast<float>(y0);
            const float tz = gz - static_cast<float>(z0);

            auto at = [&](uint32_t x, uint32_t y, uint32_t z) -> float
            {
                return grid.values[SdfGridIndex(grid, x, y, z)];
            };

            const float c000 = at(x0, y0, z0);
            const float c100 = at(x1, y0, z0);
            const float c010 = at(x0, y1, z0);
            const float c110 = at(x1, y1, z0);
            const float c001 = at(x0, y0, z1);
            const float c101 = at(x1, y0, z1);
            const float c011 = at(x0, y1, z1);
            const float c111 = at(x1, y1, z1);

            const float c00 = Lerp(c000, c100, tx);
            const float c10 = Lerp(c010, c110, tx);
            const float c01 = Lerp(c001, c101, tx);
            const float c11 = Lerp(c011, c111, tx);
            const float c0 = Lerp(c00, c10, ty);
            const float c1 = Lerp(c01, c11, ty);
            return Lerp(c0, c1, tz);
        }

        Vec3 EstimateSdfGradient(const SdfGridCollider& grid, const Vec3& point)
        {
            const Vec3 cellSize = SdfGridCellSize(grid);
            const float epsX = std::max(cellSize.x, 1.0e-4f);
            const float epsY = std::max(cellSize.y, 1.0e-4f);
            const float epsZ = std::max(cellSize.z, 1.0e-4f);
            const float dx = SampleSdfGrid(grid, point + Vec3(epsX, 0.0f, 0.0f)) - SampleSdfGrid(grid, point - Vec3(epsX, 0.0f, 0.0f));
            const float dy = SampleSdfGrid(grid, point + Vec3(0.0f, epsY, 0.0f)) - SampleSdfGrid(grid, point - Vec3(0.0f, epsY, 0.0f));
            const float dz = SampleSdfGrid(grid, point + Vec3(0.0f, 0.0f, epsZ)) - SampleSdfGrid(grid, point - Vec3(0.0f, 0.0f, epsZ));
            Vec3 gradient(dx, dy, dz);
            gradient = Normalize(gradient);
            return (Length(gradient) > 1.0e-6f) ? gradient : Vec3(0.0f, 1.0f, 0.0f);
        }

        PenetrationResult ResolveAgainstSdfGrid(const Vec3& point, const SdfGridCollider& grid, float shellDistance)
        {
            PenetrationResult result = {};
            result.correctedPoint = point;
            const float signedDistance = SampleSdfGrid(grid, point);
            if (signedDistance >= shellDistance || signedDistance == std::numeric_limits<float>::max())
            {
                return result;
            }

            const Vec3 normal = EstimateSdfGradient(grid, point);
            result.penetration = shellDistance - signedDistance;
            result.correctedPoint = point + normal * result.penetration;
            result.corrected = true;
            return result;
        }

        float ComputeContactScoreFromSignedDistance(float signedDistance, float shellDistance, float bandScale)
        {
            const float contactBand = std::max(shellDistance * bandScale, 1.0e-4f);
            return Clamp((contactBand - signedDistance) / contactBand, 0.0f, 1.0f);
        }

        float ComputeColliderContactScore(const Vec3& point, const ColliderSet& colliders, float floorY, float shellDistance, bool includeFloor)
        {
            float bestScore = 0.0f;
            if (includeFloor)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(point.y - floorY, shellDistance, 2.0f));
            }
            for (const SphereCollider& sphere : colliders.spheres)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SignedDistanceToSphere(point, sphere), shellDistance, 3.0f));
            }
            for (const CapsuleCollider& capsule : colliders.capsules)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SignedDistanceToCapsule(point, capsule), shellDistance, 3.5f));
            }
            for (const PlaneCollider& plane : colliders.planes)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SignedDistanceToPlane(point, plane), shellDistance, 2.0f));
            }
            for (const AabbCollider& box : colliders.aabbs)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SignedDistanceToAabb(point, box), shellDistance, 3.0f));
            }
            for (const SdfGridCollider& grid : colliders.sdfGrids)
            {
                bestScore = std::max(bestScore, ComputeContactScoreFromSignedDistance(SampleSdfGrid(grid, point), shellDistance, 3.0f));
            }
            return Clamp(bestScore, 0.0f, 1.0f);
        }

        PenetrationResult ResolveAgainstColliderSet(const Vec3& point,
                                                    const ColliderSet& colliders,
                                                    float shellDistance,
                                                    bool affectFloor,
                                                    float floorY)
        {
            PenetrationResult best = {};
            best.correctedPoint = point;
            if (affectFloor)
            {
                PenetrationResult floorResult = ResolveAgainstFloor(point, floorY, shellDistance);
                if (floorResult.penetration > best.penetration)
                {
                    best = floorResult;
                }
            }
            for (const SphereCollider& sphere : colliders.spheres)
            {
                PenetrationResult result = ResolveAgainstSphere(point, sphere, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            for (const CapsuleCollider& capsule : colliders.capsules)
            {
                PenetrationResult result = ResolveAgainstCapsule(point, capsule, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            for (const PlaneCollider& plane : colliders.planes)
            {
                PenetrationResult result = ResolveAgainstPlane(point, plane, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            for (const AabbCollider& box : colliders.aabbs)
            {
                PenetrationResult result = ResolveAgainstAabb(point, box, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            for (const SdfGridCollider& grid : colliders.sdfGrids)
            {
                PenetrationResult result = ResolveAgainstSdfGrid(point, grid, shellDistance);
                if (result.penetration > best.penetration)
                {
                    best = result;
                }
            }
            return best;
        }

        ColliderSet BuildFallbackTeacherColliders(const HairAsset& asset)
        {
            ColliderSet colliders;
            if (asset.restPoints.empty())
            {
                return colliders;
            }

            Vec3 minP = asset.restPoints.front();
            Vec3 maxP = asset.restPoints.front();
            for (const Vec3& p : asset.restPoints)
            {
                minP.x = std::min(minP.x, p.x);
                minP.y = std::min(minP.y, p.y);
                minP.z = std::min(minP.z, p.z);
                maxP.x = std::max(maxP.x, p.x);
                maxP.y = std::max(maxP.y, p.y);
                maxP.z = std::max(maxP.z, p.z);
            }
            const Vec3 center = (minP + maxP) * 0.5f;
            const float height = std::max(maxP.y - minP.y, 0.2f);
            const float width = std::max(maxP.x - minP.x, 0.12f);
            const float depth = std::max(maxP.z - minP.z, 0.12f);

            SphereCollider head = {};
            head.center = Vec3(center.x, minP.y + height * 0.70f, center.z + depth * 0.06f);
            head.radius = std::max({ width, depth, height * 0.55f }) * 0.34f;
            colliders.spheres.push_back(head);

            CapsuleCollider shoulder = {};
            shoulder.pointA = Vec3(center.x - width * 0.30f, minP.y + height * 0.24f, center.z + depth * 0.08f);
            shoulder.pointB = Vec3(center.x + width * 0.30f, minP.y + height * 0.24f, center.z + depth * 0.08f);
            shoulder.radiusA = width * 0.16f;
            shoulder.radiusB = width * 0.16f;
            colliders.capsules.push_back(shoulder);

            AabbCollider torso = {};
            torso.minBounds = Vec3(center.x - width * 0.22f, minP.y + height * 0.02f, center.z - depth * 0.10f);
            torso.maxBounds = Vec3(center.x + width * 0.22f, minP.y + height * 0.36f, center.z + depth * 0.16f);
            colliders.aabbs.push_back(torso);

            PlaneCollider floor = {};
            floor.point = Vec3(0.0f, minP.y - std::max(height * 0.12f, 0.06f), 0.0f);
            floor.normal = Vec3(0.0f, 1.0f, 0.0f);
            colliders.planes.push_back(floor);
            return colliders;
        }

        void BuildProxyFramePoints(const HairAsset& asset,
                                   float timeSeconds,
                                   float swayAmplitude,
                                   float tipNoiseAmplitude,
                                   float windCyclesPerSecond,
                                   std::vector<Vec3>& outPoints)
        {
            outPoints = asset.restPoints;
            const float basePhase = timeSeconds * 2.0f * kPi * windCyclesPerSecond;
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                const StrandRange& strand = asset.strands[strandIndex];
                if (strand.pointCount == 0u)
                {
                    continue;
                }
                const float strandPhase = static_cast<float>(strand.groupId) * 0.41f + static_cast<float>(strandIndex) * 0.083f;
                const float strandBias = std::sin(basePhase * 0.47f + strandPhase * 1.3f);
                for (uint32_t localPoint = 0u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= outPoints.size())
                    {
                        continue;
                    }

                    const float t = (strand.pointCount > 1u) ? (static_cast<float>(localPoint) / static_cast<float>(strand.pointCount - 1u)) : 0.0f;
                    if (localPoint == 0u)
                    {
                        continue;
                    }

                    const float sway = std::sin(basePhase + strandPhase + t * 0.85f);
                    const float twist = std::cos(basePhase * 1.17f + strandPhase * 1.7f + t * 1.3f);
                    const float gust = std::sin(basePhase * 0.63f + outPoints[pointIndex].z * 5.5f - t * 1.1f);
                    const float contactPulse = std::max(0.0f, std::sin(basePhase * 0.32f + outPoints[pointIndex].x * 10.0f + t * 0.8f));
                    const float tipScale = std::pow(t, 1.15f);

                    Vec3 offset = {};
                    offset.x = swayAmplitude * tipScale * (0.75f * sway + 0.25f * strandBias);
                    offset.y = -swayAmplitude * tipScale * 0.22f * std::fabs(gust) - swayAmplitude * tipScale * 0.12f * contactPulse;
                    offset.z = swayAmplitude * tipScale * (0.55f * twist + 0.35f * gust);

                    const float noise = tipNoiseAmplitude * tipScale;
                    offset.x += noise * std::sin(basePhase * 1.91f + static_cast<float>(pointIndex) * 0.37f);
                    offset.z += noise * std::cos(basePhase * 1.53f + static_cast<float>(pointIndex) * 0.23f);

                    outPoints[pointIndex] = outPoints[pointIndex] + offset;
                }
            }
        }

        void BuildGuideRootsFromPoints(const HairAsset& asset,
                                       const std::vector<Vec3>& points,
                                       std::vector<Vec3>& outRoots)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            outRoots.assign(guideCount, Vec3(0.0f, 0.0f, 0.0f));
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                if (strand.pointCount > 0u && strand.pointOffset < points.size())
                {
                    outRoots[guideIndex] = points[strand.pointOffset];
                }
            }
        }

        std::vector<float> BuildGuideContactScores(const HairAsset& asset,
                                                   const std::vector<Vec3>& points,
                                                   const ColliderSet& colliders,
                                                   float floorY,
                                                   float shellDistance)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> scores(guideCount, 0.0f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                float bestScore = 0.0f;
                for (uint32_t localPoint = 0u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= points.size())
                    {
                        continue;
                    }
                    bestScore = std::max(bestScore,
                                         ComputeColliderContactScore(points[pointIndex], colliders, floorY, shellDistance, true));
                }
                scores[guideIndex] = Clamp(bestScore, 0.0f, 1.0f);
            }
            return scores;
        }

        std::vector<float> BuildGuidePenetrationScores(const HairAsset& asset,
                                                       const std::vector<Vec3>& points,
                                                       const ColliderSet& colliders,
                                                       float floorY,
                                                       float shellDistance)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> scores(guideCount, 0.0f);
            const float normalization = std::max(shellDistance * 2.5f, 1.0e-4f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                float bestPenetration = 0.0f;
                for (uint32_t localPoint = 0u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= points.size())
                    {
                        continue;
                    }
                    const PenetrationResult result = ResolveAgainstColliderSet(points[pointIndex], colliders, shellDistance, true, floorY);
                    bestPenetration = std::max(bestPenetration, result.penetration);
                }
                scores[guideIndex] = Clamp(bestPenetration / normalization, 0.0f, 1.0f);
            }
            return scores;
        }

        std::vector<float> BuildGuideUncertainty(const HairAsset& asset,
                                                 const std::vector<Vec3>& currentRoots,
                                                 const std::vector<Vec3>& previousRoots,
                                                 const std::vector<float>& previousUncertainty,
                                                 const std::vector<float>& contactScores,
                                                 const std::vector<float>& penetrationScores,
                                                 const std::vector<float>& modelErrorScores,
                                                 float fixedTimeStep,
                                                 float temporalCoherenceGain,
                                                 float smoothingAlpha)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> uncertainty(guideCount, 0.0f);
            const float safeDt = std::max(fixedTimeStep, 1.0e-4f);
            const float alpha = Clamp(smoothingAlpha, 0.0f, 1.0f);
            const float temporalGain = std::max(temporalCoherenceGain, 0.0f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                const Vec3 restRoot = (strand.pointCount > 0u && strand.pointOffset < asset.restPoints.size())
                    ? asset.restPoints[strand.pointOffset]
                    : Vec3(0.0f, 0.0f, 0.0f);
                const Vec3 current = (guideIndex < currentRoots.size()) ? currentRoots[guideIndex] : restRoot;
                const Vec3 previous = (guideIndex < previousRoots.size()) ? previousRoots[guideIndex] : current;
                const float motion = Clamp(Length(current - previous) / safeDt / 0.30f, 0.0f, 1.0f);
                const float displacement = Clamp(Length(current - restRoot) / 0.030f, 0.0f, 1.0f);
                const float contact = (guideIndex < contactScores.size()) ? contactScores[guideIndex] : 0.0f;
                const float penetration = (guideIndex < penetrationScores.size()) ? penetrationScores[guideIndex] : 0.0f;
                const float modelError = (guideIndex < modelErrorScores.size()) ? modelErrorScores[guideIndex] : 0.0f;
                const float previousValue = (guideIndex < previousUncertainty.size()) ? previousUncertainty[guideIndex] : 0.0f;
                const float temporalResidual = Clamp(std::fabs(previousValue - contact) * 0.5f + std::fabs(previousValue - penetration) * 0.5f,
                                                     0.0f,
                                                     1.0f) * temporalGain;
                const float instantaneous = Clamp(motion * 0.26f +
                                                  displacement * 0.18f +
                                                  contact * 0.18f +
                                                  penetration * 0.20f +
                                                  modelError * 0.18f +
                                                  temporalResidual * 0.10f,
                                                  0.0f,
                                                  1.0f);
                uncertainty[guideIndex] = Clamp(Lerp(previousValue, instantaneous, alpha), 0.0f, 1.0f);
            }
            return uncertainty;
        }

        void BuildGuideFramePoints(const HairAsset& asset,
                                   const std::vector<Vec3>& fullPoints,
                                   std::vector<Vec3>& outGuidePoints)
        {
            outGuidePoints.clear();
            outGuidePoints.reserve(static_cast<size_t>(asset.guideStrandCount) * asset.particlesPerStrand);
            for (uint32_t guideIndex = 0u; guideIndex < asset.guideStrandCount && guideIndex < asset.strands.size(); ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                for (uint32_t localPoint = 0u; localPoint < asset.particlesPerStrand; ++localPoint)
                {
                    const uint32_t clampedPoint = std::min<uint32_t>(localPoint, strand.pointCount > 0u ? (strand.pointCount - 1u) : 0u);
                    const uint32_t pointIndex = strand.pointOffset + clampedPoint;
                    outGuidePoints.push_back((pointIndex < fullPoints.size()) ? fullPoints[pointIndex] : Vec3(0.0f, 0.0f, 0.0f));
                }
            }
        }

        void AccumulateLengthError(const HairAsset& asset,
                                   const std::vector<Vec3>& points,
                                   const std::vector<uint8_t>* mask,
                                   float& outAverageError)
        {
            double total = 0.0;
            uint32_t count = 0u;
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                if (mask && strandIndex < mask->size() && (*mask)[strandIndex] == 0u)
                {
                    continue;
                }
                const StrandRange& strand = asset.strands[strandIndex];
                for (uint32_t localPoint = 1u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t prevIndex = strand.pointOffset + localPoint - 1u;
                    const uint32_t currIndex = strand.pointOffset + localPoint;
                    if (currIndex >= points.size() || prevIndex >= points.size())
                    {
                        continue;
                    }
                    const float restLength = StrandSegmentRestLength(asset, strandIndex, localPoint);
                    const float currentLength = Length(points[currIndex] - points[prevIndex]);
                    total += std::fabs(currentLength - restLength);
                    ++count;
                }
            }
            outAverageError = (count > 0u) ? static_cast<float>(total / static_cast<double>(count)) : 0.0f;
        }
    }

    bool ApplySparseCorrectivePhysics(const HairAsset& asset,
                                      const ActiveSetPlan& activeSetPlan,
                                      const ColliderSet& colliders,
                                      std::vector<Vec3>& inOutPoints,
                                      const SparseCorrectiveSettings& settings,
                                      SparseCorrectiveReport* outReport,
                                      std::string& outError)
    {
        outError.clear();
        if (inOutPoints.size() != asset.restPoints.size())
        {
            outError = "Sparse corrective input size does not match the asset point layout.";
            return false;
        }

        SparseCorrectiveReport report = {};
        AccumulateLengthError(asset, inOutPoints, &activeSetPlan.strandCorrectiveMask, report.averageLengthErrorBefore);

        double penetrationSum = 0.0;
        uint32_t penetrationSamples = 0u;
        const uint32_t iterations = std::max<uint32_t>(1u, settings.iterations);
        for (uint32_t iteration = 0u; iteration < iterations; ++iteration)
        {
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                if (strandIndex >= activeSetPlan.strandCorrectiveMask.size() || activeSetPlan.strandCorrectiveMask[strandIndex] == 0u)
                {
                    continue;
                }
                const StrandRange& strand = asset.strands[strandIndex];
                bool strandCorrected = false;
                for (uint32_t localPoint = 0u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= inOutPoints.size())
                    {
                        continue;
                    }
                    if (localPoint < settings.preserveRootPointCount)
                    {
                        continue;
                    }

                    const Vec3 point = inOutPoints[pointIndex];
                    const PenetrationResult best = ResolveAgainstColliderSet(point,
                                                                            settings.affectExternalColliders ? colliders : ColliderSet(),
                                                                            settings.shellDistance,
                                                                            settings.affectFloor,
                                                                            -1.05f);

                    if (best.corrected)
                    {
                        inOutPoints[pointIndex] = Lerp(point, best.correctedPoint, Clamp(settings.contactBlend, 0.0f, 1.0f));
                        strandCorrected = true;
                        ++report.correctedPointCount;
                        report.maxPenetration = std::max(report.maxPenetration, best.penetration);
                        penetrationSum += best.penetration;
                        ++penetrationSamples;
                    }
                }

                for (uint32_t localPoint = std::max<uint32_t>(1u, settings.preserveRootPointCount); localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t prevIndex = strand.pointOffset + localPoint - 1u;
                    const uint32_t currIndex = strand.pointOffset + localPoint;
                    if (currIndex >= inOutPoints.size() || prevIndex >= inOutPoints.size())
                    {
                        continue;
                    }
                    const float restLength = StrandSegmentRestLength(asset, strandIndex, localPoint);
                    if (restLength <= 1.0e-6f)
                    {
                        continue;
                    }

                    Vec3 currentDelta = inOutPoints[currIndex] - inOutPoints[prevIndex];
                    float currentLength = Length(currentDelta);
                    if (currentLength > 1.0e-6f)
                    {
                        const Vec3 dir = currentDelta / currentLength;
                        const Vec3 desiredPoint = inOutPoints[prevIndex] + dir * restLength;
                        inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], desiredPoint, Clamp(settings.stretchBlend, 0.0f, 1.0f));
                    }

                    if (localPoint >= 2u)
                    {
                        const uint32_t prevPrevIndex = strand.pointOffset + localPoint - 2u;
                        const Vec3 restPrev = asset.restPoints[prevIndex] - asset.restPoints[prevPrevIndex];
                        const Vec3 restCurr = asset.restPoints[currIndex] - asset.restPoints[prevIndex];
                        const Vec3 desiredDir = Normalize(Lerp(Normalize(restCurr), Normalize(restPrev), 0.25f));
                        if (Length(desiredDir) > 1.0e-6f)
                        {
                            const Vec3 bendTarget = inOutPoints[prevIndex] + desiredDir * restLength;
                            inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], bendTarget, Clamp(settings.bendBlend, 0.0f, 1.0f));
                        }
                    }

                    const PenetrationResult postConstraint = ResolveAgainstColliderSet(inOutPoints[currIndex],
                                                                                      settings.affectExternalColliders ? colliders : ColliderSet(),
                                                                                      settings.shellDistance,
                                                                                      settings.affectFloor,
                                                                                      -1.05f);
                    if (postConstraint.corrected)
                    {
                        inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], postConstraint.correctedPoint, Clamp(settings.contactBlend, 0.0f, 1.0f));
                        strandCorrected = true;
                        report.maxPenetration = std::max(report.maxPenetration, postConstraint.penetration);
                        penetrationSum += postConstraint.penetration;
                        ++penetrationSamples;
                    }
                }

                if (strandCorrected)
                {
                    ++report.correctedStrandCount;
                }
            }
        }

        report.averagePenetration = (penetrationSamples > 0u)
            ? static_cast<float>(penetrationSum / static_cast<double>(penetrationSamples))
            : 0.0f;
        AccumulateLengthError(asset, inOutPoints, &activeSetPlan.strandCorrectiveMask, report.averageLengthErrorAfter);

        if (outReport)
        {
            *outReport = report;
        }
        return true;
    }

    bool GenerateHybridTeacherDataset(const HairAsset& asset,
                                      const ColliderSet& colliders,
                                      const HybridTeacherDatasetSettings& settings,
                                      const std::filesystem::path& outputDirectory,
                                      HybridTeacherDatasetReport& outReport,
                                      std::string& outError)
    {
        outError.clear();
        outReport = {};
        if (asset.restPoints.empty() || asset.strands.empty() || asset.guideStrandCount == 0u)
        {
            outError = "Teacher dataset generation requires a canonicalized asset with guide strands.";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create teacher dataset output directory.";
            return false;
        }

        const ColliderSet runtimeColliders = !HasAnyCollider(colliders)
            ? BuildFallbackTeacherColliders(asset)
            : colliders;

        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(asset, settings.clusterSettings);
        if (clusters.empty())
        {
            outError = "Failed to build active-set clusters for the teacher dataset.";
            return false;
        }

        HairCache hairCache;
        GuideCache guideCache;
        if (!InitializeHairCacheForAsset(asset, settings.frameRate, hairCache, outError) ||
            !InitializeGuideCacheForAsset(asset, settings.frameRate, guideCache, outError))
        {
            return false;
        }

        std::ofstream frameCsv(outputDirectory / "hybrid_teacher_frames.csv");
        if (!frameCsv)
        {
            outError = "Failed to open hybrid_teacher_frames.csv for writing.";
            return false;
        }
        frameCsv << "frameIndex,timeSeconds,selectedClusters,selectedGuides,selectedStrands,budgetScale,recommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctedStrands,correctedPoints,avgPenetration,maxPenetration,avgGuideContact,avgGuidePenetration,avgGuideUncertainty,maxGuideUncertainty,avgLengthErrorBefore,avgLengthErrorAfter,estimatedSimulationMilliseconds\n";

        std::vector<Vec3> previousGuideRoots;
        BuildGuideRootsFromPoints(asset, asset.restPoints, previousGuideRoots);
        std::vector<float> previousGuideUncertainty(previousGuideRoots.size(), 0.0f);

        double selectedClusterSum = 0.0;
        double selectedGuideSum = 0.0;
        double selectedStrandSum = 0.0;
        double penetrationSum = 0.0;
        double correctedPointSum = 0.0;
        float maxPenetration = 0.0f;
        float estimatedSimulationMilliseconds = 0.0f;

        for (uint32_t frameIndex = 0u; frameIndex < std::max<uint32_t>(1u, settings.frameCount); ++frameIndex)
        {
            const float timeSeconds = static_cast<float>(frameIndex) / std::max(settings.frameRate, 1.0f);

            std::vector<Vec3> framePoints;
            BuildProxyFramePoints(asset,
                                  timeSeconds,
                                  settings.swayAmplitude,
                                  settings.tipNoiseAmplitude,
                                  settings.windCyclesPerSecond,
                                  framePoints);

            std::vector<Vec3> currentGuideRoots;
            BuildGuideRootsFromPoints(asset, framePoints, currentGuideRoots);
            const std::vector<float> guideContactScores = BuildGuideContactScores(asset,
                                                                                  framePoints,
                                                                                  runtimeColliders,
                                                                                  -1.05f,
                                                                                  settings.correctiveSettings.shellDistance);
            const std::vector<float> guidePenetrationScores = BuildGuidePenetrationScores(asset,
                                                                                           framePoints,
                                                                                           runtimeColliders,
                                                                                           -1.05f,
                                                                                           settings.correctiveSettings.shellDistance);
            const std::vector<float> modelErrorScores(currentGuideRoots.size(), 0.0f);
            const std::vector<float> guideUncertainty = BuildGuideUncertainty(asset,
                                                                              currentGuideRoots,
                                                                              previousGuideRoots,
                                                                              previousGuideUncertainty,
                                                                              guideContactScores,
                                                                              guidePenetrationScores,
                                                                              modelErrorScores,
                                                                              1.0f / std::max(settings.frameRate, 1.0f),
                                                                              1.0f,
                                                                              0.45f);

            ActiveSetRuntimeInput runtimeInput = {};
            runtimeInput.currentGuideRoots = currentGuideRoots;
            runtimeInput.previousGuideRoots = previousGuideRoots;
            runtimeInput.guideContactScores = guideContactScores;
            runtimeInput.guideUncertainty = guideUncertainty;
            runtimeInput.cameraPosition = Vec3(0.0f, 1.55f, -1.85f);
            runtimeInput.cameraForward = Normalize(Vec3(0.0f, -0.05f, 1.0f));
            runtimeInput.fixedTimeStep = 1.0f / std::max(settings.frameRate, 1.0f);

            ActiveSetBudget budget = settings.budget;
            budget.previousFrameMilliseconds = estimatedSimulationMilliseconds;

            ActiveSetPlan plan;
            if (!EvaluateActiveSetPlan(asset, clusters, runtimeInput, settings.scoringSettings, budget, plan, outError))
            {
                return false;
            }

            std::vector<Vec3> correctedPoints = framePoints;
            SparseCorrectiveReport correctiveReport = {};
            if (!ApplySparseCorrectivePhysics(asset,
                                              plan,
                                              runtimeColliders,
                                              correctedPoints,
                                              settings.correctiveSettings,
                                              &correctiveReport,
                                              outError))
            {
                return false;
            }

            if (!AppendHairCacheFrame(hairCache, asset, correctedPoints, outError))
            {
                return false;
            }
            std::vector<Vec3> guideFramePoints;
            BuildGuideFramePoints(asset, correctedPoints, guideFramePoints);
            if (!AppendGuideCacheGuidePoints(guideCache, guideFramePoints, outError))
            {
                return false;
            }

            estimatedSimulationMilliseconds = 1.20f +
                                              static_cast<float>(plan.summary.selectedClusterCount) * 0.34f +
                                              static_cast<float>(plan.summary.selectedGuideCount) * 0.010f +
                                              static_cast<float>(correctiveReport.correctedPointCount) * 0.0018f +
                                              static_cast<float>(plan.summary.recommendedSolverIterations) * 0.20f +
                                              static_cast<float>(plan.summary.fullTierCount) * 0.0015f;
            outReport.peakEstimatedSimulationMilliseconds = std::max(outReport.peakEstimatedSimulationMilliseconds,
                                                                      estimatedSimulationMilliseconds);

            frameCsv << frameIndex << ","
                     << timeSeconds << ","
                     << plan.summary.selectedClusterCount << ","
                     << plan.summary.selectedGuideCount << ","
                     << plan.summary.selectedStrandCount << ","
                     << plan.summary.budgetScale << ","
                     << plan.summary.recommendedSolverIterations << ","
                     << plan.summary.fullTierCount << ","
                     << plan.summary.reducedTierCount << ","
                     << plan.summary.frozenTierCount << ","
                     << plan.summary.cachedTierCount << ","
                     << correctiveReport.correctedStrandCount << ","
                     << correctiveReport.correctedPointCount << ","
                     << correctiveReport.averagePenetration << ","
                     << correctiveReport.maxPenetration << ","
                     << ComputeAverageScalar(guideContactScores) << ","
                     << ComputeAverageScalar(guidePenetrationScores) << ","
                     << ComputeAverageScalar(guideUncertainty) << ","
                     << ComputeMaxScalar(guideUncertainty) << ","
                     << correctiveReport.averageLengthErrorBefore << ","
                     << correctiveReport.averageLengthErrorAfter << ","
                     << estimatedSimulationMilliseconds << "\n";

            selectedClusterSum += static_cast<double>(plan.summary.selectedClusterCount);
            selectedGuideSum += static_cast<double>(plan.summary.selectedGuideCount);
            selectedStrandSum += static_cast<double>(plan.summary.selectedStrandCount);
            penetrationSum += static_cast<double>(correctiveReport.averagePenetration);
            correctedPointSum += static_cast<double>(correctiveReport.correctedPointCount);
            maxPenetration = std::max(maxPenetration, correctiveReport.maxPenetration);
            previousGuideRoots = currentGuideRoots;
            previousGuideUncertainty = guideUncertainty;
        }

        if (!SaveHairCacheToFile(outputDirectory / "hybrid_teacher.haircache", hairCache, outError) ||
            !SaveGuideCacheToFile(outputDirectory / "hybrid_teacher_guides.guidecache", guideCache, outError) ||
            !SaveHairAssetToHairBinary(outputDirectory / "hybrid_teacher_reference.hairbin", asset, outError))
        {
            return false;
        }

        const float frameCountF = static_cast<float>(std::max<uint32_t>(1u, settings.frameCount));
        outReport.frameCount = std::max<uint32_t>(1u, settings.frameCount);
        outReport.averageSelectedClusterCount = static_cast<float>(selectedClusterSum / frameCountF);
        outReport.averageSelectedGuideCount = static_cast<float>(selectedGuideSum / frameCountF);
        outReport.averageSelectedStrandCount = static_cast<float>(selectedStrandSum / frameCountF);
        outReport.averagePenetration = static_cast<float>(penetrationSum / frameCountF);
        outReport.maxPenetration = maxPenetration;
        outReport.averageCorrectedPointCount = static_cast<float>(correctedPointSum / frameCountF);

        std::ofstream summaryJson(outputDirectory / "hybrid_teacher_summary.json");
        if (!summaryJson)
        {
            outError = "Failed to open hybrid_teacher_summary.json for writing.";
            return false;
        }
        summaryJson << std::fixed << std::setprecision(6);
        summaryJson << "{\n";
        summaryJson << "  \"frameCount\": " << outReport.frameCount << ",\n";
        summaryJson << "  \"averageSelectedClusterCount\": " << outReport.averageSelectedClusterCount << ",\n";
        summaryJson << "  \"averageSelectedGuideCount\": " << outReport.averageSelectedGuideCount << ",\n";
        summaryJson << "  \"averageSelectedStrandCount\": " << outReport.averageSelectedStrandCount << ",\n";
        summaryJson << "  \"averagePenetration\": " << outReport.averagePenetration << ",\n";
        summaryJson << "  \"maxPenetration\": " << outReport.maxPenetration << ",\n";
        summaryJson << "  \"averageCorrectedPointCount\": " << outReport.averageCorrectedPointCount << ",\n";
        summaryJson << "  \"peakEstimatedSimulationMilliseconds\": " << outReport.peakEstimatedSimulationMilliseconds << "\n";
        summaryJson << "}\n";
        return true;
    }

    bool LoadHybridTeacherSequenceFromDirectory(const std::filesystem::path& directoryPath,
                                                ReducedModelTrainingSequence& outSequence,
                                                std::string& outError)
    {
        outError.clear();
        HairAsset referenceAsset;
        HairCache hairCache;
        if (!LoadHairAssetFromHairBinary(directoryPath / "hybrid_teacher_reference.hairbin", referenceAsset, outError))
        {
            return false;
        }
        if (!LoadHairCacheFromFile(directoryPath / "hybrid_teacher.haircache", hairCache, outError))
        {
            return false;
        }
        if (hairCache.pointCount != referenceAsset.restPoints.size())
        {
            outError = "Teacher hair cache point count does not match hybrid_teacher_reference.hairbin.";
            return false;
        }

        outSequence = {};
        outSequence.referenceAsset = std::move(referenceAsset);
        outSequence.frameRate = hairCache.frameRate;
        outSequence.frames = hairCache.frames;
        return true;
    }

    namespace
    {
        const Vec3* SequenceFramePoints(const ReducedModelTrainingSequence& sequence, uint32_t frameIndex)
        {
            const uint32_t pointCount = sequence.PointCountPerFrame();
            if (pointCount == 0u || frameIndex >= sequence.FrameCount())
            {
                return nullptr;
            }
            return sequence.frames.data() + static_cast<size_t>(frameIndex) * pointCount;
        }

        const Vec3* CacheFramePoints(const HairCache& cache, uint32_t frameIndex)
        {
            if (cache.pointCount == 0u || frameIndex >= cache.FrameCount())
            {
                return nullptr;
            }
            return cache.frames.data() + static_cast<size_t>(frameIndex) * cache.pointCount;
        }

        void AppendSequenceFrame(const std::vector<Vec3>& frame, ReducedModelTrainingSequence& ioSequence)
        {
            ioSequence.frames.insert(ioSequence.frames.end(), frame.begin(), frame.end());
        }

        void CopySequenceFrame(const ReducedModelTrainingSequence& sequence, uint32_t frameIndex, std::vector<Vec3>& outPoints)
        {
            const Vec3* frame = SequenceFramePoints(sequence, frameIndex);
            if (!frame)
            {
                outPoints.clear();
                return;
            }
            outPoints.assign(frame, frame + sequence.PointCountPerFrame());
        }

        void BuildGuideFramePointsForModel(const ReducedModel& model,
                                           const std::vector<Vec3>& fullPoints,
                                           std::vector<Vec3>& outGuideFramePoints)
        {
            outGuideFramePoints.clear();
            const uint32_t particlesPerStrand = model.referenceAsset.particlesPerStrand;
            outGuideFramePoints.reserve(static_cast<size_t>(model.selectedGuideStrandIndices.size()) * particlesPerStrand);
            for (uint32_t selectedGuide : model.selectedGuideStrandIndices)
            {
                if (selectedGuide >= model.referenceAsset.strands.size())
                {
                    for (uint32_t i = 0u; i < particlesPerStrand; ++i)
                    {
                        outGuideFramePoints.push_back(Vec3(0.0f, 0.0f, 0.0f));
                    }
                    continue;
                }
                const StrandRange& strand = model.referenceAsset.strands[selectedGuide];
                const uint32_t lastLocalPoint = (strand.pointCount > 0u) ? (strand.pointCount - 1u) : 0u;
                for (uint32_t localPoint = 0u; localPoint < particlesPerStrand; ++localPoint)
                {
                    const uint32_t clampedLocalPoint = std::min(localPoint, lastLocalPoint);
                    const uint32_t pointIndex = strand.pointOffset + clampedLocalPoint;
                    outGuideFramePoints.push_back((pointIndex < fullPoints.size()) ? fullPoints[pointIndex] : Vec3(0.0f, 0.0f, 0.0f));
                }
            }
        }

        void BuildRestGuideFramePointsForModel(const ReducedModel& model, std::vector<Vec3>& outGuideFramePoints)
        {
            BuildGuideFramePointsForModel(model, model.referenceAsset.restPoints, outGuideFramePoints);
        }

        float ComputeRmsError(const std::vector<Vec3>& a, const std::vector<Vec3>& b)
        {
            if (a.size() != b.size() || a.empty())
            {
                return 0.0f;
            }
            double sumSq = 0.0;
            for (size_t i = 0u; i < a.size(); ++i)
            {
                const Vec3 d = a[i] - b[i];
                sumSq += static_cast<double>(Dot(d, d));
            }
            return static_cast<float>(std::sqrt(sumSq / static_cast<double>(a.size())));
        }

        float ComputeAverageMagnitude(const std::vector<Vec3>& points)
        {
            if (points.empty())
            {
                return 0.0f;
            }
            double total = 0.0;
            for (const Vec3& p : points)
            {
                total += static_cast<double>(Length(p));
            }
            return static_cast<float>(total / static_cast<double>(points.size()));
        }

        float ComputeAverageScalar(const std::vector<float>& values)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            double total = 0.0;
            for (float v : values)
            {
                total += static_cast<double>(v);
            }
            return static_cast<float>(total / static_cast<double>(values.size()));
        }

        float ComputeMaxScalar(const std::vector<float>& values)
        {
            float best = 0.0f;
            for (float v : values)
            {
                best = std::max(best, v);
            }
            return best;
        }

        void BuildSmoothedPriorSequence(const ReducedModelTrainingSequence& teacherSequence,
                                        float smoothingAlpha,
                                        ReducedModelTrainingSequence& outSequence)
        {
            outSequence = {};
            outSequence.referenceAsset = teacherSequence.referenceAsset;
            outSequence.frameRate = teacherSequence.frameRate;
            const uint32_t frameCount = teacherSequence.FrameCount();
            const uint32_t pointCount = teacherSequence.PointCountPerFrame();
            if (frameCount == 0u || pointCount == 0u)
            {
                return;
            }

            std::vector<Vec3> smoothed(pointCount);
            CopySequenceFrame(teacherSequence, 0u, smoothed);
            AppendSequenceFrame(smoothed, outSequence);
            const float alpha = Clamp(smoothingAlpha, 0.01f, 1.0f);
            for (uint32_t frameIndex = 1u; frameIndex < frameCount; ++frameIndex)
            {
                std::vector<Vec3> current;
                CopySequenceFrame(teacherSequence, frameIndex, current);
                for (uint32_t i = 0u; i < pointCount; ++i)
                {
                    smoothed[i] = Lerp(smoothed[i], current[i], alpha);
                }
                AppendSequenceFrame(smoothed, outSequence);
            }
        }

        void BuildResidualSequence(const ReducedModelTrainingSequence& teacherSequence,
                                   const ReducedModelTrainingSequence& priorSequence,
                                   ReducedModelTrainingSequence& outSequence,
                                   float* outAverageResidualMagnitude,
                                   float* outMaxResidualMagnitude)
        {
            outSequence = {};
            outSequence.referenceAsset = teacherSequence.referenceAsset;
            outSequence.frameRate = teacherSequence.frameRate;

            double averageResidualMagnitude = 0.0;
            float maxResidualMagnitude = 0.0f;
            uint64_t residualCount = 0u;
            const uint32_t frameCount = std::min(teacherSequence.FrameCount(), priorSequence.FrameCount());
            for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
            {
                std::vector<Vec3> teacher;
                std::vector<Vec3> prior;
                CopySequenceFrame(teacherSequence, frameIndex, teacher);
                CopySequenceFrame(priorSequence, frameIndex, prior);
                std::vector<Vec3> residual = teacherSequence.referenceAsset.restPoints;
                for (uint32_t i = 0u; i < residual.size() && i < teacher.size() && i < prior.size(); ++i)
                {
                    const Vec3 delta = teacher[i] - prior[i];
                    residual[i] += delta;
                    const float magnitude = Length(delta);
                    averageResidualMagnitude += static_cast<double>(magnitude);
                    maxResidualMagnitude = std::max(maxResidualMagnitude, magnitude);
                    ++residualCount;
                }
                AppendSequenceFrame(residual, outSequence);
            }

            if (outAverageResidualMagnitude)
            {
                *outAverageResidualMagnitude = (residualCount > 0u)
                    ? static_cast<float>(averageResidualMagnitude / static_cast<double>(residualCount))
                    : 0.0f;
            }
            if (outMaxResidualMagnitude)
            {
                *outMaxResidualMagnitude = maxResidualMagnitude;
            }
        }

        void ComposeHybridPoints(const HairAsset& referenceAsset,
                                 const std::vector<Vec3>& priorPoints,
                                 const std::vector<Vec3>& residualPoints,
                                 float residualBlend,
                                 std::vector<Vec3>& outPoints)
        {
            outPoints = priorPoints;
            if (outPoints.size() != referenceAsset.restPoints.size() || residualPoints.size() != referenceAsset.restPoints.size())
            {
                return;
            }
            const float blend = std::max(0.0f, residualBlend);
            for (uint32_t i = 0u; i < outPoints.size(); ++i)
            {
                outPoints[i] = priorPoints[i] + (residualPoints[i] - referenceAsset.restPoints[i]) * blend;
            }
        }

        bool WriteHybridBundleConfig(const std::filesystem::path& filePath,
                                     const HybridModelBundle& bundle,
                                     const HybridModelTrainingReport* report,
                                     std::string& outError)
        {
            std::ofstream out(filePath);
            if (!out)
            {
                outError = "Failed to open hybrid bundle config for writing.";
                return false;
            }
            out << std::fixed << std::setprecision(8);
            out << "priorGuideCount=" << bundle.trainingSettings.priorGuideCount << "\n";
            out << "residualGuideCount=" << bundle.trainingSettings.residualGuideCount << "\n";
            out << "maxInfluencesPerFollower=" << bundle.trainingSettings.maxInfluencesPerFollower << "\n";
            out << "regularization=" << bundle.trainingSettings.regularization << "\n";
            out << "validationStride=" << bundle.trainingSettings.validationStride << "\n";
            out << "priorGuideSmoothingAlpha=" << bundle.trainingSettings.priorGuideSmoothingAlpha << "\n";
            out << "residualGuideGain=" << bundle.trainingSettings.residualGuideGain << "\n";
            out << "residualBlend=" << bundle.trainingSettings.residualBlend << "\n";
            out << "enableScalpCollision=" << (bundle.trainingSettings.enableScalpCollision ? 1 : 0) << "\n";
            out << "clusterGuideCount=" << bundle.clusterSettings.targetGuideCountPerCluster << "\n";
            out << "clusterMaxGuideRootDistance=" << bundle.clusterSettings.maxGuideRootDistance << "\n";
            out << "scoringMotionWeight=" << bundle.scoringSettings.motionWeight << "\n";
            out << "scoringDisplacementWeight=" << bundle.scoringSettings.displacementWeight << "\n";
            out << "scoringContactWeight=" << bundle.scoringSettings.contactWeight << "\n";
            out << "scoringVisibilityWeight=" << bundle.scoringSettings.visibilityWeight << "\n";
            out << "scoringSilhouetteWeight=" << bundle.scoringSettings.silhouetteWeight << "\n";
            out << "scoringClusterSizeWeight=" << bundle.scoringSettings.clusterSizeWeight << "\n";
            out << "scoringUncertaintyWeight=" << bundle.scoringSettings.uncertaintyWeight << "\n";
            out << "budgetMaxActiveClusters=" << bundle.budget.maxActiveClusters << "\n";
            out << "budgetMaxActiveGuides=" << bundle.budget.maxActiveGuides << "\n";
            out << "budgetPromoteScoreThreshold=" << bundle.budget.promoteScoreThreshold << "\n";
            out << "budgetTargetFrameMs=" << bundle.budget.targetFrameMilliseconds << "\n";
            out << "budgetMinSolverIterations=" << bundle.budget.minSolverIterations << "\n";
            out << "budgetMaxSolverIterations=" << bundle.budget.maxSolverIterations << "\n";
            out << "correctiveShellDistance=" << bundle.correctiveSettings.shellDistance << "\n";
            out << "correctiveContactBlend=" << bundle.correctiveSettings.contactBlend << "\n";
            out << "correctiveStretchBlend=" << bundle.correctiveSettings.stretchBlend << "\n";
            out << "correctiveBendBlend=" << bundle.correctiveSettings.bendBlend << "\n";
            out << "correctiveIterations=" << bundle.correctiveSettings.iterations << "\n";
            out << "uncertaintyGain=" << bundle.uncertaintyGain << "\n";
            out << "contactGain=" << bundle.contactGain << "\n";
            out << "penetrationGain=" << bundle.penetrationGain << "\n";
            out << "temporalCoherenceGain=" << bundle.temporalCoherenceGain << "\n";
            out << "uncertaintySmoothingAlpha=" << bundle.uncertaintySmoothingAlpha << "\n";
            out << "priorVelocityPredictionGain=" << bundle.priorVelocityPredictionGain << "\n";
            out << "residualVelocityPredictionGain=" << bundle.residualVelocityPredictionGain << "\n";
            if (report)
            {
                out << "reportPriorRms=" << report->priorEvaluation.rmsPositionError << "\n";
                out << "reportResidualRms=" << report->residualEvaluation.rmsPositionError << "\n";
                out << "reportAverageResidualMagnitude=" << report->averageResidualMagnitude << "\n";
                out << "reportMaxResidualMagnitude=" << report->maxResidualMagnitude << "\n";
            }
            outError.clear();
            return true;
        }

        bool ParseHybridConfigLine(const std::string& line, std::string& outKey, std::string& outValue)
        {
            const size_t eq = line.find('=');
            if (eq == std::string::npos)
            {
                return false;
            }
            outKey = line.substr(0u, eq);
            outValue = line.substr(eq + 1u);
            return !outKey.empty();
        }

        void ApplyHybridConfigValue(HybridModelBundle& ioBundle, const std::string& key, const std::string& value)
        {
            auto toUInt = [&](uint32_t fallback) -> uint32_t { try { return static_cast<uint32_t>(std::stoul(value)); } catch (...) { return fallback; } };
            auto toFloat = [&](float fallback) -> float { try { return std::stof(value); } catch (...) { return fallback; } };
            if (key == "priorGuideCount") ioBundle.trainingSettings.priorGuideCount = toUInt(ioBundle.trainingSettings.priorGuideCount);
            else if (key == "residualGuideCount") ioBundle.trainingSettings.residualGuideCount = toUInt(ioBundle.trainingSettings.residualGuideCount);
            else if (key == "maxInfluencesPerFollower") ioBundle.trainingSettings.maxInfluencesPerFollower = toUInt(ioBundle.trainingSettings.maxInfluencesPerFollower);
            else if (key == "regularization") ioBundle.trainingSettings.regularization = toFloat(ioBundle.trainingSettings.regularization);
            else if (key == "validationStride") ioBundle.trainingSettings.validationStride = toUInt(ioBundle.trainingSettings.validationStride);
            else if (key == "priorGuideSmoothingAlpha") ioBundle.trainingSettings.priorGuideSmoothingAlpha = toFloat(ioBundle.trainingSettings.priorGuideSmoothingAlpha);
            else if (key == "residualGuideGain") ioBundle.trainingSettings.residualGuideGain = toFloat(ioBundle.trainingSettings.residualGuideGain);
            else if (key == "residualBlend") ioBundle.trainingSettings.residualBlend = toFloat(ioBundle.trainingSettings.residualBlend);
            else if (key == "enableScalpCollision") ioBundle.trainingSettings.enableScalpCollision = toUInt(ioBundle.trainingSettings.enableScalpCollision ? 1u : 0u) != 0u;
            else if (key == "clusterGuideCount") ioBundle.clusterSettings.targetGuideCountPerCluster = toUInt(ioBundle.clusterSettings.targetGuideCountPerCluster);
            else if (key == "clusterMaxGuideRootDistance") ioBundle.clusterSettings.maxGuideRootDistance = toFloat(ioBundle.clusterSettings.maxGuideRootDistance);
            else if (key == "scoringMotionWeight") ioBundle.scoringSettings.motionWeight = toFloat(ioBundle.scoringSettings.motionWeight);
            else if (key == "scoringDisplacementWeight") ioBundle.scoringSettings.displacementWeight = toFloat(ioBundle.scoringSettings.displacementWeight);
            else if (key == "scoringContactWeight") ioBundle.scoringSettings.contactWeight = toFloat(ioBundle.scoringSettings.contactWeight);
            else if (key == "scoringVisibilityWeight") ioBundle.scoringSettings.visibilityWeight = toFloat(ioBundle.scoringSettings.visibilityWeight);
            else if (key == "scoringSilhouetteWeight") ioBundle.scoringSettings.silhouetteWeight = toFloat(ioBundle.scoringSettings.silhouetteWeight);
            else if (key == "scoringClusterSizeWeight") ioBundle.scoringSettings.clusterSizeWeight = toFloat(ioBundle.scoringSettings.clusterSizeWeight);
            else if (key == "scoringUncertaintyWeight") ioBundle.scoringSettings.uncertaintyWeight = toFloat(ioBundle.scoringSettings.uncertaintyWeight);
            else if (key == "budgetMaxActiveClusters") ioBundle.budget.maxActiveClusters = toUInt(ioBundle.budget.maxActiveClusters);
            else if (key == "budgetMaxActiveGuides") ioBundle.budget.maxActiveGuides = toUInt(ioBundle.budget.maxActiveGuides);
            else if (key == "budgetPromoteScoreThreshold") ioBundle.budget.promoteScoreThreshold = toFloat(ioBundle.budget.promoteScoreThreshold);
            else if (key == "budgetTargetFrameMs") ioBundle.budget.targetFrameMilliseconds = toFloat(ioBundle.budget.targetFrameMilliseconds);
            else if (key == "budgetMinSolverIterations") ioBundle.budget.minSolverIterations = toUInt(ioBundle.budget.minSolverIterations);
            else if (key == "budgetMaxSolverIterations") ioBundle.budget.maxSolverIterations = toUInt(ioBundle.budget.maxSolverIterations);
            else if (key == "correctiveShellDistance") ioBundle.correctiveSettings.shellDistance = toFloat(ioBundle.correctiveSettings.shellDistance);
            else if (key == "correctiveContactBlend") ioBundle.correctiveSettings.contactBlend = toFloat(ioBundle.correctiveSettings.contactBlend);
            else if (key == "correctiveStretchBlend") ioBundle.correctiveSettings.stretchBlend = toFloat(ioBundle.correctiveSettings.stretchBlend);
            else if (key == "correctiveBendBlend") ioBundle.correctiveSettings.bendBlend = toFloat(ioBundle.correctiveSettings.bendBlend);
            else if (key == "correctiveIterations") ioBundle.correctiveSettings.iterations = toUInt(ioBundle.correctiveSettings.iterations);
            else if (key == "uncertaintyGain") ioBundle.uncertaintyGain = toFloat(ioBundle.uncertaintyGain);
            else if (key == "contactGain") ioBundle.contactGain = toFloat(ioBundle.contactGain);
            else if (key == "penetrationGain") ioBundle.penetrationGain = toFloat(ioBundle.penetrationGain);
            else if (key == "temporalCoherenceGain") ioBundle.temporalCoherenceGain = toFloat(ioBundle.temporalCoherenceGain);
            else if (key == "uncertaintySmoothingAlpha") ioBundle.uncertaintySmoothingAlpha = toFloat(ioBundle.uncertaintySmoothingAlpha);
            else if (key == "priorVelocityPredictionGain") ioBundle.priorVelocityPredictionGain = toFloat(ioBundle.priorVelocityPredictionGain);
            else if (key == "residualVelocityPredictionGain") ioBundle.residualVelocityPredictionGain = toFloat(ioBundle.residualVelocityPredictionGain);
        }

        bool WriteHybridSummaryJson(const std::filesystem::path& filePath,
                                    const HybridModelBundle& bundle,
                                    const HybridModelTrainingReport* trainingReport,
                                    const FullHybridRuntimeReport* runtimeReport,
                                    std::string& outError)
        {
            std::ofstream out(filePath);
            if (!out)
            {
                outError = "Failed to open hybrid summary JSON for writing.";
                return false;
            }
            out << std::fixed << std::setprecision(6);
            out << "{\n";
            out << "  \"priorGuideCount\": " << bundle.trainingSettings.priorGuideCount << ",\n";
            out << "  \"residualGuideCount\": " << bundle.trainingSettings.residualGuideCount << ",\n";
            out << "  \"priorGuideSmoothingAlpha\": " << bundle.trainingSettings.priorGuideSmoothingAlpha << ",\n";
            out << "  \"residualGuideGain\": " << bundle.trainingSettings.residualGuideGain << ",\n";
            out << "  \"residualBlend\": " << bundle.trainingSettings.residualBlend << ",\n";
            out << "  \"uncertaintyGain\": " << bundle.uncertaintyGain << ",\n";
            out << "  \"contactGain\": " << bundle.contactGain << ",\n";
            out << "  \"priorVelocityPredictionGain\": " << bundle.priorVelocityPredictionGain << ",\n";
            out << "  \"residualVelocityPredictionGain\": " << bundle.residualVelocityPredictionGain;
            if (trainingReport)
            {
                out << ",\n  \"priorRms\": " << trainingReport->priorEvaluation.rmsPositionError
                    << ",\n  \"residualRms\": " << trainingReport->residualEvaluation.rmsPositionError
                    << ",\n  \"averageResidualMagnitude\": " << trainingReport->averageResidualMagnitude
                    << ",\n  \"maxResidualMagnitude\": " << trainingReport->maxResidualMagnitude;
            }
            if (runtimeReport)
            {
                out << ",\n  \"runtimeFrameCount\": " << runtimeReport->frameCount
                    << ",\n  \"averagePriorRmsError\": " << runtimeReport->averagePriorRmsError
                    << ",\n  \"averageHybridRmsErrorBeforeCorrection\": " << runtimeReport->averageHybridRmsErrorBeforeCorrection
                    << ",\n  \"averageHybridRmsErrorAfterCorrection\": " << runtimeReport->averageHybridRmsErrorAfterCorrection
                    << ",\n  \"maxHybridRmsErrorAfterCorrection\": " << runtimeReport->maxHybridRmsErrorAfterCorrection
                    << ",\n  \"averageSelectedClusterCount\": " << runtimeReport->averageSelectedClusterCount
                    << ",\n  \"averageSelectedGuideCount\": " << runtimeReport->averageSelectedGuideCount
                    << ",\n  \"averageCorrectedPointCount\": " << runtimeReport->averageCorrectedPointCount
                    << ",\n  \"averagePenetration\": " << runtimeReport->averagePenetration
                    << ",\n  \"maxPenetration\": " << runtimeReport->maxPenetration;
            }
            out << "\n}\n";
            outError.clear();
            return true;
        }
    }

    bool TrainHybridModelBundle(const ReducedModelTrainingSequence& teacherSequence,
                                const ColliderSet& colliders,
                                const HybridModelTrainingSettings& settings,
                                HybridModelBundle& outBundle,
                                HybridModelTrainingReport& outReport,
                                std::string& outError)
    {
        outError.clear();
        outBundle = {};
        outReport = {};
        if (teacherSequence.Empty())
        {
            outError = "Hybrid training requires a non-empty teacher sequence.";
            return false;
        }

        ReducedModelTrainingSequence priorSequence;
        BuildSmoothedPriorSequence(teacherSequence, settings.priorGuideSmoothingAlpha, priorSequence);

        float averageResidualMagnitude = 0.0f;
        float maxResidualMagnitude = 0.0f;
        ReducedModelTrainingSequence residualSequence;
        BuildResidualSequence(teacherSequence, priorSequence, residualSequence, &averageResidualMagnitude, &maxResidualMagnitude);

        ReducedModelSettings priorSettings = {};
        priorSettings.guideCount = settings.priorGuideCount;
        priorSettings.maxInfluencesPerFollower = settings.maxInfluencesPerFollower;
        priorSettings.regularization = settings.regularization;
        priorSettings.validationStride = settings.validationStride;
        priorSettings.useGuideDisplacements = true;
        priorSettings.rootLockBlend = 1.0f;
        priorSettings.enableScalpCollision = settings.enableScalpCollision;

        ReducedModelSettings residualSettings = priorSettings;
        residualSettings.guideCount = std::max<uint32_t>(settings.residualGuideCount, settings.priorGuideCount);
        residualSettings.rootLockBlend = 0.55f;
        residualSettings.lengthProjectionBlend = 0.35f;
        residualSettings.enableScalpCollision = false;

        if (!TrainReducedModel(priorSequence, priorSettings, outBundle.priorModel, outError))
        {
            return false;
        }
        if (!TrainReducedModel(residualSequence, residualSettings, outBundle.residualModel, outError))
        {
            return false;
        }
        if (!EvaluateReducedModel(outBundle.priorModel, priorSequence, outReport.priorEvaluation, outError))
        {
            return false;
        }
        if (!EvaluateReducedModel(outBundle.residualModel, residualSequence, outReport.residualEvaluation, outError))
        {
            return false;
        }

        outReport.averageResidualMagnitude = averageResidualMagnitude;
        outReport.maxResidualMagnitude = maxResidualMagnitude;

        outBundle.trainingSettings = settings;
        outBundle.clusterSettings.targetGuideCountPerCluster = std::max<uint32_t>(4u, std::min<uint32_t>(8u, settings.residualGuideCount / 3u + 1u));
        outBundle.clusterSettings.maxGuideRootDistance = 0.09f;
        outBundle.scoringSettings.motionWeight = 0.22f;
        outBundle.scoringSettings.displacementWeight = 0.20f;
        outBundle.scoringSettings.contactWeight = Clamp(0.24f + averageResidualMagnitude * 18.0f, 0.22f, 0.40f);
        outBundle.scoringSettings.visibilityWeight = 0.12f;
        outBundle.scoringSettings.silhouetteWeight = 0.13f;
        outBundle.scoringSettings.clusterSizeWeight = 0.05f;
        outBundle.scoringSettings.uncertaintyWeight = Clamp(0.12f + outReport.priorEvaluation.rmsPositionError * 12.0f + outReport.residualEvaluation.rmsPositionError * 6.0f, 0.12f, 0.36f);
        outBundle.budget.maxActiveClusters = std::max<uint32_t>(6u, settings.residualGuideCount / 4u);
        outBundle.budget.maxActiveGuides = std::max<uint32_t>(32u, settings.residualGuideCount * 2u);
        outBundle.budget.promoteScoreThreshold = Clamp(0.52f - averageResidualMagnitude * 4.0f, 0.30f, 0.60f);
        outBundle.budget.targetFrameMilliseconds = 8.0f;
        outBundle.budget.minSolverIterations = 2u;
        outBundle.budget.maxSolverIterations = 8u;
        outBundle.correctiveSettings = {};
        outBundle.correctiveSettings.contactBlend = Clamp(0.82f + averageResidualMagnitude * 4.0f, 0.82f, 0.95f);
        outBundle.correctiveSettings.stretchBlend = 0.40f;
        outBundle.correctiveSettings.bendBlend = 0.18f;
        outBundle.correctiveSettings.iterations = 2u;
        outBundle.uncertaintyGain = Clamp(1.0f + outReport.priorEvaluation.rmsPositionError * 25.0f, 0.8f, 2.5f);
        outBundle.contactGain = Clamp(1.0f + averageResidualMagnitude * 12.0f, 0.8f, 2.0f);
        outBundle.penetrationGain = Clamp(1.0f + averageResidualMagnitude * 20.0f + (HasAnyCollider(colliders) ? 0.15f : 0.0f), 0.9f, 2.8f);
        outBundle.temporalCoherenceGain = Clamp(1.0f + outReport.priorEvaluation.rmsPositionError * 10.0f, 0.8f, 1.8f);
        outBundle.uncertaintySmoothingAlpha = Clamp(0.28f + outReport.residualEvaluation.rmsPositionError * 18.0f, 0.20f, 0.65f);
        outBundle.priorVelocityPredictionGain = Clamp(0.10f + averageResidualMagnitude * 18.0f - settings.priorGuideSmoothingAlpha * 0.20f,
                                                      0.0f,
                                                      0.65f);
        outBundle.residualVelocityPredictionGain = Clamp(0.18f + averageResidualMagnitude * 12.0f + outReport.residualEvaluation.rmsPositionError * 14.0f,
                                                         0.0f,
                                                         0.85f);
        return true;
    }

    bool SaveHybridModelBundle(const std::filesystem::path& directoryPath,
                               const HybridModelBundle& bundle,
                               const HybridModelTrainingReport* report,
                               std::string& outError)
    {
        std::error_code ec;
        std::filesystem::create_directories(directoryPath, ec);
        if (ec)
        {
            outError = "Failed to create hybrid bundle directory.";
            return false;
        }
        if (!SaveReducedModelToFile(directoryPath / "prior.reducedbin", bundle.priorModel, outError) ||
            !SaveReducedModelToFile(directoryPath / "residual.reducedbin", bundle.residualModel, outError) ||
            !WriteHybridBundleConfig(directoryPath / "hybrid_bundle.cfg", bundle, report, outError) ||
            !WriteHybridSummaryJson(directoryPath / "hybrid_bundle_summary.json", bundle, report, nullptr, outError))
        {
            return false;
        }
        return true;
    }

    bool LoadHybridModelBundle(const std::filesystem::path& directoryPath,
                               HybridModelBundle& outBundle,
                               std::string& outError)
    {
        outError.clear();
        outBundle = {};
        if (!LoadReducedModelFromFile(directoryPath / "prior.reducedbin", outBundle.priorModel, outError) ||
            !LoadReducedModelFromFile(directoryPath / "residual.reducedbin", outBundle.residualModel, outError))
        {
            return false;
        }
        std::ifstream in(directoryPath / "hybrid_bundle.cfg");
        if (!in)
        {
            outError = "Failed to open hybrid_bundle.cfg.";
            return false;
        }
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::string key;
            std::string value;
            if (ParseHybridConfigLine(line, key, value))
            {
                ApplyHybridConfigValue(outBundle, key, value);
            }
        }
        return true;
    }

    std::string BuildHybridModelBundleSummary(const HybridModelBundle& bundle,
                                              const HybridModelTrainingReport* report)
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(5);
        ss << "priorGuides=" << bundle.trainingSettings.priorGuideCount
           << " residualGuides=" << bundle.trainingSettings.residualGuideCount
           << " residualBlend=" << bundle.trainingSettings.residualBlend
           << " smoothingAlpha=" << bundle.trainingSettings.priorGuideSmoothingAlpha
           << " uncertaintyAlpha=" << bundle.uncertaintySmoothingAlpha
           << " penetrationGain=" << bundle.penetrationGain
           << " priorVelPred=" << bundle.priorVelocityPredictionGain
           << " residualVelPred=" << bundle.residualVelocityPredictionGain
           << " budgetClusters=" << bundle.budget.maxActiveClusters
           << " budgetGuides=" << bundle.budget.maxActiveGuides;
        if (report)
        {
            ss << " priorRms=" << report->priorEvaluation.rmsPositionError
               << " residualRms=" << report->residualEvaluation.rmsPositionError
               << " avgResidualMag=" << report->averageResidualMagnitude;
        }
        return ss.str();
    }

    bool RunFullHybridRuntime(const ReducedModelTrainingSequence& teacherSequence,
                              const ColliderSet& colliders,
                              const HybridModelBundle& bundle,
                              const FullHybridRuntimeSettings& settings,
                              const std::filesystem::path& outputDirectory,
                              FullHybridRuntimeReport& outReport,
                              std::string& outError)
    {
        outError.clear();
        outReport = {};
        if (teacherSequence.Empty())
        {
            outError = "Full hybrid runtime requires a non-empty teacher sequence.";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create full hybrid runtime output directory.";
            return false;
        }

        const ColliderSet runtimeColliders = !HasAnyCollider(colliders)
            ? BuildFallbackTeacherColliders(teacherSequence.referenceAsset)
            : colliders;
        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(teacherSequence.referenceAsset, bundle.clusterSettings);
        if (clusters.empty())
        {
            outError = "Failed to build clusters for full hybrid runtime.";
            return false;
        }

        HairCache hairCache;
        GuideCache guideCache;
        const float runtimeFrameRate = (settings.frameRateOverride > 0.0f) ? settings.frameRateOverride : teacherSequence.frameRate;
        if (!InitializeHairCacheForAsset(teacherSequence.referenceAsset, runtimeFrameRate, hairCache, outError) ||
            !InitializeGuideCacheForAsset(teacherSequence.referenceAsset, runtimeFrameRate, guideCache, outError))
        {
            return false;
        }

        std::ofstream frameCsv(outputDirectory / "full_hybrid_frames.csv");
        if (!frameCsv)
        {
            outError = "Failed to open full_hybrid_frames.csv for writing.";
            return false;
        }
        frameCsv << "frameIndex,timeSeconds,priorRms,hybridRmsBeforeCorrection,hybridRmsAfterCorrection,selectedClusters,selectedGuides,selectedStrands,budgetScale,recommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctedStrands,correctedPoints,avgPenetration,maxPenetration,avgGuideContact,avgGuidePenetration,avgGuideUncertainty,maxGuideUncertainty,avgGuideModelError\n";

        std::vector<Vec3> restPriorGuidePoints;
        std::vector<Vec3> smoothedPriorGuidePoints;
        std::vector<Vec3> previousSmoothedPriorGuidePoints;
        BuildRestGuideFramePointsForModel(bundle.priorModel, restPriorGuidePoints);
        smoothedPriorGuidePoints = restPriorGuidePoints;
        previousSmoothedPriorGuidePoints = smoothedPriorGuidePoints;

        std::vector<Vec3> previousResidualGuidePoints;
        BuildRestGuideFramePointsForModel(bundle.residualModel, previousResidualGuidePoints);

        std::vector<Vec3> previousHybridGuideRoots;
        BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherSequence.referenceAsset.restPoints, previousHybridGuideRoots);
        std::vector<float> previousGuideUncertainty(previousHybridGuideRoots.size(), 0.0f);

        double priorRmsSum = 0.0;
        double hybridBeforeRmsSum = 0.0;
        double hybridAfterRmsSum = 0.0;
        double selectedClusterSum = 0.0;
        double selectedGuideSum = 0.0;
        double correctedPointSum = 0.0;
        double penetrationSum = 0.0;
        float maxHybridAfterRms = 0.0f;
        float maxPenetration = 0.0f;
        float previousFrameMilliseconds = 0.0f;

        const float residualBlend = (settings.residualBlendOverride >= 0.0f)
            ? settings.residualBlendOverride
            : bundle.trainingSettings.residualBlend;

        for (uint32_t frameIndex = 0u; frameIndex < teacherSequence.FrameCount(); ++frameIndex)
        {
            std::vector<Vec3> teacherFrame;
            CopySequenceFrame(teacherSequence, frameIndex, teacherFrame);

            std::vector<Vec3> currentPriorGuidePoints;
            BuildGuideFramePointsForModel(bundle.priorModel, teacherFrame, currentPriorGuidePoints);
            for (uint32_t i = 0u; i < smoothedPriorGuidePoints.size() && i < currentPriorGuidePoints.size(); ++i)
            {
                smoothedPriorGuidePoints[i] = Lerp(smoothedPriorGuidePoints[i],
                                                   currentPriorGuidePoints[i],
                                                   Clamp(bundle.trainingSettings.priorGuideSmoothingAlpha, 0.01f, 1.0f));
            }

            std::vector<Vec3> predictedPriorGuidePoints = smoothedPriorGuidePoints;
            for (uint32_t i = 0u; i < predictedPriorGuidePoints.size() && i < previousSmoothedPriorGuidePoints.size(); ++i)
            {
                predictedPriorGuidePoints[i] += (smoothedPriorGuidePoints[i] - previousSmoothedPriorGuidePoints[i]) *
                                               Clamp(bundle.priorVelocityPredictionGain, 0.0f, 1.0f);
            }

            HairAsset priorAsset;
            if (!ReconstructReducedModelFrame(bundle.priorModel, predictedPriorGuidePoints, priorAsset, outError))
            {
                return false;
            }

            std::vector<Vec3> currentResidualGuidePoints;
            BuildGuideFramePointsForModel(bundle.residualModel, teacherFrame, currentResidualGuidePoints);
            std::vector<Vec3> priorResidualGuidePoints;
            BuildGuideFramePointsForModel(bundle.residualModel, priorAsset.restPoints, priorResidualGuidePoints);
            std::vector<Vec3> restResidualGuidePoints;
            BuildRestGuideFramePointsForModel(bundle.residualModel, restResidualGuidePoints);
            std::vector<Vec3> residualGuideInput = restResidualGuidePoints;
            for (uint32_t i = 0u; i < residualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
            {
                residualGuideInput[i] += (currentResidualGuidePoints[i] - priorResidualGuidePoints[i]) * bundle.trainingSettings.residualGuideGain;
                if (i < previousResidualGuidePoints.size())
                {
                    residualGuideInput[i] += (currentResidualGuidePoints[i] - previousResidualGuidePoints[i]) *
                                             Clamp(bundle.residualVelocityPredictionGain, 0.0f, 1.0f);
                }
            }

            HairAsset residualAsset;
            if (!ReconstructReducedModelFrame(bundle.residualModel, residualGuideInput, residualAsset, outError))
            {
                return false;
            }

            std::vector<Vec3> hybridBeforeCorrection;
            ComposeHybridPoints(teacherSequence.referenceAsset,
                                priorAsset.restPoints,
                                residualAsset.restPoints,
                                residualBlend,
                                hybridBeforeCorrection);

            std::vector<Vec3> currentHybridGuideRoots;
            BuildGuideRootsFromPoints(teacherSequence.referenceAsset, hybridBeforeCorrection, currentHybridGuideRoots);
            std::vector<Vec3> teacherGuideRoots;
            BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherFrame, teacherGuideRoots);
            std::vector<float> guideContactScores = BuildGuideContactScores(teacherSequence.referenceAsset,
                                                                            hybridBeforeCorrection,
                                                                            runtimeColliders,
                                                                            -1.05f,
                                                                            bundle.correctiveSettings.shellDistance);
            std::vector<float> guidePenetrationScores = BuildGuidePenetrationScores(teacherSequence.referenceAsset,
                                                                                     hybridBeforeCorrection,
                                                                                     runtimeColliders,
                                                                                     -1.05f,
                                                                                     bundle.correctiveSettings.shellDistance);
            const uint32_t guideCount = std::min<uint32_t>(teacherSequence.referenceAsset.guideStrandCount, static_cast<uint32_t>(teacherSequence.referenceAsset.strands.size()));
            std::vector<float> guideModelErrorScores(guideCount, 0.0f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const Vec3 teacherRoot = (guideIndex < teacherGuideRoots.size()) ? teacherGuideRoots[guideIndex] : Vec3(0.0f, 0.0f, 0.0f);
                const Vec3 hybridRoot = (guideIndex < currentHybridGuideRoots.size()) ? currentHybridGuideRoots[guideIndex] : teacherRoot;
                const StrandRange& strand = teacherSequence.referenceAsset.strands[guideIndex];
                const uint32_t pointIndex = strand.pointOffset;
                const float residualMagnitude = (pointIndex < residualAsset.restPoints.size() && pointIndex < teacherSequence.referenceAsset.restPoints.size())
                    ? Length(residualAsset.restPoints[pointIndex] - teacherSequence.referenceAsset.restPoints[pointIndex])
                    : 0.0f;
                const float rootError = Clamp(Length(hybridRoot - teacherRoot) / 0.025f, 0.0f, 1.0f);
                guideModelErrorScores[guideIndex] = Clamp(std::max(rootError, Clamp(residualMagnitude / 0.025f, 0.0f, 1.0f)) * bundle.uncertaintyGain,
                                                          0.0f,
                                                          1.0f);
                guideContactScores[guideIndex] = Clamp(guideContactScores[guideIndex] * bundle.contactGain, 0.0f, 1.0f);
                guidePenetrationScores[guideIndex] = Clamp(guidePenetrationScores[guideIndex] * bundle.penetrationGain, 0.0f, 1.0f);
            }
            std::vector<float> guideUncertainty = BuildGuideUncertainty(teacherSequence.referenceAsset,
                                                                        currentHybridGuideRoots,
                                                                        previousHybridGuideRoots,
                                                                        previousGuideUncertainty,
                                                                        guideContactScores,
                                                                        guidePenetrationScores,
                                                                        guideModelErrorScores,
                                                                        1.0f / std::max(runtimeFrameRate, 1.0f),
                                                                        bundle.temporalCoherenceGain,
                                                                        bundle.uncertaintySmoothingAlpha);

            ActiveSetRuntimeInput runtimeInput = {};
            runtimeInput.currentGuideRoots = currentHybridGuideRoots;
            runtimeInput.previousGuideRoots = previousHybridGuideRoots;
            runtimeInput.guideContactScores = guideContactScores;
            runtimeInput.guideUncertainty = guideUncertainty;
            runtimeInput.cameraPosition = Vec3(0.0f, 1.55f, -1.85f);
            runtimeInput.cameraForward = Normalize(Vec3(0.0f, -0.05f, 1.0f));
            runtimeInput.fixedTimeStep = 1.0f / std::max(runtimeFrameRate, 1.0f);

            ActiveSetBudget budget = settings.useBudgetOverride ? settings.budgetOverride : bundle.budget;
            budget.previousFrameMilliseconds = previousFrameMilliseconds;
            ActiveSetPlan plan;
            if (!EvaluateActiveSetPlan(teacherSequence.referenceAsset, clusters, runtimeInput, bundle.scoringSettings, budget, plan, outError))
            {
                return false;
            }

            std::vector<Vec3> correctedPoints = hybridBeforeCorrection;
            SparseCorrectiveReport correctiveReport = {};
            if (!ApplySparseCorrectivePhysics(teacherSequence.referenceAsset,
                                              plan,
                                              runtimeColliders,
                                              correctedPoints,
                                              bundle.correctiveSettings,
                                              &correctiveReport,
                                              outError))
            {
                return false;
            }

            if (!AppendHairCacheFrame(hairCache, teacherSequence.referenceAsset, correctedPoints, outError))
            {
                return false;
            }
            std::vector<Vec3> guideFramePoints;
            BuildGuideFramePoints(teacherSequence.referenceAsset, correctedPoints, guideFramePoints);
            if (!AppendGuideCacheGuidePoints(guideCache, guideFramePoints, outError))
            {
                return false;
            }

            const float priorRms = ComputeRmsError(priorAsset.restPoints, teacherFrame);
            const float hybridBeforeRms = ComputeRmsError(hybridBeforeCorrection, teacherFrame);
            const float hybridAfterRms = ComputeRmsError(correctedPoints, teacherFrame);

            frameCsv << frameIndex << ","
                     << (static_cast<float>(frameIndex) / std::max(runtimeFrameRate, 1.0f)) << ","
                     << priorRms << ","
                     << hybridBeforeRms << ","
                     << hybridAfterRms << ","
                     << plan.summary.selectedClusterCount << ","
                     << plan.summary.selectedGuideCount << ","
                     << plan.summary.selectedStrandCount << ","
                     << plan.summary.budgetScale << ","
                     << plan.summary.recommendedSolverIterations << ","
                     << plan.summary.fullTierCount << ","
                     << plan.summary.reducedTierCount << ","
                     << plan.summary.frozenTierCount << ","
                     << plan.summary.cachedTierCount << ","
                     << correctiveReport.correctedStrandCount << ","
                     << correctiveReport.correctedPointCount << ","
                     << correctiveReport.averagePenetration << ","
                     << correctiveReport.maxPenetration << ","
                     << ComputeAverageScalar(guideContactScores) << ","
                     << ComputeAverageScalar(guidePenetrationScores) << ","
                     << ComputeAverageScalar(guideUncertainty) << ","
                     << ComputeMaxScalar(guideUncertainty) << ","
                     << ComputeAverageScalar(guideModelErrorScores) << "\n";

            if (settings.exportPerFrameHairBins)
            {
                HairAsset frameAsset = teacherSequence.referenceAsset;
                frameAsset.restPoints = correctedPoints;
                std::ostringstream name;
                name << "full_hybrid_frame_" << std::setw(4) << std::setfill('0') << frameIndex << ".hairbin";
                if (!SaveHairAssetToHairBinary(outputDirectory / name.str(), frameAsset, outError))
                {
                    return false;
                }

                ActiveSetDispatchPlan dispatchPlan = {};
                if (!BuildActiveSetDispatchPlan(teacherSequence.referenceAsset, plan, dispatchPlan, outError))
                {
                    return false;
                }

                ActiveSetDispatchPack dispatchPack = {};
                if (!BuildActiveSetDispatchPack(teacherSequence.referenceAsset, plan, dispatchPlan, dispatchPack, outError))
                {
                    return false;
                }

                std::ostringstream dispatchName;
                dispatchName << "full_hybrid_dispatch_" << std::setw(4) << std::setfill('0') << frameIndex << ".json";
                if (!WriteActiveSetDispatchPlanJson(outputDirectory / dispatchName.str(),
                                                    teacherSequence.referenceAsset,
                                                    plan,
                                                    dispatchPlan,
                                                    outError))
                {
                    return false;
                }

                std::ostringstream dispatchPackJsonName;
                dispatchPackJsonName << "full_hybrid_dispatch_pack_" << std::setw(4) << std::setfill('0') << frameIndex << ".json";
                if (!WriteActiveSetDispatchPackJson(outputDirectory / dispatchPackJsonName.str(),
                                                    teacherSequence.referenceAsset,
                                                    plan,
                                                    dispatchPack,
                                                    outError))
                {
                    return false;
                }

                std::ostringstream dispatchPackBinName;
                dispatchPackBinName << "full_hybrid_dispatch_pack_" << std::setw(4) << std::setfill('0') << frameIndex << ".dispatchbin";
                if (!SaveActiveSetDispatchPackToFile(outputDirectory / dispatchPackBinName.str(),
                                                     dispatchPack,
                                                     outError))
                {
                    return false;
                }
            }

            previousFrameMilliseconds = 1.10f +
                                        static_cast<float>(plan.summary.selectedClusterCount) * 0.32f +
                                        static_cast<float>(plan.summary.selectedGuideCount) * 0.012f +
                                        static_cast<float>(correctiveReport.correctedPointCount) * 0.0019f +
                                        static_cast<float>(plan.summary.recommendedSolverIterations) * 0.24f;
            previousSmoothedPriorGuidePoints = smoothedPriorGuidePoints;
            previousResidualGuidePoints = currentResidualGuidePoints;
            previousHybridGuideRoots = currentHybridGuideRoots;
            previousGuideUncertainty = guideUncertainty;

            priorRmsSum += static_cast<double>(priorRms);
            hybridBeforeRmsSum += static_cast<double>(hybridBeforeRms);
            hybridAfterRmsSum += static_cast<double>(hybridAfterRms);
            selectedClusterSum += static_cast<double>(plan.summary.selectedClusterCount);
            selectedGuideSum += static_cast<double>(plan.summary.selectedGuideCount);
            correctedPointSum += static_cast<double>(correctiveReport.correctedPointCount);
            penetrationSum += static_cast<double>(correctiveReport.averagePenetration);
            maxHybridAfterRms = std::max(maxHybridAfterRms, hybridAfterRms);
            maxPenetration = std::max(maxPenetration, correctiveReport.maxPenetration);
        }

        if (!SaveHairCacheToFile(outputDirectory / "full_hybrid_runtime.haircache", hairCache, outError) ||
            !SaveGuideCacheToFile(outputDirectory / "full_hybrid_runtime_guides.guidecache", guideCache, outError) ||
            !SaveHairAssetToHairBinary(outputDirectory / "full_hybrid_reference.hairbin", teacherSequence.referenceAsset, outError))
        {
            return false;
        }

        const float frameCountF = static_cast<float>(std::max<uint32_t>(1u, teacherSequence.FrameCount()));
        outReport.frameCount = teacherSequence.FrameCount();
        outReport.averagePriorRmsError = static_cast<float>(priorRmsSum / frameCountF);
        outReport.averageHybridRmsErrorBeforeCorrection = static_cast<float>(hybridBeforeRmsSum / frameCountF);
        outReport.averageHybridRmsErrorAfterCorrection = static_cast<float>(hybridAfterRmsSum / frameCountF);
        outReport.maxHybridRmsErrorAfterCorrection = maxHybridAfterRms;
        outReport.averageSelectedClusterCount = static_cast<float>(selectedClusterSum / frameCountF);
        outReport.averageSelectedGuideCount = static_cast<float>(selectedGuideSum / frameCountF);
        outReport.averageCorrectedPointCount = static_cast<float>(correctedPointSum / frameCountF);
        outReport.averagePenetration = static_cast<float>(penetrationSum / frameCountF);
        outReport.maxPenetration = maxPenetration;

        if (!WriteHybridSummaryJson(outputDirectory / "full_hybrid_summary.json", bundle, nullptr, &outReport, outError))
        {
            return false;
        }
        return true;
    }

}
