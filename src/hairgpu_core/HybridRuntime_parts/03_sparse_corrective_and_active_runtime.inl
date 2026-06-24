// HybridRuntime.cpp ordered part 03: sparse_corrective_and_active_runtime
// This file is included by ../HybridRuntime.cpp; do not compile it directly.

void BuildActiveSetPredictorModel(const std::vector<std::array<float, 9u>>& featureRows,
    const std::vector<float>& targets,
    const ActiveSetPredictorSettings& settings,
    ActiveSetPredictor& outPredictor,
    ActiveSetPredictorEvaluation* outEvaluation)
{
    outPredictor = {};
    outPredictor.settings = settings;
    outPredictor.featureCount = 9u;
    outPredictor.hiddenWidth = std::max<uint32_t>(4u, settings.hiddenWidth);
    outPredictor.featureMean.assign(outPredictor.featureCount, 0.0f);
    outPredictor.featureStd.assign(outPredictor.featureCount, 1.0f);
    outPredictor.weights.assign(outPredictor.featureCount, 0.0f);
    outPredictor.hiddenWeights.assign(static_cast<size_t>(outPredictor.hiddenWidth) * outPredictor.featureCount, 0.0f);
    outPredictor.hiddenBias.assign(outPredictor.hiddenWidth, 0.0f);
    outPredictor.outputWeights.assign(outPredictor.hiddenWidth, 0.0f);
    outPredictor.outputBias = 0.0f;

    if (outEvaluation)
    {
        *outEvaluation = {};
    }
    if (featureRows.empty() || targets.empty())
    {
        return;
    }

    const uint32_t sampleCount = static_cast<uint32_t>(std::min(featureRows.size(), targets.size()));
    for (uint32_t featureIndex = 0u; featureIndex < outPredictor.featureCount; ++featureIndex)
    {
        double sum = 0.0;
        for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
        {
            sum += static_cast<double>(featureRows[sampleIndex][featureIndex]);
        }
        outPredictor.featureMean[featureIndex] = static_cast<float>(sum / static_cast<double>(sampleCount));

        double variance = 0.0;
        for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
        {
            const double d = static_cast<double>(featureRows[sampleIndex][featureIndex]) - static_cast<double>(outPredictor.featureMean[featureIndex]);
            variance += d * d;
        }
        outPredictor.featureStd[featureIndex] = static_cast<float>(std::sqrt(std::max(variance / static_cast<double>(sampleCount), 1.0e-8)));
    }

    std::mt19937 rng(4242u);
    std::uniform_real_distribution<float> init(-0.08f, 0.08f);
    for (float& value : outPredictor.hiddenWeights)
    {
        value = init(rng);
    }
    for (float& value : outPredictor.outputWeights)
    {
        value = init(rng);
    }

    const float weightDecay = std::max(settings.weightDecay, 0.0f);
    const float gradientClip = std::max(settings.gradientClip, 1.0e-4f);
    std::array<float, 9u> normalized = {};
    std::vector<float> hidden(outPredictor.hiddenWidth, 0.0f);
    std::vector<float> gradHidden(outPredictor.hiddenWidth, 0.0f);
    std::vector<uint32_t> trainingOrder;
    std::vector<uint32_t> validationOrder;
    const uint32_t validationStride = std::max<uint32_t>(2u, settings.validationStride);
    for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
    {
        if ((sampleIndex % validationStride) == 0u)
        {
            validationOrder.push_back(sampleIndex);
        }
        else
        {
            trainingOrder.push_back(sampleIndex);
        }
    }
    if (trainingOrder.empty())
    {
        trainingOrder = validationOrder;
        validationOrder.clear();
    }

    auto evaluatePredictor = [&](const std::vector<uint32_t>& evalOrder, ActiveSetPredictorEvaluation& outEval)
    {
        outEval = {};
        outEval.sampleCount = static_cast<uint32_t>(evalOrder.size());
        if (evalOrder.empty())
        {
            return;
        }
        double sq = 0.0;
        uint32_t correct = 0u;
        uint32_t positives = 0u;
        for (uint32_t sampleIndex : evalOrder)
        {
            const float prediction = PredictActiveSetRisk(outPredictor, featureRows[sampleIndex]);
            const float target = Clamp(targets[sampleIndex], 0.0f, 1.0f);
            const float d = prediction - target;
            sq += static_cast<double>(d * d);
            if ((prediction >= 0.5f) == (target >= 0.5f))
            {
                ++correct;
            }
            if (target >= 0.5f)
            {
                ++positives;
            }
        }
        const double denom = static_cast<double>(std::max<size_t>(1u, evalOrder.size()));
        outEval.rmsScoreError = static_cast<float>(std::sqrt(sq / denom));
        outEval.classificationAccuracy = static_cast<float>(correct) / static_cast<float>(denom);
        outEval.positiveRate = static_cast<float>(positives) / static_cast<float>(denom);
    };

    float learningRate = std::max(settings.learningRate, settings.minLearningRate);
    float bestValidationRms = std::numeric_limits<float>::max();
    uint32_t epochsWithoutImprovement = 0u;
    ActiveSetPredictor bestPredictor = outPredictor;
    for (uint32_t epoch = 0u; epoch < std::max<uint32_t>(1u, settings.epochs); ++epoch)
    {
        std::shuffle(trainingOrder.begin(), trainingOrder.end(), rng);
        learningRate = std::max(settings.minLearningRate, learningRate * ((epoch == 0u) ? 1.0f : settings.learningRateDecay));
        for (uint32_t sampleIndex : trainingOrder)
        {
            for (uint32_t featureIndex = 0u; featureIndex < outPredictor.featureCount; ++featureIndex)
            {
                normalized[featureIndex] = (featureRows[sampleIndex][featureIndex] - outPredictor.featureMean[featureIndex]) /
                    std::max(outPredictor.featureStd[featureIndex], 1.0e-4f);
            }
            for (uint32_t h = 0u; h < outPredictor.hiddenWidth; ++h)
            {
                float sum = outPredictor.hiddenBias[h];
                const size_t rowOffset = static_cast<size_t>(h) * outPredictor.featureCount;
                for (uint32_t featureIndex = 0u; featureIndex < outPredictor.featureCount; ++featureIndex)
                {
                    sum += outPredictor.hiddenWeights[rowOffset + featureIndex] * normalized[featureIndex];
                }
                hidden[h] = std::tanh(sum);
            }
            float logit = outPredictor.outputBias;
            for (uint32_t h = 0u; h < outPredictor.hiddenWidth; ++h)
            {
                logit += outPredictor.outputWeights[h] * hidden[h];
            }
            const float prediction = SafeSigmoid(logit);
            const float target = Clamp(targets[sampleIndex], 0.0f, 1.0f);
            const float classWeight = (target >= 0.5f) ? std::max(settings.positiveClassWeight, 1.0f) : 1.0f;
            const float error = Clamp((prediction - target) * classWeight, -gradientClip, gradientClip);
            outPredictor.outputBias -= learningRate * error;
            for (uint32_t h = 0u; h < outPredictor.hiddenWidth; ++h)
            {
                gradHidden[h] = Clamp(error * outPredictor.outputWeights[h] * (1.0f - hidden[h] * hidden[h]), -gradientClip, gradientClip);
                outPredictor.outputWeights[h] -= learningRate * (error * hidden[h] + weightDecay * outPredictor.outputWeights[h]);
            }
            for (uint32_t h = 0u; h < outPredictor.hiddenWidth; ++h)
            {
                const size_t rowOffset = static_cast<size_t>(h) * outPredictor.featureCount;
                outPredictor.hiddenBias[h] -= learningRate * gradHidden[h];
                for (uint32_t featureIndex = 0u; featureIndex < outPredictor.featureCount; ++featureIndex)
                {
                    outPredictor.hiddenWeights[rowOffset + featureIndex] -= learningRate *
                        (gradHidden[h] * normalized[featureIndex] + weightDecay * outPredictor.hiddenWeights[rowOffset + featureIndex]);
                }
            }
        }

        ActiveSetPredictorEvaluation validationEval = {};
        evaluatePredictor(validationOrder.empty() ? trainingOrder : validationOrder, validationEval);
        if (validationEval.sampleCount > 0u && validationEval.rmsScoreError < bestValidationRms - 1.0e-6f)
        {
            bestValidationRms = validationEval.rmsScoreError;
            bestPredictor = outPredictor;
            epochsWithoutImprovement = 0u;
        }
        else
        {
            ++epochsWithoutImprovement;
        }
        if (!validationOrder.empty() && epochsWithoutImprovement >= std::max<uint32_t>(3u, settings.earlyStoppingPatience))
        {
            break;
        }
    }

    outPredictor = bestPredictor;
    if (outEvaluation)
    {
        evaluatePredictor(validationOrder.empty() ? trainingOrder : validationOrder, *outEvaluation);
    }
}

