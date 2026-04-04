#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "hairgpu/AssetIO.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/Binding.h"
#include "hairgpu/BoneAnimation.h"
#include "hairgpu/Broadphase.h"
#include "hairgpu/Cards.h"
#include "hairgpu/Collision.h"
#include "hairgpu/GuideCache.h"
#include "hairgpu/HairCache.h"
#include "hairgpu/LOD.h"
#include "hairgpu/MeshIO.h"
#include "hairgpu/Metrics.h"
#include "hairgpu/Pipeline.h"
#include "hairgpu/ReducedModel.h"
#include "hairgpu/UsdExport.h"
#include "hairgpu/Validation.h"

namespace
{
    using namespace hairgpu;

    std::string ToLowerExtension(const std::filesystem::path& path)
    {
        std::string ext = path.extension().string();
        for (char& c : ext)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return ext;
    }

    bool LoadHairAssetFromAnySupportedInput(const std::filesystem::path& path, HairAsset& asset, std::string& error)
    {
        const std::string ext = ToLowerExtension(path);
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

    uint32_t ParseUIntArgument(int argc, char** argv, int index, uint32_t fallback)
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

    float ParseFloatArgument(int argc, char** argv, int index, float fallback)
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

    int ParseIntArgument(int argc, char** argv, int index, int fallback)
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

    std::vector<std::string> SplitSimpleCsvLine(const std::string& line)
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
        s.mode = static_cast<RegionSelectMode>(ParseUIntArgument(argc, argv, startIndex, static_cast<uint32_t>(s.mode)));
        s.centerUv.x = ParseFloatArgument(argc, argv, startIndex + 1, s.centerUv.x);
        s.centerUv.y = ParseFloatArgument(argc, argv, startIndex + 2, s.centerUv.y);
        s.radiusUv.x = ParseFloatArgument(argc, argv, startIndex + 3, s.radiusUv.x);
        s.radiusUv.y = ParseFloatArgument(argc, argv, startIndex + 4, s.radiusUv.y);
        s.groupId = ParseUIntArgument(argc, argv, startIndex + 5, s.groupId);
        s.guidesOnly = ParseIntArgument(argc, argv, startIndex + 6, s.guidesOnly ? 1 : 0) != 0;
        return s;
    }

    struct Phase1FrameMetric
    {
        uint32_t frameIndex = 0;
        float frameTimeMs = 0.0f;
        uint32_t activeClusterCount = 0;
        uint32_t penetrationCount = 0;
        float maxPenetrationDepthMm = 0.0f;
        float visibleHairRatio = 1.0f;
        float gpuMemoryMb = 0.0f;
        float cpuMemoryMb = 0.0f;
    };

    struct Phase1ProxySummary
    {
        uint32_t frameCount = 0;
        float targetFps = 60.0f;
        float fps60Ratio = 0.0f;
        float frameTimeP95Ms = 0.0f;
        float worstFrameTimeMs = 0.0f;
        float peakRamMb = 0.0f;
        float penetrationCountPerSec = 0.0f;
        float maxPenetrationDepthMm = 0.0f;
        float silhouetteError = 0.0f;
        float clumpPreservationScore = 1.0f;
        float editToRunLatencyMs = 0.0f;
        float averageActiveClusterCount = 0.0f;
        uint32_t maxActiveClusterCount = 0;
    };

    std::string JsonEscape(const std::string& value)
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

    std::string LowerCopy(const std::string& text)
    {
        std::string lowered = text;
        for (char& c : lowered)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return lowered;
    }

    float Percentile(std::vector<float> values, float percentile)
    {
        if (values.empty())
        {
            return 0.0f;
        }
        std::sort(values.begin(), values.end());
        const float clamped = std::max(0.0f, std::min(1.0f, percentile));
        const float index = clamped * static_cast<float>(values.size() - 1u);
        const size_t i0 = static_cast<size_t>(std::floor(index));
        const size_t i1 = std::min(i0 + 1u, values.size() - 1u);
        const float t = index - static_cast<float>(i0);
        return values[i0] * (1.0f - t) + values[i1] * t;
    }

