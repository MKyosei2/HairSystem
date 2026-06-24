// HybridRuntime.cpp ordered part 01: guide_lod_and_cache_runtime
// This file is included by ../HybridRuntime.cpp; do not compile it directly.

namespace hairgpu
{
    namespace
    {

        std::vector<float> BuildGuideContactScores(const HairAsset& asset, const ColliderSet& colliders,
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
                const Vec3 root = inOutPoints[rootIndex];
                const Vec3 tipBase = inOutPoints[strand.pointOffset + strand.pointCount - 1u];
                Vec3 direction = Normalize(tipBase - root);
                if (Length(direction) <= 1.0e-6f)
                {
                    direction = Vec3(0.0f, -1.0f, 0.0f);
                }
                const Vec3 lateral = Normalize(Cross(Vec3(0.0f, 1.0f, 0.0f), direction));
                const float targetScale = 1.0f + lengthScaleAmplitude * (editWeight - 0.5f);
                const Vec3 tipOffset = lateral * (tipOffsetAmplitude * (0.35f + 0.65f * editWeight));
                for (uint32_t localPoint = 1u; localPoint < strand.pointCount; ++localPoint)
                {
                    const uint32_t pointIndex = strand.pointOffset + localPoint;
                    const float tt = static_cast<float>(localPoint) / static_cast<float>(strand.pointCount - 1u);
                    const Vec3 basePoint = inOutPoints[pointIndex];
                    const Vec3 along = root + (basePoint - root) * targetScale;
                    inOutPoints[pointIndex] = Lerp(basePoint, along + tipOffset * tt, editWeight * (0.35f + 0.65f * tt));
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
                        const uint32_t pointIndex = strand.pointOffset + localPoint;
                        if (pointIndex >= inOutPoints.size())
                        {
                            continue;
                        }
                        if (localPoint < settings.preserveRootPointCount)
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
                    }


                    const uint32_t rootIndex = strand.pointOffset;
                    const bool isFollower = strandIndex >= asset.guideStrandCount && strandIndex < asset.followData.size();
                    const FollowData* followData = isFollower ? &asset.followData[strandIndex] : nullptr;
                    const StrandRange* parentGuide = nullptr;
                    if (followData && followData->parentGuide >= 0 && static_cast<uint32_t>(followData->parentGuide) < asset.guideStrandCount)
                    {
                        parentGuide = &asset.strands[static_cast<uint32_t>(followData->parentGuide)];
                    }
                    for (uint32_t localPoint = std::max<uint32_t>(1u, settings.preserveRootPointCount); localPoint < strand.pointCount; ++localPoint)
                    {
                        const uint32_t prevIndex = strand.pointOffset + localPoint - 1u;
                        const uint32_t currIndex = strand.pointOffset + localPoint;
                        if (currIndex >= inOutPoints.size() || prevIndex >= inOutPoints.size() || rootIndex >= inOutPoints.size())
                        {
                            continue;
                        }
                        const float restLength = StrandSegmentRestLength(asset, strandIndex, localPoint);
                        if (restLength <= 1.0e-6f)
                        {
                            continue;
                        }

                        const float stretchBlend = Clamp(settings.stretchBlend, 0.0f, 1.0f);
                        const float bendBlend = Clamp(settings.bendBlend, 0.0f, 1.0f);
                        const float shapeBlend = Clamp(settings.shapePreservationBlend, 0.0f, 1.0f);
                        const float clumpBlend = Clamp(settings.clumpPreservationBlend, 0.0f, 1.0f);
                        const float curlBlend = Clamp(settings.curlPreservationBlend, 0.0f, 1.0f);
                        const float twistBlend = Clamp(settings.twistBlend, 0.0f, 1.0f);
                        const float complianceScale = 1.0f / static_cast<float>(std::max<uint32_t>(iterations, 1u));
                        const float strandT = static_cast<float>(localPoint) / static_cast<float>(std::max<uint32_t>(strand.pointCount - 1u, 1u));

                        if (localPoint == settings.preserveRootPointCount && settings.preserveRootPointCount > 0u)
                        {
                            const Vec3 restAnchor = asset.restPoints[currIndex];
                            inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], restAnchor, 0.15f * complianceScale);
                        }

                        Vec3 currentDelta = inOutPoints[currIndex] - inOutPoints[prevIndex];
                        float currentLength = Length(currentDelta);
                        if (currentLength > 1.0e-6f)
                        {
                            const Vec3 dir = currentDelta / currentLength;
                            const float correction = (restLength - currentLength) * stretchBlend * complianceScale;
                            inOutPoints[currIndex] += dir * correction;
                        }

