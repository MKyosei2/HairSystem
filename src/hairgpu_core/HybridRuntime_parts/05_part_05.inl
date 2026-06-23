// HybridRuntime.cpp ordered part 05: part_05
// This file is included by ../HybridRuntime.cpp; do not compile it directly.

std::vector<Vec3> currentHybridGuideRoots;
BuildGuideRootsFromPoints(teacherSequence.referenceAsset, hybridBeforeCorrection, currentHybridGuideRoots);
std::vector<Vec3> teacherGuideRoots;
BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherFrame, teacherGuideRoots);
std::vector<float> guideContactScores = BuildGuideContactScores(teacherSequence.referenceAsset,
    hybridBeforeCorrection,
    predictorColliders,
    -1.05f,
    outBundle.correctiveSettings.shellDistance);
std::vector<float> guidePenetrationScores = BuildGuidePenetrationScores(teacherSequence.referenceAsset,
    hybridBeforeCorrection,
    predictorColliders,
    -1.05f,
    outBundle.correctiveSettings.shellDistance);
for (float& value : guideContactScores) value = Clamp(value * outBundle.contactGain, 0.0f, 1.0f);
for (float& value : guidePenetrationScores) value = Clamp(value * outBundle.penetrationGain, 0.0f, 1.0f);
const uint32_t guideCount = std::min<uint32_t>(teacherSequence.referenceAsset.guideStrandCount, static_cast<uint32_t>(teacherSequence.referenceAsset.strands.size()));
std::vector<float> guideModelErrorScores(guideCount, 0.0f);
for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
{
    const Vec3 teacherRoot = (guideIndex < teacherGuideRoots.size()) ? teacherGuideRoots[guideIndex] : Vec3(0.0f, 0.0f, 0.0f);
    const Vec3 hybridRoot = (guideIndex < currentHybridGuideRoots.size()) ? currentHybridGuideRoots[guideIndex] : teacherRoot;
    const StrandRange& strand = teacherSequence.referenceAsset.strands[guideIndex];
    const uint32_t pointIndex = strand.pointOffset + strand.pointCount / 2u;
    const float residualMagnitude = (pointIndex < residualAsset.restPoints.size() && pointIndex < teacherSequence.referenceAsset.restPoints.size())
        ? Length(residualAsset.restPoints[pointIndex] - teacherSequence.referenceAsset.restPoints[pointIndex])
        : 0.0f;
    const float rootError = Clamp(Length(hybridRoot - teacherRoot) / 0.025f, 0.0f, 1.0f);
    guideModelErrorScores[guideIndex] = Clamp(rootError * 0.70f + residualMagnitude * 18.0f * 0.30f, 0.0f, 1.0f);
}
std::vector<float> guideUncertainty = BuildGuideUncertainty(teacherSequence.referenceAsset,
    currentHybridGuideRoots,
    trainingPreviousHybridGuideRoots,
    trainingPreviousGuideUncertainty,
    guideContactScores,
    guidePenetrationScores,
    guideModelErrorScores,
    1.0f / runtimeFrameRate,
    outBundle.temporalCoherenceGain,
    outBundle.uncertaintySmoothingAlpha);
ActiveSetRuntimeInput runtimeInput = {};
runtimeInput.currentGuideRoots = currentHybridGuideRoots;
runtimeInput.previousGuideRoots = trainingPreviousHybridGuideRoots;
runtimeInput.guideContactScores = guideContactScores;
runtimeInput.guideUncertainty = guideUncertainty;
runtimeInput.cameraPosition = Vec3(0.0f, 1.55f, -1.85f);
runtimeInput.cameraForward = Normalize(Vec3(0.0f, -0.05f, 1.0f));
runtimeInput.fixedTimeStep = 1.0f / runtimeFrameRate;
ActiveSetPlan plan;
if (!EvaluateActiveSetPlan(teacherSequence.referenceAsset,
    predictorClusters,
    runtimeInput,
    outBundle.scoringSettings,
    outBundle.budget,
    plan,
    outError))
{
    return false;
}
std::vector<float> targets;
BuildActiveSetPredictorTarget(predictorClusters,
    plan,
    guideContactScores,
    guidePenetrationScores,
    guideModelErrorScores,
    targets);
