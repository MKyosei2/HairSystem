#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
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
#include "hairgpu/DccInterop.h"
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
    std::string Trim(std::string value)
    {
        auto notSpace = [](unsigned char c) { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
        value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
        return value;
    }
    std::string ToLower(std::string value)
    {
        for (char& c : value)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return value;
    }
    bool TryParseFloatToken(const std::string& text, float& outValue)
    {
        try
        {
            outValue = std::stof(text);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    bool TryParseUIntToken(const std::string& text, uint32_t& outValue)
    {
        try
        {
            outValue = static_cast<uint32_t>(std::stoul(text));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    bool LoadSdfGridCsv(const std::filesystem::path& path, SdfGridCollider& outGrid, std::string& outError)
    {
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open SDF grid CSV: " + path.string();
            return false;
        }
        outGrid = {};
        std::string line;
        while (std::getline(in, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::vector<std::string> tokens = SplitSimpleCsv(line);
            for (std::string& token : tokens)
            {
                token = Trim(token);
            }
            if (tokens.empty())
            {
                continue;
            }
            const std::string key = ToLower(tokens[0]);
            if ((key == "resolution" || key == "dims") && tokens.size() >= 4u)
            {
                if (!TryParseUIntToken(tokens[1], outGrid.resolutionX) ||
                    !TryParseUIntToken(tokens[2], outGrid.resolutionY) ||
                    !TryParseUIntToken(tokens[3], outGrid.resolutionZ))
                {
                    outError = "Malformed SDF resolution row in: " + path.string();
                    return false;
                }
            }
            else if ((key == "bounds_min" || key == "min") && tokens.size() >= 4u)
            {
                if (!TryParseFloatToken(tokens[1], outGrid.boundsMin.x) ||
                    !TryParseFloatToken(tokens[2], outGrid.boundsMin.y) ||
                    !TryParseFloatToken(tokens[3], outGrid.boundsMin.z))
                {
                    outError = "Malformed SDF min-bounds row in: " + path.string();
                    return false;
                }
            }
            else if ((key == "bounds_max" || key == "max") && tokens.size() >= 4u)
            {
                if (!TryParseFloatToken(tokens[1], outGrid.boundsMax.x) ||
                    !TryParseFloatToken(tokens[2], outGrid.boundsMax.y) ||
                    !TryParseFloatToken(tokens[3], outGrid.boundsMax.z))
                {
                    outError = "Malformed SDF max-bounds row in: " + path.string();
                    return false;
                }
            }
            else if (key == "data")
            {
                outGrid.values.clear();
                outGrid.values.reserve(tokens.size() > 1u ? (tokens.size() - 1u) : 0u);
                for (size_t i = 1u; i < tokens.size(); ++i)
                {
                    float value = 0.0f;
                    if (!TryParseFloatToken(tokens[i], value))
                    {
                        outError = "Malformed SDF data value in: " + path.string();
                        return false;
                    }
                    outGrid.values.push_back(value);
                }
            }
        }
        const size_t expectedCount = static_cast<size_t>(outGrid.resolutionX) * outGrid.resolutionY * outGrid.resolutionZ;
        if (outGrid.resolutionX < 2u || outGrid.resolutionY < 2u || outGrid.resolutionZ < 2u || outGrid.values.size() != expectedCount)
        {
            outError = "SDF grid CSV is incomplete. Expected resolution>=2 and exactly resolutionX*resolutionY*resolutionZ samples.";
            return false;
        }
        outError.clear();
        return true;
    }
    bool LoadColliderSetFromFile(const std::filesystem::path& path, ColliderSet& outColliders, std::string& outError)
    {
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open collider config: " + path.string();
            return false;
        }
        outColliders = {};
        std::string line;
        uint32_t lineNumber = 0u;
        while (std::getline(in, line))
        {
            ++lineNumber;
            line = Trim(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::vector<std::string> tokens = SplitSimpleCsv(line);
            for (std::string& token : tokens)
            {
                token = Trim(token);
            }
            if (tokens.empty())
            {
                continue;
            }
            const std::string type = ToLower(tokens[0]);
            auto parseFloatAt = [&](size_t index, float& outValue) -> bool
            {
                return index < tokens.size() && TryParseFloatToken(tokens[index], outValue);
            };
            if (type == "sphere" && tokens.size() >= 5u)
            {
                SphereCollider sphere = {};
                if (!parseFloatAt(1u, sphere.center.x) || !parseFloatAt(2u, sphere.center.y) || !parseFloatAt(3u, sphere.center.z) || !parseFloatAt(4u, sphere.radius))
                {
                    outError = "Malformed sphere collider row at line " + std::to_string(lineNumber);
                    return false;
                }
                outColliders.spheres.push_back(sphere);
            }
            else if (type == "capsule" && tokens.size() >= 9u)
            {
                CapsuleCollider capsule = {};
                if (!parseFloatAt(1u, capsule.pointA.x) || !parseFloatAt(2u, capsule.pointA.y) || !parseFloatAt(3u, capsule.pointA.z) || !parseFloatAt(4u, capsule.radiusA) ||
                    !parseFloatAt(5u, capsule.pointB.x) || !parseFloatAt(6u, capsule.pointB.y) || !parseFloatAt(7u, capsule.pointB.z) || !parseFloatAt(8u, capsule.radiusB))
                {
                    outError = "Malformed capsule collider row at line " + std::to_string(lineNumber);
                    return false;
                }
                outColliders.capsules.push_back(capsule);
            }
            else if (type == "plane" && tokens.size() >= 7u)
            {
                PlaneCollider plane = {};
                if (!parseFloatAt(1u, plane.point.x) || !parseFloatAt(2u, plane.point.y) || !parseFloatAt(3u, plane.point.z) ||
                    !parseFloatAt(4u, plane.normal.x) || !parseFloatAt(5u, plane.normal.y) || !parseFloatAt(6u, plane.normal.z))
                {
                    outError = "Malformed plane collider row at line " + std::to_string(lineNumber);
                    return false;
                }
                outColliders.planes.push_back(plane);
            }
            else if ((type == "aabb" || type == "box") && tokens.size() >= 7u)
            {
                AabbCollider box = {};
                if (!parseFloatAt(1u, box.minBounds.x) || !parseFloatAt(2u, box.minBounds.y) || !parseFloatAt(3u, box.minBounds.z) ||
                    !parseFloatAt(4u, box.maxBounds.x) || !parseFloatAt(5u, box.maxBounds.y) || !parseFloatAt(6u, box.maxBounds.z))
                {
                    outError = "Malformed AABB collider row at line " + std::to_string(lineNumber);
                    return false;
                }
                outColliders.aabbs.push_back(box);
            }
            else if (type == "sdf" && tokens.size() >= 2u)
            {
                SdfGridCollider grid = {};
                const std::filesystem::path gridPath = path.parent_path() / tokens[1];
                if (!LoadSdfGridCsv(gridPath, grid, outError))
                {
                    return false;
                }
                outColliders.sdfGrids.push_back(std::move(grid));
            }
            else
            {
                outError = "Unsupported collider row at line " + std::to_string(lineNumber) + ": " + line;
                return false;
            }
        }
        outError.clear();
        return true;
    }
    bool LoadOptionalColliders(int argc, char** argv, ColliderSet& outColliders, std::string& outError)
    {
        for (int i = 2; i + 1 < argc; ++i)
        {
            if (std::string(argv[i]) == "--colliders")
            {
                return LoadColliderSetFromFile(argv[i + 1], outColliders, outError);
            }
        }
        outColliders = {};
        outError.clear();
        return true;
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
    bool BuildGuideOnlyReferenceAsset(const HairAsset& referenceAsset, HairAsset& outAsset, std::string& outError)
    {
        if (referenceAsset.guideStrandCount == 0u || referenceAsset.guideStrandCount > referenceAsset.strands.size())
        {
            outError = "Reference asset does not contain a valid guide layout.";
            return false;
        }
        HairAsset result = {};
        result.guideStrandCount = referenceAsset.guideStrandCount;
        result.followerStrandCount = 0u;
        result.particlesPerStrand = referenceAsset.particlesPerStrand;
        result.scalpMesh = referenceAsset.scalpMesh;
        result.strands.reserve(referenceAsset.guideStrandCount);
        result.materials.reserve(referenceAsset.guideStrandCount);
        result.rootBindings.reserve(referenceAsset.guideStrandCount);
        result.followData.reserve(referenceAsset.guideStrandCount);
        for (uint32_t guideIndex = 0u; guideIndex < referenceAsset.guideStrandCount; ++guideIndex)
        {
            const StrandRange& srcStrand = referenceAsset.strands[guideIndex];
            StrandRange dstStrand = srcStrand;
            dstStrand.pointOffset = static_cast<uint32_t>(result.restPoints.size());
            dstStrand.flags |= static_cast<uint32_t>(StrandFlags::Guide);
            result.strands.push_back(dstStrand);
            for (uint32_t p = 0u; p < srcStrand.pointCount; ++p)
            {
                const uint32_t srcPointIndex = srcStrand.pointOffset + p;
                result.restPoints.push_back(referenceAsset.restPoints[srcPointIndex]);
                result.pointWidths.push_back(srcPointIndex < referenceAsset.pointWidths.size() ? referenceAsset.pointWidths[srcPointIndex] : 0.004f);
            }
            result.materials.push_back(guideIndex < referenceAsset.materials.size() ? referenceAsset.materials[guideIndex] : StrandMaterial{});
            result.rootBindings.push_back(guideIndex < referenceAsset.rootBindings.size() ? referenceAsset.rootBindings[guideIndex] : RootBinding{});
            FollowData follow = {};
            follow.parentGuide = -1;
            result.followData.push_back(follow);
        }
        if (result.strands.empty())
        {
            outError = "Reference asset contains no guide strands to export.";
            return false;
        }
        outAsset = std::move(result);
        outError.clear();
        return true;
    }
    bool BuildGuideFrameAsset(const GuideCache& cache,
                              const HairAsset& referenceAsset,
                              uint32_t frameIndex,
                              HairAsset& outAsset,
                              std::string& outError)
    {
        HairAsset guideAsset = {};
        if (!BuildGuideOnlyReferenceAsset(referenceAsset, guideAsset, outError))
        {
            return false;
        }
        const uint32_t pointsPerFrame = cache.GuidePointCountPerFrame();
        if (cache.FrameCount() == 0u || frameIndex >= cache.FrameCount())
        {
            outError = "Guide cache frame index is out of range.";
            return false;
        }
        if (pointsPerFrame != guideAsset.restPoints.size())
        {
            outError = "Guide cache layout does not match the reference guide asset.";
            return false;
        }
        const size_t base = static_cast<size_t>(frameIndex) * pointsPerFrame;
        guideAsset.restPoints.assign(cache.frames.begin() + static_cast<std::ptrdiff_t>(base),
                                     cache.frames.begin() + static_cast<std::ptrdiff_t>(base + pointsPerFrame));
        outAsset = std::move(guideAsset);
        outError.clear();
        return true;
    }
    bool WriteUsdSequenceManifest(const std::filesystem::path& outputDirectory,
                                  const char* sequenceName,
                                  uint32_t frameCount,
                                  float frameRate,
                                  uint32_t strandCount,
                                  bool guidesOnly,
                                  uint32_t everyNth,
                                  std::string& outError)
    {
        std::ofstream out(outputDirectory / "sequence_manifest.json");
        if (!out)
        {
            outError = "Failed to write USDA sequence manifest.";
            return false;
        }
        out << "{\n";
        out << "  \"sequence\": \"" << sequenceName << "\",\n";
        out << "  \"frameCount\": " << frameCount << ",\n";
        out << "  \"frameRate\": " << frameRate << ",\n";
        out << "  \"strandCount\": " << strandCount << ",\n";
        out << "  \"guidesOnly\": " << (guidesOnly ? "true" : "false") << ",\n";
        out << "  \"everyNth\": " << everyNth << ",\n";
        out << "  \"pattern\": \"frame_%04d.usda\"\n";
        out << "}\n";
        outError.clear();
        return true;
    }
    bool TryParseBoolToken(const std::string& text, bool& outValue)
    {
        const std::string lowered = ToLower(Trim(text));
        if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
        {
            outValue = true;
            return true;
        }
        if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
        {
            outValue = false;
            return true;
        }
        return false;
    }

    std::string JsonEscape(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size() + 8u);
        for (char c : value)
        {
            switch (c)
            {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped.push_back(c); break;
            }
        }
        return escaped;
    }

    bool ParseRegionModeToken(const std::string& text, RegionSelectMode& outMode)
    {
        const std::string lowered = ToLower(Trim(text));
        if (lowered.empty() || lowered == "all")
        {
            outMode = RegionSelectMode::All;
            return true;
        }
        if (lowered == "ellipse" || lowered == "circle")
        {
            outMode = RegionSelectMode::Ellipse;
            return true;
        }
        if (lowered == "rectangle" || lowered == "rect" || lowered == "box")
        {
            outMode = RegionSelectMode::Rectangle;
            return true;
        }
        if (lowered == "sector" || lowered == "fan")
        {
            outMode = RegionSelectMode::Sector;
            return true;
        }
        return false;
    }

    const char* RegionModeToString(RegionSelectMode mode)
    {
        switch (mode)
        {
        case RegionSelectMode::Ellipse: return "ellipse";
        case RegionSelectMode::Rectangle: return "rectangle";
        case RegionSelectMode::Sector: return "sector";
        default: return "all";
        }
    }

    bool ParseStrandSelectionModeToken(const std::string& text, StrandSelectionMode& outMode)
    {
        const std::string lowered = ToLower(Trim(text));
        if (lowered.empty() || lowered == "none")
        {
            outMode = StrandSelectionMode::None;
            return true;
        }
        if (lowered == "union")
        {
            outMode = StrandSelectionMode::Union;
            return true;
        }
        if (lowered == "replace" || lowered == "exact")
        {
            outMode = StrandSelectionMode::Replace;
            return true;
        }
        if (lowered == "guide_family" || lowered == "guidefamily" || lowered == "family")
        {
            outMode = StrandSelectionMode::GuideFamily;
            return true;
        }
        return false;
    }

    const char* StrandSelectionModeToString(StrandSelectionMode mode)
    {
        switch (mode)
        {
        case StrandSelectionMode::Union: return "union";
        case StrandSelectionMode::Replace: return "replace";
        case StrandSelectionMode::GuideFamily: return "guide_family";
        default: return "none";
        }
    }

    bool LocalLoadEditStackCsv(const std::filesystem::path& path, EditStack& outStack, std::string& outError)
    {
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open edit stack CSV: " + path.string();
            return false;
        }

        outStack = {};
        std::string line;
        uint32_t lineNumber = 0u;
        while (std::getline(in, line))
        {
            ++lineNumber;
            line = Trim(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::vector<std::string> tokens = SplitSimpleCsv(line);
            for (std::string& token : tokens)
            {
                token = Trim(token);
            }
            if (tokens.empty())
            {
                continue;
            }

            const std::string kind = ToLower(tokens[0]);
            if (kind == "stack")
            {
                if (tokens.size() >= 2u && !tokens[1].empty() && !TryParseFloatToken(tokens[1], outStack.boundarySmoothing))
                {
                    outError = "Malformed boundary smoothing at line " + std::to_string(lineNumber);
                    return false;
                }
                if (tokens.size() >= 3u && !tokens[2].empty() && !TryParseFloatToken(tokens[2], outStack.boundaryRadiusUv))
                {
                    outError = "Malformed boundary radius at line " + std::to_string(lineNumber);
                    return false;
                }
                continue;
            }

            if (kind != "layer")
            {
                outError = "Unsupported edit stack row at line " + std::to_string(lineNumber) + ": " + line;
                return false;
            }

            EditLayer layer = {};
            layer.name = (tokens.size() >= 2u && !tokens[1].empty()) ? tokens[1] : ("layer_" + std::to_string(outStack.layers.size()));
            bool enabled = true;
            if (tokens.size() >= 3u && !tokens[2].empty() && !TryParseBoolToken(tokens[2], enabled))
            {
                outError = "Malformed enabled flag at line " + std::to_string(lineNumber);
                return false;
            }
            layer.enabled = enabled;
            if (tokens.size() >= 4u && !tokens[3].empty() && !TryParseFloatToken(tokens[3], layer.layerWeight))
            {
                outError = "Malformed layer weight at line " + std::to_string(lineNumber);
                return false;
            }
            if (tokens.size() >= 5u && !tokens[4].empty() && !ParseRegionModeToken(tokens[4], layer.region.mode))
            {
                outError = "Malformed region mode at line " + std::to_string(lineNumber);
                return false;
            }

            auto parseFloatOptional = [&](size_t index, float& value, const char* label) -> bool
            {
                if (index >= tokens.size() || tokens[index].empty())
                {
                    return true;
                }
                if (!TryParseFloatToken(tokens[index], value))
                {
                    outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                    return false;
                }
                return true;
            };
            auto parseUIntOptional = [&](size_t index, uint32_t& value, const char* label) -> bool
            {
                if (index >= tokens.size() || tokens[index].empty())
                {
                    return true;
                }
                if (!TryParseUIntToken(tokens[index], value))
                {
                    outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                    return false;
                }
                return true;
            };
            auto parseBoolOptional = [&](size_t index, bool& value, const char* label) -> bool
            {
                if (index >= tokens.size() || tokens[index].empty())
                {
                    return true;
                }
                if (!TryParseBoolToken(tokens[index], value))
                {
                    outError = std::string("Malformed ") + label + " at line " + std::to_string(lineNumber);
                    return false;
                }
                return true;
            };

            uint32_t groupId = UINT32_MAX;
            if (!parseFloatOptional(5u, layer.region.centerUv.x, "center u") ||
                !parseFloatOptional(6u, layer.region.centerUv.y, "center v") ||
                !parseFloatOptional(7u, layer.region.radiusUv.x, "radius x") ||
                !parseFloatOptional(8u, layer.region.radiusUv.y, "radius y") ||
                !parseFloatOptional(9u, layer.region.innerRadius, "inner radius") ||
                !parseFloatOptional(10u, layer.region.angleCenterRadians, "angle center") ||
                !parseFloatOptional(11u, layer.region.angleHalfExtentRadians, "angle half extent") ||
                !parseFloatOptional(12u, layer.region.feather, "feather") ||
                !parseUIntOptional(13u, groupId, "groupId") ||
                !parseBoolOptional(14u, layer.region.guidesOnly, "guidesOnly") ||
                !parseFloatOptional(15u, layer.edit.lengthScale, "lengthScale") ||
                !parseFloatOptional(16u, layer.edit.widthScale, "widthScale") ||
                !parseFloatOptional(17u, layer.edit.rootOffset.x, "rootOffsetX") ||
                !parseFloatOptional(18u, layer.edit.rootOffset.y, "rootOffsetY") ||
                !parseFloatOptional(19u, layer.edit.rootOffset.z, "rootOffsetZ") ||
                !parseFloatOptional(20u, layer.edit.tipOffset.x, "tipOffsetX") ||
                !parseFloatOptional(21u, layer.edit.tipOffset.y, "tipOffsetY") ||
                !parseFloatOptional(22u, layer.edit.tipOffset.z, "tipOffsetZ") ||
                !parseFloatOptional(23u, layer.edit.sideOffset, "sideOffset") ||
                !parseFloatOptional(24u, layer.edit.yawRadians, "yawRadians") ||
                !parseFloatOptional(25u, layer.edit.followerInfluence, "followerInfluence") ||
                !parseFloatOptional(26u, layer.edit.clumpStrength, "clumpStrength") ||
                !parseFloatOptional(27u, layer.edit.curlStrength, "curlStrength") ||
                !parseFloatOptional(28u, layer.edit.partingStrength, "partingStrength") ||
                !parseFloatOptional(29u, layer.edit.partingCenterU, "partingCenterU") ||
                !parseFloatOptional(30u, layer.edit.partingWidthUv, "partingWidthUv"))
            {
                return false;
            }
            layer.region.groupId = groupId;
            const bool hasExtendedEditColumns = tokens.size() >= 34u;
            const size_t strandSelectionColumn = hasExtendedEditColumns ? 31u : 26u;
            const size_t strandSelectionModeColumn = hasExtendedEditColumns ? 32u : 27u;
            const size_t strandSelectionWeightColumn = hasExtendedEditColumns ? 33u : 28u;
            if (tokens.size() > strandSelectionColumn && !tokens[strandSelectionColumn].empty())
            {
                if (!ParseStrandSelectionSpec(tokens[strandSelectionColumn], layer.strandSelection, outError))
                {
                    outError += " at line " + std::to_string(lineNumber);
                    return false;
                }
            }
            if (tokens.size() > strandSelectionModeColumn && !tokens[strandSelectionModeColumn].empty() && !ParseStrandSelectionModeToken(tokens[strandSelectionModeColumn], layer.strandSelectionMode))
            {
                outError = "Malformed strand selection mode at line " + std::to_string(lineNumber);
                return false;
            }
            if (tokens.size() > strandSelectionWeightColumn && !tokens[strandSelectionWeightColumn].empty() && !TryParseFloatToken(tokens[strandSelectionWeightColumn], layer.strandSelectionWeight))
            {
                outError = "Malformed strand selection weight at line " + std::to_string(lineNumber);
                return false;
            }
            if (!layer.strandSelection.empty() && layer.strandSelectionMode == StrandSelectionMode::None)
            {
                layer.strandSelectionMode = StrandSelectionMode::Union;
            }
            outStack.layers.push_back(layer);
        }

        outError.clear();
        return true;
    }

    bool LocalWriteEditStackTemplateCsv(const std::filesystem::path& path, std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to open edit stack template for writing: " + path.string();
            return false;
        }
        out << "# edit stack csv\n";
        out << "# stack,boundarySmoothing,boundaryRadiusUv\n";
        out << "stack,0.42,0.10\n";
        out << "# layer,name,enabled,layerWeight,mode,cx,cy,rx,ry,innerRadius,angleCenterRadians,angleHalfExtentRadians,feather,groupId,guidesOnly,lengthScale,widthScale,rootOffsetX,rootOffsetY,rootOffsetZ,tipOffsetX,tipOffsetY,tipOffsetZ,sideOffset,yawRadians,followerInfluence,clumpStrength,curlStrength,partingStrength,partingCenterU,partingWidthUv,strandSelection,strandSelectionMode,strandSelectionWeight\n";
        out << "layer,front_sector,1,1.0,sector,0.50,0.50,0.50,0.50,0.00,0.0000000,3.1415926,0.20,,1,1.05,1.00,0.00,-0.01,0.00,0.00,-0.03,0.00,0.00,0.05,1.00,0.18,0.00,0.012,0.50,0.14,,,1.00\n";
        out << "layer,tip_refine,1,0.85,ellipse,0.50,0.50,0.16,0.16,0.00,0.00,3.1415926,0.35,,1,0.97,1.00,0.00,0.00,0.00,0.00,0.02,0.00,0.01,-0.08,0.85,0.10,0.018,0.000,0.50,0.14,,,1.00\n";
        out << "layer,single_strand_fix,1,1.00,all,0.50,0.50,0.20,0.20,0.00,0.00,3.1415926,0.20,,0,1.00,1.00,0.00,0.00,0.00,0.00,0.01,0.00,0.00,0.10,0.00,0.00,0.00,0.000,0.50,0.14,42,replace,1.00\n";
        outError.clear();
        return true;
    }

    bool WriteEditRuntimeStateJson(const std::filesystem::path& path,
                                   const EditStack& stack,
                                   const RuntimeEditState& state,
                                   const HairAsset& asset,
                                   std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to write runtime edit state json: " + path.string();
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"layerCount\": " << stack.layers.size() << ",\n";
        out << "  \"boundarySmoothing\": " << stack.boundarySmoothing << ",\n";
        out << "  \"boundaryRadiusUv\": " << stack.boundaryRadiusUv << ",\n";
        out << "  \"activeGuideCount\": " << state.activeGuideCount << ",\n";
        out << "  \"guideCount\": " << asset.guideStrandCount << ",\n";
        out << "  \"strandCount\": " << asset.strands.size() << ",\n";
        out << "  \"maxGuideInfluence\": " << state.maxGuideInfluence << ",\n";
        out << "  \"activeGuideRatio\": " << (asset.guideStrandCount > 0u ? (static_cast<double>(state.activeGuideCount) / static_cast<double>(asset.guideStrandCount)) : 0.0) << ",\n";
        out << "  \"layers\": [\n";
        for (size_t i = 0; i < stack.layers.size(); ++i)
        {
            const EditLayer& layer = stack.layers[i];
            out << "    {\n";
            out << "      \"name\": \"" << JsonEscape(layer.name) << "\",\n";
            out << "      \"enabled\": " << (layer.enabled ? "true" : "false") << ",\n";
            out << "      \"layerWeight\": " << layer.layerWeight << ",\n";
            out << "      \"mode\": \"" << RegionModeToString(layer.region.mode) << "\",\n";
            out << "      \"guidesOnly\": " << (layer.region.guidesOnly ? "true" : "false") << ",\n";
            out << "      \"groupId\": ";
            if (layer.region.groupId == UINT32_MAX)
            {
                out << "null";
            }
            else
            {
                out << layer.region.groupId;
            }
            out << "\n    }";
            if (i + 1u < stack.layers.size())
            {
                out << ",";
            }
            out << "\n";
        }
        out << "  ],\n";
        out << "  \"guideInfluence\": [";
        for (size_t i = 0; i < state.guideInfluence.size(); ++i)
        {
            if (i != 0u)
            {
                out << ", ";
            }
            out << state.guideInfluence[i];
        }
        out << "],\n";
        out << "  \"strandInfluence\": [";
        for (size_t i = 0; i < state.strandInfluence.size(); ++i)
        {
            if (i != 0u)
            {
                out << ", ";
            }
            out << state.strandInfluence[i];
        }
        out << "]\n}";
        out << "\n";
        outError.clear();
        return true;
    }

    bool WriteEditComparisonJson(const std::filesystem::path& path,
                                 const HairEditComparisonReport& report,
                                 std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to write edit comparison json: " + path.string();
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"rmsPositionError\": " << report.shape.rmsPositionError << ",\n";
        out << "  \"silhouetteError\": " << report.shape.silhouetteError << ",\n";
        out << "  \"clumpPreservationScore\": " << report.shape.clumpPreservationScore << ",\n";
        out << "  \"rootLocalityScore\": " << report.shape.rootLocalityScore << ",\n";
        out << "  \"editLocalityScore\": " << report.editLocalityScore << ",\n";
        out << "  \"boundaryNaturalnessScore\": " << report.boundaryNaturalnessScore << ",\n";
        out << "  \"editedStrandRatio\": " << report.editedStrandRatio << ",\n";
        out << "  \"editToRunLatencyMilliseconds\": " << report.editToRunLatencyMilliseconds << "\n";
        out << "}\n";
        outError.clear();
        return true;
    }


    struct ExperimentSuiteSettings
    {
        uint32_t samplesPerStrand = 16u;
        ActiveSetClusterSettings clusterSettings = {};
        ActiveSetBudget proposalBudget = {};
    };

    struct ExperimentVariantSummary
    {
        std::string name;
        bool usesEditScores = false;
        ActiveSetBudget budget = {};
        ActiveSetPlan plan = {};
    };

    bool WriteMetricDefinitionsJson(const std::filesystem::path& path, std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to write metric definitions json: " + path.string();
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"editMetrics\": [\n";
        out << "    {\n";
        out << "      \"name\": \"rmsPositionError\",\n";
        out << "      \"category\": \"shape\",\n";
        out << "      \"formula\": \"sqrt(mean(||p_ref(t)-p_edit(t)||^2)) over all sampled strand points\",\n";
        out << "      \"interpretation\": \"Lower is better; measures average spatial deviation after editing.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"silhouetteError\",\n";
        out << "      \"category\": \"shape\",\n";
        out << "      \"formula\": \"sqrt(mean((x_ref-x_edit)^2 + (z_ref-z_edit)^2)) over sampled points\",\n";
        out << "      \"interpretation\": \"Lower is better; approximates screen-space outer contour change using the XZ projection.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"clumpPreservationScore\",\n";
        out << "      \"category\": \"shape\",\n";
        out << "      \"formula\": \"clamp(1 - averageLengthDelta / (0.35 * averageReferenceLength + eps), 0, 1)\",\n";
        out << "      \"interpretation\": \"Higher is better; rewards edits that preserve strand-group length structure.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"rootLocalityScore\",\n";
        out << "      \"category\": \"shape\",\n";
        out << "      \"formula\": \"clamp(1 - averageRootDisplacement / (0.10 * assetBoundsDiagonal + eps), 0, 1)\",\n";
        out << "      \"interpretation\": \"Higher is better; indicates how stable root positions stay after editing.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"editLocalityScore\",\n";
        out << "      \"category\": \"editing\",\n";
        out << "      \"formula\": \"1 - editedStrandRatio, where a strand is edited when RMS strand delta > 0.01 * averageReferenceLength\",\n";
        out << "      \"interpretation\": \"Higher is better; estimates how much of the hairstyle remained untouched outside the intended region.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"boundaryNaturalnessScore\",\n";
        out << "      \"category\": \"editing\",\n";
        out << "      \"formula\": \"clamp(1 - averageNeighborDisplacementJump / (0.08 * averageReferenceLength + eps), 0, 1)\",\n";
        out << "      \"interpretation\": \"Higher is better; compares nearby guide strands across UV-space boundaries to penalize visible discontinuities.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"editedStrandRatio\",\n";
        out << "      \"category\": \"editing\",\n";
        out << "      \"formula\": \"changedStrandCount / min(referenceStrands, editedStrands)\",\n";
        out << "      \"interpretation\": \"Lower is better when locality is important; reports the fraction of strands whose sampled deformation exceeds the edit threshold.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"editToRunLatencyMilliseconds\",\n";
        out << "      \"category\": \"timing\",\n";
        out << "      \"formula\": \"wall-clock milliseconds spent applying the edit stack and preparing runtime state\",\n";
        out << "      \"interpretation\": \"Lower is better; approximates interactive response time from edit commit to runtime-ready output.\"\n";
        out << "    }\n";
        out << "  ],\n";
        out << "  \"runtimeMetrics\": [\n";
        out << "    {\n";
        out << "      \"name\": \"selectedClusterCount\",\n";
        out << "      \"formula\": \"number of active-set clusters selected under the current budget\",\n";
        out << "      \"interpretation\": \"Higher generally means more corrective work scheduled this frame.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"selectedGuideCount\",\n";
        out << "      \"formula\": \"total guide strands promoted to full corrective simulation\",\n";
        out << "      \"interpretation\": \"Higher means more of the hairstyle receives high-cost correction.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"estimatedFrameMilliseconds\",\n";
        out << "      \"formula\": \"estimatedFixedCostMilliseconds + selectedClusterCount * estimatedClusterCostMilliseconds\",\n";
        out << "      \"interpretation\": \"Predicted frame cost used for bounded-latency reasoning; lower is safer for the 16.67 ms budget.\"\n";
        out << "    },\n";
        out << "    {\n";
        out << "      \"name\": \"recommendedSolverIterations\",\n";
        out << "      \"formula\": \"budget-adapted iteration recommendation derived from the current budget scale\",\n";
        out << "      \"interpretation\": \"Higher means the runtime can afford more local corrective iterations this frame.\"\n";
        out << "    }\n";
        out << "  ]\n";
        out << "}\n";
        outError.clear();
        return true;
    }

    bool BuildEmptyVariantPlan(const HairAsset& asset,
                              const std::vector<ActiveSetCluster>& clusters,
                              const ActiveSetBudget& budget,
                              ExperimentVariantSummary& outVariant,
                              std::string& outError);

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
                << report.averagePenetration << "," << report.maxPenetration << ","
                << report.averageGuidePredictorRisk << "," << report.p95EstimatedFrameMilliseconds << ","
                << report.maxEstimatedFrameMilliseconds << "," << report.hitRate60Fps << "\n";
            if (!first)
            {
                json << ",\n";
            }
            first = false;
            json << "    {\n"
                 << "      \"methodCode\": \"" << variant.code << "\",\n"
                 << "      \"methodLabel\": \"" << variant.label << "\",\n"
                 << "      \"frameCount\": " << report.frameCount << ",\n"
                 << "      \"avgPriorRms\": " << report.averagePriorRmsError << ",\n"
                 << "      \"avgHybridRmsBefore\": " << report.averageHybridRmsErrorBeforeCorrection << ",\n"
                 << "      \"avgHybridRmsAfter\": " << report.averageHybridRmsErrorAfterCorrection << ",\n"
                 << "      \"avgSelectedClusters\": " << report.averageSelectedClusterCount << ",\n"
                 << "      \"avgSelectedGuides\": " << report.averageSelectedGuideCount << ",\n"
                 << "      \"avgCorrectedPoints\": " << report.averageCorrectedPointCount << ",\n"
                 << "      \"avgPenetration\": " << report.averagePenetration << ",\n"
                 << "      \"maxPenetration\": " << report.maxPenetration << ",\n"
                 << "      \"avgPredictorRisk\": " << report.averageGuidePredictorRisk << ",\n"
                 << "      \"p95EstimatedFrameMs\": " << report.p95EstimatedFrameMilliseconds << ",\n"
                 << "      \"maxEstimatedFrameMs\": " << report.maxEstimatedFrameMilliseconds << ",\n"
                 << "      \"hitRate60Fps\": " << report.hitRate60Fps << "\n"
                 << "    }";
        }
        json << "\n  ]\n}\n";
        std::cout << "comparisonWritten=" << (std::filesystem::path(argv[4]) / "hybrid_compare_summary.csv").string() << "\n";
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
    if (command == "dispatchpack-info")
    {
        if (argc < 3)
        {
            PrintUsage();
            return 1;
        }
        ActiveSetDispatchPack pack;
        std::string error;
        if (!LoadActiveSetDispatchPackFromFile(argv[2], pack, error))
        {
            return PrintLoadFailure("Dispatch pack load failed", error, 11);
        }
        std::cout << BuildActiveSetDispatchPackSummary(pack) << "\n";
        return 0;
    }
    if (command == "dispatchpack-gpubuffers")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        ActiveSetDispatchPack pack = {};
        std::string error;
        if (!LoadActiveSetDispatchPackFromFile(argv[2], pack, error))
        {
            return PrintLoadFailure("Dispatch pack load failed", error, 179);
        }
        if (!SaveActiveSetDispatchPackGpuBuffers(argv[3], pack, error))
        {
            return PrintLoadFailure("Dispatch GPU buffer export failed", error, 180);
        }
        std::cout << BuildActiveSetDispatchPackSummary(pack) << " outputDir=" << argv[3] << "\n";
        return 0;
    }
    if (command == "asset-export-abc-compat")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairAsset asset = {};
        std::string error;
        if (!LoadHairAssetAny(argv[2], asset, error))
        {
            return PrintLoadFailure("Hair asset load failed", error, 181);
        }
        if (!SaveHairAssetToAlembic(argv[3], asset, error))
        {
            return PrintLoadFailure("ABC compatibility export failed", error, 182);
        }
        std::cout << "abcCompat=" << argv[3] << "\n";
        return 0;
    }
    if (command == "alembic-roundtrip-selftest")
    {
        if (argc < 4)
        {
            PrintUsage();
            return 1;
        }
        HairAsset asset = {};
        std::string error;
        if (!LoadHairAssetAny(argv[2], asset, error))
        {
            return PrintLoadFailure("Hair asset load failed", error, 183);
        }
        HairAlembicSettings settings = {};
        AlembicRoundTripReport report = {};
        if (!RoundTripHairAssetThroughAlembic(asset, argv[3], settings, report, error))
        {
            return PrintLoadFailure("Alembic round-trip self-test failed", error, 184);
        }
        std::ostringstream summary;
        summary.setf(std::ios::fixed);
        summary.precision(6);
        summary << "usedAlembicSdk=" << (report.usedAlembicSdk ? 1 : 0) << "\n"
                << "sourceStrands=" << report.sourceStrandCount << "\n"
                << "roundTripStrands=" << report.roundTripStrandCount << "\n"
                << "sourcePoints=" << report.sourcePointCount << "\n"
                << "roundTripPoints=" << report.roundTripPointCount << "\n"
                << "rmsRestPointError=" << report.rmsRestPointError << "\n"
                << "maxRestPointError=" << report.maxRestPointError << "\n"
                << "rmsWidthError=" << report.rmsWidthError << "\n"
                << "maxWidthError=" << report.maxWidthError << "\n"
                << "scalpRoundTripOk=" << (report.scalpRoundTripOk ? 1 : 0) << "\n";
        std::cout << summary.str();
        if (argc >= 5)
        {
            std::ofstream out(argv[4]);
            if (!out)
            {
                std::cerr << "Failed to write round-trip report.\n";
                return 185;
            }
            out << summary.str();
        }
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
    if (command == "haircache-export-usda-sequence")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        HairCache cache = {};
        HairAsset referenceAsset = {};
        std::string error;
        if (!LoadHairCacheFromFile(argv[2], cache, error))
        {
            return PrintLoadFailure("Hair cache load failed", error, 181);
        }
        if (!LoadHairAssetAny(argv[3], referenceAsset, error))
        {
            return PrintLoadFailure("Reference asset load failed", error, 182);
        }
        const uint32_t everyNth = std::max<uint32_t>(1u, ArgToUInt(argc, argv, 5, 1u));
        const bool includeScalp = ArgToInt(argc, argv, 6, 1) != 0;
        const bool includeCards = ArgToInt(argc, argv, 7, 0) != 0;
        std::error_code ec;
        std::filesystem::create_directories(argv[4], ec);
        if (ec)
        {
            return PrintLoadFailure("Output directory creation failed", ec.message(), 183);
        }
        HairUsdExportSettings settings = {};
        settings.includeScalp = includeScalp;
        settings.includeCards = includeCards;
        settings.cardSettings.cardsPerGuide = 1u;
        settings.cardSettings.segmentsPerCard = std::max<uint32_t>(4u, referenceAsset.particlesPerStrand > 1u ? referenceAsset.particlesPerStrand - 1u : 4u);
        uint32_t written = 0u;
        for (uint32_t frameIndex = 0u; frameIndex < cache.FrameCount(); frameIndex += everyNth)
        {
            HairAsset frameAsset = referenceAsset;
            const float timeSeconds = static_cast<float>(frameIndex) / std::max(cache.frameRate, 1.0f);
            if (!BuildHairAssetFrameFromCache(cache, timeSeconds, false, frameAsset, error))
            {
                return PrintLoadFailure("Hair cache frame build failed", error, 184);
            }
            std::ostringstream name;
            name << "frame_" << std::setw(4) << std::setfill('0') << frameIndex << ".usda";
            if (!SaveHairAssetToUsda(std::filesystem::path(argv[4]) / name.str(), frameAsset, settings, error))
            {
                return PrintLoadFailure("Hair cache USDA export failed", error, 185);
            }
            ++written;
        }
        if (!WriteUsdSequenceManifest(argv[4], "haircache_usda_sequence", cache.FrameCount(), cache.frameRate, cache.strandCount, false, everyNth, error))
        {
            return PrintLoadFailure("USDA sequence manifest export failed", error, 186);
        }
        std::cout << "framesWritten=" << written << " sourceFrames=" << cache.FrameCount() << " outputDir=" << argv[4] << "\n";
        return 0;
    }
    if (command == "guidecache-export-usda-sequence")
    {
        if (argc < 5)
        {
            PrintUsage();
            return 1;
        }
        GuideCache cache = {};
        HairAsset referenceAsset = {};
        std::string error;
        if (!LoadGuideCacheFromFile(argv[2], cache, error))
        {
            return PrintLoadFailure("Guide cache load failed", error, 187);
        }
        if (!LoadHairAssetAny(argv[3], referenceAsset, error))
        {
            return PrintLoadFailure("Reference asset load failed", error, 188);
        }
        const uint32_t everyNth = std::max<uint32_t>(1u, ArgToUInt(argc, argv, 5, 1u));
        const bool includeScalp = ArgToInt(argc, argv, 6, 1) != 0;
        std::error_code ec;
        std::filesystem::create_directories(argv[4], ec);
        if (ec)
        {
            return PrintLoadFailure("Output directory creation failed", ec.message(), 189);
        }
        HairUsdExportSettings settings = {};
        settings.includeScalp = includeScalp;
        settings.includeCards = false;
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
