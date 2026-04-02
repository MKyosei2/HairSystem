#include "hairgpu/AssetIO.h"

#include "hairgpu/Binding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace hairgpu
{
    namespace
    {
        struct ImportedPoint
        {
            uint32_t pointIndex = 0;
            Vec3 position = {};
            float width = 0.0f;
        };

        struct ImportedStrand
        {
            uint32_t originalStrandId = 0;
            std::vector<ImportedPoint> points;
            bool isGuide = true;
            int32_t parentGuideOriginal = -1;
            uint32_t groupId = 0;
            float followWeight = 1.0f;
            float clumpWeight = 0.0f;
            float noiseWeight = 0.0f;
        };

        std::string Trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        std::vector<std::string> SplitCsvLine(const std::string& line)
        {
            std::vector<std::string> values;
            std::string current;
            bool inQuotes = false;
            for (size_t i = 0; i < line.size(); ++i)
            {
                const char c = line[i];
                if (c == '"')
                {
                    if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
                    {
                        current.push_back('"');
                        ++i;
                    }
                    else
                    {
                        inQuotes = !inQuotes;
                    }
                }
                else if (c == ',' && !inQuotes)
                {
                    values.push_back(Trim(current));
                    current.clear();
                }
                else
                {
                    current.push_back(c);
                }
            }
            values.push_back(Trim(current));
            return values;
        }

        bool TryGetField(const std::unordered_map<std::string, size_t>& columns, const std::vector<std::string>& values, std::initializer_list<const char*> names, std::string& outValue)
        {
            for (const char* name : names)
            {
                auto it = columns.find(ToLower(name));
                if (it != columns.end() && it->second < values.size())
                {
                    outValue = values[it->second];
                    return true;
                }
            }
            return false;
        }

        float ParseFloatOr(const std::string& text, float fallback)
        {
            try
            {
                return std::stof(text);
            }
            catch (...)
            {
                return fallback;
            }
        }

        int32_t ParseIntOr(const std::string& text, int32_t fallback)
        {
            try
            {
                return static_cast<int32_t>(std::stoi(text));
            }
            catch (...)
            {
                return fallback;
            }
        }

        uint32_t ParseUIntOr(const std::string& text, uint32_t fallback)
        {
            try
            {
                return static_cast<uint32_t>(std::stoul(text));
            }
            catch (...)
            {
                return fallback;
            }
        }

        bool ParseBoolText(const std::string& text, bool fallback)
        {
            const std::string lowered = ToLower(text);
            if (lowered == "1" || lowered == "true" || lowered == "yes")
            {
                return true;
            }
            if (lowered == "0" || lowered == "false" || lowered == "no")
            {
                return false;
            }
            return fallback;
        }

        Vec3 SamplePolylinePosition(const std::vector<ImportedPoint>& points, float t)
        {
            if (points.empty())
            {
                return {};
            }
            if (points.size() == 1)
            {
                return points.front().position;
            }

            const float scaled = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(points.size() - 1);
            const size_t i0 = static_cast<size_t>(std::floor(scaled));
            const size_t i1 = std::min(i0 + 1, points.size() - 1);
            const float localT = scaled - static_cast<float>(i0);
            return Lerp(points[i0].position, points[i1].position, localT);
        }

        float SamplePolylineWidth(const std::vector<ImportedPoint>& points, float t)
        {
            if (points.empty())
            {
                return 0.004f;
            }
            if (points.size() == 1)
            {
                return points.front().width;
            }

            const float scaled = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(points.size() - 1);
            const size_t i0 = static_cast<size_t>(std::floor(scaled));
            const size_t i1 = std::min(i0 + 1, points.size() - 1);
            const float localT = scaled - static_cast<float>(i0);
            return Lerp(points[i0].width, points[i1].width, localT);
        }

        void BuildSyntheticScalpForRoots(HairAsset& asset, const std::vector<Vec3>& roots)
        {
            asset.scalpMesh.positions.clear();
            asset.scalpMesh.normals.clear();
            asset.scalpMesh.uvs.clear();
            asset.scalpMesh.triangles.clear();
            asset.scalpMesh.skinWeights.clear();
            asset.scalpMesh.boneCount = 1;

            asset.scalpMesh.positions.reserve(roots.size() * 3);
            asset.scalpMesh.normals.reserve(roots.size() * 3);
            asset.scalpMesh.uvs.reserve(roots.size() * 3);
            asset.scalpMesh.triangles.reserve(roots.size());
            asset.scalpMesh.skinWeights.reserve(roots.size() * 3);

            for (size_t strandIndex = 0; strandIndex < roots.size(); ++strandIndex)
            {
                const Vec3& root = roots[strandIndex];
                const float eps = 0.0025f;
                const uint32_t baseIndex = static_cast<uint32_t>(asset.scalpMesh.positions.size());
                asset.scalpMesh.positions.push_back(root);
                asset.scalpMesh.positions.push_back(root + Vec3(eps, 0.0f, 0.0f));
                asset.scalpMesh.positions.push_back(root + Vec3(0.0f, 0.0f, eps));
                asset.scalpMesh.normals.push_back({ 0.0f, 1.0f, 0.0f });
                asset.scalpMesh.normals.push_back({ 0.0f, 1.0f, 0.0f });
                asset.scalpMesh.normals.push_back({ 0.0f, 1.0f, 0.0f });
                const Vec2 uv = { 0.5f + root.x * 0.5f, 0.5f + root.z * 0.5f };
                asset.scalpMesh.uvs.push_back(uv);
                asset.scalpMesh.uvs.push_back(uv);
                asset.scalpMesh.uvs.push_back(uv);
                asset.scalpMesh.triangles.push_back({ baseIndex, baseIndex + 1u, baseIndex + 2u });
                asset.scalpMesh.skinWeights.push_back({ {0u, 0u, 0u, 0u}, {1.0f, 0.0f, 0.0f, 0.0f} });
                asset.scalpMesh.skinWeights.push_back({ {0u, 0u, 0u, 0u}, {1.0f, 0.0f, 0.0f, 0.0f} });
                asset.scalpMesh.skinWeights.push_back({ {0u, 0u, 0u, 0u}, {1.0f, 0.0f, 0.0f, 0.0f} });
            }
        }

        size_t FindMatchingDelimiter(const std::string& text, size_t openPos, char openChar, char closeChar)
        {
            if (openPos >= text.size() || text[openPos] != openChar)
            {
                return std::string::npos;
            }
            int depth = 0;
            for (size_t i = openPos; i < text.size(); ++i)
            {
                if (text[i] == openChar)
                {
                    ++depth;
                }
                else if (text[i] == closeChar)
                {
                    --depth;
                    if (depth == 0)
                    {
                        return i;
                    }
                }
            }
            return std::string::npos;
        }

        bool ExtractUsdDefBlock(const std::string& text, const char* defType, const char* defName, std::string& outBlock)
        {
            const std::string signature = std::string("def ") + defType + " \"" + defName + "\"";
            const size_t defPos = text.find(signature);
            if (defPos == std::string::npos)
            {
                return false;
            }
            const size_t openBrace = text.find('{', defPos + signature.size());
            if (openBrace == std::string::npos)
            {
                return false;
            }
            const size_t closeBrace = FindMatchingDelimiter(text, openBrace, '{', '}');
            if (closeBrace == std::string::npos)
            {
                return false;
            }
            outBlock = text.substr(openBrace + 1u, closeBrace - openBrace - 1u);
            return true;
        }

        bool ExtractUsdArrayText(const std::string& block, const char* key, std::string& outArrayText)
        {
            const size_t keyPos = block.find(key);
            if (keyPos == std::string::npos)
            {
                return false;
            }
            const size_t equalsPos = block.find('=', keyPos + std::char_traits<char>::length(key));
            if (equalsPos == std::string::npos)
            {
                return false;
            }
            const size_t openBracket = block.find('[', equalsPos + 1u);
            if (openBracket == std::string::npos)
            {
                return false;
            }
            const size_t closeBracket = FindMatchingDelimiter(block, openBracket, '[', ']');
            if (closeBracket == std::string::npos)
            {
                return false;
            }
            outArrayText = block.substr(openBracket + 1u, closeBracket - openBracket - 1u);
            return true;
        }

        std::vector<float> ParseFloatArrayText(const std::string& text)
        {
            std::vector<float> values;
            const char* ptr = text.c_str();
            while (*ptr != '\0')
            {
                while (*ptr != '\0' && !(std::isdigit(static_cast<unsigned char>(*ptr)) || *ptr == '-' || *ptr == '+' || *ptr == '.'))
                {
                    ++ptr;
                }
                if (*ptr == '\0')
                {
                    break;
                }
                char* endPtr = nullptr;
                errno = 0;
                const float value = std::strtof(ptr, &endPtr);
                if (endPtr == ptr)
                {
                    ++ptr;
                    continue;
                }
                if (errno == 0)
                {
                    values.push_back(value);
                }
                ptr = endPtr;
            }
            return values;
        }

        std::vector<int32_t> ParseIntArrayText(const std::string& text)
        {
            std::vector<int32_t> values;
            const char* ptr = text.c_str();
            while (*ptr != '\0')
            {
                while (*ptr != '\0' && !(std::isdigit(static_cast<unsigned char>(*ptr)) || *ptr == '-' || *ptr == '+'))
                {
                    ++ptr;
                }
                if (*ptr == '\0')
                {
                    break;
                }
                char* endPtr = nullptr;
                errno = 0;
                const long value = std::strtol(ptr, &endPtr, 10);
                if (endPtr == ptr)
                {
                    ++ptr;
                    continue;
                }
                if (errno == 0)
                {
                    values.push_back(static_cast<int32_t>(value));
                }
                ptr = endPtr;
            }
            return values;
        }

        ScalpMesh ParseUsdaScalpMesh(const std::string& text)
        {
            ScalpMesh scalp;
            std::string block;
            if (!ExtractUsdDefBlock(text, "Mesh", "Scalp", block))
            {
                return scalp;
            }

            std::string pointsText;
            if (ExtractUsdArrayText(block, "point3f[] points", pointsText))
            {
                const std::vector<float> values = ParseFloatArrayText(pointsText);
                scalp.positions.reserve(values.size() / 3u);
                for (size_t i = 0; i + 2u < values.size(); i += 3u)
                {
                    scalp.positions.push_back({ values[i], values[i + 1u], values[i + 2u] });
                }
            }

            std::string normalsText;
            if (ExtractUsdArrayText(block, "normal3f[] normals", normalsText))
            {
                const std::vector<float> values = ParseFloatArrayText(normalsText);
                scalp.normals.reserve(values.size() / 3u);
                for (size_t i = 0; i + 2u < values.size(); i += 3u)
                {
                    scalp.normals.push_back({ values[i], values[i + 1u], values[i + 2u] });
                }
            }

            std::string uvsText;
            if (ExtractUsdArrayText(block, "texCoord2f[] primvars:st", uvsText))
            {
                const std::vector<float> values = ParseFloatArrayText(uvsText);
                scalp.uvs.reserve(values.size() / 2u);
                for (size_t i = 0; i + 1u < values.size(); i += 2u)
                {
                    scalp.uvs.push_back({ values[i], values[i + 1u] });
                }
            }

            std::string faceCountsText;
            std::string faceIndicesText;
            if (ExtractUsdArrayText(block, "int[] faceVertexCounts", faceCountsText) && ExtractUsdArrayText(block, "int[] faceVertexIndices", faceIndicesText))
            {
                const std::vector<int32_t> faceCounts = ParseIntArrayText(faceCountsText);
                const std::vector<int32_t> faceIndices = ParseIntArrayText(faceIndicesText);
                size_t cursor = 0u;
                for (int32_t count : faceCounts)
                {
                    if (count < 3 || cursor + static_cast<size_t>(count) > faceIndices.size())
                    {
                        break;
                    }
                    const uint32_t root = static_cast<uint32_t>(std::max(faceIndices[cursor], 0));
                    for (int32_t local = 1; local + 1 < count; ++local)
                    {
                        scalp.triangles.push_back({
                            root,
                            static_cast<uint32_t>(std::max(faceIndices[cursor + static_cast<size_t>(local)], 0)),
                            static_cast<uint32_t>(std::max(faceIndices[cursor + static_cast<size_t>(local + 1)], 0))
                        });
                    }
                    cursor += static_cast<size_t>(count);
                }
            }

            scalp.skinWeights.assign(scalp.positions.size(), SkinWeight4{});
            scalp.boneCount = scalp.positions.empty() ? 0u : 1u;
            return scalp;
        }
    }

    bool LoadHairAssetFromCurvesCsv(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        std::ifstream in(filePath);
        if (!in)
        {
            outError = "Failed to open curves CSV: " + filePath.string();
            return false;
        }

        std::string headerLine;
        if (!std::getline(in, headerLine))
        {
            outError = "Curves CSV is empty.";
            return false;
        }

        const std::vector<std::string> headers = SplitCsvLine(headerLine);
        std::unordered_map<std::string, size_t> columns;
        for (size_t i = 0; i < headers.size(); ++i)
        {
            columns[ToLower(headers[i])] = i;
        }

        if (columns.find("x") == columns.end() || columns.find("y") == columns.end() || columns.find("z") == columns.end())
        {
            outError = "Curves CSV must contain x,y,z columns.";
            return false;
        }

        std::unordered_map<uint32_t, ImportedStrand> strandMap;
        std::string line;
        while (std::getline(in, line))
        {
            if (Trim(line).empty())
            {
                continue;
            }

            const std::vector<std::string> values = SplitCsvLine(line);
            std::string strandText;
            uint32_t strandId = 0;
            if (TryGetField(columns, values, { "strandindex", "strand_id", "strand", "strandid" }, strandText))
            {
                strandId = ParseUIntOr(strandText, 0u);
            }
            else
            {
                outError = "Curves CSV must contain strand index column (strandIndex / strand_id).";
                return false;
            }

            ImportedStrand& strand = strandMap[strandId];
            strand.originalStrandId = strandId;

            std::string pointText;
            const uint32_t pointId = TryGetField(columns, values, { "pointindex", "point_id", "point", "pointid" }, pointText)
                ? ParseUIntOr(pointText, static_cast<uint32_t>(strand.points.size()))
                : static_cast<uint32_t>(strand.points.size());

            ImportedPoint point = {};
            point.pointIndex = pointId;
            point.position.x = ParseFloatOr(values[columns["x"]], 0.0f);
            point.position.y = ParseFloatOr(values[columns["y"]], 0.0f);
            point.position.z = ParseFloatOr(values[columns["z"]], 0.0f);

            std::string widthText;
            const bool hasWidth = TryGetField(columns, values, { "width", "radius" }, widthText);
            point.width = hasWidth ? ParseFloatOr(widthText, 0.004f) : 0.004f;

            std::string groupText;
            if (TryGetField(columns, values, { "groupid", "group_id", "group" }, groupText))
            {
                strand.groupId = ParseUIntOr(groupText, strand.groupId);
            }

            std::string guideText;
            if (TryGetField(columns, values, { "isguide", "guide", "is_guide" }, guideText))
            {
                strand.isGuide = ParseBoolText(guideText, strand.isGuide);
            }

            std::string parentText;
            if (TryGetField(columns, values, { "parentguide", "parent_guide", "parentguideindex" }, parentText))
            {
                strand.parentGuideOriginal = ParseIntOr(parentText, strand.parentGuideOriginal);
            }

            std::string followWeightText;
            if (TryGetField(columns, values, { "followweight", "follow_weight" }, followWeightText))
            {
                strand.followWeight = ParseFloatOr(followWeightText, strand.followWeight);
            }

            std::string clumpWeightText;
            if (TryGetField(columns, values, { "clumpweight", "clump_weight" }, clumpWeightText))
            {
                strand.clumpWeight = ParseFloatOr(clumpWeightText, strand.clumpWeight);
            }

            std::string noiseWeightText;
            if (TryGetField(columns, values, { "noiseweight", "noise_weight" }, noiseWeightText))
            {
                strand.noiseWeight = ParseFloatOr(noiseWeightText, strand.noiseWeight);
            }

            strand.points.push_back(point);
        }

        if (strandMap.empty())
        {
            outError = "Curves CSV did not contain any strand points.";
            return false;
        }

        std::vector<ImportedStrand> inputStrands;
        inputStrands.reserve(strandMap.size());
        for (auto& pair : strandMap)
        {
            ImportedStrand strand = std::move(pair.second);
            std::sort(strand.points.begin(), strand.points.end(), [](const ImportedPoint& a, const ImportedPoint& b) { return a.pointIndex < b.pointIndex; });
            if (strand.points.size() < 2)
            {
                continue;
            }
            if (strand.parentGuideOriginal < 0)
            {
                strand.parentGuideOriginal = strand.isGuide ? static_cast<int32_t>(strand.originalStrandId) : -1;
            }
            if (strand.groupId == 0u)
            {
                strand.groupId = strand.isGuide ? strand.originalStrandId : static_cast<uint32_t>(std::max(strand.parentGuideOriginal, 0));
            }
            inputStrands.push_back(std::move(strand));
        }

        if (inputStrands.empty())
        {
            outError = "Curves CSV must contain at least one strand with 2 or more points.";
            return false;
        }

        std::sort(inputStrands.begin(), inputStrands.end(), [](const ImportedStrand& a, const ImportedStrand& b) {
            if (a.isGuide != b.isGuide)
            {
                return a.isGuide && !b.isGuide;
            }
            return a.originalStrandId < b.originalStrandId;
        });

        uint32_t guideCount = 0;
        uint32_t maxPointCount = 0;
        for (const ImportedStrand& strand : inputStrands)
        {
            guideCount += strand.isGuide ? 1u : 0u;
            maxPointCount = std::max<uint32_t>(maxPointCount, static_cast<uint32_t>(strand.points.size()));
        }
        if (guideCount == 0)
        {
            guideCount = static_cast<uint32_t>(inputStrands.size());
            for (ImportedStrand& strand : inputStrands)
            {
                strand.isGuide = true;
                strand.parentGuideOriginal = static_cast<int32_t>(strand.originalStrandId);
            }
        }

        const uint32_t particlesPerStrand = std::max<uint32_t>(4u, std::min<uint32_t>(maxPointCount, 64u));
        HairAsset asset;
        asset.guideStrandCount = guideCount;
        asset.followerStrandCount = static_cast<uint32_t>(inputStrands.size()) - guideCount;
        asset.particlesPerStrand = particlesPerStrand;
        asset.strands.reserve(inputStrands.size());
        asset.materials.reserve(inputStrands.size());
        asset.rootBindings.reserve(inputStrands.size());
        asset.followData.reserve(inputStrands.size());
        asset.restPoints.reserve(static_cast<size_t>(inputStrands.size()) * particlesPerStrand);
        asset.pointWidths.reserve(static_cast<size_t>(inputStrands.size()) * particlesPerStrand);

        std::unordered_map<uint32_t, uint32_t> originalGuideIdToNewIndex;
        std::vector<Vec3> strandRoots;
        strandRoots.reserve(inputStrands.size());

        for (size_t strandIndex = 0; strandIndex < inputStrands.size(); ++strandIndex)
        {
            const ImportedStrand& imported = inputStrands[strandIndex];
            if (imported.isGuide)
            {
                originalGuideIdToNewIndex[imported.originalStrandId] = static_cast<uint32_t>(strandIndex);
            }
        }

        for (size_t strandIndex = 0; strandIndex < inputStrands.size(); ++strandIndex)
        {
            const ImportedStrand& imported = inputStrands[strandIndex];
            StrandRange strandRange = {};
            strandRange.pointOffset = static_cast<uint32_t>(asset.restPoints.size());
            strandRange.pointCount = particlesPerStrand;
            strandRange.groupId = imported.groupId;
            strandRange.flags = imported.isGuide ? static_cast<uint32_t>(StrandFlags::Guide) : static_cast<uint32_t>(StrandFlags::None);
            asset.strands.push_back(strandRange);

            float rootWidth = 0.0f;
            float tipWidth = 0.0f;
            for (uint32_t pointIndex = 0; pointIndex < particlesPerStrand; ++pointIndex)
            {
                const float t = (particlesPerStrand > 1u) ? (static_cast<float>(pointIndex) / static_cast<float>(particlesPerStrand - 1u)) : 0.0f;
                const Vec3 p = SamplePolylinePosition(imported.points, t);
                const float width = std::max(SamplePolylineWidth(imported.points, t), 0.0005f);
                asset.restPoints.push_back(p);
                asset.pointWidths.push_back(width);
                if (pointIndex == 0u)
                {
                    rootWidth = width;
                    strandRoots.push_back(p);
                }
                if (pointIndex + 1u == particlesPerStrand)
                {
                    tipWidth = width;
                }
            }

            StrandMaterial material = {};
            material.rootWidth = std::max(rootWidth, 0.0008f);
            material.tipWidth = std::max(std::min(tipWidth, material.rootWidth), 0.0003f);
            material.stiffness = imported.isGuide ? 0.82f : 0.70f;
            material.damping = 0.03f;
            asset.materials.push_back(material);

            RootBinding binding = {};
            binding.triangleIndex = static_cast<uint32_t>(strandIndex);
            binding.barycentric = { 1.0f, 0.0f, 0.0f };
            binding.rootUv = { 0.5f, 0.5f };
            asset.rootBindings.push_back(binding);

            FollowData follow = {};
            if (imported.isGuide)
            {
                follow.parentGuide = static_cast<int32_t>(strandIndex);
                follow.followWeight = 1.0f;
            }
            else
            {
                const auto guideIt = originalGuideIdToNewIndex.find(static_cast<uint32_t>(std::max(imported.parentGuideOriginal, 0)));
                follow.parentGuide = (guideIt != originalGuideIdToNewIndex.end()) ? static_cast<int32_t>(guideIt->second) : 0;
                follow.followWeight = imported.followWeight;
            }
            follow.clumpWeight = imported.clumpWeight;
            follow.noiseWeight = imported.noiseWeight;
            asset.followData.push_back(follow);
        }

        BuildSyntheticScalpForRoots(asset, strandRoots);
        outAsset = std::move(asset);
        outError.clear();
        return true;
    }

    bool LoadHairAssetFromUsda(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        std::ifstream in(filePath);
        if (!in)
        {
            outError = "Failed to open USDA file: " + filePath.string();
            return false;
        }

        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.empty())
        {
            outError = "USDA file is empty.";
            return false;
        }

        std::string curvesBlock;
        if (!ExtractUsdDefBlock(text, "BasisCurves", "HairStrands", curvesBlock))
        {
            outError = "USDA file does not contain def BasisCurves \"HairStrands\".";
            return false;
        }

        std::string countsText;
        std::string pointsText;
        if (!ExtractUsdArrayText(curvesBlock, "int[] curveVertexCounts", countsText) || !ExtractUsdArrayText(curvesBlock, "point3f[] points", pointsText))
        {
            outError = "USDA HairStrands block is missing curveVertexCounts or points arrays.";
            return false;
        }

        const std::vector<int32_t> curveCounts = ParseIntArrayText(countsText);
        const std::vector<float> pointValues = ParseFloatArrayText(pointsText);
        if (curveCounts.empty() || (pointValues.size() % 3u) != 0u)
        {
            outError = "USDA HairStrands arrays are malformed.";
            return false;
        }

        std::vector<Vec3> points;
        points.reserve(pointValues.size() / 3u);
        for (size_t i = 0; i + 2u < pointValues.size(); i += 3u)
        {
            points.push_back({ pointValues[i], pointValues[i + 1u], pointValues[i + 2u] });
        }

        std::vector<float> widths;
        std::string widthsText;
        if (ExtractUsdArrayText(curvesBlock, "float[] widths", widthsText))
        {
            widths = ParseFloatArrayText(widthsText);
        }

        std::vector<int32_t> guideFlags;
        std::string guidesText;
        if (ExtractUsdArrayText(curvesBlock, "int[] primvars:guide", guidesText))
        {
            guideFlags = ParseIntArrayText(guidesText);
        }

        std::vector<int32_t> groupIds;
        std::string groupsText;
        if (ExtractUsdArrayText(curvesBlock, "int[] primvars:groupId", groupsText))
        {
            groupIds = ParseIntArrayText(groupsText);
        }

        size_t expectedPointCount = 0u;
        for (int32_t count : curveCounts)
        {
            if (count < 2)
            {
                outError = "USDA contains a curve with fewer than 2 points.";
                return false;
            }
            expectedPointCount += static_cast<size_t>(count);
        }
        if (expectedPointCount != points.size())
        {
            outError = "USDA point count does not match curveVertexCounts.";
            return false;
        }

        std::vector<ImportedStrand> inputStrands;
        inputStrands.reserve(curveCounts.size());
        size_t pointCursor = 0u;
        for (size_t curveIndex = 0; curveIndex < curveCounts.size(); ++curveIndex)
        {
            ImportedStrand strand = {};
            strand.originalStrandId = static_cast<uint32_t>(curveIndex);
            strand.isGuide = guideFlags.empty() ? true : (curveIndex < guideFlags.size() ? (guideFlags[curveIndex] != 0) : true);
            strand.groupId = (curveIndex < groupIds.size()) ? static_cast<uint32_t>(std::max(groupIds[curveIndex], 0)) : static_cast<uint32_t>(curveIndex);
            strand.parentGuideOriginal = strand.isGuide ? static_cast<int32_t>(strand.originalStrandId) : -1;
            const size_t count = static_cast<size_t>(curveCounts[curveIndex]);
            strand.points.reserve(count);
            for (size_t localIndex = 0; localIndex < count; ++localIndex)
            {
                ImportedPoint point = {};
                point.pointIndex = static_cast<uint32_t>(localIndex);
                point.position = points[pointCursor];
                if (widths.size() == points.size())
                {
                    point.width = std::max(widths[pointCursor], 0.0005f);
                }
                else if (curveIndex < widths.size())
                {
                    point.width = std::max(widths[curveIndex], 0.0005f);
                }
                else
                {
                    point.width = 0.004f;
                }
                strand.points.push_back(point);
                ++pointCursor;
            }
            inputStrands.push_back(std::move(strand));
        }

        std::unordered_map<uint32_t, uint32_t> groupToGuideOriginal;
        for (const ImportedStrand& strand : inputStrands)
        {
            if (strand.isGuide && groupToGuideOriginal.find(strand.groupId) == groupToGuideOriginal.end())
            {
                groupToGuideOriginal[strand.groupId] = strand.originalStrandId;
            }
        }
        for (ImportedStrand& strand : inputStrands)
        {
            if (strand.isGuide)
            {
                strand.parentGuideOriginal = static_cast<int32_t>(strand.originalStrandId);
                continue;
            }
            auto guideIt = groupToGuideOriginal.find(strand.groupId);
            if (guideIt != groupToGuideOriginal.end())
            {
                strand.parentGuideOriginal = static_cast<int32_t>(guideIt->second);
                continue;
            }
            float bestDistanceSq = std::numeric_limits<float>::max();
            int32_t bestGuide = -1;
            const Vec3 root = strand.points.empty() ? Vec3{} : strand.points.front().position;
            for (const ImportedStrand& candidate : inputStrands)
            {
                if (!candidate.isGuide || candidate.points.empty())
                {
                    continue;
                }
                const Vec3 delta = candidate.points.front().position - root;
                const float distanceSq = Dot(delta, delta);
                if (distanceSq < bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    bestGuide = static_cast<int32_t>(candidate.originalStrandId);
                }
            }
            strand.parentGuideOriginal = (bestGuide >= 0) ? bestGuide : 0;
        }

        std::sort(inputStrands.begin(), inputStrands.end(), [](const ImportedStrand& a, const ImportedStrand& b) {
            if (a.isGuide != b.isGuide)
            {
                return a.isGuide && !b.isGuide;
            }
            if (a.groupId != b.groupId)
            {
                return a.groupId < b.groupId;
            }
            return a.originalStrandId < b.originalStrandId;
        });

        uint32_t guideCount = 0u;
        uint32_t maxPointCount = 0u;
        for (const ImportedStrand& strand : inputStrands)
        {
            guideCount += strand.isGuide ? 1u : 0u;
            maxPointCount = std::max<uint32_t>(maxPointCount, static_cast<uint32_t>(strand.points.size()));
        }
        if (guideCount == 0u)
        {
            guideCount = static_cast<uint32_t>(inputStrands.size());
            for (ImportedStrand& strand : inputStrands)
            {
                strand.isGuide = true;
                strand.parentGuideOriginal = static_cast<int32_t>(strand.originalStrandId);
            }
        }

        const uint32_t particlesPerStrand = std::max<uint32_t>(4u, std::min<uint32_t>(maxPointCount, 64u));
        HairAsset asset;
        asset.guideStrandCount = guideCount;
        asset.followerStrandCount = static_cast<uint32_t>(inputStrands.size()) - guideCount;
        asset.particlesPerStrand = particlesPerStrand;
        asset.strands.reserve(inputStrands.size());
        asset.materials.reserve(inputStrands.size());
        asset.rootBindings.reserve(inputStrands.size());
        asset.followData.reserve(inputStrands.size());
        asset.restPoints.reserve(static_cast<size_t>(inputStrands.size()) * particlesPerStrand);
        asset.pointWidths.reserve(static_cast<size_t>(inputStrands.size()) * particlesPerStrand);

        std::unordered_map<uint32_t, uint32_t> originalGuideIdToNewIndex;
        std::vector<Vec3> strandRoots;
        strandRoots.reserve(inputStrands.size());
        for (size_t strandIndex = 0; strandIndex < inputStrands.size(); ++strandIndex)
        {
            if (inputStrands[strandIndex].isGuide)
            {
                originalGuideIdToNewIndex[inputStrands[strandIndex].originalStrandId] = static_cast<uint32_t>(strandIndex);
            }
        }

        for (size_t strandIndex = 0; strandIndex < inputStrands.size(); ++strandIndex)
        {
            const ImportedStrand& imported = inputStrands[strandIndex];
            StrandRange strandRange = {};
            strandRange.pointOffset = static_cast<uint32_t>(asset.restPoints.size());
            strandRange.pointCount = particlesPerStrand;
            strandRange.groupId = imported.groupId;
            strandRange.flags = imported.isGuide ? static_cast<uint32_t>(StrandFlags::Guide) : static_cast<uint32_t>(StrandFlags::None);
            asset.strands.push_back(strandRange);

            float rootWidth = 0.0f;
            float tipWidth = 0.0f;
            for (uint32_t pointIndex = 0; pointIndex < particlesPerStrand; ++pointIndex)
            {
                const float t = (particlesPerStrand > 1u) ? (static_cast<float>(pointIndex) / static_cast<float>(particlesPerStrand - 1u)) : 0.0f;
                const Vec3 p = SamplePolylinePosition(imported.points, t);
                const float width = std::max(SamplePolylineWidth(imported.points, t), 0.0005f);
                asset.restPoints.push_back(p);
                asset.pointWidths.push_back(width);
                if (pointIndex == 0u)
                {
                    rootWidth = width;
                    strandRoots.push_back(p);
                }
                if (pointIndex + 1u == particlesPerStrand)
                {
                    tipWidth = width;
                }
            }

            StrandMaterial material = {};
            material.rootWidth = std::max(rootWidth, 0.0008f);
            material.tipWidth = std::max(std::min(tipWidth, material.rootWidth), 0.0003f);
            material.stiffness = imported.isGuide ? 0.82f : 0.70f;
            material.damping = 0.03f;
            asset.materials.push_back(material);
            asset.rootBindings.push_back({});

            FollowData follow = {};
            if (imported.isGuide)
            {
                follow.parentGuide = static_cast<int32_t>(strandIndex);
                follow.followWeight = 1.0f;
            }
            else
            {
                const auto guideIt = originalGuideIdToNewIndex.find(static_cast<uint32_t>(std::max(imported.parentGuideOriginal, 0)));
                follow.parentGuide = (guideIt != originalGuideIdToNewIndex.end()) ? static_cast<int32_t>(guideIt->second) : 0;
                follow.followWeight = imported.followWeight;
            }
            follow.clumpWeight = imported.clumpWeight;
            follow.noiseWeight = imported.noiseWeight;
            asset.followData.push_back(follow);
        }

        const ScalpMesh scalp = ParseUsdaScalpMesh(text);
        if (!scalp.positions.empty() && !scalp.triangles.empty())
        {
            asset.scalpMesh = scalp;
            RootBindingQualityReport report = {};
            if (!GenerateRootBindingsFromScalp(asset, false, &report))
            {
                BuildSyntheticScalpForRoots(asset, strandRoots);
            }
        }
        else
        {
            BuildSyntheticScalpForRoots(asset, strandRoots);
        }

        outAsset = std::move(asset);
        outError.clear();
        return true;
    }

    bool LoadHairAssetFromAlembic(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
#ifdef HAIRGPU_WITH_ALEMBIC
        (void)filePath;
        (void)outAsset;
        outError = "Alembic import hook is enabled, but the concrete AbcGeom reader still needs to be connected in AssetIO.cpp for this local SDK setup.";
        return false;
#else
        (void)filePath;
        (void)outAsset;
        outError = "Alembic import entry point was added, but this package does not bundle the Alembic SDK. Build with HAIRGPU_WITH_ALEMBIC and wire your local Alembic/OpenEXR/Imath SDK to enable real .abc import. For immediate testing, use the CSV curves importer path.";
        return false;
#endif
    }
}