void BuildSmoothedPriorSequence(const ReducedModelTrainingSequence& teacherSequence,
    float smoothingAlpha,
    ReducedModelTrainingSequence& outSequence)
{
    outSequence = {};
    outSequence.referenceAsset = teacherSequence.referenceAsset;
    outSequence.frameRate = teacherSequence.frameRate;
    const uint32_t frameCount = teacherSequence.FrameCount();
    const uint32_t pointCount = teacherSequence.PointCountPerFrame();
    if (frameCount == 0u || pointCount == 0u)
    {
        return;
    }

    std::vector<Vec3> smoothed(pointCount);
    CopySequenceFrame(teacherSequence, 0u, smoothed);
    AppendSequenceFrame(smoothed, outSequence);
    const float alpha = Clamp(smoothingAlpha, 0.01f, 1.0f);
    for (uint32_t frameIndex = 1u; frameIndex < frameCount; ++frameIndex)
    {
        std::vector<Vec3> current;
        CopySequenceFrame(teacherSequence, frameIndex, current);
        if (current.size() != smoothed.size())
        {
            continue;
        }
        for (uint32_t i = 0u; i < pointCount; ++i)
        {
            smoothed[i] = Lerp(smoothed[i], current[i], alpha);
        }
        AppendSequenceFrame(smoothed, outSequence);
    }
}

