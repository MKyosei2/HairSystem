// HybridRuntime.cpp ordered part 06: part_06
// This file is included by ../HybridRuntime.cpp; do not compile it directly.

frameCsv << frameIndex << ","
<< (static_cast<float>(frameIndex) / std::max(runtimeFrameRate, 1.0f)) << ","
<< priorRms << ","
<< hybridBeforeRms << ","
<< hybridAfterRms << ","
<< finalOutputRms << ","
<< estimatedFrameMilliseconds << ","
<< plan.summary.estimatedMemoryMegabytes << ","
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
<< ComputeAverageScalar(guideModelErrorScores) << ","
<< ComputeAverageScalar(guidePredictorRisk) << ","
<< ComputeMaxScalar(guidePredictorRisk) << "\n";

if (settings.exportPerFrameHairBins)
{
    HairAsset frameAsset = teacherSequence.referenceAsset;
    frameAsset.restPoints = finalOutputPoints;
    std::ostringstream name;
    name << "full_hybrid_frame_" << std::setw(4) << std::setfill('0') << frameIndex << ".hairbin";
    if (!SaveHairAssetToHairBinary(outputDirectory / name.str(), frameAsset, outError))
    {
        return false;
    }

    ActiveSetDispatchPlan dispatchPlan = {};
    if (!BuildActiveSetDispatchPlan(teacherSequence.referenceAsset, plan, dispatchPlan, outError))
    {
        return false;
    }

    ActiveSetDispatchPack dispatchPack = {};
    if (!BuildActiveSetDispatchPack(teacherSequence.referenceAsset, plan, dispatchPlan, dispatchPack, outError))
    {
        return false;
    }

    std::ostringstream dispatchName;
    dispatchName << "full_hybrid_dispatch_" << std::setw(4) << std::setfill('0') << frameIndex << ".json";
    if (!WriteActiveSetDispatchPlanJson(outputDirectory / dispatchName.str(),
        teacherSequence.referenceAsset,
        plan,
        dispatchPlan,
        outError))
    {
        return false;
    }

    std::ostringstream dispatchPackJsonName;
    dispatchPackJsonName << "full_hybrid_dispatch_pack_" << std::setw(4) << std::setfill('0') << frameIndex << ".json";
    if (!WriteActiveSetDispatchPackJson(outputDirectory / dispatchPackJsonName.str(),
        teacherSequence.referenceAsset,
        plan,
        dispatchPack,
        outError))
    {
        return false;
    }

    std::ostringstream dispatchPackBinName;
    dispatchPackBinName << "full_hybrid_dispatch_pack_" << std::setw(4) << std::setfill('0') << frameIndex << ".dispatchbin";
    if (!SaveActiveSetDispatchPackToFile(outputDirectory / dispatchPackBinName.str(),
        dispatchPack,
        outError))
    {
        return false;
    }
}

previousFrameMilliseconds = estimatedFrameMilliseconds;
previousPreviousSmoothedPriorGuidePoints = previousSmoothedPriorGuidePoints;
previousSmoothedPriorGuidePoints = smoothedPriorGuidePoints;
previousPreviousRawPriorGuidePoints = previousRawPriorGuidePoints;
previousRawPriorGuidePoints = currentPriorGuidePoints;
previousPreviousResidualGuidePoints = previousResidualGuidePoints;
previousResidualGuidePoints = currentResidualGuidePoints;
previousPreviousBaseResidualGuideInput = previousBaseResidualGuideInput;
previousBaseResidualGuideInput = baseResidualGuideInput;
previousHybridGuideRoots = finalGuideRoots;
previousGuideUncertainty = guideUncertainty;
previousOutputPoints = finalOutputPoints;
estimatedFrameMillisecondsHistory.push_back(previousFrameMilliseconds);

priorRmsSum += static_cast<double>(priorRms);
hybridBeforeRmsSum += static_cast<double>(hybridBeforeRms);
hybridAfterRmsSum += static_cast<double>(finalOutputRms);
selectedClusterSum += static_cast<double>(plan.summary.selectedClusterCount);
selectedGuideSum += static_cast<double>(plan.summary.selectedGuideCount);
correctedPointSum += static_cast<double>(correctiveReport.correctedPointCount);
penetrationSum += static_cast<double>(correctiveReport.averagePenetration);
predictorRiskSum += static_cast<double>(ComputeAverageScalar(guidePredictorRisk));
silhouetteErrorSum += static_cast<double>(shapeReport.silhouetteError);
clumpPreservationSum += static_cast<double>(shapeReport.clumpPreservationScore);
rootLocalitySum += static_cast<double>(shapeReport.rootLocalityScore);
if (correctiveReport.maxPenetration > 1.0e-5f)
{
    ++penetrationEventCount;
}
peakEstimatedRamMegabytes = std::max(peakEstimatedRamMegabytes,
    EstimateRuntimeRamMegabytes(teacherSequence.referenceAsset,
        bundle,
        static_cast<uint32_t>(clusters.size()),
        static_cast<uint32_t>(cachedTierSnapshotPoints.size())));
