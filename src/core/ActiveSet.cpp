#include "hairgpu/ActiveSet.h"

#include <algorithm>
#include <array>
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

        constexpr std::array<char, 8> kDispatchPackMagic = { 'H','D','S','P','A','C','K','1' };
        constexpr uint32_t kDispatchPackVersion = 1u;

        template <typename T>
        bool WriteValue(std::ofstream& out, const T& value)
        {
            out.write(reinterpret_cast<const char*>(&value), sizeof(T));
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadValue(std::ifstream& in, T& value)
        {
            in.read(reinterpret_cast<char*>(&value), sizeof(T));
            return static_cast<bool>(in);
        }

        template <typename T>
        bool WriteVector(std::ofstream& out, const std::vector<T>& values)
        {
            const uint64_t count = static_cast<uint64_t>(values.size());
            if (!WriteValue(out, count))
            {
                return false;
            }
            if (!values.empty())
            {
                out.write(reinterpret_cast<const char*>(values.data()),
                          static_cast<std::streamsize>(sizeof(T) * values.size()));
            }
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadVector(std::ifstream& in, std::vector<T>& values)
        {
            uint64_t count = 0u;
            if (!ReadValue(in, count))
            {
                return false;
            }
            if (count > (1ull << 31))
            {
                return false;
            }
            values.resize(static_cast<size_t>(count));
            if (!values.empty())
            {
                in.read(reinterpret_cast<char*>(values.data()),
                        static_cast<std::streamsize>(sizeof(T) * values.size()));
            }
            return static_cast<bool>(in);
        }

        template <typename T>
        bool WriteRawVectorFile(const std::filesystem::path& filePath, const std::vector<T>& values)
        {
            std::ofstream out(filePath, std::ios::binary);
            if (!out)
            {
                return false;
            }
            if (!values.empty())
            {
                out.write(reinterpret_cast<const char*>(values.data()),
                          static_cast<std::streamsize>(sizeof(T) * values.size()));
            }
            return static_cast<bool>(out);
        }

        template <typename T, size_t N>
        bool WriteRawArrayFile(const std::filesystem::path& filePath, const std::array<T, N>& values)
        {
            std::ofstream out(filePath, std::ios::binary);
            if (!out)
            {
                return false;
            }
            if constexpr (N > 0)
            {
                out.write(reinterpret_cast<const char*>(values.data()),
                          static_cast<std::streamsize>(sizeof(T) * values.size()));
            }
            return static_cast<bool>(out);
        }

        DispatchIndirectArgs BuildDispatchArgs(uint32_t itemCount, uint32_t threadsPerGroup)
        {
            DispatchIndirectArgs args = {};
            args.threadGroupCountX = (threadsPerGroup > 0u)
                ? ((itemCount + threadsPerGroup - 1u) / threadsPerGroup)
                : 0u;
            args.threadGroupCountY = 1u;
            args.threadGroupCountZ = 1u;
            return args;
        }

        ActiveSetCompactedStrandRecord BuildStrandRecord(const HairAsset& asset, uint32_t strandIndex)
        {
            ActiveSetCompactedStrandRecord record = {};
            record.strandIndex = strandIndex;
            if (strandIndex < asset.strands.size())
            {
                const StrandRange& strand = asset.strands[strandIndex];
                record.pointOffset = strand.pointOffset;
                record.pointCount = strand.pointCount;
                record.groupId = strand.groupId;
                record.flags = strand.flags;
            }
            return record;
        }

        ActiveSetCompactedGuideRecord BuildGuideRecord(const HairAsset& asset, uint32_t strandIndex)
        {
            ActiveSetCompactedGuideRecord record = {};
            record.strandIndex = strandIndex;
            if (strandIndex < asset.strands.size())
            {
                const StrandRange& strand = asset.strands[strandIndex];
                record.pointOffset = strand.pointOffset;
                record.pointCount = strand.pointCount;
                record.groupId = strand.groupId;
            }
            return record;
        }

        void PopulateDispatchPackTier(const HairAsset& asset,
                                      const std::vector<uint32_t>& strandIndices,
                                      uint32_t threadsPerGroup,
                                      ActiveSetDispatchPackTier& outTier)
        {
            outTier = {};
            outTier.strands.reserve(strandIndices.size());
            for (uint32_t strandIndex : strandIndices)
            {
                const ActiveSetCompactedStrandRecord record = BuildStrandRecord(asset, strandIndex);
                outTier.strands.push_back(record);
                outTier.totalPointCount += record.pointCount;
            }
            outTier.strandDispatchArgs = BuildDispatchArgs(static_cast<uint32_t>(outTier.strands.size()), threadsPerGroup);
            outTier.pointDispatchArgs = BuildDispatchArgs(outTier.totalPointCount, threadsPerGroup);
        }


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
        if (currentGuideRoots.size() != guideCount)
        {
            currentGuideRoots = restGuideRoots;
        }
        if (previousGuideRoots.size() != guideCount)
        {
            previousGuideRoots = currentGuideRoots;
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
            float editScore = 0.0f;
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
            const float edit = (runtimeInput.guideEditScores.size() == guideCount)
                ? Clamp(AverageScalar(runtimeInput.guideEditScores, cluster.guideStrandIndices), 0.0f, 1.0f)
                : 0.0f;
            const float clusterSizeScore = Clamp(static_cast<float>(cluster.strandIndices.size()) / maxClusterStrands, 0.0f, 1.0f);

            const float finalScore = Clamp(
                motion * scoringSettings.motionWeight +
                displacement * scoringSettings.displacementWeight +
                visibility * scoringSettings.visibilityWeight +
                silhouette * scoringSettings.silhouetteWeight +
                uncertainty * scoringSettings.uncertaintyWeight +
                contact * scoringSettings.contactWeight +
                edit * scoringSettings.editWeight +
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
            score.editScore = edit;
            score.clusterSizeScore = clusterSizeScore;
            score.finalScore = finalScore;
            outPlan.scores.push_back(score);

            ranked.push_back({ cluster.clusterIndex,
                               finalScore,
                               visibility,
                               edit,
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
        const uint32_t configuredMinActiveClusters = budget.minActiveClusters;
        const uint32_t effectiveMaxActiveClusters = std::max<uint32_t>(configuredMinActiveClusters,
            static_cast<uint32_t>(std::round(static_cast<float>(std::max<uint32_t>(budget.maxActiveClusters, configuredMinActiveClusters)) * budgetScale)));
        uint32_t maxActiveClusters = std::min<uint32_t>(std::max<uint32_t>(configuredMinActiveClusters, budget.maxActiveClusters), effectiveMaxActiveClusters);
        uint32_t hardMaxActiveClusters = UINT32_MAX;
        if (budget.enableHardBudgetClamp)
        {
            const float availableBudget = budget.frameBudgetMilliseconds - budget.estimatedFixedCostMilliseconds;
            if (availableBudget <= 0.0f)
            {
                hardMaxActiveClusters = 0u;
            }
            else if (budget.estimatedClusterCostMilliseconds > 1.0e-4f)
            {
                hardMaxActiveClusters = static_cast<uint32_t>(std::floor(availableBudget / budget.estimatedClusterCostMilliseconds));
            }
        }
        if (hardMaxActiveClusters != UINT32_MAX)
        {
            maxActiveClusters = std::min<uint32_t>(maxActiveClusters, hardMaxActiveClusters);
        }
        const uint32_t minActiveClusters = (hardMaxActiveClusters == 0u) ? 0u : configuredMinActiveClusters;
        const uint32_t maxActiveGuides = (budget.maxActiveGuides > 0u)
            ? ((minActiveClusters > 0u)
                ? std::max<uint32_t>(minActiveClusters, static_cast<uint32_t>(std::round(static_cast<float>(budget.maxActiveGuides) * budgetScale)))
                : static_cast<uint32_t>(std::round(static_cast<float>(budget.maxActiveGuides) * budgetScale)))
            : 0u;

        for (size_t rankedIndex = 0u; rankedIndex < ranked.size(); ++rankedIndex)
        {
            const RankedCluster& candidate = ranked[rankedIndex];
            if (candidate.clusterIndex >= clusters.size())
            {
                continue;
            }

            const bool requiredByMinimum = rankedIndex < minActiveClusters;
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
        outPlan.summary.pendingClusterCount = 0u;
        for (const RankedCluster& candidate : ranked)
        {
            const bool passesThreshold = candidate.finalScore >= budget.promoteScoreThreshold;
            const bool selected = std::find(outPlan.selectedClusterIndices.begin(), outPlan.selectedClusterIndices.end(), candidate.clusterIndex) != outPlan.selectedClusterIndices.end();
            if (passesThreshold && !selected)
            {
                ++outPlan.summary.pendingClusterCount;
            }
        }
        outPlan.summary.hardMaxActiveClusters = (hardMaxActiveClusters == UINT32_MAX) ? maxActiveClusters : hardMaxActiveClusters;
        outPlan.summary.estimatedFixedCostMilliseconds = budget.estimatedFixedCostMilliseconds;
        outPlan.summary.estimatedClusterCostMilliseconds = budget.estimatedClusterCostMilliseconds;
        outPlan.summary.estimatedFrameMilliseconds = budget.estimatedFixedCostMilliseconds +
            static_cast<float>(outPlan.summary.selectedClusterCount) * budget.estimatedClusterCostMilliseconds;
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
            << " pendingClusters=" << plan.summary.pendingClusterCount
            << " hardMaxClusters=" << plan.summary.hardMaxActiveClusters
            << " estFrameMs=" << plan.summary.estimatedFrameMilliseconds
            << " recommendedSolverIterations=" << plan.summary.recommendedSolverIterations
            << " tierCounts=" << plan.summary.fullTierCount << "/" << plan.summary.reducedTierCount << "/"
            << plan.summary.frozenTierCount << "/" << plan.summary.cachedTierCount;
        return out.str();
    }

    bool BuildActiveSetDispatchPlan(const HairAsset& asset,
                                    const ActiveSetPlan& plan,
                                    ActiveSetDispatchPlan& outDispatchPlan,
                                    std::string& outError)
    {
        outError.clear();
        outDispatchPlan = {};
        if (plan.clusters.empty())
        {
            outError = "Active-set dispatch plan requires at least one cluster.";
            return false;
        }

        outDispatchPlan.correctiveStrands = plan.selectedStrandIndices;

        for (uint32_t selectedClusterIndex : plan.selectedClusterIndices)
        {
            if (selectedClusterIndex >= plan.clusters.size())
            {
                continue;
            }
            const ActiveSetCluster& cluster = plan.clusters[selectedClusterIndex];
            ActiveSetDispatchRange range = {};
            range.clusterIndex = selectedClusterIndex;
            range.guideOffset = static_cast<uint32_t>(outDispatchPlan.fullSimulationGuides.size());
            range.guideCount = static_cast<uint32_t>(cluster.guideStrandIndices.size());
            range.strandOffset = static_cast<uint32_t>(outDispatchPlan.fullSimulationStrands.size());
            range.strandCount = static_cast<uint32_t>(cluster.strandIndices.size());
            outDispatchPlan.fullSimulationClusters.push_back(range);
            outDispatchPlan.fullSimulationGuides.insert(outDispatchPlan.fullSimulationGuides.end(),
                                                        cluster.guideStrandIndices.begin(),
                                                        cluster.guideStrandIndices.end());
            outDispatchPlan.fullSimulationStrands.insert(outDispatchPlan.fullSimulationStrands.end(),
                                                         cluster.strandIndices.begin(),
                                                         cluster.strandIndices.end());
        }

        std::sort(outDispatchPlan.correctiveStrands.begin(), outDispatchPlan.correctiveStrands.end());
        outDispatchPlan.correctiveStrands.erase(std::unique(outDispatchPlan.correctiveStrands.begin(),
                                                            outDispatchPlan.correctiveStrands.end()),
                                                outDispatchPlan.correctiveStrands.end());

        std::sort(outDispatchPlan.fullSimulationGuides.begin(), outDispatchPlan.fullSimulationGuides.end());
        outDispatchPlan.fullSimulationGuides.erase(std::unique(outDispatchPlan.fullSimulationGuides.begin(),
                                                               outDispatchPlan.fullSimulationGuides.end()),
                                                   outDispatchPlan.fullSimulationGuides.end());

        for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size() && strandIndex < plan.recommendedSimulationTiers.size(); ++strandIndex)
        {
            switch (plan.recommendedSimulationTiers[strandIndex])
            {
            case SimTier::Full:
                break;
            case SimTier::Frozen:
                outDispatchPlan.frozenStrands.push_back(strandIndex);
                break;
            case SimTier::Cached:
                outDispatchPlan.cachedStrands.push_back(strandIndex);
                break;
            case SimTier::Reduced:
            default:
                outDispatchPlan.reducedSimulationStrands.push_back(strandIndex);
                break;
            }
        }
        return true;
    }

    bool BuildActiveSetDispatchPack(const HairAsset& asset,
                                    const ActiveSetPlan& plan,
                                    const ActiveSetDispatchPlan& dispatchPlan,
                                    ActiveSetDispatchPack& outDispatchPack,
                                    std::string& outError)
    {
        outDispatchPack = {};
        outDispatchPack.threadsPerGroup = 64u;
        outDispatchPack.fullSimulationClusters = dispatchPlan.fullSimulationClusters;

        outDispatchPack.fullSimulationGuides.reserve(dispatchPlan.fullSimulationGuides.size());
        for (uint32_t guideIndex : dispatchPlan.fullSimulationGuides)
        {
            const ActiveSetCompactedGuideRecord record = BuildGuideRecord(asset, guideIndex);
            outDispatchPack.fullSimulationGuides.push_back(record);
            outDispatchPack.totalGuidePointCount += record.pointCount;
        }
        outDispatchPack.guideDispatchArgs = BuildDispatchArgs(static_cast<uint32_t>(outDispatchPack.fullSimulationGuides.size()),
                                                             outDispatchPack.threadsPerGroup);
        outDispatchPack.guidePointDispatchArgs = BuildDispatchArgs(outDispatchPack.totalGuidePointCount,
                                                                  outDispatchPack.threadsPerGroup);

        PopulateDispatchPackTier(asset,
                                 dispatchPlan.fullSimulationStrands,
                                 outDispatchPack.threadsPerGroup,
                                 outDispatchPack.fullSimulation);
        PopulateDispatchPackTier(asset,
                                 dispatchPlan.correctiveStrands,
                                 outDispatchPack.threadsPerGroup,
                                 outDispatchPack.corrective);
        PopulateDispatchPackTier(asset,
                                 dispatchPlan.reducedSimulationStrands,
                                 outDispatchPack.threadsPerGroup,
                                 outDispatchPack.reducedSimulation);
        PopulateDispatchPackTier(asset,
                                 dispatchPlan.frozenStrands,
                                 outDispatchPack.threadsPerGroup,
                                 outDispatchPack.frozen);
        PopulateDispatchPackTier(asset,
                                 dispatchPlan.cachedStrands,
                                 outDispatchPack.threadsPerGroup,
                                 outDispatchPack.cached);

        outError.clear();
        return true;
    }

    bool WriteActiveSetDispatchPackJson(const std::filesystem::path& outputPath,
                                        const HairAsset& asset,
                                        const ActiveSetPlan& plan,
                                        const ActiveSetDispatchPack& dispatchPack,
                                        std::string& outError)
    {
        std::ofstream out(outputPath);
        if (!out)
        {
            outError = "Failed to open active-set dispatch pack JSON output file.";
            return false;
        }

        auto writeArgs = [&](const char* name, const DispatchIndirectArgs& args)
        {
            out << "  \"" << name << "\": {"
                << "\"x\": " << args.threadGroupCountX
                << ", \"y\": " << args.threadGroupCountY
                << ", \"z\": " << args.threadGroupCountZ
                << "}";
        };

        auto writeTier = [&](const char* name, const ActiveSetDispatchPackTier& tier, bool trailingComma)
        {
            out << "  \"" << name << "\": {\n";
            out << "    \"strandCount\": " << tier.strands.size() << ",\n";
            out << "    \"totalPointCount\": " << tier.totalPointCount << ",\n";
            out << "    \"strandDispatchArgs\": {\"x\": " << tier.strandDispatchArgs.threadGroupCountX
                << ", \"y\": " << tier.strandDispatchArgs.threadGroupCountY
                << ", \"z\": " << tier.strandDispatchArgs.threadGroupCountZ << "},\n";
            out << "    \"pointDispatchArgs\": {\"x\": " << tier.pointDispatchArgs.threadGroupCountX
                << ", \"y\": " << tier.pointDispatchArgs.threadGroupCountY
                << ", \"z\": " << tier.pointDispatchArgs.threadGroupCountZ << "},\n";
            out << "    \"strands\": [\n";
            for (size_t i = 0; i < tier.strands.size(); ++i)
            {
                const auto& s = tier.strands[i];
                out << "      {\"strandIndex\": " << s.strandIndex
                    << ", \"pointOffset\": " << s.pointOffset
                    << ", \"pointCount\": " << s.pointCount
                    << ", \"groupId\": " << s.groupId
                    << ", \"flags\": " << s.flags << "}";
                out << ((i + 1u < tier.strands.size()) ? "," : "") << "\n";
            }
            out << "    ]\n";
            out << "  }" << (trailingComma ? "," : "") << "\n";
        };

        out << "{\n";
        out << "  \"asset\": {\n";
        out << "    \"strandCount\": " << asset.strands.size() << ",\n";
        out << "    \"guideCount\": " << asset.guideStrandCount << ",\n";
        out << "    \"followerCount\": " << asset.followerStrandCount << "\n";
        out << "  },\n";
        out << "  \"summary\": {\n";
        out << "    \"selectedClusterCount\": " << plan.summary.selectedClusterCount << ",\n";
        out << "    \"selectedGuideCount\": " << plan.summary.selectedGuideCount << ",\n";
        out << "    \"selectedStrandCount\": " << plan.summary.selectedStrandCount << ",\n";
        out << "    \"recommendedSolverIterations\": " << plan.summary.recommendedSolverIterations << "\n";
        out << "  },\n";
        out << "  \"threadsPerGroup\": " << dispatchPack.threadsPerGroup << ",\n";
        out << "  \"totalGuidePointCount\": " << dispatchPack.totalGuidePointCount << ",\n";
        writeArgs("guideDispatchArgs", dispatchPack.guideDispatchArgs); out << ",\n";
        writeArgs("guidePointDispatchArgs", dispatchPack.guidePointDispatchArgs); out << ",\n";

        out << "  \"fullSimulationClusters\": [\n";
        for (size_t i = 0; i < dispatchPack.fullSimulationClusters.size(); ++i)
        {
            const auto& range = dispatchPack.fullSimulationClusters[i];
            out << "    {\"clusterIndex\": " << range.clusterIndex
                << ", \"guideOffset\": " << range.guideOffset
                << ", \"guideCount\": " << range.guideCount
                << ", \"strandOffset\": " << range.strandOffset
                << ", \"strandCount\": " << range.strandCount << "}";
            out << ((i + 1u < dispatchPack.fullSimulationClusters.size()) ? "," : "") << "\n";
        }
        out << "  ],\n";

        out << "  \"fullSimulationGuides\": [\n";
        for (size_t i = 0; i < dispatchPack.fullSimulationGuides.size(); ++i)
        {
            const auto& g = dispatchPack.fullSimulationGuides[i];
            out << "    {\"strandIndex\": " << g.strandIndex
                << ", \"pointOffset\": " << g.pointOffset
                << ", \"pointCount\": " << g.pointCount
                << ", \"groupId\": " << g.groupId << "}";
            out << ((i + 1u < dispatchPack.fullSimulationGuides.size()) ? "," : "") << "\n";
        }
        out << "  ],\n";

        writeTier("fullSimulation", dispatchPack.fullSimulation, true);
        writeTier("corrective", dispatchPack.corrective, true);
        writeTier("reducedSimulation", dispatchPack.reducedSimulation, true);
        writeTier("frozen", dispatchPack.frozen, true);
        writeTier("cached", dispatchPack.cached, false);
        out << "}\n";
        outError.clear();
        return true;
    }

    bool SaveActiveSetDispatchPackToFile(const std::filesystem::path& filePath,
                                         const ActiveSetDispatchPack& dispatchPack,
                                         std::string& outError)
    {
        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open active-set dispatch pack file.";
            return false;
        }

        out.write(kDispatchPackMagic.data(), static_cast<std::streamsize>(kDispatchPackMagic.size()));
        auto writeTier = [&](const ActiveSetDispatchPackTier& tier) -> bool
        {
            return WriteValue(out, tier.totalPointCount) &&
                   WriteValue(out, tier.strandDispatchArgs) &&
                   WriteValue(out, tier.pointDispatchArgs) &&
                   WriteVector(out, tier.strands);
        };

        if (!out ||
            !WriteValue(out, kDispatchPackVersion) ||
            !WriteValue(out, dispatchPack.threadsPerGroup) ||
            !WriteVector(out, dispatchPack.fullSimulationClusters) ||
            !WriteVector(out, dispatchPack.fullSimulationGuides) ||
            !WriteValue(out, dispatchPack.totalGuidePointCount) ||
            !WriteValue(out, dispatchPack.guideDispatchArgs) ||
            !WriteValue(out, dispatchPack.guidePointDispatchArgs) ||
            !writeTier(dispatchPack.fullSimulation) ||
            !writeTier(dispatchPack.corrective) ||
            !writeTier(dispatchPack.reducedSimulation) ||
            !writeTier(dispatchPack.frozen) ||
            !writeTier(dispatchPack.cached))
        {
            outError = "Failed to write active-set dispatch pack.";
            return false;
        }

        outError.clear();
        return true;
    }

    bool SaveActiveSetDispatchPackGpuBuffers(const std::filesystem::path& outputDirectory,
                                             const ActiveSetDispatchPack& dispatchPack,
                                             std::string& outError)
    {
        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create dispatch GPU buffer output directory.";
            return false;
        }

        struct TierFileDesc
        {
            const char* name = nullptr;
            const ActiveSetDispatchPackTier* tier = nullptr;
        };

        const std::array<DispatchIndirectArgs, 12> indirectArgs =
        {{
            dispatchPack.guideDispatchArgs,
            dispatchPack.guidePointDispatchArgs,
            dispatchPack.fullSimulation.strandDispatchArgs,
            dispatchPack.fullSimulation.pointDispatchArgs,
            dispatchPack.corrective.strandDispatchArgs,
            dispatchPack.corrective.pointDispatchArgs,
            dispatchPack.reducedSimulation.strandDispatchArgs,
            dispatchPack.reducedSimulation.pointDispatchArgs,
            dispatchPack.frozen.strandDispatchArgs,
            dispatchPack.frozen.pointDispatchArgs,
            dispatchPack.cached.strandDispatchArgs,
            dispatchPack.cached.pointDispatchArgs
        }};

        if (!WriteRawVectorFile(outputDirectory / "full_simulation_clusters.bin", dispatchPack.fullSimulationClusters) ||
            !WriteRawVectorFile(outputDirectory / "full_simulation_guides.bin", dispatchPack.fullSimulationGuides) ||
            !WriteRawArrayFile(outputDirectory / "dispatch_indirect_args.bin", indirectArgs))
        {
            outError = "Failed to write dispatch GPU buffer files.";
            return false;
        }

        const std::array<TierFileDesc, 5> tiers =
        {{
            { "full_simulation_strands.bin", &dispatchPack.fullSimulation },
            { "corrective_strands.bin", &dispatchPack.corrective },
            { "reduced_simulation_strands.bin", &dispatchPack.reducedSimulation },
            { "frozen_strands.bin", &dispatchPack.frozen },
            { "cached_strands.bin", &dispatchPack.cached }
        }};
        for (const TierFileDesc& tierDesc : tiers)
        {
            if (!WriteRawVectorFile(outputDirectory / tierDesc.name, tierDesc.tier->strands))
            {
                outError = "Failed to write tier GPU buffer file.";
                return false;
            }
        }

        std::ofstream manifest(outputDirectory / "dispatch_gpu_manifest.json");
        if (!manifest)
        {
            outError = "Failed to write dispatch GPU buffer manifest.";
            return false;
        }

        auto writeTierMeta = [&](const char* name, const char* fileName, const ActiveSetDispatchPackTier& tier, bool trailingComma)
        {
            manifest << "    \"" << name << "\": {\n";
            manifest << "      \"file\": \"" << fileName << "\",\n";
            manifest << "      \"recordStrideBytes\": " << sizeof(ActiveSetCompactedStrandRecord) << ",\n";
            manifest << "      \"recordCount\": " << tier.strands.size() << ",\n";
            manifest << "      \"totalPointCount\": " << tier.totalPointCount << ",\n";
            manifest << "      \"strandDispatchArgs\": {\"x\": " << tier.strandDispatchArgs.threadGroupCountX
                     << ", \"y\": " << tier.strandDispatchArgs.threadGroupCountY
                     << ", \"z\": " << tier.strandDispatchArgs.threadGroupCountZ << "},\n";
            manifest << "      \"pointDispatchArgs\": {\"x\": " << tier.pointDispatchArgs.threadGroupCountX
                     << ", \"y\": " << tier.pointDispatchArgs.threadGroupCountY
                     << ", \"z\": " << tier.pointDispatchArgs.threadGroupCountZ << "}\n";
            manifest << "    }" << (trailingComma ? "," : "") << "\n";
        };

        manifest << "{\n";
        manifest << "  \"threadsPerGroup\": " << dispatchPack.threadsPerGroup << ",\n";
        manifest << "  \"clusterRanges\": {\n";
        manifest << "    \"file\": \"full_simulation_clusters.bin\",\n";
        manifest << "    \"recordStrideBytes\": " << sizeof(ActiveSetDispatchRange) << ",\n";
        manifest << "    \"recordCount\": " << dispatchPack.fullSimulationClusters.size() << "\n";
        manifest << "  },\n";
        manifest << "  \"guideRecords\": {\n";
        manifest << "    \"file\": \"full_simulation_guides.bin\",\n";
        manifest << "    \"recordStrideBytes\": " << sizeof(ActiveSetCompactedGuideRecord) << ",\n";
        manifest << "    \"recordCount\": " << dispatchPack.fullSimulationGuides.size() << ",\n";
        manifest << "    \"totalPointCount\": " << dispatchPack.totalGuidePointCount << ",\n";
        manifest << "    \"guideDispatchArgs\": {\"x\": " << dispatchPack.guideDispatchArgs.threadGroupCountX
                 << ", \"y\": " << dispatchPack.guideDispatchArgs.threadGroupCountY
                 << ", \"z\": " << dispatchPack.guideDispatchArgs.threadGroupCountZ << "},\n";
        manifest << "    \"guidePointDispatchArgs\": {\"x\": " << dispatchPack.guidePointDispatchArgs.threadGroupCountX
                 << ", \"y\": " << dispatchPack.guidePointDispatchArgs.threadGroupCountY
                 << ", \"z\": " << dispatchPack.guidePointDispatchArgs.threadGroupCountZ << "}\n";
        manifest << "  },\n";
        manifest << "  \"indirectArgs\": {\n";
        manifest << "    \"file\": \"dispatch_indirect_args.bin\",\n";
        manifest << "    \"recordStrideBytes\": " << sizeof(DispatchIndirectArgs) << ",\n";
        manifest << "    \"recordCount\": " << indirectArgs.size() << ",\n";
        manifest << "    \"layout\": [\n";
        const std::array<const char*, 12> argNames =
        {{
            "guideDispatchArgs",
            "guidePointDispatchArgs",
            "fullSimulationStrandDispatchArgs",
            "fullSimulationPointDispatchArgs",
            "correctiveStrandDispatchArgs",
            "correctivePointDispatchArgs",
            "reducedSimulationStrandDispatchArgs",
            "reducedSimulationPointDispatchArgs",
            "frozenStrandDispatchArgs",
            "frozenPointDispatchArgs",
            "cachedStrandDispatchArgs",
            "cachedPointDispatchArgs"
        }};
        for (size_t i = 0; i < argNames.size(); ++i)
        {
            manifest << "      \"" << argNames[i] << "\"" << (i + 1u < argNames.size() ? "," : "") << "\n";
        }
        manifest << "    ]\n";
        manifest << "  },\n";
        manifest << "  \"tiers\": {\n";
        writeTierMeta("fullSimulation", "full_simulation_strands.bin", dispatchPack.fullSimulation, true);
        writeTierMeta("corrective", "corrective_strands.bin", dispatchPack.corrective, true);
        writeTierMeta("reducedSimulation", "reduced_simulation_strands.bin", dispatchPack.reducedSimulation, true);
        writeTierMeta("frozen", "frozen_strands.bin", dispatchPack.frozen, true);
        writeTierMeta("cached", "cached_strands.bin", dispatchPack.cached, false);
        manifest << "  }\n";
        manifest << "}\n";

        outError.clear();
        return true;
    }

    bool LoadActiveSetDispatchPackFromFile(const std::filesystem::path& filePath,
                                           ActiveSetDispatchPack& outDispatchPack,
                                           std::string& outError)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open active-set dispatch pack file.";
            return false;
        }

        std::array<char, 8> magic = {};
        in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!in || magic != kDispatchPackMagic)
        {
            outError = "Invalid active-set dispatch pack magic.";
            return false;
        }

        uint32_t version = 0u;
        ActiveSetDispatchPack pack = {};
        auto readTier = [&](ActiveSetDispatchPackTier& tier) -> bool
        {
            return ReadValue(in, tier.totalPointCount) &&
                   ReadValue(in, tier.strandDispatchArgs) &&
                   ReadValue(in, tier.pointDispatchArgs) &&
                   ReadVector(in, tier.strands);
        };

        if (!ReadValue(in, version) ||
            version != kDispatchPackVersion ||
            !ReadValue(in, pack.threadsPerGroup) ||
            !ReadVector(in, pack.fullSimulationClusters) ||
            !ReadVector(in, pack.fullSimulationGuides) ||
            !ReadValue(in, pack.totalGuidePointCount) ||
            !ReadValue(in, pack.guideDispatchArgs) ||
            !ReadValue(in, pack.guidePointDispatchArgs) ||
            !readTier(pack.fullSimulation) ||
            !readTier(pack.corrective) ||
            !readTier(pack.reducedSimulation) ||
            !readTier(pack.frozen) ||
            !readTier(pack.cached))
        {
            outError = "Active-set dispatch pack is truncated or unsupported.";
            return false;
        }

        outDispatchPack = std::move(pack);
        outError.clear();
        return true;
    }

    std::string BuildActiveSetDispatchPackSummary(const ActiveSetDispatchPack& dispatchPack)
    {
        std::ostringstream out;
        out << "threadsPerGroup=" << dispatchPack.threadsPerGroup
            << " guides=" << dispatchPack.fullSimulationGuides.size()
            << " guidePoints=" << dispatchPack.totalGuidePointCount
            << " full=" << dispatchPack.fullSimulation.strands.size()
            << " reduced=" << dispatchPack.reducedSimulation.strands.size()
            << " frozen=" << dispatchPack.frozen.strands.size()
            << " cached=" << dispatchPack.cached.strands.size()
            << " corrective=" << dispatchPack.corrective.strands.size()
            << " fullDispatchGroups=" << dispatchPack.fullSimulation.strandDispatchArgs.threadGroupCountX
            << " correctiveGroups=" << dispatchPack.corrective.strandDispatchArgs.threadGroupCountX;
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

    bool WriteActiveSetDispatchPlanJson(const std::filesystem::path& outputPath,
                                        const HairAsset& asset,
                                        const ActiveSetPlan& plan,
                                        const ActiveSetDispatchPlan& dispatchPlan,
                                        std::string& outError)
    {
        std::ofstream out(outputPath);
        if (!out)
        {
            outError = "Failed to open active-set dispatch JSON output file.";
            return false;
        }

        out << "{\n";
        out << "  \"asset\": {\n";
        out << "    \"strandCount\": " << asset.strands.size() << ",\n";
        out << "    \"guideCount\": " << asset.guideStrandCount << ",\n";
        out << "    \"followerCount\": " << asset.followerStrandCount << "\n";
        out << "  },\n";
        out << "  \"summary\": {\n";
        out << "    \"selectedClusterCount\": " << plan.summary.selectedClusterCount << ",\n";
        out << "    \"selectedGuideCount\": " << plan.summary.selectedGuideCount << ",\n";
        out << "    \"selectedStrandCount\": " << plan.summary.selectedStrandCount << ",\n";
        out << "    \"recommendedSolverIterations\": " << plan.summary.recommendedSolverIterations << "\n";
        out << "  },\n";

        out << "  \"fullSimulationClusters\": [\n";
        for (size_t i = 0; i < dispatchPlan.fullSimulationClusters.size(); ++i)
        {
            const ActiveSetDispatchRange& range = dispatchPlan.fullSimulationClusters[i];
            out << "    {\"clusterIndex\": " << range.clusterIndex
                << ", \"guideOffset\": " << range.guideOffset
                << ", \"guideCount\": " << range.guideCount
                << ", \"strandOffset\": " << range.strandOffset
                << ", \"strandCount\": " << range.strandCount << "}";
            out << ((i + 1 < dispatchPlan.fullSimulationClusters.size()) ? "," : "") << "\n";
        }
        out << "  ],\n";

        auto writeArray = [&](const char* name, const std::vector<uint32_t>& values)
        {
            out << "  \"" << name << "\": [";
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0) out << ", ";
                out << values[i];
            }
            out << "]";
        };

        writeArray("fullSimulationGuides", dispatchPlan.fullSimulationGuides); out << ",\n";
        writeArray("fullSimulationStrands", dispatchPlan.fullSimulationStrands); out << ",\n";
        writeArray("correctiveStrands", dispatchPlan.correctiveStrands); out << ",\n";
        writeArray("reducedSimulationStrands", dispatchPlan.reducedSimulationStrands); out << ",\n";
        writeArray("frozenStrands", dispatchPlan.frozenStrands); out << ",\n";
        writeArray("cachedStrands", dispatchPlan.cachedStrands); out << "\n";
        out << "}\n";

        outError.clear();
        return true;
    }
}
