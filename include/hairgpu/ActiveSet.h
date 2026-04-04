#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct ActiveSetClusterSettings
    {
        uint32_t targetGuideCountPerCluster = 8u;
        float maxGuideRootDistance = 0.085f;
        bool splitByGroupId = true;
        bool preserveGuideFamilies = true;
    };

    struct ActiveSetCluster
    {
        uint32_t clusterIndex = 0u;
        uint32_t groupId = 0u;
        Vec3 restRootCentroid = { 0.0f, 0.0f, 0.0f };
        float restRootRadius = 0.0f;
        std::vector<uint32_t> guideStrandIndices;
        std::vector<uint32_t> strandIndices;
    };

    struct ActiveSetRuntimeInput
    {
        std::vector<Vec3> currentGuideRoots;
        std::vector<Vec3> previousGuideRoots;
        std::vector<Vec3> prePreviousGuideRoots;
        std::vector<float> guideUncertainty;
        std::vector<float> guideContactScores;
        std::vector<float> guideExternalContactScores;
        std::vector<float> guideAccelerationScores;
        std::vector<float> guideStretchScores;
        std::vector<float> guideBendScores;
        std::vector<float> guideDensityScores;
        std::vector<float> guideVisibilityScores;
        Vec3 cameraPosition = { 0.0f, 1.6f, -1.8f };
        Vec3 cameraForward = { 0.0f, 0.0f, 1.0f };
        float fixedTimeStep = 1.0f / 60.0f;
    };

    struct ActiveSetScoringSettings
    {
        float motionWeight = 0.18f;
        float displacementWeight = 0.14f;
        float visibilityWeight = 0.10f;
        float silhouetteWeight = 0.10f;
        float uncertaintyWeight = 0.08f;
        float contactWeight = 0.10f;
        float accelerationWeight = 0.08f;
        float stretchWeight = 0.08f;
        float bendWeight = 0.06f;
        float densityWeight = 0.04f;
        float externalContactWeight = 0.02f;
        float clusterSizeWeight = 0.02f;
        float velocitySoftRange = 0.35f;
        float displacementSoftRange = 0.030f;
        float accelerationSoftRange = 8.0f;
        float nearDistance = 0.55f;
        float farDistance = 2.50f;
    };

    struct ActiveSetBudget
    {
        uint32_t maxActiveClusters = 6u;
        uint32_t maxActiveGuides = 64u;
        uint32_t maxActiveStrands = 0u;
        uint32_t minActiveClusters = 1u;
        float promoteScoreThreshold = 0.52f;
        bool enableBudgetAdaptation = true;
        float targetFrameMilliseconds = 8.0f;
        float previousFrameMilliseconds = 0.0f;
        float overloadRatio = 1.05f;
        float underloadRatio = 0.72f;
        float budgetStep = 0.18f;
        uint32_t minSolverIterations = 3u;
        uint32_t maxSolverIterations = 8u;
    };

    struct ActiveSetClusterScore
    {
        uint32_t clusterIndex = 0u;
        float motionScore = 0.0f;
        float displacementScore = 0.0f;
        float visibilityScore = 0.0f;
        float silhouetteScore = 0.0f;
        float uncertaintyScore = 0.0f;
        float contactScore = 0.0f;
        float accelerationScore = 0.0f;
        float stretchScore = 0.0f;
        float bendScore = 0.0f;
        float densityScore = 0.0f;
        float externalContactScore = 0.0f;
        float clusterSizeScore = 0.0f;
        float finalScore = 0.0f;
        bool selected = false;
    };

    struct ActiveSetPlanSummary
    {
        uint32_t clusterCount = 0u;
        uint32_t selectedClusterCount = 0u;
        uint32_t selectedGuideCount = 0u;
        uint32_t selectedStrandCount = 0u;
        float highestScore = 0.0f;
        float lowestSelectedScore = 0.0f;
        float averageSelectedScore = 0.0f;
        float budgetScale = 1.0f;
        uint32_t recommendedSolverIterations = 0u;
        uint32_t fullTierCount = 0u;
        uint32_t reducedTierCount = 0u;
        uint32_t frozenTierCount = 0u;
        uint32_t cachedTierCount = 0u;
    };

    struct ActiveSetPlan
    {
        std::vector<ActiveSetCluster> clusters;
        std::vector<ActiveSetClusterScore> scores;
        std::vector<uint32_t> selectedClusterIndices;
        std::vector<uint32_t> selectedGuideStrandIndices;
        std::vector<uint32_t> selectedStrandIndices;
        std::vector<uint8_t> strandCorrectiveMask;
        std::vector<SimTier> recommendedSimulationTiers;
        ActiveSetPlanSummary summary = {};
    };

    std::vector<ActiveSetCluster> BuildActiveSetClusters(const HairAsset& asset,
                                                         const ActiveSetClusterSettings& settings = {});

    bool EvaluateActiveSetPlan(const HairAsset& asset,
                               const std::vector<ActiveSetCluster>& clusters,
                               const ActiveSetRuntimeInput& runtimeInput,
                               const ActiveSetScoringSettings& scoringSettings,
                               const ActiveSetBudget& budget,
                               ActiveSetPlan& outPlan,
                               std::string& outError);

    std::string BuildActiveSetPlanSummary(const ActiveSetPlan& plan);

    bool WriteActiveSetPlanJson(const std::filesystem::path& outputPath,
                                const HairAsset& asset,
                                const ActiveSetPlan& plan,
                                std::string& outError);
}
