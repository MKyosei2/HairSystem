// main.cpp ordered part 03: main_argument_dispatch
// This file is included by ../main.cpp; do not compile it directly.

    if (command == "active-set-plan")
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
            return PrintLoadFailure("Hair asset load failed", error, 63);
        }
        ActiveSetClusterSettings clusterSettings = {};
        clusterSettings.targetGuideCountPerCluster = ArgToUInt(argc, argv, 4, clusterSettings.targetGuideCountPerCluster);
        clusterSettings.maxGuideRootDistance = ArgToFloat(argc, argv, 5, clusterSettings.maxGuideRootDistance);
        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(asset, clusterSettings);
        if (clusters.empty())
        {
            return PrintLoadFailure("Active-set cluster build failed", "No guide clusters were built from the asset.", 64);
        }
        ActiveSetRuntimeInput runtimeInput = {};
        runtimeInput.fixedTimeStep = 1.0f / 60.0f;
        ActiveSetScoringSettings scoringSettings = {};
        ActiveSetBudget budget = {};
        budget.maxActiveClusters = ArgToUInt(argc, argv, 6, budget.maxActiveClusters);
        budget.maxActiveGuides = ArgToUInt(argc, argv, 7, budget.maxActiveGuides);
        budget.promoteScoreThreshold = ArgToFloat(argc, argv, 8, budget.promoteScoreThreshold);
        ActiveSetPlan plan;
        if (!EvaluateActiveSetPlan(asset, clusters, runtimeInput, scoringSettings, budget, plan, error))
        {
            return PrintLoadFailure("Active-set evaluation failed", error, 65);
        }
        if (!WriteActiveSetPlanJson(argv[3], asset, plan, error))
        {
            return PrintLoadFailure("Active-set JSON export failed", error, 66);
        }
        std::cout << BuildActiveSetPlanSummary(plan) << "\n";
        return 0;
    }
    if (command == "active-set-dispatch")
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
            return PrintLoadFailure("Hair asset load failed", error, 169);
        }
        ActiveSetClusterSettings clusterSettings = {};
        clusterSettings.targetGuideCountPerCluster = ArgToUInt(argc, argv, 4, clusterSettings.targetGuideCountPerCluster);
        clusterSettings.maxGuideRootDistance = ArgToFloat(argc, argv, 5, clusterSettings.maxGuideRootDistance);
        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(asset, clusterSettings);
        if (clusters.empty())
        {
            return PrintLoadFailure("Active-set cluster build failed", "No guide clusters were built from the asset.", 169);
        }
        ActiveSetRuntimeInput runtimeInput = {};
        runtimeInput.fixedTimeStep = 1.0f / 60.0f;
        ActiveSetScoringSettings scoringSettings = {};
        ActiveSetBudget budget = {};
        budget.maxActiveClusters = ArgToUInt(argc, argv, 6, budget.maxActiveClusters);
        budget.maxActiveGuides = ArgToUInt(argc, argv, 7, budget.maxActiveGuides);
        budget.promoteScoreThreshold = ArgToFloat(argc, argv, 8, budget.promoteScoreThreshold);
        ActiveSetPlan plan;
        if (!EvaluateActiveSetPlan(asset, clusters, runtimeInput, scoringSettings, budget, plan, error))
        {
            return PrintLoadFailure("Active-set evaluation failed", error, 170);
        }
        ActiveSetDispatchPlan dispatchPlan;
        if (!BuildActiveSetDispatchPlan(asset, plan, dispatchPlan, error) ||
            !WriteActiveSetDispatchPlanJson(argv[3], asset, plan, dispatchPlan, error))
        {
            return PrintLoadFailure("Active-set dispatch export failed", error, 171);
        }
        std::cout << BuildActiveSetPlanSummary(plan)
                  << " fullDispatchStrands=" << dispatchPlan.fullSimulationStrands.size()
                  << " reducedDispatchStrands=" << dispatchPlan.reducedSimulationStrands.size()
                  << "\n";
        return 0;
    }
    if (command == "active-set-dispatch-pack")
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
            return PrintLoadFailure("Hair asset load failed", error, 173);
        }
        ActiveSetClusterSettings clusterSettings = {};
        clusterSettings.targetGuideCountPerCluster = ArgToUInt(argc, argv, 4, clusterSettings.targetGuideCountPerCluster);
        clusterSettings.maxGuideRootDistance = ArgToFloat(argc, argv, 5, clusterSettings.maxGuideRootDistance);
        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(asset, clusterSettings);
        if (clusters.empty())
        {
            return PrintLoadFailure("Active-set cluster build failed", "No guide clusters were built from the asset.", 174);
        }
        ActiveSetRuntimeInput runtimeInput = {};
        runtimeInput.fixedTimeStep = 1.0f / 60.0f;
        ActiveSetScoringSettings scoringSettings = {};
        ActiveSetBudget budget = {};
        budget.maxActiveClusters = ArgToUInt(argc, argv, 6, budget.maxActiveClusters);
        budget.maxActiveGuides = ArgToUInt(argc, argv, 7, budget.maxActiveGuides);
        budget.promoteScoreThreshold = ArgToFloat(argc, argv, 8, budget.promoteScoreThreshold);
        const bool dumpJson = ArgToInt(argc, argv, 9, 1) != 0;
        ActiveSetPlan plan;
        if (!EvaluateActiveSetPlan(asset, clusters, runtimeInput, scoringSettings, budget, plan, error))
        {
            return PrintLoadFailure("Active-set evaluation failed", error, 175);
        }
        ActiveSetDispatchPlan dispatchPlan = {};
        ActiveSetDispatchPack dispatchPack = {};
        if (!BuildActiveSetDispatchPlan(asset, plan, dispatchPlan, error) ||
            !BuildActiveSetDispatchPack(asset, plan, dispatchPlan, dispatchPack, error) ||
            !SaveActiveSetDispatchPackToFile(argv[3], dispatchPack, error))
        {
            return PrintLoadFailure("Active-set dispatch pack export failed", error, 176);
        }
        if (dumpJson)
        {
            std::filesystem::path jsonPath(argv[3]);
            jsonPath.replace_extension(".json");
            if (!WriteActiveSetDispatchPackJson(jsonPath, asset, plan, dispatchPack, error))
            {
                return PrintLoadFailure("Active-set dispatch pack JSON export failed", error, 177);
            }
        }
        std::cout << BuildActiveSetPlanSummary(plan)
                  << " " << BuildActiveSetDispatchPackSummary(dispatchPack)
                  << "\n";
        return 0;
    }
    if (command == "hybrid-train-bundle")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ReducedModelTrainingSequence teacherSequence;
        std::string error;
        if (!LoadHybridTeacherSequenceFromDirectory(argv[2], teacherSequence, error))
        {
            return PrintLoadFailure("Hybrid teacher sequence load failed", error, 172);
        }
        HybridModelTrainingSettings settings = {};
        settings.priorGuideCount = ArgToUInt(argc, argv, 4, settings.priorGuideCount);
        settings.residualGuideCount = ArgToUInt(argc, argv, 5, settings.residualGuideCount);
        settings.maxInfluencesPerFollower = ArgToUInt(argc, argv, 6, settings.maxInfluencesPerFollower);
        settings.regularization = ArgToFloat(argc, argv, 7, settings.regularization);
        settings.validationStride = ArgToUInt(argc, argv, 8, settings.validationStride);
        settings.priorGuideSmoothingAlpha = ArgToFloat(argc, argv, 9, settings.priorGuideSmoothingAlpha);
        settings.residualGuideGain = ArgToFloat(argc, argv, 10, settings.residualGuideGain);
        settings.residualBlend = ArgToFloat(argc, argv, 11, settings.residualBlend);
        ColliderSet colliders = {};
        if (!LoadOptionalColliders(argc, argv, colliders, error))
        {
            return PrintLoadFailure("Collider config load failed", error, 173);
        }
        HybridModelBundle bundle;
        HybridModelTrainingReport report;
        if (!TrainHybridModelBundle(teacherSequence, colliders, settings, bundle, report, error))
        {
            return PrintLoadFailure("Hybrid bundle training failed", error, 174);
        }
        if (!SaveHybridModelBundle(argv[3], bundle, &report, error))
        {
            return PrintLoadFailure("Hybrid bundle save failed", error, 175);
        }
        std::cout << BuildHybridModelBundleSummary(bundle, &report) << "\n";
        return 0;
    }
    if (command == "hybrid-bundle-info")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        HybridModelBundle bundle;
        std::string error;
        if (!LoadHybridModelBundle(argv[2], bundle, error))
        {
            return PrintLoadFailure("Hybrid bundle load failed", error, 176);
        }
        std::cout << BuildHybridModelBundleSummary(bundle, nullptr) << "\n";
        return 0;
    }
    if (command == "temporal-export-csv")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        TemporalGuideNetwork model = {};
        std::string error;
        if (!LoadTemporalGuideNetworkFromFile(argv[2], model, error))
        {
            return PrintLoadFailure("Temporal network load failed", error, 183);
        }
        if (!ExportTemporalGuideNetworkToCsvDirectory(argv[3], model, error))
        {
            return PrintLoadFailure("Temporal CSV export failed", error, 184);
        }
        std::cout << BuildTemporalGuideNetworkSummary(model, nullptr) << " outputDir=" << argv[3] << "\n";
        return 0;
    }
    if (command == "temporal-import-csv")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        TemporalGuideNetwork model = {};
        std::string error;
        if (!ImportTemporalGuideNetworkFromCsvDirectory(argv[2], model, error))
        {
            return PrintLoadFailure("Temporal CSV import failed", error, 185);
        }
        if (!SaveTemporalGuideNetworkToFile(argv[3], model, error))
        {
            return PrintLoadFailure("Temporal network save failed", error, 186);
        }
        std::cout << BuildTemporalGuideNetworkSummary(model, nullptr) << " output=" << argv[3] << "\n";
        return 0;
    }
    if (command == "active-set-predictor-export-csv")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ActiveSetPredictor predictor = {};
        std::string error;
        if (!LoadActiveSetPredictorFromFile(argv[2], predictor, error))
        {
            return PrintLoadFailure("Active-set predictor load failed", error, 187);
        }
        if (!ExportActiveSetPredictorToCsvDirectory(argv[3], predictor, error))
        {
            return PrintLoadFailure("Active-set predictor CSV export failed", error, 188);
        }
        std::cout << BuildActiveSetPredictorSummary(predictor, nullptr) << " outputDir=" << argv[3] << "\n";
        return 0;
    }
    if (command == "active-set-predictor-import-csv")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ActiveSetPredictor predictor = {};
        std::string error;
        if (!ImportActiveSetPredictorFromCsvDirectory(argv[2], predictor, error))
        {
            return PrintLoadFailure("Active-set predictor CSV import failed", error, 189);
        }
        if (!SaveActiveSetPredictorToFile(argv[3], predictor, error))
        {
            return PrintLoadFailure("Active-set predictor save failed", error, 190);
        }
        std::cout << BuildActiveSetPredictorSummary(predictor, nullptr) << " output=" << argv[3] << "\n";
        return 0;
    }
    if (command == "hybrid-bundle-export-interop")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HybridModelBundle bundle = {};
        std::string error;
        if (!LoadHybridModelBundle(argv[2], bundle, error))
        {
            return PrintLoadFailure("Hybrid bundle load failed", error, 191);
        }
        std::error_code ec;
        std::filesystem::create_directories(argv[3], ec);
        if (ec)
        {
            return PrintLoadFailure("Interop directory creation failed", ec.message(), 192);
        }
        if (!ExportTemporalGuideNetworkToCsvDirectory(std::filesystem::path(argv[3]) / "prior_temporal_csv", bundle.priorTemporalNetwork, error) ||
            !ExportTemporalGuideNetworkToCsvDirectory(std::filesystem::path(argv[3]) / "residual_temporal_csv", bundle.residualTemporalNetwork, error) ||
            !ExportActiveSetPredictorToCsvDirectory(std::filesystem::path(argv[3]) / "active_set_predictor_csv", bundle.activeSetPredictor, error))
        {
            return PrintLoadFailure("Hybrid bundle interop export failed", error, 193);
        }
        std::ofstream manifest(std::filesystem::path(argv[3]) / "interop_manifest.json");
        if (!manifest)
        {
            return PrintLoadFailure("Hybrid bundle interop export failed", "Failed to write interop manifest.", 194);
        }
        manifest << "{\n";
        manifest << "  \"type\": \"hybrid_bundle_interop\",\n";
        manifest << "  \"priorTemporal\": \"prior_temporal_csv\",\n";
        manifest << "  \"residualTemporal\": \"residual_temporal_csv\",\n";
        manifest << "  \"activeSetPredictor\": \"active_set_predictor_csv\"\n";
        manifest << "}\n";
        std::cout << "interopDir=" << argv[3] << "\n";
        return 0;
    }
    if (command == "hybrid-bundle-import-interop")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        HybridModelBundle bundle = {};
        std::string error;
        if (!LoadHybridModelBundle(argv[2], bundle, error))
        {
            return PrintLoadFailure("Hybrid bundle load failed", error, 195);
        }
        const std::filesystem::path interopDir(argv[3]);
        const std::filesystem::path priorDir = interopDir / "prior_temporal_csv";
        const std::filesystem::path residualDir = interopDir / "residual_temporal_csv";
        const std::filesystem::path predictorDir = interopDir / "active_set_predictor_csv";
        if (std::filesystem::exists(priorDir))
        {
            if (!ImportTemporalGuideNetworkFromCsvDirectory(priorDir, bundle.priorTemporalNetwork, error))
            {
                return PrintLoadFailure("Prior temporal import failed", error, 196);
            }
        }
        if (std::filesystem::exists(residualDir))
        {
            if (!ImportTemporalGuideNetworkFromCsvDirectory(residualDir, bundle.residualTemporalNetwork, error))
            {
                return PrintLoadFailure("Residual temporal import failed", error, 197);
            }
        }
        if (std::filesystem::exists(predictorDir))
        {
            if (!ImportActiveSetPredictorFromCsvDirectory(predictorDir, bundle.activeSetPredictor, error))
            {
                return PrintLoadFailure("Predictor import failed", error, 198);
            }
        }
        if (!SaveHybridModelBundle(argv[4], bundle, nullptr, error))
        {
            return PrintLoadFailure("Hybrid bundle save failed", error, 199);
        }
        std::cout << BuildHybridModelBundleSummary(bundle, nullptr) << " outputDir=" << argv[4] << "\n";
        return 0;
    }
    if (command == "hybrid-run-bundle")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        ReducedModelTrainingSequence teacherSequence;
        HybridModelBundle bundle;
        std::string error;
        if (!LoadHybridTeacherSequenceFromDirectory(argv[2], teacherSequence, error))
        {
            return PrintLoadFailure("Hybrid teacher sequence load failed", error, 177);
        }
        if (!LoadHybridModelBundle(argv[3], bundle, error))
        {
            return PrintLoadFailure("Hybrid bundle load failed", error, 178);
        }
        FullHybridRuntimeSettings settings = {};
        settings.residualBlendOverride = ArgToFloat(argc, argv, 5, settings.residualBlendOverride);
        settings.useBudgetOverride = argc >= 9;
        if (settings.useBudgetOverride)
        {
            settings.budgetOverride = bundle.budget;
            settings.budgetOverride.maxActiveClusters = ArgToUInt(argc, argv, 6, settings.budgetOverride.maxActiveClusters);
            settings.budgetOverride.maxActiveGuides = ArgToUInt(argc, argv, 7, settings.budgetOverride.maxActiveGuides);
            settings.budgetOverride.targetFrameMilliseconds = ArgToFloat(argc, argv, 8, settings.budgetOverride.targetFrameMilliseconds);
        }
        settings.exportPerFrameHairBins = ArgToInt(argc, argv, 9, 0) != 0;
        ColliderSet colliders = {};
        if (!LoadOptionalColliders(argc, argv, colliders, error))
        {
            return PrintLoadFailure("Collider config load failed", error, 179);
        }
        FullHybridRuntimeReport report;
        if (!RunFullHybridRuntime(teacherSequence, colliders, bundle, settings, argv[4], report, error))
        {
            return PrintLoadFailure("Full hybrid runtime failed", error, 180);
        }
        std::cout << "frames=" << report.frameCount
                  << " avgPriorRms=" << report.averagePriorRmsError
                  << " avgHybridRmsBefore=" << report.averageHybridRmsErrorBeforeCorrection
                  << " avgHybridRmsAfter=" << report.averageHybridRmsErrorAfterCorrection
                  << " avgSelectedClusters=" << report.averageSelectedClusterCount
                  << " avgSelectedGuides=" << report.averageSelectedGuideCount
                  << " avgPenetration=" << report.averagePenetration
                  << " avgPredictorRisk=" << report.averageGuidePredictorRisk
                  << " p95EstimatedMs=" << report.p95EstimatedFrameMilliseconds
                  << " hit60=" << report.hitRate60Fps << "\n";
        return 0;
    }
    if (command == "hybrid-teacher-dataset")
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
            return PrintLoadFailure("Hair asset load failed", error, 70);
        }
        HybridTeacherDatasetSettings settings = {};
        settings.frameCount = ArgToUInt(argc, argv, 4, settings.frameCount);
        settings.frameRate = ArgToFloat(argc, argv, 5, settings.frameRate);
        settings.swayAmplitude = ArgToFloat(argc, argv, 6, settings.swayAmplitude);
        settings.budget.maxActiveClusters = ArgToUInt(argc, argv, 7, settings.budget.maxActiveClusters);
        settings.budget.maxActiveGuides = ArgToUInt(argc, argv, 8, settings.budget.maxActiveGuides);
        settings.budget.targetFrameMilliseconds = ArgToFloat(argc, argv, 9, settings.budget.targetFrameMilliseconds);
        settings.budget.maxSolverIterations = ArgToUInt(argc, argv, 10, settings.budget.maxSolverIterations);
        settings.budget.minSolverIterations = std::min<uint32_t>(settings.budget.minSolverIterations, settings.budget.maxSolverIterations);
        HybridTeacherDatasetReport report;
        ColliderSet colliders = {};
        if (!LoadOptionalColliders(argc, argv, colliders, error))
        {
            return PrintLoadFailure("Collider config load failed", error, 170);
        }
        if (!GenerateHybridTeacherDataset(asset, colliders, settings, argv[3], report, error))
        {
            return PrintLoadFailure("Hybrid teacher dataset generation failed", error, 71);
        }
        std::cout << "frames=" << report.frameCount
                  << " avgSelectedClusters=" << report.averageSelectedClusterCount
                  << " avgSelectedGuides=" << report.averageSelectedGuideCount
                  << " avgSelectedStrands=" << report.averageSelectedStrandCount
                  << " avgPenetration=" << report.averagePenetration
                  << " maxPenetration=" << report.maxPenetration
                  << " avgCorrectedPoints=" << report.averageCorrectedPointCount
                  << " peakEstimatedMs=" << report.peakEstimatedSimulationMilliseconds << "\n";
        return 0;
    }
    if (command == "hybrid-compare")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        ReducedModelTrainingSequence teacherSequence;
        HybridModelBundle bundle;
        std::string error;
        if (!LoadHybridTeacherSequenceFromDirectory(argv[2], teacherSequence, error))
        {
            return PrintLoadFailure("Hybrid teacher sequence load failed", error, 181);
        }
        if (!LoadHybridModelBundle(argv[3], bundle, error))
        {
            return PrintLoadFailure("Hybrid bundle load failed", error, 182);
        }
        FullHybridRuntimeSettings baseSettings = {};
        baseSettings.useBudgetOverride = argc >= 8;
        if (baseSettings.useBudgetOverride)
        {
            baseSettings.budgetOverride = bundle.budget;
            baseSettings.budgetOverride.maxActiveClusters = ArgToUInt(argc, argv, 5, baseSettings.budgetOverride.maxActiveClusters);
            baseSettings.budgetOverride.maxActiveGuides = ArgToUInt(argc, argv, 6, baseSettings.budgetOverride.maxActiveGuides);
            baseSettings.budgetOverride.targetFrameMilliseconds = ArgToFloat(argc, argv, 7, baseSettings.budgetOverride.targetFrameMilliseconds);
        }
        ColliderSet colliders = {};
        if (!LoadOptionalColliders(argc, argv, colliders, error))
        {
            return PrintLoadFailure("Collider config load failed", error, 183);
        }
        struct VariantSpec
        {
            const char* code;
            const char* label;
            FullHybridRuntimeSettings settings;
        };
        std::vector<VariantSpec> variants;
        {
            VariantSpec spec = { "A", "pure_physics", baseSettings };
            spec.settings.useTeacherPhysicsProxy = true;
            spec.settings.enableResidualModel = false;
            spec.settings.enableSparseCorrectivePhysics = false;
            spec.settings.enableTemporalNetworks = false;
            spec.settings.enableActiveSetPredictor = false;
            variants.push_back(spec);
        }
        {
            VariantSpec spec = { "B", "prior_residual_only", baseSettings };
            spec.settings.enableSparseCorrectivePhysics = false;
            spec.settings.enableActiveSetPredictor = false;
            variants.push_back(spec);
        }
        {
            VariantSpec spec = { "C", "active_set_physics_only", baseSettings };
            spec.settings.enableResidualModel = false;
            spec.settings.enableTemporalNetworks = false;
            spec.settings.enableActiveSetPredictor = false;
            variants.push_back(spec);
        }
        {
            VariantSpec spec = { "D", "full_hybrid", baseSettings };
            variants.push_back(spec);
        }
        std::error_code ec;
        std::filesystem::create_directories(argv[4], ec);
        if (ec)
        {
            return PrintLoadFailure("Compare output failed", "Failed to create comparison output directory.", 184);
        }
        std::ofstream csv(std::filesystem::path(argv[4]) / "hybrid_compare_summary.csv");
        if (!csv)
        {
            return PrintLoadFailure("Compare output failed", "Failed to open hybrid_compare_summary.csv.", 185);
        }
        csv << "methodCode,methodLabel,frameCount,avgPriorRms,avgHybridRmsBefore,avgHybridRmsAfter,avgSelectedClusters,avgSelectedGuides,avgCorrectedPoints,avgPenetration,maxPenetration,avgPredictorRisk,p95EstimatedFrameMs,maxEstimatedFrameMs,hitRate60Fps\n";
        std::ofstream json(std::filesystem::path(argv[4]) / "hybrid_compare_summary.json");
        if (!json)
        {
            return PrintLoadFailure("Compare output failed", "Failed to open hybrid_compare_summary.json.", 186);
        }
        json << "{\n  \"variants\": [\n";
        bool first = true;
        for (const VariantSpec& variant : variants)
        {
            const std::filesystem::path variantDir = std::filesystem::path(argv[4]) / variant.label;
            FullHybridRuntimeReport report;
            if (!RunFullHybridRuntime(teacherSequence, colliders, bundle, variant.settings, variantDir, report, error))
            {
                return PrintLoadFailure("Hybrid compare runtime failed", error, 187);
            }
            csv << variant.code << "," << variant.label << "," << report.frameCount << ","
                << report.averagePriorRmsError << "," << report.averageHybridRmsErrorBeforeCorrection << ","
                << report.averageHybridRmsErrorAfterCorrection << "," << report.averageSelectedClusterCount << ","
                << report.averageSelectedGuideCount << "," << report.averageCorrectedPointCount << ","