void BuildResidualSequence(const ReducedModelTrainingSequence& teacherSequence,
    const ReducedModelTrainingSequence& priorSequence,
    ReducedModelTrainingSequence& outSequence,
    float* outAverageResidualMagnitude,
    float* outMaxResidualMagnitude)
{
    outSequence = {};
    outSequence.referenceAsset = teacherSequence.referenceAsset;
    outSequence.frameRate = teacherSequence.frameRate;

    double averageResidualMagnitude = 0.0;
    float maxResidualMagnitude = 0.0f;
    uint64_t residualCount = 0u;
    const uint32_t frameCount = std::min(teacherSequence.FrameCount(), priorSequence.FrameCount());
    for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
    {
        std::vector<Vec3> teacher;
        std::vector<Vec3> prior;
        CopySequenceFrame(teacherSequence, frameIndex, teacher);
        CopySequenceFrame(priorSequence, frameIndex, prior);
        std::vector<Vec3> residual = teacherSequence.referenceAsset.restPoints;
        for (uint32_t i = 0u; i < residual.size() && i < teacher.size() && i < prior.size(); ++i)
        {
            const Vec3 delta = teacher[i] - prior[i];
            residual[i] += delta;
            const float magnitude = Length(delta);
            averageResidualMagnitude += static_cast<double>(magnitude);
            maxResidualMagnitude = std::max(maxResidualMagnitude, magnitude);
            ++residualCount;
        }
        AppendSequenceFrame(residual, outSequence);
    }

    if (outAverageResidualMagnitude)
    {
        *outAverageResidualMagnitude = (residualCount > 0u)
            ? static_cast<float>(averageResidualMagnitude / static_cast<double>(residualCount))
            : 0.0f;
    }
    if (outMaxResidualMagnitude)
    {
        *outMaxResidualMagnitude = maxResidualMagnitude;
    }
}

void ComposeHybridPoints(const HairAsset& referenceAsset,
    const std::vector<Vec3>& priorPoints,
    const std::vector<Vec3>& residualPoints,
    float residualBlend,
    std::vector<Vec3>& outPoints)
{
    outPoints = priorPoints;
    if (outPoints.size() != referenceAsset.restPoints.size() || residualPoints.size() != referenceAsset.restPoints.size())
    {
        return;
    }
    const float blend = std::max(0.0f, residualBlend);
    for (uint32_t i = 0u; i < outPoints.size(); ++i)
    {
        outPoints[i] = priorPoints[i] + (residualPoints[i] - referenceAsset.restPoints[i]) * blend;
    }
}

