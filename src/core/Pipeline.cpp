#include "hairgpu/Pipeline.h"

#include <fstream>
#include <sstream>

#include "hairgpu/AssetIO.h"
#include "hairgpu/Binding.h"
#include "hairgpu/Cards.h"
#include "hairgpu/GuideCache.h"
#include "hairgpu/HairCache.h"
#include "hairgpu/Lod.h"
#include "hairgpu/MeshIO.h"
#include "hairgpu/Metrics.h"
#include "hairgpu/UsdExport.h"
#include "hairgpu/Validation.h"

namespace hairgpu
{
    namespace
    {
        std::string EscapeJsonString(const std::string& value)
        {
            std::string out;
            out.reserve(value.size() + 8u);
            for (char c : value)
            {
                switch (c)
                {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(c); break;
                }
            }
            return out;
        }
    }

    bool WriteHairAssetMetricsJson(const std::filesystem::path& outputPath,
                                   const HairAsset& asset,
                                   std::string& outError)
    {
        const HairAssetMetrics metrics = ComputeHairAssetMetrics(asset);
        std::ofstream out(outputPath);
        if (!out)
        {
            outError = "Failed to open metrics JSON output file.";
            return false;
        }
        out << "{\n";
        out << "  \"strandCount\": " << metrics.strandCount << ",\n";
        out << "  \"guideCount\": " << metrics.guideCount << ",\n";
        out << "  \"followerCount\": " << metrics.followerCount << ",\n";
        out << "  \"pointCount\": " << metrics.pointCount << ",\n";
        out << "  \"particlesPerStrand\": " << metrics.particlesPerStrand << ",\n";
        out << "  \"totalLength\": " << metrics.totalLength << ",\n";
        out << "  \"averageLength\": " << metrics.averageLength << ",\n";
        out << "  \"maxLength\": " << metrics.maxLength << ",\n";
        out << "  \"averageWidth\": " << metrics.averageWidth << ",\n";
        out << "  \"averageFollowerPerGuide\": " << metrics.averageFollowerPerGuide << ",\n";
        out << "  \"bounds\": {\n";
        out << "    \"min\": [" << metrics.bounds.min.x << ", " << metrics.bounds.min.y << ", " << metrics.bounds.min.z << "],\n";
        out << "    \"max\": [" << metrics.bounds.max.x << ", " << metrics.bounds.max.y << ", " << metrics.bounds.max.z << "]\n";
        out << "  }\n";
        out << "}\n";
        return true;
    }

    bool ScaffoldHairProject(const HairAsset& asset,
                             const std::filesystem::path& outputDirectory,
                             const ScaffoldProjectSettings& settings,
                             ScaffoldProjectOutputs& outOutputs,
                             std::string& outError)
    {
        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create scaffold output directory.";
            return false;
        }

        ScaffoldProjectOutputs outputs = {};
        outputs.normalizedHairAssetPath = outputDirectory / "asset_normalized.hairbin";
        outputs.validationReportPath = outputDirectory / "validation_report.txt";
        outputs.metricsJsonPath = outputDirectory / "metrics.json";
        outputs.manifestJsonPath = outputDirectory / "manifest.json";
        outputs.lodDirectory = outputDirectory;
        outputs.cardsObjPath = outputDirectory / "cards.obj";
        outputs.usdaPath = outputDirectory / "asset.usda";
        outputs.guideCachePath = outputDirectory / "guide_rest.guidecache";
        outputs.hairCachePath = outputDirectory / "rest_pose.haircache";
        outputs.scalpObjPath = outputDirectory / "scalp.obj";

        std::string error;
        if (settings.saveHairBinary && !SaveHairAssetToHairBinary(outputs.normalizedHairAssetPath, asset, error))
        {
            outError = "Failed to save normalized hair asset: " + error;
            return false;
        }

        if (settings.saveValidationReport)
        {
            const HairAssetValidationReport report = ValidateHairAsset(asset);
            std::ofstream out(outputs.validationReportPath);
            if (!out)
            {
                outError = "Failed to write validation report.";
                return false;
            }
            out << BuildHairAssetValidationSummary(report, true, kStandardParticlesPerStrand) << "\n";
        }

        if (settings.saveMetricsJson && !WriteHairAssetMetricsJson(outputs.metricsJsonPath, asset, error))
        {
            outError = "Failed to save metrics JSON: " + error;
            return false;
        }

