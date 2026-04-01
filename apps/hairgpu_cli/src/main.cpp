#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "hairgpu/AssetIO.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/Binding.h"
#include "hairgpu/Cards.h"
#include "hairgpu/GuideCache.h"
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
        if (ext == ".csv") return LoadHairAssetFromCurvesCsv(path, asset, error);
        if (ext == ".hair") return LoadHairAssetFromHairFile(path, asset, error);
        if (ext == ".hairbin") return LoadHairAssetFromHairBinary(path, asset, error);
        if (ext == ".abc") return LoadHairAssetFromAlembic(path, asset, error);
        error = "Unsupported input extension: " + ext;
        return false;
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
        std::cout
            << "hairgpu_cli usage:\n"
            << "  hairgpu_cli help\n"
            << "  hairgpu_cli version\n"
            << "  hairgpu_cli info <input>\n"
            << "  hairgpu_cli validate <input> [report.txt]\n"
            << "  hairgpu_cli metrics-json <input> <metrics.json>\n"
            << "  hairgpu_cli export-usda <input> <output.usda>\n"
            << "  hairgpu_cli export-cards <input> <output.obj>\n"
            << "  hairgpu_cli build-lods <input> <output_dir>\n"
            << "  hairgpu_cli guidecache-info <input.guidecache>\n"
            << "  hairgpu_cli guidecache-rest <input> <output.guidecache> [fps] [frames]\n"
            << "  hairgpu_cli save-hairbin <input> <output.hairbin>\n"
            << "  hairgpu_cli select-region <input> [mode cx cy rx ry groupId guidesOnly]\n"
            << "  hairgpu_cli edit-region <input> <output.hairbin> [mode cx cy rx ry groupId guidesOnly] [lengthScale widthScale rootY tipY side yaw follower]\n"
            << "  hairgpu_cli constrain <input> <output.hairbin> [maxStretch maxBend floorY maxRootRadius]\n"
            << "  hairgpu_cli palette-region <input> <palette.csv> <output.hairbin> [mode cx cy rx ry groupId guidesOnly] [strength]\n"
            << "  hairgpu_cli export-scalp-obj <input> <output.obj>\n"
            << "  hairgpu_cli import-scalp-obj <input> <scalp.obj> <output.hairbin>\n"
            << "  hairgpu_cli rebind-roots <input> <scalp.obj> <output.hairbin> [snapRoots=1]\n"
            << "  hairgpu_cli scaffold-project <input> <output_dir> [guideCacheFps] [guideCacheFrames]\n"
            << "  hairgpu_cli synthesize-training-set <input> <output_dir> [frames] [amplitude] [fps]\n"
            << "  hairgpu_cli reduced-train <sequence_dir> <output.reducedbin> [guideCount] [maxInfluences] [regularization] [validationStride] [useGuideDisplacements] [rootLockBlend]\n"
            << "  hairgpu_cli reduced-autotune <sequence_dir> <output.reducedbin> [minGuides] [maxGuides] [step] [maxInfluences] [regularization] [validationStride]\n"
            << "  hairgpu_cli reduced-info <input.reducedbin>\n"
            << "  hairgpu_cli reduced-export-guidecache <input.reducedbin> <sequence_dir> <output.guidecache>\n"
            << "  hairgpu_cli reduced-reconstruct-frame <input.reducedbin> <sequence_dir> <frameIndex> <output.hairbin>\n"
            << "  hairgpu_cli reduced-reconstruct-sequence <input.reducedbin> <sequence_dir> <output_dir>\n"
            << "  hairgpu_cli reduced-eval <input.reducedbin> <sequence_dir> [report.txt]\n"
            << "  hairgpu_cli reduced-eval-json <input.reducedbin> <sequence_dir> <report.json>\n";
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
