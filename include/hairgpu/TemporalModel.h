#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct TemporalGuideNetworkSettings
    {
        uint32_t hiddenWidth = 24u;
        uint32_t epochs = 120u;
        float learningRate = 2.5e-3f;
        float weightDecay = 1.0e-5f;
        float gradientClip = 0.35f;
        bool lockRootPoints = true;
    };

    struct TemporalGuideNetworkEvaluation
    {
        float rmsPositionError = 0.0f;
        float maxPositionError = 0.0f;
        uint32_t sampleCount = 0u;
    };

    struct TemporalGuideNetwork
    {
        TemporalGuideNetworkSettings settings = {};
        uint32_t guideStrandCount = 0u;
        uint32_t particlesPerStrand = 0u;
        uint32_t featureCount = 13u;
        uint32_t hiddenWidth = 0u;
        std::vector<float> inputMean;
        std::vector<float> inputStd;
        std::vector<float> targetMean;
        std::vector<float> targetStd;
        std::vector<float> hiddenWeights;
        std::vector<float> hiddenBias;
        std::vector<float> outputWeights;
        std::vector<float> outputBias;
    };

    [[nodiscard]] bool IsTemporalGuideNetworkReady(const TemporalGuideNetwork& model);

    bool SaveTemporalGuideNetworkToFile(const std::filesystem::path& filePath,
                                        const TemporalGuideNetwork& model,
                                        std::string& outError);
    bool LoadTemporalGuideNetworkFromFile(const std::filesystem::path& filePath,
                                          TemporalGuideNetwork& outModel,
                                          std::string& outError);
    std::string BuildTemporalGuideNetworkSummary(const TemporalGuideNetwork& model,
                                                 const TemporalGuideNetworkEvaluation* evaluation);

    bool TrainTemporalGuideNetwork(const std::vector<Vec3>& restGuidePoints,
                                   const std::vector<Vec3>& inputFrames,
                                   const std::vector<Vec3>& targetFrames,
                                   uint32_t frameCount,
                                   uint32_t guideStrandCount,
                                   uint32_t particlesPerStrand,
                                   const TemporalGuideNetworkSettings& settings,
                                   TemporalGuideNetwork& outModel,
                                   TemporalGuideNetworkEvaluation* outEvaluation,
                                   std::string& outError);

    bool PredictTemporalGuideFrame(const TemporalGuideNetwork& model,
                                   const std::vector<Vec3>& restGuidePoints,
                                   const std::vector<Vec3>& previousPreviousInput,
                                   const std::vector<Vec3>& previousInput,
                                   const std::vector<Vec3>& currentInput,
                                   std::vector<Vec3>& outPredictedFrame,
                                   std::string& outError);

    bool ExportTemporalGuideNetworkToCsvDirectory(const std::filesystem::path& directoryPath,
                                                  const TemporalGuideNetwork& model,
                                                  std::string& outError);
    bool ImportTemporalGuideNetworkFromCsvDirectory(const std::filesystem::path& directoryPath,
                                                    TemporalGuideNetwork& outModel,
                                                    std::string& outError);
    bool ExportTemporalGuideTrainingDatasetCsv(const std::filesystem::path& filePath,
                                               const std::vector<Vec3>& restGuidePoints,
                                               const std::vector<Vec3>& inputFrames,
                                               const std::vector<Vec3>& targetFrames,
                                               uint32_t frameCount,
                                               uint32_t guideStrandCount,
                                               uint32_t particlesPerStrand,
                                               bool lockRootPoints,
                                               std::string& outError);
}
