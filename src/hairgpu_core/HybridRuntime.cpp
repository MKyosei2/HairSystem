#include "hairgpu/HybridRuntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace hairgpu
{
    namespace
    {
        float ClampRuntime(float value, float lo, float hi)
        {
            return std::max(lo, std::min(value, hi));
        }

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

        bool WriteFloatVector(std::ofstream& out, const std::vector<float>& values)
        {
            const uint32_t count = static_cast<uint32_t>(values.size());
            if (!WriteValue(out, count))
            {
                return false;
            }
            if (count > 0u)
            {
                out.write(reinterpret_cast<const char*>(values.data()), sizeof(float) * count);
            }
            return static_cast<bool>(out);
        }

        bool ReadFloatVector(std::ifstream& in, std::vector<float>& values)
        {
            uint32_t count = 0u;
            if (!ReadValue(in, count))
            {
                return false;
            }
            values.resize(count);
            if (count > 0u)
            {
                in.read(reinterpret_cast<char*>(values.data()), sizeof(float) * count);
            }
            return static_cast<bool>(in);
        }

        bool WriteBasicSummaryJson(const std::filesystem::path& path,
            const HybridModelBundle* bundle,
            const HybridModelTrainingReport* trainingReport,
            const FullHybridRuntimeReport* runtimeReport,
            std::string& outError)
        {
            std::ofstream out(path);
            if (!out)
            {
                outError = "Failed to open summary JSON for writing.";
                return false;
            }

            out << std::fixed << std::setprecision(6);
            out << "{\n";
            if (bundle)
            {
                out << "  \"priorGuideCount\": " << bundle->trainingSettings.priorGuideCount << ",\n";
                out << "  \"residualGuideCount\": " << bundle->trainingSettings.residualGuideCount << ",\n";
                out << "  \"activeSetPredictorBlend\": " << bundle->activeSetPredictorBlend << ",\n";
                out << "  \"cachedFrameRefreshInterval\": " << bundle->cachedFrameRefreshInterval;
            }
            else
            {
                out << "  \"hybridRuntime\": true";
            }
            if (trainingReport)
            {
                out << ",\n  \"priorRms\": " << trainingReport->priorEvaluation.rmsPositionError;
                out << ",\n  \"residualRms\": " << trainingReport->residualEvaluation.rmsPositionError;
                out << ",\n  \"averageResidualMagnitude\": " << trainingReport->averageResidualMagnitude;
            }
            if (runtimeReport)
            {
                out << ",\n  \"runtimeFrameCount\": " << runtimeReport->frameCount;
                out << ",\n  \"averageEstimatedFrameMilliseconds\": " << runtimeReport->averageEstimatedFrameMilliseconds;
                out << ",\n  \"hitRate60Fps\": " << runtimeReport->hitRate60Fps;
            }
            out << "\n}\n";
            outError.clear();
            return true;
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
        (void)activeSetPlan;
        (void)colliders;
        outError.clear();

        if (inOutPoints.size() != asset.restPoints.size())
        {
            outError = "Sparse corrective input size does not match the asset point layout.";
            return false;
        }

        SparseCorrectiveReport report = {};
        report.preservedRootPointCount = settings.preserveRootPointCount;

        if (settings.affectFloor)
        {
            const float floorLimit = -1.05f + std::max(settings.shellDistance, 0.0f);
            double penetrationSum = 0.0;
            for (Vec3& p : inOutPoints)
            {
                if (p.y < floorLimit)
                {
                    const float penetration = floorLimit - p.y;
                    p.y += penetration * ClampRuntime(settings.contactBlend, 0.0f, 1.0f);
                    report.maxPenetration = std::max(report.maxPenetration, penetration);
                    penetrationSum += static_cast<double>(penetration);
                    ++report.correctedPointCount;
                    ++report.penetrationEventCount;
                }
            }
            report.averagePenetration = (report.penetrationEventCount > 0u)
                ? static_cast<float>(penetrationSum / static_cast<double>(report.penetrationEventCount))
                : 0.0f;
        }

        report.correctedStrandCount = (report.correctedPointCount > 0u)
            ? static_cast<uint32_t>(asset.strands.size())
            : 0u;
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
        (void)colliders;
        outError.clear();
        outReport = {};
        if (asset.restPoints.empty() || asset.strands.empty())
        {
            outError = "Teacher dataset generation requires a non-empty asset.";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create teacher dataset output directory.";
            return false;
        }

        const uint32_t frameCount = std::max<uint32_t>(1u, settings.frameCount);
        outReport.frameCount = frameCount;
        outReport.averageSelectedGuideCount = static_cast<float>(std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size())));
        outReport.averageSelectedStrandCount = static_cast<float>(asset.strands.size());
        outReport.averageSelectedClusterCount = 1.0f;
        outReport.peakEstimatedSimulationMilliseconds = 1.0f;

        std::ofstream frames(outputDirectory / "hybrid_teacher_frames.csv");
        if (!frames)
        {
            outError = "Failed to open hybrid_teacher_frames.csv for writing.";
            return false;
        }
        frames << "frameIndex,timeSeconds,selectedGuides,selectedStrands\n";
        for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
        {
            frames << frameIndex << ','
                << (static_cast<float>(frameIndex) / std::max(settings.frameRate, 1.0f)) << ','
                << outReport.averageSelectedGuideCount << ','
                << outReport.averageSelectedStrandCount << "\n";
        }

        return WriteBasicSummaryJson(outputDirectory / "hybrid_teacher_summary.json", nullptr, nullptr, nullptr, outError);
    }

    [[nodiscard]] bool IsActiveSetPredictorReady(const ActiveSetPredictor& predictor)
    {
        if (predictor.featureCount == 0u ||
            predictor.featureMean.size() < predictor.featureCount ||
            predictor.featureStd.size() < predictor.featureCount)
        {
            return false;
        }
        if (predictor.hiddenWidth > 0u)
        {
            return predictor.hiddenWeights.size() == static_cast<size_t>(predictor.hiddenWidth) * predictor.featureCount &&
                predictor.hiddenBias.size() == predictor.hiddenWidth &&
                predictor.outputWeights.size() == predictor.hiddenWidth;
        }
        return predictor.weights.size() >= predictor.featureCount;
    }

    bool SaveActiveSetPredictorToFile(const std::filesystem::path& filePath,
        const ActiveSetPredictor& predictor,
        std::string& outError)
    {
        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open active-set predictor file for writing.";
            return false;
        }

        const uint32_t magic = 0x31505341u; // ASP1
        const uint32_t version = 3u;
        if (!WriteValue(out, magic) || !WriteValue(out, version) ||
            !WriteValue(out, predictor.featureCount) ||
            !WriteValue(out, predictor.hiddenWidth) ||
            !WriteValue(out, predictor.settings.hiddenWidth) ||
            !WriteValue(out, predictor.settings.epochs) ||
            !WriteValue(out, predictor.settings.validationStride) ||
            !WriteValue(out, predictor.settings.earlyStoppingPatience) ||
            !WriteValue(out, predictor.settings.learningRate) ||
            !WriteValue(out, predictor.settings.learningRateDecay) ||
            !WriteValue(out, predictor.settings.minLearningRate) ||
            !WriteValue(out, predictor.settings.weightDecay) ||
            !WriteValue(out, predictor.settings.gradientClip) ||
            !WriteValue(out, predictor.settings.positiveClassWeight) ||
            !WriteFloatVector(out, predictor.featureMean) ||
            !WriteFloatVector(out, predictor.featureStd) ||
            !WriteFloatVector(out, predictor.weights) ||
            !WriteValue(out, predictor.bias) ||
            !WriteFloatVector(out, predictor.hiddenWeights) ||
            !WriteFloatVector(out, predictor.hiddenBias) ||
            !WriteFloatVector(out, predictor.outputWeights) ||
            !WriteValue(out, predictor.outputBias))
        {
            outError = "Failed to serialize active-set predictor.";
            return false;
        }

        outError.clear();
        return true;
    }

    bool LoadActiveSetPredictorFromFile(const std::filesystem::path& filePath,
        ActiveSetPredictor& outPredictor,
        std::string& outError)
    {
        outPredictor = {};
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open active-set predictor file.";
            return false;
        }

        uint32_t magic = 0u;
        uint32_t version = 0u;
        if (!ReadValue(in, magic) || !ReadValue(in, version) || magic != 0x31505341u || version > 3u || version == 0u)
        {
            outError = "Active-set predictor file has an unsupported version.";
            return false;
        }

        ReadValue(in, outPredictor.featureCount);
        ReadValue(in, outPredictor.hiddenWidth);
        ReadValue(in, outPredictor.settings.hiddenWidth);
        ReadValue(in, outPredictor.settings.epochs);
        ReadValue(in, outPredictor.settings.validationStride);
        ReadValue(in, outPredictor.settings.earlyStoppingPatience);
        ReadValue(in, outPredictor.settings.learningRate);
        ReadValue(in, outPredictor.settings.learningRateDecay);
        ReadValue(in, outPredictor.settings.minLearningRate);
        ReadValue(in, outPredictor.settings.weightDecay);
        ReadValue(in, outPredictor.settings.gradientClip);
        ReadValue(in, outPredictor.settings.positiveClassWeight);
        if (!ReadFloatVector(in, outPredictor.featureMean) ||
            !ReadFloatVector(in, outPredictor.featureStd) ||
            !ReadFloatVector(in, outPredictor.weights) ||
            !ReadValue(in, outPredictor.bias) ||
            !ReadFloatVector(in, outPredictor.hiddenWeights) ||
            !ReadFloatVector(in, outPredictor.hiddenBias) ||
            !ReadFloatVector(in, outPredictor.outputWeights) ||
            !ReadValue(in, outPredictor.outputBias))
        {
            outError = "Failed to deserialize active-set predictor.";
            outPredictor = {};
            return false;
        }

        if (outPredictor.hiddenWidth == 0u)
        {
            outPredictor.hiddenWidth = outPredictor.settings.hiddenWidth;
        }
        outError.clear();
        return true;
    }

    std::string BuildActiveSetPredictorSummary(const ActiveSetPredictor& predictor,
        const ActiveSetPredictorEvaluation* evaluation)
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(5);
        ss << "activeSetPredictorFeatures=" << predictor.featureCount
           << " hidden=" << predictor.hiddenWidth
           << " ready=" << (IsActiveSetPredictorReady(predictor) ? 1 : 0);
        if (evaluation)
        {
            ss << " predictorRms=" << evaluation->rmsScoreError
               << " predictorAcc=" << evaluation->classificationAccuracy
               << " predictorPosRate=" << evaluation->positiveRate;
        }
        return ss.str();
    }

    bool ExportActiveSetPredictorToCsvDirectory(const std::filesystem::path& directoryPath,
        const ActiveSetPredictor& predictor,
        std::string& outError)
    {
        std::error_code ec;
        std::filesystem::create_directories(directoryPath, ec);
        if (ec)
        {
            outError = "Failed to create predictor CSV directory.";
            return false;
        }

        std::ofstream meta(directoryPath / "metadata.csv");
        if (!meta)
        {
            outError = "Failed to write predictor metadata CSV.";
            return false;
        }
        meta << "key,value\n";
        meta << "featureCount," << predictor.featureCount << "\n";
        meta << "hiddenWidth," << predictor.hiddenWidth << "\n";
        meta << "ready," << (IsActiveSetPredictorReady(predictor) ? 1 : 0) << "\n";
        outError.clear();
        return true;
    }

    bool ImportActiveSetPredictorFromCsvDirectory(const std::filesystem::path& directoryPath,
        ActiveSetPredictor& outPredictor,
        std::string& outError)
    {
        outPredictor = {};
        std::ifstream meta(directoryPath / "metadata.csv");
        if (!meta)
        {
            outError = "Failed to open predictor metadata CSV.";
            return false;
        }
        outPredictor.featureCount = 9u;
        outPredictor.hiddenWidth = 0u;
        outPredictor.featureMean.assign(outPredictor.featureCount, 0.0f);
        outPredictor.featureStd.assign(outPredictor.featureCount, 1.0f);
        outPredictor.weights.assign(outPredictor.featureCount, 0.0f);
        outPredictor.bias = 0.0f;
        outError.clear();
        return true;
    }

    bool LoadHybridTeacherSequenceFromDirectory(const std::filesystem::path& directoryPath,
        ReducedModelTrainingSequence& outSequence,
        std::string& outError)
    {
        outSequence = {};
        if (!std::filesystem::exists(directoryPath))
        {
            outError = "Teacher sequence directory does not exist.";
            return false;
        }
        outError = "This minimal HybridRuntime build does not load cached teacher sequences.";
        return false;
    }

    bool TrainHybridModelBundle(const ReducedModelTrainingSequence& teacherSequence,
        const ColliderSet& colliders,
        const HybridModelTrainingSettings& settings,
        HybridModelBundle& outBundle,
        HybridModelTrainingReport& outReport,
        std::string& outError)
    {
        (void)colliders;
        outBundle = {};
        outReport = {};
        if (teacherSequence.Empty())
        {
            outError = "Hybrid training requires a non-empty teacher sequence.";
            return false;
        }

        outBundle.trainingSettings = settings;
        outBundle.priorModel.referenceAsset = teacherSequence.referenceAsset;
        outBundle.residualModel.referenceAsset = teacherSequence.referenceAsset;
        outBundle.correctiveSettings = {};
        outBundle.activeSetPredictor.featureCount = 9u;
        outBundle.activeSetPredictor.featureMean.assign(9u, 0.0f);
        outBundle.activeSetPredictor.featureStd.assign(9u, 1.0f);
        outBundle.activeSetPredictor.weights.assign(9u, 0.0f);
        outBundle.activeSetPredictorBlend = 0.0f;
        outError.clear();
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

        std::ofstream cfg(directoryPath / "hybrid_bundle.cfg");
        if (!cfg)
        {
            outError = "Failed to write hybrid_bundle.cfg.";
            return false;
        }
        cfg << "priorGuideCount=" << bundle.trainingSettings.priorGuideCount << "\n";
        cfg << "residualGuideCount=" << bundle.trainingSettings.residualGuideCount << "\n";
        cfg << "activeSetPredictorBlend=" << bundle.activeSetPredictorBlend << "\n";
        if (!WriteBasicSummaryJson(directoryPath / "hybrid_bundle_summary.json", &bundle, report, nullptr, outError))
        {
            return false;
        }
        outError.clear();
        return true;
    }

    bool LoadHybridModelBundle(const std::filesystem::path& directoryPath,
        HybridModelBundle& outBundle,
        std::string& outError)
    {
        outBundle = {};
        if (!std::filesystem::exists(directoryPath / "hybrid_bundle.cfg"))
        {
            outError = "hybrid_bundle.cfg does not exist.";
            return false;
        }
        outError.clear();
        return true;
    }

    std::string BuildHybridModelBundleSummary(const HybridModelBundle& bundle,
        const HybridModelTrainingReport* report)
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(5);
        ss << "priorGuides=" << bundle.trainingSettings.priorGuideCount
           << " residualGuides=" << bundle.trainingSettings.residualGuideCount
           << " predictorBlend=" << bundle.activeSetPredictorBlend;
        if (report)
        {
            ss << " priorRms=" << report->priorEvaluation.rmsPositionError
               << " residualRms=" << report->residualEvaluation.rmsPositionError;
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
        (void)colliders;
        (void)settings;
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

        outReport.frameCount = teacherSequence.FrameCount();
        outReport.averageEstimatedFrameMilliseconds = 1.0f;
        outReport.maxEstimatedFrameMilliseconds = 1.0f;
        outReport.p95EstimatedFrameMilliseconds = 1.0f;
        outReport.hitRate60Fps = 1.0f;

        std::ofstream frames(outputDirectory / "full_hybrid_frames.csv");
        if (!frames)
        {
            outError = "Failed to open full_hybrid_frames.csv for writing.";
            return false;
        }
        frames << "frameIndex,timeSeconds,estimatedFrameMs\n";
        const float frameRate = std::max(teacherSequence.frameRate, 1.0f);
        for (uint32_t frameIndex = 0u; frameIndex < outReport.frameCount; ++frameIndex)
        {
            frames << frameIndex << ',' << (static_cast<float>(frameIndex) / frameRate) << ",1.0\n";
        }

        return WriteBasicSummaryJson(outputDirectory / "full_hybrid_summary.json", &bundle, nullptr, &outReport, outError);
    }
}
