#include "hairgpu/HybridRuntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

#include "hairgpu/AssetIO.h"
#include "hairgpu/GuideCache.h"
#include "hairgpu/HairCache.h"
#include "hairgpu/Metrics.h"
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

        HairAsset BuildFrameAsset(const HairAsset& referenceAsset, const std::vector<Vec3>& points)
        {
            HairAsset frameAsset = referenceAsset;
            frameAsset.restPoints = points;
            if (frameAsset.pointWidths.size() != frameAsset.restPoints.size())
            {
                const float fallbackWidth = frameAsset.pointWidths.empty() ? 0.0025f : frameAsset.pointWidths.back();
                frameAsset.pointWidths.resize(frameAsset.restPoints.size(), fallbackWidth);
            }
            return frameAsset;
        }

        float EstimateRuntimeRamMegabytes(const HairAsset& asset,
                                          const HybridModelBundle& bundle,
                                          uint32_t clusterCount,
                                          uint32_t cachedFramePointCount)
        {
            uint64_t bytes = 0u;
            const uint64_t pointCount = static_cast<uint64_t>(asset.restPoints.size());
            const uint64_t strandCount = static_cast<uint64_t>(asset.strands.size());
            bytes += pointCount * static_cast<uint64_t>(sizeof(Vec3)) * 14u;
            bytes += pointCount * static_cast<uint64_t>(sizeof(float)) * 6u;
            bytes += strandCount * static_cast<uint64_t>(sizeof(StrandRange)) * 2u;
            bytes += static_cast<uint64_t>(asset.rootBindings.size()) * static_cast<uint64_t>(sizeof(RootBinding));
            bytes += static_cast<uint64_t>(clusterCount) * (16u * static_cast<uint64_t>(sizeof(uint32_t)) + 16u * static_cast<uint64_t>(sizeof(float)));
            bytes += static_cast<uint64_t>(bundle.priorModel.selectedGuideStrandIndices.size()) * static_cast<uint64_t>(sizeof(uint32_t));
            bytes += static_cast<uint64_t>(bundle.residualModel.selectedGuideStrandIndices.size()) * static_cast<uint64_t>(sizeof(uint32_t));
            bytes += static_cast<uint64_t>(bundle.priorModel.strandRecords.size()) * static_cast<uint64_t>(sizeof(ReducedModelStrandRecord));
            bytes += static_cast<uint64_t>(bundle.residualModel.strandRecords.size()) * static_cast<uint64_t>(sizeof(ReducedModelStrandRecord));
            bytes += static_cast<uint64_t>(bundle.priorModel.influences.size()) * static_cast<uint64_t>(sizeof(ReducedModelInfluence));
            bytes += static_cast<uint64_t>(bundle.residualModel.influences.size()) * static_cast<uint64_t>(sizeof(ReducedModelInfluence));
            bytes += static_cast<uint64_t>(bundle.priorModel.pointBiases.size()) * static_cast<uint64_t>(sizeof(Vec3));
            bytes += static_cast<uint64_t>(bundle.residualModel.pointBiases.size()) * static_cast<uint64_t>(sizeof(Vec3));
            bytes += static_cast<uint64_t>(bundle.activeSetPredictor.featureMean.size() + bundle.activeSetPredictor.featureStd.size() +
                                           bundle.activeSetPredictor.weights.size() + bundle.activeSetPredictor.hiddenWeights.size() +
                                           bundle.activeSetPredictor.hiddenBias.size() + bundle.activeSetPredictor.outputWeights.size()) * static_cast<uint64_t>(sizeof(float));
            bytes += static_cast<uint64_t>(bundle.priorTemporalNetwork.inputMean.size() + bundle.priorTemporalNetwork.inputStd.size() +
                                           bundle.priorTemporalNetwork.targetMean.size() + bundle.priorTemporalNetwork.targetStd.size() +
                                           bundle.priorTemporalNetwork.hiddenWeights.size() + bundle.priorTemporalNetwork.hiddenBias.size() +
                                           bundle.priorTemporalNetwork.hiddenWeights2.size() + bundle.priorTemporalNetwork.hiddenBias2.size() +
                                           bundle.priorTemporalNetwork.outputWeights.size() + bundle.priorTemporalNetwork.outputBias.size()) * static_cast<uint64_t>(sizeof(float));
            bytes += static_cast<uint64_t>(bundle.residualTemporalNetwork.inputMean.size() + bundle.residualTemporalNetwork.inputStd.size() +
                                           bundle.residualTemporalNetwork.targetMean.size() + bundle.residualTemporalNetwork.targetStd.size() +
                                           bundle.residualTemporalNetwork.hiddenWeights.size() + bundle.residualTemporalNetwork.hiddenBias.size() +
                                           bundle.residualTemporalNetwork.hiddenWeights2.size() + bundle.residualTemporalNetwork.hiddenBias2.size() +
                                           bundle.residualTemporalNetwork.outputWeights.size() + bundle.residualTemporalNetwork.outputBias.size()) * static_cast<uint64_t>(sizeof(float));
            bytes += static_cast<uint64_t>(cachedFramePointCount) * static_cast<uint64_t>(sizeof(Vec3)) * 2u;
            return static_cast<float>(static_cast<double>(bytes) / (1024.0 * 1024.0));
        }

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

        void CopyStrandPoints(const HairAsset& asset,
                              const std::vector<Vec3>& sourcePoints,
                              uint32_t strandIndex,
                              std::vector<Vec3>& outPoints)
        {
            outPoints.clear();
            if (strandIndex >= asset.strands.size())
            {
                return;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            outPoints.reserve(strand.pointCount);
            for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
            {
                const uint32_t globalIndex = strand.pointOffset + pointIndex;
                outPoints.push_back((globalIndex < sourcePoints.size()) ? sourcePoints[globalIndex] : Vec3(0.0f, 0.0f, 0.0f));
            }
        }

        void WriteStrandPoints(const HairAsset& asset,
                               uint32_t strandIndex,
                               const std::vector<Vec3>& strandPoints,
                               std::vector<Vec3>& inOutPoints)
        {
            if (strandIndex >= asset.strands.size())
            {
                return;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount && pointIndex < strandPoints.size(); ++pointIndex)
            {
                const uint32_t globalIndex = strand.pointOffset + pointIndex;
                if (globalIndex < inOutPoints.size())
                {
                    inOutPoints[globalIndex] = strandPoints[pointIndex];
                }
            }
        }

        void BuildRootAnchoredTierPose(const HairAsset& asset,
                                       uint32_t strandIndex,
                                       const std::vector<Vec3>& currentPoints,
                                       const std::vector<Vec3>& anchorPoints,
                                       float rootFollow,
                                       float anchorBlend,
                                       std::vector<Vec3>& outStrandPoints)
        {
            outStrandPoints.clear();
            if (strandIndex >= asset.strands.size())
            {
                return;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0u)
            {
                return;
            }

            const uint32_t rootIndex = strand.pointOffset;
            if (rootIndex >= currentPoints.size() || rootIndex >= anchorPoints.size())
            {
                return;
            }

            const Vec3 currentRoot = currentPoints[rootIndex];
            const Vec3 anchorRoot = anchorPoints[rootIndex];
            const Vec3 translation = (currentRoot - anchorRoot) * Clamp(rootFollow, 0.0f, 1.0f);
            const float blend = Clamp(anchorBlend, 0.0f, 1.0f);
            outStrandPoints.reserve(strand.pointCount);
            for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
            {
                const uint32_t globalIndex = strand.pointOffset + pointIndex;
                const Vec3 current = (globalIndex < currentPoints.size()) ? currentPoints[globalIndex] : Vec3(0.0f, 0.0f, 0.0f);
                const Vec3 anchored = (globalIndex < anchorPoints.size()) ? (anchorPoints[globalIndex] + translation) : current;
                outStrandPoints.push_back(Lerp(current, anchored, blend));
            }
            if (!outStrandPoints.empty())
            {
                outStrandPoints[0] = Lerp(currentRoot, anchorRoot + translation, blend);
            }
        }

        float EstimateSecondOrderPredictionGain(const std::vector<Vec3>& previousPreviousPoints,
                                                const std::vector<Vec3>& previousPoints,
                                                const std::vector<Vec3>& currentPoints,
                                                float velocityGain,
                                                float fallbackGain,
                                                float maxGain)
        {
            const size_t pointCount = std::min(previousPreviousPoints.size(), std::min(previousPoints.size(), currentPoints.size()));
            if (pointCount == 0u)
            {
                return Clamp(fallbackGain, 0.0f, maxGain);
            }

            double numerator = 0.0;
            double denominator = 0.0;
            for (size_t i = 0u; i < pointCount; ++i)
            {
                const Vec3 velocity = currentPoints[i] - previousPoints[i];
                const Vec3 acceleration = currentPoints[i] - previousPoints[i] * 2.0f + previousPreviousPoints[i];
                const Vec3 desiredResidual = velocity * Clamp(velocityGain, 0.0f, 1.0f);
                numerator += static_cast<double>(Dot(desiredResidual, acceleration));
                denominator += static_cast<double>(Dot(acceleration, acceleration));
            }
            if (denominator <= 1.0e-8)
            {
                return Clamp(fallbackGain, 0.0f, maxGain);
            }
            return Clamp(static_cast<float>(numerator / denominator), 0.0f, maxGain);
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

        std::vector<float> BuildGuideWindExposureScores(const HairAsset& asset,
                                                        const std::vector<Vec3>& currentRoots,
                                                        const std::vector<Vec3>& previousRoots,
                                                        float fixedTimeStep,
                                                        float windStrengthProxy,
                                                        const Vec3& windDirection)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> scores(guideCount, 0.0f);
            const Vec3 safeWindDir = Normalize((Length(windDirection) > 1.0e-6f) ? windDirection : Vec3(1.0f, 0.0f, 0.0f));
            const float windStrength = Clamp(windStrengthProxy, 0.0f, 1.0f);
            const float safeDt = std::max(fixedTimeStep, 1.0e-4f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                const Vec3 root = (guideIndex < currentRoots.size()) ? currentRoots[guideIndex] : Vec3(0.0f, 0.0f, 0.0f);
                const Vec3 previous = (guideIndex < previousRoots.size()) ? previousRoots[guideIndex] : root;
                Vec3 tangent = Vec3(0.0f, 1.0f, 0.0f);
                if (strand.pointCount > 1u && strand.pointOffset + 1u < asset.restPoints.size())
                {
                    tangent = Normalize(asset.restPoints[strand.pointOffset + 1u] - asset.restPoints[strand.pointOffset]);
                }
                const float velocityMagnitude = Clamp(Length(root - previous) / safeDt / 0.35f, 0.0f, 1.0f);
                const float crossWind = 1.0f - std::fabs(Dot(tangent, safeWindDir));
                const float heightFactor = Clamp((root.y - 0.9f) / 0.9f, 0.0f, 1.0f);
                scores[guideIndex] = Clamp(windStrength * (0.55f * crossWind + 0.25f * velocityMagnitude + 0.20f * heightFactor), 0.0f, 1.0f);
            }
            return scores;
        }

        std::vector<float> BuildGuideEnvironmentComplexityScores(const std::vector<float>& contactScores,
                                                                 const std::vector<float>& penetrationScores,
                                                                 const std::vector<float>& windExposureScores,
                                                                 const std::vector<float>& modelErrorScores)
        {
            const size_t count = std::max(std::max(contactScores.size(), penetrationScores.size()),
                                          std::max(windExposureScores.size(), modelErrorScores.size()));
            std::vector<float> scores(count, 0.0f);
            for (size_t i = 0u; i < count; ++i)
            {
                const float contact = (i < contactScores.size()) ? contactScores[i] : 0.0f;
                const float penetration = (i < penetrationScores.size()) ? penetrationScores[i] : 0.0f;
                const float wind = (i < windExposureScores.size()) ? windExposureScores[i] : 0.0f;
                const float modelError = (i < modelErrorScores.size()) ? modelErrorScores[i] : 0.0f;
                scores[i] = Clamp(contact * 0.35f + penetration * 0.20f + wind * 0.25f + modelError * 0.20f, 0.0f, 1.0f);
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
        frameCsv << "frameIndex,timeSeconds,selectedClusters,selectedGuides,selectedStrands,selectedNaturalnessGain,averageEfficiencyScore,editCoverageRatio,budgetScale,recommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctedStrands,correctedPoints,avgPenetration,maxPenetration,avgGuideContact,avgGuidePenetration,avgGuideUncertainty,maxGuideUncertainty,avgLengthErrorBefore,avgLengthErrorAfter,estimatedSimulationMilliseconds,estimatedPlanMemoryMegabytes\n";

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
            const float teacherWindStrengthProxy = Clamp(settings.swayAmplitude * settings.windCyclesPerSecond * 6.0f, 0.0f, 1.0f);
            const std::vector<float> guideWindExposureScores = BuildGuideWindExposureScores(asset,
                                                                                           currentGuideRoots,
                                                                                           previousGuideRoots,
                                                                                           1.0f / std::max(settings.frameRate, 1.0f),
                                                                                           teacherWindStrengthProxy,
                                                                                           Vec3(1.0f, 0.0f, 0.0f));
            const std::vector<float> guideEnvironmentComplexityScores = BuildGuideEnvironmentComplexityScores(guideContactScores,
                                                                                                             guidePenetrationScores,
                                                                                                             guideWindExposureScores,
                                                                                                             modelErrorScores);

            ActiveSetRuntimeInput runtimeInput = {};
            runtimeInput.currentGuideRoots = currentGuideRoots;
            runtimeInput.previousGuideRoots = previousGuideRoots;
            runtimeInput.guideContactScores = guideContactScores;
            runtimeInput.guidePenetrationScores = guidePenetrationScores;
            runtimeInput.guideUncertainty = guideUncertainty;
            runtimeInput.guideTemporalScores = guideUncertainty;
            runtimeInput.guideModelErrorScores = modelErrorScores;
            runtimeInput.guideWindExposureScores = guideWindExposureScores;
            runtimeInput.guideEnvironmentComplexityScores = guideEnvironmentComplexityScores;
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
                     << plan.summary.selectedNaturalnessGain << ","
                     << plan.summary.averageEfficiencyScore << ","
                     << plan.summary.editCoverageRatio << ","
                     << plan.summary.averageSelectedParticlesPerStrand << ","
                     << plan.summary.maxSelectedParticlesPerStrand << ","
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
                     << estimatedSimulationMilliseconds << ","
                     << plan.summary.estimatedMemoryMegabytes << "\n";

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

        void BuildGuidePointSequenceForModel(const ReducedModel& model,
                                            const ReducedModelTrainingSequence& sequence,
                                            std::vector<Vec3>& outGuideFrames)
        {
            outGuideFrames.clear();
            const uint32_t frameCount = sequence.FrameCount();
            const uint32_t guidePointCount = static_cast<uint32_t>(model.selectedGuideStrandIndices.size()) * model.referenceAsset.particlesPerStrand;
            outGuideFrames.reserve(static_cast<size_t>(frameCount) * guidePointCount);
            for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
            {
                std::vector<Vec3> fullFrame;
                CopySequenceFrame(sequence, frameIndex, fullFrame);
                std::vector<Vec3> guideFrame;
                BuildGuideFramePointsForModel(model, fullFrame, guideFrame);
                outGuideFrames.insert(outGuideFrames.end(), guideFrame.begin(), guideFrame.end());
            }
        }

        void BuildPriorTemporalTrainingSequences(const ReducedModel& model,
                                                 const ReducedModelTrainingSequence& teacherSequence,
                                                 const ReducedModelTrainingSequence& priorSequence,
                                                 std::vector<Vec3>& outInputFrames,
                                                 std::vector<Vec3>& outTargetFrames)
        {
            BuildGuidePointSequenceForModel(model, teacherSequence, outInputFrames);
            BuildGuidePointSequenceForModel(model, priorSequence, outTargetFrames);
        }

        void BuildResidualTemporalTrainingSequences(const ReducedModel& model,
                                                    const ReducedModelTrainingSequence& teacherSequence,
                                                    const ReducedModelTrainingSequence& priorSequence,
                                                    float residualGuideGain,
                                                    std::vector<Vec3>& outInputFrames,
                                                    std::vector<Vec3>& outTargetFrames)
        {
            outInputFrames.clear();
            outTargetFrames.clear();
            const uint32_t frameCount = std::min(teacherSequence.FrameCount(), priorSequence.FrameCount());
            for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
            {
                std::vector<Vec3> teacherFrame;
                std::vector<Vec3> priorFrame;
                CopySequenceFrame(teacherSequence, frameIndex, teacherFrame);
                CopySequenceFrame(priorSequence, frameIndex, priorFrame);

                std::vector<Vec3> currentResidualGuidePoints;
                std::vector<Vec3> priorResidualGuidePoints;
                std::vector<Vec3> restResidualGuidePoints;
                BuildGuideFramePointsForModel(model, teacherFrame, currentResidualGuidePoints);
                BuildGuideFramePointsForModel(model, priorFrame, priorResidualGuidePoints);
                BuildRestGuideFramePointsForModel(model, restResidualGuidePoints);

                std::vector<Vec3> inputFrame = restResidualGuidePoints;
                for (uint32_t i = 0u; i < inputFrame.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    inputFrame[i] += (currentResidualGuidePoints[i] - priorResidualGuidePoints[i]) * residualGuideGain;
                }
                outInputFrames.insert(outInputFrames.end(), inputFrame.begin(), inputFrame.end());
                outTargetFrames.insert(outTargetFrames.end(), currentResidualGuidePoints.begin(), currentResidualGuidePoints.end());
            }
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

        float ComputeQuantile95(std::vector<float> values)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            std::sort(values.begin(), values.end());
            const size_t index = std::min(values.size() - 1u,
                                          static_cast<size_t>(std::floor(static_cast<double>(values.size() - 1u) * 0.95)));
            return values[index];
        }

        float SafeSigmoid(float x)
        {
            if (x >= 0.0f)
            {
                const float e = std::exp(-x);
                return 1.0f / (1.0f + e);
            }
            const float e = std::exp(x);
            return e / (1.0f + e);
        }

        float AverageScalarAtIndices(const std::vector<float>& values, const std::vector<uint32_t>& indices)
        {
            if (values.empty() || indices.empty())
            {
                return 0.0f;
            }
            double total = 0.0;
            uint32_t count = 0u;
            for (uint32_t index : indices)
            {
                if (index < values.size())
                {
                    total += static_cast<double>(values[index]);
                    ++count;
                }
            }
            return (count > 0u) ? static_cast<float>(total / static_cast<double>(count)) : 0.0f;
        }

        void BuildGuideBoundsLocal(const std::vector<Vec3>& roots, Vec3& outMin, Vec3& outMax)
        {
            if (roots.empty())
            {
                outMin = Vec3(0.0f, 0.0f, 0.0f);
                outMax = Vec3(0.0f, 0.0f, 0.0f);
                return;
            }
            outMin = roots.front();
            outMax = roots.front();
            for (const Vec3& root : roots)
            {
                outMin.x = std::min(outMin.x, root.x);
                outMin.y = std::min(outMin.y, root.y);
                outMin.z = std::min(outMin.z, root.z);
                outMax.x = std::max(outMax.x, root.x);
                outMax.y = std::max(outMax.y, root.y);
                outMax.z = std::max(outMax.z, root.z);
            }
        }

        Vec3 AveragePositionLocal(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices)
        {
            if (positions.empty() || indices.empty())
            {
                return Vec3(0.0f, 0.0f, 0.0f);
            }
            Vec3 sum = {};
            uint32_t count = 0u;
            for (uint32_t index : indices)
            {
                if (index < positions.size())
                {
                    sum += positions[index];
                    ++count;
                }
            }
            return (count > 0u) ? (sum / static_cast<float>(count)) : Vec3(0.0f, 0.0f, 0.0f);
        }

        float ComputeDistanceVisibilityLocal(const Vec3& position,
                                             const Vec3& cameraPosition,
                                             const Vec3& cameraForward,
                                             float nearDistance,
                                             float farDistance)
        {
            const Vec3 toPoint = position - cameraPosition;
            const float distance = Length(toPoint);
            const float distanceWeight = 1.0f - Clamp((distance - nearDistance) / std::max(farDistance - nearDistance, 1.0e-4f), 0.0f, 1.0f);
            const Vec3 direction = Normalize(toPoint);
            const float facing = Clamp(Dot(direction, Normalize(cameraForward)) * 0.5f + 0.5f, 0.0f, 1.0f);
            return Clamp(distanceWeight * 0.75f + facing * 0.25f, 0.0f, 1.0f);
        }

        void ExtractActiveSetPredictorFeatures(const std::vector<ActiveSetCluster>& clusters,
                                               const std::vector<Vec3>& restGuideRoots,
                                               const std::vector<Vec3>& currentGuideRoots,
                                               const std::vector<Vec3>& previousGuideRoots,
                                               const std::vector<float>& guideUncertainty,
                                               const std::vector<float>& guideContactScores,
                                               const std::vector<float>& guidePenetrationScores,
                                               const std::vector<float>& guideModelErrorScores,
                                               const Vec3& cameraPosition,
                                               const Vec3& cameraForward,
                                               const ActiveSetScoringSettings& scoringSettings,
                                               uint32_t clusterIndex,
                                               std::array<float, 9u>& outFeatures)
        {
            outFeatures.fill(0.0f);
            if (clusterIndex >= clusters.size() || restGuideRoots.empty())
            {
                return;
            }

            const ActiveSetCluster& cluster = clusters[clusterIndex];
            Vec3 boundsMin = {};
            Vec3 boundsMax = {};
            BuildGuideBoundsLocal(restGuideRoots, boundsMin, boundsMax);
            const Vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;
            const float halfWidth = std::max((boundsMax.x - boundsMin.x) * 0.5f, 1.0e-4f);
            size_t maxClusterStrands = 1u;
            for (const ActiveSetCluster& candidate : clusters)
            {
                maxClusterStrands = std::max(maxClusterStrands, candidate.strandIndices.size());
            }
            const float safeTimeStep = 1.0f / 60.0f;
            const Vec3 centroidCurrent = AveragePositionLocal(currentGuideRoots, cluster.guideStrandIndices);
            const Vec3 centroidPrevious = AveragePositionLocal(previousGuideRoots, cluster.guideStrandIndices);
            const Vec3 centroidRest = AveragePositionLocal(restGuideRoots, cluster.guideStrandIndices);
            outFeatures[0] = Clamp(Length(centroidCurrent - centroidPrevious) / safeTimeStep /
                                   std::max(scoringSettings.velocitySoftRange, 1.0e-4f),
                                   0.0f,
                                   1.0f);
            outFeatures[1] = Clamp(Length(centroidCurrent - centroidRest) /
                                   std::max(scoringSettings.displacementSoftRange, 1.0e-4f),
                                   0.0f,
                                   1.0f);
            outFeatures[2] = ComputeDistanceVisibilityLocal(centroidCurrent,
                                                       cameraPosition,
                                                       cameraForward,
                                                       scoringSettings.nearDistance,
                                                       scoringSettings.farDistance);
            outFeatures[3] = Clamp(std::fabs(centroidRest.x - boundsCenter.x) / halfWidth, 0.0f, 1.0f);
            outFeatures[4] = Clamp(AverageScalarAtIndices(guideUncertainty, cluster.guideStrandIndices), 0.0f, 1.0f);
            outFeatures[5] = Clamp(AverageScalarAtIndices(guideContactScores, cluster.guideStrandIndices), 0.0f, 1.0f);
            outFeatures[6] = Clamp(AverageScalarAtIndices(guidePenetrationScores, cluster.guideStrandIndices), 0.0f, 1.0f);
            outFeatures[7] = Clamp(AverageScalarAtIndices(guideModelErrorScores, cluster.guideStrandIndices), 0.0f, 1.0f);
            outFeatures[8] = Clamp(static_cast<float>(cluster.strandIndices.size()) / static_cast<float>(std::max<size_t>(1u, maxClusterStrands)),
                                   0.0f,
                                   1.0f);
        }

        float PredictActiveSetRisk(const ActiveSetPredictor& predictor, const std::array<float, 9u>& features)
        {
            if (!IsActiveSetPredictorReady(predictor) || predictor.featureCount == 0u)
            {
                return 0.0f;
            }
            const uint32_t featureCount = std::min<uint32_t>(predictor.featureCount, static_cast<uint32_t>(features.size()));
            std::array<float, 9u> normalized = {};
            for (uint32_t featureIndex = 0u; featureIndex < featureCount; ++featureIndex)
            {
                const float mean = (featureIndex < predictor.featureMean.size()) ? predictor.featureMean[featureIndex] : 0.0f;
                const float stdDev = (featureIndex < predictor.featureStd.size()) ? std::max(predictor.featureStd[featureIndex], 1.0e-4f) : 1.0f;
                normalized[featureIndex] = ((features[featureIndex] - mean) / stdDev);
            }
            if (predictor.hiddenWidth > 0u &&
                predictor.hiddenWeights.size() == static_cast<size_t>(predictor.hiddenWidth) * predictor.featureCount &&
                predictor.hiddenBias.size() == predictor.hiddenWidth &&
                predictor.outputWeights.size() == predictor.hiddenWidth)
            {
                std::vector<float> hidden(predictor.hiddenWidth, 0.0f);
                for (uint32_t h = 0u; h < predictor.hiddenWidth; ++h)
                {
                    float sum = predictor.hiddenBias[h];
                    const size_t rowOffset = static_cast<size_t>(h) * predictor.featureCount;
                    for (uint32_t i = 0u; i < featureCount; ++i)
                    {
                        sum += predictor.hiddenWeights[rowOffset + i] * normalized[i];
                    }
                    hidden[h] = std::tanh(sum);
                }
                float sum = predictor.outputBias;
                for (uint32_t h = 0u; h < predictor.hiddenWidth; ++h)
                {
                    sum += predictor.outputWeights[h] * hidden[h];
                }
                return Clamp(SafeSigmoid(sum), 0.0f, 1.0f);
            }
            float sum = predictor.bias;
            for (uint32_t featureIndex = 0u; featureIndex < featureCount; ++featureIndex)
            {
                const float weight = (featureIndex < predictor.weights.size()) ? predictor.weights[featureIndex] : 0.0f;
                sum += normalized[featureIndex] * weight;
            }
            return Clamp(SafeSigmoid(sum), 0.0f, 1.0f);
        }

        void ApplyActiveSetPredictorBoost(const HairAsset& asset,
                                          const std::vector<ActiveSetCluster>& clusters,
                                          const std::vector<Vec3>& restGuideRoots,
                                          const std::vector<Vec3>& currentGuideRoots,
                                          const std::vector<Vec3>& previousGuideRoots,
                                          const std::vector<float>& guidePenetrationScores,
                                          const std::vector<float>& guideModelErrorScores,
                                          const ActiveSetPredictor& predictor,
                                          float predictorBlend,
                                          const ActiveSetScoringSettings& scoringSettings,
                                          std::vector<float>& inOutGuideUncertainty,
                                          std::vector<float>& inOutGuideContactScores,
                                          std::vector<float>* outGuidePredictorRisk)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            if (!IsActiveSetPredictorReady(predictor) || guideCount == 0u || predictorBlend <= 1.0e-5f)
            {
                if (outGuidePredictorRisk)
                {
                    outGuidePredictorRisk->assign(guideCount, 0.0f);
                }
                return;
            }
            if (inOutGuideUncertainty.size() < guideCount)
            {
                inOutGuideUncertainty.resize(guideCount, 0.0f);
            }
            if (inOutGuideContactScores.size() < guideCount)
            {
                inOutGuideContactScores.resize(guideCount, 0.0f);
            }
            if (outGuidePredictorRisk)
            {
                outGuidePredictorRisk->assign(guideCount, 0.0f);
            }

            for (uint32_t clusterIndex = 0u; clusterIndex < clusters.size(); ++clusterIndex)
            {
                std::array<float, 9u> features = {};
                ExtractActiveSetPredictorFeatures(clusters,
                                                  restGuideRoots,
                                                  currentGuideRoots,
                                                  previousGuideRoots,
                                                  inOutGuideUncertainty,
                                                  inOutGuideContactScores,
                                                  guidePenetrationScores,
                                                  guideModelErrorScores,
                                                  Vec3(0.0f, 1.55f, -1.85f),
                                                  Normalize(Vec3(0.0f, -0.05f, 1.0f)),
                                                  scoringSettings,
                                                  clusterIndex,
                                                  features);
                const float risk = PredictActiveSetRisk(predictor, features);
                const ActiveSetCluster& cluster = clusters[clusterIndex];
                for (uint32_t guideIndex : cluster.guideStrandIndices)
                {
                    if (guideIndex >= guideCount)
                    {
                        continue;
                    }
                    inOutGuideUncertainty[guideIndex] = Clamp(std::max(inOutGuideUncertainty[guideIndex], risk * predictorBlend), 0.0f, 1.0f);
                    inOutGuideContactScores[guideIndex] = Clamp(inOutGuideContactScores[guideIndex] + risk * predictorBlend * 0.18f, 0.0f, 1.0f);
                    if (outGuidePredictorRisk)
                    {
                        (*outGuidePredictorRisk)[guideIndex] = std::max((*outGuidePredictorRisk)[guideIndex], risk);
                    }
                }
            }
        }

        void BuildPurePhysicsProxyPlan(const HairAsset& asset,
                                       const std::vector<ActiveSetCluster>& clusters,
                                       const ActiveSetBudget& budget,
                                       ActiveSetPlan& outPlan)
        {
            outPlan = {};
            outPlan.clusters = clusters;
            outPlan.scores.reserve(clusters.size());
            outPlan.recommendedSimulationTiers.assign(asset.strands.size(), SimTier::Full);
            outPlan.strandCorrectiveMask.assign(asset.strands.size(), 1u);
            for (const ActiveSetCluster& cluster : clusters)
            {
                ActiveSetClusterScore score = {};
                score.clusterIndex = cluster.clusterIndex;
                score.finalScore = 1.0f;
                score.selected = true;
                outPlan.scores.push_back(score);
                outPlan.selectedClusterIndices.push_back(cluster.clusterIndex);
                outPlan.selectedGuideStrandIndices.insert(outPlan.selectedGuideStrandIndices.end(),
                                                          cluster.guideStrandIndices.begin(),
                                                          cluster.guideStrandIndices.end());
                outPlan.selectedStrandIndices.insert(outPlan.selectedStrandIndices.end(),
                                                     cluster.strandIndices.begin(),
                                                     cluster.strandIndices.end());
            }
            std::sort(outPlan.selectedGuideStrandIndices.begin(), outPlan.selectedGuideStrandIndices.end());
            outPlan.selectedGuideStrandIndices.erase(std::unique(outPlan.selectedGuideStrandIndices.begin(), outPlan.selectedGuideStrandIndices.end()),
                                                     outPlan.selectedGuideStrandIndices.end());
            std::sort(outPlan.selectedStrandIndices.begin(), outPlan.selectedStrandIndices.end());
            outPlan.selectedStrandIndices.erase(std::unique(outPlan.selectedStrandIndices.begin(), outPlan.selectedStrandIndices.end()),
                                                outPlan.selectedStrandIndices.end());
            outPlan.summary.clusterCount = static_cast<uint32_t>(clusters.size());
            outPlan.summary.selectedClusterCount = static_cast<uint32_t>(clusters.size());
            outPlan.summary.selectedGuideCount = static_cast<uint32_t>(outPlan.selectedGuideStrandIndices.size());
            outPlan.summary.selectedStrandCount = static_cast<uint32_t>(outPlan.selectedStrandIndices.size());
            outPlan.summary.highestScore = 1.0f;
            outPlan.summary.lowestSelectedScore = 1.0f;
            outPlan.summary.averageSelectedScore = 1.0f;
            outPlan.summary.budgetScale = 1.0f;
            outPlan.summary.recommendedSolverIterations = std::max<uint32_t>(budget.maxSolverIterations, budget.minSolverIterations);
            outPlan.summary.fullTierCount = static_cast<uint32_t>(asset.strands.size());
        }

        void BuildActiveSetPredictorTarget(const std::vector<ActiveSetCluster>& clusters,
                                           const ActiveSetPlan& plan,
                                           const std::vector<float>& guideContactScores,
                                           const std::vector<float>& guidePenetrationScores,
                                           const std::vector<float>& guideModelErrorScores,
                                           std::vector<float>& outTargets)
        {
            outTargets.assign(clusters.size(), 0.0f);
            for (uint32_t clusterIndex = 0u; clusterIndex < clusters.size(); ++clusterIndex)
            {
                const ActiveSetCluster& cluster = clusters[clusterIndex];
                const float contact = AverageScalarAtIndices(guideContactScores, cluster.guideStrandIndices);
                const float penetration = AverageScalarAtIndices(guidePenetrationScores, cluster.guideStrandIndices);
                const float modelError = AverageScalarAtIndices(guideModelErrorScores, cluster.guideStrandIndices);
                const bool selected = std::find(plan.selectedClusterIndices.begin(), plan.selectedClusterIndices.end(), clusterIndex) != plan.selectedClusterIndices.end();
                const float score = std::max(selected ? 1.0f : 0.0f,
                                             Clamp(contact * 0.55f + penetration * 0.60f + modelError * 0.45f,
                                                   0.0f,
                                                   1.0f));
                outTargets[clusterIndex] = score;
            }
        }

        void AppendPredictorTrainingRows(const std::vector<ActiveSetCluster>& clusters,
                                         const std::vector<Vec3>& restGuideRoots,
                                         const std::vector<Vec3>& currentGuideRoots,
                                         const std::vector<Vec3>& previousGuideRoots,
                                         const std::vector<float>& guideUncertainty,
                                         const std::vector<float>& guideContactScores,
                                         const std::vector<float>& guidePenetrationScores,
                                         const std::vector<float>& guideModelErrorScores,
                                         const ActiveSetScoringSettings& scoringSettings,
                                         const std::vector<float>& targets,
                                         std::vector<std::array<float, 9u>>& outFeatures,
                                         std::vector<float>& outTargets)
        {
            for (uint32_t clusterIndex = 0u; clusterIndex < clusters.size() && clusterIndex < targets.size(); ++clusterIndex)
            {
                std::array<float, 9u> features = {};
                ExtractActiveSetPredictorFeatures(clusters,
                                                  restGuideRoots,
                                                  currentGuideRoots,
                                                  previousGuideRoots,
                                                  guideUncertainty,
                                                  guideContactScores,
                                                  guidePenetrationScores,
                                                  guideModelErrorScores,
                                                  Vec3(0.0f, 1.55f, -1.85f),
                                                  Normalize(Vec3(0.0f, -0.05f, 1.0f)),
                                                  scoringSettings,
                                                  clusterIndex,
                                                  features);
                outFeatures.push_back(features);
                outTargets.push_back(targets[clusterIndex]);
            }
        }

        void BuildActiveSetPredictorModel(const std::vector<std::array<float, 9u>>& featureRows,
                                          const std::vector<float>& targets,
                                          const ActiveSetPredictorSettings& settings,
                                          ActiveSetPredictor& outPredictor,
                                          ActiveSetPredictorEvaluation* outEvaluation)
        {
            outPredictor = {};
            outPredictor.settings = settings;
            outPredictor.featureCount = 9u;
            outPredictor.hiddenWidth = std::max<uint32_t>(4u, settings.hiddenWidth);
            outPredictor.featureMean.assign(outPredictor.featureCount, 0.0f);
            outPredictor.featureStd.assign(outPredictor.featureCount, 1.0f);
            outPredictor.weights.assign(outPredictor.featureCount, 0.0f);
            outPredictor.hiddenWeights.assign(static_cast<size_t>(outPredictor.hiddenWidth) * outPredictor.featureCount, 0.0f);
            outPredictor.hiddenBias.assign(outPredictor.hiddenWidth, 0.0f);
            outPredictor.outputWeights.assign(outPredictor.hiddenWidth, 0.0f);
            outPredictor.outputBias = 0.0f;
            if (featureRows.empty() || targets.empty())
            {
                if (outEvaluation) *outEvaluation = {};
                return;
            }
            const uint32_t sampleCount = static_cast<uint32_t>(std::min(featureRows.size(), targets.size()));
            for (uint32_t featureIndex = 0u; featureIndex < outPredictor.featureCount; ++featureIndex)
            {
                double sum = 0.0;
                for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
                {
                    sum += static_cast<double>(featureRows[sampleIndex][featureIndex]);
                }
                outPredictor.featureMean[featureIndex] = static_cast<float>(sum / static_cast<double>(sampleCount));
                double variance = 0.0;
                for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
                {
                    const double d = static_cast<double>(featureRows[sampleIndex][featureIndex]) - outPredictor.featureMean[featureIndex];
                    variance += d * d;
                }
                outPredictor.featureStd[featureIndex] = static_cast<float>(std::sqrt(std::max(variance / static_cast<double>(sampleCount), 1.0e-8)));
            }

            std::mt19937 rng(4242u);
            std::uniform_real_distribution<float> init(-0.08f, 0.08f);
            for (float& value : outPredictor.hiddenWeights) value = init(rng);
            for (float& value : outPredictor.outputWeights) value = init(rng);

            const float wd = std::max(settings.weightDecay, 0.0f);
            std::array<float, 9u> normalized = {};
            std::vector<float> hidden(outPredictor.hiddenWidth, 0.0f);
            std::vector<float> gradHidden(outPredictor.hiddenWidth, 0.0f);
            std::vector<uint32_t> trainingOrder;
            std::vector<uint32_t> validationOrder;
            const uint32_t validationStride = std::max<uint32_t>(2u, settings.validationStride);
            for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
            {
                if ((sampleIndex % validationStride) == 0u)
                {
                    validationOrder.push_back(sampleIndex);
                }
                else
                {
                    trainingOrder.push_back(sampleIndex);
                }
            }
            if (trainingOrder.empty())
            {
                trainingOrder = validationOrder;
                validationOrder.clear();
            }
            auto evaluatePredictor = [&](const std::vector<uint32_t>& evalOrder, ActiveSetPredictorEvaluation& outEval)
            {
                outEval = {};
                outEval.sampleCount = static_cast<uint32_t>(evalOrder.size());
                if (evalOrder.empty())
                {
                    return;
                }
                double sq = 0.0;
                uint32_t correct = 0u;
                uint32_t positives = 0u;
                for (uint32_t sampleIndex : evalOrder)
                {
                    const float prediction = PredictActiveSetRisk(outPredictor, featureRows[sampleIndex]);
                    const float target = Clamp(targets[sampleIndex], 0.0f, 1.0f);
                    const float d = prediction - target;
                    sq += static_cast<double>(d * d);
                    if ((prediction >= 0.5f) == (target >= 0.5f))
                    {
                        ++correct;
                    }
                    if (target >= 0.5f)
                    {
                        ++positives;
                    }
                }
                outEval.rmsScoreError = static_cast<float>(std::sqrt(sq / static_cast<double>(std::max<size_t>(1u, evalOrder.size()))));
                outEval.classificationAccuracy = static_cast<float>(correct) / static_cast<float>(std::max<size_t>(1u, evalOrder.size()));
                outEval.positiveRate = static_cast<float>(positives) / static_cast<float>(std::max<size_t>(1u, evalOrder.size()));
            };

            float lr = std::max(settings.learningRate, settings.minLearningRate);
            float bestValidationRms = std::numeric_limits<float>::max();
            uint32_t epochsWithoutImprovement = 0u;
            ActiveSetPredictor bestPredictor = outPredictor;
            for (uint32_t epoch = 0u; epoch < std::max<uint32_t>(1u, settings.epochs); ++epoch)
            {
                std::shuffle(trainingOrder.begin(), trainingOrder.end(), rng);
                lr = std::max(settings.minLearningRate, lr * ((epoch == 0u) ? 1.0f : settings.learningRateDecay));
                for (uint32_t sampleIndex : trainingOrder)
                {
                    for (uint32_t featureIndex = 0u; featureIndex < outPredictor.featureCount; ++featureIndex)
                    {
                        normalized[featureIndex] = (featureRows[sampleIndex][featureIndex] - outPredictor.featureMean[featureIndex]) /
                                                   std::max(outPredictor.featureStd[featureIndex], 1.0e-4f);
                    }
                    for (uint32_t h = 0u; h < outPredictor.hiddenWidth; ++h)
                    {
                        float sum = outPredictor.hiddenBias[h];
                        const size_t rowOffset = static_cast<size_t>(h) * outPredictor.featureCount;
                        for (uint32_t featureIndex = 0u; featureIndex < outPredictor.featureCount; ++featureIndex)
                        {
                            sum += outPredictor.hiddenWeights[rowOffset + featureIndex] * normalized[featureIndex];
                        }
                        hidden[h] = std::tanh(sum);
                    }
                    float logit = outPredictor.outputBias;
                    for (uint32_t h = 0u; h < outPredictor.hiddenWidth; ++h)
                    {
                        logit += outPredictor.outputWeights[h] * hidden[h];
                    }
                    const float prediction = SafeSigmoid(logit);
                    const float target = Clamp(targets[sampleIndex], 0.0f, 1.0f);
                    const float classWeight = (target >= 0.5f) ? std::max(settings.positiveClassWeight, 1.0f) : 1.0f;
                    const float error = Clamp((prediction - target) * classWeight,
                                              -settings.gradientClip,
                                              settings.gradientClip);
                    outPredictor.outputBias -= lr * error;
                    for (uint32_t h = 0u; h < outPredictor.hiddenWidth; ++h)
                    {
                        gradHidden[h] = Clamp(error * outPredictor.outputWeights[h] * (1.0f - hidden[h] * hidden[h]),
                                              -settings.gradientClip,
                                              settings.gradientClip);
                        outPredictor.outputWeights[h] -= lr * (error * hidden[h] + wd * outPredictor.outputWeights[h]);
                    }
                    for (uint32_t h = 0u; h < outPredictor.hiddenWidth; ++h)
                    {
                        const size_t rowOffset = static_cast<size_t>(h) * outPredictor.featureCount;
                        outPredictor.hiddenBias[h] -= lr * gradHidden[h];
                        for (uint32_t featureIndex = 0u; featureIndex < outPredictor.featureCount; ++featureIndex)
                        {
                            outPredictor.hiddenWeights[rowOffset + featureIndex] -= lr * (gradHidden[h] * normalized[featureIndex] + wd * outPredictor.hiddenWeights[rowOffset + featureIndex]);
                        }
                    }
                }
                ActiveSetPredictorEvaluation validationEval = {};
                evaluatePredictor(validationOrder.empty() ? trainingOrder : validationOrder, validationEval);
                if (validationEval.sampleCount > 0u && validationEval.rmsScoreError < bestValidationRms - 1.0e-6f)
                {
                    bestValidationRms = validationEval.rmsScoreError;
                    bestPredictor = outPredictor;
                    epochsWithoutImprovement = 0u;
                }
                else
                {
                    ++epochsWithoutImprovement;
                }
                if (!validationOrder.empty() && epochsWithoutImprovement >= std::max<uint32_t>(3u, settings.earlyStoppingPatience))
                {
                    break;
                }
            }
            outPredictor = bestPredictor;

            if (outEvaluation)
            {
                evaluatePredictor(validationOrder.empty() ? trainingOrder : validationOrder, *outEvaluation);
            }
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
            out << "budgetStandardParticlesPerStrand=" << bundle.budget.standardParticlesPerStrand << "\n";
            out << "budgetMaxParticlesPerStrand=" << bundle.budget.maxParticlesPerStrand << "\n";
            out << "budgetMaxRefinementLevel=" << bundle.budget.maxRefinementLevel << "\n";
            out << "budgetMemoryBudgetMB=" << bundle.budget.memoryBudgetMegabytes << "\n";
            out << "budgetFixedMemoryMB=" << bundle.budget.estimatedFixedMemoryMegabytes << "\n";
            out << "budgetClusterMemoryMB=" << bundle.budget.estimatedClusterMemoryMegabytes << "\n";
            out << "scoringPenetrationWeight=" << bundle.scoringSettings.penetrationWeight << "\n";
            out << "scoringBoundaryWeight=" << bundle.scoringSettings.boundaryWeight << "\n";
            out << "scoringTemporalWeight=" << bundle.scoringSettings.temporalWeight << "\n";
            out << "scoringLeakageWeight=" << bundle.scoringSettings.leakageWeight << "\n";
            out << "scoringModelErrorWeight=" << bundle.scoringSettings.modelErrorWeight << "\n";
            out << "scoringWindWeight=" << bundle.scoringSettings.windWeight << "\n";
            out << "scoringEnvironmentWeight=" << bundle.scoringSettings.environmentWeight << "\n";
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
            out << "priorAccelerationPredictionGain=" << bundle.priorAccelerationPredictionGain << "\n";
            out << "residualAccelerationPredictionGain=" << bundle.residualAccelerationPredictionGain << "\n";
            out << "priorTemporalBlend=" << bundle.priorTemporalBlend << "\n";
            out << "residualTemporalBlend=" << bundle.residualTemporalBlend << "\n";
            out << "activeSetPredictorBlend=" << bundle.activeSetPredictorBlend << "\n";
            out << "frozenStrandBlend=" << bundle.frozenStrandBlend << "\n";
            out << "frozenRootFollow=" << bundle.frozenRootFollow << "\n";
            out << "cachedStrandBlend=" << bundle.cachedStrandBlend << "\n";
            out << "cachedRootFollow=" << bundle.cachedRootFollow << "\n";
            out << "cachedFrameRefreshInterval=" << bundle.cachedFrameRefreshInterval << "\n";
            if (report)
            {
                out << "reportPriorRms=" << report->priorEvaluation.rmsPositionError << "\n";
                out << "reportResidualRms=" << report->residualEvaluation.rmsPositionError << "\n";
                out << "reportAverageResidualMagnitude=" << report->averageResidualMagnitude << "\n";
                out << "reportMaxResidualMagnitude=" << report->maxResidualMagnitude << "\n";
                out << "reportPriorTemporalRms=" << report->priorTemporalEvaluation.rmsPositionError << "\n";
                out << "reportResidualTemporalRms=" << report->residualTemporalEvaluation.rmsPositionError << "\n";
                out << "reportActiveSetPredictorRms=" << report->activeSetPredictorEvaluation.rmsScoreError << "\n";
                out << "reportActiveSetPredictorAccuracy=" << report->activeSetPredictorEvaluation.classificationAccuracy << "\n";
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
            else if (key == "scoringPenetrationWeight") ioBundle.scoringSettings.penetrationWeight = toFloat(ioBundle.scoringSettings.penetrationWeight);
            else if (key == "scoringBoundaryWeight") ioBundle.scoringSettings.boundaryWeight = toFloat(ioBundle.scoringSettings.boundaryWeight);
            else if (key == "scoringTemporalWeight") ioBundle.scoringSettings.temporalWeight = toFloat(ioBundle.scoringSettings.temporalWeight);
            else if (key == "scoringLeakageWeight") ioBundle.scoringSettings.leakageWeight = toFloat(ioBundle.scoringSettings.leakageWeight);
            else if (key == "scoringModelErrorWeight") ioBundle.scoringSettings.modelErrorWeight = toFloat(ioBundle.scoringSettings.modelErrorWeight);
            else if (key == "scoringWindWeight") ioBundle.scoringSettings.windWeight = toFloat(ioBundle.scoringSettings.windWeight);
            else if (key == "scoringEnvironmentWeight") ioBundle.scoringSettings.environmentWeight = toFloat(ioBundle.scoringSettings.environmentWeight);
            else if (key == "scoringNaturalnessGainWeight") ioBundle.scoringSettings.naturalnessGainWeight = toFloat(ioBundle.scoringSettings.naturalnessGainWeight);
            else if (key == "scoringEfficiencyWeight") ioBundle.scoringSettings.efficiencyWeight = toFloat(ioBundle.scoringSettings.efficiencyWeight);
            else if (key == "budgetMaxActiveClusters") ioBundle.budget.maxActiveClusters = toUInt(ioBundle.budget.maxActiveClusters);
            else if (key == "budgetMaxActiveGuides") ioBundle.budget.maxActiveGuides = toUInt(ioBundle.budget.maxActiveGuides);
            else if (key == "budgetPromoteScoreThreshold") ioBundle.budget.promoteScoreThreshold = toFloat(ioBundle.budget.promoteScoreThreshold);
            else if (key == "budgetTargetFrameMs") ioBundle.budget.targetFrameMilliseconds = toFloat(ioBundle.budget.targetFrameMilliseconds);
            else if (key == "budgetMinSolverIterations") ioBundle.budget.minSolverIterations = toUInt(ioBundle.budget.minSolverIterations);
            else if (key == "budgetMaxSolverIterations") ioBundle.budget.maxSolverIterations = toUInt(ioBundle.budget.maxSolverIterations);
            else if (key == "budgetStandardParticlesPerStrand") ioBundle.budget.standardParticlesPerStrand = ClampRuntimeParticlesPerStrand(toUInt(ioBundle.budget.standardParticlesPerStrand));
            else if (key == "budgetMaxParticlesPerStrand") ioBundle.budget.maxParticlesPerStrand = ClampRuntimeParticlesPerStrand(toUInt(ioBundle.budget.maxParticlesPerStrand));
            else if (key == "budgetMaxRefinementLevel") ioBundle.budget.maxRefinementLevel = std::min<uint32_t>(toUInt(ioBundle.budget.maxRefinementLevel), 8u);
            else if (key == "budgetMemoryBudgetMB") ioBundle.budget.memoryBudgetMegabytes = toFloat(ioBundle.budget.memoryBudgetMegabytes);
            else if (key == "budgetFixedMemoryMB") ioBundle.budget.estimatedFixedMemoryMegabytes = toFloat(ioBundle.budget.estimatedFixedMemoryMegabytes);
            else if (key == "budgetClusterMemoryMB") ioBundle.budget.estimatedClusterMemoryMegabytes = toFloat(ioBundle.budget.estimatedClusterMemoryMegabytes);
            else if (key == "budgetEnforceEditCoverage") ioBundle.budget.enforceEditCoverage = toUInt(ioBundle.budget.enforceEditCoverage ? 1u : 0u) != 0u;
            else if (key == "budgetMandatoryEditThreshold") ioBundle.budget.mandatoryEditThreshold = toFloat(ioBundle.budget.mandatoryEditThreshold);
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
            else if (key == "priorAccelerationPredictionGain") ioBundle.priorAccelerationPredictionGain = toFloat(ioBundle.priorAccelerationPredictionGain);
            else if (key == "residualAccelerationPredictionGain") ioBundle.residualAccelerationPredictionGain = toFloat(ioBundle.residualAccelerationPredictionGain);
            else if (key == "priorTemporalBlend") ioBundle.priorTemporalBlend = toFloat(ioBundle.priorTemporalBlend);
            else if (key == "residualTemporalBlend") ioBundle.residualTemporalBlend = toFloat(ioBundle.residualTemporalBlend);
            else if (key == "activeSetPredictorBlend") ioBundle.activeSetPredictorBlend = toFloat(ioBundle.activeSetPredictorBlend);
            else if (key == "frozenStrandBlend") ioBundle.frozenStrandBlend = toFloat(ioBundle.frozenStrandBlend);
            else if (key == "frozenRootFollow") ioBundle.frozenRootFollow = toFloat(ioBundle.frozenRootFollow);
            else if (key == "cachedStrandBlend") ioBundle.cachedStrandBlend = toFloat(ioBundle.cachedStrandBlend);
            else if (key == "cachedRootFollow") ioBundle.cachedRootFollow = toFloat(ioBundle.cachedRootFollow);
            else if (key == "cachedFrameRefreshInterval") ioBundle.cachedFrameRefreshInterval = toUInt(ioBundle.cachedFrameRefreshInterval);
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
            out << "  \"residualVelocityPredictionGain\": " << bundle.residualVelocityPredictionGain << ",\n";
            out << "  \"priorAccelerationPredictionGain\": " << bundle.priorAccelerationPredictionGain << ",\n";
            out << "  \"residualAccelerationPredictionGain\": " << bundle.residualAccelerationPredictionGain << ",\n";
            out << "  \"priorTemporalBlend\": " << bundle.priorTemporalBlend << ",\n";
            out << "  \"residualTemporalBlend\": " << bundle.residualTemporalBlend << ",\n";
            out << "  \"activeSetPredictorBlend\": " << bundle.activeSetPredictorBlend << ",\n";
            out << "  \"frozenStrandBlend\": " << bundle.frozenStrandBlend << ",\n";
            out << "  \"frozenRootFollow\": " << bundle.frozenRootFollow << ",\n";
            out << "  \"cachedStrandBlend\": " << bundle.cachedStrandBlend << ",\n";
            out << "  \"cachedRootFollow\": " << bundle.cachedRootFollow << ",\n";
            out << "  \"cachedFrameRefreshInterval\": " << bundle.cachedFrameRefreshInterval;
            if (trainingReport)
            {
                out << ",\n  \"priorRms\": " << trainingReport->priorEvaluation.rmsPositionError
                    << ",\n  \"residualRms\": " << trainingReport->residualEvaluation.rmsPositionError
                    << ",\n  \"priorTemporalRms\": " << trainingReport->priorTemporalEvaluation.rmsPositionError
                    << ",\n  \"residualTemporalRms\": " << trainingReport->residualTemporalEvaluation.rmsPositionError
                    << ",\n  \"averageResidualMagnitude\": " << trainingReport->averageResidualMagnitude
                    << ",\n  \"maxResidualMagnitude\": " << trainingReport->maxResidualMagnitude
                    << ",\n  \"activeSetPredictorRms\": " << trainingReport->activeSetPredictorEvaluation.rmsScoreError
                    << ",\n  \"activeSetPredictorAccuracy\": " << trainingReport->activeSetPredictorEvaluation.classificationAccuracy;
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
                    << ",\n  \"maxPenetration\": " << runtimeReport->maxPenetration
                    << ",\n  \"averageGuidePredictorRisk\": " << runtimeReport->averageGuidePredictorRisk
                    << ",\n  \"maxGuidePredictorRisk\": " << runtimeReport->maxGuidePredictorRisk
                    << ",\n  \"averageEstimatedFrameMilliseconds\": " << runtimeReport->averageEstimatedFrameMilliseconds
                    << ",\n  \"p95EstimatedFrameMilliseconds\": " << runtimeReport->p95EstimatedFrameMilliseconds
                    << ",\n  \"maxEstimatedFrameMilliseconds\": " << runtimeReport->maxEstimatedFrameMilliseconds
                    << ",\n  \"hitRate60Fps\": " << runtimeReport->hitRate60Fps;
            }
            out << "\n}\n";
            outError.clear();
            return true;
        }
    }

    [[nodiscard]] bool IsActiveSetPredictorReady(const ActiveSetPredictor& predictor)
    {
        if (predictor.featureCount == 0u ||
            predictor.featureMean.size() < predictor.featureCount ||
            predictor.featureStd.size() < predictor.featureCount)
        {
            return false;
        }
        if (predictor.hiddenWidth > 0u)
        {
            return predictor.hiddenWeights.size() == static_cast<size_t>(predictor.hiddenWidth) * predictor.featureCount &&
                   predictor.hiddenBias.size() == predictor.hiddenWidth &&
                   predictor.outputWeights.size() == predictor.hiddenWidth;
        }
        return predictor.weights.size() >= predictor.featureCount;
    }

    bool SaveActiveSetPredictorToFile(const std::filesystem::path& filePath,
                                      const ActiveSetPredictor& predictor,
                                      std::string& outError)
    {
        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open active-set predictor file for writing.";
            return false;
        }
        const uint32_t magic = 0x31505341u; // ASP1
        const uint32_t version = 3u;
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(&predictor.featureCount), sizeof(predictor.featureCount));
        out.write(reinterpret_cast<const char*>(&predictor.hiddenWidth), sizeof(predictor.hiddenWidth));
        out.write(reinterpret_cast<const char*>(&predictor.settings.hiddenWidth), sizeof(predictor.settings.hiddenWidth));
        out.write(reinterpret_cast<const char*>(&predictor.settings.epochs), sizeof(predictor.settings.epochs));
        out.write(reinterpret_cast<const char*>(&predictor.settings.validationStride), sizeof(predictor.settings.validationStride));
        out.write(reinterpret_cast<const char*>(&predictor.settings.earlyStoppingPatience), sizeof(predictor.settings.earlyStoppingPatience));
        out.write(reinterpret_cast<const char*>(&predictor.settings.learningRate), sizeof(predictor.settings.learningRate));
        out.write(reinterpret_cast<const char*>(&predictor.settings.learningRateDecay), sizeof(predictor.settings.learningRateDecay));
        out.write(reinterpret_cast<const char*>(&predictor.settings.minLearningRate), sizeof(predictor.settings.minLearningRate));
        out.write(reinterpret_cast<const char*>(&predictor.settings.weightDecay), sizeof(predictor.settings.weightDecay));
        out.write(reinterpret_cast<const char*>(&predictor.settings.gradientClip), sizeof(predictor.settings.gradientClip));
        out.write(reinterpret_cast<const char*>(&predictor.settings.positiveClassWeight), sizeof(predictor.settings.positiveClassWeight));
        auto writeVector = [&](const std::vector<float>& values)
        {
            const uint32_t count = static_cast<uint32_t>(values.size());
            out.write(reinterpret_cast<const char*>(&count), sizeof(count));
            if (count > 0u)
            {
                out.write(reinterpret_cast<const char*>(values.data()), sizeof(float) * count);
            }
        };
        writeVector(predictor.featureMean);
        writeVector(predictor.featureStd);
        writeVector(predictor.weights);
        out.write(reinterpret_cast<const char*>(&predictor.bias), sizeof(predictor.bias));
        writeVector(predictor.hiddenWeights);
        writeVector(predictor.hiddenBias);
        writeVector(predictor.outputWeights);
        out.write(reinterpret_cast<const char*>(&predictor.outputBias), sizeof(predictor.outputBias));
        if (!out)
        {
            outError = "Failed to serialize active-set predictor.";
            return false;
        }
        outError.clear();
        return true;
    }

    bool LoadActiveSetPredictorFromFile(const std::filesystem::path& filePath,
                                        ActiveSetPredictor& outPredictor,
                                        std::string& outError)
    {
        outPredictor = {};
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open active-set predictor file.";
            return false;
        }
        uint32_t magic = 0u;
        uint32_t version = 0u;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (magic != 0x31505341u || (version != 1u && version != 2u && version != 3u))
        {
            outError = "Active-set predictor file has an unsupported version.";
            return false;
        }
        in.read(reinterpret_cast<char*>(&outPredictor.featureCount), sizeof(outPredictor.featureCount));
        if (version >= 2u)
        {
            in.read(reinterpret_cast<char*>(&outPredictor.hiddenWidth), sizeof(outPredictor.hiddenWidth));
            in.read(reinterpret_cast<char*>(&outPredictor.settings.hiddenWidth), sizeof(outPredictor.settings.hiddenWidth));
        }
        in.read(reinterpret_cast<char*>(&outPredictor.settings.epochs), sizeof(outPredictor.settings.epochs));
        if (version >= 3u)
        {
            in.read(reinterpret_cast<char*>(&outPredictor.settings.validationStride), sizeof(outPredictor.settings.validationStride));
            in.read(reinterpret_cast<char*>(&outPredictor.settings.earlyStoppingPatience), sizeof(outPredictor.settings.earlyStoppingPatience));
        }
        in.read(reinterpret_cast<char*>(&outPredictor.settings.learningRate), sizeof(outPredictor.settings.learningRate));
        if (version >= 3u)
        {
            in.read(reinterpret_cast<char*>(&outPredictor.settings.learningRateDecay), sizeof(outPredictor.settings.learningRateDecay));
            in.read(reinterpret_cast<char*>(&outPredictor.settings.minLearningRate), sizeof(outPredictor.settings.minLearningRate));
        }
        in.read(reinterpret_cast<char*>(&outPredictor.settings.weightDecay), sizeof(outPredictor.settings.weightDecay));
        if (version >= 2u)
        {
            in.read(reinterpret_cast<char*>(&outPredictor.settings.gradientClip), sizeof(outPredictor.settings.gradientClip));
        }
        if (version >= 3u)
        {
            in.read(reinterpret_cast<char*>(&outPredictor.settings.positiveClassWeight), sizeof(outPredictor.settings.positiveClassWeight));
        }
        auto readVector = [&](std::vector<float>& values)
        {
            uint32_t count = 0u;
            in.read(reinterpret_cast<char*>(&count), sizeof(count));
            values.resize(count);
            if (count > 0u)
            {
                in.read(reinterpret_cast<char*>(values.data()), sizeof(float) * count);
            }
        };
        readVector(outPredictor.featureMean);
        readVector(outPredictor.featureStd);
        readVector(outPredictor.weights);
        in.read(reinterpret_cast<char*>(&outPredictor.bias), sizeof(outPredictor.bias));
        if (version >= 2u)
        {
            readVector(outPredictor.hiddenWeights);
            readVector(outPredictor.hiddenBias);
            readVector(outPredictor.outputWeights);
            in.read(reinterpret_cast<char*>(&outPredictor.outputBias), sizeof(outPredictor.outputBias));
        }
        if (!in)
        {
            outError = "Failed to deserialize active-set predictor.";
            return false;
        }
        if (outPredictor.hiddenWidth == 0u)
        {
            outPredictor.hiddenWidth = outPredictor.settings.hiddenWidth;
        }
        outError.clear();
        return true;
    }

    std::string BuildActiveSetPredictorSummary(const ActiveSetPredictor& predictor,
                                               const ActiveSetPredictorEvaluation* evaluation)
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(5);
        ss << "activeSetPredictorFeatures=" << predictor.featureCount << " hidden=" << predictor.hiddenWidth
           << " valStride=" << predictor.settings.validationStride
           << " patience=" << predictor.settings.earlyStoppingPatience;
        if (evaluation)
        {
            ss << " predictorRms=" << evaluation->rmsScoreError
               << " predictorAcc=" << evaluation->classificationAccuracy
               << " predictorPosRate=" << evaluation->positiveRate;
        }
        return ss.str();
    }

    bool ExportActiveSetPredictorToCsvDirectory(const std::filesystem::path& directoryPath,
                                                const ActiveSetPredictor& predictor,
                                                std::string& outError)
    {
        if (!IsActiveSetPredictorReady(predictor))
        {
            outError = "Active-set predictor is not initialized.";
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(directoryPath, ec);
        if (ec)
        {
            outError = "Failed to create predictor CSV directory: " + directoryPath.string();
            return false;
        }

        std::ofstream meta(directoryPath / "metadata.csv");
        if (!meta)
        {
            outError = "Failed to write predictor metadata CSV.";
            return false;
        }
        meta << "key,value\n";
        meta << "featureCount," << predictor.featureCount << "\n";
        meta << "hiddenWidth," << predictor.hiddenWidth << "\n";
        meta << "settings.hiddenWidth," << predictor.settings.hiddenWidth << "\n";
        meta << "settings.epochs," << predictor.settings.epochs << "\n";
        meta << "settings.learningRate," << predictor.settings.learningRate << "\n";
        meta << "settings.weightDecay," << predictor.settings.weightDecay << "\n";
        meta << "settings.gradientClip," << predictor.settings.gradientClip << "\n";
        meta << "bias," << predictor.bias << "\n";
        meta << "outputBias," << predictor.outputBias << "\n";

        std::ofstream weights(directoryPath / "weights.csv");
        if (!weights)
        {
            outError = "Failed to write predictor weights CSV.";
            return false;
        }
        weights << "index,mean,std,weight\n";
        weights.setf(std::ios::fixed);
        weights.precision(9);
        for (uint32_t i = 0u; i < predictor.featureCount; ++i)
        {
            const float mean = (i < predictor.featureMean.size()) ? predictor.featureMean[i] : 0.0f;
            const float stdDev = (i < predictor.featureStd.size()) ? predictor.featureStd[i] : 1.0f;
            const float weight = (i < predictor.weights.size()) ? predictor.weights[i] : 0.0f;
            weights << i << ',' << mean << ',' << stdDev << ',' << weight << "\n";
        }
        std::ofstream hiddenWeights(directoryPath / "hidden_weights.csv");
        if (!hiddenWeights)
        {
            outError = "Failed to write predictor hidden weights CSV.";
            return false;
        }
        hiddenWeights.setf(std::ios::fixed);
        hiddenWeights.precision(9);
        for (uint32_t h = 0u; h < predictor.hiddenWidth; ++h)
        {
            hiddenWeights << h;
            const size_t rowOffset = static_cast<size_t>(h) * predictor.featureCount;
            for (uint32_t i = 0u; i < predictor.featureCount; ++i)
            {
                hiddenWeights << ',' << predictor.hiddenWeights[rowOffset + i];
            }
            hiddenWeights << ',' << predictor.hiddenBias[h] << "\n";
        }
        std::ofstream outputWeights(directoryPath / "output_weights.csv");
        if (!outputWeights)
        {
            outError = "Failed to write predictor output weights CSV.";
            return false;
        }
        outputWeights << "hiddenIndex,weight\n";
        for (uint32_t h = 0u; h < predictor.hiddenWidth; ++h)
        {
            outputWeights << h << ',' << predictor.outputWeights[h] << "\n";
        }

        std::ofstream readme(directoryPath / "README.txt");
        if (readme)
        {
            readme << "Active-set predictor CSV interchange\n";
            readme << "Edit weights.csv, hidden_weights.csv, output_weights.csv and metadata.csv, then re-import with active-set-predictor-import-csv.\n";
        }
        outError.clear();
        return true;
    }

    bool ImportActiveSetPredictorFromCsvDirectory(const std::filesystem::path& directoryPath,
                                                  ActiveSetPredictor& outPredictor,
                                                  std::string& outError)
    {
        outPredictor = {};
        auto trim = [](std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        };
        auto splitCsv = [&](const std::string& line)
        {
            std::vector<std::string> parts;
            std::string current;
            for (char c : line)
            {
                if (c == ',')
                {
                    parts.push_back(trim(current));
                    current.clear();
                }
                else
                {
                    current.push_back(c);
                }
            }
            parts.push_back(trim(current));
            return parts;
        };
        auto parseUInt = [](const std::string& text, uint32_t& outValue) -> bool
        {
            try { outValue = static_cast<uint32_t>(std::stoul(text)); return true; } catch (...) { return false; }
        };
        auto parseFloat = [](const std::string& text, float& outValue) -> bool
        {
            try { outValue = std::stof(text); return true; } catch (...) { return false; }
        };

        std::ifstream meta(directoryPath / "metadata.csv");
        if (!meta)
        {
            outError = "Failed to open predictor metadata CSV.";
            return false;
        }
        std::string line;
        if (!std::getline(meta, line))
        {
            outError = "Predictor metadata CSV is empty.";
            return false;
        }
        while (std::getline(meta, line))
        {
            line = trim(line);
            if (line.empty()) continue;
            const std::vector<std::string> tokens = splitCsv(line);
            if (tokens.size() < 2u) continue;
            const std::string& key = tokens[0];
            const std::string& value = tokens[1];
            if (key == "featureCount") parseUInt(value, outPredictor.featureCount);
            else if (key == "hiddenWidth") parseUInt(value, outPredictor.hiddenWidth);
            else if (key == "settings.hiddenWidth") parseUInt(value, outPredictor.settings.hiddenWidth);
            else if (key == "settings.epochs") parseUInt(value, outPredictor.settings.epochs);
            else if (key == "settings.validationStride") parseUInt(value, outPredictor.settings.validationStride);
            else if (key == "settings.earlyStoppingPatience") parseUInt(value, outPredictor.settings.earlyStoppingPatience);
            else if (key == "settings.learningRate") parseFloat(value, outPredictor.settings.learningRate);
            else if (key == "settings.learningRateDecay") parseFloat(value, outPredictor.settings.learningRateDecay);
            else if (key == "settings.minLearningRate") parseFloat(value, outPredictor.settings.minLearningRate);
            else if (key == "settings.weightDecay") parseFloat(value, outPredictor.settings.weightDecay);
            else if (key == "settings.gradientClip") parseFloat(value, outPredictor.settings.gradientClip);
            else if (key == "settings.positiveClassWeight") parseFloat(value, outPredictor.settings.positiveClassWeight);
            else if (key == "bias") parseFloat(value, outPredictor.bias);
            else if (key == "outputBias") parseFloat(value, outPredictor.outputBias);
        }

        std::ifstream weights(directoryPath / "weights.csv");
        if (!weights)
        {
            outError = "Failed to open predictor weights CSV.";
            return false;
        }
        if (!std::getline(weights, line))
        {
            outError = "Predictor weights CSV is empty.";
            return false;
        }
        outPredictor.featureMean.clear();
        outPredictor.featureStd.clear();
        outPredictor.weights.clear();
        while (std::getline(weights, line))
        {
            line = trim(line);
            if (line.empty()) continue;
            const std::vector<std::string> tokens = splitCsv(line);
            if (tokens.size() < 4u)
            {
                outError = "Malformed predictor weights row.";
                return false;
            }
            float mean = 0.0f;
            float stdDev = 1.0f;
            float weight = 0.0f;
            if (!parseFloat(tokens[1], mean) || !parseFloat(tokens[2], stdDev) || !parseFloat(tokens[3], weight))
            {
                outError = "Malformed predictor weight values.";
                return false;
            }
            outPredictor.featureMean.push_back(mean);
            outPredictor.featureStd.push_back(std::max(stdDev, 1.0e-4f));
            outPredictor.weights.push_back(weight);
        }
        if (outPredictor.featureCount == 0u)
        {
            outPredictor.featureCount = static_cast<uint32_t>(outPredictor.weights.size());
        }
        if (outPredictor.hiddenWidth == 0u)
        {
            outPredictor.hiddenWidth = outPredictor.settings.hiddenWidth;
        }
        outPredictor.hiddenWeights.assign(static_cast<size_t>(outPredictor.hiddenWidth) * outPredictor.featureCount, 0.0f);
        outPredictor.hiddenBias.assign(outPredictor.hiddenWidth, 0.0f);
        outPredictor.outputWeights.assign(outPredictor.hiddenWidth, 0.0f);

        std::ifstream hiddenIn(directoryPath / "hidden_weights.csv");
        if (hiddenIn)
        {
            while (std::getline(hiddenIn, line))
            {
                line = trim(line);
                if (line.empty()) continue;
                const std::vector<std::string> tokens = splitCsv(line);
                if (tokens.size() != outPredictor.featureCount + 2u)
                {
                    continue;
                }
                uint32_t row = 0u;
                if (!parseUInt(tokens[0], row) || row >= outPredictor.hiddenWidth)
                {
                    continue;
                }
                const size_t rowOffset = static_cast<size_t>(row) * outPredictor.featureCount;
                for (uint32_t i = 0u; i < outPredictor.featureCount; ++i)
                {
                    parseFloat(tokens[i + 1u], outPredictor.hiddenWeights[rowOffset + i]);
                }
                parseFloat(tokens.back(), outPredictor.hiddenBias[row]);
            }
        }

        std::ifstream outputIn(directoryPath / "output_weights.csv");
        if (outputIn)
        {
            std::getline(outputIn, line);
            while (std::getline(outputIn, line))
            {
                line = trim(line);
                if (line.empty()) continue;
                const std::vector<std::string> tokens = splitCsv(line);
                if (tokens.size() < 2u)
                {
                    continue;
                }
                uint32_t row = 0u;
                if (!parseUInt(tokens[0], row) || row >= outPredictor.hiddenWidth)
                {
                    continue;
                }
                parseFloat(tokens[1], outPredictor.outputWeights[row]);
            }
        }

        if (!IsActiveSetPredictorReady(outPredictor))
        {
            outError = "Predictor CSV import is incomplete or inconsistent.";
            return false;
        }
        outError.clear();
        return true;
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

        std::vector<Vec3> priorTemporalInputFrames;
        std::vector<Vec3> priorTemporalTargetFrames;
        BuildPriorTemporalTrainingSequences(outBundle.priorModel,
                                            teacherSequence,
                                            priorSequence,
                                            priorTemporalInputFrames,
                                            priorTemporalTargetFrames);
        TemporalGuideNetworkSettings priorTemporalSettings = {};
        priorTemporalSettings.hiddenWidth = std::max<uint32_t>(24u, settings.priorGuideCount * 2u);
        priorTemporalSettings.hiddenWidth2 = std::max<uint32_t>(12u, settings.priorGuideCount);
        priorTemporalSettings.epochs = std::max<uint32_t>(120u, 56u + settings.validationStride * 10u);
        priorTemporalSettings.learningRate = 1.8e-3f;
        priorTemporalSettings.weightDecay = std::max(5.0e-6f, settings.regularization * 0.2f);
        priorTemporalSettings.gradientClip = 0.28f;
        priorTemporalSettings.lockRootPoints = true;
        std::vector<Vec3> restPriorGuidePoints;
        BuildRestGuideFramePointsForModel(outBundle.priorModel, restPriorGuidePoints);
        if (!TrainTemporalGuideNetwork(restPriorGuidePoints,
                                       priorTemporalInputFrames,
                                       priorTemporalTargetFrames,
                                       teacherSequence.FrameCount(),
                                       static_cast<uint32_t>(outBundle.priorModel.selectedGuideStrandIndices.size()),
                                       outBundle.priorModel.referenceAsset.particlesPerStrand,
                                       priorTemporalSettings,
                                       outBundle.priorTemporalNetwork,
                                       &outReport.priorTemporalEvaluation,
                                       outError))
        {
            return false;
        }

        std::vector<Vec3> residualTemporalInputFrames;
        std::vector<Vec3> residualTemporalTargetFrames;
        BuildResidualTemporalTrainingSequences(outBundle.residualModel,
                                               teacherSequence,
                                               priorSequence,
                                               settings.residualGuideGain,
                                               residualTemporalInputFrames,
                                               residualTemporalTargetFrames);
        TemporalGuideNetworkSettings residualTemporalSettings = {};
        residualTemporalSettings.hiddenWidth = std::max<uint32_t>(28u, settings.residualGuideCount * 2u);
        residualTemporalSettings.hiddenWidth2 = std::max<uint32_t>(14u, settings.residualGuideCount);
        residualTemporalSettings.epochs = std::max<uint32_t>(128u, 60u + settings.validationStride * 10u);
        residualTemporalSettings.learningRate = 1.6e-3f;
        residualTemporalSettings.weightDecay = std::max(5.0e-6f, settings.regularization * 0.25f);
        residualTemporalSettings.gradientClip = 0.32f;
        residualTemporalSettings.lockRootPoints = true;
        std::vector<Vec3> restResidualGuidePoints;
        BuildRestGuideFramePointsForModel(outBundle.residualModel, restResidualGuidePoints);
        if (!TrainTemporalGuideNetwork(restResidualGuidePoints,
                                       residualTemporalInputFrames,
                                       residualTemporalTargetFrames,
                                       teacherSequence.FrameCount(),
                                       static_cast<uint32_t>(outBundle.residualModel.selectedGuideStrandIndices.size()),
                                       outBundle.residualModel.referenceAsset.particlesPerStrand,
                                       residualTemporalSettings,
                                       outBundle.residualTemporalNetwork,
                                       &outReport.residualTemporalEvaluation,
                                       outError))
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
        outBundle.budget.standardParticlesPerStrand = kStandardParticlesPerStrand;
        outBundle.budget.maxParticlesPerStrand = kMaximumParticlesPerStrand;
        outBundle.budget.maxRefinementLevel = 4u;
        outBundle.budget.memoryBudgetMegabytes = 32768.0f;
        outBundle.budget.estimatedFixedMemoryMegabytes = 768.0f;
        outBundle.budget.estimatedClusterMemoryMegabytes = 12.0f;
        outBundle.budget.enforceEditCoverage = true;
        outBundle.budget.mandatoryEditThreshold = 0.55f;
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
        outBundle.priorAccelerationPredictionGain = Clamp(outBundle.priorVelocityPredictionGain * 0.32f + outReport.priorEvaluation.rmsPositionError * 2.5f,
                                                          0.0f,
                                                          0.35f);
        outBundle.residualAccelerationPredictionGain = Clamp(outBundle.residualVelocityPredictionGain * 0.28f + outReport.residualEvaluation.rmsPositionError * 3.0f,
                                                             0.0f,
                                                             0.40f);
        outBundle.priorTemporalBlend = Clamp(0.45f + outReport.priorTemporalEvaluation.rmsPositionError * 8.0f,
                                             0.35f,
                                             0.85f);
        outBundle.residualTemporalBlend = Clamp(0.50f + outReport.residualTemporalEvaluation.rmsPositionError * 10.0f,
                                                0.40f,
                                                0.90f);
        outBundle.frozenStrandBlend = Clamp(0.88f + outReport.priorEvaluation.rmsPositionError * 4.0f,
                                            0.80f,
                                            0.98f);
        outBundle.frozenRootFollow = 1.0f;
        outBundle.cachedStrandBlend = Clamp(0.72f + averageResidualMagnitude * 6.0f + outReport.residualEvaluation.rmsPositionError * 5.0f,
                                            0.60f,
                                            0.95f);
        outBundle.cachedRootFollow = Clamp(0.90f + outReport.priorEvaluation.rmsPositionError * 3.5f,
                                           0.80f,
                                           1.0f);
        outBundle.cachedFrameRefreshInterval = std::max<uint32_t>(2u,
                                                                  std::min<uint32_t>(6u,
                                                                                     2u + settings.validationStride / 2u));
        outBundle.activeSetPredictorBlend = 0.0f;

        const ColliderSet predictorColliders = !HasAnyCollider(colliders)
            ? BuildFallbackTeacherColliders(teacherSequence.referenceAsset)
            : colliders;
        const std::vector<ActiveSetCluster> predictorClusters = BuildActiveSetClusters(teacherSequence.referenceAsset, outBundle.clusterSettings);
        if (!predictorClusters.empty())
        {
            std::vector<std::array<float, 9u>> predictorFeatureRows;
            std::vector<float> predictorTargets;
            std::vector<Vec3> restGuideRoots;
            BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherSequence.referenceAsset.restPoints, restGuideRoots);

            std::vector<Vec3> trainingRestPriorGuidePoints;
            std::vector<Vec3> trainingSmoothedPriorGuidePoints;
            std::vector<Vec3> trainingPreviousSmoothedPriorGuidePoints;
            std::vector<Vec3> trainingPreviousPreviousSmoothedPriorGuidePoints;
            std::vector<Vec3> trainingPreviousRawPriorGuidePoints;
            std::vector<Vec3> trainingPreviousPreviousRawPriorGuidePoints;
            BuildRestGuideFramePointsForModel(outBundle.priorModel, trainingRestPriorGuidePoints);
            trainingSmoothedPriorGuidePoints = trainingRestPriorGuidePoints;
            trainingPreviousSmoothedPriorGuidePoints = trainingSmoothedPriorGuidePoints;
            trainingPreviousPreviousSmoothedPriorGuidePoints = trainingPreviousSmoothedPriorGuidePoints;
            trainingPreviousRawPriorGuidePoints = trainingRestPriorGuidePoints;
            trainingPreviousPreviousRawPriorGuidePoints = trainingPreviousRawPriorGuidePoints;

            std::vector<Vec3> trainingRestResidualGuidePoints;
            std::vector<Vec3> trainingPreviousResidualGuidePoints;
            std::vector<Vec3> trainingPreviousPreviousResidualGuidePoints;
            std::vector<Vec3> trainingPreviousBaseResidualGuideInput;
            std::vector<Vec3> trainingPreviousPreviousBaseResidualGuideInput;
            BuildRestGuideFramePointsForModel(outBundle.residualModel, trainingRestResidualGuidePoints);
            trainingPreviousResidualGuidePoints = trainingRestResidualGuidePoints;
            trainingPreviousPreviousResidualGuidePoints = trainingPreviousResidualGuidePoints;
            trainingPreviousBaseResidualGuideInput = trainingRestResidualGuidePoints;
            trainingPreviousPreviousBaseResidualGuideInput = trainingPreviousBaseResidualGuideInput;

            std::vector<Vec3> trainingPreviousHybridGuideRoots = restGuideRoots;
            std::vector<float> trainingPreviousGuideUncertainty(trainingPreviousHybridGuideRoots.size(), 0.0f);
            const float trainingResidualBlend = outBundle.trainingSettings.residualBlend;
            const float runtimeFrameRate = std::max(teacherSequence.frameRate, 1.0f);
            for (uint32_t frameIndex = 0u; frameIndex < teacherSequence.FrameCount(); ++frameIndex)
            {
                std::vector<Vec3> teacherFrame;
                CopySequenceFrame(teacherSequence, frameIndex, teacherFrame);

                std::vector<Vec3> currentPriorGuidePoints;
                BuildGuideFramePointsForModel(outBundle.priorModel, teacherFrame, currentPriorGuidePoints);
                for (uint32_t i = 0u; i < trainingSmoothedPriorGuidePoints.size() && i < currentPriorGuidePoints.size(); ++i)
                {
                    trainingSmoothedPriorGuidePoints[i] = Lerp(trainingSmoothedPriorGuidePoints[i],
                                                               currentPriorGuidePoints[i],
                                                               Clamp(settings.priorGuideSmoothingAlpha, 0.01f, 1.0f));
                }
                const float priorVelocityPredictionGain = Clamp(outBundle.priorVelocityPredictionGain, 0.0f, 1.0f);
                const float priorAccelerationPredictionGain = EstimateSecondOrderPredictionGain(trainingPreviousPreviousSmoothedPriorGuidePoints,
                                                                                               trainingPreviousSmoothedPriorGuidePoints,
                                                                                               trainingSmoothedPriorGuidePoints,
                                                                                               priorVelocityPredictionGain,
                                                                                               outBundle.priorAccelerationPredictionGain,
                                                                                               0.50f);
                std::vector<Vec3> predictedPriorGuidePoints = trainingSmoothedPriorGuidePoints;
                for (uint32_t i = 0u; i < predictedPriorGuidePoints.size() && i < trainingPreviousSmoothedPriorGuidePoints.size(); ++i)
                {
                    const Vec3 velocity = trainingSmoothedPriorGuidePoints[i] - trainingPreviousSmoothedPriorGuidePoints[i];
                    predictedPriorGuidePoints[i] += velocity * priorVelocityPredictionGain;
                    if (i < trainingPreviousPreviousSmoothedPriorGuidePoints.size())
                    {
                        const Vec3 acceleration = trainingSmoothedPriorGuidePoints[i] - trainingPreviousSmoothedPriorGuidePoints[i] * 2.0f + trainingPreviousPreviousSmoothedPriorGuidePoints[i];
                        predictedPriorGuidePoints[i] += acceleration * priorAccelerationPredictionGain;
                    }
                }
                if (frameIndex >= 2u && IsTemporalGuideNetworkReady(outBundle.priorTemporalNetwork))
                {
                    std::vector<Vec3> temporalPriorGuidePoints;
                    if (!PredictTemporalGuideFrame(outBundle.priorTemporalNetwork,
                                                   trainingRestPriorGuidePoints,
                                                   trainingPreviousPreviousRawPriorGuidePoints,
                                                   trainingPreviousRawPriorGuidePoints,
                                                   currentPriorGuidePoints,
                                                   temporalPriorGuidePoints,
                                                   outError))
                    {
                        return false;
                    }
                    const float temporalBlend = Clamp(outBundle.priorTemporalBlend, 0.0f, 1.0f);
                    for (uint32_t i = 0u; i < predictedPriorGuidePoints.size() && i < temporalPriorGuidePoints.size(); ++i)
                    {
                        predictedPriorGuidePoints[i] = Lerp(predictedPriorGuidePoints[i], temporalPriorGuidePoints[i], temporalBlend);
                    }
                }
                HairAsset priorAsset;
                if (!ReconstructReducedModelFrame(outBundle.priorModel, predictedPriorGuidePoints, priorAsset, outError))
                {
                    return false;
                }

                std::vector<Vec3> currentResidualGuidePoints;
                BuildGuideFramePointsForModel(outBundle.residualModel, teacherFrame, currentResidualGuidePoints);
                std::vector<Vec3> priorResidualGuidePoints;
                BuildGuideFramePointsForModel(outBundle.residualModel, priorAsset.restPoints, priorResidualGuidePoints);
                const float residualVelocityPredictionGain = Clamp(outBundle.residualVelocityPredictionGain, 0.0f, 1.0f);
                const float residualAccelerationPredictionGain = EstimateSecondOrderPredictionGain(trainingPreviousPreviousResidualGuidePoints,
                                                                                                  trainingPreviousResidualGuidePoints,
                                                                                                  currentResidualGuidePoints,
                                                                                                  residualVelocityPredictionGain,
                                                                                                  outBundle.residualAccelerationPredictionGain,
                                                                                                  0.60f);
                std::vector<Vec3> baseResidualGuideInput = trainingRestResidualGuidePoints;
                for (uint32_t i = 0u; i < baseResidualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    baseResidualGuideInput[i] += (currentResidualGuidePoints[i] - priorResidualGuidePoints[i]) * outBundle.trainingSettings.residualGuideGain;
                }
                std::vector<Vec3> residualGuideInput = baseResidualGuideInput;
                for (uint32_t i = 0u; i < residualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    if (i < trainingPreviousResidualGuidePoints.size())
                    {
                        const Vec3 velocity = currentResidualGuidePoints[i] - trainingPreviousResidualGuidePoints[i];
                        residualGuideInput[i] += velocity * residualVelocityPredictionGain;
                    }
                    if (i < trainingPreviousResidualGuidePoints.size() && i < trainingPreviousPreviousResidualGuidePoints.size())
                    {
                        const Vec3 acceleration = currentResidualGuidePoints[i] - trainingPreviousResidualGuidePoints[i] * 2.0f + trainingPreviousPreviousResidualGuidePoints[i];
                        residualGuideInput[i] += acceleration * residualAccelerationPredictionGain;
                    }
                }
                if (frameIndex >= 2u && IsTemporalGuideNetworkReady(outBundle.residualTemporalNetwork))
                {
                    std::vector<Vec3> temporalResidualGuidePoints;
                    if (!PredictTemporalGuideFrame(outBundle.residualTemporalNetwork,
                                                   trainingRestResidualGuidePoints,
                                                   trainingPreviousPreviousBaseResidualGuideInput,
                                                   trainingPreviousBaseResidualGuideInput,
                                                   baseResidualGuideInput,
                                                   temporalResidualGuidePoints,
                                                   outError))
                    {
                        return false;
                    }
                    const float temporalBlend = Clamp(outBundle.residualTemporalBlend, 0.0f, 1.0f);
                    for (uint32_t i = 0u; i < residualGuideInput.size() && i < temporalResidualGuidePoints.size(); ++i)
                    {
                        residualGuideInput[i] = Lerp(residualGuideInput[i], temporalResidualGuidePoints[i], temporalBlend);
                    }
                }
                HairAsset residualAsset;
                if (!ReconstructReducedModelFrame(outBundle.residualModel, residualGuideInput, residualAsset, outError))
                {
                    return false;
                }
                std::vector<Vec3> hybridBeforeCorrection;
                ComposeHybridPoints(teacherSequence.referenceAsset,
                                    priorAsset.restPoints,
                                    residualAsset.restPoints,
                                    trainingResidualBlend,
                                    hybridBeforeCorrection);

                std::vector<Vec3> currentHybridGuideRoots;
                BuildGuideRootsFromPoints(teacherSequence.referenceAsset, hybridBeforeCorrection, currentHybridGuideRoots);
                std::vector<Vec3> teacherGuideRoots;
                BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherFrame, teacherGuideRoots);
                std::vector<float> guideContactScores = BuildGuideContactScores(teacherSequence.referenceAsset,
                                                                                hybridBeforeCorrection,
                                                                                predictorColliders,
                                                                                -1.05f,
                                                                                outBundle.correctiveSettings.shellDistance);
                std::vector<float> guidePenetrationScores = BuildGuidePenetrationScores(teacherSequence.referenceAsset,
                                                                                        hybridBeforeCorrection,
                                                                                        predictorColliders,
                                                                                        -1.05f,
                                                                                        outBundle.correctiveSettings.shellDistance);
                for (float& value : guideContactScores) value = Clamp(value * outBundle.contactGain, 0.0f, 1.0f);
                for (float& value : guidePenetrationScores) value = Clamp(value * outBundle.penetrationGain, 0.0f, 1.0f);
                const uint32_t guideCount = std::min<uint32_t>(teacherSequence.referenceAsset.guideStrandCount, static_cast<uint32_t>(teacherSequence.referenceAsset.strands.size()));
                std::vector<float> guideModelErrorScores(guideCount, 0.0f);
                for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
                {
                    const Vec3 teacherRoot = (guideIndex < teacherGuideRoots.size()) ? teacherGuideRoots[guideIndex] : Vec3(0.0f, 0.0f, 0.0f);
                    const Vec3 hybridRoot = (guideIndex < currentHybridGuideRoots.size()) ? currentHybridGuideRoots[guideIndex] : teacherRoot;
                    const StrandRange& strand = teacherSequence.referenceAsset.strands[guideIndex];
                    const uint32_t pointIndex = strand.pointOffset + strand.pointCount / 2u;
                    const float residualMagnitude = (pointIndex < residualAsset.restPoints.size() && pointIndex < teacherSequence.referenceAsset.restPoints.size())
                        ? Length(residualAsset.restPoints[pointIndex] - teacherSequence.referenceAsset.restPoints[pointIndex])
                        : 0.0f;
                    const float rootError = Clamp(Length(hybridRoot - teacherRoot) / 0.025f, 0.0f, 1.0f);
                    guideModelErrorScores[guideIndex] = Clamp(rootError * 0.70f + residualMagnitude * 18.0f * 0.30f, 0.0f, 1.0f);
                }
                std::vector<float> guideUncertainty = BuildGuideUncertainty(teacherSequence.referenceAsset,
                                                                            currentHybridGuideRoots,
                                                                            trainingPreviousHybridGuideRoots,
                                                                            trainingPreviousGuideUncertainty,
                                                                            guideContactScores,
                                                                            guidePenetrationScores,
                                                                            guideModelErrorScores,
                                                                            1.0f / runtimeFrameRate,
                                                                            outBundle.temporalCoherenceGain,
                                                                            outBundle.uncertaintySmoothingAlpha);
                const float trainingWindStrengthProxy = Clamp(settings.residualBlend * 0.35f + settings.priorGuideSmoothingAlpha * 0.65f, 0.0f, 1.0f);
                std::vector<float> guideWindExposureScores = BuildGuideWindExposureScores(teacherSequence.referenceAsset,
                                                                                          currentHybridGuideRoots,
                                                                                          trainingPreviousHybridGuideRoots,
                                                                                          1.0f / runtimeFrameRate,
                                                                                          trainingWindStrengthProxy,
                                                                                          Vec3(1.0f, 0.0f, 0.0f));
                std::vector<float> guideEnvironmentComplexityScores = BuildGuideEnvironmentComplexityScores(guideContactScores,
                                                                                                           guidePenetrationScores,
                                                                                                           guideWindExposureScores,
                                                                                                           guideModelErrorScores);
                ActiveSetRuntimeInput runtimeInput = {};
                runtimeInput.currentGuideRoots = currentHybridGuideRoots;
                runtimeInput.previousGuideRoots = trainingPreviousHybridGuideRoots;
                runtimeInput.guideContactScores = guideContactScores;
                runtimeInput.guidePenetrationScores = guidePenetrationScores;
                runtimeInput.guideModelErrorScores = guideModelErrorScores;
                runtimeInput.guideWindExposureScores = guideWindExposureScores;
                runtimeInput.guideEnvironmentComplexityScores = guideEnvironmentComplexityScores;
                runtimeInput.guideUncertainty = guideUncertainty;
                runtimeInput.cameraPosition = Vec3(0.0f, 1.55f, -1.85f);
                runtimeInput.cameraForward = Normalize(Vec3(0.0f, -0.05f, 1.0f));
                runtimeInput.fixedTimeStep = 1.0f / runtimeFrameRate;
                ActiveSetPlan plan;
                if (!EvaluateActiveSetPlan(teacherSequence.referenceAsset,
                                           predictorClusters,
                                           runtimeInput,
                                           outBundle.scoringSettings,
                                           outBundle.budget,
                                           plan,
                                           outError))
                {
                    return false;
                }
                std::vector<float> targets;
                BuildActiveSetPredictorTarget(predictorClusters,
                                              plan,
                                              guideContactScores,
                                              guidePenetrationScores,
                                              guideModelErrorScores,
                                              targets);
                AppendPredictorTrainingRows(predictorClusters,
                                            restGuideRoots,
                                            currentHybridGuideRoots,
                                            trainingPreviousHybridGuideRoots,
                                            guideUncertainty,
                                            guideContactScores,
                                            guidePenetrationScores,
                                            guideModelErrorScores,
                                            outBundle.scoringSettings,
                                            targets,
                                            predictorFeatureRows,
                                            predictorTargets);
                trainingPreviousPreviousSmoothedPriorGuidePoints = trainingPreviousSmoothedPriorGuidePoints;
                trainingPreviousSmoothedPriorGuidePoints = trainingSmoothedPriorGuidePoints;
                trainingPreviousPreviousRawPriorGuidePoints = trainingPreviousRawPriorGuidePoints;
                trainingPreviousRawPriorGuidePoints = currentPriorGuidePoints;
                trainingPreviousPreviousResidualGuidePoints = trainingPreviousResidualGuidePoints;
                trainingPreviousResidualGuidePoints = currentResidualGuidePoints;
                trainingPreviousPreviousBaseResidualGuideInput = trainingPreviousBaseResidualGuideInput;
                trainingPreviousBaseResidualGuideInput = baseResidualGuideInput;
                trainingPreviousHybridGuideRoots = currentHybridGuideRoots;
                trainingPreviousGuideUncertainty = guideUncertainty;
            }

            ActiveSetPredictorSettings predictorSettings = {};
            predictorSettings.epochs = std::max<uint32_t>(90u, 60u + settings.validationStride * 10u);
            predictorSettings.learningRate = 1.6e-2f;
            predictorSettings.weightDecay = std::max(1.0e-5f, settings.regularization * 0.5f);
            BuildActiveSetPredictorModel(predictorFeatureRows,
                                         predictorTargets,
                                         predictorSettings,
                                         outBundle.activeSetPredictor,
                                         &outReport.activeSetPredictorEvaluation);
            outBundle.activeSetPredictorBlend = Clamp(0.30f + outReport.activeSetPredictorEvaluation.classificationAccuracy * 0.45f +
                                                      (1.0f - outReport.activeSetPredictorEvaluation.rmsScoreError) * 0.10f,
                                                      0.25f,
                                                      0.85f);
        }
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
            !SaveTemporalGuideNetworkToFile(directoryPath / "prior.temporalbin", bundle.priorTemporalNetwork, outError) ||
            !SaveTemporalGuideNetworkToFile(directoryPath / "residual.temporalbin", bundle.residualTemporalNetwork, outError) ||
            !SaveActiveSetPredictorToFile(directoryPath / "active_set.predictorbin", bundle.activeSetPredictor, outError) ||
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
        if (!LoadTemporalGuideNetworkFromFile(directoryPath / "prior.temporalbin", outBundle.priorTemporalNetwork, outError))
        {
            return false;
        }
        if (!LoadTemporalGuideNetworkFromFile(directoryPath / "residual.temporalbin", outBundle.residualTemporalNetwork, outError))
        {
            return false;
        }
        if (!LoadActiveSetPredictorFromFile(directoryPath / "active_set.predictorbin", outBundle.activeSetPredictor, outError))
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
           << " priorAccelPred=" << bundle.priorAccelerationPredictionGain
           << " residualAccelPred=" << bundle.residualAccelerationPredictionGain
           << " priorTemporalBlend=" << bundle.priorTemporalBlend
           << " residualTemporalBlend=" << bundle.residualTemporalBlend
           << " predictorBlend=" << bundle.activeSetPredictorBlend
           << " frozenBlend=" << bundle.frozenStrandBlend
           << " cachedBlend=" << bundle.cachedStrandBlend
           << " budgetClusters=" << bundle.budget.maxActiveClusters
           << " budgetGuides=" << bundle.budget.maxActiveGuides;
        if (report)
        {
            ss << " priorRms=" << report->priorEvaluation.rmsPositionError
               << " residualRms=" << report->residualEvaluation.rmsPositionError
               << " priorTemporalRms=" << report->priorTemporalEvaluation.rmsPositionError
               << " residualTemporalRms=" << report->residualTemporalEvaluation.rmsPositionError
               << " activeSetPredictorAcc=" << report->activeSetPredictorEvaluation.classificationAccuracy
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
        frameCsv << "frameIndex,timeSeconds,priorRms,hybridRmsBeforeCorrection,hybridRmsAfterCorrection,finalOutputRms,estimatedFrameMs,estimatedPlanMemoryMegabytes,selectedClusters,selectedGuides,selectedStrands,selectedNaturalnessGain,averageEfficiencyScore,editCoverageRatio,avgSelectedParticlesPerStrand,maxSelectedParticlesPerStrand,budgetScale,recommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctedStrands,correctedPoints,avgPenetration,maxPenetration,avgGuideContact,avgGuidePenetration,avgGuideUncertainty,maxGuideUncertainty,avgGuideModelError,avgGuidePredictorRisk,maxGuidePredictorRisk\n";

        std::vector<Vec3> restPriorGuidePoints;
        std::vector<Vec3> smoothedPriorGuidePoints;
        std::vector<Vec3> previousSmoothedPriorGuidePoints;
        std::vector<Vec3> previousPreviousSmoothedPriorGuidePoints;
        std::vector<Vec3> previousRawPriorGuidePoints;
        std::vector<Vec3> previousPreviousRawPriorGuidePoints;
        BuildRestGuideFramePointsForModel(bundle.priorModel, restPriorGuidePoints);
        smoothedPriorGuidePoints = restPriorGuidePoints;
        previousSmoothedPriorGuidePoints = smoothedPriorGuidePoints;
        previousPreviousSmoothedPriorGuidePoints = previousSmoothedPriorGuidePoints;
        previousRawPriorGuidePoints = restPriorGuidePoints;
        previousPreviousRawPriorGuidePoints = previousRawPriorGuidePoints;

        std::vector<Vec3> restResidualGuidePoints;
        std::vector<Vec3> previousResidualGuidePoints;
        std::vector<Vec3> previousPreviousResidualGuidePoints;
        std::vector<Vec3> previousBaseResidualGuideInput;
        std::vector<Vec3> previousPreviousBaseResidualGuideInput;
        BuildRestGuideFramePointsForModel(bundle.residualModel, restResidualGuidePoints);
        previousResidualGuidePoints = restResidualGuidePoints;
        previousPreviousResidualGuidePoints = previousResidualGuidePoints;
        previousBaseResidualGuideInput = restResidualGuidePoints;
        previousPreviousBaseResidualGuideInput = previousBaseResidualGuideInput;

        std::vector<Vec3> restGuideRoots;
        BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherSequence.referenceAsset.restPoints, restGuideRoots);
        std::vector<Vec3> previousHybridGuideRoots = restGuideRoots;
        std::vector<float> previousGuideUncertainty(previousHybridGuideRoots.size(), 0.0f);
        std::vector<Vec3> previousOutputPoints = teacherSequence.referenceAsset.restPoints;
        std::vector<Vec3> cachedTierSnapshotPoints = teacherSequence.referenceAsset.restPoints;

        double priorRmsSum = 0.0;
        double hybridBeforeRmsSum = 0.0;
        double hybridAfterRmsSum = 0.0;
        double selectedClusterSum = 0.0;
        double selectedGuideSum = 0.0;
        double correctedPointSum = 0.0;
        double selectedNaturalnessGainSum = 0.0;
        double efficiencyScoreSum = 0.0;
        double editCoverageRatioSum = 0.0;
        double selectedParticlesPerStrandSum = 0.0;
        double estimatedPlanMemorySum = 0.0;
        double penetrationSum = 0.0;
        double predictorRiskSum = 0.0;
        double silhouetteErrorSum = 0.0;
        double clumpPreservationSum = 0.0;
        double rootLocalitySum = 0.0;
        uint32_t penetrationEventCount = 0u;
        float peakEstimatedRamMegabytes = 0.0f;
        float maxHybridAfterRms = 0.0f;
        float maxPenetration = 0.0f;
        float maxGuidePredictorRisk = 0.0f;
        uint32_t maxSelectedParticlesPerStrand = 0u;
        float previousFrameMilliseconds = 0.0f;
        std::vector<float> estimatedFrameMillisecondsHistory;

        const float residualBlend = (settings.residualBlendOverride >= 0.0f)
            ? settings.residualBlendOverride
            : bundle.trainingSettings.residualBlend;
        const float predictorBlend = (settings.activeSetPredictorBlendOverride >= 0.0f)
            ? settings.activeSetPredictorBlendOverride
            : bundle.activeSetPredictorBlend;

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

            const float priorVelocityPredictionGain = Clamp(bundle.priorVelocityPredictionGain, 0.0f, 1.0f);
            const float priorAccelerationPredictionGain = EstimateSecondOrderPredictionGain(previousPreviousSmoothedPriorGuidePoints,
                                                                                           previousSmoothedPriorGuidePoints,
                                                                                           smoothedPriorGuidePoints,
                                                                                           priorVelocityPredictionGain,
                                                                                           bundle.priorAccelerationPredictionGain,
                                                                                           0.50f);
            std::vector<Vec3> predictedPriorGuidePoints = smoothedPriorGuidePoints;
            for (uint32_t i = 0u; i < predictedPriorGuidePoints.size() && i < previousSmoothedPriorGuidePoints.size(); ++i)
            {
                const Vec3 velocity = smoothedPriorGuidePoints[i] - previousSmoothedPriorGuidePoints[i];
                predictedPriorGuidePoints[i] += velocity * priorVelocityPredictionGain;
                if (i < previousPreviousSmoothedPriorGuidePoints.size())
                {
                    const Vec3 acceleration = smoothedPriorGuidePoints[i] - previousSmoothedPriorGuidePoints[i] * 2.0f + previousPreviousSmoothedPriorGuidePoints[i];
                    predictedPriorGuidePoints[i] += acceleration * priorAccelerationPredictionGain;
                }
            }
            if (settings.enableTemporalNetworks && frameIndex >= 2u && IsTemporalGuideNetworkReady(bundle.priorTemporalNetwork))
            {
                std::vector<Vec3> temporalPriorGuidePoints;
                if (PredictTemporalGuideFrame(bundle.priorTemporalNetwork,
                                              restPriorGuidePoints,
                                              previousPreviousRawPriorGuidePoints,
                                              previousRawPriorGuidePoints,
                                              currentPriorGuidePoints,
                                              temporalPriorGuidePoints,
                                              outError))
                {
                    const float temporalBlend = Clamp(bundle.priorTemporalBlend, 0.0f, 1.0f);
                    for (uint32_t i = 0u; i < predictedPriorGuidePoints.size() && i < temporalPriorGuidePoints.size(); ++i)
                    {
                        predictedPriorGuidePoints[i] = Lerp(predictedPriorGuidePoints[i], temporalPriorGuidePoints[i], temporalBlend);
                    }
                }
                else
                {
                    return false;
                }
            }

            HairAsset priorAsset;
            if (settings.useTeacherPhysicsProxy)
            {
                priorAsset = teacherSequence.referenceAsset;
                priorAsset.restPoints = teacherFrame;
            }
            else if (!ReconstructReducedModelFrame(bundle.priorModel, predictedPriorGuidePoints, priorAsset, outError))
            {
                return false;
            }

            std::vector<Vec3> baseResidualGuideInput = restResidualGuidePoints;
            HairAsset residualAsset = priorAsset;
            std::vector<Vec3> currentResidualGuidePoints;
            if (settings.enableResidualModel && !settings.useTeacherPhysicsProxy)
            {
                BuildGuideFramePointsForModel(bundle.residualModel, teacherFrame, currentResidualGuidePoints);
                std::vector<Vec3> priorResidualGuidePoints;
                BuildGuideFramePointsForModel(bundle.residualModel, priorAsset.restPoints, priorResidualGuidePoints);
                const float residualVelocityPredictionGain = Clamp(bundle.residualVelocityPredictionGain, 0.0f, 1.0f);
                const float residualAccelerationPredictionGain = EstimateSecondOrderPredictionGain(previousPreviousResidualGuidePoints,
                                                                                                  previousResidualGuidePoints,
                                                                                                  currentResidualGuidePoints,
                                                                                                  residualVelocityPredictionGain,
                                                                                                  bundle.residualAccelerationPredictionGain,
                                                                                                  0.60f);
                for (uint32_t i = 0u; i < baseResidualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    baseResidualGuideInput[i] += (currentResidualGuidePoints[i] - priorResidualGuidePoints[i]) * bundle.trainingSettings.residualGuideGain;
                }
                std::vector<Vec3> residualGuideInput = baseResidualGuideInput;
                for (uint32_t i = 0u; i < residualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    if (i < previousResidualGuidePoints.size())
                    {
                        const Vec3 velocity = currentResidualGuidePoints[i] - previousResidualGuidePoints[i];
                        residualGuideInput[i] += velocity * residualVelocityPredictionGain;
                    }
                    if (i < previousResidualGuidePoints.size() && i < previousPreviousResidualGuidePoints.size())
                    {
                        const Vec3 acceleration = currentResidualGuidePoints[i] - previousResidualGuidePoints[i] * 2.0f + previousPreviousResidualGuidePoints[i];
                        residualGuideInput[i] += acceleration * residualAccelerationPredictionGain;
                    }
                }
                if (settings.enableTemporalNetworks && frameIndex >= 2u && IsTemporalGuideNetworkReady(bundle.residualTemporalNetwork))
                {
                    std::vector<Vec3> temporalResidualGuidePoints;
                    if (PredictTemporalGuideFrame(bundle.residualTemporalNetwork,
                                                  restResidualGuidePoints,
                                                  previousPreviousBaseResidualGuideInput,
                                                  previousBaseResidualGuideInput,
                                                  baseResidualGuideInput,
                                                  temporalResidualGuidePoints,
                                                  outError))
                    {
                        const float temporalBlend = Clamp(bundle.residualTemporalBlend, 0.0f, 1.0f);
                        for (uint32_t i = 0u; i < residualGuideInput.size() && i < temporalResidualGuidePoints.size(); ++i)
                        {
                            residualGuideInput[i] = Lerp(residualGuideInput[i], temporalResidualGuidePoints[i], temporalBlend);
                        }
                    }
                    else
                    {
                        return false;
                    }
                }
                if (!ReconstructReducedModelFrame(bundle.residualModel, residualGuideInput, residualAsset, outError))
                {
                    return false;
                }
            }

            std::vector<Vec3> hybridBeforeCorrection;
            if (settings.useTeacherPhysicsProxy)
            {
                hybridBeforeCorrection = teacherFrame;
            }
            else if (settings.enableResidualModel)
            {
                ComposeHybridPoints(teacherSequence.referenceAsset,
                                    priorAsset.restPoints,
                                    residualAsset.restPoints,
                                    residualBlend,
                                    hybridBeforeCorrection);
            }
            else
            {
                hybridBeforeCorrection = priorAsset.restPoints;
            }

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
            std::vector<float> guidePredictorRisk;
            if (settings.enableActiveSetPredictor)
            {
                ApplyActiveSetPredictorBoost(teacherSequence.referenceAsset,
                                             clusters,
                                             restGuideRoots,
                                             currentHybridGuideRoots,
                                             previousHybridGuideRoots,
                                             guidePenetrationScores,
                                             guideModelErrorScores,
                                             bundle.activeSetPredictor,
                                             predictorBlend,
                                             bundle.scoringSettings,
                                             guideUncertainty,
                                             guideContactScores,
                                             &guidePredictorRisk);
            }
            else
            {
                guidePredictorRisk.assign(guideUncertainty.size(), 0.0f);
            }

            const float runtimeWindStrengthProxy = Clamp(bundle.contactGain * 0.15f + bundle.uncertaintyGain * 0.35f + bundle.temporalCoherenceGain * 0.35f, 0.0f, 1.0f);
            std::vector<float> guideWindExposureScores = BuildGuideWindExposureScores(teacherSequence.referenceAsset,
                                                                                      currentHybridGuideRoots,
                                                                                      previousHybridGuideRoots,
                                                                                      1.0f / std::max(runtimeFrameRate, 1.0f),
                                                                                      runtimeWindStrengthProxy,
                                                                                      Vec3(1.0f, 0.0f, 0.0f));
            std::vector<float> guideEnvironmentComplexityScores = BuildGuideEnvironmentComplexityScores(guideContactScores,
                                                                                                       guidePenetrationScores,
                                                                                                       guideWindExposureScores,
                                                                                                       guideModelErrorScores);
            ActiveSetRuntimeInput runtimeInput = {};
            runtimeInput.currentGuideRoots = currentHybridGuideRoots;
            runtimeInput.previousGuideRoots = previousHybridGuideRoots;
            runtimeInput.guideContactScores = guideContactScores;
            runtimeInput.guidePenetrationScores = guidePenetrationScores;
            runtimeInput.guideModelErrorScores = guideModelErrorScores;
            runtimeInput.guideWindExposureScores = guideWindExposureScores;
            runtimeInput.guideEnvironmentComplexityScores = guideEnvironmentComplexityScores;
            runtimeInput.guideUncertainty = guideUncertainty;
            runtimeInput.cameraPosition = Vec3(0.0f, 1.55f, -1.85f);
            runtimeInput.cameraForward = Normalize(Vec3(0.0f, -0.05f, 1.0f));
            runtimeInput.fixedTimeStep = 1.0f / std::max(runtimeFrameRate, 1.0f);

            ActiveSetBudget budget = settings.useBudgetOverride ? settings.budgetOverride : bundle.budget;
            budget.previousFrameMilliseconds = previousFrameMilliseconds;
            ActiveSetPlan plan;
            if (settings.useTeacherPhysicsProxy)
            {
                BuildPurePhysicsProxyPlan(teacherSequence.referenceAsset, clusters, budget, plan);
            }
            else if (!EvaluateActiveSetPlan(teacherSequence.referenceAsset, clusters, runtimeInput, bundle.scoringSettings, budget, plan, outError))
            {
                return false;
            }

            std::vector<Vec3> correctedPoints = hybridBeforeCorrection;
            SparseCorrectiveReport correctiveReport = {};
            if (settings.useTeacherPhysicsProxy)
            {
                correctedPoints = teacherFrame;
                correctiveReport.correctedStrandCount = static_cast<uint32_t>(teacherSequence.referenceAsset.strands.size());
                correctiveReport.correctedPointCount = static_cast<uint32_t>(teacherFrame.size());
            }
            else if (settings.enableSparseCorrectivePhysics && !ApplySparseCorrectivePhysics(teacherSequence.referenceAsset,
                                                                                              plan,
                                                                                              runtimeColliders,
                                                                                              correctedPoints,
                                                                                              bundle.correctiveSettings,
                                                                                              &correctiveReport,
                                                                                              outError))
            {
                return false;
            }

            std::vector<Vec3> finalOutputPoints = correctedPoints;
            std::vector<Vec3> tierScratchPoints;
            for (uint32_t strandIndex = 0u; strandIndex < teacherSequence.referenceAsset.strands.size(); ++strandIndex)
            {
                const SimTier tier = (strandIndex < plan.recommendedSimulationTiers.size())
                    ? plan.recommendedSimulationTiers[strandIndex]
                    : SimTier::Reduced;
                switch (tier)
                {
                case SimTier::Full:
                    break;
                case SimTier::Reduced:
                    CopyStrandPoints(teacherSequence.referenceAsset, hybridBeforeCorrection, strandIndex, tierScratchPoints);
                    WriteStrandPoints(teacherSequence.referenceAsset, strandIndex, tierScratchPoints, finalOutputPoints);
                    break;
                case SimTier::Frozen:
                    BuildRootAnchoredTierPose(teacherSequence.referenceAsset,
                                              strandIndex,
                                              hybridBeforeCorrection,
                                              previousOutputPoints,
                                              bundle.frozenRootFollow,
                                              bundle.frozenStrandBlend,
                                              tierScratchPoints);
                    WriteStrandPoints(teacherSequence.referenceAsset, strandIndex, tierScratchPoints, finalOutputPoints);
                    break;
                case SimTier::Cached:
                    BuildRootAnchoredTierPose(teacherSequence.referenceAsset,
                                              strandIndex,
                                              hybridBeforeCorrection,
                                              cachedTierSnapshotPoints,
                                              bundle.cachedRootFollow,
                                              bundle.cachedStrandBlend,
                                              tierScratchPoints);
                    WriteStrandPoints(teacherSequence.referenceAsset, strandIndex, tierScratchPoints, finalOutputPoints);
                    break;
                }
            }

            const uint32_t cacheRefreshInterval = std::max<uint32_t>(1u, bundle.cachedFrameRefreshInterval);
            for (uint32_t strandIndex = 0u; strandIndex < teacherSequence.referenceAsset.strands.size(); ++strandIndex)
            {
                const SimTier tier = (strandIndex < plan.recommendedSimulationTiers.size())
                    ? plan.recommendedSimulationTiers[strandIndex]
                    : SimTier::Reduced;
                if (tier != SimTier::Cached || frameIndex == 0u || ((frameIndex + 1u) % cacheRefreshInterval) == 0u)
                {
                    CopyStrandPoints(teacherSequence.referenceAsset, finalOutputPoints, strandIndex, tierScratchPoints);
                    WriteStrandPoints(teacherSequence.referenceAsset, strandIndex, tierScratchPoints, cachedTierSnapshotPoints);
                }
            }

            if (!AppendHairCacheFrame(hairCache, teacherSequence.referenceAsset, finalOutputPoints, outError))
            {
                return false;
            }
            std::vector<Vec3> guideFramePoints;
            BuildGuideFramePoints(teacherSequence.referenceAsset, finalOutputPoints, guideFramePoints);
            if (!AppendGuideCacheGuidePoints(guideCache, guideFramePoints, outError))
            {
                return false;
            }
            std::vector<Vec3> finalGuideRoots;
            BuildGuideRootsFromPoints(teacherSequence.referenceAsset, finalOutputPoints, finalGuideRoots);

            const float priorRms = ComputeRmsError(priorAsset.restPoints, teacherFrame);
            const float hybridBeforeRms = ComputeRmsError(hybridBeforeCorrection, teacherFrame);
            const float hybridAfterRms = ComputeRmsError(correctedPoints, teacherFrame);
            const float finalOutputRms = ComputeRmsError(finalOutputPoints, teacherFrame);
            const HairAsset teacherFrameAsset = BuildFrameAsset(teacherSequence.referenceAsset, teacherFrame);
            const HairAsset finalFrameAsset = BuildFrameAsset(teacherSequence.referenceAsset, finalOutputPoints);
            const HairShapeComparisonReport shapeReport = CompareHairShape(teacherFrameAsset, finalFrameAsset, 12u);

            const float estimatedFrameMilliseconds = settings.useTeacherPhysicsProxy
                ? (2.80f + static_cast<float>(teacherSequence.referenceAsset.strands.size()) * 0.020f + static_cast<float>(teacherSequence.referenceAsset.restPoints.size()) * 0.0022f + static_cast<float>(std::max<uint32_t>(budget.maxSolverIterations, budget.minSolverIterations)) * 0.35f)
                : (1.10f +
                   static_cast<float>(plan.summary.selectedClusterCount) * 0.32f +
                   static_cast<float>(plan.summary.selectedGuideCount) * 0.012f +
                   static_cast<float>(correctiveReport.correctedPointCount) * 0.0019f +
                   static_cast<float>(plan.summary.recommendedSolverIterations) * 0.24f);

            frameCsv << frameIndex << ","
                     << (static_cast<float>(frameIndex) / std::max(runtimeFrameRate, 1.0f)) << ","
                     << priorRms << ","
                     << hybridBeforeRms << ","
                     << hybridAfterRms << ","
                     << finalOutputRms << ","
                     << estimatedFrameMilliseconds << ","
                     << plan.summary.estimatedMemoryMegabytes << ","
                     << plan.summary.selectedClusterCount << ","
                     << plan.summary.selectedGuideCount << ","
                     << plan.summary.selectedStrandCount << ","
                     << plan.summary.selectedNaturalnessGain << ","
                     << plan.summary.averageEfficiencyScore << ","
                     << plan.summary.editCoverageRatio << ","
                     << plan.summary.averageSelectedParticlesPerStrand << ","
                     << plan.summary.maxSelectedParticlesPerStrand << ","
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
                     << ComputeAverageScalar(guideModelErrorScores) << ","
                     << ComputeAverageScalar(guidePredictorRisk) << ","
                     << ComputeMaxScalar(guidePredictorRisk) << "\n";

            if (settings.exportPerFrameHairBins)
            {
                HairAsset frameAsset = teacherSequence.referenceAsset;
                frameAsset.restPoints = finalOutputPoints;
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

            previousFrameMilliseconds = estimatedFrameMilliseconds;
            previousPreviousSmoothedPriorGuidePoints = previousSmoothedPriorGuidePoints;
            previousSmoothedPriorGuidePoints = smoothedPriorGuidePoints;
            previousPreviousRawPriorGuidePoints = previousRawPriorGuidePoints;
            previousRawPriorGuidePoints = currentPriorGuidePoints;
            previousPreviousResidualGuidePoints = previousResidualGuidePoints;
            previousResidualGuidePoints = currentResidualGuidePoints;
            previousPreviousBaseResidualGuideInput = previousBaseResidualGuideInput;
            previousBaseResidualGuideInput = baseResidualGuideInput;
            previousHybridGuideRoots = finalGuideRoots;
            previousGuideUncertainty = guideUncertainty;
            previousOutputPoints = finalOutputPoints;
            estimatedFrameMillisecondsHistory.push_back(previousFrameMilliseconds);

            priorRmsSum += static_cast<double>(priorRms);
            hybridBeforeRmsSum += static_cast<double>(hybridBeforeRms);
            hybridAfterRmsSum += static_cast<double>(finalOutputRms);
            selectedClusterSum += static_cast<double>(plan.summary.selectedClusterCount);
            selectedGuideSum += static_cast<double>(plan.summary.selectedGuideCount);
            correctedPointSum += static_cast<double>(correctiveReport.correctedPointCount);
            selectedNaturalnessGainSum += static_cast<double>(plan.summary.selectedNaturalnessGain);
            efficiencyScoreSum += static_cast<double>(plan.summary.averageEfficiencyScore);
            editCoverageRatioSum += static_cast<double>(plan.summary.editCoverageRatio);
            selectedParticlesPerStrandSum += static_cast<double>(plan.summary.averageSelectedParticlesPerStrand);
            estimatedPlanMemorySum += static_cast<double>(plan.summary.estimatedMemoryMegabytes);
            penetrationSum += static_cast<double>(correctiveReport.averagePenetration);
            predictorRiskSum += static_cast<double>(ComputeAverageScalar(guidePredictorRisk));
            silhouetteErrorSum += static_cast<double>(shapeReport.silhouetteError);
            clumpPreservationSum += static_cast<double>(shapeReport.clumpPreservationScore);
            rootLocalitySum += static_cast<double>(shapeReport.rootLocalityScore);
            if (correctiveReport.maxPenetration > 1.0e-5f)
            {
                ++penetrationEventCount;
            }
            peakEstimatedRamMegabytes = std::max(peakEstimatedRamMegabytes,
                                                 EstimateRuntimeRamMegabytes(teacherSequence.referenceAsset,
                                                                             bundle,
                                                                             static_cast<uint32_t>(clusters.size()),
                                                                             static_cast<uint32_t>(cachedTierSnapshotPoints.size())));
            maxHybridAfterRms = std::max(maxHybridAfterRms, finalOutputRms);
            maxPenetration = std::max(maxPenetration, correctiveReport.maxPenetration);
            maxGuidePredictorRisk = std::max(maxGuidePredictorRisk, ComputeMaxScalar(guidePredictorRisk));
            maxSelectedParticlesPerStrand = std::max(maxSelectedParticlesPerStrand, plan.summary.maxSelectedParticlesPerStrand);
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
        outReport.averageSelectedNaturalnessGain = static_cast<float>(selectedNaturalnessGainSum / frameCountF);
        outReport.averageEfficiencyScore = static_cast<float>(efficiencyScoreSum / frameCountF);
        outReport.averageEditCoverageRatio = static_cast<float>(editCoverageRatioSum / frameCountF);
        outReport.averageSelectedParticlesPerStrand = static_cast<float>(selectedParticlesPerStrandSum / frameCountF);
        outReport.maxSelectedParticlesPerStrand = maxSelectedParticlesPerStrand;
        outReport.averageEstimatedPlanMemoryMegabytes = static_cast<float>(estimatedPlanMemorySum / frameCountF);
        outReport.averagePenetration = static_cast<float>(penetrationSum / frameCountF);
        outReport.maxPenetration = maxPenetration;
        outReport.averageGuidePredictorRisk = static_cast<float>(predictorRiskSum / frameCountF);
        outReport.maxGuidePredictorRisk = maxGuidePredictorRisk;
        outReport.averageSilhouetteError = static_cast<float>(silhouetteErrorSum / frameCountF);
        outReport.averageClumpPreservationScore = static_cast<float>(clumpPreservationSum / frameCountF);
        outReport.averageRootLocalityScore = static_cast<float>(rootLocalitySum / frameCountF);
        outReport.peakEstimatedRamMegabytes = peakEstimatedRamMegabytes;
        const float durationSeconds = std::max(frameCountF / std::max(teacherSequence.frameRate, 1.0e-4f), 1.0e-4f);
        outReport.penetrationEventsPerSecond = static_cast<float>(static_cast<double>(penetrationEventCount) / static_cast<double>(durationSeconds));
        double estimatedFrameMillisecondsSum = 0.0;
        for (const float value : estimatedFrameMillisecondsHistory)
        {
            estimatedFrameMillisecondsSum += static_cast<double>(value);
        }
        outReport.averageEstimatedFrameMilliseconds = estimatedFrameMillisecondsHistory.empty()
            ? 0.0f
            : static_cast<float>(estimatedFrameMillisecondsSum / static_cast<double>(estimatedFrameMillisecondsHistory.size()));
        outReport.p95EstimatedFrameMilliseconds = ComputeQuantile95(estimatedFrameMillisecondsHistory);
        outReport.maxEstimatedFrameMilliseconds = estimatedFrameMillisecondsHistory.empty()
            ? 0.0f
            : *std::max_element(estimatedFrameMillisecondsHistory.begin(), estimatedFrameMillisecondsHistory.end());
        outReport.hitRate60Fps = estimatedFrameMillisecondsHistory.empty()
            ? 0.0f
            : static_cast<float>(std::count_if(estimatedFrameMillisecondsHistory.begin(), estimatedFrameMillisecondsHistory.end(),
                                               [](const float value)
                                               {
                                                   return value <= 16.67f;
                                               }) / static_cast<double>(estimatedFrameMillisecondsHistory.size()));

        if (!WriteHybridSummaryJson(outputDirectory / "full_hybrid_summary.json", bundle, nullptr, &outReport, outError))
        {
            return false;
        }
        return true;
    }

}
