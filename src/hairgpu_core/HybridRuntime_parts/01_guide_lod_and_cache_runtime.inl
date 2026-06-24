// HybridRuntime.cpp ordered part 01: guide_lod_and_cache_runtime
// This file is included by ../HybridRuntime.cpp; do not compile it directly.

namespace hairgpu
{
    namespace
    {
        std::vector<float> BuildGuideContactScores(const HairAsset& asset,
            const std::vector<Vec3>& points,
            const ColliderSet& colliders,
            float floorY,
            float shellDistance)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> scores(guideCount, 0.0f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                float bestScore = 0.0f;
                for (uint32_t localPoint = 0u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= points.size())
                    {
                        continue;
                    }
                    bestScore = std::max(bestScore,
                        ComputeColliderContactScore(points[pointIndex], colliders, floorY, shellDistance, true));
                }
                scores[guideIndex] = Clamp(bestScore, 0.0f, 1.0f);
            }
            return scores;
        }

        std::vector<float> BuildGuidePenetrationScores(const HairAsset& asset,
            const std::vector<Vec3>& points,
            const ColliderSet& colliders,
            float floorY,
            float shellDistance)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> scores(guideCount, 0.0f);
            const float normalization = std::max(shellDistance * 2.5f, 1.0e-4f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                float bestPenetration = 0.0f;
                for (uint32_t localPoint = 0u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= points.size())
                    {
                        continue;
                    }
                    const PenetrationResult result = ResolveAgainstColliderSet(points[pointIndex], colliders, shellDistance, true, floorY);
                    bestPenetration = std::max(bestPenetration, result.penetration);
                }
                scores[guideIndex] = Clamp(bestPenetration / normalization, 0.0f, 1.0f);
            }
            return scores;
        }

        std::vector<float> BuildGuideUncertainty(const HairAsset& asset,
            const std::vector<Vec3>& currentRoots,
            const std::vector<Vec3>& previousRoots,
            const std::vector<float>& previousUncertainty,
            const std::vector<float>& contactScores,
            const std::vector<float>& penetrationScores,
            const std::vector<float>& modelErrorScores,
            float fixedTimeStep,
            float temporalCoherenceGain,
            float smoothingAlpha)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> uncertainty(guideCount, 0.0f);
            const float safeDt = std::max(fixedTimeStep, 1.0e-4f);
            const float alpha = Clamp(smoothingAlpha, 0.0f, 1.0f);
            const float temporalGain = std::max(temporalCoherenceGain, 0.0f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                const Vec3 restRoot = (strand.pointCount > 0u && strand.pointOffset < asset.restPoints.size())
                    ? asset.restPoints[strand.pointOffset]
                    : Vec3(0.0f, 0.0f, 0.0f);
                const Vec3 current = (guideIndex < currentRoots.size()) ? currentRoots[guideIndex] : restRoot;
                const Vec3 previous = (guideIndex < previousRoots.size()) ? previousRoots[guideIndex] : current;
                const float motion = Clamp(Length(current - previous) / safeDt / 0.30f, 0.0f, 1.0f);
                const float displacement = Clamp(Length(current - restRoot) / 0.030f, 0.0f, 1.0f);
                const float contact = (guideIndex < contactScores.size()) ? contactScores[guideIndex] : 0.0f;
                const float penetration = (guideIndex < penetrationScores.size()) ? penetrationScores[guideIndex] : 0.0f;
                const float modelError = (guideIndex < modelErrorScores.size()) ? modelErrorScores[guideIndex] : 0.0f;
                const float previousValue = (guideIndex < previousUncertainty.size()) ? previousUncertainty[guideIndex] : 0.0f;
                const float temporalResidual = Clamp(std::fabs(previousValue - contact) * 0.5f + std::fabs(previousValue - penetration) * 0.5f,
                    0.0f,
                    1.0f) * temporalGain;
                const float instantaneous = Clamp(motion * 0.26f +
                    displacement * 0.18f +
                    contact * 0.18f +
                    penetration * 0.20f +
                    modelError * 0.18f +
                    temporalResidual * 0.10f,
                    0.0f,
                    1.0f);
                uncertainty[guideIndex] = Clamp(Lerp(previousValue, instantaneous, alpha), 0.0f, 1.0f);
            }
            return uncertainty;
        }

        void BuildGuideFramePoints(const HairAsset& asset,
            const std::vector<Vec3>& fullPoints,
            std::vector<Vec3>& outGuidePoints)
        {
            outGuidePoints.clear();
            outGuidePoints.reserve(static_cast<size_t>(asset.guideStrandCount) * asset.particlesPerStrand);
            for (uint32_t guideIndex = 0u; guideIndex < asset.guideStrandCount && guideIndex < asset.strands.size(); ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                for (uint32_t localPoint = 0u; localPoint < asset.particlesPerStrand; ++localPoint)
                {
                    const uint32_t clampedPoint = std::min<uint32_t>(localPoint, strand.pointCount > 0u ? (strand.pointCount - 1u) : 0u);
                    const uint32_t pointIndex = strand.pointOffset + clampedPoint;
                    outGuidePoints.push_back((pointIndex < fullPoints.size()) ? fullPoints[pointIndex] : Vec3(0.0f, 0.0f, 0.0f));
                }
            }
        }

        void AccumulateLengthError(const HairAsset& asset,
            const std::vector<Vec3>& points,
            const std::vector<uint8_t>* mask,
            float& outAverageError)
        {
            double total = 0.0;
            uint32_t count = 0u;
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                if (mask && strandIndex < mask->size() && (*mask)[strandIndex] == 0u)
                {
                    continue;
                }
                const StrandRange& strand = asset.strands[strandIndex];
                for (uint32_t localPoint = 1u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t prevIndex = strand.pointOffset + localPoint - 1u;
                    const uint32_t currIndex = strand.pointOffset + localPoint;
                    if (currIndex >= points.size() || prevIndex >= points.size())
                    {
                        continue;
                    }
                    const float restLength = StrandSegmentRestLength(asset, strandIndex, localPoint);
                    const float currentLength = Length(points[currIndex] - points[prevIndex]);
                    total += std::fabs(currentLength - restLength);
                    ++count;
                }
            }
            outAverageError = (count > 0u) ? static_cast<float>(total / static_cast<double>(count)) : 0.0f;
        }

        void AccumulateShapeError(const HairAsset& asset,
            const std::vector<Vec3>& points,
            const std::vector<uint8_t>* mask,
            float& outAverageError)
        {
            double total = 0.0;
            uint32_t count = 0u;
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                if (mask && strandIndex < mask->size() && (*mask)[strandIndex] == 0u)
                {
                    continue;
                }
                const StrandRange& strand = asset.strands[strandIndex];
                if (strand.pointCount == 0u)
                {
                    continue;
                }
                const uint32_t rootIndex = strand.pointOffset;
                if (rootIndex >= points.size() || rootIndex >= asset.restPoints.size())
                {
                    continue;
                }
                for (uint32_t localPoint = 1u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= points.size() || pointIndex >= asset.restPoints.size())
                    {
                        continue;
                    }
                    const Vec3 currentLocal = points[pointIndex] - points[rootIndex];
                    const Vec3 restLocal = asset.restPoints[pointIndex] - asset.restPoints[rootIndex];
                    total += static_cast<double>(Length(currentLocal - restLocal));
                    ++count;
                }
            }
            outAverageError = (count > 0u) ? static_cast<float>(total / static_cast<double>(count)) : 0.0f;
        }

        std::vector<float> BuildTeacherEditScores(const HairAsset& asset, float timeSeconds, float boundaryBias)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> scores(guideCount, 0.0f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                if (guideIndex >= asset.rootBindings.size())
                {
                    continue;
                }
                const Vec2 uv = asset.rootBindings[guideIndex].rootUv;
                const float frontBand = Clamp(1.0f - std::fabs(uv.y - 0.20f) / 0.22f, 0.0f, 1.0f);
                const float partingBand = Clamp(1.0f - std::fabs(uv.x - 0.52f) / 0.16f, 0.0f, 1.0f);
                const float oscillation = 0.5f + 0.5f * std::sin(timeSeconds * 1.9f + static_cast<float>(guideIndex) * 0.37f);
                scores[guideIndex] = Clamp((frontBand * 0.65f + partingBand * 0.35f) * (boundaryBias * 0.55f + oscillation * 0.45f), 0.0f, 1.0f);
            }
            return scores;
        }

        std::vector<float> BuildTeacherBoundaryScores(const HairAsset& asset, const std::vector<float>& editScores)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            std::vector<float> boundaryScores(guideCount, 0.0f);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                if (guideIndex >= asset.rootBindings.size())
                {
                    continue;
                }
                const Vec2 uv = asset.rootBindings[guideIndex].rootUv;
                float bestDelta = 0.0f;
                for (uint32_t other = 0u; other < guideCount; ++other)
                {
                    if (other == guideIndex || other >= asset.rootBindings.size())
                    {
                        continue;
                    }
                    if (asset.strands[other].groupId != asset.strands[guideIndex].groupId)
                    {
                        continue;
                    }
                    const Vec2 otherUv = asset.rootBindings[other].rootUv;
                    const float du = uv.x - otherUv.x;
                    const float dv = uv.y - otherUv.y;
                    const float dist = std::sqrt(du * du + dv * dv);
                    if (dist > 0.14f)
                    {
                        continue;
                    }
                    bestDelta = std::max(bestDelta, std::fabs(editScores[guideIndex] - editScores[other]));
                }
                boundaryScores[guideIndex] = Clamp(bestDelta, 0.0f, 1.0f);
            }
            return boundaryScores;
        }

        void ApplyTeacherEditCase(const HairAsset& asset,
            const std::vector<float>& guideEditScores,
            float lengthScaleAmplitude,
            float tipOffsetAmplitude,
            std::vector<Vec3>& inOutPoints)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                if (strand.pointCount < 2u)
                {
                    continue;
                }
                const float editWeight = (guideIndex < guideEditScores.size()) ? guideEditScores[guideIndex] : 0.0f;
                if (editWeight <= 1.0e-4f)
                {
                    continue;
                }
                const uint32_t rootIndex = strand.pointOffset;
                if (rootIndex >= inOutPoints.size())
                {
                    continue;
                }
                const Vec3 root = inOutPoints[rootIndex];
                const uint32_t tipIndex = strand.pointOffset + strand.pointCount - 1u;
                if (tipIndex >= inOutPoints.size())
                {
                    continue;
                }
                const Vec3 tipBase = inOutPoints[tipIndex];
                Vec3 direction = Normalize(tipBase - root);
                if (Length(direction) <= 1.0e-6f)
                {
                    direction = Vec3(0.0f, -1.0f, 0.0f);
                }
                Vec3 lateral = Normalize(Cross(Vec3(0.0f, 1.0f, 0.0f), direction));
                if (Length(lateral) <= 1.0e-6f)
                {
                    lateral = Vec3(1.0f, 0.0f, 0.0f);
                }
                const float targetScale = 1.0f + lengthScaleAmplitude * (editWeight - 0.5f);
                const Vec3 tipOffset = lateral * (tipOffsetAmplitude * (0.35f + 0.65f * editWeight));
                for (uint32_t localPoint = 1u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= inOutPoints.size())
                    {
                        continue;
                    }
                    const float tt = static_cast<float>(localPoint) / static_cast<float>(std::max<uint32_t>(strand.pointCount - 1u, 1u));
                    const Vec3 basePoint = inOutPoints[pointIndex];
                    const Vec3 along = root + (basePoint - root) * targetScale;
                    inOutPoints[pointIndex] = Lerp(basePoint, along + tipOffset * tt, editWeight * (0.35f + 0.65f * tt));
                }
            }
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
        outError.clear();
        if (inOutPoints.size() != asset.restPoints.size())
        {
            outError = "Sparse corrective input size does not match the asset point layout.";
            return false;
        }

        SparseCorrectiveReport report = {};
        report.preservedRootPointCount = settings.preserveRootPointCount;
        AccumulateLengthError(asset, inOutPoints, &activeSetPlan.strandCorrectiveMask, report.averageLengthErrorBefore);
        AccumulateShapeError(asset, inOutPoints, &activeSetPlan.strandCorrectiveMask, report.averageShapeErrorBefore);

        double penetrationSum = 0.0;
        uint32_t penetrationSamples = 0u;
        const uint32_t iterations = std::max<uint32_t>(1u, settings.iterations);
        for (uint32_t iteration = 0u; iteration < iterations; ++iteration)
        {
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                if (strandIndex >= activeSetPlan.strandCorrectiveMask.size() || activeSetPlan.strandCorrectiveMask[strandIndex] == 0u)
                {
                    continue;
                }
                const StrandRange& strand = asset.strands[strandIndex];
                bool strandCorrected = false;
                for (uint32_t localPoint = 0u; localPoint < strand.pointCount; ++localPoint)
                {
                    if (localPoint < settings.preserveRootPointCount)
                    {
                        continue;
                    }
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    if (pointIndex >= inOutPoints.size())
                    {
                        continue;
                    }
                    const Vec3 point = inOutPoints[pointIndex];
                    const PenetrationResult best = ResolveAgainstColliderSet(point,
                        settings.affectExternalColliders ? colliders : ColliderSet(),
                        settings.shellDistance,
                        settings.affectFloor,
                        -1.05f);
                    if (best.corrected)
                    {
                        inOutPoints[pointIndex] = Lerp(point, best.correctedPoint, Clamp(settings.contactBlend, 0.0f, 1.0f));
                        strandCorrected = true;
                        ++report.correctedPointCount;
                        report.maxPenetration = std::max(report.maxPenetration, best.penetration);
                        penetrationSum += best.penetration;
                        ++penetrationSamples;
                        ++report.penetrationEventCount;
                    }

                    if (localPoint > settings.preserveRootPointCount)
                    {
                        const uint32_t prevIndex = strand.pointOffset + localPoint - 1u;
                        if (prevIndex < inOutPoints.size())
                        {
                            const float restLength = StrandSegmentRestLength(asset, strandIndex, localPoint);
                            Vec3 delta = inOutPoints[pointIndex] - inOutPoints[prevIndex];
                            const float currentLength = Length(delta);
                            if (restLength > 1.0e-6f && currentLength > 1.0e-6f)
                            {
                                const Vec3 dir = delta / currentLength;
                                const float correction = (restLength - currentLength) * Clamp(settings.stretchBlend, 0.0f, 1.0f) / static_cast<float>(iterations);
                                inOutPoints[pointIndex] += dir * correction;
                            }
                        }
                    }
                }
                if (strandCorrected)
                {
                    ++report.correctedStrandCount;
                }
            }
        }

        report.averagePenetration = (penetrationSamples > 0u)
            ? static_cast<float>(penetrationSum / static_cast<double>(penetrationSamples))
            : 0.0f;
        AccumulateLengthError(asset, inOutPoints, &activeSetPlan.strandCorrectiveMask, report.averageLengthErrorAfter);
        AccumulateShapeError(asset, inOutPoints, &activeSetPlan.strandCorrectiveMask, report.averageShapeErrorAfter);
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
        outError.clear();
        outReport = {};
        if (asset.restPoints.empty() || asset.strands.empty() || asset.guideStrandCount == 0u)
        {
            outError = "Teacher dataset generation requires a canonicalized asset with guide strands.";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create teacher dataset output directory.";
            return false;
        }

        const ColliderSet runtimeColliders = !HasAnyCollider(colliders)
            ? BuildFallbackTeacherColliders(asset)
            : colliders;

        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(asset, settings.clusterSettings);
        if (clusters.empty())
        {
            outError = "Failed to build active-set clusters for the teacher dataset.";
            return false;
        }

        HairCache hairCache;
        GuideCache guideCache;
        if (!InitializeHairCacheForAsset(asset, settings.frameRate, hairCache, outError) ||
            !InitializeGuideCacheForAsset(asset, settings.frameRate, guideCache, outError))
        {
            return false;
        }

        std::ofstream frameCsv(outputDirectory / "hybrid_teacher_frames.csv");
        if (!frameCsv)
        {
            outError = "Failed to open hybrid_teacher_frames.csv for writing.";
            return false;
        }
        frameCsv << "frameIndex,timeSeconds,selectedClusters,selectedGuides,selectedStrands,selectedNaturalnessGain,averageEfficiencyScore,editCoverageRatio,budgetScale,recommendedSolverIterations,fullTierCount,reducedTierCount,frozenTierCount,cachedTierCount,correctedStrands,correctedPoints,avgPenetration,maxPenetration,avgGuideContact,avgGuidePenetration,avgGuideUncertainty,maxGuideUncertainty,avgGuideEdit,avgGuideBoundary,avgLengthErrorBefore,avgLengthErrorAfter,estimatedSimulationMilliseconds,estimatedPlanMemoryMegabytes\n";

        std::vector<Vec3> previousGuideRoots;
        BuildGuideRootsFromPoints(asset, asset.restPoints, previousGuideRoots);
        std::vector<float> previousGuideUncertainty(previousGuideRoots.size(), 0.0f);

        double selectedClusterSum = 0.0;
        double selectedGuideSum = 0.0;
        double selectedStrandSum = 0.0;
        double penetrationSum = 0.0;
        double correctedPointSum = 0.0;
        float maxPenetration = 0.0f;
        float estimatedSimulationMilliseconds = 0.0f;

        for (uint32_t frameIndex = 0u; frameIndex < std::max<uint32_t>(1u, settings.frameCount); ++frameIndex)
        {
            const float timeSeconds = static_cast<float>(frameIndex) / std::max(settings.frameRate, 1.0f);

            std::vector<Vec3> framePoints;
            BuildProxyFramePoints(asset,
                timeSeconds,
                settings.swayAmplitude,
                settings.tipNoiseAmplitude,
                settings.windCyclesPerSecond,
                framePoints);
            const std::vector<float> guideEditScores = BuildTeacherEditScores(asset, timeSeconds, settings.editBoundaryBias);
            const std::vector<float> guideBoundaryScores = BuildTeacherBoundaryScores(asset, guideEditScores);
            ApplyTeacherEditCase(asset, guideEditScores, settings.editLengthScaleAmplitude, settings.editTipOffsetAmplitude, framePoints);

            std::vector<Vec3> currentGuideRoots;
            BuildGuideRootsFromPoints(asset, framePoints, currentGuideRoots);
            const std::vector<float> guideContactScores = BuildGuideContactScores(asset,
                framePoints,
                runtimeColliders,
                -1.05f,
                settings.correctiveSettings.shellDistance);
            const std::vector<float> guidePenetrationScores = BuildGuidePenetrationScores(asset,
                framePoints,
                runtimeColliders,
                -1.05f,
                settings.correctiveSettings.shellDistance);
            const std::vector<float> modelErrorScores(currentGuideRoots.size(), 0.0f);
            const std::vector<float> guideUncertainty = BuildGuideUncertainty(asset,
                currentGuideRoots,
                previousGuideRoots,
                previousGuideUncertainty,
                guideContactScores,
                guidePenetrationScores,
                modelErrorScores,
                1.0f / std::max(settings.frameRate, 1.0f),
                1.0f,
                0.45f);

            ActiveSetRuntimeInput runtimeInput = {};
            runtimeInput.currentGuideRoots = currentGuideRoots;
            runtimeInput.previousGuideRoots = previousGuideRoots;
            runtimeInput.guideContactScores = guideContactScores;
            runtimeInput.guidePenetrationScores = guidePenetrationScores;
            runtimeInput.guideUncertainty = guideUncertainty;
            runtimeInput.guideEditScores = guideEditScores;
            runtimeInput.guideBoundaryScores = guideBoundaryScores;
            runtimeInput.guideTemporalScores = guideUncertainty;
            runtimeInput.guideModelErrorScores = modelErrorScores;
            runtimeInput.cameraPosition = Vec3(0.0f, 1.55f, -1.85f);
            runtimeInput.cameraForward = Normalize(Vec3(0.0f, -0.05f, 1.0f));
            runtimeInput.fixedTimeStep = 1.0f / std::max(settings.frameRate, 1.0f);

            ActiveSetBudget budget = settings.budget;
            budget.previousFrameMilliseconds = estimatedSimulationMilliseconds;

            ActiveSetPlan plan;
            if (!EvaluateActiveSetPlan(asset, clusters, runtimeInput, settings.scoringSettings, budget, plan, outError))
            {
                return false;
            }

            std::vector<Vec3> correctedPoints = framePoints;
            SparseCorrectiveReport correctiveReport = {};
            if (!ApplySparseCorrectivePhysics(asset,
                plan,
                runtimeColliders,
                correctedPoints,
                settings.correctiveSettings,
                &correctiveReport,
                outError))
            {
                return false;
            }

            if (!AppendHairCacheFrame(hairCache, asset, correctedPoints, outError))
            {
                return false;
            }
            std::vector<Vec3> guideFramePoints;
            BuildGuideFramePoints(asset, correctedPoints, guideFramePoints);
            if (!AppendGuideCacheGuidePoints(guideCache, guideFramePoints, outError))
            {
                return false;
            }

            estimatedSimulationMilliseconds = 1.20f +
                static_cast<float>(plan.summary.selectedClusterCount) * 0.34f +
                static_cast<float>(plan.summary.selectedGuideCount) * 0.010f +
                static_cast<float>(correctiveReport.correctedPointCount) * 0.0018f +
                static_cast<float>(plan.summary.recommendedSolverIterations) * 0.20f +
                static_cast<float>(plan.summary.fullTierCount) * 0.0015f;
            outReport.peakEstimatedSimulationMilliseconds = std::max(outReport.peakEstimatedSimulationMilliseconds,
                estimatedSimulationMilliseconds);
