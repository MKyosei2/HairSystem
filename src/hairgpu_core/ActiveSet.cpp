#include "hairgpu/ActiveSet.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <tuple>

namespace hairgpu
{
    namespace
    {
        struct GuideDescriptor
        {
            uint32_t strandIndex = 0u;
            uint32_t groupId = 0u;
            Vec3 restRoot = { 0.0f, 0.0f, 0.0f };
        };

        Vec3 AveragePosition(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices)
        {
            if (indices.empty())
            {
                return Vec3(0.0f, 0.0f, 0.0f);
            }

            Vec3 total(0.0f, 0.0f, 0.0f);
            uint32_t count = 0u;
            for (uint32_t index : indices)
            {
                if (index >= positions.size())
                {
                    continue;
                }
                total += positions[index];
                ++count;
            }
            return (count > 0u) ? (total / static_cast<float>(count)) : Vec3(0.0f, 0.0f, 0.0f);
        }

        float AverageScalar(const std::vector<float>& values, const std::vector<uint32_t>& indices)
        {
            if (indices.empty())
            {
                return 0.0f;
            }

            float total = 0.0f;
            uint32_t count = 0u;
            for (uint32_t index : indices)
            {
                if (index >= values.size())
                {
                    continue;
                }
                total += values[index];
                ++count;
            }
            return (count > 0u) ? (total / static_cast<float>(count)) : 0.0f;
        }

        Vec3 ComputeGuideRestRoot(const HairAsset& asset, uint32_t strandIndex)
        {
            if (strandIndex >= asset.strands.size())
            {
                return Vec3(0.0f, 0.0f, 0.0f);
            }
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0u || strand.pointOffset >= asset.restPoints.size())
            {
                return Vec3(0.0f, 0.0f, 0.0f);
            }
            return asset.restPoints[strand.pointOffset];
        }

