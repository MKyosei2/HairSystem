#include "hairgpu/HybridRuntime.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
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
                    const Vec3 point = points[pointIndex];
                    const float floorProximity = Clamp((shellDistance * 2.0f - (point.y - floorY)) / std::max(shellDistance * 2.0f, 1.0e-4f), 0.0f, 1.0f);
                    bestScore = std::max(bestScore, floorProximity);
                    for (const SphereCollider& sphere : colliders.spheres)
                    {
                        const float distance = Length(point - sphere.center) - sphere.radius;
                        bestScore = std::max(bestScore, Clamp((shellDistance * 3.0f - distance) / std::max(shellDistance * 3.0f, 1.0e-4f), 0.0f, 1.0f));
                    }
                    for (const CapsuleCollider& capsule : colliders.capsules)
                    {
                        const Vec3 closest = ClosestPointOnSegment(capsule.pointA, capsule.pointB, point);
                        const float distance = Length(point - closest) - std::max(capsule.radiusA, capsule.radiusB);
                        bestScore = std::max(bestScore, Clamp((shellDistance * 3.5f - distance) / std::max(shellDistance * 3.5f, 1.0e-4f), 0.0f, 1.0f));
                    }
                }
                scores[guideIndex] = Clamp(bestScore, 0.0f, 1.0f);
            }
            return scores;
        }

        std::vector<float> BuildGuideUncertainty(const HairAsset& asset,
                                                 const std::vector<Vec3>& currentRoots,
                                                 const std::vector<Vec3>& previousRoots,
                                                 const std::vector<float>& contactScores,
                                                 float fixedTimeStep)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> uncertainty(guideCount, 0.0f);
            const float safeDt = std::max(fixedTimeStep, 1.0e-4f);
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
                uncertainty[guideIndex] = Clamp(motion * 0.45f + displacement * 0.32f + contact * 0.23f, 0.0f, 1.0f);
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

                    Vec3 point = inOutPoints[pointIndex];
                    PenetrationResult best = {};
                    best.correctedPoint = point;

                    if (settings.affectFloor)
                    {
                        PenetrationResult floorResult = ResolveAgainstFloor(point, -1.05f, settings.shellDistance);
                        if (floorResult.penetration > best.penetration)
                        {
                            best = floorResult;
                        }
                    }
                    if (settings.affectExternalColliders)
                    {
                        for (const SphereCollider& sphere : colliders.spheres)
                        {
                            PenetrationResult result = ResolveAgainstSphere(point, sphere, settings.shellDistance);
                            if (result.penetration > best.penetration)
                            {
                                best = result;
                            }
                        }
                        for (const CapsuleCollider& capsule : colliders.capsules)
                        {
                            PenetrationResult result = ResolveAgainstCapsule(point, capsule, settings.shellDistance);
                            if (result.penetration > best.penetration)
                            {
                                best = result;
                            }
                        }
                    }

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

        const ColliderSet runtimeColliders = (colliders.spheres.empty() && colliders.capsules.empty())
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
        frameCsv << "frameIndex,timeSeconds,selectedClusters,selectedGuides,selectedStrands,budgetScale,recommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctedStrands,correctedPoints,avgPenetration,maxPenetration,avgLengthErrorBefore,avgLengthErrorAfter,estimatedSimulationMilliseconds\n";

        std::vector<Vec3> previousGuideRoots;
        BuildGuideRootsFromPoints(asset, asset.restPoints, previousGuideRoots);

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
            const std::vector<float> guideUncertainty = BuildGuideUncertainty(asset,
                                                                              currentGuideRoots,
                                                                              previousGuideRoots,
                                                                              guideContactScores,
                                                                              1.0f / std::max(settings.frameRate, 1.0f));

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
            out << "  \"contactGain\": " << bundle.contactGain;
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

        (void)colliders;
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

        const ColliderSet runtimeColliders = (colliders.spheres.empty() && colliders.capsules.empty())
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
        frameCsv << "frameIndex,timeSeconds,priorRms,hybridRmsBeforeCorrection,hybridRmsAfterCorrection,selectedClusters,selectedGuides,selectedStrands,budgetScale,recommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctedStrands,correctedPoints,avgPenetration,maxPenetration\n";

        std::vector<Vec3> restPriorGuidePoints;
        std::vector<Vec3> smoothedPriorGuidePoints;
        BuildRestGuideFramePointsForModel(bundle.priorModel, restPriorGuidePoints);
        smoothedPriorGuidePoints = restPriorGuidePoints;
        std::vector<Vec3> previousHybridGuideRoots;
        BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherSequence.referenceAsset.restPoints, previousHybridGuideRoots);

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

            HairAsset priorAsset;
            if (!ReconstructReducedModelFrame(bundle.priorModel, smoothedPriorGuidePoints, priorAsset, outError))
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
            const std::vector<float> guideContactScores = BuildGuideContactScores(teacherSequence.referenceAsset,
                                                                                  hybridBeforeCorrection,
                                                                                  runtimeColliders,
                                                                                  -1.05f,
                                                                                  bundle.correctiveSettings.shellDistance);
            std::vector<float> guideUncertainty = BuildGuideUncertainty(teacherSequence.referenceAsset,
                                                                        currentHybridGuideRoots,
                                                                        previousHybridGuideRoots,
                                                                        guideContactScores,
                                                                        1.0f / std::max(runtimeFrameRate, 1.0f));
            const uint32_t guideCount = std::min<uint32_t>(teacherSequence.referenceAsset.guideStrandCount, static_cast<uint32_t>(teacherSequence.referenceAsset.strands.size()));
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = teacherSequence.referenceAsset.strands[guideIndex];
                const uint32_t pointIndex = strand.pointOffset;
                const float residualMagnitude = (pointIndex < residualAsset.restPoints.size() && pointIndex < teacherSequence.referenceAsset.restPoints.size())
                    ? Length(residualAsset.restPoints[pointIndex] - teacherSequence.referenceAsset.restPoints[pointIndex])
                    : 0.0f;
                const float residualProxy = Clamp(residualMagnitude / 0.025f, 0.0f, 1.0f) * bundle.uncertaintyGain;
                guideUncertainty[guideIndex] = Clamp(std::max(guideUncertainty[guideIndex], residualProxy) + guideContactScores[guideIndex] * (bundle.contactGain - 1.0f), 0.0f, 1.0f);
            }

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
                     << correctiveReport.maxPenetration << "\n";

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
            }

            previousFrameMilliseconds = 1.10f +
                                        static_cast<float>(plan.summary.selectedClusterCount) * 0.32f +
                                        static_cast<float>(plan.summary.selectedGuideCount) * 0.012f +
                                        static_cast<float>(correctiveReport.correctedPointCount) * 0.0019f +
                                        static_cast<float>(plan.summary.recommendedSolverIterations) * 0.24f;
            previousHybridGuideRoots = currentHybridGuideRoots;

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