        if (settings.saveManifestJson)
        {
            std::ofstream out(outputs.manifestJsonPath);
            if (!out)
            {
                outError = "Failed to write scaffold manifest JSON.";
                return false;
            }
            out << "{\n";
            out << "  \"normalizedHairAsset\": \"" << EscapeJsonString(outputs.normalizedHairAssetPath.filename().string()) << "\",\n";
            out << "  \"validationReport\": \"" << EscapeJsonString(outputs.validationReportPath.filename().string()) << "\",\n";
            out << "  \"metricsJson\": \"" << EscapeJsonString(outputs.metricsJsonPath.filename().string()) << "\",\n";
            out << "  \"lods\": [\"lod0.hairbin\", \"lod1.hairbin\", \"lod2.hairbin\"],\n";
            out << "  \"cardsObj\": \"" << EscapeJsonString(outputs.cardsObjPath.filename().string()) << "\",\n";
            out << "  \"usda\": \"" << EscapeJsonString(outputs.usdaPath.filename().string()) << "\",\n";
            out << "  \"guideCache\": \"" << EscapeJsonString(outputs.guideCachePath.filename().string()) << "\",\n";
            out << "  \"scalpObj\": \"" << EscapeJsonString(outputs.scalpObjPath.filename().string()) << "\"\n";
            out << "}\n";
        }

        if (settings.saveLods)
        {
            const std::vector<HairLodAsset> lods = BuildDefaultHairLodChain(asset);
            for (size_t lodIndex = 0u; lodIndex < lods.size(); ++lodIndex)
            {
                const std::filesystem::path lodPath = outputDirectory / ("lod" + std::to_string(lodIndex) + ".hairbin");
                if (!SaveHairAssetToHairBinary(lodPath, lods[lodIndex].asset, error))
                {
                    outError = "Failed to save LOD asset: " + error;
                    return false;
                }
            }
        }

        if (settings.saveCards)
        {
            HairCardBuilderSettings cardSettings = {};
            cardSettings.cardsPerGuide = 1u;
            cardSettings.segmentsPerCard = std::max<uint32_t>(4u, asset.particlesPerStrand > 1u ? asset.particlesPerStrand - 1u : 4u);
            const HairCardMesh cards = BuildHairCardMesh(asset, cardSettings);
            if (!SaveHairCardMeshToObj(outputs.cardsObjPath, cards, error))
            {
                outError = "Failed to save cards OBJ: " + error;
                return false;
            }
        }

        if (settings.saveUsda)
        {
            HairUsdExportSettings usdSettings = {};
            usdSettings.includeScalp = true;
            usdSettings.includeCards = true;
            usdSettings.cardSettings.cardsPerGuide = 1u;
            usdSettings.cardSettings.segmentsPerCard = std::max<uint32_t>(4u, asset.particlesPerStrand > 1u ? asset.particlesPerStrand - 1u : 4u);
            if (!SaveHairAssetToUsda(outputs.usdaPath, asset, usdSettings, error))
            {
                outError = "Failed to save USDA file: " + error;
                return false;
            }
        }

        if (settings.saveGuideCache)
        {
            GuideCache cache = {};
            if (!InitializeGuideCacheForAsset(asset, settings.guideCacheFrameRate, cache, error))
            {
                outError = "Failed to initialize guide cache: " + error;
                return false;
            }
            std::vector<Vec3> guidePoints;
            guidePoints.reserve(static_cast<size_t>(asset.guideStrandCount) * asset.particlesPerStrand);
            for (uint32_t guideIndex = 0u; guideIndex < asset.guideStrandCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                for (uint32_t pointIndex = 0u; pointIndex < asset.particlesPerStrand; ++pointIndex)
                {
                    guidePoints.push_back(asset.restPoints[strand.pointOffset + std::min<uint32_t>(pointIndex, strand.pointCount - 1u)]);
                }
            }
            for (uint32_t frameIndex = 0u; frameIndex < std::max<uint32_t>(1u, settings.guideCacheFrameCount); ++frameIndex)
            {
                if (!AppendGuideCacheGuidePoints(cache, guidePoints, error))
                {
                    outError = "Failed to append guide cache frame: " + error;
                    return false;
                }
            }
            if (!SaveGuideCacheToFile(outputs.guideCachePath, cache, error))
            {
                outError = "Failed to save guide cache: " + error;
                return false;
            }
        }

        if (settings.saveHairCache)
        {
            HairCache hairCache = {};
            if (!InitializeHairCacheForAsset(asset, settings.guideCacheFrameRate, hairCache, error))
            {
                outError = "Failed to initialize hair cache: " + error;
                return false;
            }
            for (uint32_t frameIndex = 0u; frameIndex < std::max<uint32_t>(1u, settings.guideCacheFrameCount); ++frameIndex)
            {
                if (!AppendHairCacheFrame(hairCache, asset, asset.restPoints, error))
                {
                    outError = "Failed to append hair cache frame: " + error;
                    return false;
                }
            }
            if (!SaveHairCacheToFile(outputs.hairCachePath, hairCache, error))
            {
                outError = "Failed to save hair cache: " + error;
                return false;
            }
        }

        if (settings.saveScalpObj && !asset.scalpMesh.positions.empty())
        {
            if (!SaveScalpMeshToObj(outputs.scalpObjPath, asset.scalpMesh, error))
            {
                outError = "Failed to save scalp OBJ: " + error;
                return false;
            }
        }

        outOutputs = std::move(outputs);
        return true;
    }
}
