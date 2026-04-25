#include "hairgpu/TemporalModel.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

#include "hairgpu/BinaryIO.h"

#include "hairgpu/ErrorMetrics.h"
namespace hairgpu
{
    namespace
    {
        constexpr std::array<char, 8> kTemporalMagic = { 'H', 'T', 'E', 'M', 'P', 'N', 'E', '1' };
        constexpr uint32_t kTemporalVersion = 3u;
        constexpr uint32_t kFeatureCount = 13u;
        constexpr uint32_t kTargetCount = 3u;

        struct TemporalTrainingSample
        {
            std::array<float, kFeatureCount> features = {};
            std::array<float, kTargetCount> target = {};
            bool lockOutput = false;
        };

        void BuildSampleFeatures(const Vec3* restGuidePoints,
                                 const Vec3* previousPreviousInput,
                                 const Vec3* previousInput,
                                 const Vec3* currentInput,
                                 uint32_t guideStrandCount,
                                 uint32_t particlesPerStrand,
                                 size_t pointIndex,
                                 std::array<float, kFeatureCount>& outFeatures,
                                 bool& outLockOutput)
        {
            const Vec3 rest = restGuidePoints[pointIndex];
            const Vec3 prevPrev = previousPreviousInput[pointIndex];
            const Vec3 prev = previousInput[pointIndex];
            const Vec3 current = currentInput[pointIndex];
            const Vec3 currentDelta = current - rest;
            const Vec3 velocity = current - prev;
            const Vec3 acceleration = current - prev * 2.0f + prevPrev;
            const uint32_t strandIndex = (particlesPerStrand > 0u)
                ? static_cast<uint32_t>(pointIndex / particlesPerStrand)
                : 0u;
            const uint32_t localPoint = (particlesPerStrand > 0u)
                ? static_cast<uint32_t>(pointIndex % particlesPerStrand)
                : 0u;
            const float pointT = (particlesPerStrand > 1u)
                ? static_cast<float>(localPoint) / static_cast<float>(particlesPerStrand - 1u)
                : 0.0f;
            const float strandT = (guideStrandCount > 1u)
                ? static_cast<float>(strandIndex) / static_cast<float>(guideStrandCount - 1u)
                : 0.0f;
            const Vec3 rootRest = restGuidePoints[static_cast<size_t>(strandIndex) * particlesPerStrand];
            const float rootDistance = Length(rest - rootRest);
            const float displacementMagnitude = Length(currentDelta);

            outFeatures[0] = currentDelta.x;
            outFeatures[1] = currentDelta.y;
            outFeatures[2] = currentDelta.z;
            outFeatures[3] = velocity.x;
            outFeatures[4] = velocity.y;
            outFeatures[5] = velocity.z;
            outFeatures[6] = acceleration.x;
            outFeatures[7] = acceleration.y;
            outFeatures[8] = acceleration.z;
            outFeatures[9] = pointT;
            outFeatures[10] = strandT;
            outFeatures[11] = rootDistance;
            outFeatures[12] = displacementMagnitude;
            outLockOutput = (localPoint == 0u);
        }

        void AccumulateStats(const std::array<float, kFeatureCount>& values,
                             std::array<double, kFeatureCount>& ioSum,
                             std::array<double, kFeatureCount>& ioSqSum)
        {
            for (uint32_t i = 0u; i < kFeatureCount; ++i)
            {
                ioSum[i] += static_cast<double>(values[i]);
                ioSqSum[i] += static_cast<double>(values[i]) * values[i];
            }
        }

        void AccumulateStats(const std::array<float, kTargetCount>& values,
                             std::array<double, kTargetCount>& ioSum,
                             std::array<double, kTargetCount>& ioSqSum)
        {
            for (uint32_t i = 0u; i < kTargetCount; ++i)
            {
                ioSum[i] += static_cast<double>(values[i]);
                ioSqSum[i] += static_cast<double>(values[i]) * values[i];
            }
        }

        void FinalizeStats(const std::array<double, kFeatureCount>& sum,
                           const std::array<double, kFeatureCount>& sqSum,
                           double count,
                           std::vector<float>& outMean,
                           std::vector<float>& outStd)
        {
            outMean.resize(kFeatureCount);
            outStd.resize(kFeatureCount);
            for (uint32_t i = 0u; i < kFeatureCount; ++i)
            {
                const double mean = (count > 0.0) ? (sum[i] / count) : 0.0;
                const double variance = (count > 0.0) ? std::max(0.0, (sqSum[i] / count) - mean * mean) : 1.0;
                outMean[i] = static_cast<float>(mean);
                outStd[i] = static_cast<float>(std::sqrt(variance) + 1.0e-4);
            }
        }

        void FinalizeStats(const std::array<double, kTargetCount>& sum,
                           const std::array<double, kTargetCount>& sqSum,
                           double count,
                           std::vector<float>& outMean,
                           std::vector<float>& outStd)
        {
            outMean.resize(kTargetCount);
            outStd.resize(kTargetCount);
            for (uint32_t i = 0u; i < kTargetCount; ++i)
            {
                const double mean = (count > 0.0) ? (sum[i] / count) : 0.0;
                const double variance = (count > 0.0) ? std::max(0.0, (sqSum[i] / count) - mean * mean) : 1.0;
                outMean[i] = static_cast<float>(mean);
                outStd[i] = static_cast<float>(std::sqrt(variance) + 1.0e-4);
            }
        }

        float NormalizeValue(float value, float mean, float stddev)
        {
            return (std::fabs(stddev) > 1.0e-8f) ? ((value - mean) / stddev) : (value - mean);
        }