    bool WritePhase1FrameCsv(const std::filesystem::path& outputPath,
                             const std::vector<Phase1FrameMetric>& frames,
                             std::string& outError)
    {
        std::ofstream out(outputPath);
        if (!out)
        {
            outError = "Failed to open frame CSV output file.";
            return false;
        }
        out << "frame_index,frame_time_ms,active_cluster_count,penetration_count,max_penetration_depth_mm,visible_hair_ratio,gpu_memory_mb,cpu_memory_mb\n";
        out << std::fixed << std::setprecision(4);
        for (const Phase1FrameMetric& f : frames)
        {
            out << f.frameIndex << ','
                << f.frameTimeMs << ','
                << f.activeClusterCount << ','
                << f.penetrationCount << ','
                << f.maxPenetrationDepthMm << ','
                << f.visibleHairRatio << ','
                << f.gpuMemoryMb << ','
                << f.cpuMemoryMb << '\n';
        }
        outError.clear();
        return true;
    }

    bool WritePhase1ProxySummaryJson(const std::filesystem::path& outputPath,
                                     const std::string& scenarioCode,
                                     const std::string& methodCode,
                                     const HairAssetMetrics& assetMetrics,
                                     const HairAssetValidationReport& validation,
                                     const Phase1ProxySummary& summary,
                                     std::string& outError)
    {
        std::ofstream out(outputPath);
        if (!out)
        {
            outError = "Failed to open phase1 summary JSON output file.";
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"runtimeMode\": \"proxy\",\n";
        out << "  \"scenarioCode\": \"" << JsonEscape(scenarioCode) << "\",\n";
        out << "  \"methodCode\": \"" << JsonEscape(methodCode) << "\",\n";
        out << "  \"isProxyRuntime\": true,\n";
        out << "  \"frameCount\": " << summary.frameCount << ",\n";
        out << "  \"targetFps\": " << summary.targetFps << ",\n";
        out << "  \"fps60Ratio\": " << summary.fps60Ratio << ",\n";
        out << "  \"frameTimeP95Ms\": " << summary.frameTimeP95Ms << ",\n";
        out << "  \"worstFrameTimeMs\": " << summary.worstFrameTimeMs << ",\n";
        out << "  \"peakRamMb\": " << summary.peakRamMb << ",\n";
        out << "  \"penetrationCountPerSec\": " << summary.penetrationCountPerSec << ",\n";
        out << "  \"maxPenetrationDepthMm\": " << summary.maxPenetrationDepthMm << ",\n";
        out << "  \"silhouetteError\": " << summary.silhouetteError << ",\n";
        out << "  \"clumpPreservationScore\": " << summary.clumpPreservationScore << ",\n";
        out << "  \"editToRunLatencyMs\": " << summary.editToRunLatencyMs << ",\n";
        out << "  \"averageActiveClusterCount\": " << summary.averageActiveClusterCount << ",\n";
        out << "  \"maxActiveClusterCount\": " << summary.maxActiveClusterCount << ",\n";
        out << "  \"assetMetrics\": {\n";
        out << "    \"strandCount\": " << assetMetrics.strandCount << ",\n";
        out << "    \"guideCount\": " << assetMetrics.guideCount << ",\n";
        out << "    \"followerCount\": " << assetMetrics.followerCount << ",\n";
        out << "    \"pointCount\": " << assetMetrics.pointCount << ",\n";
        out << "    \"particlesPerStrand\": " << assetMetrics.particlesPerStrand << ",\n";
        out << "    \"totalLength\": " << assetMetrics.totalLength << ",\n";
        out << "    \"averageLength\": " << assetMetrics.averageLength << ",\n";
        out << "    \"maxLength\": " << assetMetrics.maxLength << ",\n";
        out << "    \"averageWidth\": " << assetMetrics.averageWidth << ",\n";
        out << "    \"averageFollowerPerGuide\": " << assetMetrics.averageFollowerPerGuide << "\n";
        out << "  },\n";
        out << "  \"validation\": {\n";
        out << "    \"valid\": " << (validation.valid ? "true" : "false") << ",\n";
        out << "    \"infoCount\": " << validation.infoCount << ",\n";
        out << "    \"warningCount\": " << validation.warningCount << ",\n";
        out << "    \"errorCount\": " << validation.errorCount << "\n";
        out << "  }\n";
        out << "}\n";
        outError.clear();
        return true;
    }

    bool GeneratePhase1ProxyMetrics(const HairAsset& asset,
                                    const std::string& scenarioCode,
                                    const std::string& methodCode,
                                    uint32_t frameCount,
                                    float targetFps,
                                    std::vector<Phase1FrameMetric>& outFrames,
                                    Phase1ProxySummary& outSummary)
    {
        const HairAssetMetrics assetMetrics = ComputeHairAssetMetrics(asset);
        frameCount = std::max<uint32_t>(16u, frameCount);
        targetFps = std::max(1.0f, targetFps);
        const float targetFrameMs = 1000.0f / targetFps;
        const float strandsK = static_cast<float>(assetMetrics.strandCount) / 1000.0f;
        const float pointsK = static_cast<float>(assetMetrics.pointCount) / 10000.0f;
        const float complexity = 0.8f + 0.65f * strandsK + 0.35f * pointsK + assetMetrics.averageFollowerPerGuide * 0.08f;

        float methodFrameScale = 1.0f;
        float methodMemoryScale = 1.0f;
        float methodPenetrationScale = 1.0f;
        float methodSilhouetteScale = 1.0f;
        float activeClusterBias = 0.0f;
        const std::string method = LowerCopy(methodCode);
        if (method == "a")
        {
            methodFrameScale = 1.30f;
            methodMemoryScale = 1.10f;
            methodPenetrationScale = 0.60f;
            methodSilhouetteScale = 0.85f;
            activeClusterBias = 0.75f;
        }
        else if (method == "b")
        {
            methodFrameScale = 0.82f;
            methodMemoryScale = 0.78f;
            methodPenetrationScale = 1.35f;
            methodSilhouetteScale = 1.20f;
            activeClusterBias = 0.20f;
        }
        else if (method == "c")
        {
            methodFrameScale = 1.00f;
            methodMemoryScale = 0.92f;
            methodPenetrationScale = 0.78f;
            methodSilhouetteScale = 0.92f;
            activeClusterBias = 0.95f;
        }
        else if (method == "d")
        {
            methodFrameScale = 0.90f;
            methodMemoryScale = 0.88f;
            methodPenetrationScale = 0.58f;
            methodSilhouetteScale = 0.72f;
            activeClusterBias = 0.68f;
        }

        float scenarioFrameScale = 1.0f;
        float scenarioPenetrationBias = 0.0f;
        float scenarioSilhouetteBias = 0.0f;
        float scenarioEditLatencyBias = 0.0f;
        float scenarioVisibilitySwing = 0.015f;
        const std::string scenario = LowerCopy(scenarioCode);
        if (scenario == "s2")
        {
            scenarioFrameScale = 1.18f;
            scenarioPenetrationBias = 0.35f;
            scenarioSilhouetteBias = 0.04f;
            scenarioVisibilitySwing = 0.05f;
        }
        else if (scenario == "s3")
        {
            scenarioFrameScale = 1.12f;
            scenarioPenetrationBias = 0.90f;
            scenarioSilhouetteBias = 0.03f;
            scenarioVisibilitySwing = 0.035f;
        }
        else if (scenario == "s4")
        {
            scenarioFrameScale = 1.08f;
            scenarioPenetrationBias = 0.12f;
            scenarioSilhouetteBias = 0.02f;
            scenarioEditLatencyBias = 14.0f;
            scenarioVisibilitySwing = 0.025f;
        }

        const uint32_t clusterCount = std::max<uint32_t>(1u, std::min<uint32_t>(256u, assetMetrics.guideCount > 0u ? (assetMetrics.guideCount + 15u) / 16u : (assetMetrics.strandCount + 31u) / 32u));
        const float baseFrameMs = (3.0f + 2.2f * complexity) * methodFrameScale * scenarioFrameScale;
        const float baseMemoryMb = (220.0f + assetMetrics.pointCount * 0.004f + assetMetrics.strandCount * 0.015f) * methodMemoryScale;
        const float basePenetration = std::max(0.0f, (0.08f * complexity + scenarioPenetrationBias) * methodPenetrationScale);
        const float baseSilhouetteError = std::max(0.0f, (0.01f * complexity + scenarioSilhouetteBias) * methodSilhouetteScale);
        const float baseClumpScore = std::max(0.0f, std::min(1.0f, 0.96f - baseSilhouetteError * 1.8f - basePenetration * 0.03f));
        const float baseEditLatencyMs = std::max(1.0f, 7.0f + complexity * 4.0f + scenarioEditLatencyBias + (method == "d" ? 4.0f : 0.0f));

        outFrames.clear();
        outFrames.reserve(frameCount);
        std::vector<float> frameTimes;
        frameTimes.reserve(frameCount);
        uint32_t underTargetCount = 0u;
        float penetrationDepthMax = 0.0f;
        double penetrationAccum = 0.0;
        double activeClusterAccum = 0.0;
        float peakRamMb = 0.0f;

        for (uint32_t i = 0u; i < frameCount; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(std::max<uint32_t>(1u, frameCount - 1u));
            const float wave1 = std::sin(t * 6.2831853f * 2.0f);
            const float wave2 = std::cos(t * 6.2831853f * 3.0f + 0.35f);
            const float burst = (scenario == "s2" || scenario == "s3") ? std::max(0.0f, std::sin(t * 6.2831853f * 4.0f)) : std::max(0.0f, std::sin(t * 6.2831853f));
            const float jitter = 0.45f * wave1 + 0.25f * wave2 + 0.95f * burst;

            Phase1FrameMetric fm = {};
            fm.frameIndex = i;
            fm.frameTimeMs = std::max(0.2f, baseFrameMs + jitter * (0.7f + complexity * 0.18f));
            const float activeRatio = std::max(0.0f, std::min(1.0f, activeClusterBias * (0.45f + 0.35f * burst + 0.12f * std::fabs(wave1))));
            fm.activeClusterCount = static_cast<uint32_t>(std::round(activeRatio * static_cast<float>(clusterCount)));
            const float penetrationFloat = std::max(0.0f, basePenetration + (0.4f * burst + 0.1f * std::fabs(wave2)) * methodPenetrationScale);
            fm.penetrationCount = static_cast<uint32_t>(std::round(penetrationFloat));
            fm.maxPenetrationDepthMm = std::max(0.0f, (0.7f + 1.4f * penetrationFloat) * (scenario == "s3" ? 1.8f : 1.0f));
            fm.visibleHairRatio = std::max(0.75f, std::min(1.0f, 0.98f - scenarioVisibilitySwing * std::fabs(wave1)));
            fm.gpuMemoryMb = baseMemoryMb * (0.70f + activeRatio * 0.18f);
            fm.cpuMemoryMb = baseMemoryMb * (0.22f + 0.06f * std::fabs(wave2));
            outFrames.push_back(fm);

            frameTimes.push_back(fm.frameTimeMs);
            if (fm.frameTimeMs <= targetFrameMs)
            {
                ++underTargetCount;
            }
            penetrationAccum += static_cast<double>(fm.penetrationCount);
            activeClusterAccum += static_cast<double>(fm.activeClusterCount);
            penetrationDepthMax = std::max(penetrationDepthMax, fm.maxPenetrationDepthMm);
            peakRamMb = std::max(peakRamMb, fm.gpuMemoryMb + fm.cpuMemoryMb);
            outSummary.maxActiveClusterCount = std::max(outSummary.maxActiveClusterCount, fm.activeClusterCount);
        }

        outSummary.frameCount = frameCount;
        outSummary.targetFps = targetFps;
        outSummary.fps60Ratio = static_cast<float>(underTargetCount) / static_cast<float>(frameCount);
        outSummary.frameTimeP95Ms = Percentile(frameTimes, 0.95f);
        outSummary.worstFrameTimeMs = frameTimes.empty() ? 0.0f : *std::max_element(frameTimes.begin(), frameTimes.end());
        outSummary.peakRamMb = peakRamMb;
        const float durationSeconds = static_cast<float>(frameCount) / targetFps;
        outSummary.penetrationCountPerSec = durationSeconds > 0.0f ? static_cast<float>(penetrationAccum / static_cast<double>(durationSeconds)) : 0.0f;
        outSummary.maxPenetrationDepthMm = penetrationDepthMax;
        outSummary.silhouetteError = baseSilhouetteError;
        outSummary.clumpPreservationScore = baseClumpScore;
        outSummary.editToRunLatencyMs = baseEditLatencyMs;
        outSummary.averageActiveClusterCount = static_cast<float>(activeClusterAccum / static_cast<double>(frameCount));
        return true;
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
            << "  hairgpu_cli haircache-info <input.haircache>\n"
            << "  hairgpu_cli haircache-rest <input> <output.haircache> [fps] [frames]\n"
            << "  hairgpu_cli save-hairbin <input> <output.hairbin>\n"
            << "  hairgpu_cli select-region <input> [mode cx cy rx ry groupId guidesOnly]\n"
            << "  hairgpu_cli edit-region <input> <output.hairbin> [mode cx cy rx ry groupId guidesOnly] [lengthScale widthScale rootY tipY side yaw follower]\n"
            << "  hairgpu_cli constrain <input> <output.hairbin> [maxStretch maxBend floorY maxRootRadius]\n"
            << "  hairgpu_cli broadphase-stats <input> [cellSize] [mode=dense|hash|adaptive|sorted] [hashBucketCount]\n"
            << "  hairgpu_cli broadphase-export <input> <output.csv> [cellSize] [mode=dense|hash|adaptive|sorted] [hashBucketCount] [maxPairs=512]\n"
            << "  hairgpu_cli relax-angular-bend <input> <output.hairbin> [maxBendRadians] [iterations] [smoothing] [preserveLengths=1] [affectRootAdjacent=1]\n"
            << "  hairgpu_cli relax-angular-xpbd <input> <output.hairbin> [targetBendRadians] [bendCompliance] [iterations] [preserveLengths=1] [affectRootAdjacent=0] [damping]\n"
            << "  hairgpu_cli collide-scalp-mesh <input> <output.hairbin> [shellDistance] [stiffness] [iterations] [affectRoots=0] [allPoints=1]\n"
            << "  hairgpu_cli collide-scalp-sdf <input> <output.hairbin> [resolution] [shellDistance] [stiffness] [iterations] [affectRoots=0] [allPoints=1] [padding]\n"
            << "  hairgpu_cli export-scalp-sdf <input> <output.csv> [resolution] [padding]\n"
            << "  hairgpu_cli palette-region <input> <palette.csv> <output.hairbin> [mode cx cy rx ry groupId guidesOnly] [strength]\n"
            << "  hairgpu_cli export-scalp-obj <input> <output.obj>\n"
            << "  hairgpu_cli export-bone-template <input> <output.csv>\n"
            << "  hairgpu_cli export-bone-anim-template <input> <output.csv> [frames] [fps]\n"
            << "  hairgpu_cli apply-scalp-pose <input> <bones.csv> <output.hairbin> [translateEntireStrands=1]\n"
            << "  hairgpu_cli build-scalp-anim-cache <input> <bones_anim.csv> <output.haircache> [translateEntireStrands=1]\n"
            << "  hairgpu_cli sample-haircache-frame <input.haircache> <timeSeconds> <output.hairbin> [loop=1]\n"
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
        const uint32_t frameIndex = ParseUIntArgument(argc, argv, 4, 0u);
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
        const uint32_t minGuides = ParseUIntArgument(argc, argv, 4, 4u);
        const uint32_t maxGuides = ParseUIntArgument(argc, argv, 5, std::max<uint32_t>(minGuides, 24u));
        const uint32_t step = ParseUIntArgument(argc, argv, 6, 4u);
        settings.maxInfluencesPerFollower = ParseUIntArgument(argc, argv, 7, settings.maxInfluencesPerFollower);
        settings.regularization = ParseFloatArgument(argc, argv, 8, settings.regularization);
        settings.validationStride = ParseUIntArgument(argc, argv, 9, 4u);
        settings.localGuideCandidateCount = ParseUIntArgument(argc, argv, 10, settings.localGuideCandidateCount);
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
        const uint32_t minGuides = ParseUIntArgument(argc, argv, 4, 4u);
        const uint32_t maxGuides = ParseUIntArgument(argc, argv, 5, std::max<uint32_t>(minGuides, 24u));
        const uint32_t step = ParseUIntArgument(argc, argv, 6, 4u);
        settings.maxInfluencesPerFollower = ParseUIntArgument(argc, argv, 7, settings.maxInfluencesPerFollower);
        settings.regularization = ParseFloatArgument(argc, argv, 8, settings.regularization);
        settings.validationStride = ParseUIntArgument(argc, argv, 9, 4u);
        const bool compareModes = ParseIntArgument(argc, argv, 10, 1) != 0;
        settings.localGuideCandidateCount = ParseUIntArgument(argc, argv, 11, settings.localGuideCandidateCount);
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
        settings.guideCount = ParseUIntArgument(argc, argv, 4, settings.guideCount);
        settings.maxInfluencesPerFollower = ParseUIntArgument(argc, argv, 5, settings.maxInfluencesPerFollower);
        settings.regularization = ParseFloatArgument(argc, argv, 6, settings.regularization);
        settings.validationStride = ParseUIntArgument(argc, argv, 7, settings.validationStride);
        settings.useGuideDisplacements = ParseIntArgument(argc, argv, 8, settings.useGuideDisplacements ? 1 : 0) != 0;
        settings.rootLockBlend = ParseFloatArgument(argc, argv, 9, settings.rootLockBlend);
        settings.localGuideCandidateCount = ParseUIntArgument(argc, argv, 10, settings.localGuideCandidateCount);
        settings.enableScalpCollision = ParseIntArgument(argc, argv, 11, settings.enableScalpCollision ? 1 : 0) != 0;
        settings.scalpCollisionLockedPoints = ParseUIntArgument(argc, argv, 12, settings.scalpCollisionLockedPoints);
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
        if (!LoadHairAssetFromAnySupportedInput(argv[2], asset, error))
        {
            return PrintLoadFailure("Load failed", error, 53);
        }
        const uint32_t frameCount = ParseUIntArgument(argc, argv, 4, 48u);
        const float amplitude = ParseFloatArgument(argc, argv, 5, 0.035f);
        const float fps = ParseFloatArgument(argc, argv, 6, 30.0f);
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
        const float timeSeconds = ParseFloatArgument(argc, argv, 3, 0.0f);
        const bool loop = ParseIntArgument(argc, argv, 5, 1) != 0;
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
    if (!LoadHairAssetFromAnySupportedInput(inputPath, asset, error))
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

    if (command == "phase1-proxy-run")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        const std::string scenarioCode = (argc >= 6) ? argv[5] : "S1";
        const std::string methodCode = (argc >= 7) ? argv[6] : "D";
        const uint32_t frameCount = ParseUIntArgument(argc, argv, 7, 240u);
        const float targetFps = ParseFloatArgument(argc, argv, 8, 60.0f);
        const HairAssetMetrics metrics = ComputeHairAssetMetrics(asset);
        const HairAssetValidationReport report = ValidateHairAsset(asset);
        std::vector<Phase1FrameMetric> frames;
        Phase1ProxySummary summary = {};
        if (!GeneratePhase1ProxyMetrics(asset, scenarioCode, methodCode, frameCount, targetFps, frames, summary))
        {
            std::cerr << "Failed to generate phase1 proxy metrics.\n";
            return 86;
        }
        if (!WritePhase1ProxySummaryJson(argv[3], scenarioCode, methodCode, metrics, report, summary, error))
        {
            return PrintLoadFailure("Phase1 proxy summary export failed", error, 87);
        }
        if (!WritePhase1FrameCsv(argv[4], frames, error))
        {
            return PrintLoadFailure("Phase1 frame CSV export failed", error, 88);
        }
        std::cout << "phase1_proxy frameCount=" << summary.frameCount
                  << " fps60Ratio=" << summary.fps60Ratio
                  << " p95Ms=" << summary.frameTimeP95Ms
                  << " peakRamMb=" << summary.peakRamMb
                  << " maxPenDepthMm=" << summary.maxPenetrationDepthMm
                  << "\n";
        return report.valid ? 0 : 4;
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
        edit.lengthScale = ParseFloatArgument(argc, argv, base, edit.lengthScale);
        edit.widthScale = ParseFloatArgument(argc, argv, base + 1, edit.widthScale);
        edit.rootOffset.y = ParseFloatArgument(argc, argv, base + 2, edit.rootOffset.y);
        edit.tipOffset.y = ParseFloatArgument(argc, argv, base + 3, edit.tipOffset.y);
        edit.sideOffset = ParseFloatArgument(argc, argv, base + 4, edit.sideOffset);
        edit.yawRadians = ParseFloatArgument(argc, argv, base + 5, edit.yawRadians);
        edit.followerInfluence = ParseFloatArgument(argc, argv, base + 6, edit.followerInfluence);
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
        s.maxStretchRatio = ParseFloatArgument(argc, argv, 4, s.maxStretchRatio);
        s.maxBendRadians = ParseFloatArgument(argc, argv, 5, s.maxBendRadians);
        s.floorY = ParseFloatArgument(argc, argv, 6, s.floorY);
        s.maxRootRadius = ParseFloatArgument(argc, argv, 7, s.maxRootRadius);
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
        if (!LoadHairAssetFromAnySupportedInput(argv[2], asset, error))
        {
            return PrintLoadFailure("Failed to load asset", error, 2);
        }

        const float cellSize = ParseFloatArgument(argc, argv, 3, 0.085f);
        const SelfCollisionBroadphaseMode mode = (argc >= 5) ? ParseBroadphaseMode(argv[4]) : SelfCollisionBroadphaseMode::DenseGrid;
        uint32_t hashBucketCount = ParseUIntArgument(argc, argv, 5, 32768u);
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
        if (!LoadHairAssetFromAnySupportedInput(argv[2], asset, error))
        {
            return PrintLoadFailure("Failed to load asset", error, 2);
        }

        const float cellSize = ParseFloatArgument(argc, argv, 4, 0.085f);
        const SelfCollisionBroadphaseMode mode = (argc >= 6) ? ParseBroadphaseMode(argv[5]) : SelfCollisionBroadphaseMode::DenseGrid;
        uint32_t hashBucketCount = ParseUIntArgument(argc, argv, 6, 32768u);
        if (mode == SelfCollisionBroadphaseMode::DenseGrid || mode == SelfCollisionBroadphaseMode::SortedGrid)
        {
            hashBucketCount = 0u;
        }
        const uint32_t maxPairs = ParseUIntArgument(argc, argv, 7, 512u);

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
        s.maxBendRadians = ParseFloatArgument(argc, argv, 4, s.maxBendRadians);
        s.iterations = ParseUIntArgument(argc, argv, 5, s.iterations);
        s.smoothing = ParseFloatArgument(argc, argv, 6, s.smoothing);
        s.preserveSegmentLengths = ParseIntArgument(argc, argv, 7, s.preserveSegmentLengths ? 1 : 0) != 0;
        s.affectRootAdjacentPoint = ParseIntArgument(argc, argv, 8, s.affectRootAdjacentPoint ? 1 : 0) != 0;
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
        s.targetBendRadians = ParseFloatArgument(argc, argv, 4, s.targetBendRadians);
        s.bendCompliance = ParseFloatArgument(argc, argv, 5, s.bendCompliance);
        s.iterations = ParseUIntArgument(argc, argv, 6, s.iterations);
        s.preserveSegmentLengths = ParseIntArgument(argc, argv, 7, s.preserveSegmentLengths ? 1 : 0) != 0;
        s.affectRootAdjacentPoint = ParseIntArgument(argc, argv, 8, s.affectRootAdjacentPoint ? 1 : 0) != 0;
        s.damping = ParseFloatArgument(argc, argv, 9, s.damping);
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
        s.shellDistance = ParseFloatArgument(argc, argv, 4, s.shellDistance);
        s.stiffness = ParseFloatArgument(argc, argv, 5, s.stiffness);
        s.iterations = ParseUIntArgument(argc, argv, 6, s.iterations);
        s.affectRootPoints = ParseIntArgument(argc, argv, 7, s.affectRootPoints ? 1 : 0) != 0;
        s.affectAllPoints = ParseIntArgument(argc, argv, 8, s.affectAllPoints ? 1 : 0) != 0;
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
        s.sdfResolution = ParseUIntArgument(argc, argv, 4, s.sdfResolution);
        s.shellDistance = ParseFloatArgument(argc, argv, 5, s.shellDistance);
        s.stiffness = ParseFloatArgument(argc, argv, 6, s.stiffness);
        s.iterations = ParseUIntArgument(argc, argv, 7, s.iterations);
        s.affectRootPoints = ParseIntArgument(argc, argv, 8, s.affectRootPoints ? 1 : 0) != 0;
        s.affectAllPoints = ParseIntArgument(argc, argv, 9, s.affectAllPoints ? 1 : 0) != 0;
        s.sdfPadding = ParseFloatArgument(argc, argv, 10, s.sdfPadding);
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
        const uint32_t resolution = ParseUIntArgument(argc, argv, 4, 28u);
        const float padding = ParseFloatArgument(argc, argv, 5, 0.04f);
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
        const float strength = ParseFloatArgument(argc, argv, 12, 1.0f);
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
        const float fps = ParseFloatArgument(argc, argv, 4, 30.0f);
        const uint32_t frameCount = ParseUIntArgument(argc, argv, 5, 60u);
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
        const float fps = ParseFloatArgument(argc, argv, 4, 30.0f);
        const uint32_t frameCount = ParseUIntArgument(argc, argv, 5, 60u);
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
        const uint32_t frameCount = ParseUIntArgument(argc, argv, 4, 60u);
        const float fps = ParseFloatArgument(argc, argv, 5, 30.0f);
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
        const bool translateEntireStrands = ParseIntArgument(argc, argv, 5, 1) != 0;
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
        const bool translateEntireStrands = ParseIntArgument(argc, argv, 5, 1) != 0;
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
        const bool snapRoots = ParseIntArgument(argc, argv, 5, 1) != 0;
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
        settings.guideCacheFrameRate = ParseFloatArgument(argc, argv, 4, settings.guideCacheFrameRate);
        settings.guideCacheFrameCount = ParseUIntArgument(argc, argv, 5, settings.guideCacheFrameCount);
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
