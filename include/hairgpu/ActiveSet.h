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
        std::vector<float> guideUncertainty;
        std::vector<float> guideContactScores;
        std::vector<float> guidePenetrationScores;
        std::vector<float> guideVisibilityScores;
        std::vector<float> guideEditScores;
        std::vector<float> guideBoundaryScores;
        std::vector<float> guideTemporalScores;
        std::vector<float> guideLeakageScores;
        std::vector<float> guideModelErrorScores;
        std::vector<float> guideWindExposureScores;
        std::vector<float> guideEnvironmentComplexityScores;
        Vec3 cameraPosition = { 0.0f, 1.6f, -1.8f };
        Vec3 cameraForward = { 0.0f, 0.0f, 1.0f };
        float fixedTimeStep = 1.0f / 60.0f;
    };

    struct ActiveSetScoringSettings
    {
        float motionWeight = 0.10f;
        float displacementWeight = 0.08f;
        float visibilityWeight = 0.05f;
        float silhouetteWeight = 0.07f;
        float uncertaintyWeight = 0.08f;
        float contactWeight = 0.12f;
        float editWeight = 0.20f;
        float clusterSizeWeight = 0.03f;
        float penetrationWeight = 0.18f;
        float boundaryWeight = 0.14f;
        float temporalWeight = 0.09f;
        float leakageWeight = 0.08f;
        float modelErrorWeight = 0.10f;
        float windWeight = 0.09f;
        float environmentWeight = 0.07f;
        float naturalnessGainWeight = 0.70f;
        float efficiencyWeight = 0.30f;
        float velocitySoftRange = 0.35f;
        float displacementSoftRange = 0.030f;
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
        bool enableHardBudgetClamp = true;
        float frameBudgetMilliseconds = 16.67f;
        float estimatedFixedCostMilliseconds = 6.0f;
        float estimatedClusterCostMilliseconds = 0.85f;
        float memoryBudgetMegabytes = 32768.0f;
        float estimatedFixedMemoryMegabytes = 512.0f;
        float estimatedClusterMemoryMegabytes = 12.0f;
        bool enforceEditCoverage = true;
        float mandatoryEditThreshold = 0.55f;
        bool enableSegmentAdaptation = true;
        uint32_t standardParticlesPerStrand = kStandardParticlesPerStrand;
        uint32_t maxParticlesPerStrand = kIdealParticlesPerStrand;
        uint32_t maxRefinementLevel = 3u;
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
        float penetrationScore = 0.0f;
        float boundaryScore = 0.0f;
        float temporalScore = 0.0f;
        float leakageScore = 0.0f;
        float modelErrorScore = 0.0f;
        float windExposureScore = 0.0f;
        float environmentComplexityScore = 0.0f;
        float editScore = 0.0f;
        float clusterSizeScore = 0.0f;
        float naturalnessGain = 0.0f;
        float estimatedTimeCostMilliseconds = 0.0f;
        float estimatedMemoryCostMegabytes = 0.0f;
        float efficiencyScore = 0.0f;
        float finalScore = 0.0f;
        bool mandatoryEditCluster = false;
        uint32_t refinementLevel = 0u;
        uint32_t selectedParticlesPerStrand = 0u;
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
        float selectedNaturalnessGain = 0.0f;
        float averageEfficiencyScore = 0.0f;
        float editCoverageRatio = 0.0f;
        uint32_t mandatoryClusterCount = 0u;
        uint32_t selectedMandatoryClusterCount = 0u;
        float budgetScale = 1.0f;
        uint32_t pendingClusterCount = 0u;
        uint32_t hardMaxActiveClusters = 0u;
        float estimatedFixedCostMilliseconds = 0.0f;
        float estimatedClusterCostMilliseconds = 0.0f;
        float estimatedFrameMilliseconds = 0.0f;
        float estimatedMemoryMegabytes = 0.0f;
        float estimatedFixedMemoryMegabytes = 0.0f;
        float estimatedClusterMemoryMegabytes = 0.0f;
        float averageSelectedParticlesPerStrand = 0.0f;
        uint32_t maxSelectedParticlesPerStrand = 0u;
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
        std::vector<uint32_t> recommendedParticlesPerStrand;
        ActiveSetPlanSummary summary = {};
    };

    struct ActiveSetDispatchRange
    {
        uint32_t clusterIndex = UINT32_MAX;
        uint32_t guideOffset = 0u;
        uint32_t guideCount = 0u;
        uint32_t strandOffset = 0u;
        uint32_t strandCount = 0u;
    };


    struct DispatchIndirectArgs
    {
        uint32_t threadGroupCountX = 0u;
        uint32_t threadGroupCountY = 1u;
        uint32_t threadGroupCountZ = 1u;
    };

    struct ActiveSetCompactedStrandRecord
    {
        uint32_t strandIndex = UINT32_MAX;
        uint32_t pointOffset = 0u;
        uint32_t pointCount = 0u;
        uint32_t groupId = 0u;
        uint32_t flags = 0u;
    };

    struct ActiveSetCompactedGuideRecord
    {
        uint32_t strandIndex = UINT32_MAX;
        uint32_t pointOffset = 0u;
        uint32_t pointCount = 0u;
        uint32_t groupId = 0u;
    };

    struct ActiveSetDispatchPackTier
    {
        std::vector<ActiveSetCompactedStrandRecord> strands;
        uint32_t totalPointCount = 0u;
        DispatchIndirectArgs strandDispatchArgs = {};
        DispatchIndirectArgs pointDispatchArgs = {};
    };

    struct ActiveSetDispatchPack
    {
        uint32_t threadsPerGroup = 64u;
        std::vector<ActiveSetDispatchRange> fullSimulationClusters;
        std::vector<ActiveSetCompactedGuideRecord> fullSimulationGuides;
        uint32_t totalGuidePointCount = 0u;
        DispatchIndirectArgs guideDispatchArgs = {};
        DispatchIndirectArgs guidePointDispatchArgs = {};
        ActiveSetDispatchPackTier fullSimulation = {};
        ActiveSetDispatchPackTier corrective = {};
        ActiveSetDispatchPackTier reducedSimulation = {};
        ActiveSetDispatchPackTier frozen = {};
        ActiveSetDispatchPackTier cached = {};
    };


    struct ActiveSetDispatchPlan
    {
        std::vector<ActiveSetDispatchRange> fullSimulationClusters;
        std::vector<uint32_t> fullSimulationGuides;
        std::vector<uint32_t> fullSimulationStrands;
        std::vector<uint32_t> correctiveStrands;
        std::vector<uint32_t> reducedSimulationStrands;
        std::vector<uint32_t> frozenStrands;
        std::vector<uint32_t> cachedStrands;
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

    bool BuildActiveSetDispatchPlan(const HairAsset& asset,
                                    const ActiveSetPlan& plan,
                                    ActiveSetDispatchPlan& outDispatchPlan,
                                    std::string& outError);

    bool WriteActiveSetPlanJson(const std::filesystem::path& outputPath,
                                const HairAsset& asset,
                                const ActiveSetPlan& plan,
                                std::string& outError);

    bool WriteActiveSetDispatchPlanJson(const std::filesystem::path& outputPath,
                                        const HairAsset& asset,
                                        const ActiveSetPlan& plan,
                                        const ActiveSetDispatchPlan& dispatchPlan,
                                        std::string& outError);

    bool BuildActiveSetDispatchPack(const HairAsset& asset,
                                    const ActiveSetPlan& plan,
                                    const ActiveSetDispatchPlan& dispatchPlan,
                                    ActiveSetDispatchPack& outDispatchPack,
                                    std::string& outError);

    bool WriteActiveSetDispatchPackJson(const std::filesystem::path& outputPath,
                                        const HairAsset& asset,
                                        const ActiveSetPlan& plan,
                                        const ActiveSetDispatchPack& dispatchPack,
                                        std::string& outError);

    bool SaveActiveSetDispatchPackToFile(const std::filesystem::path& filePath,
                                         const ActiveSetDispatchPack& dispatchPack,
                                         std::string& outError);

    bool SaveActiveSetDispatchPackGpuBuffers(const std::filesystem::path& outputDirectory,
                                             const ActiveSetDispatchPack& dispatchPack,
                                             std::string& outError);

    bool LoadActiveSetDispatchPackFromFile(const std::filesystem::path& filePath,
                                           ActiveSetDispatchPack& outDispatchPack,
                                           std::string& outError);

    std::string BuildActiveSetDispatchPackSummary(const ActiveSetDispatchPack& dispatchPack);

}