        float DenormalizeValue(float value, float mean, float stddev)
        {
            return value * ((std::fabs(stddev) > 1.0e-8f) ? stddev : 1.0f) + mean;
        }

        void Forward(const TemporalGuideNetwork& model,
                     const std::array<float, kFeatureCount>& features,
                     std::vector<float>& hidden,
                     std::array<float, kTargetCount>& output)
        {
            hidden.assign(model.hiddenWidth, 0.0f);
            std::vector<float> hidden2(model.hiddenWidth2, 0.0f);
            std::array<float, kFeatureCount> normalizedFeatures = {};
            for (uint32_t i = 0u; i < model.featureCount; ++i)
            {
                normalizedFeatures[i] = NormalizeValue(features[i], model.inputMean[i], model.inputStd[i]);
            }
            for (uint32_t h = 0u; h < model.hiddenWidth; ++h)
            {
                float sum = model.hiddenBias[h];
                const size_t rowOffset = static_cast<size_t>(h) * model.featureCount;
                for (uint32_t i = 0u; i < model.featureCount; ++i)
                {
                    sum += model.hiddenWeights[rowOffset + i] * normalizedFeatures[i];
                }
                hidden[h] = std::tanh(sum);
            }
            const bool useSecondLayer = model.hiddenWidth2 > 0u &&
                                        model.hiddenWeights2.size() == static_cast<size_t>(model.hiddenWidth2) * model.hiddenWidth &&
                                        model.hiddenBias2.size() == model.hiddenWidth2;
            if (useSecondLayer)
            {
                for (uint32_t h = 0u; h < model.hiddenWidth2; ++h)
                {
                    float sum = model.hiddenBias2[h];
                    const size_t rowOffset = static_cast<size_t>(h) * model.hiddenWidth;
                    for (uint32_t i = 0u; i < model.hiddenWidth; ++i)
                    {
                        sum += model.hiddenWeights2[rowOffset + i] * hidden[i];
                    }
                    hidden2[h] = std::tanh(sum);
                }
            }
            for (uint32_t o = 0u; o < kTargetCount; ++o)
            {
                float sum = model.outputBias[o];
                if (useSecondLayer)
                {
                    const size_t rowOffset = static_cast<size_t>(o) * model.hiddenWidth2;
                    for (uint32_t h = 0u; h < model.hiddenWidth2; ++h)
                    {
                        sum += model.outputWeights[rowOffset + h] * hidden2[h];
                    }
                }
                else
                {
                    const size_t rowOffset = static_cast<size_t>(o) * model.hiddenWidth;
                    for (uint32_t h = 0u; h < model.hiddenWidth; ++h)
                    {
                        sum += model.outputWeights[rowOffset + h] * hidden[h];
                    }
                }
                output[o] = sum;
            }
        }

        void BuildSamples(const std::vector<Vec3>& restGuidePoints,
                          const std::vector<Vec3>& inputFrames,
                          const std::vector<Vec3>& targetFrames,
                          uint32_t frameCount,
                          uint32_t guideStrandCount,
                          uint32_t particlesPerStrand,
                          bool lockRootPoints,
                          std::vector<TemporalTrainingSample>& outSamples,
                          std::string& outError)
        {
            outSamples.clear();
            const size_t pointsPerFrame = restGuidePoints.size();
            if (pointsPerFrame == 0u || inputFrames.size() != pointsPerFrame * frameCount || targetFrames.size() != pointsPerFrame * frameCount)
            {
                outError = "Temporal guide training requires matching non-empty input/target sequences.";
                return;
            }
            if (frameCount < 3u)
            {
                outError = "Temporal guide training requires at least 3 frames.";
                return;
            }

            outSamples.reserve(static_cast<size_t>(frameCount - 2u) * pointsPerFrame);
            for (uint32_t frameIndex = 2u; frameIndex < frameCount; ++frameIndex)
            {
                const size_t ppBase = static_cast<size_t>(frameIndex - 2u) * pointsPerFrame;
                const size_t pBase = static_cast<size_t>(frameIndex - 1u) * pointsPerFrame;
                const size_t cBase = static_cast<size_t>(frameIndex) * pointsPerFrame;
                for (size_t pointIndex = 0u; pointIndex < pointsPerFrame; ++pointIndex)
                {
                    TemporalTrainingSample sample = {};
                    BuildSampleFeatures(restGuidePoints.data(),
                                        inputFrames.data() + ppBase,
                                        inputFrames.data() + pBase,
                                        inputFrames.data() + cBase,
                                        guideStrandCount,
                                        particlesPerStrand,
                                        pointIndex,
                                        sample.features,
                                        sample.lockOutput);
                    const Vec3 rest = restGuidePoints[pointIndex];
                    const Vec3 target = targetFrames[cBase + pointIndex] - rest;
                    sample.target[0] = target.x;
                    sample.target[1] = target.y;
                    sample.target[2] = target.z;
                    sample.lockOutput = sample.lockOutput && lockRootPoints;
                    outSamples.push_back(sample);
                }
            }
            outError.clear();
        }
    }

    bool IsTemporalGuideNetworkReady(const TemporalGuideNetwork& model)
    {
        const bool hasBase = model.featureCount == kFeatureCount &&
                             model.hiddenWidth > 0u &&
                             !model.hiddenWeights.empty() &&
                             !model.outputWeights.empty() &&
                             model.inputMean.size() == kFeatureCount &&
                             model.inputStd.size() == kFeatureCount &&
                             model.targetMean.size() == kTargetCount &&
                             model.targetStd.size() == kTargetCount;
        if (!hasBase)
        {
            return false;
        }
        if (model.hiddenWidth2 == 0u)
        {
            return model.outputWeights.size() == static_cast<size_t>(kTargetCount) * model.hiddenWidth;
        }
        return model.hiddenWeights2.size() == static_cast<size_t>(model.hiddenWidth2) * model.hiddenWidth &&
               model.hiddenBias2.size() == model.hiddenWidth2 &&
               model.outputWeights.size() == static_cast<size_t>(kTargetCount) * model.hiddenWidth2;
    }