maxHybridAfterRms = std::max(maxHybridAfterRms, finalOutputRms);
maxPenetration = std::max(maxPenetration, correctiveReport.maxPenetration);
maxGuidePredictorRisk = std::max(maxGuidePredictorRisk, ComputeMaxScalar(guidePredictorRisk));
        }

        if (!SaveHairCacheToFile(outputDirectory / "full_hybrid_runtime.haircache", hairCache, outError) ||
            !SaveGuideCacheToFile(outputDirectory / "full_hybrid_runtime_guides.guidecache", guideCache, outError) ||
            !SaveHairAssetToHairBinary(outputDirectory / "full_hybrid_reference.hairbin", teacherSequence.referenceAsset, outError))
        {
            return false;
        }

        const float frameCountF = static_cast<float>(std::max<uint32_t>(1u, teacherSequence.FrameCount()));
        outReport.frameCount = teacherSequence.FrameCount();
        outReport.averagePriorRmsError = static_cast<float>(priorRmsSum / frameCountF);
        outReport.averageHybridRmsErrorBeforeCorrection = static_cast<float>(hybridBeforeRmsSum / frameCountF);
        outReport.averageHybridRmsErrorAfterCorrection = static_cast<float>(hybridAfterRmsSum / frameCountF);
        outReport.maxHybridRmsErrorAfterCorrection = maxHybridAfterRms;
        outReport.averageSelectedClusterCount = static_cast<float>(selectedClusterSum / frameCountF);
        outReport.averageSelectedGuideCount = static_cast<float>(selectedGuideSum / frameCountF);
        outReport.averageCorrectedPointCount = static_cast<float>(correctedPointSum / frameCountF);
        outReport.averagePenetration = static_cast<float>(penetrationSum / frameCountF);
        outReport.maxPenetration = maxPenetration;
        outReport.averageGuidePredictorRisk = static_cast<float>(predictorRiskSum / frameCountF);
        outReport.maxGuidePredictorRisk = maxGuidePredictorRisk;
        outReport.averageSilhouetteError = static_cast<float>(silhouetteErrorSum / frameCountF);
        outReport.averageClumpPreservationScore = static_cast<float>(clumpPreservationSum / frameCountF);
        outReport.averageRootLocalityScore = static_cast<float>(rootLocalitySum / frameCountF);
        outReport.peakEstimatedRamMegabytes = peakEstimatedRamMegabytes;
        const float durationSeconds = std::max(frameCountF / std::max(teacherSequence.frameRate, 1.0e-4f), 1.0e-4f);
        outReport.penetrationEventsPerSecond = static_cast<float>(static_cast<double>(penetrationEventCount) / static_cast<double>(durationSeconds));
        double estimatedFrameMillisecondsSum = 0.0;
        for (const float value : estimatedFrameMillisecondsHistory)
        {
            estimatedFrameMillisecondsSum += static_cast<double>(value);
        }
        outReport.averageEstimatedFrameMilliseconds = estimatedFrameMillisecondsHistory.empty()
            ? 0.0f
            : static_cast<float>(estimatedFrameMillisecondsSum / static_cast<double>(estimatedFrameMillisecondsHistory.size()));
        outReport.p95EstimatedFrameMilliseconds = ComputeQuantile95(estimatedFrameMillisecondsHistory);
        outReport.maxEstimatedFrameMilliseconds = estimatedFrameMillisecondsHistory.empty()
            ? 0.0f
            : *std::max_element(estimatedFrameMillisecondsHistory.begin(), estimatedFrameMillisecondsHistory.end());
        outReport.hitRate60Fps = estimatedFrameMillisecondsHistory.empty()
            ? 0.0f
            : static_cast<float>(std::count_if(estimatedFrameMillisecondsHistory.begin(), estimatedFrameMillisecondsHistory.end(),
                [](const float value)
                {
                    return value <= 16.67f;
                }) / static_cast<double>(estimatedFrameMillisecondsHistory.size()));

        if (!WriteHybridSummaryJson(outputDirectory / "full_hybrid_summary.json", bundle, nullptr, &outReport, outError))
        {
            return false;
        }
        return true;
    }
}
