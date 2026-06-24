// HybridRuntime.cpp ordered part 02: hybrid_reduction_and_error_reports
// This file is included by ../HybridRuntime.cpp; do not compile it directly.

frameCsv << frameIndex << ","
<< timeSeconds << ","
<< plan.summary.selectedClusterCount << ","
<< plan.summary.selectedGuideCount << ","
<< plan.summary.selectedStrandCount << ","
<< plan.summary.selectedNaturalnessGain << ","
<< plan.summary.averageEfficiencyScore << ","
<< plan.summary.editCoverageRatio << ","
<< plan.summary.budgetScale << ","
<< plan.summary.recommendedSolverIterations << ","
<< plan.summary.fullTierCount << ","
<< plan.summary.reducedTierCount << ","
<< plan.summary.frozenTierCount << ","
<< plan.summary.cachedTierCount << ","
<< correctiveReport.correctedStrandCount << ","
<< correctiveReport.correctedPointCount << ","
<< correctiveReport.averagePenetration << ","
<< correctiveReport.maxPenetration << ","
<< ComputeAverageScalar(guideContactScores) << ","
<< ComputeAverageScalar(guidePenetrationScores) << ","
<< ComputeAverageScalar(guideUncertainty) << ","
<< ComputeMaxScalar(guideUncertainty) << ","
<< correctiveReport.averageLengthErrorBefore << ","
<< correctiveReport.averageLengthErrorAfter << ","
<< estimatedSimulationMilliseconds << ","
<< plan.summary.estimatedMemoryMegabytes << "\n";