                        if (localPoint >= 2u)
                        {
                            const uint32_t prevPrevIndex = strand.pointOffset + localPoint - 2u;
                            const Vec3 restPrev = asset.restPoints[prevIndex] - asset.restPoints[prevPrevIndex];
                            const Vec3 restCurr = asset.restPoints[currIndex] - asset.restPoints[prevIndex];
                            const Vec3 desiredDir = Normalize(Lerp(Normalize(restCurr), Normalize(restPrev), 0.25f));
                            if (Length(desiredDir) > 1.0e-6f)
                            {
                                const Vec3 bendTarget = inOutPoints[prevIndex] + desiredDir * restLength;
                                inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], bendTarget, bendBlend * complianceScale);
                            }

                            const Vec3 currentPrev = Normalize(inOutPoints[prevIndex] - inOutPoints[prevPrevIndex]);
                            const Vec3 currentCurr = Normalize(inOutPoints[currIndex] - inOutPoints[prevIndex]);
                            const Vec3 restBinormal = Normalize(Cross(Normalize(restPrev), Normalize(restCurr)));
                            const Vec3 currentBinormal = Normalize(Cross(currentPrev, currentCurr));
                            if (Length(restBinormal) > 1.0e-6f && Length(currentBinormal) > 1.0e-6f)
                            {
                                const Vec3 twistAssist = Normalize(Cross(currentBinormal, restBinormal));
                                if (Length(twistAssist) > 1.0e-6f)
                                {
                                    inOutPoints[currIndex] += twistAssist * (restLength * twistBlend * complianceScale * 0.20f * strandT);
                                }
                            }
                        }

                        const Vec3 rootAlignedRestTarget = inOutPoints[rootIndex] + (asset.restPoints[currIndex] - asset.restPoints[rootIndex]);
                        inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], rootAlignedRestTarget, shapeBlend * complianceScale * (0.25f + 0.75f * strandT));

                        if (localPoint >= 2u)
                        {
                            const uint32_t prevPrevIndex = strand.pointOffset + localPoint - 2u;
                            const Vec3 restPrev = asset.restPoints[prevIndex] - asset.restPoints[prevPrevIndex];
                            const Vec3 restCurr = asset.restPoints[currIndex] - asset.restPoints[prevIndex];
                            const Vec3 restCurveDelta = Normalize(restCurr) - Normalize(restPrev);
                            const float restCurveMag = Length(restCurveDelta);
                            if (restCurveMag > 1.0e-6f)
                            {
                                const Vec3 curlTarget = inOutPoints[prevIndex] + Normalize(restCurr) * restLength;
                                inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], curlTarget, curlBlend * complianceScale * Clamp(restCurveMag * 1.75f, 0.0f, 1.0f));
                            }
                        }

                        if (parentGuide && followData)
                        {
                            const uint32_t guideLocalPoint = std::min<uint32_t>(localPoint, parentGuide->pointCount - 1u);
                            const uint32_t guidePointIndex = parentGuide->pointOffset + guideLocalPoint;
                            if (guidePointIndex < inOutPoints.size() && guidePointIndex < asset.restPoints.size())
                            {
                                const Vec3 restFamilyOffset = asset.restPoints[currIndex] - asset.restPoints[guidePointIndex];
                                const float familyWeight = Clamp(followData->clumpWeight, 0.0f, 1.0f) * std::clamp(followData->followWeight, 0.0f, 1.0f);
                                const Vec3 familyTarget = inOutPoints[guidePointIndex] + restFamilyOffset;
                                inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], familyTarget, clumpBlend * complianceScale * familyWeight * (0.30f + 0.70f * strandT));
                            }
                        }

                        const PenetrationResult postConstraint = ResolveAgainstColliderSet(inOutPoints[currIndex],
                            settings.affectExternalColliders ? colliders : ColliderSet(),
                            settings.shellDistance,
                            settings.affectFloor,
                            -1.05f);
                        if (postConstraint.corrected)
                        {
                            inOutPoints[currIndex] = Lerp(inOutPoints[currIndex], postConstraint.correctedPoint, Clamp(settings.contactBlend, 0.0f, 1.0f));
                            strandCorrected = true;
                            report.maxPenetration = std::max(report.maxPenetration, postConstraint.penetration);
                            penetrationSum += postConstraint.penetration;
                            ++penetrationSamples;
                            ++report.penetrationEventCount;
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
            }