    bool SaveTemporalGuideNetworkToFile(const std::filesystem::path& filePath,
                                        const TemporalGuideNetwork& model,
                                        std::string& outError)
    {
        if (!IsTemporalGuideNetworkReady(model))
        {
            outError = "Temporal guide network is not initialized.";
            return false;
        }
        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open temporal network file for writing.";
            return false;
        }
        out.write(kTemporalMagic.data(), static_cast<std::streamsize>(kTemporalMagic.size()));
        if (!WriteValue(out, kTemporalVersion) ||
            !WriteValue(out, model.settings.hiddenWidth) ||
            !WriteValue(out, model.settings.hiddenWidth2) ||
            !WriteValue(out, model.settings.epochs) ||
            !WriteValue(out, model.settings.validationStride) ||
            !WriteValue(out, model.settings.earlyStoppingPatience) ||
            !WriteValue(out, model.settings.learningRate) ||
            !WriteValue(out, model.settings.learningRateDecay) ||
            !WriteValue(out, model.settings.minLearningRate) ||
            !WriteValue(out, model.settings.weightDecay) ||
            !WriteValue(out, model.settings.gradientClip) ||
            !WriteValue(out, model.guideStrandCount) ||
            !WriteValue(out, model.particlesPerStrand) ||
            !WriteValue(out, model.featureCount) ||
            !WriteValue(out, model.hiddenWidth) ||
            !WriteValue(out, model.hiddenWidth2) ||
            !WriteValue(out, model.settings.lockRootPoints) ||
            !WriteVector(out, model.inputMean) ||
            !WriteVector(out, model.inputStd) ||
            !WriteVector(out, model.targetMean) ||
            !WriteVector(out, model.targetStd) ||
            !WriteVector(out, model.hiddenWeights) ||
            !WriteVector(out, model.hiddenBias) ||
            !WriteVector(out, model.hiddenWeights2) ||
            !WriteVector(out, model.hiddenBias2) ||
            !WriteVector(out, model.outputWeights) ||
            !WriteVector(out, model.outputBias))
        {
            outError = "Failed to serialize temporal guide network.";
            return false;
        }
        outError.clear();
        return true;
    }

    bool LoadTemporalGuideNetworkFromFile(const std::filesystem::path& filePath,
                                          TemporalGuideNetwork& outModel,
                                          std::string& outError)
    {
        outModel = {};
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open temporal network file.";
            return false;
        }
        std::array<char, 8> magic = {};
        in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!in || magic != kTemporalMagic)
        {
            outError = "Temporal network file has an invalid signature.";
            return false;
        }
        uint32_t version = 0u;
        if (!ReadValue(in, version) || (version != 1u && version != 2u && version != kTemporalVersion))
        {
            outError = "Temporal network file has an unsupported version.";
            return false;
        }
        if (!ReadValue(in, outModel.settings.hiddenWidth))
        {
            outError = "Failed to deserialize temporal guide network.";
            return false;
        }
        if (version >= 2u)
        {
            if (!ReadValue(in, outModel.settings.hiddenWidth2))
            {
                outError = "Failed to deserialize temporal guide network.";
                return false;
            }
        }
        if (!ReadValue(in, outModel.settings.epochs) ||
            !(version < 3u || ReadValue(in, outModel.settings.validationStride)) ||
            !(version < 3u || ReadValue(in, outModel.settings.earlyStoppingPatience)) ||
            !ReadValue(in, outModel.settings.learningRate) ||
            !(version < 3u || ReadValue(in, outModel.settings.learningRateDecay)) ||
            !(version < 3u || ReadValue(in, outModel.settings.minLearningRate)) ||
            !ReadValue(in, outModel.settings.weightDecay) ||
            !ReadValue(in, outModel.settings.gradientClip) ||
            !ReadValue(in, outModel.guideStrandCount) ||
            !ReadValue(in, outModel.particlesPerStrand) ||
            !ReadValue(in, outModel.featureCount) ||
            !ReadValue(in, outModel.hiddenWidth))
        {
            outError = "Failed to deserialize temporal guide network.";
            return false;
        }
        if (version >= 2u)
        {
            if (!ReadValue(in, outModel.hiddenWidth2))
            {
                outError = "Failed to deserialize temporal guide network.";
                return false;
            }
        }
        if (!ReadValue(in, outModel.settings.lockRootPoints) ||
            !ReadVector(in, outModel.inputMean) ||
            !ReadVector(in, outModel.inputStd) ||
            !ReadVector(in, outModel.targetMean) ||
            !ReadVector(in, outModel.targetStd) ||
            !ReadVector(in, outModel.hiddenWeights) ||
            !ReadVector(in, outModel.hiddenBias))
        {
            outError = "Failed to deserialize temporal guide network.";
            return false;
        }
        if (version >= 2u)
        {
            if (!ReadVector(in, outModel.hiddenWeights2) ||
                !ReadVector(in, outModel.hiddenBias2))
            {
                outError = "Failed to deserialize temporal guide network.";
                return false;
            }
        }
        if (!ReadVector(in, outModel.outputWeights) ||
            !ReadVector(in, outModel.outputBias))
        {
            outError = "Failed to deserialize temporal guide network.";
            return false;
        }
        if (!IsTemporalGuideNetworkReady(outModel))
        {
            outError = "Temporal guide network file is incomplete.";
            return false;
        }
        outError.clear();
        return true;
    }

    std::string BuildTemporalGuideNetworkSummary(const TemporalGuideNetwork& model,
                                                 const TemporalGuideNetworkEvaluation* evaluation)
    {
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(5);
        ss << "guides=" << model.guideStrandCount
           << " particles=" << model.particlesPerStrand
           << " hidden=" << model.hiddenWidth;
        if (model.hiddenWidth2 > 0u)
        {
            ss << "x" << model.hiddenWidth2;
        }
        ss << " epochs=" << model.settings.epochs
           << " lr=" << model.settings.learningRate
           << " valStride=" << model.settings.validationStride
           << " patience=" << model.settings.earlyStoppingPatience;
        if (evaluation)
        {
            ss << " rms=" << evaluation->rmsPositionError
               << " max=" << evaluation->maxPositionError;
        }
        return ss.str();
    }

    bool TrainTemporalGuideNetwork(const std::vector<Vec3>& restGuidePoints,
                                   const std::vector<Vec3>& inputFrames,
                                   const std::vector<Vec3>& targetFrames,
                                   uint32_t frameCount,
                                   uint32_t guideStrandCount,
                                   uint32_t particlesPerStrand,
                                   const TemporalGuideNetworkSettings& settings,
                                   TemporalGuideNetwork& outModel,
                                   TemporalGuideNetworkEvaluation* outEvaluation,
                                   std::string& outError)
    {
        outError.clear();
        outModel = {};
        if (guideStrandCount == 0u || particlesPerStrand == 0u || restGuidePoints.empty())
        {
            outError = "Temporal guide training requires a valid guide layout.";
            return false;
        }

        std::vector<TemporalTrainingSample> samples;
        BuildSamples(restGuidePoints,
                     inputFrames,
                     targetFrames,
                     frameCount,
                     guideStrandCount,
                     particlesPerStrand,
                     settings.lockRootPoints,
                     samples,
                     outError);
        if (!outError.empty())
        {
            return false;
        }
        if (samples.empty())
        {
            outError = "Temporal guide training generated no samples.";
            return false;
        }

        outModel.settings = settings;
        outModel.guideStrandCount = guideStrandCount;
        outModel.particlesPerStrand = particlesPerStrand;
        outModel.featureCount = kFeatureCount;
        outModel.hiddenWidth = std::max<uint32_t>(6u, settings.hiddenWidth);
        outModel.hiddenWidth2 = std::max<uint32_t>(0u, settings.hiddenWidth2);

        std::array<double, kFeatureCount> inputSum = {};
        std::array<double, kFeatureCount> inputSqSum = {};
        std::array<double, kTargetCount> targetSum = {};
        std::array<double, kTargetCount> targetSqSum = {};
        for (const TemporalTrainingSample& sample : samples)
        {
            AccumulateStats(sample.features, inputSum, inputSqSum);
            AccumulateStats(sample.target, targetSum, targetSqSum);
        }
        const double sampleCount = static_cast<double>(samples.size());
        FinalizeStats(inputSum, inputSqSum, sampleCount, outModel.inputMean, outModel.inputStd);
        FinalizeStats(targetSum, targetSqSum, sampleCount, outModel.targetMean, outModel.targetStd);

        outModel.hiddenWeights.assign(static_cast<size_t>(outModel.hiddenWidth) * outModel.featureCount, 0.0f);
        outModel.hiddenBias.assign(outModel.hiddenWidth, 0.0f);
        outModel.hiddenWeights2.assign(static_cast<size_t>(outModel.hiddenWidth2) * outModel.hiddenWidth, 0.0f);
        outModel.hiddenBias2.assign(outModel.hiddenWidth2, 0.0f);
        const uint32_t outputWidth = (outModel.hiddenWidth2 > 0u) ? outModel.hiddenWidth2 : outModel.hiddenWidth;
        outModel.outputWeights.assign(static_cast<size_t>(kTargetCount) * outputWidth, 0.0f);
        outModel.outputBias.assign(kTargetCount, 0.0f);

        std::mt19937 rng(1337u);
        std::uniform_real_distribution<float> dist(-0.07f, 0.07f);
        for (float& value : outModel.hiddenWeights) value = dist(rng);
        for (float& value : outModel.hiddenWeights2) value = dist(rng);
        for (float& value : outModel.outputWeights) value = dist(rng);

        std::vector<size_t> order(samples.size());
        std::iota(order.begin(), order.end(), 0u);
        std::vector<float> hidden1(outModel.hiddenWidth, 0.0f);
        std::vector<float> hidden2(outModel.hiddenWidth2, 0.0f);
        std::array<float, kTargetCount> output = {};
        std::array<float, kFeatureCount> normalizedFeatures = {};
        std::array<float, kTargetCount> targetNorm = {};
        std::vector<float> gradHidden1(outModel.hiddenWidth, 0.0f);
        std::vector<float> gradHidden2(outModel.hiddenWidth2, 0.0f);

        auto forwardDetailed = [&](const TemporalTrainingSample& sample)
        {
            for (uint32_t i = 0u; i < outModel.featureCount; ++i)
            {
                normalizedFeatures[i] = NormalizeValue(sample.features[i], outModel.inputMean[i], outModel.inputStd[i]);
            }
            for (uint32_t h = 0u; h < outModel.hiddenWidth; ++h)
            {
                float sum = outModel.hiddenBias[h];
                const size_t rowOffset = static_cast<size_t>(h) * outModel.featureCount;
                for (uint32_t i = 0u; i < outModel.featureCount; ++i)
                {
                    sum += outModel.hiddenWeights[rowOffset + i] * normalizedFeatures[i];
                }
                hidden1[h] = std::tanh(sum);
            }
            for (uint32_t h = 0u; h < outModel.hiddenWidth2; ++h)
            {
                float sum = outModel.hiddenBias2[h];
                const size_t rowOffset = static_cast<size_t>(h) * outModel.hiddenWidth;
                for (uint32_t i = 0u; i < outModel.hiddenWidth; ++i)
                {
                    sum += outModel.hiddenWeights2[rowOffset + i] * hidden1[i];
                }
                hidden2[h] = std::tanh(sum);
            }
            for (uint32_t o = 0u; o < kTargetCount; ++o)
            {
                float sum = outModel.outputBias[o];
                const size_t rowOffset = static_cast<size_t>(o) * outputWidth;
                if (outModel.hiddenWidth2 > 0u)
                {
                    for (uint32_t i = 0u; i < outModel.hiddenWidth2; ++i)
                    {
                        sum += outModel.outputWeights[rowOffset + i] * hidden2[i];
                    }
                }
                else
                {
                    for (uint32_t i = 0u; i < outModel.hiddenWidth; ++i)
                    {
                        sum += outModel.outputWeights[rowOffset + i] * hidden1[i];
                    }
                }
                output[o] = sum;
                targetNorm[o] = NormalizeValue(sample.target[o], outModel.targetMean[o], outModel.targetStd[o]);
            }
        };

        std::vector<size_t> trainingIndices;
        std::vector<size_t> validationIndices;
        const uint32_t validationStride = std::max<uint32_t>(2u, settings.validationStride);
        for (size_t sampleIndex = 0u; sampleIndex < samples.size(); ++sampleIndex)
        {
            if ((sampleIndex % validationStride) == 0u)
            {
                validationIndices.push_back(sampleIndex);
            }
            else
            {
                trainingIndices.push_back(sampleIndex);
            }
        }
        if (trainingIndices.empty())
        {
            trainingIndices = validationIndices;
            validationIndices.clear();
        }
        order = trainingIndices;

        auto evaluateIndices = [&](const std::vector<size_t>& indices, TemporalGuideNetworkEvaluation& outEval)
        {
            outEval = {};
            if (indices.empty())
            {
                return;
            }
            RmsAccumulator rmsPosition;
            for (size_t sampleIndex : indices)
            {
                const TemporalTrainingSample& sample = samples[sampleIndex];
                forwardDetailed(sample);
                Vec3 predicted(
                    DenormalizeValue(output[0], outModel.targetMean[0], outModel.targetStd[0]),
                    DenormalizeValue(output[1], outModel.targetMean[1], outModel.targetStd[1]),
                    DenormalizeValue(output[2], outModel.targetMean[2], outModel.targetStd[2]));
                Vec3 target(sample.target[0], sample.target[1], sample.target[2]);
                if (sample.lockOutput)
                {
                    predicted = target;
                }
                const Vec3 error = predicted - target;
                const float errorLength = Length(error);
                rmsPosition.AddVectorError(error);
                outEval.maxPositionError = std::max(outEval.maxPositionError, errorLength);
                ++outEval.sampleCount;
            }
            if (outEval.sampleCount > 0u)
            {
                outEval.rmsPositionError = rmsPosition.Rms();
            }
        };

        const uint32_t maxEpochs = std::max<uint32_t>(1u, settings.epochs);
        float lr = std::max(settings.learningRate, settings.minLearningRate);
        float bestValidationRms = std::numeric_limits<float>::max();
        uint32_t epochsWithoutImprovement = 0u;
        TemporalGuideNetwork bestModel = outModel;
        for (uint32_t epoch = 0u; epoch < maxEpochs; ++epoch)
        {
            std::shuffle(order.begin(), order.end(), rng);
            lr = std::max(settings.minLearningRate, lr * ((epoch == 0u) ? 1.0f : settings.learningRateDecay));
            for (size_t orderedIndex : order)
            {
                const TemporalTrainingSample& sample = samples[orderedIndex];
                if (sample.lockOutput)
                {
                    continue;
                }
                forwardDetailed(sample);

                std::array<float, kTargetCount> outputError = {};
                for (uint32_t o = 0u; o < kTargetCount; ++o)
                {
                    outputError[o] = Clamp(output[o] - targetNorm[o], -settings.gradientClip, settings.gradientClip);
                }

                if (outModel.hiddenWidth2 > 0u)
                {
                    std::fill(gradHidden2.begin(), gradHidden2.end(), 0.0f);
                    for (uint32_t h = 0u; h < outModel.hiddenWidth2; ++h)
                    {
                        float g = 0.0f;
                        for (uint32_t o = 0u; o < kTargetCount; ++o)
                        {
                            const size_t rowOffset = static_cast<size_t>(o) * outputWidth;
                            g += outputError[o] * outModel.outputWeights[rowOffset + h];
                            outModel.outputWeights[rowOffset + h] -= lr * (outputError[o] * hidden2[h] + settings.weightDecay * outModel.outputWeights[rowOffset + h]);
                        }
                        gradHidden2[h] = Clamp(g * (1.0f - hidden2[h] * hidden2[h]), -settings.gradientClip, settings.gradientClip);
                    }
                    for (uint32_t o = 0u; o < kTargetCount; ++o)
                    {
                        outModel.outputBias[o] -= lr * outputError[o];
                    }
                    std::fill(gradHidden1.begin(), gradHidden1.end(), 0.0f);
                    for (uint32_t h = 0u; h < outModel.hiddenWidth2; ++h)
                    {
                        const size_t rowOffset = static_cast<size_t>(h) * outModel.hiddenWidth;
                        outModel.hiddenBias2[h] -= lr * gradHidden2[h];
                        for (uint32_t i = 0u; i < outModel.hiddenWidth; ++i)
                        {
                            gradHidden1[i] += gradHidden2[h] * outModel.hiddenWeights2[rowOffset + i];
                            outModel.hiddenWeights2[rowOffset + i] -= lr * (gradHidden2[h] * hidden1[i] + settings.weightDecay * outModel.hiddenWeights2[rowOffset + i]);
                        }
                    }
                    for (float& value : gradHidden1)
                    {
                        value = Clamp(value, -settings.gradientClip, settings.gradientClip);
                    }
                }
                else
                {
                    std::fill(gradHidden1.begin(), gradHidden1.end(), 0.0f);
                    for (uint32_t h = 0u; h < outModel.hiddenWidth; ++h)
                    {
                        float g = 0.0f;
                        for (uint32_t o = 0u; o < kTargetCount; ++o)
                        {
                            const size_t rowOffset = static_cast<size_t>(o) * outputWidth;
                            g += outputError[o] * outModel.outputWeights[rowOffset + h];
                            outModel.outputWeights[rowOffset + h] -= lr * (outputError[o] * hidden1[h] + settings.weightDecay * outModel.outputWeights[rowOffset + h]);
                        }
                        gradHidden1[h] = Clamp(g, -settings.gradientClip, settings.gradientClip);
                    }
                    for (uint32_t o = 0u; o < kTargetCount; ++o)
                    {
                        outModel.outputBias[o] -= lr * outputError[o];
                    }
                }

                for (uint32_t h = 0u; h < outModel.hiddenWidth; ++h)
                {
                    const float localGrad = Clamp(gradHidden1[h] * (1.0f - hidden1[h] * hidden1[h]), -settings.gradientClip, settings.gradientClip);
                    const size_t rowOffset = static_cast<size_t>(h) * outModel.featureCount;
                    outModel.hiddenBias[h] -= lr * localGrad;
                    for (uint32_t i = 0u; i < outModel.featureCount; ++i)
                    {
                        outModel.hiddenWeights[rowOffset + i] -= lr * (localGrad * normalizedFeatures[i] + settings.weightDecay * outModel.hiddenWeights[rowOffset + i]);
                    }
                }
            }

            TemporalGuideNetworkEvaluation validationEval = {};
            evaluateIndices(validationIndices.empty() ? trainingIndices : validationIndices, validationEval);
            if (validationEval.sampleCount > 0u && validationEval.rmsPositionError < bestValidationRms - 1.0e-6f)
            {
                bestValidationRms = validationEval.rmsPositionError;
                bestModel = outModel;
                epochsWithoutImprovement = 0u;
            }
            else
            {
                ++epochsWithoutImprovement;
            }
            if (!validationIndices.empty() && epochsWithoutImprovement >= std::max<uint32_t>(3u, settings.earlyStoppingPatience))
            {
                break;
            }
        }
        outModel = bestModel;

        if (outEvaluation)
        {
            evaluateIndices(validationIndices.empty() ? trainingIndices : validationIndices, *outEvaluation);
        }
        return true;
    }

    bool PredictTemporalGuideFrame(const TemporalGuideNetwork& model,
                                   const std::vector<Vec3>& restGuidePoints,
                                   const std::vector<Vec3>& previousPreviousInput,
                                   const std::vector<Vec3>& previousInput,
                                   const std::vector<Vec3>& currentInput,
                                   std::vector<Vec3>& outPredictedFrame,
                                   std::string& outError)
    {
        outError.clear();
        if (!IsTemporalGuideNetworkReady(model))
        {
            outError = "Temporal guide network is not initialized.";
            return false;
        }
        if (restGuidePoints.size() != currentInput.size() || previousInput.size() != currentInput.size() || previousPreviousInput.size() != currentInput.size())
        {
            outError = "Temporal guide prediction requires matching guide point buffers.";
            return false;
        }
        outPredictedFrame = currentInput;
        std::vector<float> hidden;
        std::array<float, kTargetCount> output = {};
        for (size_t pointIndex = 0u; pointIndex < currentInput.size(); ++pointIndex)
        {
            std::array<float, kFeatureCount> features = {};
            bool lockOutput = false;
            BuildSampleFeatures(restGuidePoints.data(),
                                previousPreviousInput.data(),
                                previousInput.data(),
                                currentInput.data(),
                                model.guideStrandCount,
                                model.particlesPerStrand,
                                pointIndex,
                                features,
                                lockOutput);
            if (lockOutput && model.settings.lockRootPoints)
            {
                outPredictedFrame[pointIndex] = currentInput[pointIndex];
                continue;
            }
            Forward(model, features, hidden, output);
            const Vec3 rest = restGuidePoints[pointIndex];
            outPredictedFrame[pointIndex] = Vec3(
                rest.x + DenormalizeValue(output[0], model.targetMean[0], model.targetStd[0]),
                rest.y + DenormalizeValue(output[1], model.targetMean[1], model.targetStd[1]),
                rest.z + DenormalizeValue(output[2], model.targetMean[2], model.targetStd[2]));
        }
        return true;
    }


    bool ExportTemporalGuideNetworkToCsvDirectory(const std::filesystem::path& directoryPath,
                                                  const TemporalGuideNetwork& model,
                                                  std::string& outError)
    {
        if (!IsTemporalGuideNetworkReady(model))
        {
            outError = "Temporal guide network is not initialized.";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(directoryPath, ec);
        if (ec)
        {
            outError = "Failed to create temporal CSV directory: " + directoryPath.string();
            return false;
        }

        auto writeScalarVector = [](const std::filesystem::path& path, const std::vector<float>& values, std::string& error) -> bool
        {
            std::ofstream out(path);
            if (!out)
            {
                error = "Failed to write CSV: " + path.string();
                return false;
            }
            out.setf(std::ios::fixed);
            out.precision(9);
            for (size_t i = 0u; i < values.size(); ++i)
            {
                if (i > 0u) out << ',';
                out << values[i];
            }
            out << "\n";
            return true;
        };

        auto writeMatrix = [](const std::filesystem::path& path, const std::vector<float>& values, uint32_t rows, uint32_t cols, std::string& error) -> bool
        {
            std::ofstream out(path);
            if (!out)
            {
                error = "Failed to write CSV: " + path.string();
                return false;
            }
            out.setf(std::ios::fixed);
            out.precision(9);
            for (uint32_t r = 0u; r < rows; ++r)
            {
                const size_t rowOffset = static_cast<size_t>(r) * cols;
                for (uint32_t c = 0u; c < cols; ++c)
                {
                    if (c > 0u) out << ',';
                    out << values[rowOffset + c];
                }
                out << "\n";
            }
            return true;
        };

        std::ofstream meta(directoryPath / "metadata.csv");
        if (!meta)
        {
            outError = "Failed to write temporal metadata CSV.";
            return false;
        }
        meta << "key,value\n";
        meta << "guideStrandCount," << model.guideStrandCount << "\n";
        meta << "particlesPerStrand," << model.particlesPerStrand << "\n";
        meta << "featureCount," << model.featureCount << "\n";
        meta << "hiddenWidth," << model.hiddenWidth << "\n";
        meta << "settings.hiddenWidth," << model.settings.hiddenWidth << "\n";
        meta << "settings.epochs," << model.settings.epochs << "\n";
        meta << "settings.learningRate," << model.settings.learningRate << "\n";
        meta << "settings.weightDecay," << model.settings.weightDecay << "\n";
        meta << "settings.gradientClip," << model.settings.gradientClip << "\n";
        meta << "settings.lockRootPoints," << (model.settings.lockRootPoints ? 1 : 0) << "\n";

        if (!writeScalarVector(directoryPath / "input_mean.csv", model.inputMean, outError) ||
            !writeScalarVector(directoryPath / "input_std.csv", model.inputStd, outError) ||
            !writeScalarVector(directoryPath / "target_mean.csv", model.targetMean, outError) ||
            !writeScalarVector(directoryPath / "target_std.csv", model.targetStd, outError) ||
            !writeScalarVector(directoryPath / "hidden_bias.csv", model.hiddenBias, outError) ||
            !writeScalarVector(directoryPath / "hidden_bias2.csv", model.hiddenBias2, outError) ||
            !writeScalarVector(directoryPath / "output_bias.csv", model.outputBias, outError) ||
            !writeMatrix(directoryPath / "hidden_weights.csv", model.hiddenWeights, model.hiddenWidth, model.featureCount, outError) ||
            !writeMatrix(directoryPath / "hidden_weights2.csv", model.hiddenWeights2, model.hiddenWidth2, model.hiddenWidth, outError) ||
            !writeMatrix(directoryPath / "output_weights.csv", model.outputWeights, kTargetCount, (model.hiddenWidth2 > 0u ? model.hiddenWidth2 : model.hiddenWidth), outError))
        {
            return false;
        }

        std::ofstream manifest(directoryPath / "README.txt");
        if (manifest)
        {
            manifest << "TemporalGuideNetwork CSV interchange\n";
            manifest << "metadata.csv plus per-array CSV files can be edited by external tools and re-imported.\n";
        }
        outError.clear();
        return true;
    }

    bool ImportTemporalGuideNetworkFromCsvDirectory(const std::filesystem::path& directoryPath,
                                                    TemporalGuideNetwork& outModel,
                                                    std::string& outError)
    {
        outModel = {};
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
        auto parseScalarVector = [&](const std::filesystem::path& path, std::vector<float>& outValues) -> bool
        {
            std::ifstream in(path);
            if (!in)
            {
                outError = "Failed to open CSV: " + path.string();
                return false;
            }
            std::string line;
            if (!std::getline(in, line))
            {
                outError = "CSV is empty: " + path.string();
                return false;
            }
            const std::vector<std::string> tokens = splitCsv(line);
            outValues.clear();
            outValues.reserve(tokens.size());
            for (const std::string& token : tokens)
            {
                if (token.empty()) continue;
                float value = 0.0f;
                if (!parseFloat(token, value))
                {
                    outError = "Malformed float in CSV: " + path.string();
                    return false;
                }
                outValues.push_back(value);
            }
            return true;
        };
        auto parseMatrix = [&](const std::filesystem::path& path, std::vector<float>& outValues, uint32_t expectedRows, uint32_t expectedCols) -> bool
        {
            if (expectedRows == 0u || expectedCols == 0u)
            {
                outValues.clear();
                return true;
            }
            std::ifstream in(path);
            if (!in)
            {
                outError = "Failed to open CSV: " + path.string();
                return false;
            }
            outValues.clear();
            std::string line;
            uint32_t rowCount = 0u;
            while (std::getline(in, line))
            {
                line = trim(line);
                if (line.empty()) continue;
                const std::vector<std::string> tokens = splitCsv(line);
                if (tokens.size() != expectedCols)
                {
                    outError = "Unexpected CSV column count in: " + path.string();
                    return false;
                }
                for (const std::string& token : tokens)
                {
                    float value = 0.0f;
                    if (!parseFloat(token, value))
                    {
                        outError = "Malformed float in CSV: " + path.string();
                        return false;
                    }
                    outValues.push_back(value);
                }
                ++rowCount;
            }
            if (rowCount != expectedRows)
            {
                outError = "Unexpected CSV row count in: " + path.string();
                return false;
            }
            return true;
        };

        std::ifstream meta(directoryPath / "metadata.csv");
        if (!meta)
        {
            outError = "Failed to open temporal metadata CSV.";
            return false;
        }
        std::string line;
        if (!std::getline(meta, line))
        {
            outError = "Temporal metadata CSV is empty.";
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
            if (key == "guideStrandCount") parseUInt(value, outModel.guideStrandCount);
            else if (key == "particlesPerStrand") parseUInt(value, outModel.particlesPerStrand);
            else if (key == "featureCount") parseUInt(value, outModel.featureCount);
            else if (key == "hiddenWidth") parseUInt(value, outModel.hiddenWidth);
            else if (key == "hiddenWidth2") parseUInt(value, outModel.hiddenWidth2);
            else if (key == "settings.hiddenWidth") parseUInt(value, outModel.settings.hiddenWidth);
            else if (key == "settings.hiddenWidth2") parseUInt(value, outModel.settings.hiddenWidth2);
            else if (key == "settings.epochs") parseUInt(value, outModel.settings.epochs);
            else if (key == "settings.validationStride") parseUInt(value, outModel.settings.validationStride);
            else if (key == "settings.earlyStoppingPatience") parseUInt(value, outModel.settings.earlyStoppingPatience);
            else if (key == "settings.learningRate") parseFloat(value, outModel.settings.learningRate);
            else if (key == "settings.learningRateDecay") parseFloat(value, outModel.settings.learningRateDecay);
            else if (key == "settings.minLearningRate") parseFloat(value, outModel.settings.minLearningRate);
            else if (key == "settings.weightDecay") parseFloat(value, outModel.settings.weightDecay);
            else if (key == "settings.gradientClip") parseFloat(value, outModel.settings.gradientClip);
            else if (key == "settings.lockRootPoints")
            {
                uint32_t temp = 0u;
                if (parseUInt(value, temp)) outModel.settings.lockRootPoints = (temp != 0u);
            }
        }
        if (outModel.featureCount == 0u) outModel.featureCount = kFeatureCount;
        if (outModel.hiddenWidth == 0u) outModel.hiddenWidth = outModel.settings.hiddenWidth;
        if (outModel.hiddenWidth2 == 0u) outModel.hiddenWidth2 = outModel.settings.hiddenWidth2;

        if (!parseScalarVector(directoryPath / "input_mean.csv", outModel.inputMean) ||
            !parseScalarVector(directoryPath / "input_std.csv", outModel.inputStd) ||
            !parseScalarVector(directoryPath / "target_mean.csv", outModel.targetMean) ||
            !parseScalarVector(directoryPath / "target_std.csv", outModel.targetStd) ||
            !parseScalarVector(directoryPath / "hidden_bias.csv", outModel.hiddenBias) ||
            !parseScalarVector(directoryPath / "hidden_bias2.csv", outModel.hiddenBias2) ||
            !parseScalarVector(directoryPath / "output_bias.csv", outModel.outputBias) ||
            !parseMatrix(directoryPath / "hidden_weights.csv", outModel.hiddenWeights, outModel.hiddenWidth, outModel.featureCount) ||
            !parseMatrix(directoryPath / "hidden_weights2.csv", outModel.hiddenWeights2, outModel.hiddenWidth2, outModel.hiddenWidth) ||
            !parseMatrix(directoryPath / "output_weights.csv", outModel.outputWeights, kTargetCount, (outModel.hiddenWidth2 > 0u ? outModel.hiddenWidth2 : outModel.hiddenWidth)))
        {
            return false;
        }

        if (!IsTemporalGuideNetworkReady(outModel))
        {
            outError = "Temporal CSV import is incomplete or inconsistent.";
            return false;
        }
        outError.clear();
        return true;
    }

    bool ExportTemporalGuideTrainingDatasetCsv(const std::filesystem::path& filePath,
                                               const std::vector<Vec3>& restGuidePoints,
                                               const std::vector<Vec3>& inputFrames,
                                               const std::vector<Vec3>& targetFrames,
                                               uint32_t frameCount,
                                               uint32_t guideStrandCount,
                                               uint32_t particlesPerStrand,
                                               bool lockRootPoints,
                                               std::string& outError)
    {
        std::vector<TemporalTrainingSample> samples;
        BuildSamples(restGuidePoints,
                     inputFrames,
                     targetFrames,
                     frameCount,
                     guideStrandCount,
                     particlesPerStrand,
                     lockRootPoints,
                     samples,
                     outError);
        if (!outError.empty())
        {
            return false;
        }

        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to write temporal training dataset CSV: " + filePath.string();
            return false;
        }
        out.setf(std::ios::fixed);
        out.precision(9);
        out << "currentDeltaX,currentDeltaY,currentDeltaZ,velocityX,velocityY,velocityZ,accelX,accelY,accelZ,pointT,strandT,rootDistance,displacementMagnitude,targetX,targetY,targetZ,lockOutput\n";
        for (const TemporalTrainingSample& sample : samples)
        {
            for (uint32_t i = 0u; i < kFeatureCount; ++i)
            {
                if (i > 0u) out << ',';
                out << sample.features[i];
            }
            out << ',' << sample.target[0] << ',' << sample.target[1] << ',' << sample.target[2] << ',' << (sample.lockOutput ? 1 : 0) << "\n";
        }
        outError.clear();
        return true;
    }
}
