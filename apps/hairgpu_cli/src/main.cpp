#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/AssetIO.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/Binding.h"
#include "hairgpu/BoneAnimation.h"
#include "hairgpu/Broadphase.h"
#include "hairgpu/Cards.h"
#include "hairgpu/Collision.h"
#include "hairgpu/GuideCache.h"
#include "hairgpu/HybridRuntime.h"
#include "hairgpu/HairCache.h"
#include "hairgpu/Lod.h"
#include "hairgpu/MeshIO.h"
#include "hairgpu/Metrics.h"
#include "hairgpu/Pipeline.h"
#include "hairgpu/ReducedModel.h"
#include "hairgpu/UsdExport.h"
#include "hairgpu/Validation.h"

namespace
{
    using namespace hairgpu;

    std::string LowerExt(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        for (char& c : ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return ext;
    }

    bool LoadHairAssetAny(const std::filesystem::path& path, HairAsset& asset, std::string& error)
    {
        const std::string ext = LowerExt(path);
        bool loaded = false;
        if (ext == ".csv") loaded = LoadHairAssetFromCurvesCsv(path, asset, error);
        else if (ext == ".usd" || ext == ".usda") loaded = LoadHairAssetFromUsda(path, asset, error);
        else if (ext == ".hair") loaded = LoadHairAssetFromHairFile(path, asset, error);
        else if (ext == ".hairbin") loaded = LoadHairAssetFromHairBinary(path, asset, error);
        else if (ext == ".abc") loaded = LoadHairAssetFromAlembic(path, asset, error);
        else
        {
            error = "Unsupported input extension: " + ext;
            return false;
        }

        if (!loaded)
        {
            return false;
        }

        ParticleChainCanonicalizationSettings canonicalSettings = {};
        canonicalSettings.targetPointCount = asset.particlesPerStrand;
        canonicalSettings.snapRootsToScalp = true;
        if (!CanonicalizeHairAssetToParticleChains(asset, canonicalSettings, error))
        {
            return false;
        }
        return true;
    }

    uint32_t ArgToUInt(int argc, char** argv, int index, uint32_t fallback)
    {
        if (index >= argc)
        {
            return fallback;
        }
        try
        {
            return static_cast<uint32_t>(std::stoul(argv[index]));
        }
        catch (...)
        {
            return fallback;
        }
    }

    float ArgToFloat(int argc, char** argv, int index, float fallback)
    {
        if (index >= argc)
        {
            return fallback;
        }
        try
        {
            return std::stof(argv[index]);
        }
        catch (...)
        {
            return fallback;
        }
    }

    int ArgToInt(int argc, char** argv, int index, int fallback)
    {
        if (index >= argc)
        {
            return fallback;
        }
        try
        {
            return std::stoi(argv[index]);
        }
        catch (...)
        {
            return fallback;
        }
    }

    std::vector<std::string> SplitSimpleCsv(const std::string& line)
    {
        std::vector<std::string> parts;
        std::string current;
        for (char c : line)
        {
            if (c == ',')
            {
                parts.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(c);
            }
        }
        parts.push_back(current);
        return parts;
    }

    bool ParseBoneMatrixLine(const std::string& line, uint32_t& outBoneIndex, hairgpu::Mat4& outMatrix, bool& outHasExplicitIndex)
    {
        std::string normalized = line;
        for (char& c : normalized)
        {
            if (c == ',')
            {
                c = ' ';
            }
        }

        std::stringstream ss(normalized);
        std::vector<float> values;
        float value = 0.0f;
        while (ss >> value)
        {
            values.push_back(value);
        }

        if (values.size() == 16u)
        {
            outHasExplicitIndex = false;
            for (size_t i = 0; i < 16u; ++i)
            {
                outMatrix.m[i] = values[i];
            }
            return true;
        }
        if (values.size() == 17u)
        {
            outHasExplicitIndex = true;
            outBoneIndex = static_cast<uint32_t>(std::max(0.0f, values[0]));
            for (size_t i = 0; i < 16u; ++i)
            {
                outMatrix.m[i] = values[i + 1u];
            }
            return true;
        }
        return false;
    }

    bool LoadBoneMatricesCsv(const std::filesystem::path& path, uint32_t expectedBoneCount, std::vector<hairgpu::Mat4>& outMatrices, std::string& outError)
    {
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open bone matrix CSV: " + path.string();
            return false;
        }

        outMatrices.assign(std::max<uint32_t>(1u, expectedBoneCount), hairgpu::Identity());
        std::string line;
        uint32_t sequentialIndex = 0u;
        bool anyLoaded = false;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            uint32_t boneIndex = sequentialIndex;
            hairgpu::Mat4 matrix = hairgpu::Identity();
            bool hasExplicitIndex = false;
            if (!ParseBoneMatrixLine(line, boneIndex, matrix, hasExplicitIndex))
            {
                continue;
            }

            if (!hasExplicitIndex)
            {
                boneIndex = sequentialIndex;
            }
            sequentialIndex = std::max(sequentialIndex + 1u, boneIndex + 1u);
            if (boneIndex >= outMatrices.size())
            {
                outMatrices.resize(boneIndex + 1u, hairgpu::Identity());
            }
            outMatrices[boneIndex] = matrix;
            anyLoaded = true;
        }

