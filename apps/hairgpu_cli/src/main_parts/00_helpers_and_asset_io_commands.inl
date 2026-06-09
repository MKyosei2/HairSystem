// main.cpp ordered part 00: helpers_and_asset_io_commands
// This file is included by ../main.cpp; do not compile it directly.

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