bool WriteHybridBundleConfig(const std::filesystem::path& filePath,
    const HybridModelBundle& bundle,
    const HybridModelTrainingReport* report,
    std::string& outError)
{
    std::ofstream out(filePath);
    if (!out)
    {
        outError = "Failed to open hybrid bundle config for writing.";
        return false;
    }
    out << std::fixed << std::setprecision(8);
    out << "priorGuideCount=" << bundle.trainingSettings.priorGuideCount << "\n";
    out << "residualGuideCount=" << bundle.trainingSettings.residualGuideCount << "\n";
    out << "maxInfluencesPerFollower=" << bundle.trainingSettings.maxInfluencesPerFollower << "\n";
    out << "regularization=" << bundle.trainingSettings.regularization << "\n";
    out << "validationStride=" << bundle.trainingSettings.validationStride << "\n";
    out << "priorGuideSmoothingAlpha=" << bundle.trainingSettings.priorGuideSmoothingAlpha << "\n";
    out << "residualGuideGain=" << bundle.trainingSettings.residualGuideGain << "\n";
    out << "residualBlend=" << bundle.trainingSettings.residualBlend << "\n";
    out << "enableScalpCollision=" << (bundle.trainingSettings.enableScalpCollision ? 1 : 0) << "\n";
    out << "clusterGuideCount=" << bundle.clusterSettings.targetGuideCountPerCluster << "\n";
    out << "clusterMaxGuideRootDistance=" << bundle.clusterSettings.maxGuideRootDistance << "\n";
    out << "scoringMotionWeight=" << bundle.scoringSettings.motionWeight << "\n";
    out << "scoringDisplacementWeight=" << bundle.scoringSettings.displacementWeight << "\n";
    out << "scoringContactWeight=" << bundle.scoringSettings.contactWeight << "\n";
    out << "scoringVisibilityWeight=" << bundle.scoringSettings.visibilityWeight << "\n";
    out << "scoringSilhouetteWeight=" << bundle.scoringSettings.silhouetteWeight << "\n";
    out << "scoringClusterSizeWeight=" << bundle.scoringSettings.clusterSizeWeight << "\n";
    out << "scoringUncertaintyWeight=" << bundle.scoringSettings.uncertaintyWeight << "\n";
    out << "budgetMaxActiveClusters=" << bundle.budget.maxActiveClusters << "\n";
    out << "budgetMaxActiveGuides=" << bundle.budget.maxActiveGuides << "\n";
    out << "budgetPromoteScoreThreshold=" << bundle.budget.promoteScoreThreshold << "\n";
    out << "budgetTargetFrameMs=" << bundle.budget.targetFrameMilliseconds << "\n";
    out << "budgetMinSolverIterations=" << bundle.budget.minSolverIterations << "\n";
    out << "budgetMaxSolverIterations=" << bundle.budget.maxSolverIterations << "\n";
    out << "correctiveShellDistance=" << bundle.correctiveSettings.shellDistance << "\n";
    out << "correctiveContactBlend=" << bundle.correctiveSettings.contactBlend << "\n";
    out << "correctiveStretchBlend=" << bundle.correctiveSettings.stretchBlend << "\n";
    out << "correctiveBendBlend=" << bundle.correctiveSettings.bendBlend << "\n";
    out << "correctiveIterations=" << bundle.correctiveSettings.iterations << "\n";
    out << "uncertaintyGain=" << bundle.uncertaintyGain << "\n";
    out << "contactGain=" << bundle.contactGain << "\n";
    out << "penetrationGain=" << bundle.penetrationGain << "\n";
    out << "temporalCoherenceGain=" << bundle.temporalCoherenceGain << "\n";
    out << "uncertaintySmoothingAlpha=" << bundle.uncertaintySmoothingAlpha << "\n";
    out << "priorVelocityPredictionGain=" << bundle.priorVelocityPredictionGain << "\n";
    out << "residualVelocityPredictionGain=" << bundle.residualVelocityPredictionGain << "\n";
    out << "priorAccelerationPredictionGain=" << bundle.priorAccelerationPredictionGain << "\n";
    out << "residualAccelerationPredictionGain=" << bundle.residualAccelerationPredictionGain << "\n";
    out << "priorTemporalBlend=" << bundle.priorTemporalBlend << "\n";
    out << "residualTemporalBlend=" << bundle.residualTemporalBlend << "\n";
    out << "activeSetPredictorBlend=" << bundle.activeSetPredictorBlend << "\n";
    out << "frozenStrandBlend=" << bundle.frozenStrandBlend << "\n";
    out << "frozenRootFollow=" << bundle.frozenRootFollow << "\n";
    out << "cachedStrandBlend=" << bundle.cachedStrandBlend << "\n";
    out << "cachedRootFollow=" << bundle.cachedRootFollow << "\n";
    out << "cachedFrameRefreshInterval=" << bundle.cachedFrameRefreshInterval << "\n";
    if (report)
    {
        out << "reportPriorRms=" << report->priorEvaluation.rmsPositionError << "\n";
        out << "reportResidualRms=" << report->residualEvaluation.rmsPositionError << "\n";
        out << "reportAverageResidualMagnitude=" << report->averageResidualMagnitude << "\n";
        out << "reportMaxResidualMagnitude=" << report->maxResidualMagnitude << "\n";
        out << "reportPriorTemporalRms=" << report->priorTemporalEvaluation.rmsPositionError << "\n";
        out << "reportResidualTemporalRms=" << report->residualTemporalEvaluation.rmsPositionError << "\n";
        out << "reportActiveSetPredictorRms=" << report->activeSetPredictorEvaluation.rmsScoreError << "\n";
        out << "reportActiveSetPredictorAccuracy=" << report->activeSetPredictorEvaluation.classificationAccuracy << "\n";
    }
    outError.clear();
    return true;
}

bool ParseHybridConfigLine(const std::string& line, std::string& outKey, std::string& outValue)
{
    const size_t eq = line.find('=');
    if (eq == std::string::npos)
    {
        return false;
    }
    outKey = line.substr(0u, eq);
    outValue = line.substr(eq + 1u);
    return !outKey.empty();
}

