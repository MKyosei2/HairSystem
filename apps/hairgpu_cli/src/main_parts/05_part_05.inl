// main.cpp ordered part 05: part_05
// This file is included by ../main.cpp; do not compile it directly.

        HairAsset guideReference = {};
        if (!BuildGuideOnlyReferenceAsset(referenceAsset, guideReference, error))
        {
            return PrintLoadFailure("Guide reference build failed", error, 190);
        }
        uint32_t written = 0u;
        for (uint32_t frameIndex = 0u; frameIndex < cache.FrameCount(); frameIndex += everyNth)
        {
            HairAsset frameAsset = {};
            if (!BuildGuideFrameAsset(cache, referenceAsset, frameIndex, frameAsset, error))
            {
                return PrintLoadFailure("Guide cache frame build failed", error, 191);
            }
            frameAsset.scalpMesh = includeScalp ? guideReference.scalpMesh : ScalpMesh{};
            std::ostringstream name;
            name << "frame_" << std::setw(4) << std::setfill('0') << frameIndex << ".usda";
            if (!SaveHairAssetToUsda(std::filesystem::path(argv[4]) / name.str(), frameAsset, settings, error))
            {
                return PrintLoadFailure("Guide cache USDA export failed", error, 192);
            }
            ++written;
        }
        if (!WriteUsdSequenceManifest(argv[4], "guidecache_usda_sequence", cache.FrameCount(), cache.frameRate, static_cast<uint32_t>(guideReference.strands.size()), true, everyNth, error))
        {
            return PrintLoadFailure("USDA sequence manifest export failed", error, 193);
        }
        std::cout << "framesWritten=" << written << " sourceFrames=" << cache.FrameCount() << " outputDir=" << argv[4] << "\n";
        return 0;
    }
    if (command == "bake-obj-sdf")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ScalpMesh scalpMesh;
        std::string error;
        if (!LoadScalpMeshFromObj(argv[2], scalpMesh, error))
        {
            return PrintLoadFailure("OBJ scalp load failed", error, 167);
        }
        const uint32_t resolution = ArgToUInt(argc, argv, 4, 28u);
        const float padding = ArgToFloat(argc, argv, 5, 0.04f);
        SdfGridCollider grid = {};
        if (!BakeSdfGridFromScalpMesh(scalpMesh, resolution, padding, grid, error) ||
            !ExportSdfGridColliderCsv(grid, argv[3], error))
        {
            return PrintLoadFailure("OBJ SDF bake failed", error, 168);
        }
        std::cout << "resolution=" << grid.resolutionX << "x" << grid.resolutionY << "x" << grid.resolutionZ
                  << " values=" << grid.values.size() << "\n";
        return 0;
    }
    if (command == "metrics-definitions")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        std::string error;
        if (!WriteMetricDefinitionsJson(argv[2], error))
        {
            return PrintLoadFailure("Metric definition export failed", error, 336);
        }
        std::cout << "output=" << argv[2] << "\n";
        return 0;
    }
    if (command == "experiment-suite-template")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        ExperimentSuiteSettings settings = {};
        settings.proposalBudget.targetFrameMilliseconds = 8.0f;
        settings.proposalBudget.frameBudgetMilliseconds = 16.67f;
        settings.proposalBudget.estimatedFixedCostMilliseconds = 6.0f;
        settings.proposalBudget.estimatedClusterCostMilliseconds = 0.85f;
        std::string error;
        if (!WriteExperimentSuiteTemplate(argv[2], settings, error))
        {
            return PrintLoadFailure("Experiment suite template write failed", error, 337);
        }
        std::cout << "outputDir=" << argv[2] << "\n";
        return 0;
    }
    if (command == "edit-stack-template")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        std::string error;
        if (!LocalWriteEditStackTemplateCsv(argv[2], error))
        {
            return PrintLoadFailure("Edit stack template write failed", error, 200);
        }
        std::cout << "templateWritten=" << argv[2] << "\n";
        return 0;
    }
    if (argc < 3)
    {
        PrintUsage();
        return 1;
    }
    const std::filesystem::path inputPath = argv[2];
    HairAsset asset;
    std::string error;
    if (!LoadHairAssetAny(inputPath, asset, error))
    {
        return PrintLoadFailure("Load failed", error, 2);
    }
    if (command == "info")
    {
        const HairAssetMetrics metrics = ComputeHairAssetMetrics(asset);
        std::cout << "strands=" << metrics.strandCount
                  << " guides=" << metrics.guideCount
                  << " followers=" << metrics.followerCount
                  << " points=" << metrics.pointCount
                  << " pps=" << metrics.particlesPerStrand
                  << " avgLen=" << metrics.averageLength
                  << " maxLen=" << metrics.maxLength
                  << " avgWidth=" << metrics.averageWidth
                  << "\n";
        return 0;
    }
    if (command == "validate")
    {
        const HairAssetValidationReport report = ValidateHairAsset(asset);
        const std::string text = BuildHairAssetValidationSummary(report, true, 32u);
        std::cout << text << "\n";
        if (argc >= 4)
        {
            std::ofstream out(argv[3]);
            if (!out)
            {
                std::cerr << "Failed to write validation report.\n";
                return 3;
            }
            out << text << "\n";
        }
        return report.valid ? 0 : 4;
    }
    if (command == "metrics-json")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        if (!WriteHairAssetMetricsJson(argv[3], asset, error))
        {
            return PrintLoadFailure("Metrics JSON export failed", error, 55);
        }
        return 0;
    }
    if (command == "retessellate-strands")
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
            return PrintLoadFailure("Hair asset load failed", error, 330);
        }
        StrandRetessellationSettings settings = {};
        settings.targetPointCount = ClampParticlesPerStrandValue(ArgToUInt(argc, argv, 4, std::max<uint32_t>(asset.particlesPerStrand, 15u)));
        settings.snapRootsToScalp = ArgToInt(argc, argv, 5, 1) != 0;
        if (!RetessellateHairAsset(asset, settings, error))
        {
            return PrintLoadFailure("Retessellate strands failed", error, 331);
        }
        if (!SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Hair binary save failed", error, 332);
        }
        const uint32_t segmentCount = (asset.particlesPerStrand > 0u) ? (asset.particlesPerStrand - 1u) : 0u;
        std::cout << "Retessellated strands to " << asset.particlesPerStrand << " particles / " << segmentCount << " segments per strand.\n";
        return 0;
    }
    if (command == "segment-profile")
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
            return PrintLoadFailure("Hair asset load failed", error, 333);
        }
        const std::string profile = (argc >= 5) ? argv[4] : "balanced";
        StrandRetessellationSettings settings = {};
        settings.targetPointCount = ResolveSegmentProfileTarget(asset, profile, ArgToUInt(argc, argv, 5, 0u));
        settings.snapRootsToScalp = true;
        if (!RetessellateHairAsset(asset, settings, error))
        {
            return PrintLoadFailure("Segment profile retessellation failed", error, 334);
        }
        if (!SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Hair binary save failed", error, 335);
        }
        const uint32_t segmentCount = (asset.particlesPerStrand > 0u) ? (asset.particlesPerStrand - 1u) : 0u;
        std::cout << "Applied segment profile '" << profile << "' => "
                  << asset.particlesPerStrand << " particles / " << segmentCount << " segments per strand.\n";
        return 0;
    }

    if (command == "save-hairbin")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        if (!SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Save failed", error, 14);
        }
        return 0;
    }
    if (command == "select-region")
    {
        RegionSelectionSettings region = ParseRegion(argc, argv, 3);
        const RegionSelectionResult result = SelectRegion(asset, region);
        std::cout << "selected=" << result.strands.size() << "\n";
        return 0;
    }
    if (command == "edit-region")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        RegionSelectionSettings region = ParseRegion(argc, argv, 4);
        EditSettings edit;
        const int base = 11;
        edit.lengthScale = ArgToFloat(argc, argv, base, edit.lengthScale);
        edit.widthScale = ArgToFloat(argc, argv, base + 1, edit.widthScale);
        edit.rootOffset.y = ArgToFloat(argc, argv, base + 2, edit.rootOffset.y);
        edit.tipOffset.y = ArgToFloat(argc, argv, base + 3, edit.tipOffset.y);
        edit.sideOffset = ArgToFloat(argc, argv, base + 4, edit.sideOffset);
        edit.yawRadians = ArgToFloat(argc, argv, base + 5, edit.yawRadians);
        edit.followerInfluence = ArgToFloat(argc, argv, base + 6, edit.followerInfluence);
        edit.clumpStrength = ArgToFloat(argc, argv, base + 7, edit.clumpStrength);
        edit.curlStrength = ArgToFloat(argc, argv, base + 8, edit.curlStrength);
        edit.partingStrength = ArgToFloat(argc, argv, base + 9, edit.partingStrength);
        edit.partingCenterU = ArgToFloat(argc, argv, base + 10, edit.partingCenterU);
        edit.partingWidthUv = ArgToFloat(argc, argv, base + 11, edit.partingWidthUv);
        const RegionSelectionResult result = SelectRegion(asset, region);
        if (!ApplyEditToRegion(asset, result, edit, error) || !SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Edit failed", error, 15);
        }
        return 0;
    }
    if (command == "edit-strand")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        EditLayer layer = {};
        layer.name = "explicit_strand_edit";
        layer.region.mode = RegionSelectMode::All;
        if (!ParseStrandSelectionSpec(argv[4], layer.strandSelection, error))
        {
            return PrintLoadFailure("Strand selection parse failed", error, 198);
        }
        layer.strandSelectionMode = StrandSelectionMode::Replace;
        if (argc >= 6 && !ParseStrandSelectionModeToken(argv[5], layer.strandSelectionMode))
        {
            return PrintLoadFailure("Strand selection mode parse failed", std::string("Unsupported mode: ") + argv[5], 199);
        }
        const int base = 6;
        layer.edit.lengthScale = ArgToFloat(argc, argv, base, layer.edit.lengthScale);
        layer.edit.widthScale = ArgToFloat(argc, argv, base + 1, layer.edit.widthScale);
        layer.edit.rootOffset.x = ArgToFloat(argc, argv, base + 2, layer.edit.rootOffset.x);
        layer.edit.rootOffset.y = ArgToFloat(argc, argv, base + 3, layer.edit.rootOffset.y);
        layer.edit.rootOffset.z = ArgToFloat(argc, argv, base + 4, layer.edit.rootOffset.z);
        layer.edit.tipOffset.x = ArgToFloat(argc, argv, base + 5, layer.edit.tipOffset.x);
        layer.edit.tipOffset.y = ArgToFloat(argc, argv, base + 6, layer.edit.tipOffset.y);
        layer.edit.tipOffset.z = ArgToFloat(argc, argv, base + 7, layer.edit.tipOffset.z);
        layer.edit.sideOffset = ArgToFloat(argc, argv, base + 8, layer.edit.sideOffset);
        layer.edit.yawRadians = ArgToFloat(argc, argv, base + 9, layer.edit.yawRadians);
        layer.edit.followerInfluence = ArgToFloat(argc, argv, base + 10, layer.edit.followerInfluence);
        layer.edit.clumpStrength = ArgToFloat(argc, argv, base + 11, layer.edit.clumpStrength);
        layer.edit.curlStrength = ArgToFloat(argc, argv, base + 12, layer.edit.curlStrength);
        layer.edit.partingStrength = ArgToFloat(argc, argv, base + 13, layer.edit.partingStrength);
        layer.edit.partingCenterU = ArgToFloat(argc, argv, base + 14, layer.edit.partingCenterU);
        layer.edit.partingWidthUv = ArgToFloat(argc, argv, base + 15, layer.edit.partingWidthUv);

        EditStack stack = {};
        stack.layers.push_back(layer);
        const HairAsset baseAsset = asset;
        RuntimeEditState state = {};
        if (!ApplyEditStackToAsset(asset, baseAsset, stack, &state, error) || !SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Strand edit failed", error, 200);
        }
        std::cout << "selectedStrands=" << layer.strandSelection.size()
                  << " mode=" << StrandSelectionModeToString(layer.strandSelectionMode)
                  << " activeGuides=" << state.activeGuideCount
                  << " output=" << argv[3] << "\n";
        return 0;
    }

    if (command == "edit-stack-apply")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        EditStack stack = {};
        if (!LocalLoadEditStackCsv(argv[3], stack, error))
        {
            return PrintLoadFailure("Edit stack load failed", error, 201);
        }
        const HairAsset baseAsset = asset;
        RuntimeEditState state = {};
        if (!ApplyEditStackToAsset(asset, baseAsset, stack, &state, error) || !SaveHairAssetToHairBinary(argv[4], asset, error))
        {
            return PrintLoadFailure("Edit stack apply failed", error, 202);
        }
        if (argc >= 6 && !WriteEditRuntimeStateJson(argv[5], stack, state, baseAsset, error))
        {
            return PrintLoadFailure("Edit runtime state write failed", error, 203);
        }
        std::cout << "layers=" << stack.layers.size()
                  << " activeGuides=" << state.activeGuideCount
                  << " maxGuideInfluence=" << state.maxGuideInfluence
                  << " output=" << argv[4] << "\n";
        return 0;
    }
    if (command == "edit-stack-eval")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        EditStack stack = {};
        if (!LocalLoadEditStackCsv(argv[3], stack, error))
        {
            return PrintLoadFailure("Edit stack load failed", error, 204);
        }
        const HairAsset baseAsset = asset;
        RuntimeEditState state = {};
        const uint32_t samplesPerStrand = std::max<uint32_t>(2u, ArgToUInt(argc, argv, 5, 16u));
        const auto startTime = std::chrono::steady_clock::now();
        if (!ApplyEditStackToAsset(asset, baseAsset, stack, &state, error))
        {
            return PrintLoadFailure("Edit stack eval failed", error, 205);
        }
        const auto endTime = std::chrono::steady_clock::now();
        HairEditComparisonReport report = CompareHairEditRegion(baseAsset, asset, samplesPerStrand);
        report.editToRunLatencyMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(endTime - startTime).count());

        std::error_code ec;
        std::filesystem::create_directories(argv[4], ec);
        if (ec)
        {
            return PrintLoadFailure("Edit eval output failed", ec.message(), 206);
        }
        const std::filesystem::path outputDir(argv[4]);
        if (!SaveHairAssetToHairBinary(outputDir / "edited.hairbin", asset, error) ||
            !WriteEditRuntimeStateJson(outputDir / "edit_runtime.json", stack, state, baseAsset, error) ||
            !WriteEditComparisonJson(outputDir / "edit_metrics.json", report, error))
        {
            return PrintLoadFailure("Edit eval write failed", error, 207);
        }
        std::cout << "layers=" << stack.layers.size()
                  << " activeGuides=" << state.activeGuideCount
                  << " locality=" << report.editLocalityScore
                  << " boundary=" << report.boundaryNaturalnessScore
                  << " latencyMs=" << report.editToRunLatencyMilliseconds
                  << " outputDir=" << outputDir.string() << "\n";
        return 0;
    }
    if (command == "edit-compare")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        HairAsset editedAsset = {};
        if (!LoadHairAssetAny(argv[3], editedAsset, error))
        {
            return PrintLoadFailure("Edited asset load failed", error, 208);
        }
        HairEditComparisonReport report = CompareHairEditRegion(asset, editedAsset, std::max<uint32_t>(2u, ArgToUInt(argc, argv, 5, 16u)));
        if (!WriteEditComparisonJson(argv[4], report, error))
        {
            return PrintLoadFailure("Edit comparison write failed", error, 209);
        }
        std::cout << "locality=" << report.editLocalityScore
                  << " boundary=" << report.boundaryNaturalnessScore
                  << " editedStrandRatio=" << report.editedStrandRatio
                  << " output=" << argv[4] << "\n";
        return 0;
    }
    if (command == "experiment-suite")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        EditStack stack = {};
        if (!LocalLoadEditStackCsv(argv[3], stack, error))
        {
            return PrintLoadFailure("Edit stack load failed", error, 338);
        }
        const HairAsset baseAsset = asset;
        RuntimeEditState state = {};
        HairAsset editedAsset = asset;
        const uint32_t samplesPerStrand = std::max<uint32_t>(2u, ArgToUInt(argc, argv, 5, 16u));
        const auto startTime = std::chrono::steady_clock::now();
        if (!ApplyEditStackToAsset(editedAsset, baseAsset, stack, &state, error))
        {
            return PrintLoadFailure("Experiment suite edit apply failed", error, 339);
        }
        const auto endTime = std::chrono::steady_clock::now();
        HairEditComparisonReport editReport = CompareHairEditRegion(baseAsset, editedAsset, samplesPerStrand);
        editReport.editToRunLatencyMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(endTime - startTime).count());

        ExperimentSuiteSettings settings = {};
        settings.samplesPerStrand = samplesPerStrand;
        settings.clusterSettings.targetGuideCountPerCluster = ArgToUInt(argc, argv, 6, settings.clusterSettings.targetGuideCountPerCluster);
        settings.clusterSettings.maxGuideRootDistance = ArgToFloat(argc, argv, 7, settings.clusterSettings.maxGuideRootDistance);
        settings.proposalBudget.maxActiveClusters = ArgToUInt(argc, argv, 8, settings.proposalBudget.maxActiveClusters);
        settings.proposalBudget.maxActiveGuides = ArgToUInt(argc, argv, 9, settings.proposalBudget.maxActiveGuides);
        settings.proposalBudget.promoteScoreThreshold = ArgToFloat(argc, argv, 10, settings.proposalBudget.promoteScoreThreshold);

        std::error_code ec;
        std::filesystem::create_directories(argv[4], ec);
        if (ec)
        {
            return PrintLoadFailure("Experiment suite output failed", ec.message(), 340);
        }
        const std::filesystem::path outputDir(argv[4]);
        const std::filesystem::path runtimeDir = outputDir / "runtime_variants";
        std::filesystem::create_directories(runtimeDir, ec);
        if (ec)
        {
            return PrintLoadFailure("Experiment suite output failed", ec.message(), 341);
        }
        if (!SaveHairAssetToHairBinary(outputDir / "edited.hairbin", editedAsset, error) ||
            !WriteEditRuntimeStateJson(outputDir / "edit_runtime.json", stack, state, baseAsset, error) ||
            !WriteEditComparisonJson(outputDir / "edit_metrics.json", editReport, error) ||
            !WriteMetricDefinitionsJson(outputDir / "metric_definitions.json", error) ||
            !WriteExperimentSuitePresetJson(outputDir / "experiment_suite_preset.json", settings, error))
        {
            return PrintLoadFailure("Experiment suite write failed", error, 342);
        }

        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(baseAsset, settings.clusterSettings);
        if (clusters.empty())
        {
            return PrintLoadFailure("Experiment suite cluster build failed", "No guide clusters were built from the asset.", 343);
        }

        std::vector<ExperimentVariantSummary> variants;
        variants.reserve(4u);

        ExperimentVariantSummary predictionOnly = {};
        predictionOnly.name = "prediction_only";
        predictionOnly.budget = settings.proposalBudget;
        predictionOnly.budget.minActiveClusters = 0u;
        predictionOnly.budget.maxActiveClusters = 0u;
        predictionOnly.budget.maxActiveGuides = 0u;
        predictionOnly.budget.maxActiveStrands = 0u;
        predictionOnly.budget.promoteScoreThreshold = 2.0f;
        predictionOnly.budget.enableHardBudgetClamp = false;
        if (!BuildEmptyVariantPlan(baseAsset, clusters, predictionOnly.budget, predictionOnly, error) ||
            !WriteActiveSetPlanJson(runtimeDir / "prediction_only_plan.json", baseAsset, predictionOnly.plan, error))
        {
            return PrintLoadFailure("Experiment suite prediction-only plan failed", error, 344);
        }
        variants.push_back(std::move(predictionOnly));

        ExperimentVariantSummary runtimeOnly = {};
        runtimeOnly.name = "active_set_runtime";
        runtimeOnly.budget = settings.proposalBudget;
        runtimeOnly.budget.minActiveClusters = 0u;
        if (!EvaluateVariantPlan(baseAsset, clusters, &state, false, runtimeOnly.budget, runtimeOnly, error) ||
            !WriteActiveSetPlanJson(runtimeDir / "active_set_runtime_plan.json", baseAsset, runtimeOnly.plan, error))
        {
            return PrintLoadFailure("Experiment suite active-set runtime plan failed", error, 345);
        }
        variants.push_back(std::move(runtimeOnly));

        ExperimentVariantSummary proposalHybrid = {};
        proposalHybrid.name = "proposal_hybrid";
        proposalHybrid.budget = settings.proposalBudget;
        proposalHybrid.budget.minActiveClusters = 0u;
        if (!EvaluateVariantPlan(baseAsset, clusters, &state, true, proposalHybrid.budget, proposalHybrid, error) ||
            !WriteActiveSetPlanJson(runtimeDir / "proposal_hybrid_plan.json", baseAsset, proposalHybrid.plan, error))
        {
            return PrintLoadFailure("Experiment suite proposal hybrid plan failed", error, 346);
        }
        variants.push_back(std::move(proposalHybrid));

        ExperimentVariantSummary fullPhysics = {};
        fullPhysics.name = "full_physics";
        fullPhysics.budget = settings.proposalBudget;
        fullPhysics.budget.minActiveClusters = 0u;
        fullPhysics.budget.maxActiveClusters = static_cast<uint32_t>(clusters.size());
        fullPhysics.budget.maxActiveGuides = std::max<uint32_t>(1u, baseAsset.guideStrandCount);
        fullPhysics.budget.maxActiveStrands = static_cast<uint32_t>(baseAsset.strands.size());
        fullPhysics.budget.promoteScoreThreshold = 0.0f;
        fullPhysics.budget.enableHardBudgetClamp = false;
        if (!EvaluateVariantPlan(baseAsset, clusters, &state, true, fullPhysics.budget, fullPhysics, error) ||
            !WriteActiveSetPlanJson(runtimeDir / "full_physics_plan.json", baseAsset, fullPhysics.plan, error))
        {
            return PrintLoadFailure("Experiment suite full physics plan failed", error, 347);
        }
        variants.push_back(std::move(fullPhysics));

        if (!WriteExperimentSuiteSummary(outputDir, editReport, variants, error))
        {
            return PrintLoadFailure("Experiment suite summary write failed", error, 348);
        }

        std::cout << "variants=" << variants.size()
                  << " locality=" << editReport.editLocalityScore
                  << " boundary=" << editReport.boundaryNaturalnessScore
                  << " latencyMs=" << editReport.editToRunLatencyMilliseconds
                  << " outputDir=" << outputDir.string() << "\n";
        return 0;
    }

