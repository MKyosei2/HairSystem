#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/ReducedModel.h"
#include "hairgpu/Types.h"

namespace hairgpu
{
    struct SparseCorrectiveSettings
    {
        float shellDistance = 0.0035f;
        float contactBlend = 0.85f;
        float stretchBlend = 0.42f;
        float bendBlend = 0.18f;
        uint32_t iterations = 2u;
        uint32_t preserveRootPointCount = 1u;
        bool affectFloor = true;
        bool affectExternalColliders = true;
    };

    struct SparseCorrectiveReport
    {
        uint32_t correctedStrandCount = 0u;
        uint32_t correctedPointCount = 0u;
        float averagePenetration = 0.0f;
        float maxPenetration = 0.0f;
        float averageLengthErrorBefore = 0.0f;
        float averageLengthErrorAfter = 0.0f;
    };

    bool ApplySparseCorrectivePhysics(const HairAsset& asset,
                                      const ActiveSetPlan& activeSetPlan,
                                      const ColliderSet& colliders,
                                      std::vector<Vec3>& inOutPoints,
                                      const SparseCorrectiveSettings& settings,
                                      SparseCorrectiveReport* outReport,
                                      std::string& outError);

    struct HybridTeacherDatasetSettings
    {
        uint32_t frameCount = 240u;
        float frameRate = 60.0f;
        float swayAmplitude = 0.035f;
        float tipNoiseAmplitude = 0.006f;
        float windCyclesPerSecond = 0.55f;
        ActiveSetClusterSettings clusterSettings = {};
        ActiveSetScoringSettings scoringSettings = {};
        ActiveSetBudget budget = {};
        SparseCorrectiveSettings correctiveSettings = {};
    };

    struct HybridTeacherDatasetReport
    {
        uint32_t frameCount = 0u;
        float averageSelectedClusterCount = 0.0f;
        float averageSelectedGuideCount = 0.0f;
        float averageSelectedStrandCount = 0.0f;
        float averagePenetration = 0.0f;
        float maxPenetration = 0.0f;
        float averageCorrectedPointCount = 0.0f;
        float peakEstimatedSimulationMilliseconds = 0.0f;
    };

    bool GenerateHybridTeacherDataset(const HairAsset& asset,
                                      const ColliderSet& colliders,
                                      const HybridTeacherDatasetSettings& settings,
                                      const std::filesystem::path& outputDirectory,
                                      HybridTeacherDatasetReport& outReport,
                                      std::string& outError);


    struct HybridModelTrainingSettings
    {
        uint32_t priorGuideCount = 12u;
        uint32_t residualGuideCount = 20u;
        uint32_t maxInfluencesPerFollower = 4u;
        float regularization = 1.0e-4f;
        uint32_t validationStride = 4u;
        float priorGuideSmoothingAlpha = 0.18f;
        float residualGuideGain = 1.0f;
        float residualBlend = 1.0f;
        bool enableScalpCollision = true;
    };

    struct HybridModelBundle
    {
        ReducedModel priorModel;
        ReducedModel residualModel;
        HybridModelTrainingSettings trainingSettings = {};
        ActiveSetClusterSettings clusterSettings = {};
        ActiveSetScoringSettings scoringSettings = {};
        ActiveSetBudget budget = {};
        SparseCorrectiveSettings correctiveSettings = {};
        float uncertaintyGain = 1.0f;
        float contactGain = 1.0f;
        float penetrationGain = 1.0f;
        float temporalCoherenceGain = 1.0f;
        float uncertaintySmoothingAlpha = 0.35f;
        float priorVelocityPredictionGain = 0.0f;
        float residualVelocityPredictionGain = 0.0f;
    };

    struct HybridModelTrainingReport
    {
        ReducedModelEvaluationReport priorEvaluation = {};
        ReducedModelEvaluationReport residualEvaluation = {};
        float averageResidualMagnitude = 0.0f;
        float maxResidualMagnitude = 0.0f;
    };

    struct FullHybridRuntimeSettings
    {
        float frameRateOverride = 0.0f;
        float residualBlendOverride = -1.0f;
        ActiveSetBudget budgetOverride = {};
        bool useBudgetOverride = false;
        bool exportPerFrameHairBins = false;
    };

    struct FullHybridRuntimeReport
    {
        uint32_t frameCount = 0u;
        float averagePriorRmsError = 0.0f;
        float averageHybridRmsErrorBeforeCorrection = 0.0f;
        float averageHybridRmsErrorAfterCorrection = 0.0f;
        float maxHybridRmsErrorAfterCorrection = 0.0f;
        float averageSelectedClusterCount = 0.0f;
        float averageSelectedGuideCount = 0.0f;
        float averageCorrectedPointCount = 0.0f;
        float averagePenetration = 0.0f;
        float maxPenetration = 0.0f;
    };

    bool LoadHybridTeacherSequenceFromDirectory(const std::filesystem::path& directoryPath,
                                                ReducedModelTrainingSequence& outSequence,
                                                std::string& outError);
    bool TrainHybridModelBundle(const ReducedModelTrainingSequence& teacherSequence,
                                const ColliderSet& colliders,
                                const HybridModelTrainingSettings& settings,
                                HybridModelBundle& outBundle,
                                HybridModelTrainingReport& outReport,
                                std::string& outError);
    bool SaveHybridModelBundle(const std::filesystem::path& directoryPath,
                               const HybridModelBundle& bundle,
                               const HybridModelTrainingReport* report,
                               std::string& outError);
    bool LoadHybridModelBundle(const std::filesystem::path& directoryPath,
                               HybridModelBundle& outBundle,
                               std::string& outError);
    std::string BuildHybridModelBundleSummary(const HybridModelBundle& bundle,
                                              const HybridModelTrainingReport* report);
    bool RunFullHybridRuntime(const ReducedModelTrainingSequence& teacherSequence,
                              const ColliderSet& colliders,
                              const HybridModelBundle& bundle,
                              const FullHybridRuntimeSettings& settings,
                              const std::filesystem::path& outputDirectory,
                              FullHybridRuntimeReport& outReport,
                              std::string& outError);
}
