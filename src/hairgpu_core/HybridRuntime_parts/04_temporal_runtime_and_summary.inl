// HybridRuntime.cpp ordered part 04: temporal_runtime_and_summary
// This file is included by ../HybridRuntime.cpp; do not compile it directly.

    std::string BuildActiveSetPredictorSummary(const ActiveSetPredictor& predictor,
                                               const ActiveSetPredictorEvaluation* evaluation)
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(5);
        ss << "activeSetPredictorFeatures=" << predictor.featureCount << " hidden=" << predictor.hiddenWidth
           << " valStride=" << predictor.settings.validationStride
           << " patience=" << predictor.settings.earlyStoppingPatience;
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
        if (!IsActiveSetPredictorReady(predictor))
        {
            outError = "Active-set predictor is not initialized.";
            return false;
        }
        std::error_code ec;
        std::filesystem::create_directories(directoryPath, ec);
        if (ec)
        {
            outError = "Failed to create predictor CSV directory: " + directoryPath.string();
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
        meta << "settings.hiddenWidth," << predictor.settings.hiddenWidth << "\n";
        meta << "settings.epochs," << predictor.settings.epochs << "\n";
        meta << "settings.learningRate," << predictor.settings.learningRate << "\n";
        meta << "settings.weightDecay," << predictor.settings.weightDecay << "\n";
        meta << "settings.gradientClip," << predictor.settings.gradientClip << "\n";
        meta << "bias," << predictor.bias << "\n";
        meta << "outputBias," << predictor.outputBias << "\n";

        std::ofstream weights(directoryPath / "weights.csv");
        if (!weights)
        {
            outError = "Failed to write predictor weights CSV.";
            return false;
        }
        weights << "index,mean,std,weight\n";
        weights.setf(std::ios::fixed);
        weights.precision(9);
        for (uint32_t i = 0u; i < predictor.featureCount; ++i)
        {
            const float mean = (i < predictor.featureMean.size()) ? predictor.featureMean[i] : 0.0f;
            const float stdDev = (i < predictor.featureStd.size()) ? predictor.featureStd[i] : 1.0f;
            const float weight = (i < predictor.weights.size()) ? predictor.weights[i] : 0.0f;
            weights << i << ',' << mean << ',' << stdDev << ',' << weight << "\n";
        }
        std::ofstream hiddenWeights(directoryPath / "hidden_weights.csv");
        if (!hiddenWeights)
        {
            outError = "Failed to write predictor hidden weights CSV.";
            return false;
        }
        hiddenWeights.setf(std::ios::fixed);
        hiddenWeights.precision(9);
        for (uint32_t h = 0u; h < predictor.hiddenWidth; ++h)
        {
            hiddenWeights << h;
            const size_t rowOffset = static_cast<size_t>(h) * predictor.featureCount;
            for (uint32_t i = 0u; i < predictor.featureCount; ++i)
            {
                hiddenWeights << ',' << predictor.hiddenWeights[rowOffset + i];
            }
            hiddenWeights << ',' << predictor.hiddenBias[h] << "\n";
        }
        std::ofstream outputWeights(directoryPath / "output_weights.csv");
        if (!outputWeights)
        {
            outError = "Failed to write predictor output weights CSV.";
            return false;
        }
        outputWeights << "hiddenIndex,weight\n";
        for (uint32_t h = 0u; h < predictor.hiddenWidth; ++h)
        {
            outputWeights << h << ',' << predictor.outputWeights[h] << "\n";
        }

        std::ofstream readme(directoryPath / "README.txt");
        if (readme)
        {
            readme << "Active-set predictor CSV interchange\n";
            readme << "Edit weights.csv, hidden_weights.csv, output_weights.csv and metadata.csv, then re-import with active-set-predictor-import-csv.\n";
        }
        outError.clear();
        return true;
    }

    bool ImportActiveSetPredictorFromCsvDirectory(const std::filesystem::path& directoryPath,
                                                  ActiveSetPredictor& outPredictor,
                                                  std::string& outError)
    {
        outPredictor = {};
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

        std::ifstream meta(directoryPath / "metadata.csv");
        if (!meta)
        {
            outError = "Failed to open predictor metadata CSV.";
            return false;
        }
        std::string line;
        if (!std::getline(meta, line))
        {
            outError = "Predictor metadata CSV is empty.";
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
            if (key == "featureCount") parseUInt(value, outPredictor.featureCount);
            else if (key == "hiddenWidth") parseUInt(value, outPredictor.hiddenWidth);
            else if (key == "settings.hiddenWidth") parseUInt(value, outPredictor.settings.hiddenWidth);
            else if (key == "settings.epochs") parseUInt(value, outPredictor.settings.epochs);
            else if (key == "settings.validationStride") parseUInt(value, outPredictor.settings.validationStride);
            else if (key == "settings.earlyStoppingPatience") parseUInt(value, outPredictor.settings.earlyStoppingPatience);
            else if (key == "settings.learningRate") parseFloat(value, outPredictor.settings.learningRate);
            else if (key == "settings.learningRateDecay") parseFloat(value, outPredictor.settings.learningRateDecay);
            else if (key == "settings.minLearningRate") parseFloat(value, outPredictor.settings.minLearningRate);
            else if (key == "settings.weightDecay") parseFloat(value, outPredictor.settings.weightDecay);
            else if (key == "settings.gradientClip") parseFloat(value, outPredictor.settings.gradientClip);
            else if (key == "settings.positiveClassWeight") parseFloat(value, outPredictor.settings.positiveClassWeight);
            else if (key == "bias") parseFloat(value, outPredictor.bias);
            else if (key == "outputBias") parseFloat(value, outPredictor.outputBias);
        }

        std::ifstream weights(directoryPath / "weights.csv");
        if (!weights)
        {
            outError = "Failed to open predictor weights CSV.";
            return false;
        }
        if (!std::getline(weights, line))
        {
            outError = "Predictor weights CSV is empty.";
            return false;
        }
        outPredictor.featureMean.clear();
        outPredictor.featureStd.clear();
        outPredictor.weights.clear();
        while (std::getline(weights, line))
        {
            line = trim(line);
            if (line.empty()) continue;
            const std::vector<std::string> tokens = splitCsv(line);
            if (tokens.size() < 4u)
            {
                outError = "Malformed predictor weights row.";
                return false;
            }
            float mean = 0.0f;
            float stdDev = 1.0f;
            float weight = 0.0f;
            if (!parseFloat(tokens[1], mean) || !parseFloat(tokens[2], stdDev) || !parseFloat(tokens[3], weight))
            {
                outError = "Malformed predictor weight values.";
                return false;
            }
            outPredictor.featureMean.push_back(mean);
            outPredictor.featureStd.push_back(std::max(stdDev, 1.0e-4f));
            outPredictor.weights.push_back(weight);
        }
        if (outPredictor.featureCount == 0u)
        {
            outPredictor.featureCount = static_cast<uint32_t>(outPredictor.weights.size());
        }
        if (outPredictor.hiddenWidth == 0u)
        {
            outPredictor.hiddenWidth = outPredictor.settings.hiddenWidth;
        }
        outPredictor.hiddenWeights.assign(static_cast<size_t>(outPredictor.hiddenWidth) * outPredictor.featureCount, 0.0f);
        outPredictor.hiddenBias.assign(outPredictor.hiddenWidth, 0.0f);
        outPredictor.outputWeights.assign(outPredictor.hiddenWidth, 0.0f);

        std::ifstream hiddenIn(directoryPath / "hidden_weights.csv");
        if (hiddenIn)
        {
            while (std::getline(hiddenIn, line))
            {
                line = trim(line);
                if (line.empty()) continue;
                const std::vector<std::string> tokens = splitCsv(line);
                if (tokens.size() != outPredictor.featureCount + 2u)
                {
                    continue;
                }
                uint32_t row = 0u;
                if (!parseUInt(tokens[0], row) || row >= outPredictor.hiddenWidth)
                {
                    continue;
                }
                const size_t rowOffset = static_cast<size_t>(row) * outPredictor.featureCount;
                for (uint32_t i = 0u; i < outPredictor.featureCount; ++i)
                {
                    parseFloat(tokens[i + 1u], outPredictor.hiddenWeights[rowOffset + i]);
                }
                parseFloat(tokens.back(), outPredictor.hiddenBias[row]);
            }
        }

        std::ifstream outputIn(directoryPath / "output_weights.csv");
        if (outputIn)
        {
            std::getline(outputIn, line);
            while (std::getline(outputIn, line))
            {
                line = trim(line);
                if (line.empty()) continue;
                const std::vector<std::string> tokens = splitCsv(line);
                if (tokens.size() < 2u)
                {
                    continue;
                }
                uint32_t row = 0u;
                if (!parseUInt(tokens[0], row) || row >= outPredictor.hiddenWidth)
                {
                    continue;
                }
                parseFloat(tokens[1], outPredictor.outputWeights[row]);
            }
        }

        if (!IsActiveSetPredictorReady(outPredictor))
        {
            outError = "Predictor CSV import is incomplete or inconsistent.";
            return false;
        }
        outError.clear();
        return true;
    }

    bool TrainHybridModelBundle(const ReducedModelTrainingSequence& teacherSequence,
                                const ColliderSet& colliders,
                                const HybridModelTrainingSettings& settings,
                                HybridModelBundle& outBundle,
                                HybridModelTrainingReport& outReport,
                                std::string& outError)
    {
        outError.clear();
        outBundle = {};
        outReport = {};
        if (teacherSequence.Empty())
        {
            outError = "Hybrid training requires a non-empty teacher sequence.";
            return false;
        }

        ReducedModelTrainingSequence priorSequence;
        BuildSmoothedPriorSequence(teacherSequence, settings.priorGuideSmoothingAlpha, priorSequence);

        float averageResidualMagnitude = 0.0f;
        float maxResidualMagnitude = 0.0f;
        ReducedModelTrainingSequence residualSequence;
        BuildResidualSequence(teacherSequence, priorSequence, residualSequence, &averageResidualMagnitude, &maxResidualMagnitude);

        ReducedModelSettings priorSettings = {};
        priorSettings.guideCount = settings.priorGuideCount;
        priorSettings.maxInfluencesPerFollower = settings.maxInfluencesPerFollower;
        priorSettings.regularization = settings.regularization;
        priorSettings.validationStride = settings.validationStride;
        priorSettings.useGuideDisplacements = true;
        priorSettings.rootLockBlend = 1.0f;
        priorSettings.enableScalpCollision = settings.enableScalpCollision;

        ReducedModelSettings residualSettings = priorSettings;
        residualSettings.guideCount = std::max<uint32_t>(settings.residualGuideCount, settings.priorGuideCount);
        residualSettings.rootLockBlend = 0.55f;
        residualSettings.lengthProjectionBlend = 0.35f;
        residualSettings.enableScalpCollision = false;

        if (!TrainReducedModel(priorSequence, priorSettings, outBundle.priorModel, outError))
        {
            return false;
        }
        if (!TrainReducedModel(residualSequence, residualSettings, outBundle.residualModel, outError))
        {
            return false;
        }
        if (!EvaluateReducedModel(outBundle.priorModel, priorSequence, outReport.priorEvaluation, outError))
        {
            return false;
        }
        if (!EvaluateReducedModel(outBundle.residualModel, residualSequence, outReport.residualEvaluation, outError))
        {
            return false;
        }

        std::vector<Vec3> priorTemporalInputFrames;
        std::vector<Vec3> priorTemporalTargetFrames;
        BuildPriorTemporalTrainingSequences(outBundle.priorModel,
                                            teacherSequence,
                                            priorSequence,
                                            priorTemporalInputFrames,
                                            priorTemporalTargetFrames);
        TemporalGuideNetworkSettings priorTemporalSettings = {};
        priorTemporalSettings.hiddenWidth = std::max<uint32_t>(24u, settings.priorGuideCount * 2u);
        priorTemporalSettings.hiddenWidth2 = std::max<uint32_t>(12u, settings.priorGuideCount);
        priorTemporalSettings.epochs = std::max<uint32_t>(120u, 56u + settings.validationStride * 10u);
        priorTemporalSettings.learningRate = 1.8e-3f;
        priorTemporalSettings.weightDecay = std::max(5.0e-6f, settings.regularization * 0.2f);
        priorTemporalSettings.gradientClip = 0.28f;
        priorTemporalSettings.lockRootPoints = true;
        std::vector<Vec3> restPriorGuidePoints;
        BuildRestGuideFramePointsForModel(outBundle.priorModel, restPriorGuidePoints);
        if (!TrainTemporalGuideNetwork(restPriorGuidePoints,
                                       priorTemporalInputFrames,
                                       priorTemporalTargetFrames,
                                       teacherSequence.FrameCount(),
                                       static_cast<uint32_t>(outBundle.priorModel.selectedGuideStrandIndices.size()),
                                       outBundle.priorModel.referenceAsset.particlesPerStrand,
                                       priorTemporalSettings,
                                       outBundle.priorTemporalNetwork,
                                       &outReport.priorTemporalEvaluation,
                                       outError))
        {
            return false;
        }

        std::vector<Vec3> residualTemporalInputFrames;
        std::vector<Vec3> residualTemporalTargetFrames;
        BuildResidualTemporalTrainingSequences(outBundle.residualModel,
                                               teacherSequence,
                                               priorSequence,
                                               settings.residualGuideGain,
                                               residualTemporalInputFrames,
                                               residualTemporalTargetFrames);
        TemporalGuideNetworkSettings residualTemporalSettings = {};
        residualTemporalSettings.hiddenWidth = std::max<uint32_t>(28u, settings.residualGuideCount * 2u);
        residualTemporalSettings.hiddenWidth2 = std::max<uint32_t>(14u, settings.residualGuideCount);
        residualTemporalSettings.epochs = std::max<uint32_t>(128u, 60u + settings.validationStride * 10u);
        residualTemporalSettings.learningRate = 1.6e-3f;
        residualTemporalSettings.weightDecay = std::max(5.0e-6f, settings.regularization * 0.25f);
        residualTemporalSettings.gradientClip = 0.32f;
        residualTemporalSettings.lockRootPoints = true;
        std::vector<Vec3> restResidualGuidePoints;
        BuildRestGuideFramePointsForModel(outBundle.residualModel, restResidualGuidePoints);
        if (!TrainTemporalGuideNetwork(restResidualGuidePoints,
                                       residualTemporalInputFrames,
                                       residualTemporalTargetFrames,
                                       teacherSequence.FrameCount(),
                                       static_cast<uint32_t>(outBundle.residualModel.selectedGuideStrandIndices.size()),
                                       outBundle.residualModel.referenceAsset.particlesPerStrand,
                                       residualTemporalSettings,
                                       outBundle.residualTemporalNetwork,
                                       &outReport.residualTemporalEvaluation,
                                       outError))
        {
            return false;
        }

        outReport.averageResidualMagnitude = averageResidualMagnitude;
        outReport.maxResidualMagnitude = maxResidualMagnitude;

        outBundle.trainingSettings = settings;
        outBundle.clusterSettings.targetGuideCountPerCluster = std::max<uint32_t>(4u, std::min<uint32_t>(8u, settings.residualGuideCount / 3u + 1u));
        outBundle.clusterSettings.maxGuideRootDistance = 0.09f;
        outBundle.scoringSettings.motionWeight = 0.22f;
        outBundle.scoringSettings.displacementWeight = 0.20f;
        outBundle.scoringSettings.contactWeight = Clamp(0.24f + averageResidualMagnitude * 18.0f, 0.22f, 0.40f);
        outBundle.scoringSettings.visibilityWeight = 0.12f;
        outBundle.scoringSettings.silhouetteWeight = 0.13f;
        outBundle.scoringSettings.clusterSizeWeight = 0.05f;
        outBundle.scoringSettings.uncertaintyWeight = Clamp(0.12f + outReport.priorEvaluation.rmsPositionError * 12.0f + outReport.residualEvaluation.rmsPositionError * 6.0f, 0.12f, 0.36f);
        outBundle.budget.maxActiveClusters = std::max<uint32_t>(6u, settings.residualGuideCount / 4u);
        outBundle.budget.maxActiveGuides = std::max<uint32_t>(32u, settings.residualGuideCount * 2u);
        outBundle.budget.promoteScoreThreshold = Clamp(0.52f - averageResidualMagnitude * 4.0f, 0.30f, 0.60f);
        outBundle.budget.targetFrameMilliseconds = 8.0f;
        outBundle.budget.minSolverIterations = 2u;
        outBundle.budget.maxSolverIterations = 8u;
        outBundle.budget.memoryBudgetMegabytes = 32768.0f;
        outBundle.budget.estimatedFixedMemoryMegabytes = 768.0f;
        outBundle.budget.estimatedClusterMemoryMegabytes = 12.0f;
        outBundle.budget.enforceEditCoverage = true;
        outBundle.budget.mandatoryEditThreshold = 0.55f;
        outBundle.correctiveSettings = {};
        outBundle.correctiveSettings.contactBlend = Clamp(0.82f + averageResidualMagnitude * 4.0f, 0.82f, 0.95f);
        outBundle.correctiveSettings.stretchBlend = 0.40f;
        outBundle.correctiveSettings.bendBlend = 0.18f;
        outBundle.correctiveSettings.iterations = 2u;
        outBundle.uncertaintyGain = Clamp(1.0f + outReport.priorEvaluation.rmsPositionError * 25.0f, 0.8f, 2.5f);
        outBundle.contactGain = Clamp(1.0f + averageResidualMagnitude * 12.0f, 0.8f, 2.0f);
        outBundle.penetrationGain = Clamp(1.0f + averageResidualMagnitude * 20.0f + (HasAnyCollider(colliders) ? 0.15f : 0.0f), 0.9f, 2.8f);
        outBundle.temporalCoherenceGain = Clamp(1.0f + outReport.priorEvaluation.rmsPositionError * 10.0f, 0.8f, 1.8f);
        outBundle.uncertaintySmoothingAlpha = Clamp(0.28f + outReport.residualEvaluation.rmsPositionError * 18.0f, 0.20f, 0.65f);
        outBundle.priorVelocityPredictionGain = Clamp(0.10f + averageResidualMagnitude * 18.0f - settings.priorGuideSmoothingAlpha * 0.20f,
                                                      0.0f,
                                                      0.65f);
        outBundle.residualVelocityPredictionGain = Clamp(0.18f + averageResidualMagnitude * 12.0f + outReport.residualEvaluation.rmsPositionError * 14.0f,
                                                         0.0f,
                                                         0.85f);
        outBundle.priorAccelerationPredictionGain = Clamp(outBundle.priorVelocityPredictionGain * 0.32f + outReport.priorEvaluation.rmsPositionError * 2.5f,
                                                          0.0f,
                                                          0.35f);
        outBundle.residualAccelerationPredictionGain = Clamp(outBundle.residualVelocityPredictionGain * 0.28f + outReport.residualEvaluation.rmsPositionError * 3.0f,
                                                             0.0f,
                                                             0.40f);
        outBundle.priorTemporalBlend = Clamp(0.45f + outReport.priorTemporalEvaluation.rmsPositionError * 8.0f,
                                             0.35f,
                                             0.85f);
        outBundle.residualTemporalBlend = Clamp(0.50f + outReport.residualTemporalEvaluation.rmsPositionError * 10.0f,
                                                0.40f,
                                                0.90f);
        outBundle.frozenStrandBlend = Clamp(0.88f + outReport.priorEvaluation.rmsPositionError * 4.0f,
                                            0.80f,
                                            0.98f);
        outBundle.frozenRootFollow = 1.0f;
        outBundle.cachedStrandBlend = Clamp(0.72f + averageResidualMagnitude * 6.0f + outReport.residualEvaluation.rmsPositionError * 5.0f,
                                            0.60f,
                                            0.95f);
        outBundle.cachedRootFollow = Clamp(0.90f + outReport.priorEvaluation.rmsPositionError * 3.5f,
                                           0.80f,
                                           1.0f);
        outBundle.cachedFrameRefreshInterval = std::max<uint32_t>(2u,
                                                                  std::min<uint32_t>(6u,
                                                                                     2u + settings.validationStride / 2u));
        outBundle.activeSetPredictorBlend = 0.0f;

        const ColliderSet predictorColliders = !HasAnyCollider(colliders)
            ? BuildFallbackTeacherColliders(teacherSequence.referenceAsset)
            : colliders;
        const std::vector<ActiveSetCluster> predictorClusters = BuildActiveSetClusters(teacherSequence.referenceAsset, outBundle.clusterSettings);
        if (!predictorClusters.empty())
        {
            std::vector<std::array<float, 9u>> predictorFeatureRows;
            std::vector<float> predictorTargets;
            std::vector<Vec3> restGuideRoots;
            BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherSequence.referenceAsset.restPoints, restGuideRoots);

            std::vector<Vec3> trainingRestPriorGuidePoints;
            std::vector<Vec3> trainingSmoothedPriorGuidePoints;
            std::vector<Vec3> trainingPreviousSmoothedPriorGuidePoints;
            std::vector<Vec3> trainingPreviousPreviousSmoothedPriorGuidePoints;
            std::vector<Vec3> trainingPreviousRawPriorGuidePoints;
            std::vector<Vec3> trainingPreviousPreviousRawPriorGuidePoints;
            BuildRestGuideFramePointsForModel(outBundle.priorModel, trainingRestPriorGuidePoints);
            trainingSmoothedPriorGuidePoints = trainingRestPriorGuidePoints;
            trainingPreviousSmoothedPriorGuidePoints = trainingSmoothedPriorGuidePoints;
            trainingPreviousPreviousSmoothedPriorGuidePoints = trainingPreviousSmoothedPriorGuidePoints;
            trainingPreviousRawPriorGuidePoints = trainingRestPriorGuidePoints;
            trainingPreviousPreviousRawPriorGuidePoints = trainingPreviousRawPriorGuidePoints;

            std::vector<Vec3> trainingRestResidualGuidePoints;
            std::vector<Vec3> trainingPreviousResidualGuidePoints;
            std::vector<Vec3> trainingPreviousPreviousResidualGuidePoints;
            std::vector<Vec3> trainingPreviousBaseResidualGuideInput;
            std::vector<Vec3> trainingPreviousPreviousBaseResidualGuideInput;
            BuildRestGuideFramePointsForModel(outBundle.residualModel, trainingRestResidualGuidePoints);
            trainingPreviousResidualGuidePoints = trainingRestResidualGuidePoints;
            trainingPreviousPreviousResidualGuidePoints = trainingPreviousResidualGuidePoints;
            trainingPreviousBaseResidualGuideInput = trainingRestResidualGuidePoints;
            trainingPreviousPreviousBaseResidualGuideInput = trainingPreviousBaseResidualGuideInput;

            std::vector<Vec3> trainingPreviousHybridGuideRoots = restGuideRoots;
            std::vector<float> trainingPreviousGuideUncertainty(trainingPreviousHybridGuideRoots.size(), 0.0f);
            const float trainingResidualBlend = outBundle.trainingSettings.residualBlend;
            const float runtimeFrameRate = std::max(teacherSequence.frameRate, 1.0f);
            for (uint32_t frameIndex = 0u; frameIndex < teacherSequence.FrameCount(); ++frameIndex)
            {
                std::vector<Vec3> teacherFrame;
                CopySequenceFrame(teacherSequence, frameIndex, teacherFrame);

                std::vector<Vec3> currentPriorGuidePoints;
                BuildGuideFramePointsForModel(outBundle.priorModel, teacherFrame, currentPriorGuidePoints);
                for (uint32_t i = 0u; i < trainingSmoothedPriorGuidePoints.size() && i < currentPriorGuidePoints.size(); ++i)
                {
                    trainingSmoothedPriorGuidePoints[i] = Lerp(trainingSmoothedPriorGuidePoints[i],
                                                               currentPriorGuidePoints[i],
                                                               Clamp(settings.priorGuideSmoothingAlpha, 0.01f, 1.0f));
                }
                const float priorVelocityPredictionGain = Clamp(outBundle.priorVelocityPredictionGain, 0.0f, 1.0f);
                const float priorAccelerationPredictionGain = EstimateSecondOrderPredictionGain(trainingPreviousPreviousSmoothedPriorGuidePoints,
                                                                                               trainingPreviousSmoothedPriorGuidePoints,
                                                                                               trainingSmoothedPriorGuidePoints,
                                                                                               priorVelocityPredictionGain,
                                                                                               outBundle.priorAccelerationPredictionGain,
                                                                                               0.50f);
                std::vector<Vec3> predictedPriorGuidePoints = PredictSecondOrderGuidePoints(
                    trainingSmoothedPriorGuidePoints,
                    trainingPreviousSmoothedPriorGuidePoints,
                    trainingPreviousPreviousSmoothedPriorGuidePoints,
                    priorVelocityPredictionGain,
                    priorAccelerationPredictionGain);
                if (frameIndex >= 2u && IsTemporalGuideNetworkReady(outBundle.priorTemporalNetwork))
                {
                    std::vector<Vec3> temporalPriorGuidePoints;
                    if (!PredictTemporalGuideFrame(outBundle.priorTemporalNetwork,
                                                   trainingRestPriorGuidePoints,
                                                   trainingPreviousPreviousRawPriorGuidePoints,
                                                   trainingPreviousRawPriorGuidePoints,
                                                   currentPriorGuidePoints,
                                                   temporalPriorGuidePoints,
                                                   outError))
                    {
                        return false;
                    }
                    BlendGuidePoints(predictedPriorGuidePoints,
                                     temporalPriorGuidePoints,
                                     outBundle.priorTemporalBlend);
                }
                HairAsset priorAsset;
                if (!ReconstructReducedModelFrame(outBundle.priorModel, predictedPriorGuidePoints, priorAsset, outError))
                {
                    return false;
                }

                std::vector<Vec3> currentResidualGuidePoints;
                BuildGuideFramePointsForModel(outBundle.residualModel, teacherFrame, currentResidualGuidePoints);
                std::vector<Vec3> priorResidualGuidePoints;
                BuildGuideFramePointsForModel(outBundle.residualModel, priorAsset.restPoints, priorResidualGuidePoints);
                const float residualVelocityPredictionGain = Clamp(outBundle.residualVelocityPredictionGain, 0.0f, 1.0f);
                const float residualAccelerationPredictionGain = EstimateSecondOrderPredictionGain(trainingPreviousPreviousResidualGuidePoints,
                                                                                                  trainingPreviousResidualGuidePoints,
                                                                                                  currentResidualGuidePoints,
                                                                                                  residualVelocityPredictionGain,
                                                                                                  outBundle.residualAccelerationPredictionGain,
                                                                                                  0.60f);
                std::vector<Vec3> baseResidualGuideInput = trainingRestResidualGuidePoints;
                for (uint32_t i = 0u; i < baseResidualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    baseResidualGuideInput[i] += (currentResidualGuidePoints[i] - priorResidualGuidePoints[i]) * outBundle.trainingSettings.residualGuideGain;
                }
                std::vector<Vec3> residualGuideInput = baseResidualGuideInput;
                for (uint32_t i = 0u; i < residualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    if (i < trainingPreviousResidualGuidePoints.size())
                    {
                        const Vec3 velocity = currentResidualGuidePoints[i] - trainingPreviousResidualGuidePoints[i];
                        residualGuideInput[i] += velocity * residualVelocityPredictionGain;
                    }
                    if (i < trainingPreviousResidualGuidePoints.size() && i < trainingPreviousPreviousResidualGuidePoints.size())
                    {
                        const Vec3 acceleration = currentResidualGuidePoints[i] - trainingPreviousResidualGuidePoints[i] * 2.0f + trainingPreviousPreviousResidualGuidePoints[i];
                        residualGuideInput[i] += acceleration * residualAccelerationPredictionGain;
                    }
                }
                if (frameIndex >= 2u && IsTemporalGuideNetworkReady(outBundle.residualTemporalNetwork))
                {
                    std::vector<Vec3> temporalResidualGuidePoints;
                    if (!PredictTemporalGuideFrame(outBundle.residualTemporalNetwork,
                                                   trainingRestResidualGuidePoints,
                                                   trainingPreviousPreviousBaseResidualGuideInput,
                                                   trainingPreviousBaseResidualGuideInput,
                                                   baseResidualGuideInput,
                                                   temporalResidualGuidePoints,
                                                   outError))
                    {
                        return false;
                    }
                    const float temporalBlend = Clamp(outBundle.residualTemporalBlend, 0.0f, 1.0f);
                    for (uint32_t i = 0u; i < residualGuideInput.size() && i < temporalResidualGuidePoints.size(); ++i)
                    {
                        residualGuideInput[i] = Lerp(residualGuideInput[i], temporalResidualGuidePoints[i], temporalBlend);
                    }
                }
                HairAsset residualAsset;
                if (!ReconstructReducedModelFrame(outBundle.residualModel, residualGuideInput, residualAsset, outError))
                {
                    return false;
                }
                std::vector<Vec3> hybridBeforeCorrection;
                ComposeHybridPoints(teacherSequence.referenceAsset,
                                    priorAsset.restPoints,
                                    residualAsset.restPoints,
                                    trainingResidualBlend,
                                    hybridBeforeCorrection);

