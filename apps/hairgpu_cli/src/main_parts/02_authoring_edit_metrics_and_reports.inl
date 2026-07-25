// main.cpp ordered part 02: authoring_edit_metrics_and_reports
// This file is included by ../main.cpp; do not compile it directly.

    bool EvaluateVariantPlan(const HairAsset& asset,
                             const std::vector<ActiveSetCluster>& clusters,
                             const RuntimeEditState* state,
                             bool useEditScores,
                             const ActiveSetBudget& budget,
                             ExperimentVariantSummary& outVariant,
                             std::string& outError)
    {
        ActiveSetRuntimeInput runtimeInput = {};
        runtimeInput.fixedTimeStep = 1.0f / 60.0f;
        if (useEditScores && state)
        {
            runtimeInput.guideEditScores = state->guideInfluence;
        }
        ActiveSetScoringSettings scoringSettings = {};
        outVariant.usesEditScores = useEditScores;
        outVariant.budget = budget;
        if (EvaluateActiveSetPlan(asset, clusters, runtimeInput, scoringSettings, budget, outVariant.plan, outError))
        {
            return true;
        }
        if (outError.find("No clusters satisfied the active-set budget and threshold") != std::string::npos)
        {
            return BuildEmptyVariantPlan(asset, clusters, budget, outVariant, outError);
        }
        return false;
    }

    bool BuildEmptyVariantPlan(const HairAsset& asset,
                              const std::vector<ActiveSetCluster>& clusters,
                              const ActiveSetBudget& budget,
                              ExperimentVariantSummary& outVariant,
                              std::string& outError)
    {
        outVariant.plan = {};
        outVariant.plan.clusters = clusters;
        outVariant.plan.scores.resize(clusters.size());
        outVariant.plan.strandCorrectiveMask.assign(asset.strands.size(), 0u);
        outVariant.plan.recommendedSimulationTiers.assign(asset.strands.size(), SimTier::Cached);
        for (size_t i = 0; i < clusters.size(); ++i)
        {
            outVariant.plan.scores[i].clusterIndex = static_cast<uint32_t>(i);
        }
        outVariant.plan.summary.clusterCount = static_cast<uint32_t>(clusters.size());
        outVariant.plan.summary.selectedClusterCount = 0u;
        outVariant.plan.summary.selectedGuideCount = 0u;
        outVariant.plan.summary.selectedStrandCount = 0u;
        outVariant.plan.summary.highestScore = 0.0f;
        outVariant.plan.summary.lowestSelectedScore = 0.0f;
        outVariant.plan.summary.averageSelectedScore = 0.0f;
        outVariant.plan.summary.budgetScale = 1.0f;
        outVariant.plan.summary.pendingClusterCount = static_cast<uint32_t>(clusters.size());
        outVariant.plan.summary.hardMaxActiveClusters = budget.maxActiveClusters;
        outVariant.plan.summary.estimatedFixedCostMilliseconds = budget.estimatedFixedCostMilliseconds;
        outVariant.plan.summary.estimatedClusterCostMilliseconds = budget.estimatedClusterCostMilliseconds;
        outVariant.plan.summary.estimatedFrameMilliseconds = budget.estimatedFixedCostMilliseconds;
        outVariant.plan.summary.recommendedSolverIterations = budget.minSolverIterations;
        outVariant.plan.summary.fullTierCount = 0u;
        outVariant.plan.summary.reducedTierCount = 0u;
        outVariant.plan.summary.frozenTierCount = 0u;
        outVariant.plan.summary.cachedTierCount = static_cast<uint32_t>(asset.strands.size());
        outError.clear();
        return true;
    }

    bool WriteExperimentSuitePresetJson(const std::filesystem::path& path,
                                        const ExperimentSuiteSettings& settings,
                                        std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to write experiment preset json: " + path.string();
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"samplesPerStrand\": " << settings.samplesPerStrand << ",\n";
        out << "  \"clusterSettings\": {\n";
        out << "    \"targetGuideCountPerCluster\": " << settings.clusterSettings.targetGuideCountPerCluster << ",\n";
        out << "    \"maxGuideRootDistance\": " << settings.clusterSettings.maxGuideRootDistance << "\n";
        out << "  },\n";
        out << "  \"proposalBudget\": {\n";
        out << "    \"maxActiveClusters\": " << settings.proposalBudget.maxActiveClusters << ",\n";
        out << "    \"maxActiveGuides\": " << settings.proposalBudget.maxActiveGuides << ",\n";
        out << "    \"promoteScoreThreshold\": " << settings.proposalBudget.promoteScoreThreshold << ",\n";
        out << "    \"targetFrameMilliseconds\": " << settings.proposalBudget.targetFrameMilliseconds << ",\n";
        out << "    \"frameBudgetMilliseconds\": " << settings.proposalBudget.frameBudgetMilliseconds << ",\n";
        out << "    \"estimatedFixedCostMilliseconds\": " << settings.proposalBudget.estimatedFixedCostMilliseconds << ",\n";
        out << "    \"estimatedClusterCostMilliseconds\": " << settings.proposalBudget.estimatedClusterCostMilliseconds << "\n";
        out << "  },\n";
        out << "  \"variants\": [\n";
        out << "    \"prediction_only\",\n";
        out << "    \"active_set_runtime\",\n";
        out << "    \"proposal_hybrid\",\n";
        out << "    \"full_physics\"\n";
        out << "  ]\n";
        out << "}\n";
        outError.clear();
        return true;
    }

    bool WriteExperimentSuiteTemplate(const std::filesystem::path& outputDirectory,
                                      const ExperimentSuiteSettings& settings,
                                      std::string& outError)
    {
        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create experiment suite template directory: " + outputDirectory.string();
            return false;
        }
        if (!LocalWriteEditStackTemplateCsv(outputDirectory / "proposal_edit_stack.csv", outError))
        {
            return false;
        }
        std::ofstream strandDemo(outputDirectory / "single_strand_demo_stack.csv");
        if (!strandDemo)
        {
            outError = "Failed to write single strand demo stack: " + (outputDirectory / "single_strand_demo_stack.csv").string();
            return false;
        }
        strandDemo << "# demo stack focused on strand-level repair\n";
        strandDemo << "stack,0.42,0.10\n";
        strandDemo << "layer,base_sector,1,1.0,sector,0.50,0.50,0.50,0.50,0.00,0.0000000,3.1415926,0.20,,1,1.02,1.00,0.00,-0.01,0.00,0.00,-0.02,0.00,0.00,0.04,1.00,,,1.00\n";
        strandDemo << "layer,explicit_strand_fix,1,1.00,all,0.50,0.50,0.20,0.20,0.00,0.00,3.1415926,0.20,,0,1.00,1.00,0.00,0.00,0.00,0.01,0.02,0.00,0.00,0.15,0.00,42,replace,1.00\n";
        strandDemo.close();
        if (!WriteExperimentSuitePresetJson(outputDirectory / "experiment_suite_preset.json", settings, outError))
        {
            return false;
        }
        if (!WriteMetricDefinitionsJson(outputDirectory / "metric_definitions.json", outError))
        {
            return false;
        }
        std::ofstream readme(outputDirectory / "README_experiment_suite.txt");
        if (!readme)
        {
            outError = "Failed to write experiment suite README.";
            return false;
        }
        readme << "Experiment suite scaffold\n";
        readme << "1. Start from proposal_edit_stack.csv or single_strand_demo_stack.csv.\n";
        readme << "2. Run hairgpu_cli experiment-suite <input> <stack.csv> <output_dir>.\n";
        readme << "3. Inspect runtime_variants/*.json and experiment_summary.{csv,json}.\n";
        readme << "4. Use metric_definitions.json when writing the thesis evaluation section.\n";
        outError.clear();
        return true;
    }

    bool WriteExperimentSuiteSummary(const std::filesystem::path& outputDirectory,
                                     const HairEditComparisonReport& editReport,
                                     const std::vector<ExperimentVariantSummary>& variants,
                                     std::string& outError)
    {
        std::ofstream csv(outputDirectory / "experiment_summary.csv");
        if (!csv)
        {
            outError = "Failed to write experiment summary CSV.";
            return false;
        }
        csv << "variant,usesEditScores,selectedClusters,selectedGuides,selectedStrands,highestScore,lowestSelectedScore,averageSelectedScore,estimatedFrameMs,recommendedSolverIterations,budgetScale,maxActiveClusters,maxActiveGuides,promoteScoreThreshold\n";
        for (const ExperimentVariantSummary& variant : variants)
        {
            csv << variant.name << ","
                << (variant.usesEditScores ? 1 : 0) << ","
                << variant.plan.summary.selectedClusterCount << ","
                << variant.plan.summary.selectedGuideCount << ","
                << variant.plan.summary.selectedStrandCount << ","
                << variant.plan.summary.highestScore << ","
                << variant.plan.summary.lowestSelectedScore << ","
                << variant.plan.summary.averageSelectedScore << ","
                << variant.plan.summary.estimatedFrameMilliseconds << ","
                << variant.plan.summary.recommendedSolverIterations << ","
                << variant.plan.summary.budgetScale << ","
                << variant.budget.maxActiveClusters << ","
                << variant.budget.maxActiveGuides << ","
                << variant.budget.promoteScoreThreshold << "\n";
        }

        std::ofstream out(outputDirectory / "experiment_summary.json");
        if (!out)
        {
            outError = "Failed to write experiment summary JSON.";
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"editMetrics\": {\n";
        out << "    \"rmsPositionError\": " << editReport.shape.rmsPositionError << ",\n";
        out << "    \"silhouetteError\": " << editReport.shape.silhouetteError << ",\n";
        out << "    \"clumpPreservationScore\": " << editReport.shape.clumpPreservationScore << ",\n";
        out << "    \"rootLocalityScore\": " << editReport.shape.rootLocalityScore << ",\n";
        out << "    \"editLocalityScore\": " << editReport.editLocalityScore << ",\n";
        out << "    \"boundaryNaturalnessScore\": " << editReport.boundaryNaturalnessScore << ",\n";
        out << "    \"editedStrandRatio\": " << editReport.editedStrandRatio << ",\n";
        out << "    \"editToRunLatencyMilliseconds\": " << editReport.editToRunLatencyMilliseconds << "\n";
        out << "  },\n";
        out << "  \"variants\": [\n";
        for (size_t i = 0; i < variants.size(); ++i)
        {
            const ExperimentVariantSummary& variant = variants[i];
            out << "    {\n";
            out << "      \"name\": \"" << JsonEscape(variant.name) << "\",\n";
            out << "      \"usesEditScores\": " << (variant.usesEditScores ? "true" : "false") << ",\n";
            out << "      \"selectedClusterCount\": " << variant.plan.summary.selectedClusterCount << ",\n";
            out << "      \"selectedGuideCount\": " << variant.plan.summary.selectedGuideCount << ",\n";
            out << "      \"selectedStrandCount\": " << variant.plan.summary.selectedStrandCount << ",\n";
            out << "      \"highestScore\": " << variant.plan.summary.highestScore << ",\n";
            out << "      \"lowestSelectedScore\": " << variant.plan.summary.lowestSelectedScore << ",\n";
            out << "      \"averageSelectedScore\": " << variant.plan.summary.averageSelectedScore << ",\n";
            out << "      \"budgetScale\": " << variant.plan.summary.budgetScale << ",\n";
            out << "      \"estimatedFrameMilliseconds\": " << variant.plan.summary.estimatedFrameMilliseconds << ",\n";
            out << "      \"recommendedSolverIterations\": " << variant.plan.summary.recommendedSolverIterations << ",\n";
            out << "      \"budget\": {\n";
            out << "        \"maxActiveClusters\": " << variant.budget.maxActiveClusters << ",\n";
            out << "        \"maxActiveGuides\": " << variant.budget.maxActiveGuides << ",\n";
            out << "        \"promoteScoreThreshold\": " << variant.budget.promoteScoreThreshold << "\n";
            out << "      }\n";
            out << "    }";
            if (i + 1u < variants.size())
            {
                out << ",";
            }
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        outError.clear();
        return true;
    }

    void PrintUsage()
    {
        std::cout << R"USAGE(hairgpu_cli usage:
  hairgpu_cli help
  hairgpu_cli version
  hairgpu_cli info <input>
  hairgpu_cli validate <input> [report.txt]
  hairgpu_cli metrics-json <input> <metrics.json>
  hairgpu_cli metrics-definitions <output.json>
  hairgpu_cli active-set-plan <input> <output.json> [guidesPerCluster] [maxGuideRootDistance] [maxActiveClusters] [maxActiveGuides] [promoteThreshold]
  hairgpu_cli active-set-plan-edit <input> <stack.csv> <output.json> [guidesPerCluster] [maxGuideRootDistance] [maxActiveClusters] [maxActiveGuides] [promoteThreshold]
  hairgpu_cli active-set-dispatch <input> <output.json> [guidesPerCluster] [maxGuideRootDistance] [maxActiveClusters] [maxActiveGuides] [promoteThreshold]
  hairgpu_cli active-set-dispatch-pack <input> <output.dispatchbin> [guidesPerCluster] [maxGuideRootDistance] [maxActiveClusters] [maxActiveGuides] [promoteThreshold] [dumpJson=1]
  hairgpu_cli dispatchpack-gpubuffers <input.dispatchbin> <output_dir>
  hairgpu_cli asset-export-abc-compat <input> <output.abc>
  hairgpu_cli alembic-roundtrip-selftest <input> <temp_output.abc> [report.txt]
  hairgpu_cli export-usda <input> <output.usda>
  hairgpu_cli export-cards <input> <output.obj>
  hairgpu_cli build-lods <input> <output_dir> [levelCount=4]
  hairgpu_cli export-dcc-json <input> <output.json> [flavor=blender|maya|houdini] [includeLods=1] [repairSkinning=1]
  hairgpu_cli guidecache-info <input.guidecache>
  hairgpu_cli guidecache-rest <input> <output.guidecache> [fps] [frames]
  hairgpu_cli haircache-info <input.haircache>
  hairgpu_cli dispatchpack-info <input.dispatchbin>
  hairgpu_cli haircache-rest <input> <output.haircache> [fps] [frames]
  hairgpu_cli haircache-export-usda-sequence <input.haircache> <referenceAsset> <output_dir> [everyNth=1] [includeScalp=1] [includeCards=0]
  hairgpu_cli guidecache-export-usda-sequence <input.guidecache> <referenceAsset> <output_dir> [everyNth=1] [includeScalp=1]
  hairgpu_cli save-hairbin <input> <output.hairbin>
  hairgpu_cli retessellate-strands <input> <output.hairbin> [targetParticlesPerStrand=15] [snapRoots=1]
  hairgpu_cli segment-profile <input> <output.hairbin> [balanced|smooth|cinematic] [targetParticlesOverride=0]
  hairgpu_cli select-region <input> [mode cx cy rx ry groupId guidesOnly]
  hairgpu_cli edit-region <input> <output.hairbin> [mode cx cy rx ry groupId guidesOnly] [lengthScale widthScale rootY tipY side yaw follower clump curl parting partCenter partWidth]
  hairgpu_cli edit-strand <input> <output.hairbin> <strandSpec> [mode=replace|guide_family|union] [lengthScale widthScale rootX rootY rootZ tipX tipY tipZ side yaw follower clump curl parting partCenter partWidth]
  hairgpu_cli edit-stack-template <output.csv>
  hairgpu_cli experiment-suite-template <output_dir>
  hairgpu_cli edit-stack-apply <input> <stack.csv> <output.hairbin> [runtime.json]
  hairgpu_cli edit-stack-eval <input> <stack.csv> <output_dir> [samplesPerStrand=16]
  hairgpu_cli edit-compare <reference> <edited> <output.json> [samplesPerStrand=16]
  hairgpu_cli constrain <input> <output.hairbin> [maxStretch maxBend floorY maxRootRadius]
  hairgpu_cli broadphase-stats <input> [cellSize] [mode=dense|hash|adaptive|sorted] [hashBucketCount]
  hairgpu_cli broadphase-export <input> <output.csv> [cellSize] [mode=dense|hash|adaptive|sorted] [hashBucketCount] [maxPairs=512]
  hairgpu_cli relax-angular-bend <input> <output.hairbin> [maxBendRadians] [iterations] [smoothing] [preserveLengths=1] [affectRootAdjacent=1]
  hairgpu_cli relax-angular-xpbd <input> <output.hairbin> [targetBendRadians] [bendCompliance] [iterations] [preserveLengths=1] [affectRootAdjacent=0] [damping]
  hairgpu_cli collide-scalp-mesh <input> <output.hairbin> [shellDistance] [stiffness] [iterations] [affectRoots=0] [allPoints=1]
  hairgpu_cli collide-scalp-sdf <input> <output.hairbin> [resolution] [shellDistance] [stiffness] [iterations] [affectRoots=0] [allPoints=1] [padding]
  hairgpu_cli export-scalp-sdf <input> <output.csv> [resolution] [padding]
  hairgpu_cli export-scalp-sdf-collider <input> <output_grid.csv> [resolution] [padding]
  hairgpu_cli bake-obj-sdf <scalp.obj> <output_grid.csv> [resolution] [padding]
  hairgpu_cli palette-region <input> <palette.csv> <output.hairbin> [mode cx cy rx ry groupId guidesOnly] [strength]
  hairgpu_cli export-scalp-obj <input> <output.obj>
  hairgpu_cli export-bone-template <input> <output.csv>
  hairgpu_cli export-bone-anim-template <input> <output.csv> [frames] [fps]
  hairgpu_cli apply-scalp-pose <input> <bones.csv> <output.hairbin> [translateEntireStrands=1]
  hairgpu_cli build-scalp-anim-cache <input> <bones_anim.csv> <output.haircache> [translateEntireStrands=1]
  hairgpu_cli sample-haircache-frame <input.haircache> <timeSeconds> <output.hairbin> [loop=1]
  hairgpu_cli import-scalp-obj <input> <scalp.obj> <output.hairbin>
  hairgpu_cli rebind-roots <input> <scalp.obj> <output.hairbin> [snapRoots=1]
  hairgpu_cli scaffold-project <input> <output_dir> [guideCacheFps] [guideCacheFrames]
  hairgpu_cli synthesize-training-set <input> <output_dir> [frames] [amplitude] [fps]
  hairgpu_cli reduced-train <sequence_dir> <output.reducedbin> [guideCount] [maxInfluences] [regularization] [validationStride] [useGuideDisplacements] [rootLockBlend]
  hairgpu_cli reduced-autotune <sequence_dir> <output.reducedbin> [minGuides] [maxGuides] [step] [maxInfluences] [regularization] [validationStride]
  hairgpu_cli reduced-info <input.reducedbin>
  hairgpu_cli reduced-export-guidecache <input.reducedbin> <sequence_dir> <output.guidecache>
  hairgpu_cli reduced-reconstruct-frame <input.reducedbin> <sequence_dir> <frameIndex> <output.hairbin>
  hairgpu_cli reduced-reconstruct-sequence <input.reducedbin> <sequence_dir> <output_dir>
  hairgpu_cli reduced-eval <input.reducedbin> <sequence_dir> [report.txt]
  hairgpu_cli reduced-eval-json <input.reducedbin> <sequence_dir> <report.json>
  hairgpu_cli hybrid-teacher-dataset <input> <output_dir> [frames] [fps] [swayAmplitude] [maxActiveClusters] [maxActiveGuides] [targetFrameMs] [maxSolverIterations] [--colliders colliders.csv]
  hairgpu_cli hybrid-train-bundle <teacher_dir> <output_dir> [priorGuides] [residualGuides] [maxInfluences] [regularization] [validationStride] [smoothingAlpha] [residualGuideGain] [residualBlend] [--colliders colliders.csv]
  hairgpu_cli hybrid-bundle-info <bundle_dir>
  hairgpu_cli temporal-export-csv <input.temporalbin> <output_dir>
  hairgpu_cli temporal-import-csv <input_dir> <output.temporalbin>
  hairgpu_cli active-set-predictor-export-csv <input.predictorbin> <output_dir>
  hairgpu_cli active-set-predictor-import-csv <input_dir> <output.predictorbin>
  hairgpu_cli hybrid-bundle-export-interop <bundle_dir> <output_dir>
  hairgpu_cli hybrid-bundle-import-interop <base_bundle_dir> <interop_dir> <output_bundle_dir>
  hairgpu_cli hybrid-run-bundle <teacher_dir> <bundle_dir> <output_dir> [residualBlendOverride] [maxActiveClusters] [maxActiveGuides] [targetFrameMs] [exportPerFrameHairBins] [--colliders colliders.csv]
  hairgpu_cli hybrid-compare <teacher_dir> <bundle_dir> <output_dir> [maxActiveClusters] [maxActiveGuides] [targetFrameMs] [--colliders colliders.csv]
)USAGE";
    }
    uint32_t ClampParticlesPerStrandValue(uint32_t value)
    {
        return std::max<uint32_t>(4u, std::min<uint32_t>(value, 64u));
    }

    uint32_t ResolveSegmentProfileTarget(const HairAsset& asset, const std::string& profileName, uint32_t overrideParticles)
    {
        if (overrideParticles != 0u)
        {
            return ClampParticlesPerStrandValue(overrideParticles);
        }

        const uint32_t current = std::max<uint32_t>(asset.particlesPerStrand, 4u);
        const std::string lowered = ToLower(profileName);
        if (lowered == "smooth")
        {
            return ClampParticlesPerStrandValue(std::max<uint32_t>(current, 15u));
        }
        if (lowered == "cinematic")
        {
            return ClampParticlesPerStrandValue(std::max<uint32_t>(current, 17u));
        }
        return ClampParticlesPerStrandValue(std::max<uint32_t>(current, 13u));
    }

    int PrintLoadFailure(const std::string& context, const std::string& error, int code)
    {
        std::cerr << context << ": " << error << "\n";
        return code;
    }
}
int main(int argc, char** argv)
{
    using namespace hairgpu;
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }
    const std::string command = argv[1];
    if (command == "help")
    {
        PrintUsage();
        return 0;
    }
    if (command == "version")
    {
        std::cout << "hairgpu_cli 0.2.2\n";
        return 0;
    }
    if (command == "active-set-plan-edit")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        HairAsset asset;
        std::string error;
        if (!LoadHairAssetAny(argv[2], asset, error))
        {
            return PrintLoadFailure("Hair asset load failed", error, 166);
        }
        EditStack stack = {};
        if (!LocalLoadEditStackCsv(argv[3], stack, error))
        {
            return PrintLoadFailure("Edit stack load failed", error, 167);
        }
        RuntimeEditState state = {};
        HairAsset editedAsset = asset;
        if (!ApplyEditStackToAsset(editedAsset, asset, stack, &state, error))
        {
            return PrintLoadFailure("Edit stack apply failed", error, 168);
        }
        ActiveSetClusterSettings clusterSettings = {};
        clusterSettings.targetGuideCountPerCluster = ArgToUInt(argc, argv, 5, clusterSettings.targetGuideCountPerCluster);
        clusterSettings.maxGuideRootDistance = ArgToFloat(argc, argv, 6, clusterSettings.maxGuideRootDistance);
        const std::vector<ActiveSetCluster> clusters = BuildActiveSetClusters(asset, clusterSettings);
        if (clusters.empty())
        {
            return PrintLoadFailure("Active-set cluster build failed", "No guide clusters were built from the asset.", 169);
        }
        ActiveSetRuntimeInput runtimeInput = {};
        runtimeInput.fixedTimeStep = 1.0f / 60.0f;
        runtimeInput.guideEditScores = state.guideInfluence;
        ActiveSetScoringSettings scoringSettings = {};
        ActiveSetBudget budget = {};
        budget.maxActiveClusters = ArgToUInt(argc, argv, 7, budget.maxActiveClusters);
        budget.maxActiveGuides = ArgToUInt(argc, argv, 8, budget.maxActiveGuides);
        budget.promoteScoreThreshold = ArgToFloat(argc, argv, 9, budget.promoteScoreThreshold);
        ActiveSetPlan plan;
        if (!EvaluateActiveSetPlan(asset, clusters, runtimeInput, scoringSettings, budget, plan, error))
        {
            return PrintLoadFailure("Active-set evaluation failed", error, 170);
        }
        if (!WriteActiveSetPlanJson(argv[4], asset, plan, error))
        {
            return PrintLoadFailure("Active-set plan export failed", error, 171);
        }
        std::cout << BuildActiveSetPlanSummary(plan)
                  << " editGuides=" << state.activeGuideCount
                  << " maxEditInfluence=" << state.maxGuideInfluence
                  << "\n";
        return 0;
    }