        if (!anyLoaded)
        {
            outError = "Bone matrix CSV did not contain any valid 4x4 matrices.";
            return false;
        }
        outError.clear();
        return true;
    }

    SelfCollisionBroadphaseMode ParseBroadphaseMode(const std::string& text)
    {
        std::string lowered = text;
        for (char& c : lowered)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (lowered == "hash" || lowered == "hashed" || lowered == "spatialhash" || lowered == "spatial-hash")
        {
            return SelfCollisionBroadphaseMode::SpatialHash;
        }
        if (lowered == "adaptive" || lowered == "adaptive-hash" || lowered == "multilevel" || lowered == "multi-level")
        {
            return SelfCollisionBroadphaseMode::AdaptiveSpatialHash;
        }
        if (lowered == "sorted" || lowered == "sorted-grid" || lowered == "radix" || lowered == "radix-grid")
        {
            return SelfCollisionBroadphaseMode::SortedGrid;
        }
        return SelfCollisionBroadphaseMode::DenseGrid;
    }

    const char* BroadphaseModeName(SelfCollisionBroadphaseMode mode)
    {
        switch (mode)
        {
        case SelfCollisionBroadphaseMode::SpatialHash: return "spatial-hash";
        case SelfCollisionBroadphaseMode::AdaptiveSpatialHash: return "adaptive-spatial-hash";
        case SelfCollisionBroadphaseMode::SortedGrid: return "sorted-grid";
        default: return "dense-grid";
        }
    }

    bool WriteBoneTemplateCsv(const std::filesystem::path& path, uint32_t boneCount, std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to open output CSV: " + path.string();
            return false;
        }

        out << "# boneIndex,m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,m30,m31,m32,m33\n";
        for (uint32_t boneIndex = 0u; boneIndex < std::max<uint32_t>(1u, boneCount); ++boneIndex)
        {
            const hairgpu::Mat4 identity = hairgpu::Identity();
            out << boneIndex;
            for (float m : identity.m)
            {
                out << ',' << m;
            }
            out << '\n';
        }
        outError.clear();
        return true;
    }

    RegionSelectionSettings ParseRegion(int argc, char** argv, int startIndex)
    {
        RegionSelectionSettings s;
        s.mode = static_cast<RegionSelectMode>(ArgToUInt(argc, argv, startIndex, static_cast<uint32_t>(s.mode)));
        s.centerUv.x = ArgToFloat(argc, argv, startIndex + 1, s.centerUv.x);
        s.centerUv.y = ArgToFloat(argc, argv, startIndex + 2, s.centerUv.y);
        s.radiusUv.x = ArgToFloat(argc, argv, startIndex + 3, s.radiusUv.x);
        s.radiusUv.y = ArgToFloat(argc, argv, startIndex + 4, s.radiusUv.y);
        s.groupId = ArgToUInt(argc, argv, startIndex + 5, s.groupId);
        s.guidesOnly = ArgToInt(argc, argv, startIndex + 6, s.guidesOnly ? 1 : 0) != 0;
        return s;
    }

    void PrintUsage()
    {
        std::cout << R"USAGE(hairgpu_cli usage:
  hairgpu_cli help
  hairgpu_cli version
  hairgpu_cli info <input>
  hairgpu_cli validate <input> [report.txt]
  hairgpu_cli metrics-json <input> <metrics.json>
  hairgpu_cli active-set-plan <input> <output.json> [guidesPerCluster] [maxGuideRootDistance] [maxActiveClusters] [maxActiveGuides] [promoteThreshold]
  hairgpu_cli export-usda <input> <output.usda>
  hairgpu_cli export-cards <input> <output.obj>
  hairgpu_cli build-lods <input> <output_dir>
  hairgpu_cli guidecache-info <input.guidecache>
  hairgpu_cli guidecache-rest <input> <output.guidecache> [fps] [frames]
  hairgpu_cli haircache-info <input.haircache>
  hairgpu_cli haircache-rest <input> <output.haircache> [fps] [frames]
  hairgpu_cli save-hairbin <input> <output.hairbin>
  hairgpu_cli select-region <input> [mode cx cy rx ry groupId guidesOnly]
  hairgpu_cli edit-region <input> <output.hairbin> [mode cx cy rx ry groupId guidesOnly] [lengthScale widthScale rootY tipY side yaw follower]
  hairgpu_cli constrain <input> <output.hairbin> [maxStretch maxBend floorY maxRootRadius]
  hairgpu_cli broadphase-stats <input> [cellSize] [mode=dense|hash|adaptive|sorted] [hashBucketCount]
  hairgpu_cli broadphase-export <input> <output.csv> [cellSize] [mode=dense|hash|adaptive|sorted] [hashBucketCount] [maxPairs=512]
  hairgpu_cli relax-angular-bend <input> <output.hairbin> [maxBendRadians] [iterations] [smoothing] [preserveLengths=1] [affectRootAdjacent=1]
  hairgpu_cli relax-angular-xpbd <input> <output.hairbin> [targetBendRadians] [bendCompliance] [iterations] [preserveLengths=1] [affectRootAdjacent=0] [damping]
  hairgpu_cli collide-scalp-mesh <input> <output.hairbin> [shellDistance] [stiffness] [iterations] [affectRoots=0] [allPoints=1]
  hairgpu_cli collide-scalp-sdf <input> <output.hairbin> [resolution] [shellDistance] [stiffness] [iterations] [affectRoots=0] [allPoints=1] [padding]
  hairgpu_cli export-scalp-sdf <input> <output.csv> [resolution] [padding]
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
  hairgpu_cli hybrid-teacher-dataset <input> <output_dir> [frames] [fps] [swayAmplitude] [maxActiveClusters] [maxActiveGuides] [targetFrameMs] [maxSolverIterations]
  hairgpu_cli hybrid-train-bundle <teacher_dir> <output_dir> [priorGuides] [residualGuides] [maxInfluences] [regularization] [validationStride] [smoothingAlpha] [residualGuideGain] [residualBlend]
  hairgpu_cli hybrid-bundle-info <bundle_dir>
  hairgpu_cli hybrid-run-bundle <teacher_dir> <bundle_dir> <output_dir> [residualBlendOverride] [maxActiveClusters] [maxActiveGuides] [targetFrameMs] [exportPerFrameHairBins]
)USAGE";
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
        std::cout << "hairgpu_cli 0.2.0\n";
        return 0;
    }

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
        if (!GenerateHybridTeacherDataset(asset, colliders, settings, argv[3], report, error))
        {
            return PrintLoadFailure("Hybrid teacher dataset generation failed", error, 71);
        }

        std::cout << "frames=" << report.frameCount
                  << " avgSelectedClusters=" << report.averageSelectedClusterCount
                  << " avgSelectedGuides=" << report.averageSelectedGuideCount
                  << " avgSelectedStrands=" << report.averageSelectedStrandCount
                  << " avgPenetration=" << report.averagePenetration
                  << " peakEstimatedSimulationMs=" << report.peakEstimatedSimulationMilliseconds << "\n";
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
            return PrintLoadFailure("Hybrid teacher sequence load failed", error, 72);
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

        HybridModelBundle bundle;
        HybridModelTrainingReport report;
        ColliderSet colliders = {};
        if (!TrainHybridModelBundle(teacherSequence, colliders, settings, bundle, report, error) ||
            !SaveHybridModelBundle(argv[3], bundle, &report, error))
        {
            return PrintLoadFailure("Hybrid bundle training failed", error, 73);
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
            return PrintLoadFailure("Hybrid bundle load failed", error, 74);
        }
        std::cout << BuildHybridModelBundleSummary(bundle, nullptr) << "\n";
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
            return PrintLoadFailure("Hybrid teacher sequence load failed", error, 75);
        }
        if (!LoadHybridModelBundle(argv[3], bundle, error))
        {
            return PrintLoadFailure("Hybrid bundle load failed", error, 76);
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

        FullHybridRuntimeReport report;
        ColliderSet colliders = {};
        if (!RunFullHybridRuntime(teacherSequence, colliders, bundle, settings, argv[4], report, error))
        {
            return PrintLoadFailure("Full hybrid runtime failed", error, 77);
        }
        std::cout << "frames=" << report.frameCount
                  << " avgPriorRms=" << report.averagePriorRmsError
                  << " avgHybridRmsBefore=" << report.averageHybridRmsErrorBeforeCorrection
                  << " avgHybridRmsAfter=" << report.averageHybridRmsErrorAfterCorrection
                  << " avgSelectedClusters=" << report.averageSelectedClusterCount
                  << " avgSelectedGuides=" << report.averageSelectedGuideCount
                  << " avgPenetration=" << report.averagePenetration << "\n";
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
        const RegionSelectionResult result = SelectRegion(asset, region);
        if (!ApplyEditToRegion(asset, result, edit, error) || !SaveHairAssetToHairBinary(argv[3], asset, error))
        {
            return PrintLoadFailure("Edit failed", error, 15);
        }
        return 0;
    }

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
        const auto lods = BuildDefaultHairLodChain(asset);
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
        std::cout << "scaffold=" << std::filesystem::absolute(argv[3]).string() << "\n";
        return 0;
    }

    PrintUsage();
    return 1;
}