void ApplyHybridConfigValue(HybridModelBundle& ioBundle, const std::string& key, const std::string& value)
{
    auto toUInt = [&](uint32_t fallback) -> uint32_t { try { return static_cast<uint32_t>(std::stoul(value)); } catch (...) { return fallback; } };
    auto toFloat = [&](float fallback) -> float { try { return std::stof(value); } catch (...) { return fallback; } };
    if (key == "priorGuideCount") ioBundle.trainingSettings.priorGuideCount = toUInt(ioBundle.trainingSettings.priorGuideCount);
    else if (key == "residualGuideCount") ioBundle.trainingSettings.residualGuideCount = toUInt(ioBundle.trainingSettings.residualGuideCount);
    else if (key == "maxInfluencesPerFollower") ioBundle.trainingSettings.maxInfluencesPerFollower = toUInt(ioBundle.trainingSettings.maxInfluencesPerFollower);
    else if (key == "regularization") ioBundle.trainingSettings.regularization = toFloat(ioBundle.trainingSettings.regularization);
    else if (key == "validationStride") ioBundle.trainingSettings.validationStride = toUInt(ioBundle.trainingSettings.validationStride);
    else if (key == "priorGuideSmoothingAlpha") ioBundle.trainingSettings.priorGuideSmoothingAlpha = toFloat(ioBundle.trainingSettings.priorGuideSmoothingAlpha);
    else if (key == "residualGuideGain") ioBundle.trainingSettings.residualGuideGain = toFloat(ioBundle.trainingSettings.residualGuideGain);
    else if (key == "residualBlend") ioBundle.trainingSettings.residualBlend = toFloat(ioBundle.trainingSettings.residualBlend);
    else if (key == "enableScalpCollision") ioBundle.trainingSettings.enableScalpCollision = toUInt(ioBundle.trainingSettings.enableScalpCollision ? 1u : 0u) != 0u;
    else if (key == "clusterGuideCount") ioBundle.clusterSettings.targetGuideCountPerCluster = toUInt(ioBundle.clusterSettings.targetGuideCountPerCluster);
    else if (key == "clusterMaxGuideRootDistance") ioBundle.clusterSettings.maxGuideRootDistance = toFloat(ioBundle.clusterSettings.maxGuideRootDistance);
    else if (key == "scoringMotionWeight") ioBundle.scoringSettings.motionWeight = toFloat(ioBundle.scoringSettings.motionWeight);
    else if (key == "scoringDisplacementWeight") ioBundle.scoringSettings.displacementWeight = toFloat(ioBundle.scoringSettings.displacementWeight);
    else if (key == "scoringContactWeight") ioBundle.scoringSettings.contactWeight = toFloat(ioBundle.scoringSettings.contactWeight);
    else if (key == "scoringVisibilityWeight") ioBundle.scoringSettings.visibilityWeight = toFloat(ioBundle.scoringSettings.visibilityWeight);
    else if (key == "scoringSilhouetteWeight") ioBundle.scoringSettings.silhouetteWeight = toFloat(ioBundle.scoringSettings.silhouetteWeight);
    else if (key == "scoringClusterSizeWeight") ioBundle.scoringSettings.clusterSizeWeight = toFloat(ioBundle.scoringSettings.clusterSizeWeight);
    else if (key == "scoringUncertaintyWeight") ioBundle.scoringSettings.uncertaintyWeight = toFloat(ioBundle.scoringSettings.uncertaintyWeight);
    else if (key == "scoringPenetrationWeight") ioBundle.scoringSettings.penetrationWeight = toFloat(ioBundle.scoringSettings.penetrationWeight);
    else if (key == "scoringBoundaryWeight") ioBundle.scoringSettings.boundaryWeight = toFloat(ioBundle.scoringSettings.boundaryWeight);
    else if (key == "scoringTemporalWeight") ioBundle.scoringSettings.temporalWeight = toFloat(ioBundle.scoringSettings.temporalWeight);
    else if (key == "scoringLeakageWeight") ioBundle.scoringSettings.leakageWeight = toFloat(ioBundle.scoringSettings.leakageWeight);
    else if (key == "scoringModelErrorWeight") ioBundle.scoringSettings.modelErrorWeight = toFloat(ioBundle.scoringSettings.modelErrorWeight);
    else if (key == "scoringNaturalnessGainWeight") ioBundle.scoringSettings.naturalnessGainWeight = toFloat(ioBundle.scoringSettings.naturalnessGainWeight);
    else if (key == "scoringEfficiencyWeight") ioBundle.scoringSettings.efficiencyWeight = toFloat(ioBundle.scoringSettings.efficiencyWeight);
    else if (key == "budgetMaxActiveClusters") ioBundle.budget.maxActiveClusters = toUInt(ioBundle.budget.maxActiveClusters);
    else if (key == "budgetMaxActiveGuides") ioBundle.budget.maxActiveGuides = toUInt(ioBundle.budget.maxActiveGuides);
    else if (key == "budgetPromoteScoreThreshold") ioBundle.budget.promoteScoreThreshold = toFloat(ioBundle.budget.promoteScoreThreshold);
    else if (key == "budgetTargetFrameMs") ioBundle.budget.targetFrameMilliseconds = toFloat(ioBundle.budget.targetFrameMilliseconds);
    else if (key == "budgetMinSolverIterations") ioBundle.budget.minSolverIterations = toUInt(ioBundle.budget.minSolverIterations);
    else if (key == "budgetMaxSolverIterations") ioBundle.budget.maxSolverIterations = toUInt(ioBundle.budget.maxSolverIterations);
    else if (key == "budgetMemoryBudgetMB") ioBundle.budget.memoryBudgetMegabytes = toFloat(ioBundle.budget.memoryBudgetMegabytes);
    else if (key == "budgetFixedMemoryMB") ioBundle.budget.estimatedFixedMemoryMegabytes = toFloat(ioBundle.budget.estimatedFixedMemoryMegabytes);
    else if (key == "budgetClusterMemoryMB") ioBundle.budget.estimatedClusterMemoryMegabytes = toFloat(ioBundle.budget.estimatedClusterMemoryMegabytes);
    else if (key == "budgetEnforceEditCoverage") ioBundle.budget.enforceEditCoverage = toUInt(ioBundle.budget.enforceEditCoverage ? 1u : 0u) != 0u;
    else if (key == "budgetMandatoryEditThreshold") ioBundle.budget.mandatoryEditThreshold = toFloat(ioBundle.budget.mandatoryEditThreshold);
    else if (key == "correctiveShellDistance") ioBundle.correctiveSettings.shellDistance = toFloat(ioBundle.correctiveSettings.shellDistance);
    else if (key == "correctiveContactBlend") ioBundle.correctiveSettings.contactBlend = toFloat(ioBundle.correctiveSettings.contactBlend);
    else if (key == "correctiveStretchBlend") ioBundle.correctiveSettings.stretchBlend = toFloat(ioBundle.correctiveSettings.stretchBlend);
    else if (key == "correctiveBendBlend") ioBundle.correctiveSettings.bendBlend = toFloat(ioBundle.correctiveSettings.bendBlend);
    else if (key == "correctiveIterations") ioBundle.correctiveSettings.iterations = toUInt(ioBundle.correctiveSettings.iterations);
    else if (key == "uncertaintyGain") ioBundle.uncertaintyGain = toFloat(ioBundle.uncertaintyGain);
    else if (key == "contactGain") ioBundle.contactGain = toFloat(ioBundle.contactGain);
    else if (key == "penetrationGain") ioBundle.penetrationGain = toFloat(ioBundle.penetrationGain);
    else if (key == "temporalCoherenceGain") ioBundle.temporalCoherenceGain = toFloat(ioBundle.temporalCoherenceGain);
    else if (key == "uncertaintySmoothingAlpha") ioBundle.uncertaintySmoothingAlpha = toFloat(ioBundle.uncertaintySmoothingAlpha);
    else if (key == "priorVelocityPredictionGain") ioBundle.priorVelocityPredictionGain = toFloat(ioBundle.priorVelocityPredictionGain);
    else if (key == "residualVelocityPredictionGain") ioBundle.residualVelocityPredictionGain = toFloat(ioBundle.residualVelocityPredictionGain);
    else if (key == "priorAccelerationPredictionGain") ioBundle.priorAccelerationPredictionGain = toFloat(ioBundle.priorAccelerationPredictionGain);
    else if (key == "residualAccelerationPredictionGain") ioBundle.residualAccelerationPredictionGain = toFloat(ioBundle.residualAccelerationPredictionGain);
    else if (key == "priorTemporalBlend") ioBundle.priorTemporalBlend = toFloat(ioBundle.priorTemporalBlend);
    else if (key == "residualTemporalBlend") ioBundle.residualTemporalBlend = toFloat(ioBundle.residualTemporalBlend);
    else if (key == "activeSetPredictorBlend") ioBundle.activeSetPredictorBlend = toFloat(ioBundle.activeSetPredictorBlend);
    else if (key == "frozenStrandBlend") ioBundle.frozenStrandBlend = toFloat(ioBundle.frozenStrandBlend);
    else if (key == "frozenRootFollow") ioBundle.frozenRootFollow = toFloat(ioBundle.frozenRootFollow);
    else if (key == "cachedStrandBlend") ioBundle.cachedStrandBlend = toFloat(ioBundle.cachedStrandBlend);
    else if (key == "cachedRootFollow") ioBundle.cachedRootFollow = toFloat(ioBundle.cachedRootFollow);
    else if (key == "cachedFrameRefreshInterval") ioBundle.cachedFrameRefreshInterval = toUInt(ioBundle.cachedFrameRefreshInterval);
}