        std::vector<Vec3> BuildGuideRestRoots(const HairAsset& asset)
        {
            std::vector<Vec3> roots;
            roots.reserve(asset.guideStrandCount);
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                roots.push_back(ComputeGuideRestRoot(asset, guideIndex));
            }
            return roots;
        }

        float ComputeClusterRadius(const std::vector<Vec3>& guideRoots,
                                   const std::vector<uint32_t>& clusterGuides,
                                   const Vec3& centroid)
        {
            float radius = 0.0f;
            for (uint32_t guideIndex : clusterGuides)
            {
                if (guideIndex >= guideRoots.size())
                {
                    continue;
                }
                radius = std::max(radius, Length(guideRoots[guideIndex] - centroid));
            }
            return radius;
        }

        float ComputeDistanceVisibility(const Vec3& position,
                                        const Vec3& cameraPosition,
                                        const Vec3& cameraForward,
                                        float nearDistance,
                                        float farDistance)
        {
            const Vec3 toPoint = position - cameraPosition;
            const float distance = Length(toPoint);
            const Vec3 direction = Normalize(toPoint);
            const float front = Clamp((Dot(Normalize(cameraForward), direction) + 1.0f) * 0.5f, 0.0f, 1.0f);
            const float distanceFactor = 1.0f - Clamp((distance - nearDistance) / std::max(farDistance - nearDistance, 1.0e-4f), 0.0f, 1.0f);
            return Clamp(front * 0.65f + distanceFactor * 0.35f, 0.0f, 1.0f);
        }

        void BuildGuideBounds(const std::vector<Vec3>& guideRoots, Vec3& outMin, Vec3& outMax)
        {
            outMin = Vec3(FLT_MAX, FLT_MAX, FLT_MAX);
            outMax = Vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
            for (const Vec3& p : guideRoots)
            {
                outMin.x = std::min(outMin.x, p.x);
                outMin.y = std::min(outMin.y, p.y);
                outMin.z = std::min(outMin.z, p.z);
                outMax.x = std::max(outMax.x, p.x);
                outMax.y = std::max(outMax.y, p.y);
                outMax.z = std::max(outMax.z, p.z);
            }
            if (outMin.x > outMax.x)
            {
                outMin = Vec3(-0.1f, 1.4f, -0.1f);
                outMax = Vec3(0.1f, 1.8f, 0.1f);
            }
        }

        float ComputeBudgetScale(const ActiveSetBudget& budget)
        {
            if (!budget.enableBudgetAdaptation || budget.targetFrameMilliseconds <= 1.0e-4f || budget.previousFrameMilliseconds <= 0.0f)
            {
                return 1.0f;
            }

            const float overloadThreshold = budget.targetFrameMilliseconds * std::max(budget.overloadRatio, 1.0f);
            const float underloadThreshold = budget.targetFrameMilliseconds * Clamp(budget.underloadRatio, 0.1f, 1.0f);
            if (budget.previousFrameMilliseconds > overloadThreshold)
            {
                const float severity = Clamp((budget.previousFrameMilliseconds - overloadThreshold) /
                                             std::max(budget.targetFrameMilliseconds, 1.0e-4f),
                                             0.0f, 1.0f);
                return Clamp(1.0f - budget.budgetStep * (0.5f + severity), 0.45f, 1.0f);
            }
            if (budget.previousFrameMilliseconds < underloadThreshold)
            {
                const float headroom = Clamp((underloadThreshold - budget.previousFrameMilliseconds) /
                                             std::max(budget.targetFrameMilliseconds, 1.0e-4f),
                                             0.0f, 1.0f);
                return Clamp(1.0f + budget.budgetStep * (0.35f + headroom), 1.0f, 1.35f);
            }
            return 1.0f;
        }

        uint32_t RecommendSolverIterations(const ActiveSetBudget& budget, float budgetScale)
        {
            const uint32_t minIterations = std::max<uint32_t>(1u, budget.minSolverIterations);
            const uint32_t maxIterations = std::max<uint32_t>(minIterations, budget.maxSolverIterations);
            if (maxIterations == minIterations)
            {
                return minIterations;
            }
            const float normalized = Clamp((budgetScale - 0.45f) / (1.35f - 0.45f), 0.0f, 1.0f);
            const float blended = static_cast<float>(minIterations) +
                                  normalized * static_cast<float>(maxIterations - minIterations);
            return static_cast<uint32_t>(std::round(blended));
        }
    }

    std::vector<ActiveSetCluster> BuildActiveSetClusters(const HairAsset& asset,
                                                         const ActiveSetClusterSettings& settings)
    {
        const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
        if (guideCount == 0u)
        {
            return {};
        }

        const std::vector<Vec3> guideRoots = BuildGuideRestRoots(asset);
        std::vector<GuideDescriptor> guides;
        guides.reserve(guideCount);
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            GuideDescriptor descriptor = {};
            descriptor.strandIndex = guideIndex;
            descriptor.groupId = asset.strands[guideIndex].groupId;
            descriptor.restRoot = guideRoots[guideIndex];
            guides.push_back(descriptor);
        }

        std::vector<bool> assigned(guideCount, false);
        std::vector<uint32_t> guideToCluster(guideCount, UINT32_MAX);
        std::vector<ActiveSetCluster> clusters;
        clusters.reserve((guideCount + std::max<uint32_t>(1u, settings.targetGuideCountPerCluster) - 1u) /
                         std::max<uint32_t>(1u, settings.targetGuideCountPerCluster));

        for (uint32_t seedIndex = 0u; seedIndex < guideCount; ++seedIndex)
        {
            if (assigned[seedIndex])
            {
                continue;
            }

            ActiveSetCluster cluster = {};
            cluster.clusterIndex = static_cast<uint32_t>(clusters.size());
            cluster.groupId = guides[seedIndex].groupId;
            cluster.guideStrandIndices.push_back(guides[seedIndex].strandIndex);
            assigned[seedIndex] = true;
            guideToCluster[guides[seedIndex].strandIndex] = cluster.clusterIndex;

            std::vector<std::pair<float, uint32_t>> candidates;
            candidates.reserve(guideCount);
            for (uint32_t candidateIndex = 0u; candidateIndex < guideCount; ++candidateIndex)
            {
                if (assigned[candidateIndex])
                {
                    continue;
                }
                if (settings.splitByGroupId && guides[candidateIndex].groupId != cluster.groupId)
                {
                    continue;
                }
                const float distance = Length(guides[candidateIndex].restRoot - guides[seedIndex].restRoot);
                if (distance <= settings.maxGuideRootDistance)
                {
                    candidates.emplace_back(distance, candidateIndex);
                }
            }

            std::sort(candidates.begin(), candidates.end(),
                [](const auto& a, const auto& b)
                {
                    return std::tie(a.first, a.second) < std::tie(b.first, b.second);
                });

            const uint32_t targetGuideCount = std::max<uint32_t>(1u, settings.targetGuideCountPerCluster);
            for (const auto& candidate : candidates)
            {
                if (cluster.guideStrandIndices.size() >= targetGuideCount)
                {
                    break;
                }
                const uint32_t candidateIndex = candidate.second;
                if (assigned[candidateIndex])
                {
                    continue;
                }
                assigned[candidateIndex] = true;
                cluster.guideStrandIndices.push_back(guides[candidateIndex].strandIndex);
                guideToCluster[guides[candidateIndex].strandIndex] = cluster.clusterIndex;
            }

            cluster.restRootCentroid = AveragePosition(guideRoots, cluster.guideStrandIndices);
            cluster.restRootRadius = ComputeClusterRadius(guideRoots, cluster.guideStrandIndices, cluster.restRootCentroid);
            cluster.strandIndices = cluster.guideStrandIndices;
            clusters.push_back(std::move(cluster));
        }

        if (settings.preserveGuideFamilies)
        {
            for (uint32_t strandIndex = guideCount; strandIndex < asset.strands.size(); ++strandIndex)
            {
                int32_t parentGuide = (strandIndex < asset.followData.size()) ? asset.followData[strandIndex].parentGuide : -1;
                uint32_t clusterIndex = UINT32_MAX;
                if (parentGuide >= 0 && static_cast<uint32_t>(parentGuide) < guideToCluster.size())
                {
                    clusterIndex = guideToCluster[static_cast<uint32_t>(parentGuide)];
                }
                if (clusterIndex == UINT32_MAX)
                {
                    const Vec3 followerRoot = ComputeGuideRestRoot(asset, strandIndex);
                    float bestDistance = std::numeric_limits<float>::max();
                    for (const ActiveSetCluster& cluster : clusters)
                    {
                        const float distance = Length(cluster.restRootCentroid - followerRoot);
                        if (distance < bestDistance)
                        {
                            bestDistance = distance;
                            clusterIndex = cluster.clusterIndex;
                        }
                    }
                }
                if (clusterIndex < clusters.size())
                {
                    clusters[clusterIndex].strandIndices.push_back(strandIndex);
                }
            }
        }

        return clusters;
    }

    bool EvaluateActiveSetPlan(const HairAsset& asset,
                               const std::vector<ActiveSetCluster>& clusters,
                               const ActiveSetRuntimeInput& runtimeInput,
                               const ActiveSetScoringSettings& scoringSettings,
                               const ActiveSetBudget& budget,
                               ActiveSetPlan& outPlan,
                               std::string& outError)
    {
        outError.clear();
        outPlan = {};
        outPlan.clusters = clusters;

        const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
        if (guideCount == 0u)
        {
            outError = "Asset does not contain guide strands, so an active-set plan cannot be built.";
            return false;
        }
        if (clusters.empty())
        {
            outError = "No clusters were provided to EvaluateActiveSetPlan.";
            return false;
        }

        std::vector<Vec3> restGuideRoots = BuildGuideRestRoots(asset);
        std::vector<Vec3> currentGuideRoots = runtimeInput.currentGuideRoots;
        std::vector<Vec3> previousGuideRoots = runtimeInput.previousGuideRoots;
        std::vector<Vec3> prePreviousGuideRoots = runtimeInput.prePreviousGuideRoots;
        if (currentGuideRoots.size() != guideCount)
        {
            currentGuideRoots = restGuideRoots;
        }
        if (previousGuideRoots.size() != guideCount)
        {
            previousGuideRoots = currentGuideRoots;
        }
        if (prePreviousGuideRoots.size() != guideCount)
        {
            prePreviousGuideRoots = previousGuideRoots;
        }

        Vec3 boundsMin = {};
        Vec3 boundsMax = {};
        BuildGuideBounds(restGuideRoots, boundsMin, boundsMax);
        const Vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;
        const float halfWidth = std::max((boundsMax.x - boundsMin.x) * 0.5f, 1.0e-4f);
        const float maxClusterStrands = static_cast<float>(std::max<size_t>(1u, std::max_element(
            clusters.begin(), clusters.end(),
            [](const ActiveSetCluster& a, const ActiveSetCluster& b)
            {
                return a.strandIndices.size() < b.strandIndices.size();
            })->strandIndices.size()));
        const float safeTimeStep = std::max(runtimeInput.fixedTimeStep, 1.0e-4f);

        outPlan.scores.reserve(clusters.size());
        struct RankedCluster
        {
            uint32_t clusterIndex = 0u;
            float finalScore = 0.0f;
            float visibility = 0.0f;
            uint32_t guideCount = 0u;
            uint32_t strandCount = 0u;
        };
        std::vector<RankedCluster> ranked;
        ranked.reserve(clusters.size());

        float highestScore = 0.0f;
        for (const ActiveSetCluster& cluster : clusters)
        {
            const Vec3 centroidCurrent = AveragePosition(currentGuideRoots, cluster.guideStrandIndices);
            const Vec3 centroidPrevious = AveragePosition(previousGuideRoots, cluster.guideStrandIndices);
            const Vec3 centroidRest = AveragePosition(restGuideRoots, cluster.guideStrandIndices);

            const float motion = Clamp(Length(centroidCurrent - centroidPrevious) / safeTimeStep /
                                       std::max(scoringSettings.velocitySoftRange, 1.0e-4f),
                                       0.0f, 1.0f);
            const float displacement = Clamp(Length(centroidCurrent - centroidRest) /
                                             std::max(scoringSettings.displacementSoftRange, 1.0e-4f),
                                             0.0f, 1.0f);
            const float visibility = (runtimeInput.guideVisibilityScores.size() == guideCount)
                ? Clamp(AverageScalar(runtimeInput.guideVisibilityScores, cluster.guideStrandIndices), 0.0f, 1.0f)
                : ComputeDistanceVisibility(centroidCurrent,
                                            runtimeInput.cameraPosition,
                                            runtimeInput.cameraForward,
                                            scoringSettings.nearDistance,
                                            scoringSettings.farDistance);
            const float silhouette = Clamp(std::fabs(centroidRest.x - boundsCenter.x) / halfWidth, 0.0f, 1.0f);
            const float uncertainty = (runtimeInput.guideUncertainty.size() == guideCount)
                ? Clamp(AverageScalar(runtimeInput.guideUncertainty, cluster.guideStrandIndices), 0.0f, 1.0f)
                : 0.0f;
            const float contact = (runtimeInput.guideContactScores.size() == guideCount)
                ? Clamp(AverageScalar(runtimeInput.guideContactScores, cluster.guideStrandIndices), 0.0f, 1.0f)
                : 0.0f;
            const float externalContact = (runtimeInput.guideExternalContactScores.size() == guideCount)
                ? Clamp(AverageScalar(runtimeInput.guideExternalContactScores, cluster.guideStrandIndices), 0.0f, 1.0f)
                : contact;
            float acceleration = 0.0f;
            if (runtimeInput.guideAccelerationScores.size() == guideCount)
            {
                acceleration = Clamp(AverageScalar(runtimeInput.guideAccelerationScores, cluster.guideStrandIndices), 0.0f, 1.0f);
            }
            else
            {
                const Vec3 centroidPrePrevious = AveragePosition(prePreviousGuideRoots, cluster.guideStrandIndices);
                const Vec3 currentVelocity = (centroidCurrent - centroidPrevious) / safeTimeStep;
                const Vec3 previousVelocity = (centroidPrevious - centroidPrePrevious) / safeTimeStep;
                acceleration = Clamp(Length(currentVelocity - previousVelocity) /
                                     std::max(scoringSettings.accelerationSoftRange, 1.0e-4f),
                                     0.0f, 1.0f);
            }
            const float stretch = (runtimeInput.guideStretchScores.size() == guideCount)
                ? Clamp(AverageScalar(runtimeInput.guideStretchScores, cluster.guideStrandIndices), 0.0f, 1.0f)
                : 0.0f;
            const float bend = (runtimeInput.guideBendScores.size() == guideCount)
                ? Clamp(AverageScalar(runtimeInput.guideBendScores, cluster.guideStrandIndices), 0.0f, 1.0f)
                : 0.0f;
            const float density = (runtimeInput.guideDensityScores.size() == guideCount)
                ? Clamp(AverageScalar(runtimeInput.guideDensityScores, cluster.guideStrandIndices), 0.0f, 1.0f)
                : 0.0f;
            const float clusterSizeScore = Clamp(static_cast<float>(cluster.strandIndices.size()) / maxClusterStrands, 0.0f, 1.0f);

            const float finalScore = Clamp(
                motion * scoringSettings.motionWeight +
                displacement * scoringSettings.displacementWeight +
                visibility * scoringSettings.visibilityWeight +
                silhouette * scoringSettings.silhouetteWeight +
                uncertainty * scoringSettings.uncertaintyWeight +
                contact * scoringSettings.contactWeight +
                acceleration * scoringSettings.accelerationWeight +
                stretch * scoringSettings.stretchWeight +
                bend * scoringSettings.bendWeight +
                density * scoringSettings.densityWeight +
                externalContact * scoringSettings.externalContactWeight +
                clusterSizeScore * scoringSettings.clusterSizeWeight,
                0.0f, 1.0f);

            ActiveSetClusterScore score = {};
            score.clusterIndex = cluster.clusterIndex;
            score.motionScore = motion;
            score.displacementScore = displacement;
            score.visibilityScore = visibility;
            score.silhouetteScore = silhouette;
            score.uncertaintyScore = uncertainty;
            score.contactScore = contact;
            score.accelerationScore = acceleration;
            score.stretchScore = stretch;
            score.bendScore = bend;
            score.densityScore = density;
            score.externalContactScore = externalContact;
            score.clusterSizeScore = clusterSizeScore;
            score.finalScore = finalScore;
            outPlan.scores.push_back(score);

            ranked.push_back({ cluster.clusterIndex,
                               finalScore,
                               visibility,
                               static_cast<uint32_t>(cluster.guideStrandIndices.size()),
                               static_cast<uint32_t>(cluster.strandIndices.size()) });
            highestScore = std::max(highestScore, finalScore);
        }

        std::sort(ranked.begin(), ranked.end(),
            [](const RankedCluster& a, const RankedCluster& b)
            {
                if (a.finalScore != b.finalScore)
                {
                    return a.finalScore > b.finalScore;
                }
                if (a.guideCount != b.guideCount)
                {
                    return a.guideCount > b.guideCount;
                }
                if (a.strandCount != b.strandCount)
                {
                    return a.strandCount > b.strandCount;
                }
                return a.clusterIndex < b.clusterIndex;
            });

        outPlan.strandCorrectiveMask.assign(asset.strands.size(), 0u);
        outPlan.recommendedSimulationTiers.assign(asset.strands.size(), SimTier::Reduced);

        uint32_t selectedGuideCount = 0u;
        uint32_t selectedStrandCount = 0u;
        float selectedScoreSum = 0.0f;
        float lowestSelectedScore = 0.0f;
        const float budgetScale = ComputeBudgetScale(budget);
        const uint32_t effectiveMaxActiveClusters = std::max<uint32_t>(budget.minActiveClusters,
            static_cast<uint32_t>(std::round(static_cast<float>(std::max<uint32_t>(budget.maxActiveClusters, budget.minActiveClusters)) * budgetScale)));
        const uint32_t maxActiveClusters = std::min<uint32_t>(std::max<uint32_t>(budget.minActiveClusters, budget.maxActiveClusters), effectiveMaxActiveClusters);
        const uint32_t maxActiveGuides = (budget.maxActiveGuides > 0u)
            ? std::max<uint32_t>(budget.minActiveClusters, static_cast<uint32_t>(std::round(static_cast<float>(budget.maxActiveGuides) * budgetScale)))
            : 0u;

        for (size_t rankedIndex = 0u; rankedIndex < ranked.size(); ++rankedIndex)
        {
            const RankedCluster& candidate = ranked[rankedIndex];
            if (candidate.clusterIndex >= clusters.size())
            {
                continue;
            }

            const bool requiredByMinimum = rankedIndex < budget.minActiveClusters;
            const bool passesThreshold = candidate.finalScore >= budget.promoteScoreThreshold;
            if (!requiredByMinimum && !passesThreshold)
            {
                continue;
            }
            if (outPlan.selectedClusterIndices.size() >= maxActiveClusters)
            {
                break;
            }
            if (maxActiveGuides > 0u && selectedGuideCount + candidate.guideCount > maxActiveGuides)
            {
                continue;
            }
            if (budget.maxActiveStrands > 0u && selectedStrandCount + candidate.strandCount > budget.maxActiveStrands)
            {
                continue;
            }

            outPlan.selectedClusterIndices.push_back(candidate.clusterIndex);
            selectedGuideCount += candidate.guideCount;
            selectedStrandCount += candidate.strandCount;
            selectedScoreSum += candidate.finalScore;
            lowestSelectedScore = candidate.finalScore;

            ActiveSetClusterScore& score = outPlan.scores[candidate.clusterIndex];
            score.selected = true;

            const ActiveSetCluster& cluster = clusters[candidate.clusterIndex];
            for (uint32_t guideIndex : cluster.guideStrandIndices)
            {
                outPlan.selectedGuideStrandIndices.push_back(guideIndex);
                if (guideIndex < outPlan.recommendedSimulationTiers.size())
                {
                    outPlan.recommendedSimulationTiers[guideIndex] = SimTier::Full;
                }
            }
            for (uint32_t strandIndex : cluster.strandIndices)
            {
                outPlan.selectedStrandIndices.push_back(strandIndex);
                if (strandIndex < outPlan.strandCorrectiveMask.size())
                {
                    outPlan.strandCorrectiveMask[strandIndex] = 1u;
                }
            }
        }

        std::sort(outPlan.selectedGuideStrandIndices.begin(), outPlan.selectedGuideStrandIndices.end());
        outPlan.selectedGuideStrandIndices.erase(std::unique(outPlan.selectedGuideStrandIndices.begin(), outPlan.selectedGuideStrandIndices.end()),
                                                 outPlan.selectedGuideStrandIndices.end());
        std::sort(outPlan.selectedStrandIndices.begin(), outPlan.selectedStrandIndices.end());
        outPlan.selectedStrandIndices.erase(std::unique(outPlan.selectedStrandIndices.begin(), outPlan.selectedStrandIndices.end()),
                                            outPlan.selectedStrandIndices.end());

        uint32_t fullTierCount = 0u;
        uint32_t reducedTierCount = 0u;
        uint32_t frozenTierCount = 0u;
        uint32_t cachedTierCount = 0u;
        for (size_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
        {
            const bool selected = (strandIndex < outPlan.strandCorrectiveMask.size()) && (outPlan.strandCorrectiveMask[strandIndex] != 0u);
            if (selected)
            {
                outPlan.recommendedSimulationTiers[strandIndex] = SimTier::Full;
            }
            else
            {
                const Vec3 root = ComputeGuideRestRoot(asset, static_cast<uint32_t>(std::min<size_t>(strandIndex, guideCount > 0u ? (guideCount - 1u) : 0u)));
                const float visibility = ComputeDistanceVisibility(root,
                                                                  runtimeInput.cameraPosition,
                                                                  runtimeInput.cameraForward,
                                                                  scoringSettings.nearDistance,
                                                                  scoringSettings.farDistance);
                const float distance = Length(root - runtimeInput.cameraPosition);
                if (distance > scoringSettings.farDistance * 1.20f && visibility < 0.18f)
                {
                    outPlan.recommendedSimulationTiers[strandIndex] = SimTier::Cached;
                }
                else if (distance > scoringSettings.farDistance && visibility < 0.35f)
                {
                    outPlan.recommendedSimulationTiers[strandIndex] = SimTier::Frozen;
                }
                else
                {
                    outPlan.recommendedSimulationTiers[strandIndex] = SimTier::Reduced;
                }
            }

            if (strandIndex < asset.followData.size())
            {
                const FollowData& follow = asset.followData[strandIndex];
                if (follow.parentGuide >= 0 && static_cast<size_t>(follow.parentGuide) < outPlan.recommendedSimulationTiers.size())
                {
                    const SimTier parentTier = outPlan.recommendedSimulationTiers[static_cast<size_t>(follow.parentGuide)];
                    if (parentTier == SimTier::Full && !selected)
                    {
                        outPlan.recommendedSimulationTiers[strandIndex] = SimTier::Reduced;
                    }
                    else if (parentTier == SimTier::Cached)
                    {
                        outPlan.recommendedSimulationTiers[strandIndex] = SimTier::Cached;
                    }
                }
            }

            switch (outPlan.recommendedSimulationTiers[strandIndex])
            {
            case SimTier::Full: ++fullTierCount; break;
            case SimTier::Frozen: ++frozenTierCount; break;
            case SimTier::Cached: ++cachedTierCount; break;
            case SimTier::Reduced:
            default: ++reducedTierCount; break;
            }
        }

        outPlan.summary.clusterCount = static_cast<uint32_t>(clusters.size());
        outPlan.summary.selectedClusterCount = static_cast<uint32_t>(outPlan.selectedClusterIndices.size());
        outPlan.summary.selectedGuideCount = static_cast<uint32_t>(outPlan.selectedGuideStrandIndices.size());
        outPlan.summary.selectedStrandCount = static_cast<uint32_t>(outPlan.selectedStrandIndices.size());
        outPlan.summary.highestScore = highestScore;
        outPlan.summary.lowestSelectedScore = (outPlan.selectedClusterIndices.empty() ? 0.0f : lowestSelectedScore);
        outPlan.summary.averageSelectedScore = outPlan.selectedClusterIndices.empty()
            ? 0.0f
            : (selectedScoreSum / static_cast<float>(outPlan.selectedClusterIndices.size()));
        outPlan.summary.budgetScale = budgetScale;
        outPlan.summary.recommendedSolverIterations = RecommendSolverIterations(budget, budgetScale);
        outPlan.summary.fullTierCount = fullTierCount;
        outPlan.summary.reducedTierCount = reducedTierCount;
        outPlan.summary.frozenTierCount = frozenTierCount;
        outPlan.summary.cachedTierCount = cachedTierCount;

        if (outPlan.selectedClusterIndices.empty())
        {
            outError = "No clusters satisfied the active-set budget and threshold. Raise the budget or lower the promote threshold.";
            return false;
        }

        return true;
    }

    std::string BuildActiveSetPlanSummary(const ActiveSetPlan& plan)
    {
        std::ostringstream out;
        out << "clusters=" << plan.summary.clusterCount
            << " selectedClusters=" << plan.summary.selectedClusterCount
            << " selectedGuides=" << plan.summary.selectedGuideCount
            << " selectedStrands=" << plan.summary.selectedStrandCount
            << " highestScore=" << plan.summary.highestScore
            << " averageSelectedScore=" << plan.summary.averageSelectedScore
            << " lowestSelectedScore=" << plan.summary.lowestSelectedScore
            << " budgetScale=" << plan.summary.budgetScale
            << " recommendedSolverIterations=" << plan.summary.recommendedSolverIterations
            << " tierCounts=" << plan.summary.fullTierCount << "/" << plan.summary.reducedTierCount << "/"
            << plan.summary.frozenTierCount << "/" << plan.summary.cachedTierCount;
        return out.str();
    }

    bool WriteActiveSetPlanJson(const std::filesystem::path& outputPath,
                                const HairAsset& asset,
                                const ActiveSetPlan& plan,
                                std::string& outError)
    {
        std::ofstream out(outputPath);
        if (!out)
        {
            outError = "Failed to open active-set JSON output file.";
            return false;
        }

        out << "{\n";
        out << "  \"summary\": {\n";
        out << "    \"clusterCount\": " << plan.summary.clusterCount << ",\n";
        out << "    \"selectedClusterCount\": " << plan.summary.selectedClusterCount << ",\n";
        out << "    \"selectedGuideCount\": " << plan.summary.selectedGuideCount << ",\n";
        out << "    \"selectedStrandCount\": " << plan.summary.selectedStrandCount << ",\n";
        out << "    \"highestScore\": " << plan.summary.highestScore << ",\n";
        out << "    \"averageSelectedScore\": " << plan.summary.averageSelectedScore << ",\n";
        out << "    \"lowestSelectedScore\": " << plan.summary.lowestSelectedScore << "\n";
        out << "  },\n";
        out << "  \"asset\": {\n";
        out << "    \"strandCount\": " << asset.strands.size() << ",\n";
        out << "    \"guideCount\": " << asset.guideStrandCount << ",\n";
        out << "    \"followerCount\": " << asset.followerStrandCount << "\n";
        out << "  },\n";
        out << "  \"selectedClusterIndices\": [";
        for (size_t i = 0u; i < plan.selectedClusterIndices.size(); ++i)
        {
            if (i > 0u) out << ", ";
            out << plan.selectedClusterIndices[i];
        }
        out << "],\n";
        out << "  \"clusters\": [\n";
        for (size_t clusterIndex = 0u; clusterIndex < plan.clusters.size(); ++clusterIndex)
        {
            const ActiveSetCluster& cluster = plan.clusters[clusterIndex];
            const ActiveSetClusterScore& score = (cluster.clusterIndex < plan.scores.size())
                ? plan.scores[cluster.clusterIndex]
                : ActiveSetClusterScore{};

            out << "    {\n";
            out << "      \"clusterIndex\": " << cluster.clusterIndex << ",\n";
            out << "      \"groupId\": " << cluster.groupId << ",\n";
            out << "      \"selected\": " << (score.selected ? "true" : "false") << ",\n";
            out << "      \"guideCount\": " << cluster.guideStrandIndices.size() << ",\n";
            out << "      \"strandCount\": " << cluster.strandIndices.size() << ",\n";
            out << "      \"restRootCentroid\": ["
                << cluster.restRootCentroid.x << ", "
                << cluster.restRootCentroid.y << ", "
                << cluster.restRootCentroid.z << "],\n";
            out << "      \"restRootRadius\": " << cluster.restRootRadius << ",\n";
            out << "      \"score\": {\n";
            out << "        \"motion\": " << score.motionScore << ",\n";
            out << "        \"displacement\": " << score.displacementScore << ",\n";
            out << "        \"visibility\": " << score.visibilityScore << ",\n";
            out << "        \"silhouette\": " << score.silhouetteScore << ",\n";
            out << "        \"uncertainty\": " << score.uncertaintyScore << ",\n";
            out << "        \"contact\": " << score.contactScore << ",\n";
            out << "        \"clusterSize\": " << score.clusterSizeScore << ",\n";
            out << "        \"final\": " << score.finalScore << "\n";
            out << "      },\n";
            out << "      \"guideStrands\": [";
            for (size_t i = 0u; i < cluster.guideStrandIndices.size(); ++i)
            {
                if (i > 0u) out << ", ";
                out << cluster.guideStrandIndices[i];
            }
            out << "],\n";
            out << "      \"strands\": [";
            for (size_t i = 0u; i < cluster.strandIndices.size(); ++i)
            {
                if (i > 0u) out << ", ";
                out << cluster.strandIndices[i];
            }
            out << "]\n";
            out << "    }" << ((clusterIndex + 1u < plan.clusters.size()) ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        outError.clear();
        return true;
    }
}