namespace hairgpu
{
    namespace
    {
        template <typename T>
        bool WriteRaw(std::ofstream& out, const T& value)
        {
            out.write(reinterpret_cast<const char*>(&value), sizeof(T));
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadRaw(std::ifstream& in, T& value)
        {
            in.read(reinterpret_cast<char*>(&value), sizeof(T));
            return static_cast<bool>(in);
        }

        template <typename T>
        bool WriteVector(std::ofstream& out, const std::vector<T>& values)
        {
            uint64_t count = static_cast<uint64_t>(values.size());
            if (!WriteRaw(out, count)) return false;
            if (!values.empty()) out.write(reinterpret_cast<const char*>(values.data()), sizeof(T) * values.size());
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadVector(std::ifstream& in, std::vector<T>& values)
        {
            uint64_t count = 0;
            if (!ReadRaw(in, count)) return false;
            values.resize(static_cast<size_t>(count));
            if (!values.empty()) in.read(reinterpret_cast<char*>(values.data()), sizeof(T) * values.size());
            return static_cast<bool>(in);
        }
    }

    bool SaveHairAssetToAlembic(const std::filesystem::path& filePath, const HairAsset& asset, std::string& outError)
    {
        (void)filePath;
        (void)asset;
        outError = "Alembic export is not available in this build.";
        return false;
    }

    bool LoadHairAssetFromHairFile(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        return LoadHairAssetFromHairBinary(filePath, outAsset, outError);
    }

    bool SaveHairAssetToHairBinary(const std::filesystem::path& filePath, const HairAsset& asset, std::string& outError)
    {
        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open output file: " + filePath.string();
            return false;
        }
        const char magic[8] = {'H','G','P','U','H','B','I','N'};
        out.write(magic, sizeof(magic));
        uint32_t version = 1;
        WriteRaw(out, version);
        WriteRaw(out, asset.guideStrandCount);
        WriteRaw(out, asset.followerStrandCount);
        WriteRaw(out, asset.particlesPerStrand);
        if (!WriteVector(out, asset.strands) || !WriteVector(out, asset.restPoints) || !WriteVector(out, asset.pointWidths) ||
            !WriteVector(out, asset.materials) || !WriteVector(out, asset.rootBindings) || !WriteVector(out, asset.followData) ||
            !WriteVector(out, asset.scalpMesh.positions) || !WriteVector(out, asset.scalpMesh.normals) || !WriteVector(out, asset.scalpMesh.uvs) ||
            !WriteVector(out, asset.scalpMesh.triangles) || !WriteVector(out, asset.scalpMesh.skinWeights) || !WriteRaw(out, asset.scalpMesh.boneCount))
        {
            outError = "Failed while writing binary hair asset.";
            return false;
        }
        return true;
    }

