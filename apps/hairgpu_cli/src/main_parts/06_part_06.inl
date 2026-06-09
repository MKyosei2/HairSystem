// main.cpp ordered part 06: part_06
// This file is included by ../main.cpp; do not compile it directly.

    if (command == "constrain")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ConstraintSettings s;
        s.maxStretchRatio = ArgToFloat(argc, argv, 4, s.maxStretchRatio);
        s.maxBendRadians = ArgToFloat(argc, argv, 5, s.maxBendRadians);
        s.floorY = ArgToFloat(argc, argv, 6, s.floorY);
        s.maxRootRadius = ArgToFloat(argc, argv, 7, s.maxRootRadius);
        ApplyConstraints(asset, s);
        if (!SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Constrain save failed", error, 16);
        }
        return 0;
    }
    if (command == "broadphase-stats")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        HairAsset asset;
        std::string error;
        if (!LoadHairAssetAny(argv[2], asset, error))
        {
            return PrintLoadFailure("Failed to load asset", error, 2);
        }
        const float cellSize = ArgToFloat(argc, argv, 3, 0.085f);
        const SelfCollisionBroadphaseMode mode = (argc >= 5) ? ParseBroadphaseMode(argv[4]) : SelfCollisionBroadphaseMode::DenseGrid;
        uint32_t hashBucketCount = ArgToUInt(argc, argv, 5, 32768u);
        if (mode == SelfCollisionBroadphaseMode::DenseGrid || mode == SelfCollisionBroadphaseMode::SortedGrid)
        {
            hashBucketCount = 0u;
        }
        BroadphaseStats stats;
        if (!EvaluateBroadphaseStats(asset, cellSize, mode, hashBucketCount, stats, error))
        {
            std::cerr << "Broadphase stats failed: " << error << "\n";
            return 30;
        }
        std::cout
            << "mode=" << BroadphaseModeName(stats.mode) << "\n"
            << "cellSize=" << stats.cellSize << "\n"
            << "guideStrands=" << stats.strandCount << "\n"
            << "totalPoints=" << stats.particleCount << "\n"
            << "insertedParticles=" << stats.insertedParticles << "\n"
            << "occupiedBins=" << stats.occupiedBins << "\n"
            << "averageBinOccupancy=" << stats.averageBinOccupancy << "\n"
            << "maxBinOccupancy=" << stats.maxBinOccupancy << "\n"
            << "candidatePairs=" << stats.candidatePairs << "\n"
            << "skippedSameStrandAdjacentPairs=" << stats.skippedSameStrandAdjacentPairs << "\n"
            << "skippedSameRootTrianglePairs=" << stats.skippedSameRootTrianglePairs << "\n"
            << "duplicateCandidatePairsRemoved=" << stats.duplicateCandidatePairsRemoved << "\n"
            << "sortedCellRangeCount=" << stats.sortedCellRangeCount << "\n"
            << "activeAdaptiveLevels=" << stats.activeAdaptiveLevels << "\n"
            << "maxAdaptiveLevel=" << stats.maxAdaptiveLevel << "\n";
        if (stats.mode == SelfCollisionBroadphaseMode::SpatialHash || stats.mode == SelfCollisionBroadphaseMode::AdaptiveSpatialHash)
        {
            std::cout
                << "hashBucketCount=" << stats.hashBucketCount << "\n"
                << "usedHashBuckets=" << stats.usedHashBuckets << "\n"
                << "averageHashBucketOccupancy=" << stats.averageHashBucketOccupancy << "\n"
                << "maxHashBucketOccupancy=" << stats.maxHashBucketOccupancy << "\n";
        }
        return 0;
    }
    if (command == "broadphase-export")
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
            return PrintLoadFailure("Failed to load asset", error, 2);
        }
        const float cellSize = ArgToFloat(argc, argv, 4, 0.085f);
        const SelfCollisionBroadphaseMode mode = (argc >= 6) ? ParseBroadphaseMode(argv[5]) : SelfCollisionBroadphaseMode::DenseGrid;
        uint32_t hashBucketCount = ArgToUInt(argc, argv, 6, 32768u);
        if (mode == SelfCollisionBroadphaseMode::DenseGrid || mode == SelfCollisionBroadphaseMode::SortedGrid)
        {
            hashBucketCount = 0u;
        }
        const uint32_t maxPairs = ArgToUInt(argc, argv, 7, 512u);
        if (!ExportBroadphaseDebugCsv(asset, cellSize, mode, hashBucketCount, argv[3], maxPairs, error))
        {
            return PrintLoadFailure("Broadphase export failed", error, 31);
        }
        std::cout << "Wrote " << argv[3] << " using mode=" << BroadphaseModeName(mode) << "\n";
        return 0;
    }
    if (command == "relax-angular-bend")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        AngularBendRelaxationSettings s;
        s.maxBendRadians = ArgToFloat(argc, argv, 4, s.maxBendRadians);
        s.iterations = ArgToUInt(argc, argv, 5, s.iterations);
        s.smoothing = ArgToFloat(argc, argv, 6, s.smoothing);
        s.preserveSegmentLengths = ArgToInt(argc, argv, 7, s.preserveSegmentLengths ? 1 : 0) != 0;
        s.affectRootAdjacentPoint = ArgToInt(argc, argv, 8, s.affectRootAdjacentPoint ? 1 : 0) != 0;
        ApplyAngularBendRelaxation(asset, s);
        if (!SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Angular bend save failed", error, 160);
        }
        return 0;
    }
    if (command == "relax-angular-xpbd")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        AngularXpbdBendSettings s = {};
        s.targetBendRadians = ArgToFloat(argc, argv, 4, s.targetBendRadians);
        s.bendCompliance = ArgToFloat(argc, argv, 5, s.bendCompliance);
        s.iterations = ArgToUInt(argc, argv, 6, s.iterations);
        s.preserveSegmentLengths = ArgToInt(argc, argv, 7, s.preserveSegmentLengths ? 1 : 0) != 0;
        s.affectRootAdjacentPoint = ArgToInt(argc, argv, 8, s.affectRootAdjacentPoint ? 1 : 0) != 0;
        s.damping = ArgToFloat(argc, argv, 9, s.damping);
        ApplyAngularXpbdBendRelaxation(asset, s);
        if (!SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Angular XPBD bend save failed", error, 166);
        }
        return 0;
    }
    if (command == "collide-scalp-mesh")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairCollisionSettings s;
        s.shellDistance = ArgToFloat(argc, argv, 4, s.shellDistance);
        s.stiffness = ArgToFloat(argc, argv, 5, s.stiffness);
        s.iterations = ArgToUInt(argc, argv, 6, s.iterations);
        s.affectRootPoints = ArgToInt(argc, argv, 7, s.affectRootPoints ? 1 : 0) != 0;
        s.affectAllPoints = ArgToInt(argc, argv, 8, s.affectAllPoints ? 1 : 0) != 0;
        HairCollisionReport report = {};
        if (!ApplyScalpMeshCollision(asset, s, &report))
        {
            return PrintLoadFailure("Scalp mesh collision failed", "Asset needs strands and a scalp mesh.", 161);
        }
        if (!SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Scalp mesh collision save failed", error, 162);
        }
        std::cout << "affected=" << report.affectedPointCount
                  << " avgPenetration=" << report.averagePenetration
                  << " maxPenetration=" << report.maxPenetration << "\n";
        return 0;
    }
    if (command == "collide-scalp-sdf")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairCollisionSettings s;
        s.sdfResolution = ArgToUInt(argc, argv, 4, s.sdfResolution);
        s.shellDistance = ArgToFloat(argc, argv, 5, s.shellDistance);
        s.stiffness = ArgToFloat(argc, argv, 6, s.stiffness);
        s.iterations = ArgToUInt(argc, argv, 7, s.iterations);
        s.affectRootPoints = ArgToInt(argc, argv, 8, s.affectRootPoints ? 1 : 0) != 0;
        s.affectAllPoints = ArgToInt(argc, argv, 9, s.affectAllPoints ? 1 : 0) != 0;
        s.sdfPadding = ArgToFloat(argc, argv, 10, s.sdfPadding);
        HairCollisionReport report = {};
        if (!ApplyScalpSdfCollision(asset, s, &report))
        {
            return PrintLoadFailure("Scalp SDF collision failed", "Asset needs strands and a scalp mesh.", 163);
        }
        if (!SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Scalp SDF collision save failed", error, 164);
        }
        std::cout << "affected=" << report.affectedPointCount
                  << " avgPenetration=" << report.averagePenetration
                  << " maxPenetration=" << report.maxPenetration << "\n";
        return 0;
    }
    if (command == "export-scalp-sdf")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        const uint32_t resolution = ArgToUInt(argc, argv, 4, 28u);
        const float padding = ArgToFloat(argc, argv, 5, 0.04f);
        if (!ExportScalpSdfSlicesCsv(asset, argv[3], resolution, padding, error))
        {
            return PrintLoadFailure("Scalp SDF export failed", error, 165);
        }
        return 0;
    }
    if (command == "export-scalp-sdf-collider")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        const uint32_t resolution = ArgToUInt(argc, argv, 4, 28u);
        const float padding = ArgToFloat(argc, argv, 5, 0.04f);
        SdfGridCollider grid = {};
        if (!BakeSdfGridFromScalpMesh(asset.scalpMesh, resolution, padding, grid, error) ||
            !ExportSdfGridColliderCsv(grid, argv[3], error))
        {
            return PrintLoadFailure("Scalp SDF collider export failed", error, 166);
        }
        std::cout << "resolution=" << grid.resolutionX << "x" << grid.resolutionY << "x" << grid.resolutionZ
                  << " values=" << grid.values.size() << "\n";
        return 0;
    }
    if (command == "palette-region")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        Palette palette;
        if (!LoadPaletteCsv(argv[3], palette, error))
        {
            return PrintLoadFailure("Palette load failed", error, 17);
        }
        const RegionSelectionSettings region = ParseRegion(argc, argv, 5);
        const float strength = ArgToFloat(argc, argv, 12, 1.0f);
        const RegionSelectionResult result = SelectRegion(asset, region);
        if (!ApplyPaletteToRegion(asset, result, palette, strength, error) || !SaveHairAssetToHairBinary(argv[4], asset, error))
        {
            return PrintLoadFailure("Palette application failed", error, 18);
        }
        return 0;
    }
    if (command == "export-usda")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairUsdExportSettings settings = {};
        settings.includeScalp = true;
        settings.includeCards = true;
        settings.cardSettings.cardsPerGuide = 1u;
        settings.cardSettings.segmentsPerCard = std::max<uint32_t>(4u, asset.particlesPerStrand > 1u ? asset.particlesPerStrand - 1u : 4u);
        if (!SaveHairAssetToUsda(argv[3], asset, settings, error))
        {
            return PrintLoadFailure("USD export failed", error, 5);
        }
        return 0;
    }
    if (command == "export-cards")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairCardBuilderSettings settings = {};
        settings.cardsPerGuide = 1u;
        settings.segmentsPerCard = std::max<uint32_t>(4u, asset.particlesPerStrand > 1u ? asset.particlesPerStrand - 1u : 4u);
        const HairCardMesh cards = BuildHairCardMesh(asset, settings);
        if (!SaveHairCardMeshToObj(argv[3], cards, error))
        {
            return PrintLoadFailure("Card export failed", error, 6);
        }
        return 0;
    }
    if (command == "build-lods")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        const std::filesystem::path outDir = argv[3];
        std::error_code ec;
        std::filesystem::create_directories(outDir, ec);
        if (ec)
        {
            std::cerr << "Failed to create output directory: " << outDir.string() << "\n";
            return 7;
        }
        HairLodBuildSettings lodSettings = {};
        lodSettings.levelCount = ArgToUInt(argc, argv, 4, 4u);
        const auto lods = BuildAdaptiveHairLodChain(asset, lodSettings);
        for (size_t i = 0u; i < lods.size(); ++i)
        {
            const std::filesystem::path outPath = outDir / ("lod" + std::to_string(i) + ".hairbin");
            if (!SaveHairAssetToHairBinary(outPath, lods[i].asset, error))
            {
                return PrintLoadFailure("Failed to write LOD", error, 8);
            }
        }
        return 0;
    }
    if (command == "export-dcc-json")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairAsset exportAsset = asset;
        const bool repairSkinning = ArgToInt(argc, argv, 6, 1) != 0;
        if (repairSkinning)
        {
            RootBindingQualityReport report = {};
            RepairHairAssetSkinning(exportAsset, false, &report);
        }
        DccInteropExportSettings settings = {};
        std::string flavorText = (argc > 4) ? argv[4] : std::string("blender");
        if (!ParseDccInteropFlavor(flavorText, settings.flavor))
        {
            std::cerr << "Unknown DCC flavor: " << flavorText << "\n";
            return 8;
        }
        settings.includeLods = ArgToInt(argc, argv, 5, 1) != 0;
        settings.includeSkinning = true;
        settings.includeRootBindings = true;
        settings.lodCount = 4u;
        if (!SaveHairAssetToDccInteropJson(argv[3], exportAsset, settings, error))
        {
            return PrintLoadFailure("DCC export failed", error, 8);
        }
        return 0;
    }
    if (command == "guidecache-rest")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        const float fps = ArgToFloat(argc, argv, 4, 30.0f);
        const uint32_t frameCount = ArgToUInt(argc, argv, 5, 60u);
        GuideCache cache;
        if (!InitializeGuideCacheForAsset(asset, fps, cache, error))
        {
            return PrintLoadFailure("Guide cache init failed", error, 11);
        }
        std::vector<Vec3> guidePoints;
        guidePoints.reserve(static_cast<size_t>(asset.guideStrandCount) * asset.particlesPerStrand);
        for (uint32_t guide = 0u; guide < asset.guideStrandCount; ++guide)
        {
            const StrandRange& strand = asset.strands[guide];
            for (uint32_t i = 0u; i < asset.particlesPerStrand; ++i)
            {
                guidePoints.push_back(asset.restPoints[strand.pointOffset + std::min<uint32_t>(i, strand.pointCount - 1u)]);
            }
        }
        for (uint32_t frame = 0u; frame < frameCount; ++frame)
        {
            if (!AppendGuideCacheGuidePoints(cache, guidePoints, error))
            {
                return PrintLoadFailure("Guide cache append failed", error, 12);
            }
        }
        if (!SaveGuideCacheToFile(argv[3], cache, error))
        {
            return PrintLoadFailure("Guide cache save failed", error, 13);
        }
        return 0;
    }
    if (command == "haircache-rest")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        const float fps = ArgToFloat(argc, argv, 4, 30.0f);
        const uint32_t frameCount = ArgToUInt(argc, argv, 5, 60u);
        HairCache cache;
        if (!InitializeHairCacheForAsset(asset, fps, cache, error))
        {
            return PrintLoadFailure("Hair cache init failed", error, 67);
        }
        for (uint32_t frame = 0u; frame < frameCount; ++frame)
        {
            if (!AppendHairCacheFrame(cache, asset, asset.restPoints, error))
            {
                return PrintLoadFailure("Hair cache append failed", error, 68);
            }
        }
        if (!SaveHairCacheToFile(argv[3], cache, error))
        {
            return PrintLoadFailure("Hair cache save failed", error, 69);
        }
        return 0;
    }
    if (command == "export-scalp-obj")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        if (!SaveScalpMeshToObj(argv[3], asset.scalpMesh, error))
        {
            return PrintLoadFailure("Scalp export failed", error, 56);
        }
        return 0;
    }
    if (command == "export-bone-template")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        const uint32_t boneCount = std::max<uint32_t>(1u, asset.scalpMesh.boneCount);
        if (!WriteBoneTemplateCsv(argv[3], boneCount, error))
        {
            return PrintLoadFailure("Bone template export failed", error, 63);
        }
        return 0;
    }
    if (command == "export-bone-anim-template")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        const uint32_t boneCount = std::max<uint32_t>(1u, asset.scalpMesh.boneCount);
        const uint32_t frameCount = ArgToUInt(argc, argv, 4, 60u);
        const float fps = ArgToFloat(argc, argv, 5, 30.0f);
        if (!SaveBoneAnimationTemplateCsv(argv[3], boneCount, frameCount, fps, error))
        {
            return PrintLoadFailure("Bone animation template export failed", error, 70);
        }
        return 0;
    }
    if (command == "build-scalp-anim-cache")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        if (asset.scalpMesh.positions.empty())
        {
            return PrintLoadFailure("Animated scalp cache failed", "Asset does not contain a scalp mesh.", 71);
        }
        if (asset.rootBindings.size() < asset.strands.size())
        {
            RootBindingQualityReport report = {};
            if (!GenerateRootBindingsFromScalp(asset, false, &report))
            {
                return PrintLoadFailure("Animated scalp cache failed", "Root bindings are missing and could not be generated from the scalp mesh.", 72);
            }
        }
        BoneAnimationClip clip;
        if (!LoadBoneAnimationCsv(argv[3], clip, error))
        {
            return PrintLoadFailure("Animated scalp cache failed", error, 73);
        }
        const bool translateEntireStrands = ArgToInt(argc, argv, 5, 1) != 0;
        HairCache cache;
        if (!BuildScalpAnimatedHairCache(asset, clip, translateEntireStrands, cache, error) ||
            !SaveHairCacheToFile(argv[4], cache, error))
        {
            return PrintLoadFailure("Animated scalp cache build failed", error, 74);
        }
        std::cout << BuildHairCacheSummary(cache) << "\n";
        return 0;
    }
    if (command == "apply-scalp-pose")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        if (asset.scalpMesh.positions.empty())
        {
            return PrintLoadFailure("Scalp pose failed", "Asset does not contain a scalp mesh.", 64);
        }
        if (asset.rootBindings.size() < asset.strands.size())
        {
            RootBindingQualityReport report = {};
            if (!GenerateRootBindingsFromScalp(asset, false, &report))
            {
                return PrintLoadFailure("Scalp pose failed", "Root bindings are missing and could not be generated from the scalp mesh.", 65);
            }
        }
        std::vector<Mat4> boneMatrices;
        const uint32_t expectedBoneCount = std::max<uint32_t>(1u, asset.scalpMesh.boneCount);
        if (!LoadBoneMatricesCsv(argv[3], expectedBoneCount, boneMatrices, error))
        {
            return PrintLoadFailure("Scalp pose failed", error, 66);
        }
        const bool translateEntireStrands = ArgToInt(argc, argv, 5, 1) != 0;
        const std::vector<Vec3> skinnedPositions = ComputeSkinnedScalpPositions(asset.scalpMesh, boneMatrices);
        asset.scalpMesh.positions = skinnedPositions;
        asset.scalpMesh.boneCount = static_cast<uint32_t>(boneMatrices.size());
        RebuildRootRestPositionsFromScalpPositions(asset, skinnedPositions, translateEntireStrands);
        if (!SaveHairAssetToHairBinary(argv[4], asset, error))
        {
            return PrintLoadFailure("Scalp pose save failed", error, 67);
        }
        return 0;
    }
    if (command == "import-scalp-obj")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        ScalpMesh scalpMesh;
        if (!LoadScalpMeshFromObj(argv[3], scalpMesh, error))
        {
            return PrintLoadFailure("Scalp import failed", error, 57);
        }
        asset.scalpMesh = std::move(scalpMesh);
        if (!SaveHairAssetToHairBinary(argv[4], asset, error))
        {
            return PrintLoadFailure("Hair save after scalp import failed", error, 58);
        }
        return 0;
    }
    if (command == "rebind-roots")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        if (!LoadScalpMeshFromObj(argv[3], asset.scalpMesh, error))
        {