bool WriteHybridSummaryJson(const std::filesystem::path& filePath,
    const HybridModelBundle& bundle,
    const HybridModelTrainingReport* trainingReport,
    const FullHybridRuntimeReport* runtimeReport,
    std::string& outError)
{
    std::ofstream out(filePath);
    if (!out)
    {
        outError = "Failed to open hybrid summary JSON for writing.";
        return false;
    }
    out << std::fixed << std::setprecision(6);
    out << "{\n";
    out << "  \"priorGuideCount\": " << bundle.trainingSettings.priorGuideCount << ",\n";
    out << "  \"residualGuideCount\": " << bundle.trainingSettings.residualGuideCount << ",\n";
    out << "  \"priorGuideSmoothingAlpha\": " << bundle.trainingSettings.priorGuideSmoothingAlpha << ",\n";
    out << "  \"residualGuideGain\": " << bundle.trainingSettings.residualGuideGain << ",\n";
    out << "  \"residualBlend\": " << bundle.trainingSettings.residualBlend << ",\n";
    out << "  \"uncertaintyGain\": " << bundle.uncertaintyGain << ",\n";
    out << "  \"contactGain\": " << bundle.contactGain << ",\n";
    out << "  \"penetrationGain\": " << bundle.penetrationGain << ",\n";
    out << "  \"temporalCoherenceGain\": " << bundle.temporalCoherenceGain << ",\n";
    out << "  \"activeSetPredictorBlend\": " << bundle.activeSetPredictorBlend << ",\n";
    out << "  \"cachedFrameRefreshInterval\": " << bundle.cachedFrameRefreshInterval;
    if (trainingReport)
    {
        out << ",\n  \"priorRms\": " << trainingReport->priorEvaluation.rmsPositionError
            << ",\n  \"residualRms\": " << trainingReport->residualEvaluation.rmsPositionError
            << ",\n  \"priorTemporalRms\": " << trainingReport->priorTemporalEvaluation.rmsPositionError
            << ",\n  \"residualTemporalRms\": " << trainingReport->residualTemporalEvaluation.rmsPositionError
            << ",\n  \"averageResidualMagnitude\": " << trainingReport->averageResidualMagnitude
            << ",\n  \"maxResidualMagnitude\": " << trainingReport->maxResidualMagnitude
            << ",\n  \"activeSetPredictorRms\": " << trainingReport->activeSetPredictorEvaluation.rmsScoreError
            << ",\n  \"activeSetPredictorAccuracy\": " << trainingReport->activeSetPredictorEvaluation.classificationAccuracy;
    }
    if (runtimeReport)
    {
        out << ",\n  \"runtimeFrameCount\": " << runtimeReport->frameCount
            << ",\n  \"averagePriorRmsError\": " << runtimeReport->averagePriorRmsError
            << ",\n  \"averageHybridRmsErrorBeforeCorrection\": " << runtimeReport->averageHybridRmsErrorBeforeCorrection
            << ",\n  \"averageHybridRmsErrorAfterCorrection\": " << runtimeReport->averageHybridRmsErrorAfterCorrection
            << ",\n  \"maxHybridRmsErrorAfterCorrection\": " << runtimeReport->maxHybridRmsErrorAfterCorrection
            << ",\n  \"averageSelectedClusterCount\": " << runtimeReport->averageSelectedClusterCount
            << ",\n  \"averageSelectedGuideCount\": " << runtimeReport->averageSelectedGuideCount
            << ",\n  \"averageCorrectedPointCount\": " << runtimeReport->averageCorrectedPointCount
            << ",\n  \"averagePenetration\": " << runtimeReport->averagePenetration
            << ",\n  \"maxPenetration\": " << runtimeReport->maxPenetration
            << ",\n  \"averageGuidePredictorRisk\": " << runtimeReport->averageGuidePredictorRisk
            << ",\n  \"maxGuidePredictorRisk\": " << runtimeReport->maxGuidePredictorRisk
            << ",\n  \"averageEstimatedFrameMilliseconds\": " << runtimeReport->averageEstimatedFrameMilliseconds
            << ",\n  \"p95EstimatedFrameMilliseconds\": " << runtimeReport->p95EstimatedFrameMilliseconds
            << ",\n  \"maxEstimatedFrameMilliseconds\": " << runtimeReport->maxEstimatedFrameMilliseconds
            << ",\n  \"hitRate60Fps\": " << runtimeReport->hitRate60Fps;
    }
    out << "\n}\n";
    outError.clear();
    return true;
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
    out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&predictor.featureCount), sizeof(predictor.featureCount));
    out.write(reinterpret_cast<const char*>(&predictor.hiddenWidth), sizeof(predictor.hiddenWidth));
    out.write(reinterpret_cast<const char*>(&predictor.settings.hiddenWidth), sizeof(predictor.settings.hiddenWidth));
    out.write(reinterpret_cast<const char*>(&predictor.settings.epochs), sizeof(predictor.settings.epochs));
    out.write(reinterpret_cast<const char*>(&predictor.settings.validationStride), sizeof(predictor.settings.validationStride));
    out.write(reinterpret_cast<const char*>(&predictor.settings.earlyStoppingPatience), sizeof(predictor.settings.earlyStoppingPatience));
    out.write(reinterpret_cast<const char*>(&predictor.settings.learningRate), sizeof(predictor.settings.learningRate));
    out.write(reinterpret_cast<const char*>(&predictor.settings.learningRateDecay), sizeof(predictor.settings.learningRateDecay));
    out.write(reinterpret_cast<const char*>(&predictor.settings.minLearningRate), sizeof(predictor.settings.minLearningRate));
    out.write(reinterpret_cast<const char*>(&predictor.settings.weightDecay), sizeof(predictor.settings.weightDecay));
    out.write(reinterpret_cast<const char*>(&predictor.settings.gradientClip), sizeof(predictor.settings.gradientClip));
    out.write(reinterpret_cast<const char*>(&predictor.settings.positiveClassWeight), sizeof(predictor.settings.positiveClassWeight));
    auto writeVector = [&](const std::vector<float>& values)
    {
        const uint32_t count = static_cast<uint32_t>(values.size());
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        if (count > 0u)
        {
            out.write(reinterpret_cast<const char*>(values.data()), sizeof(float) * count);
        }
    };
    writeVector(predictor.featureMean);
    writeVector(predictor.featureStd);
    writeVector(predictor.weights);
    out.write(reinterpret_cast<const char*>(&predictor.bias), sizeof(predictor.bias));
    writeVector(predictor.hiddenWeights);
    writeVector(predictor.hiddenBias);
    writeVector(predictor.outputWeights);
    out.write(reinterpret_cast<const char*>(&predictor.outputBias), sizeof(predictor.outputBias));
    if (!out)
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
    in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (magic != 0x31505341u || (version != 1u && version != 2u && version != 3u))
    {
        outError = "Active-set predictor file has an unsupported version.";
        return false;
    }
    in.read(reinterpret_cast<char*>(&outPredictor.featureCount), sizeof(outPredictor.featureCount));
    if (version >= 2u)
    {
        in.read(reinterpret_cast<char*>(&outPredictor.hiddenWidth), sizeof(outPredictor.hiddenWidth));
        in.read(reinterpret_cast<char*>(&outPredictor.settings.hiddenWidth), sizeof(outPredictor.settings.hiddenWidth));
    }
    in.read(reinterpret_cast<char*>(&outPredictor.settings.epochs), sizeof(outPredictor.settings.epochs));
    if (version >= 3u)
    {
        in.read(reinterpret_cast<char*>(&outPredictor.settings.validationStride), sizeof(outPredictor.settings.validationStride));
        in.read(reinterpret_cast<char*>(&outPredictor.settings.earlyStoppingPatience), sizeof(outPredictor.settings.earlyStoppingPatience));
    }
    in.read(reinterpret_cast<char*>(&outPredictor.settings.learningRate), sizeof(outPredictor.settings.learningRate));
    if (version >= 3u)
    {
        in.read(reinterpret_cast<char*>(&outPredictor.settings.learningRateDecay), sizeof(outPredictor.settings.learningRateDecay));
        in.read(reinterpret_cast<char*>(&outPredictor.settings.minLearningRate), sizeof(outPredictor.settings.minLearningRate));
    }
    in.read(reinterpret_cast<char*>(&outPredictor.settings.weightDecay), sizeof(outPredictor.settings.weightDecay));
    if (version >= 2u)
    {
        in.read(reinterpret_cast<char*>(&outPredictor.settings.gradientClip), sizeof(outPredictor.settings.gradientClip));
    }
    if (version >= 3u)
    {
        in.read(reinterpret_cast<char*>(&outPredictor.settings.positiveClassWeight), sizeof(outPredictor.settings.positiveClassWeight));
    }
    auto readVector = [&](std::vector<float>& values)
    {
        uint32_t count = 0u;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        values.resize(count);
        if (count > 0u)
        {
            in.read(reinterpret_cast<char*>(values.data()), sizeof(float) * count);
        }
    };
    readVector(outPredictor.featureMean);
    readVector(outPredictor.featureStd);
    readVector(outPredictor.weights);
    in.read(reinterpret_cast<char*>(&outPredictor.bias), sizeof(outPredictor.bias));
    if (version >= 2u)
    {
        readVector(outPredictor.hiddenWeights);
        readVector(outPredictor.hiddenBias);
        readVector(outPredictor.outputWeights);
        in.read(reinterpret_cast<char*>(&outPredictor.outputBias), sizeof(outPredictor.outputBias));
    }
    if (!in)
    {
        outError = "Failed to deserialize active-set predictor.";
        return false;
    }
    if (outPredictor.hiddenWidth == 0u)
    {
        outPredictor.hiddenWidth = outPredictor.settings.hiddenWidth;
    }
    outError.clear();
    return true;
}