    bool LoadHairAssetFromHairBinary(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open binary hair asset: " + filePath.string();
            return false;
        }
        char magic[8] = {};
        in.read(magic, sizeof(magic));
        const std::string got(magic, magic + sizeof(magic));
        if (got != "HGPUHBIN")
        {
            outError = "Invalid hair binary header.";
            return false;
        }
        uint32_t version = 0;
        if (!ReadRaw(in, version) || version != 1)
        {
            outError = "Unsupported hair binary version.";
            return false;
        }
        if (!ReadRaw(in, outAsset.guideStrandCount) || !ReadRaw(in, outAsset.followerStrandCount) || !ReadRaw(in, outAsset.particlesPerStrand) ||
            !ReadVector(in, outAsset.strands) || !ReadVector(in, outAsset.restPoints) || !ReadVector(in, outAsset.pointWidths) ||
            !ReadVector(in, outAsset.materials) || !ReadVector(in, outAsset.rootBindings) || !ReadVector(in, outAsset.followData) ||
            !ReadVector(in, outAsset.scalpMesh.positions) || !ReadVector(in, outAsset.scalpMesh.normals) || !ReadVector(in, outAsset.scalpMesh.uvs) ||
            !ReadVector(in, outAsset.scalpMesh.triangles) || !ReadVector(in, outAsset.scalpMesh.skinWeights) || !ReadRaw(in, outAsset.scalpMesh.boneCount))
        {
            outError = "Failed while reading binary hair asset.";
            return false;
        }
        return true;
    }
}
