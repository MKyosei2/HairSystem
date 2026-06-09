// main.cpp ordered part 04: part_04
// This file is included by ../main.cpp; do not compile it directly.

                << report.averagePenetration << "," << report.maxPenetration << ","
                << report.averageGuidePredictorRisk << "," << report.p95EstimatedFrameMilliseconds << ","
                << report.maxEstimatedFrameMilliseconds << "," << report.hitRate60Fps << "\n";
            if (!first)
            {
                json << ",\n";
            }
            first = false;
            json << "    {\n"
                 << "      \"methodCode\": \"" << variant.code << "\",\n"
                 << "      \"methodLabel\": \"" << variant.label << "\",\n"
                 << "      \"frameCount\": " << report.frameCount << ",\n"
                 << "      \"avgPriorRms\": " << report.averagePriorRmsError << ",\n"
                 << "      \"avgHybridRmsBefore\": " << report.averageHybridRmsErrorBeforeCorrection << ",\n"
                 << "      \"avgHybridRmsAfter\": " << report.averageHybridRmsErrorAfterCorrection << ",\n"
                 << "      \"avgSelectedClusters\": " << report.averageSelectedClusterCount << ",\n"
                 << "      \"avgSelectedGuides\": " << report.averageSelectedGuideCount << ",\n"
                 << "      \"avgCorrectedPoints\": " << report.averageCorrectedPointCount << ",\n"
                 << "      \"avgPenetration\": " << report.averagePenetration << ",\n"
                 << "      \"maxPenetration\": " << report.maxPenetration << ",\n"
                 << "      \"avgPredictorRisk\": " << report.averageGuidePredictorRisk << ",\n"
                 << "      \"p95EstimatedFrameMs\": " << report.p95EstimatedFrameMilliseconds << ",\n"
                 << "      \"maxEstimatedFrameMs\": " << report.maxEstimatedFrameMilliseconds << ",\n"
                 << "      \"hitRate60Fps\": " << report.hitRate60Fps << "\n"
                 << "    }";
        }
        json << "\n  ]\n}\n";
        std::cout << "comparisonWritten=" << (std::filesystem::path(argv[4]) / "hybrid_compare_summary.csv").string() << "\n";
        return 0;
    }
    if (command == "guidecache-info")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        GuideCache cache;
        std::string error;
        if (!LoadGuideCacheFromFile(argv[2], cache, error))
        {
            return PrintLoadFailure("Guide cache load failed", error, 10);
        }
        std::cout << BuildGuideCacheSummary(cache) << "\n";
        return 0;
    }
    if (command == "dispatchpack-info")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        ActiveSetDispatchPack pack;
        std::string error;
        if (!LoadActiveSetDispatchPackFromFile(argv[2], pack, error))
        {
            return PrintLoadFailure("Dispatch pack load failed", error, 11);
        }
        std::cout << BuildActiveSetDispatchPackSummary(pack) << "\n";
        return 0;
    }
    if (command == "dispatchpack-gpubuffers")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ActiveSetDispatchPack pack = {};
        std::string error;
        if (!LoadActiveSetDispatchPackFromFile(argv[2], pack, error))
        {
            return PrintLoadFailure("Dispatch pack load failed", error, 179);
        }
        if (!SaveActiveSetDispatchPackGpuBuffers(argv[3], pack, error))
        {
            return PrintLoadFailure("Dispatch GPU buffer export failed", error, 180);
        }
        std::cout << BuildActiveSetDispatchPackSummary(pack) << " outputDir=" << argv[3] << "\n";
        return 0;
    }
    if (command == "asset-export-abc-compat")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairAsset asset = {};
        std::string error;
        if (!LoadHairAssetAny(argv[2], asset, error))
        {
            return PrintLoadFailure("Hair asset load failed", error, 181);
        }
        if (!SaveHairAssetToAlembic(argv[3], asset, error))
        {
            return PrintLoadFailure("ABC compatibility export failed", error, 182);
        }
        std::cout << "abcCompat=" << argv[3] << "\n";
        return 0;
    }
    if (command == "alembic-roundtrip-selftest")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairAsset asset = {};
        std::string error;
        if (!LoadHairAssetAny(argv[2], asset, error))
        {
            return PrintLoadFailure("Hair asset load failed", error, 183);
        }
        HairAlembicSettings settings = {};
        AlembicRoundTripReport report = {};
        if (!RoundTripHairAssetThroughAlembic(asset, argv[3], settings, report, error))
        {
            return PrintLoadFailure("Alembic round-trip self-test failed", error, 184);
        }
        std::ostringstream summary;
        summary.setf(std::ios::fixed);
        summary.precision(6);
        summary << "usedAlembicSdk=" << (report.usedAlembicSdk ? 1 : 0) << "\n"
                << "sourceStrands=" << report.sourceStrandCount << "\n"
                << "roundTripStrands=" << report.roundTripStrandCount << "\n"
                << "sourcePoints=" << report.sourcePointCount << "\n"
                << "roundTripPoints=" << report.roundTripPointCount << "\n"
                << "rmsRestPointError=" << report.rmsRestPointError << "\n"
                << "maxRestPointError=" << report.maxRestPointError << "\n"
                << "rmsWidthError=" << report.rmsWidthError << "\n"
                << "maxWidthError=" << report.maxWidthError << "\n"
                << "scalpRoundTripOk=" << (report.scalpRoundTripOk ? 1 : 0) << "\n";
        std::cout << summary.str();
        if (argc >= 5)
        {
            std::ofstream out(argv[4]);
            if (!out)
            {
                std::cerr << "Failed to write round-trip report.\n";
                return 185;
            }
            out << summary.str();
        }
        return 0;
    }
    if (command == "reduced-info")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        ReducedModel model;
        std::string error;
        if (!LoadReducedModelFromFile(argv[2], model, error))
        {
            return PrintLoadFailure("Reduced model load failed", error, 40);
        }
        std::cout << BuildReducedModelSummary(model) << "\n";
        return 0;
    }
    if (command == "reduced-export-guidecache")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        ReducedModel model;
        ReducedModelTrainingSequence sequence;
        std::string error;
        if (!LoadReducedModelFromFile(argv[2], model, error))
        {
            return PrintLoadFailure("Reduced model load failed", error, 41);
        }
        if (!LoadReducedTrainingSequenceFromDirectory(argv[3], sequence, error))
        {
            return PrintLoadFailure("Training sequence load failed", error, 42);
        }
        GuideCache cache;
        if (!ExportReducedModelGuideCache(model, sequence, cache, error) || !SaveGuideCacheToFile(argv[4], cache, error))
        {
            return PrintLoadFailure("Guide cache export failed", error, 43);
        }
        return 0;
    }
    if (command == "reduced-reconstruct-frame")
    {
        if (argc < 6)
        {
            PrintUsage();
            return 1;
        }
        ReducedModel model;
        ReducedModelTrainingSequence sequence;
        std::string error;
        if (!LoadReducedModelFromFile(argv[2], model, error))
        {
            return PrintLoadFailure("Reduced model load failed", error, 44);
        }
        if (!LoadReducedTrainingSequenceFromDirectory(argv[3], sequence, error))
        {
            return PrintLoadFailure("Training sequence load failed", error, 45);
        }
        HairAsset reconstructed;
        const uint32_t frameIndex = ArgToUInt(argc, argv, 4, 0u);
        if (!ReconstructReducedModelFrameFromSequence(model, sequence, frameIndex, reconstructed, error) ||
            !SaveHairAssetToHairBinary(argv[5], reconstructed, error))
        {
            return PrintLoadFailure("Reduced frame reconstruction failed", error, 46);
        }
        return 0;
    }
    if (command == "reduced-reconstruct-sequence")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        ReducedModel model;
        ReducedModelTrainingSequence sequence;
        std::string error;
        if (!LoadReducedModelFromFile(argv[2], model, error))
        {
            return PrintLoadFailure("Reduced model load failed", error, 60);
        }
        if (!LoadReducedTrainingSequenceFromDirectory(argv[3], sequence, error))
        {
            return PrintLoadFailure("Training sequence load failed", error, 61);
        }
        if (!ReconstructReducedModelSequence(model, sequence, argv[4], error))
        {
            return PrintLoadFailure("Reduced sequence reconstruction failed", error, 62);
        }
        return 0;
    }
    if (command == "reduced-eval-json")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        ReducedModel model;
        ReducedModelTrainingSequence sequence;
        std::string error;
        if (!LoadReducedModelFromFile(argv[2], model, error))
        {
            return PrintLoadFailure("Reduced model load failed", error, 63);
        }
        if (!LoadReducedTrainingSequenceFromDirectory(argv[3], sequence, error))
        {
            return PrintLoadFailure("Training sequence load failed", error, 64);
        }
        ReducedModelEvaluationReport report;
        if (!EvaluateReducedModel(model, sequence, report, error) || !WriteReducedEvaluationReportJson(argv[4], report, error))
        {
            return PrintLoadFailure("Reduced model JSON evaluation failed", error, 65);
        }
        return 0;
    }
    if (command == "reduced-autotune")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ReducedModelTrainingSequence sequence;
        ReducedModelSettings settings = {};
        const uint32_t minGuides = ArgToUInt(argc, argv, 4, 4u);
        const uint32_t maxGuides = ArgToUInt(argc, argv, 5, std::max<uint32_t>(minGuides, 24u));
        const uint32_t step = ArgToUInt(argc, argv, 6, 4u);
        settings.maxInfluencesPerFollower = ArgToUInt(argc, argv, 7, settings.maxInfluencesPerFollower);
        settings.regularization = ArgToFloat(argc, argv, 8, settings.regularization);
        settings.validationStride = ArgToUInt(argc, argv, 9, 4u);
        settings.localGuideCandidateCount = ArgToUInt(argc, argv, 10, settings.localGuideCandidateCount);
        std::string error;
        if (!LoadReducedTrainingSequenceFromDirectory(argv[2], sequence, error))
        {
            return PrintLoadFailure("Training sequence load failed", error, 66);
        }
        ReducedModel bestModel;
        std::string summary;
        if (!AutoTuneReducedModel(sequence, minGuides, maxGuides, step, settings, bestModel, summary, error) || !SaveReducedModelToFile(argv[3], bestModel, error))
        {
            return PrintLoadFailure("Reduced model auto-tune failed", error, 67);
        }
        std::cout << summary;
        std::cout << BuildReducedModelSummary(bestModel) << "\n";
        return 0;
    }
    if (command == "reduced-study")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ReducedModelTrainingSequence sequence;
        ReducedModelSettings settings = {};
        const uint32_t minGuides = ArgToUInt(argc, argv, 4, 4u);
        const uint32_t maxGuides = ArgToUInt(argc, argv, 5, std::max<uint32_t>(minGuides, 24u));
        const uint32_t step = ArgToUInt(argc, argv, 6, 4u);
        settings.maxInfluencesPerFollower = ArgToUInt(argc, argv, 7, settings.maxInfluencesPerFollower);
        settings.regularization = ArgToFloat(argc, argv, 8, settings.regularization);
        settings.validationStride = ArgToUInt(argc, argv, 9, 4u);
        const bool compareModes = ArgToInt(argc, argv, 10, 1) != 0;
        settings.localGuideCandidateCount = ArgToUInt(argc, argv, 11, settings.localGuideCandidateCount);
        std::string error;
        if (!LoadReducedTrainingSequenceFromDirectory(argv[2], sequence, error))
        {
            return PrintLoadFailure("Training sequence load failed", error, 68);
        }
        std::vector<ReducedModelStudyEntry> entries;
        ReducedModel bestModel;
        std::string summary;
        if (!RunReducedModelStudy(sequence, minGuides, maxGuides, step, settings, compareModes, entries, bestModel, summary, error) ||
            !WriteReducedModelStudyJson(argv[3], entries, bestModel, error))
        {
            return PrintLoadFailure("Reduced model study failed", error, 69);
        }
        std::cout << summary;
        std::cout << BuildReducedModelSummary(bestModel) << "\n";
        return 0;
    }
    if (command == "reduced-eval")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ReducedModel model;
        ReducedModelTrainingSequence sequence;
        std::string error;
        if (!LoadReducedModelFromFile(argv[2], model, error))
        {
            return PrintLoadFailure("Reduced model load failed", error, 47);
        }
        if (!LoadReducedTrainingSequenceFromDirectory(argv[3], sequence, error))
        {
            return PrintLoadFailure("Training sequence load failed", error, 48);
        }
        ReducedModelEvaluationReport report;
        if (!EvaluateReducedModel(model, sequence, report, error))
        {
            return PrintLoadFailure("Reduced model evaluation failed", error, 49);
        }
        std::ostringstream text;
        text << "frames=" << report.frameCount
             << " rmsPos=" << report.rmsPositionError
             << " maxPos=" << report.maxPositionError
             << " rmsTip=" << report.rmsTipError
             << " maxTip=" << report.maxTipError
             << " rmsRoot=" << report.rmsRootError
             << " maxRoot=" << report.maxRootError;
        std::cout << text.str() << "\n";
        if (argc >= 5)
        {
            std::ofstream out(argv[4]);
            if (!out)
            {
                std::cerr << "Failed to write evaluation report.\n";
                return 50;
            }
            out << text.str() << "\n";
        }
        return 0;
    }
    if (command == "reduced-train")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ReducedModelTrainingSequence sequence;
        ReducedModelSettings settings = {};
        settings.guideCount = ArgToUInt(argc, argv, 4, settings.guideCount);
        settings.maxInfluencesPerFollower = ArgToUInt(argc, argv, 5, settings.maxInfluencesPerFollower);
        settings.regularization = ArgToFloat(argc, argv, 6, settings.regularization);
        settings.validationStride = ArgToUInt(argc, argv, 7, settings.validationStride);
        settings.useGuideDisplacements = ArgToInt(argc, argv, 8, settings.useGuideDisplacements ? 1 : 0) != 0;
        settings.rootLockBlend = ArgToFloat(argc, argv, 9, settings.rootLockBlend);
        settings.localGuideCandidateCount = ArgToUInt(argc, argv, 10, settings.localGuideCandidateCount);
        settings.enableScalpCollision = ArgToInt(argc, argv, 11, settings.enableScalpCollision ? 1 : 0) != 0;
        settings.scalpCollisionLockedPoints = ArgToUInt(argc, argv, 12, settings.scalpCollisionLockedPoints);
        std::string error;
        if (!LoadReducedTrainingSequenceFromDirectory(argv[2], sequence, error))
        {
            return PrintLoadFailure("Training sequence load failed", error, 51);
        }
        ReducedModel model;
        if (!TrainReducedModel(sequence, settings, model, error) || !SaveReducedModelToFile(argv[3], model, error))
        {
            return PrintLoadFailure("Reduced model training failed", error, 52);
        }
        std::cout << BuildReducedModelSummary(model) << "\n";
        return 0;
    }
    if (command == "synthesize-training-set")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairAsset asset;
        std::string error;
        if (!LoadHairAssetAny(argv[2], asset, error))
        {
            return PrintLoadFailure("Load failed", error, 53);
        }
        const uint32_t frameCount = ArgToUInt(argc, argv, 4, 48u);
        const float amplitude = ArgToFloat(argc, argv, 5, 0.035f);
        const float fps = ArgToFloat(argc, argv, 6, 30.0f);
        if (!SynthesizeReducedTrainingSequence(asset, argv[3], frameCount, fps, amplitude, error))
        {
            return PrintLoadFailure("Training-set synthesis failed", error, 54);
        }
        return 0;
    }
    if (command == "haircache-info")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        HairCache cache;
        std::string error;
        if (!LoadHairCacheFromFile(argv[2], cache, error))
        {
            return PrintLoadFailure("Hair cache load failed", error, 66);
        }
        std::cout << BuildHairCacheSummary(cache) << "\n";
        return 0;
    }
    if (command == "sample-haircache-frame")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        HairCache cache;
        std::string error;
        if (!LoadHairCacheFromFile(argv[2], cache, error))
        {
            return PrintLoadFailure("Hair cache load failed", error, 75);
        }
        HairAsset sampled;
        const float timeSeconds = ArgToFloat(argc, argv, 3, 0.0f);
        const bool loop = ArgToInt(argc, argv, 5, 1) != 0;
        if (!BuildHairAssetFrameFromCache(cache, timeSeconds, loop, sampled, error) ||
            !SaveHairAssetToHairBinary(argv[4], sampled, error))
        {
            return PrintLoadFailure("Hair cache sample failed", error, 76);
        }
        return 0;
    }
    if (command == "haircache-export-usda-sequence")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        HairCache cache = {};
        HairAsset referenceAsset = {};
        std::string error;
        if (!LoadHairCacheFromFile(argv[2], cache, error))
        {
            return PrintLoadFailure("Hair cache load failed", error, 181);
        }
        if (!LoadHairAssetAny(argv[3], referenceAsset, error))
        {
            return PrintLoadFailure("Reference asset load failed", error, 182);
        }
        const uint32_t everyNth = std::max<uint32_t>(1u, ArgToUInt(argc, argv, 5, 1u));
        const bool includeScalp = ArgToInt(argc, argv, 6, 1) != 0;
        const bool includeCards = ArgToInt(argc, argv, 7, 0) != 0;
        std::error_code ec;
        std::filesystem::create_directories(argv[4], ec);
        if (ec)
        {
            return PrintLoadFailure("Output directory creation failed", ec.message(), 183);
        }
        HairUsdExportSettings settings = {};
        settings.includeScalp = includeScalp;
        settings.includeCards = includeCards;
        settings.cardSettings.cardsPerGuide = 1u;
        settings.cardSettings.segmentsPerCard = std::max<uint32_t>(4u, referenceAsset.particlesPerStrand > 1u ? referenceAsset.particlesPerStrand - 1u : 4u);
        uint32_t written = 0u;
        for (uint32_t frameIndex = 0u; frameIndex < cache.FrameCount(); frameIndex += everyNth)
        {
            HairAsset frameAsset = referenceAsset;
            const float timeSeconds = static_cast<float>(frameIndex) / std::max(cache.frameRate, 1.0f);
            if (!BuildHairAssetFrameFromCache(cache, timeSeconds, false, frameAsset, error))
            {
                return PrintLoadFailure("Hair cache frame build failed", error, 184);
            }
            std::ostringstream name;
            name << "frame_" << std::setw(4) << std::setfill('0') << frameIndex << ".usda";
            if (!SaveHairAssetToUsda(std::filesystem::path(argv[4]) / name.str(), frameAsset, settings, error))
            {
                return PrintLoadFailure("Hair cache USDA export failed", error, 185);
            }
            ++written;
        }
        if (!WriteUsdSequenceManifest(argv[4], "haircache_usda_sequence", cache.FrameCount(), cache.frameRate, cache.strandCount, false, everyNth, error))
        {
            return PrintLoadFailure("USDA sequence manifest export failed", error, 186);
        }
        std::cout << "framesWritten=" << written << " sourceFrames=" << cache.FrameCount() << " outputDir=" << argv[4] << "\n";
        return 0;
    }
    if (command == "guidecache-export-usda-sequence")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        GuideCache cache = {};
        HairAsset referenceAsset = {};
        std::string error;
        if (!LoadGuideCacheFromFile(argv[2], cache, error))
        {
            return PrintLoadFailure("Guide cache load failed", error, 187);
        }
        if (!LoadHairAssetAny(argv[3], referenceAsset, error))
        {
            return PrintLoadFailure("Reference asset load failed", error, 188);
        }
        const uint32_t everyNth = std::max<uint32_t>(1u, ArgToUInt(argc, argv, 5, 1u));
        const bool includeScalp = ArgToInt(argc, argv, 6, 1) != 0;
        std::error_code ec;
        std::filesystem::create_directories(argv[4], ec);
        if (ec)
        {
            return PrintLoadFailure("Output directory creation failed", ec.message(), 189);
        }
        HairUsdExportSettings settings = {};
        settings.includeScalp = includeScalp;
        settings.includeCards = false;
