#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/ReducedModel.h"
#include "hairgpu/TemporalModel.h"
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

    struct ActiveSetPredictorSettings
    {
        uint32_t epochs = 120u;
        float learningRate = 2.0e-2f;
        float weightDecay = 1.0e-4f;
    };

    struct ActiveSetPredictorEvaluation
    {
        float rmsScoreError = 0.0f;
        float classificationAccuracy = 0.0f;
        float positiveRate = 0.0f;
        uint32_t sampleCount = 0u;
    };

    struct ActiveSetPredictor
    {
        ActiveSetPredictorSettings settings = {};
        uint32_t featureCount = 9u;
        std::vector<float> featureMean;
        std::vector<float> featureStd;
        std::vector<float> weights;
        float bias = 0.0f;
    };

    struct HybridModelBundle
    {
        ReducedModel priorModel;
        ReducedModel residualModel;
        TemporalGuideNetwork priorTemporalNetwork;
        TemporalGuideNetwork residualTemporalNetwork;
        ActiveSetPredictor activeSetPredictor;
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
        float priorAccelerationPredictionGain = 0.0f;
        float residualAccelerationPredictionGain = 0.0f;
        float priorTemporalBlend = 0.65f;
        float residualTemporalBlend = 0.70f;
        float activeSetPredictorBlend = 0.55f;
        float frozenStrandBlend = 0.92f;
        float frozenRootFollow = 1.0f;
        float cachedStrandBlend = 0.78f;
        float cachedRootFollow = 0.95f;
        uint32_t cachedFrameRefreshInterval = 3u;
    };

    struct HybridModelTrainingReport
    {
        ReducedModelEvaluationReport priorEvaluation = {};
        ReducedModelEvaluationReport residualEvaluation = {};
        TemporalGuideNetworkEvaluation priorTemporalEvaluation = {};
        TemporalGuideNetworkEvaluation residualTemporalEvaluation = {};
        ActiveSetPredictorEvaluation activeSetPredictorEvaluation = {};
        float averageResidualMagnitude = 0.0f;
        float maxResidualMagnitude = 0.0f;
    };

    struct FullHybridRuntimeSettings
    {
        float frameRateOverride = 0.0f;
        float residualBlendOverride = -1.0f;
        float activeSetPredictorBlendOverride = -1.0f;
        ActiveSetBudget budgetOverride = {};
        bool useBudgetOverride = false;
        bool exportPerFrameHairBins = false;
        bool enableResidualModel = true;
        bool enableSparseCorrectivePhysics = true;
        bool enableTemporalNetworks = true;
        bool enableActiveSetPredictor = true;
        bool useTeacherPhysicsProxy = false;
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
        float averageGuidePredictorRisk = 0.0f;
        float maxGuidePredictorRisk = 0.0f;
        float averageEstimatedFrameMilliseconds = 0.0f;
        float p95EstimatedFrameMilliseconds = 0.0f;
        float maxEstimatedFrameMilliseconds = 0.0f;
        float hitRate60Fps = 0.0f;
    };

    [[nodiscard]] bool IsActiveSetPredictorReady(const ActiveSetPredictor& predictor);
    bool SaveActiveSetPredictorToFile(const std::filesystem::path& filePath,
                                      const ActiveSetPredictor& predictor,
                                      std::string& outError);
    bool LoadActiveSetPredictorFromFile(const std::filesystem::path& filePath,
                                        ActiveSetPredictor& outPredictor,
                                        std::string& outError);
    std::string BuildActiveSetPredictorSummary(const ActiveSetPredictor& predictor,
                                               const ActiveSetPredictorEvaluation* evaluation);
    bool ExportActiveSetPredictorToCsvDirectory(const std::filesystem::path& directoryPath,
                                                const ActiveSetPredictor& predictor,
                                                std::string& outError);
    bool ImportActiveSetPredictorFromCsvDirectory(const std::filesystem::path& directoryPath,
                                                  ActiveSetPredictor& outPredictor,
                                                  std::string& outError);

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
