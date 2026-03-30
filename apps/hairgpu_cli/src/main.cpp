#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "hairgpu/Adaptive.h"
#include "hairgpu/AssetIO.h"
#include "hairgpu/Cards.h"
#include "hairgpu/GuideCache.h"
#include "hairgpu/Lod.h"
#include "hairgpu/Metrics.h"
#include "hairgpu/UsdExport.h"
#include "hairgpu/Validation.h"

namespace
{
    using namespace hairgpu;

    std::string LowerExt(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

    void PrintUsage()
    {
        std::cout
            << "hairgpu_cli usage:\n"
            << "  hairgpu_cli info <input>\n"
            << "  hairgpu_cli validate <input> [report.txt]\n"
            << "  hairgpu_cli export-usda <input> <output.usda>\n"
            << "  hairgpu_cli export-cards <input> <output.obj>\n"
            << "  hairgpu_cli build-lods <input> <output_dir>\n"
            << "  hairgpu_cli guidecache-info <input.guidecache>\n"
            << "  hairgpu_cli guidecache-rest <input> <output.guidecache> [fps] [frames]\n";
    }
}

int main(int argc, char** argv)
{
    using namespace hairgpu;

    if (argc < 3)
    {
        PrintUsage();
        return 1;
    }

    const std::string command = argv[1];
    const std::filesystem::path inputPath = argv[2];

    if (command == "guidecache-info")
    {
        GuideCache cache;
        std::string error;
        if (!LoadGuideCacheFromFile(inputPath, cache, error))
        {
            std::cerr << "Guide cache load failed: " << error << "\n";
            return 10;
        }
        std::cout << BuildGuideCacheSummary(cache) << "\n";
        return 0;
    }

    HairAsset asset;
    std::string error;
    if (!LoadHairAssetAny(inputPath, asset, error))
    {
        std::cerr << "Load failed: " << error << "\n";
        return 2;
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
        const std::string text = BuildHairAssetValidationSummary(report, true, 32);
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
        settings.cardSettings.segmentsPerCard = std::max<uint32_t>(4u, asset.particlesPerStrand > 1u ? (asset.particlesPerStrand - 1u) : 4u);
        if (!SaveHairAssetToUsda(argv[3], asset, settings, error))
        {
            std::cerr << "USD export failed: " << error << "\n";
            return 5;
        }
        return 0;
    }

    if (command == "export-abc")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        if (!SaveHairAssetToAlembic(argv[3], asset, error))
        {
            std::cerr << "Alembic export failed: " << error << "\n";
            return 9;
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
        settings.segmentsPerCard = std::max<uint32_t>(4u, asset.particlesPerStrand > 1u ? (asset.particlesPerStrand - 1u) : 4u);
        const HairCardMesh cards = BuildHairCardMesh(asset, settings);
        if (!SaveHairCardMeshToObj(argv[3], cards, error))
        {
            std::cerr << "Card export failed: " << error << "\n";
            return 6;
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
        for (size_t i = 0; i < lods.size(); ++i)
        {
            const std::filesystem::path outPath = outDir / ("lod" + std::to_string(i) + ".hairbin");
            if (!SaveHairAssetToHairBinary(outPath, lods[i].asset, error))
            {
                std::cerr << "Failed to write " << outPath.string() << ": " << error << "\n";
                return 8;
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
        const float fps = (argc >= 5) ? std::max(1.0f, std::stof(argv[4])) : 30.0f;
        const uint32_t frameCount = (argc >= 6) ? std::max<uint32_t>(1u, static_cast<uint32_t>(std::stoul(argv[5]))) : 60u;
        GuideCache cache;
        if (!InitializeGuideCacheForAsset(asset, fps, cache, error))
        {
            std::cerr << "Guide cache init failed: " << error << "\n";
            return 11;
        }

        std::vector<Vec3> guidePoints;
        guidePoints.reserve(asset.guideStrandCount * asset.particlesPerStrand);
        for (uint32_t guide = 0; guide < asset.guideStrandCount; ++guide)
        {
            const StrandRange& strand = asset.strands[guide];
            for (uint32_t i = 0; i < asset.particlesPerStrand && i < strand.pointCount; ++i)
            {
                guidePoints.push_back(asset.restPoints[strand.pointOffset + i]);
            }
        }

        for (uint32_t frame = 0; frame < frameCount; ++frame)
        {
            if (!AppendGuideCacheGuidePoints(cache, guidePoints, error))
            {
                std::cerr << "Guide cache append failed: " << error << "\n";
                return 12;
            }
        }
        if (!SaveGuideCacheToFile(argv[3], cache, error))
        {
            std::cerr << "Guide cache save failed: " << error << "\n";
            return 13;
        }
        return 0;
    }

    PrintUsage();
    return 1;
}
