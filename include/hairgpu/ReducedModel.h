#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/GuideCache.h"
#include "hairgpu/Types.h"

namespace hairgpu
{
    struct ReducedModelTrainingSequence
    {
        HairAsset referenceAsset;
        float frameRate = 30.0f;
        std::vector<Vec3> frames; // frame-major, full asset point layout per frame

        [[nodiscard]] uint32_t PointCountPerFrame() const
        {
            return static_cast<uint32_t>(referenceAsset.restPoints.size());
        }

        [[nodiscard]] uint32_t FrameCount() const
        {
            const uint32_t pointsPerFrame = PointCountPerFrame();
            return (pointsPerFrame > 0u) ? static_cast<uint32_t>(frames.size() / pointsPerFrame) : 0u;
        }

        [[nodiscard]] bool Empty() const
        {
            return PointCountPerFrame() == 0u || frames.empty();
        }
    };

    struct ReducedModelSettings
    {
        uint32_t guideCount = 0u;
        uint32_t maxInfluencesPerFollower = 4u;
        uint32_t signatureFrameSamples = 12u;
        uint32_t validationStride = 0u;
        float regularization = 1.0e-4f;
        float affineConstraintWeight = 5.0e-2f;
        float guidePreference = 0.20f;
        float lengthProjectionBlend = 0.55f;
        float rootLockBlend = 1.0f;
        float floorY = -1.0e9f;
        float rootUvWeight = 1.0f;
        float rootPositionWeight = 0.25f;
        float scalpCollisionBlend = 0.85f;
        float scalpCollisionOffset = 0.0025f;
        bool preferOriginalGuideFlags = true;
        bool restrictGuideCandidatesToOriginalGuides = false;
        bool useGuideDisplacements = true;
        bool enableFloorCollision = false;
        bool enableScalpCollision = true;
        uint32_t localGuideCandidateCount = 12u;
        uint32_t scalpCollisionLockedPoints = 3u;
    };

    struct ReducedModelInfluence
    {
        uint32_t guideSlot = 0u;
        float weight = 0.0f;
    };

    struct ReducedModelStrandRecord
    {
        uint32_t influenceOffset = 0u;
        uint32_t influenceCount = 0u;
        uint32_t biasOffset = 0u;
        uint32_t pointCount = 0u;
        uint32_t sourceStrandIndex = 0u;
        uint32_t guideSlot = UINT32_MAX;
        bool isGuide = false;
    };


    struct ReducedModelStudyEntry
    {
        uint32_t guideCount = 0u;
        uint32_t maxInfluences = 0u;
        bool useGuideDisplacements = true;
        float trainingRmsError = 0.0f;
        float validationRmsError = 0.0f;
        float trainingTipRmsError = 0.0f;
        float validationTipRmsError = 0.0f;
        float validationMaxError = 0.0f;
        float score = 0.0f;
    };

    struct ReducedModelEvaluationReport
    {
        float rmsPositionError = 0.0f;
        float maxPositionError = 0.0f;
        float rmsTipError = 0.0f;
        float maxTipError = 0.0f;
        float rmsRootError = 0.0f;
        float maxRootError = 0.0f;
        uint32_t frameCount = 0u;
        uint32_t pointSamples = 0u;
        uint32_t strandSamples = 0u;
    };

    struct ReducedModel
    {
        HairAsset referenceAsset;
        ReducedModelSettings settings = {};
        std::vector<uint32_t> selectedGuideStrandIndices;
        std::vector<ReducedModelStrandRecord> strandRecords;
        std::vector<ReducedModelInfluence> influences;
        std::vector<Vec3> pointBiases;
        float trainingRmsError = 0.0f;
        float trainingMaxError = 0.0f;
        float trainingTipRmsError = 0.0f;
        float trainingRootRmsError = 0.0f;
        float validationRmsError = 0.0f;
        float validationMaxError = 0.0f;
        float validationTipRmsError = 0.0f;
        float validationRootRmsError = 0.0f;
        uint32_t trainingFrameCount = 0u;
        uint32_t validationFrameCount = 0u;
    };

    bool SaveReducedModelToFile(const std::filesystem::path& filePath, const ReducedModel& model, std::string& outError);
    bool LoadReducedModelFromFile(const std::filesystem::path& filePath, ReducedModel& outModel, std::string& outError);
    std::string BuildReducedModelSummary(const ReducedModel& model);

    bool LoadReducedTrainingSequenceFromDirectory(const std::filesystem::path& directoryPath,
                                                 ReducedModelTrainingSequence& outSequence,
                                                 std::string& outError);
    bool SaveReducedTrainingSequenceManifest(const std::filesystem::path& directoryPath,
                                            const ReducedModelTrainingSequence& sequence,
                                            std::string& outError);
    bool SynthesizeReducedTrainingSequence(const HairAsset& asset,
                                          const std::filesystem::path& outputDirectory,
                                          uint32_t frameCount,
                                          float frameRate,
                                          float swayAmplitude,
                                          std::string& outError);

    bool TrainReducedModel(const ReducedModelTrainingSequence& sequence,
                           const ReducedModelSettings& settings,
                           ReducedModel& outModel,
                           std::string& outError);
    bool AutoTuneReducedModel(const ReducedModelTrainingSequence& sequence,
                              uint32_t minGuideCount,
                              uint32_t maxGuideCount,
                              uint32_t guideStep,
                              const ReducedModelSettings& baseSettings,
                              ReducedModel& outBestModel,
                              std::string& outSummary,
                              std::string& outError);
    bool RunReducedModelStudy(const ReducedModelTrainingSequence& sequence,
                              uint32_t minGuideCount,
                              uint32_t maxGuideCount,
                              uint32_t guideStep,
                              const ReducedModelSettings& baseSettings,
                              bool compareDisplacementModes,
                              std::vector<ReducedModelStudyEntry>& outEntries,
                              ReducedModel& outBestModel,
                              std::string& outSummary,
                              std::string& outError);
    bool WriteReducedModelStudyJson(const std::filesystem::path& filePath,
                                    const std::vector<ReducedModelStudyEntry>& entries,
                                    const ReducedModel& bestModel,
                                    std::string& outError);
    bool EvaluateReducedModel(const ReducedModel& model,
                              const ReducedModelTrainingSequence& sequence,
                              ReducedModelEvaluationReport& outReport,
                              std::string& outError);
    bool WriteReducedEvaluationReportJson(const std::filesystem::path& filePath,
                                          const ReducedModelEvaluationReport& report,
                                          std::string& outError);
    bool ExportReducedModelGuideCache(const ReducedModel& model,
                                      const ReducedModelTrainingSequence& sequence,
                                      GuideCache& outGuideCache,
                                      std::string& outError);
    bool ReconstructReducedModelFrame(const ReducedModel& model,
                                      const std::vector<Vec3>& guideFramePoints,
                                      HairAsset& outAsset,
                                      std::string& outError);
    bool ReconstructReducedModelFrameFromSequence(const ReducedModel& model,
                                                  const ReducedModelTrainingSequence& sequence,
                                                  uint32_t frameIndex,
                                                  HairAsset& outAsset,
                                                  std::string& outError);
    bool ReconstructReducedModelSequence(const ReducedModel& model,
                                         const ReducedModelTrainingSequence& sequence,
                                         const std::filesystem::path& outputDirectory,
                                         std::string& outError);
}