selectedClusterSum += static_cast<double>(plan.summary.selectedClusterCount);
selectedGuideSum += static_cast<double>(plan.summary.selectedGuideCount);
selectedStrandSum += static_cast<double>(plan.summary.selectedStrandCount);
penetrationSum += static_cast<double>(correctiveReport.averagePenetration);
correctedPointSum += static_cast<double>(correctiveReport.correctedPointCount);
maxPenetration = std::max(maxPenetration, correctiveReport.maxPenetration);
previousGuideRoots = currentGuideRoots;
previousGuideUncertainty = guideUncertainty;
        }

        if (!SaveHairCacheToFile(outputDirectory / "hybrid_teacher.haircache", hairCache, outError) ||
            !SaveGuideCacheToFile(outputDirectory / "hybrid_teacher_guides.guidecache", guideCache, outError) ||
            !SaveHairAssetToHairBinary(outputDirectory / "hybrid_teacher_reference.hairbin", asset, outError))
        {
            return false;
        }

        const float frameCountF = static_cast<float>(std::max<uint32_t>(1u, settings.frameCount));
        outReport.frameCount = std::max<uint32_t>(1u, settings.frameCount);
        outReport.averageSelectedClusterCount = static_cast<float>(selectedClusterSum / frameCountF);
        outReport.averageSelectedGuideCount = static_cast<float>(selectedGuideSum / frameCountF);
        outReport.averageSelectedStrandCount = static_cast<float>(selectedStrandSum / frameCountF);
        outReport.averagePenetration = static_cast<float>(penetrationSum / frameCountF);
        outReport.maxPenetration = maxPenetration;
        outReport.averageCorrectedPointCount = static_cast<float>(correctedPointSum / frameCountF);

        std::ofstream summaryJson(outputDirectory / "hybrid_teacher_summary.json");
        if (!summaryJson)
        {
            outError = "Failed to open hybrid_teacher_summary.json for writing.";
            return false;
        }
        summaryJson << std::fixed << std::setprecision(6);
        summaryJson << "{\n";
        summaryJson << "  \"frameCount\": " << outReport.frameCount << ",\n";
        summaryJson << "  \"averageSelectedClusterCount\": " << outReport.averageSelectedClusterCount << ",\n";
        summaryJson << "  \"averageSelectedGuideCount\": " << outReport.averageSelectedGuideCount << ",\n";
        summaryJson << "  \"averageSelectedStrandCount\": " << outReport.averageSelectedStrandCount << ",\n";
        summaryJson << "  \"averagePenetration\": " << outReport.averagePenetration << ",\n";
        summaryJson << "  \"maxPenetration\": " << outReport.maxPenetration << ",\n";
        summaryJson << "  \"averageCorrectedPointCount\": " << outReport.averageCorrectedPointCount << ",\n";
        summaryJson << "  \"peakEstimatedSimulationMilliseconds\": " << outReport.peakEstimatedSimulationMilliseconds << "\n";
        summaryJson << "}\n";
        return true;
    }

    bool LoadHybridTeacherSequenceFromDirectory(const std::filesystem::path& directoryPath,
        ReducedModelTrainingSequence& outSequence,
        std::string& outError)
    {
        outError.clear();
        HairAsset referenceAsset;
        HairCache hairCache;
        if (!LoadHairAssetFromHairBinary(directoryPath / "hybrid_teacher_reference.hairbin", referenceAsset, outError))
        {
            return false;
        }
        if (!LoadHairCacheFromFile(directoryPath / "hybrid_teacher.haircache", hairCache, outError))
        {
            return false;
        }
        if (hairCache.pointCount != referenceAsset.restPoints.size())
        {
            outError = "Teacher hair cache point count does not match hybrid_teacher_reference.hairbin.";
            return false;
        }

        outSequence = {};
        outSequence.referenceAsset = std::move(referenceAsset);
        outSequence.frameRate = hairCache.frameRate;
        outSequence.frames = hairCache.frames;
        return true;
    }

    namespace
    {
        const Vec3* SequenceFramePoints(const ReducedModelTrainingSequence& sequence, uint32_t frameIndex)
        {
            const uint32_t pointCount = sequence.PointCountPerFrame();
            if (pointCount == 0u || frameIndex >= sequence.FrameCount())
            {
                return nullptr;
            }
            return sequence.frames.data() + static_cast<size_t>(frameIndex) * pointCount;
        }

        const Vec3* CacheFramePoints(const HairCache& cache, uint32_t frameIndex)
        {
            if (cache.pointCount == 0u || frameIndex >= cache.FrameCount())
            {
                return nullptr;
            }
            return cache.frames.data() + static_cast<size_t>(frameIndex) * cache.pointCount;
        }

        void AppendSequenceFrame(const std::vector<Vec3>& frame, ReducedModelTrainingSequence& ioSequence)
        {
            ioSequence.frames.insert(ioSequence.frames.end(), frame.begin(), frame.end());
        }

        void CopySequenceFrame(const ReducedModelTrainingSequence& sequence, uint32_t frameIndex, std::vector<Vec3>& outPoints)
        {
            const Vec3* frame = SequenceFramePoints(sequence, frameIndex);
            if (!frame)
            {
                outPoints.clear();
                return;
            }
            outPoints.assign(frame, frame + sequence.PointCountPerFrame());
        }

        void BuildGuideFramePointsForModel(const ReducedModel& model,
            const std::vector<Vec3>& fullPoints,
            std::vector<Vec3>& outGuideFramePoints)
        {
            outGuideFramePoints.clear();
            const uint32_t particlesPerStrand = model.referenceAsset.particlesPerStrand;
            outGuideFramePoints.reserve(static_cast<size_t>(model.selectedGuideStrandIndices.size()) * particlesPerStrand);
            for (uint32_t selectedGuide : model.selectedGuideStrandIndices)
            {
                if (selectedGuide >= model.referenceAsset.strands.size())
                {
                    for (uint32_t i = 0u; i < particlesPerStrand; ++i)
                    {
                        outGuideFramePoints.push_back(Vec3(0.0f, 0.0f, 0.0f));
                    }
                    continue;
                }
                const StrandRange& strand = model.referenceAsset.strands[selectedGuide];
                const uint32_t lastLocalPoint = (strand.pointCount > 0u) ? (strand.pointCount - 1u) : 0u;
                for (uint32_t localPoint = 0u; localPoint < particlesPerStrand; ++localPoint)
                {
                    const uint32_t clampedLocalPoint = std::min(localPoint, lastLocalPoint);
                    const uint32_t pointIndex = strand.pointOffset + clampedLocalPoint;
                    outGuideFramePoints.push_back((pointIndex < fullPoints.size()) ? fullPoints[pointIndex] : Vec3(0.0f, 0.0f, 0.0f));
                }
            }
        }

        void BuildRestGuideFramePointsForModel(const ReducedModel& model, std::vector<Vec3>& outGuideFramePoints)
        {
            BuildGuideFramePointsForModel(model, model.referenceAsset.restPoints, outGuideFramePoints);
        }

        void BuildGuidePointSequenceForModel(const ReducedModel& model,
            const ReducedModelTrainingSequence& sequence,
            std::vector<Vec3>& outGuideFrames)
        {
            outGuideFrames.clear();
            const uint32_t frameCount = sequence.FrameCount();
            const uint32_t guidePointCount = static_cast<uint32_t>(model.selectedGuideStrandIndices.size()) * model.referenceAsset.particlesPerStrand;
            outGuideFrames.reserve(static_cast<size_t>(frameCount) * guidePointCount);
            for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
            {
                std::vector<Vec3> fullFrame;
                CopySequenceFrame(sequence, frameIndex, fullFrame);
                std::vector<Vec3> guideFrame;
                BuildGuideFramePointsForModel(model, fullFrame, guideFrame);
                outGuideFrames.insert(outGuideFrames.end(), guideFrame.begin(), guideFrame.end());
            }
        }

        void BuildPriorTemporalTrainingSequences(const ReducedModel& model,
            const ReducedModelTrainingSequence& teacherSequence,
            const ReducedModelTrainingSequence& priorSequence,
            std::vector<Vec3>& outInputFrames,
            std::vector<Vec3>& outTargetFrames)
        {
            BuildGuidePointSequenceForModel(model, teacherSequence, outInputFrames);
            BuildGuidePointSequenceForModel(model, priorSequence, outTargetFrames);
        }

        void BuildResidualTemporalTrainingSequences(const ReducedModel& model,
            const ReducedModelTrainingSequence& teacherSequence,
            const ReducedModelTrainingSequence& priorSequence,
            float residualGuideGain,
            std::vector<Vec3>& outInputFrames,
            std::vector<Vec3>& outTargetFrames)
        {
            outInputFrames.clear();
            outTargetFrames.clear();
            const uint32_t frameCount = std::min(teacherSequence.FrameCount(), priorSequence.FrameCount());
            for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
            {
                std::vector<Vec3> teacherFrame;
                std::vector<Vec3> priorFrame;
                CopySequenceFrame(teacherSequence, frameIndex, teacherFrame);
                CopySequenceFrame(priorSequence, frameIndex, priorFrame);

                std::vector<Vec3> currentResidualGuidePoints;
                std::vector<Vec3> priorResidualGuidePoints;
                std::vector<Vec3> restResidualGuidePoints;
                BuildGuideFramePointsForModel(model, teacherFrame, currentResidualGuidePoints);
                BuildGuideFramePointsForModel(model, priorFrame, priorResidualGuidePoints);
                BuildRestGuideFramePointsForModel(model, restResidualGuidePoints);

                std::vector<Vec3> inputFrame = restResidualGuidePoints;
                for (uint32_t i = 0u; i < inputFrame.size() && i < currentResidualGuidePoints.size() && i < priorResidualGuidePoints.size(); ++i)
                {
                    inputFrame[i] += (currentResidualGuidePoints[i] - priorResidualGuidePoints[i]) * residualGuideGain;
                }
                outInputFrames.insert(outInputFrames.end(), inputFrame.begin(), inputFrame.end());
                outTargetFrames.insert(outTargetFrames.end(), currentResidualGuidePoints.begin(), currentResidualGuidePoints.end());
            }
        }

        float ComputeRmsError(const std::vector<Vec3>& a, const std::vector<Vec3>& b)
        {
            if (a.size() != b.size() || a.empty())
            {
                return 0.0f;
            }
            RmsAccumulator rms;
            for (size_t i = 0u; i < a.size(); ++i)
            {
                rms.AddVectorError(a[i] - b[i]);
            }
            return rms.Rms();
        }

        float ComputeAverageMagnitude(const std::vector<Vec3>& points)
        {
            if (points.empty())
            {
                return 0.0f;
            }
            double total = 0.0;
            for (const Vec3& p : points)
            {
                total += static_cast<double>(Length(p));
            }
            return static_cast<float>(total / static_cast<double>(points.size()));
        }

        float ComputeAverageScalar(const std::vector<float>& values)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            double total = 0.0;
            for (float v : values)
            {
                total += static_cast<double>(v);
            }
            return static_cast<float>(total / static_cast<double>(values.size()));
        }

        float ComputeMaxScalar(const std::vector<float>& values)
        {
            float best = 0.0f;
            for (float v : values)
            {
                best = std::max(best, v);
            }
            return best;
        }

        float ComputeQuantile95(std::vector<float> values)
        {
            if (values.empty())
            {
                return 0.0f;
            }
            std::sort(values.begin(), values.end());
            const size_t index = std::min(values.size() - 1u,
                static_cast<size_t>(std::floor(static_cast<double>(values.size() - 1u) * 0.95)));
            return values[index];
        }

        float SafeSigmoid(float x)
        {
            if (x >= 0.0f)
            {
                const float e = std::exp(-x);
                return 1.0f / (1.0f + e);
            }
            const float e = std::exp(x);
            return e / (1.0f + e);
        }

        float AverageScalarAtIndices(const std::vector<float>& values, const std::vector<uint32_t>& indices)
        {
            if (values.empty() || indices.empty())
            {
                return 0.0f;
            }
            double total = 0.0;
            uint32_t count = 0u;
            for (uint32_t index : indices)
            {
                if (index < values.size())
                {
                    total += static_cast<double>(values[index]);
                    ++count;
                }
            }
            return (count > 0u) ? static_cast<float>(total / static_cast<double>(count)) : 0.0f;
        }

        void BuildGuideBoundsLocal(const std::vector<Vec3>& roots, Vec3& outMin, Vec3& outMax)
        {
            if (roots.empty())
            {
                outMin = Vec3(0.0f, 0.0f, 0.0f);
                outMax = Vec3(0.0f, 0.0f, 0.0f);
                return;
            }
            outMin = roots.front();
            outMax = roots.front();
            for (const Vec3& root : roots)
            {
                ExpandBounds(outMin, outMax, root);
            }
        }

        Vec3 AveragePositionLocal(const std::vector<Vec3>& positions, const std::vector<uint32_t>& indices)
        {
            if (positions.empty() || indices.empty())
            {
                return Vec3(0.0f, 0.0f, 0.0f);
            }
            Vec3 sum = {};
            uint32_t count = 0u;
            for (uint32_t index : indices)
            {
                if (index < positions.size())
                {
                    sum += positions[index];
                    ++count;
                }
            }
            return (count > 0u) ? (sum / static_cast<float>(count)) : Vec3(0.0f, 0.0f, 0.0f);
        }

        float ComputeDistanceVisibilityLocal(const Vec3& position,
            const Vec3& cameraPosition,
            const Vec3& cameraForward,
            float nearDistance,
            float farDistance)
        {
            const Vec3 toPoint = position - cameraPosition;
            const float distance = Length(toPoint);
            const float distanceWeight = 1.0f - Clamp((distance - nearDistance) / std::max(farDistance - nearDistance, 1.0e-4f), 0.0f, 1.0f);
            const Vec3 direction = Normalize(toPoint);
            const float facing = Clamp(Dot(direction, Normalize(cameraForward)) * 0.5f + 0.5f, 0.0f, 1.0f);
            return Clamp(distanceWeight * 0.75f + facing * 0.25f, 0.0f, 1.0f);
        }

        void ExtractActiveSetPredictorFeatures(const std::vector<ActiveSetCluster>& clusters,
            const std::vector<Vec3>& restGuideRoots,
            const std::vector<Vec3>& currentGuideRoots,
            const std::vector<Vec3>& previousGuideRoots,
            const std::vector<float>& guideUncertainty,
            const std::vector<float>& guideContactScores,
            const std::vector<float>& guidePenetrationScores,
            const std::vector<float>& guideModelErrorScores,
            const Vec3& cameraPosition,
            const Vec3& cameraForward,
            const ActiveSetScoringSettings& scoringSettings,
            uint32_t clusterIndex,
            std::array<float, 9u>& outFeatures)
        {
            outFeatures.fill(0.0f);
            if (clusterIndex >= clusters.size() || restGuideRoots.empty())
            {
                return;
            }

            const ActiveSetCluster& cluster = clusters[clusterIndex];
            Vec3 boundsMin = {};
            Vec3 boundsMax = {};
            BuildGuideBoundsLocal(restGuideRoots, boundsMin, boundsMax);
            const Vec3 boundsCenter = (boundsMin + boundsMax) * 0.5f;
            const float halfWidth = std::max((boundsMax.x - boundsMin.x) * 0.5f, 1.0e-4f);
            size_t maxClusterStrands = 1u;
            for (const ActiveSetCluster& candidate : clusters)
            {
                maxClusterStrands = std::max(maxClusterStrands, candidate.strandIndices.size());
            }
            const float safeTimeStep = 1.0f / 60.0f;
            const Vec3 centroidCurrent = AveragePositionLocal(currentGuideRoots, cluster.guideStrandIndices);
            const Vec3 centroidPrevious = AveragePositionLocal(previousGuideRoots, cluster.guideStrandIndices);
            const Vec3 centroidRest = AveragePositionLocal(restGuideRoots, cluster.guideStrandIndices);
            outFeatures[0] = Clamp(Length(centroidCurrent - centroidPrevious) / safeTimeStep /
                std::max(scoringSettings.velocitySoftRange, 1.0e-4f),
                0.0f,
                1.0f);
            outFeatures[1] = Clamp(Length(centroidCurrent - centroidRest) /
                std::max(scoringSettings.displacementSoftRange, 1.0e-4f),
                0.0f,
                1.0f);
            outFeatures[2] = ComputeDistanceVisibilityLocal(centroidCurrent,
                cameraPosition,
                cameraForward,
                scoringSettings.nearDistance,
                scoringSettings.farDistance);
            outFeatures[3] = Clamp(std::fabs(centroidRest.x - boundsCenter.x) / halfWidth, 0.0f, 1.0f);
            outFeatures[4] = Clamp(AverageScalarAtIndices(guideUncertainty, cluster.guideStrandIndices), 0.0f, 1.0f);
            outFeatures[5] = Clamp(AverageScalarAtIndices(guideContactScores, cluster.guideStrandIndices), 0.0f, 1.0f);
            outFeatures[6] = Clamp(AverageScalarAtIndices(guidePenetrationScores, cluster.guideStrandIndices), 0.0f, 1.0f);
            outFeatures[7] = Clamp(AverageScalarAtIndices(guideModelErrorScores, cluster.guideStrandIndices), 0.0f, 1.0f);
            outFeatures[8] = Clamp(static_cast<float>(cluster.strandIndices.size()) / static_cast<float>(std::max<size_t>(1u, maxClusterStrands)),
                0.0f,
                1.0f);
        }

        float PredictActiveSetRisk(const ActiveSetPredictor& predictor, const std::array<float, 9u>& features)
        {
            if (!IsActiveSetPredictorReady(predictor) || predictor.featureCount == 0u)
            {
                return 0.0f;
            }
            const uint32_t featureCount = std::min<uint32_t>(predictor.featureCount, static_cast<uint32_t>(features.size()));
            std::array<float, 9u> normalized = {};
            for (uint32_t featureIndex = 0u; featureIndex < featureCount; ++featureIndex)
            {
                const float mean = (featureIndex < predictor.featureMean.size()) ? predictor.featureMean[featureIndex] : 0.0f;
                const float stdDev = (featureIndex < predictor.featureStd.size()) ? std::max(predictor.featureStd[featureIndex], 1.0e-4f) : 1.0f;
                normalized[featureIndex] = ((features[featureIndex] - mean) / stdDev);
            }
            if (predictor.hiddenWidth > 0u &&
                predictor.hiddenWeights.size() == static_cast<size_t>(predictor.hiddenWidth) * predictor.featureCount &&
                predictor.hiddenBias.size() == predictor.hiddenWidth &&
                predictor.outputWeights.size() == predictor.hiddenWidth)
            {
                std::vector<float> hidden(predictor.hiddenWidth, 0.0f);
                for (uint32_t h = 0u; h < predictor.hiddenWidth; ++h)
                {
                    float sum = predictor.hiddenBias[h];
                    const size_t rowOffset = static_cast<size_t>(h) * predictor.featureCount;
                    for (uint32_t i = 0u; i < featureCount; ++i)
                    {
                        sum += predictor.hiddenWeights[rowOffset + i] * normalized[i];
                    }
                    hidden[h] = std::tanh(sum);
                }
                float sum = predictor.outputBias;
                for (uint32_t h = 0u; h < predictor.hiddenWidth; ++h)
                {
                    sum += predictor.outputWeights[h] * hidden[h];
                }
                return Clamp(SafeSigmoid(sum), 0.0f, 1.0f);
            }
            float sum = predictor.bias;
            for (uint32_t featureIndex = 0u; featureIndex < featureCount; ++featureIndex)
            {
                const float weight = (featureIndex < predictor.weights.size()) ? predictor.weights[featureIndex] : 0.0f;
                sum += normalized[featureIndex] * weight;
            }
            return Clamp(SafeSigmoid(sum), 0.0f, 1.0f);
        }

        void ApplyActiveSetPredictorBoost(const HairAsset& asset,
            const std::vector<ActiveSetCluster>& clusters,
            const std::vector<Vec3>& restGuideRoots,
            const std::vector<Vec3>& currentGuideRoots,
            const std::vector<Vec3>& previousGuideRoots,
            const std::vector<float>& guidePenetrationScores,
            const std::vector<float>& guideModelErrorScores,
            const ActiveSetPredictor& predictor,
            float predictorBlend,
            const ActiveSetScoringSettings& scoringSettings,
            std::vector<float>& inOutGuideUncertainty,
            std::vector<float>& inOutGuideContactScores,
            std::vector<float>* outGuidePredictorRisk)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            if (!IsActiveSetPredictorReady(predictor) || guideCount == 0u || predictorBlend <= 1.0e-5f)
            {
                if (outGuidePredictorRisk)
                {
                    outGuidePredictorRisk->assign(guideCount, 0.0f);
                }
                return;
            }
            if (inOutGuideUncertainty.size() < guideCount)
            {
                inOutGuideUncertainty.resize(guideCount, 0.0f);
            }
            if (inOutGuideContactScores.size() < guideCount)
            {
                inOutGuideContactScores.resize(guideCount, 0.0f);
            }
            if (outGuidePredictorRisk)
            {
                outGuidePredictorRisk->assign(guideCount, 0.0f);
            }

            for (uint32_t clusterIndex = 0u; clusterIndex < clusters.size(); ++clusterIndex)
            {
                std::array<float, 9u> features = {};
                ExtractActiveSetPredictorFeatures(clusters,
                    restGuideRoots,
                    currentGuideRoots,
                    previousGuideRoots,
                    inOutGuideUncertainty,
                    inOutGuideContactScores,
                    guidePenetrationScores,
                    guideModelErrorScores,
                    Vec3(0.0f, 1.55f, -1.85f),
                    Normalize(Vec3(0.0f, -0.05f, 1.0f)),
                    scoringSettings,
                    clusterIndex,
                    features);
                const float risk = PredictActiveSetRisk(predictor, features);
                const ActiveSetCluster& cluster = clusters[clusterIndex];
                for (uint32_t guideIndex : cluster.guideStrandIndices)
                {
                    if (guideIndex >= guideCount)
                    {
                        continue;
                    }
                    inOutGuideUncertainty[guideIndex] = Clamp(std::max(inOutGuideUncertainty[guideIndex], risk * predictorBlend), 0.0f, 1.0f);
                    inOutGuideContactScores[guideIndex] = Clamp(inOutGuideContactScores[guideIndex] + risk * predictorBlend * 0.18f, 0.0f, 1.0f);
                    if (outGuidePredictorRisk)
                    {
                        (*outGuidePredictorRisk)[guideIndex] = std::max((*outGuidePredictorRisk)[guideIndex], risk);
                    }
                }
            }
        }

        void BuildPurePhysicsProxyPlan(const HairAsset& asset,
            const std::vector<ActiveSetCluster>& clusters,
            const ActiveSetBudget& budget,
            ActiveSetPlan& outPlan)
        {
            outPlan = {};
            outPlan.clusters = clusters;
            outPlan.scores.reserve(clusters.size());
            outPlan.recommendedSimulationTiers.assign(asset.strands.size(), SimTier::Full);
            outPlan.strandCorrectiveMask.assign(asset.strands.size(), 1u);
            for (const ActiveSetCluster& cluster : clusters)
            {
                ActiveSetClusterScore score = {};
                score.clusterIndex = cluster.clusterIndex;
                score.finalScore = 1.0f;
                score.selected = true;
                outPlan.scores.push_back(score);
                outPlan.selectedClusterIndices.push_back(cluster.clusterIndex);
                outPlan.selectedGuideStrandIndices.insert(outPlan.selectedGuideStrandIndices.end(),
                    cluster.guideStrandIndices.begin(),
                    cluster.guideStrandIndices.end());
                outPlan.selectedStrandIndices.insert(outPlan.selectedStrandIndices.end(),
                    cluster.strandIndices.begin(),
                    cluster.strandIndices.end());
            }
            std::sort(outPlan.selectedGuideStrandIndices.begin(), outPlan.selectedGuideStrandIndices.end());
            outPlan.selectedGuideStrandIndices.erase(std::unique(outPlan.selectedGuideStrandIndices.begin(), outPlan.selectedGuideStrandIndices.end()),
                outPlan.selectedGuideStrandIndices.end());
            std::sort(outPlan.selectedStrandIndices.begin(), outPlan.selectedStrandIndices.end());
            outPlan.selectedStrandIndices.erase(std::unique(outPlan.selectedStrandIndices.begin(), outPlan.selectedStrandIndices.end()),
                outPlan.selectedStrandIndices.end());
            outPlan.summary.clusterCount = static_cast<uint32_t>(clusters.size());
            outPlan.summary.selectedClusterCount = static_cast<uint32_t>(clusters.size());
            outPlan.summary.selectedGuideCount = static_cast<uint32_t>(outPlan.selectedGuideStrandIndices.size());
            outPlan.summary.selectedStrandCount = static_cast<uint32_t>(outPlan.selectedStrandIndices.size());
            outPlan.summary.highestScore = 1.0f;
            outPlan.summary.lowestSelectedScore = 1.0f;
            outPlan.summary.averageSelectedScore = 1.0f;
            outPlan.summary.budgetScale = 1.0f;
            outPlan.summary.recommendedSolverIterations = std::max<uint32_t>(budget.maxSolverIterations, budget.minSolverIterations);
            outPlan.summary.fullTierCount = static_cast<uint32_t>(asset.strands.size());
        }

        void BuildActiveSetPredictorTarget(const std::vector<ActiveSetCluster>& clusters,
            const ActiveSetPlan& plan,
            const std::vector<float>& guideContactScores,
            const std::vector<float>& guidePenetrationScores,
            const std::vector<float>& guideModelErrorScores,
            std::vector<float>& outTargets)
        {
            outTargets.assign(clusters.size(), 0.0f);
            for (uint32_t clusterIndex = 0u; clusterIndex < clusters.size(); ++clusterIndex)
            {
                const ActiveSetCluster& cluster = clusters[clusterIndex];
                const float contact = AverageScalarAtIndices(guideContactScores, cluster.guideStrandIndices);
                const float penetration = AverageScalarAtIndices(guidePenetrationScores, cluster.guideStrandIndices);
                const float modelError = AverageScalarAtIndices(guideModelErrorScores, cluster.guideStrandIndices);
                const bool selected = std::find(plan.selectedClusterIndices.begin(), plan.selectedClusterIndices.end(), clusterIndex) != plan.selectedClusterIndices.end();
                const float score = std::max(selected ? 1.0f : 0.0f,
                    Clamp(contact * 0.55f + penetration * 0.60f + modelError * 0.45f,
                        0.0f,
                        1.0f));
                outTargets[clusterIndex] = score;
            }
        }

        void AppendPredictorTrainingRows(const std::vector<ActiveSetCluster>& clusters,
            const std::vector<Vec3>& restGuideRoots,
            const std::vector<Vec3>& currentGuideRoots,
            const std::vector<Vec3>& previousGuideRoots,
            const std::vector<float>& guideUncertainty,
            const std::vector<float>& guideContactScores,
            const std::vector<float>& guidePenetrationScores,
            const std::vector<float>& guideModelErrorScores,
            const ActiveSetScoringSettings& scoringSettings,
            const std::vector<float>& targets,
            std::vector<std::array<float, 9u>>& outFeatures,
            std::vector<float>& outTargets)
        {
            for (uint32_t clusterIndex = 0u; clusterIndex < clusters.size() && clusterIndex < targets.size(); ++clusterIndex)
            {
                std::array<float, 9u> features = {};
                ExtractActiveSetPredictorFeatures(clusters,
                    restGuideRoots,
                    currentGuideRoots,
                    previousGuideRoots,
                    guideUncertainty,
                    guideContactScores,
                    guidePenetrationScores,
                    guideModelErrorScores,
                    Vec3(0.0f, 1.55f, -1.85f),
                    Normalize(Vec3(0.0f, -0.05f, 1.0f)),
                    scoringSettings,
                    clusterIndex,
                    features);
                outFeatures.push_back(features);
                outTargets.push_back(targets[clusterIndex]);
            }
        }
    }