AppendPredictorTrainingRows(predictorClusters,
    restGuideRoots,
    currentHybridGuideRoots,
    trainingPreviousHybridGuideRoots,
    guideUncertainty,
    guideContactScores,
    guidePenetrationScores,
    guideModelErrorScores,
    outBundle.scoringSettings,
    targets,
    predictorFeatureRows,
    predictorTargets);
trainingPreviousPreviousSmoothedPriorGuidePoints = trainingPreviousSmoothedPriorGuidePoints;
trainingPreviousSmoothedPriorGuidePoints = trainingSmoothedPriorGuidePoints;
trainingPreviousPreviousRawPriorGuidePoints = trainingPreviousRawPriorGuidePoints;
trainingPreviousRawPriorGuidePoints = currentPriorGuidePoints;
trainingPreviousPreviousResidualGuidePoints = trainingPreviousResidualGuidePoints;
trainingPreviousResidualGuidePoints = currentResidualGuidePoints;
trainingPreviousPreviousBaseResidualGuideInput = trainingPreviousBaseResidualGuideInput;
trainingPreviousBaseResidualGuideInput = baseResidualGuideInput;
trainingPreviousHybridGuideRoots = currentHybridGuideRoots;
trainingPreviousGuideUncertainty = guideUncertainty;
            }

            ActiveSetPredictorSettings predictorSettings = {};
            predictorSettings.epochs = std::max<uint32_t>(90u, 60u + settings.validationStride * 10u);
            predictorSettings.learningRate = 1.6e-2f;
            predictorSettings.weightDecay = std::max(1.0e-5f, settings.regularization * 0.5f);
            BuildActiveSetPredictorModel(predictorFeatureRows,
                predictorTargets,
                predictorSettings,
                outBundle.activeSetPredictor,
                &outReport.activeSetPredictorEvaluation);
            outBundle.activeSetPredictorBlend = Clamp(0.30f + outReport.activeSetPredictorEvaluation.classificationAccuracy * 0.45f +
                (1.0f - outReport.activeSetPredictorEvaluation.rmsScoreError) * 0.10f,
                0.25f,
                0.85f);
        }
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
        if (!SaveReducedModelToFile(directoryPath / "prior.reducedbin", bundle.priorModel, outError) ||
            !SaveReducedModelToFile(directoryPath / "residual.reducedbin", bundle.residualModel, outError) ||
            !SaveTemporalGuideNetworkToFile(directoryPath / "prior.temporalbin", bundle.priorTemporalNetwork, outError) ||
            !SaveTemporalGuideNetworkToFile(directoryPath / "residual.temporalbin", bundle.residualTemporalNetwork, outError) ||
            !SaveActiveSetPredictorToFile(directoryPath / "active_set.predictorbin", bundle.activeSetPredictor, outError) ||
            !WriteHybridBundleConfig(directoryPath / "hybrid_bundle.cfg", bundle, report, outError) ||
            !WriteHybridSummaryJson(directoryPath / "hybrid_bundle_summary.json", bundle, report, nullptr, outError))
        {
            return false;
        }
        return true;
    }

    bool LoadHybridModelBundle(const std::filesystem::path& directoryPath,
        HybridModelBundle& outBundle,
        std::string& outError)
    {
        outError.clear();
        outBundle = {};
        if (!LoadReducedModelFromFile(directoryPath / "prior.reducedbin", outBundle.priorModel, outError) ||
            !LoadReducedModelFromFile(directoryPath / "residual.reducedbin", outBundle.residualModel, outError))
        {
            return false;
        }
        if (!LoadTemporalGuideNetworkFromFile(directoryPath / "prior.temporalbin", outBundle.priorTemporalNetwork, outError))
        {
            return false;
        }
        if (!LoadTemporalGuideNetworkFromFile(directoryPath / "residual.temporalbin", outBundle.residualTemporalNetwork, outError))
        {
            return false;
        }
        if (!LoadActiveSetPredictorFromFile(directoryPath / "active_set.predictorbin", outBundle.activeSetPredictor, outError))
        {
            return false;
        }
        std::ifstream in(directoryPath / "hybrid_bundle.cfg");
        if (!in)
        {
            outError = "Failed to open hybrid_bundle.cfg.";
            return false;
        }
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::string key;
            std::string value;
            if (ParseHybridConfigLine(line, key, value))
            {
                ApplyHybridConfigValue(outBundle, key, value);
            }
        }
        return true;
    }

    std::string BuildHybridModelBundleSummary(const HybridModelBundle& bundle,
        const HybridModelTrainingReport* report)
    {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(5);
        ss << "priorGuides=" << bundle.trainingSettings.priorGuideCount
            << " residualGuides=" << bundle.trainingSettings.residualGuideCount
            << " residualBlend=" << bundle.trainingSettings.residualBlend
            << " smoothingAlpha=" << bundle.trainingSettings.priorGuideSmoothingAlpha
            << " uncertaintyAlpha=" << bundle.uncertaintySmoothingAlpha
            << " penetrationGain=" << bundle.penetrationGain
            << " priorVelPred=" << bundle.priorVelocityPredictionGain
            << " residualVelPred=" << bundle.residualVelocityPredictionGain
            << " priorAccelPred=" << bundle.priorAccelerationPredictionGain
            << " residualAccelPred=" << bundle.residualAccelerationPredictionGain
            << " priorTemporalBlend=" << bundle.priorTemporalBlend
            << " residualTemporalBlend=" << bundle.residualTemporalBlend
            << " predictorBlend=" << bundle.activeSetPredictorBlend
            << " frozenBlend=" << bundle.frozenStrandBlend
            << " cachedBlend=" << bundle.cachedStrandBlend
            << " budgetClusters=" << bundle.budget.maxActiveClusters
            << " budgetGuides=" << bundle.budget.maxActiveGuides;
        if (report)
        {
            ss << " priorRms=" << report->priorEvaluation.rmsPositionError
                << " residualRms=" << report->residualEvaluation.rmsPositionError
                << " priorTemporalRms=" << report->priorTemporalEvaluation.rmsPositionError
                << " residualTemporalRms=" << report->residualTemporalEvaluation.rmsPositionError
                << " activeSetPredictorAcc=" << report->activeSetPredictorEvaluation.classificationAccuracy
                << " avgResidualMag=" << report->averageResidualMagnitude;
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
        outError.clear();
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

        const ColliderSet runtimeColliders = !HasAnyCollider(colliders)
            ? BuildFallbackTeacherColliders(teacherSequence.referenceAsset)
            : colliders;
        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(teacherSequence.referenceAsset, bundle.clusterSettings);
        if (clusters.empty())
        {
            outError = "Failed to build clusters for full hybrid runtime.";
            return false;
        }

        HairCache hairCache;
        GuideCache guideCache;
        const float runtimeFrameRate = (settings.frameRateOverride > 0.0f) ? settings.frameRateOverride : teacherSequence.frameRate;
        if (!InitializeHairCacheForAsset(teacherSequence.referenceAsset, runtimeFrameRate, hairCache, outError) ||
            !InitializeGuideCacheForAsset(teacherSequence.referenceAsset, runtimeFrameRate, guideCache, outError))
        {
            return false;
        }

        std::ofstream frameCsv(outputDirectory / "full_hybrid_frames.csv");
        if (!frameCsv)
        {
            outError = "Failed to open full_hybrid_frames.csv for writing.";
            return false;
        }
        frameCsv << "frameIndex,timeSeconds,priorRms,hybridRmsBeforeCorrection,hybridRmsAfterCorrection,finalOutputRms,estimatedFrameMs,estimatedPlanMemoryMegabytes,selectedClusters,selectedGuides,selectedStrands,selectedNaturalnessGain,averageEfficiencyScore,editCoverageRatio,budgetScale,recommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctedStrands,correctedPoints,avgPenetration,maxPenetration,avgGuideContact,avgGuidePenetration,avgGuideUncertainty,maxGuideUncertainty,avgGuideModelError,avgGuidePredictorRisk,maxGuidePredictorRisk\n";

        std::vector<Vec3> restPriorGuidePoints;
        std::vector<Vec3> smoothedPriorGuidePoints;
        std::vector<Vec3> previousSmoothedPriorGuidePoints;
        std::vector<Vec3> previousPreviousSmoothedPriorGuidePoints;
        std::vector<Vec3> previousRawPriorGuidePoints;
        std::vector<Vec3> previousPreviousRawPriorGuidePoints;
        BuildRestGuideFramePointsForModel(bundle.priorModel, restPriorGuidePoints);
        smoothedPriorGuidePoints = restPriorGuidePoints;
        previousSmoothedPriorGuidePoints = smoothedPriorGuidePoints;
        previousPreviousSmoothedPriorGuidePoints = previousSmoothedPriorGuidePoints;
        previousRawPriorGuidePoints = restPriorGuidePoints;
        previousPreviousRawPriorGuidePoints = previousRawPriorGuidePoints;

        std::vector<Vec3> restResidualGuidePoints;
        std::vector<Vec3> previousResidualGuidePoints;
        std::vector<Vec3> previousPreviousResidualGuidePoints;
        std::vector<Vec3> previousBaseResidualGuideInput;
        std::vector<Vec3> previousPreviousBaseResidualGuideInput;
        BuildRestGuideFramePointsForModel(bundle.residualModel, restResidualGuidePoints);
        previousResidualGuidePoints = restResidualGuidePoints;
        previousPreviousResidualGuidePoints = previousResidualGuidePoints;
        previousBaseResidualGuideInput = restResidualGuidePoints;
        previousPreviousBaseResidualGuideInput = previousBaseResidualGuideInput;

        std::vector<Vec3> restGuideRoots;
        BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherSequence.referenceAsset.restPoints, restGuideRoots);
        std::vector<Vec3> previousHybridGuideRoots = restGuideRoots;
        std::vector<float> previousGuideUncertainty(previousHybridGuideRoots.size(), 0.0f);
        std::vector<Vec3> previousOutputPoints = teacherSequence.referenceAsset.restPoints;
        std::vector<Vec3> cachedTierSnapshotPoints = teacherSequence.referenceAsset.restPoints;

        double priorRmsSum = 0.0;
        double hybridBeforeRmsSum = 0.0;
        double hybridAfterRmsSum = 0.0;
        double selectedClusterSum = 0.0;
        double selectedGuideSum = 0.0;
        double correctedPointSum = 0.0;
        double penetrationSum = 0.0;
        double predictorRiskSum = 0.0;
        double silhouetteErrorSum = 0.0;
        double clumpPreservationSum = 0.0;
        double rootLocalitySum = 0.0;
        uint32_t penetrationEventCount = 0u;
        float peakEstimatedRamMegabytes = 0.0f;
        float maxHybridAfterRms = 0.0f;
        float maxPenetration = 0.0f;
        float maxGuidePredictorRisk = 0.0f;
        float previousFrameMilliseconds = 0.0f;
        std::vector<float> estimatedFrameMillisecondsHistory;

        const float residualBlend = (settings.residualBlendOverride >= 0.0f)
            ? settings.residualBlendOverride
            : bundle.trainingSettings.residualBlend;
        const float predictorBlend = (settings.activeSetPredictorBlendOverride >= 0.0f)
            ? settings.activeSetPredictorBlendOverride
            : bundle.activeSetPredictorBlend;

        for (uint32_t frameIndex = 0u; frameIndex < teacherSequence.FrameCount(); ++frameIndex)
        {
            std::vector<Vec3> teacherFrame;
            CopySequenceFrame(teacherSequence, frameIndex, teacherFrame);

            std::vector<Vec3> currentPriorGuidePoints;
            BuildGuideFramePointsForModel(bundle.priorModel, teacherFrame, currentPriorGuidePoints);
            SmoothGuidePoints(smoothedPriorGuidePoints,
                currentPriorGuidePoints,
                bundle.trainingSettings.priorGuideSmoothingAlpha);

            const float priorVelocityPredictionGain = Clamp(bundle.priorVelocityPredictionGain, 0.0f, 1.0f);
            const float priorAccelerationPredictionGain = EstimateSecondOrderPredictionGain(previousPreviousSmoothedPriorGuidePoints,
                previousSmoothedPriorGuidePoints,
                smoothedPriorGuidePoints,
                priorVelocityPredictionGain,
                bundle.priorAccelerationPredictionGain,
                0.50f);
            std::vector<Vec3> predictedPriorGuidePoints = PredictSecondOrderGuidePoints(
                smoothedPriorGuidePoints,
                previousSmoothedPriorGuidePoints,
                previousPreviousSmoothedPriorGuidePoints,
                priorVelocityPredictionGain,
                priorAccelerationPredictionGain);
            if (settings.enableTemporalNetworks && frameIndex >= 2u && IsTemporalGuideNetworkReady(bundle.priorTemporalNetwork))
            {
                std::vector<Vec3> temporalPriorGuidePoints;
                if (PredictTemporalGuideFrame(bundle.priorTemporalNetwork,
                    restPriorGuidePoints,
                    previousPreviousRawPriorGuidePoints,
                    previousRawPriorGuidePoints,
                    currentPriorGuidePoints,
                    temporalPriorGuidePoints,
                    outError))
                {
                    BlendGuidePoints(predictedPriorGuidePoints,
                        temporalPriorGuidePoints,
                        bundle.priorTemporalBlend);
                }
                else
                {
                    return false;
                }
            }

            HairAsset priorAsset;
            if (settings.useTeacherPhysicsProxy)
            {
                priorAsset = teacherSequence.referenceAsset;
                priorAsset.restPoints = teacherFrame;
            }
            else if (!ReconstructReducedModelFrame(bundle.priorModel, predictedPriorGuidePoints, priorAsset, outError))
            {
                return false;
            }

            std::vector<Vec3> baseResidualGuideInput = restResidualGuidePoints;
            HairAsset residualAsset = priorAsset;
            std::vector<Vec3> currentResidualGuidePoints;
            if (settings.enableResidualModel && !settings.useTeacherPhysicsProxy)
            {
                BuildGuideFramePointsForModel(bundle.residualModel, teacherFrame, currentResidualGuidePoints);
                std::vector<Vec3> priorResidualGuidePoints;
                BuildGuideFramePointsForModel(bundle.residualModel, priorAsset.restPoints, priorResidualGuidePoints);
                const float residualVelocityPredictionGain = Clamp(bundle.residualVelocityPredictionGain, 0.0f, 1.0f);
                const float residualAccelerationPredictionGain = EstimateSecondOrderPredictionGain(previousPreviousResidualGuidePoints,
                    previousResidualGuidePoints,
                    currentResidualGuidePoints,
                    residualVelocityPredictionGain,
                    bundle.residualAccelerationPredictionGain,
                    0.60f);
                for (uint32_t i = 0u; i < baseResidualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    baseResidualGuideInput[i] += (currentResidualGuidePoints[i] - priorResidualGuidePoints[i]) * bundle.trainingSettings.residualGuideGain;
                }
                std::vector<Vec3> residualGuideInput = baseResidualGuideInput;
                for (uint32_t i = 0u; i < residualGuideInput.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    if (i < previousResidualGuidePoints.size())
                    {
                        const Vec3 velocity = currentResidualGuidePoints[i] - previousResidualGuidePoints[i];
                        residualGuideInput[i] += velocity * residualVelocityPredictionGain;
                    }
                    if (i < previousResidualGuidePoints.size() && i < previousPreviousResidualGuidePoints.size())
                    {
                        const Vec3 acceleration = currentResidualGuidePoints[i] - previousResidualGuidePoints[i] * 2.0f + previousPreviousResidualGuidePoints[i];
                        residualGuideInput[i] += acceleration * residualAccelerationPredictionGain;
                    }
                }
                if (settings.enableTemporalNetworks && frameIndex >= 2u && IsTemporalGuideNetworkReady(bundle.residualTemporalNetwork))
                {
                    std::vector<Vec3> temporalResidualGuidePoints;
                    if (PredictTemporalGuideFrame(bundle.residualTemporalNetwork,
                        restResidualGuidePoints,
                        previousPreviousBaseResidualGuideInput,
                        previousBaseResidualGuideInput,
                        baseResidualGuideInput,
                        temporalResidualGuidePoints,
                        outError))
                    {
                        const float temporalBlend = Clamp(bundle.residualTemporalBlend, 0.0f, 1.0f);
                        for (uint32_t i = 0u; i < residualGuideInput.size() && i < temporalResidualGuidePoints.size(); ++i)
                        {
                            residualGuideInput[i] = Lerp(residualGuideInput[i], temporalResidualGuidePoints[i], temporalBlend);
                        }
                    }
                    else
                    {
                        return false;
                    }
                }
                if (!ReconstructReducedModelFrame(bundle.residualModel, residualGuideInput, residualAsset, outError))
                {
                    return false;
                }
            }

            std::vector<Vec3> hybridBeforeCorrection;
            if (settings.useTeacherPhysicsProxy)
            {
                hybridBeforeCorrection = teacherFrame;
            }
            else if (settings.enableResidualModel)
            {
                ComposeHybridPoints(teacherSequence.referenceAsset,
                    priorAsset.restPoints,
                    residualAsset.restPoints,
                    residualBlend,
                    hybridBeforeCorrection);
            }
            else
            {
                hybridBeforeCorrection = priorAsset.restPoints;
            }

            std::vector<Vec3> currentHybridGuideRoots;
            BuildGuideRootsFromPoints(teacherSequence.referenceAsset, hybridBeforeCorrection, currentHybridGuideRoots);
            std::vector<Vec3> teacherGuideRoots;
            BuildGuideRootsFromPoints(teacherSequence.referenceAsset, teacherFrame, teacherGuideRoots);
            std::vector<float> guideContactScores = BuildGuideContactScores(teacherSequence.referenceAsset,
                hybridBeforeCorrection,
                runtimeColliders,
                -1.05f,
                bundle.correctiveSettings.shellDistance);
            std::vector<float> guidePenetrationScores = BuildGuidePenetrationScores(teacherSequence.referenceAsset,
                hybridBeforeCorrection,
                runtimeColliders,
                -1.05f,
                bundle.correctiveSettings.shellDistance);
            const uint32_t guideCount = std::min<uint32_t>(teacherSequence.referenceAsset.guideStrandCount, static_cast<uint32_t>(teacherSequence.referenceAsset.strands.size()));
            std::vector<float> guideModelErrorScores(guideCount, 0.0f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const Vec3 teacherRoot = (guideIndex < teacherGuideRoots.size()) ? teacherGuideRoots[guideIndex] : Vec3(0.0f, 0.0f, 0.0f);
                const Vec3 hybridRoot = (guideIndex < currentHybridGuideRoots.size()) ? currentHybridGuideRoots[guideIndex] : teacherRoot;
                const StrandRange& strand = teacherSequence.referenceAsset.strands[guideIndex];
                const uint32_t pointIndex = strand.pointOffset;
                const float residualMagnitude = (pointIndex < residualAsset.restPoints.size() && pointIndex < teacherSequence.referenceAsset.restPoints.size())
                    ? Length(residualAsset.restPoints[pointIndex] - teacherSequence.referenceAsset.restPoints[pointIndex])
                    : 0.0f;
                const float rootError = Clamp(Length(hybridRoot - teacherRoot) / 0.025f, 0.0f, 1.0f);
                guideModelErrorScores[guideIndex] = Clamp(std::max(rootError, Clamp(residualMagnitude / 0.025f, 0.0f, 1.0f)) * bundle.uncertaintyGain,
                    0.0f,
                    1.0f);
                guideContactScores[guideIndex] = Clamp(guideContactScores[guideIndex] * bundle.contactGain, 0.0f, 1.0f);
                guidePenetrationScores[guideIndex] = Clamp(guidePenetrationScores[guideIndex] * bundle.penetrationGain, 0.0f, 1.0f);
            }
            std::vector<float> guideUncertainty = BuildGuideUncertainty(teacherSequence.referenceAsset,
                currentHybridGuideRoots,
                previousHybridGuideRoots,
                previousGuideUncertainty,
                guideContactScores,
                guidePenetrationScores,
                guideModelErrorScores,
                1.0f / std::max(runtimeFrameRate, 1.0f),
                bundle.temporalCoherenceGain,
                bundle.uncertaintySmoothingAlpha);
            std::vector<float> guidePredictorRisk;
            if (settings.enableActiveSetPredictor)
            {
                ApplyActiveSetPredictorBoost(teacherSequence.referenceAsset,
                    clusters,
                    restGuideRoots,
                    currentHybridGuideRoots,
                    previousHybridGuideRoots,
                    guidePenetrationScores,
                    guideModelErrorScores,
                    bundle.activeSetPredictor,
                    predictorBlend,
                    bundle.scoringSettings,
                    guideUncertainty,
                    guideContactScores,
                    &guidePredictorRisk);
            }
            else
            {
                guidePredictorRisk.assign(guideUncertainty.size(), 0.0f);
            }

            ActiveSetRuntimeInput runtimeInput = {};
            runtimeInput.currentGuideRoots = currentHybridGuideRoots;
            runtimeInput.previousGuideRoots = previousHybridGuideRoots;
            runtimeInput.guideContactScores = guideContactScores;
            runtimeInput.guideUncertainty = guideUncertainty;
            runtimeInput.cameraPosition = Vec3(0.0f, 1.55f, -1.85f);
            runtimeInput.cameraForward = Normalize(Vec3(0.0f, -0.05f, 1.0f));
            runtimeInput.fixedTimeStep = 1.0f / std::max(runtimeFrameRate, 1.0f);

            ActiveSetBudget budget = settings.useBudgetOverride ? settings.budgetOverride : bundle.budget;
            budget.previousFrameMilliseconds = previousFrameMilliseconds;
            ActiveSetPlan plan;
            if (settings.useTeacherPhysicsProxy)
            {
                BuildPurePhysicsProxyPlan(teacherSequence.referenceAsset, clusters, budget, plan);
            }
            else if (!EvaluateActiveSetPlan(teacherSequence.referenceAsset, clusters, runtimeInput, bundle.scoringSettings, budget, plan, outError))
            {
                return false;
            }

            std::vector<Vec3> correctedPoints = hybridBeforeCorrection;
            SparseCorrectiveReport correctiveReport = {};
            if (settings.useTeacherPhysicsProxy)
            {
                correctedPoints = teacherFrame;
                correctiveReport.correctedStrandCount = static_cast<uint32_t>(teacherSequence.referenceAsset.strands.size());
                correctiveReport.correctedPointCount = static_cast<uint32_t>(teacherFrame.size());
            }
            else if (settings.enableSparseCorrectivePhysics && !ApplySparseCorrectivePhysics(teacherSequence.referenceAsset,
                plan,
                runtimeColliders,
                correctedPoints,
                bundle.correctiveSettings,
                &correctiveReport,
                outError))
            {
                return false;
            }

            std::vector<Vec3> finalOutputPoints = correctedPoints;
            std::vector<Vec3> tierScratchPoints;
            for (uint32_t strandIndex = 0u; strandIndex < teacherSequence.referenceAsset.strands.size(); ++strandIndex)
            {
                const SimTier tier = (strandIndex < plan.recommendedSimulationTiers.size())
                    ? plan.recommendedSimulationTiers[strandIndex]
                    : SimTier::Reduced;
                switch (tier)
                {
                case SimTier::Full:
                    break;
                case SimTier::Reduced:
                    CopyStrandPoints(teacherSequence.referenceAsset, hybridBeforeCorrection, strandIndex, tierScratchPoints);
                    WriteStrandPoints(teacherSequence.referenceAsset, strandIndex, tierScratchPoints, finalOutputPoints);
                    break;
                case SimTier::Frozen:
                    BuildRootAnchoredTierPose(teacherSequence.referenceAsset,
                        strandIndex,
                        hybridBeforeCorrection,
                        previousOutputPoints,
                        bundle.frozenRootFollow,
                        bundle.frozenStrandBlend,
                        tierScratchPoints);
                    WriteStrandPoints(teacherSequence.referenceAsset, strandIndex, tierScratchPoints, finalOutputPoints);
                    break;
                case SimTier::Cached:
                    BuildRootAnchoredTierPose(teacherSequence.referenceAsset,
                        strandIndex,
                        hybridBeforeCorrection,
                        cachedTierSnapshotPoints,
                        bundle.cachedRootFollow,
                        bundle.cachedStrandBlend,
                        tierScratchPoints);
                    WriteStrandPoints(teacherSequence.referenceAsset, strandIndex, tierScratchPoints, finalOutputPoints);
                    break;
                }
            }

            const uint32_t cacheRefreshInterval = std::max<uint32_t>(1u, bundle.cachedFrameRefreshInterval);
            for (uint32_t strandIndex = 0u; strandIndex < teacherSequence.referenceAsset.strands.size(); ++strandIndex)
            {
                const SimTier tier = (strandIndex < plan.recommendedSimulationTiers.size())
                    ? plan.recommendedSimulationTiers[strandIndex]
                    : SimTier::Reduced;
                if (tier != SimTier::Cached || frameIndex == 0u || ((frameIndex + 1u) % cacheRefreshInterval) == 0u)
                {
                    CopyStrandPoints(teacherSequence.referenceAsset, finalOutputPoints, strandIndex, tierScratchPoints);
                    WriteStrandPoints(teacherSequence.referenceAsset, strandIndex, tierScratchPoints, cachedTierSnapshotPoints);
                }
            }

            if (!AppendHairCacheFrame(hairCache, teacherSequence.referenceAsset, finalOutputPoints, outError))
            {
                return false;
            }
            std::vector<Vec3> guideFramePoints;
            BuildGuideFramePoints(teacherSequence.referenceAsset, finalOutputPoints, guideFramePoints);
            if (!AppendGuideCacheGuidePoints(guideCache, guideFramePoints, outError))
            {
                return false;
            }
            std::vector<Vec3> finalGuideRoots;
            BuildGuideRootsFromPoints(teacherSequence.referenceAsset, finalOutputPoints, finalGuideRoots);

            const float priorRms = ComputeRmsError(priorAsset.restPoints, teacherFrame);
            const float hybridBeforeRms = ComputeRmsError(hybridBeforeCorrection, teacherFrame);
            const float hybridAfterRms = ComputeRmsError(correctedPoints, teacherFrame);
            const float finalOutputRms = ComputeRmsError(finalOutputPoints, teacherFrame);
            const HairAsset teacherFrameAsset = BuildFrameAsset(teacherSequence.referenceAsset, teacherFrame);
            const HairAsset finalFrameAsset = BuildFrameAsset(teacherSequence.referenceAsset, finalOutputPoints);
            const HairShapeComparisonReport shapeReport = CompareHairShape(teacherFrameAsset, finalFrameAsset, 12u);

            const float estimatedFrameMilliseconds = settings.useTeacherPhysicsProxy
                ? (2.80f + static_cast<float>(teacherSequence.referenceAsset.strands.size()) * 0.020f + static_cast<float>(teacherSequence.referenceAsset.restPoints.size()) * 0.0022f + static_cast<float>(std::max<uint32_t>(budget.maxSolverIterations, budget.minSolverIterations)) * 0.35f)
                : (1.10f +
                    static_cast<float>(plan.summary.selectedClusterCount) * 0.32f +
                    static_cast<float>(plan.summary.selectedGuideCount) * 0.012f +
                    static_cast<float>(correctiveReport.correctedPointCount) * 0.0019f +
                    static_cast<float>(plan.summary.recommendedSolverIterations) * 0.24f);

        }
    }
