// main.cpp ordered part 07: part_07
// This file is included by ../main.cpp; do not compile it directly.

            return PrintLoadFailure("Scalp import failed", error, 59);
        }
        RootBindingQualityReport report = {};
        const bool snapRoots = ArgToInt(argc, argv, 5, 1) != 0;
        if (!GenerateRootBindingsFromScalp(asset, snapRoots, &report))
        {
            return PrintLoadFailure("Root rebind failed", "GenerateRootBindingsFromScalp returned false.", 60);
        }
        if (!SaveHairAssetToHairBinary(argv[4], asset, error))
        {
            return PrintLoadFailure("Hair save after root rebind failed", error, 61);
        }
        std::cout << "avgRootDistance=" << report.averageRootDistance
                  << " maxRootDistance=" << report.maxRootDistance
                  << " invalidRoots=" << report.invalidStrandCount << "\n";
        return 0;
    }
    if (command == "scaffold-project")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ScaffoldProjectSettings settings = {};
        settings.guideCacheFrameRate = ArgToFloat(argc, argv, 4, settings.guideCacheFrameRate);
        settings.guideCacheFrameCount = ArgToUInt(argc, argv, 5, settings.guideCacheFrameCount);
        ScaffoldProjectOutputs outputs;
        if (!ScaffoldHairProject(asset, argv[3], settings, outputs, error))
        {
            return PrintLoadFailure("Scaffold generation failed", error, 62);
        }
        ExperimentSuiteSettings experimentSettings = {};
        experimentSettings.proposalBudget.targetFrameMilliseconds = 8.0f;
        experimentSettings.proposalBudget.frameBudgetMilliseconds = 16.67f;
        experimentSettings.proposalBudget.estimatedFixedCostMilliseconds = 6.0f;
        experimentSettings.proposalBudget.estimatedClusterCostMilliseconds = 0.85f;
        const std::filesystem::path experimentDir = std::filesystem::path(argv[3]) / "experiment_suite";
        if (!WriteExperimentSuiteTemplate(experimentDir, experimentSettings, error))
        {
            return PrintLoadFailure("Experiment scaffold generation failed", error, 349);
        }
        std::cout << "scaffold=" << std::filesystem::absolute(argv[3]).string() << "\n";
        return 0;
    }
    PrintUsage();
    return 1;
}
