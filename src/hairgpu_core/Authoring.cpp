#include "hairgpu/Authoring.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <limits>
#include <unordered_map>

#include "hairgpu/StrandSampling.h"

#include "hairgpu/CsvUtil.h"
#include "hairgpu/ParseUtil.h"
#include "hairgpu/ProceduralMath.h"
namespace hairgpu
{
    namespace
    {
        constexpr float kEpsilon = 1.0e-6f;
        constexpr float kPi = 3.1415926535f;

        float Clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        float EvaluateSemanticBoost(GroomEditSemantic semantic, GroomEditSemantic target, float boost)
        {
            return (semantic == target) ? boost : 1.0f;
        }

        float WrapAngleRadians(float radians)
        {
            while (radians > kPi)
            {
                radians -= 2.0f * kPi;
            }
            while (radians < -kPi)
            {
                radians += 2.0f * kPi;
            }
            return radians;
        }

        bool TryParseFloatToken(const std::string& token, float& outValue)
        {
            return ::hairgpu::TryParseFloat(token, outValue);
        }

        bool TryParseUIntToken(const std::string& token, uint32_t& outValue)
        {
            return ::hairgpu::TryParseUInt(token, outValue);
        }

        bool TryParseBoolToken(const std::string& token, bool& outValue)
        {
            return ::hairgpu::TryParseBool(token, outValue);
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
        bool ParseEditFalloffZoneToken(const std::string& text, EditFalloffZone& outZone)
        {
            const std::string lowered = ToLower(Trim(text));
            if (lowered.empty() || lowered == "full") { outZone = EditFalloffZone::Full; return true; }
            if (lowered == "root") { outZone = EditFalloffZone::Root; return true; }
            if (lowered == "mid" || lowered == "middle") { outZone = EditFalloffZone::Mid; return true; }
            if (lowered == "tip") { outZone = EditFalloffZone::Tip; return true; }
            return false;
        }

        const char* EditFalloffZoneToken(EditFalloffZone zone)
        {
            switch (zone)
            {
            case EditFalloffZone::Root: return "root";
            case EditFalloffZone::Mid: return "mid";
            case EditFalloffZone::Tip: return "tip";
            default: return "full";
            }
        }

        bool ParseGroomEditSemanticToken(const std::string& text, GroomEditSemantic& outSemantic)
        {
            const std::string lowered = ToLower(Trim(text));
            if (lowered.empty() || lowered == "generic") { outSemantic = GroomEditSemantic::Generic; return true; }
            if (lowered == "comb") { outSemantic = GroomEditSemantic::Comb; return true; }
            if (lowered == "grab") { outSemantic = GroomEditSemantic::Grab; return true; }
            if (lowered == "smooth") { outSemantic = GroomEditSemantic::Smooth; return true; }
            if (lowered == "length") { outSemantic = GroomEditSemantic::Length; return true; }
            if (lowered == "volume") { outSemantic = GroomEditSemantic::Volume; return true; }
            if (lowered == "part") { outSemantic = GroomEditSemantic::Part; return true; }
            if (lowered == "clump") { outSemantic = GroomEditSemantic::Clump; return true; }
            if (lowered == "curl") { outSemantic = GroomEditSemantic::Curl; return true; }
            if (lowered == "freeze") { outSemantic = GroomEditSemantic::Freeze; return true; }
            if (lowered == "strandfix" || lowered == "strand_fix") { outSemantic = GroomEditSemantic::StrandFix; return true; }
            return false;
        }

        const char* GroomEditSemanticToken(GroomEditSemantic semantic)
        {
            switch (semantic)
            {
            case GroomEditSemantic::Comb: return "comb";
            case GroomEditSemantic::Grab: return "grab";
            case GroomEditSemantic::Smooth: return "smooth";
            case GroomEditSemantic::Length: return "length";
            case GroomEditSemantic::Volume: return "volume";
            case GroomEditSemantic::Part: return "part";
            case GroomEditSemantic::Clump: return "clump";
            case GroomEditSemantic::Curl: return "curl";
            case GroomEditSemantic::Freeze: return "freeze";
            case GroomEditSemantic::StrandFix: return "strandfix";
            default: return "generic";
            }
        }

        float EvaluateZoneEnvelope(EditFalloffZone zone, float u)
        {
            u = Clamp01(u);
            switch (zone)
            {
            case EditFalloffZone::Root: return Clamp01(1.0f - u * 2.2f);
            case EditFalloffZone::Mid: return Clamp01(1.0f - std::fabs(u - 0.5f) * 2.6f);
            case EditFalloffZone::Tip: return Clamp01((u - 0.25f) / 0.75f);
            default: return 1.0f;
            }
        }


        const char* RegionModeToToken(RegionSelectMode mode)
        {
            switch (mode)
            {
            case RegionSelectMode::Ellipse: return "ellipse";
            case RegionSelectMode::Rectangle: return "rectangle";
            case RegionSelectMode::Sector: return "sector";
            case RegionSelectMode::All:
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

        const char* StrandSelectionModeToToken(StrandSelectionMode mode)
        {
            switch (mode)
            {
            case StrandSelectionMode::Union: return "union";
            case StrandSelectionMode::Replace: return "replace";
            case StrandSelectionMode::GuideFamily: return "guide_family";
            case StrandSelectionMode::None:
            default: return "none";
            }
        }

        bool AppendSelectedStrand(std::vector<SelectedStrand>& inOutStrands,
                                  uint32_t strandIndex,
                                  float weight)
        {
            for (SelectedStrand& selected : inOutStrands)
            {
                if (selected.strandIndex == strandIndex)
                {
                    selected.weight = std::max(selected.weight, Clamp01(weight));
                    return true;
                }
            }
            inOutStrands.push_back({ strandIndex, Clamp01(weight) });
            return true;
        }

        bool ParseStrandSelectionSpecLocal(const std::string& text, std::vector<uint32_t>& outIndices, std::string& outError)
        {
            outIndices.clear();
            const std::string trimmed = Trim(text);
            if (trimmed.empty())
            {
                outError.clear();
                return true;
            }
            std::string normalized = trimmed;
            for (char& c : normalized)
            {
                if (c == ';' || c == ':' || c == '|')
                {
                    c = ',';
                }
            }
            std::vector<std::string> parts = SplitSimpleCsv(normalized);
            for (std::string part : parts)
            {
                part = Trim(part);
                if (part.empty())
                {
                    continue;
                }
                const size_t dash = part.find('-');
                if (dash != std::string::npos)
                {
                    uint32_t begin = 0u;
                    uint32_t end = 0u;
                    if (!TryParseUIntToken(Trim(part.substr(0u, dash)), begin) || !TryParseUIntToken(Trim(part.substr(dash + 1u)), end))
                    {
                        outError = "Malformed strand selection range: " + part;
                        return false;
                    }
                    if (end < begin)
                    {
                        std::swap(begin, end);
                    }
                    for (uint32_t index = begin; index <= end; ++index)
                    {
                        outIndices.push_back(index);
                        if (index == UINT32_MAX)
                        {
                            break;
                        }
                    }
                    continue;
                }
                uint32_t index = 0u;
                if (!TryParseUIntToken(part, index))
                {
                    outError = "Malformed strand selection token: " + part;
                    return false;
                }
                outIndices.push_back(index);
            }
            std::sort(outIndices.begin(), outIndices.end());
            outIndices.erase(std::unique(outIndices.begin(), outIndices.end()), outIndices.end());
            outError.clear();
            return true;
        }

        std::string FormatStrandSelectionSpecLocal(const std::vector<uint32_t>& strandIndices)
        {
            if (strandIndices.empty())
            {
                return {};
            }
            std::ostringstream out;
            for (size_t i = 0; i < strandIndices.size(); ++i)
            {
                if (i > 0u)
                {
                    out << '|';
                }
                out << strandIndices[i];
            }
            return out.str();
        }

        RegionSelectionResult BuildExplicitStrandSelection(const HairAsset& asset,
                                                           const EditLayer& layer)
        {
            RegionSelectionResult result = {};
            if (layer.strandSelection.empty() || layer.strandSelectionMode == StrandSelectionMode::None)
            {
                return result;
            }

            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            for (uint32_t strandIndex : layer.strandSelection)
            {
                if (strandIndex >= asset.strands.size())
                {
                    continue;
                }
                AppendSelectedStrand(result.strands, strandIndex, layer.strandSelectionWeight);
                if (layer.strandSelectionMode == StrandSelectionMode::GuideFamily)
                {
                    if (strandIndex < guideCount)
                    {
                        for (uint32_t followerIndex = guideCount; followerIndex < asset.strands.size(); ++followerIndex)
                        {
                            if (followerIndex < asset.followData.size() &&
                                asset.followData[followerIndex].parentGuide == static_cast<int32_t>(strandIndex))
                            {
                                AppendSelectedStrand(result.strands,
                                                     followerIndex,
                                                     layer.strandSelectionWeight * std::clamp(asset.followData[followerIndex].followWeight, 0.0f, 1.0f));
                            }
                        }
                    }
                    else if (strandIndex < asset.followData.size())
                    {
                        const FollowData& follow = asset.followData[strandIndex];
                        if (follow.parentGuide >= 0 && static_cast<uint32_t>(follow.parentGuide) < guideCount)
                        {
                            AppendSelectedStrand(result.strands,
                                                 static_cast<uint32_t>(follow.parentGuide),
                                                 layer.strandSelectionWeight);
                        }
                    }
                }
            }
            return result;
        }

        RegionSelectionResult MergeSelections(const RegionSelectionResult& a,
                                              const RegionSelectionResult& b)
        {
            RegionSelectionResult merged = a;
            for (const SelectedStrand& strand : b.strands)
            {
                AppendSelectedStrand(merged.strands, strand.strandIndex, strand.weight);
            }
            return merged;
        }

        RegionSelectionResult BuildLayerSelection(const HairAsset& asset, const EditLayer& layer)
        {
            const RegionSelectionResult regionSelection = SelectRegion(asset, layer.region);
            const RegionSelectionResult explicitSelection = BuildExplicitStrandSelection(asset, layer);
            if (layer.strandSelection.empty() || layer.strandSelectionMode == StrandSelectionMode::None)
            {
                return regionSelection;
            }
            if (layer.strandSelectionMode == StrandSelectionMode::Replace)
            {
                return explicitSelection;
            }
            return MergeSelections(regionSelection, explicitSelection);
        }

        bool LoadEditStackCsvFromStream(std::istream& in, EditStack& outStack, std::string& outError)
        {
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
                if (tokens.size() > 31u && !tokens[31].empty() && !ParseEditFalloffZoneToken(tokens[31], layer.edit.zone))
                {
                    outError = "Malformed edit zone at line " + std::to_string(lineNumber);
                    return false;
                }
                if (tokens.size() > 32u && !tokens[32].empty() && !ParseGroomEditSemanticToken(tokens[32], layer.edit.semantic))
                {
                    outError = "Malformed edit semantic at line " + std::to_string(lineNumber);
                    return false;
                }
                if (!parseFloatOptional(33u, layer.edit.flowAlignStrength, "flowAlignStrength") ||
                    !parseFloatOptional(34u, layer.edit.smoothStrength, "smoothStrength") ||
                    !parseFloatOptional(35u, layer.edit.liftStrength, "liftStrength") ||
                    !parseFloatOptional(36u, layer.edit.pressStrength, "pressStrength") ||
                    !parseFloatOptional(37u, layer.edit.twistStrength, "twistStrength"))
                {
                    return false;
                }
                const bool hasExtendedEditColumns = tokens.size() >= 41u;
                const size_t strandSelectionColumn = hasExtendedEditColumns ? 38u : (tokens.size() >= 34u ? 31u : 26u);
                const size_t strandSelectionModeColumn = hasExtendedEditColumns ? 39u : (tokens.size() >= 34u ? 32u : 27u);
                const size_t strandSelectionWeightColumn = hasExtendedEditColumns ? 40u : (tokens.size() >= 34u ? 33u : 28u);
                if (tokens.size() > strandSelectionColumn && !tokens[strandSelectionColumn].empty())
                {
                    if (!ParseStrandSelectionSpecLocal(tokens[strandSelectionColumn], layer.strandSelection, outError))
                    {
                        outError += " at line " + std::to_string(lineNumber);
                        return false;
                    }
                }
                if (tokens.size() > strandSelectionModeColumn && !tokens[strandSelectionModeColumn].empty() &&
                    !ParseStrandSelectionModeToken(tokens[strandSelectionModeColumn], layer.strandSelectionMode))
                {
                    outError = "Malformed strand selection mode at line " + std::to_string(lineNumber);
                    return false;
                }
                if (tokens.size() > strandSelectionWeightColumn && !tokens[strandSelectionWeightColumn].empty() &&
                    !TryParseFloatToken(tokens[strandSelectionWeightColumn], layer.strandSelectionWeight))
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

        Vec3 MakeSafeAxis(const Vec3& primary, const Vec3& fallback)
        {
            const Vec3 axis = Normalize(primary);
            return Length(axis) > 1.0e-6f ? axis : fallback;
        }

        float Hash01(uint32_t value)
        {
            return ::hairgpu::Hash01Murmur(value);
        }

        Vec3 GetStrandRoot(const HairAsset& asset, uint32_t strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0 || strand.pointOffset >= asset.restPoints.size())
            {
                return {};
            }
            return asset.restPoints[strand.pointOffset];
        }

        Vec2 GetRootUv(const HairAsset& asset, uint32_t strandIndex)
        {
            if (strandIndex < asset.rootBindings.size())
            {
                return asset.rootBindings[strandIndex].rootUv;
            }
            const Vec3 root = GetStrandRoot(asset, strandIndex);
            return {0.5f + 0.5f * root.x, 0.5f + 0.5f * root.z};
        }

        float ComputeFeatheredWeight(float normalizedDistance, float feather)
        {
            normalizedDistance = Clamp01(normalizedDistance);
            const float safeFeather = std::max(feather, 1.0e-4f);
            const float start = Clamp01(1.0f - safeFeather);
            if (normalizedDistance <= start)
            {
                return 1.0f;
            }
            const float t = Clamp01((normalizedDistance - start) / safeFeather);
            return 1.0f - SmoothStep01(t);
        }

        bool StrandMatchesRegion(const HairAsset& asset, uint32_t strandIndex, const RegionSelectionSettings& settings, float& outWeight)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            if (settings.groupId != UINT32_MAX && strand.groupId != settings.groupId)
            {
                return false;
            }
            if (settings.guidesOnly && (strandIndex >= asset.guideStrandCount))
            {
                return false;
            }

            if (settings.mode == RegionSelectMode::All)
            {
                outWeight = 1.0f;
                return true;
            }

            const Vec2 uv = GetRootUv(asset, strandIndex);
            const float dx = uv.x - settings.centerUv.x;
            const float dy = uv.y - settings.centerUv.y;
            const float rx = std::max(settings.radiusUv.x, 1.0e-4f);
            const float ry = std::max(settings.radiusUv.y, 1.0e-4f);
            const float feather = Clamp01(settings.feather);

            if (settings.mode == RegionSelectMode::Ellipse)
            {
                const float d = std::sqrt((dx * dx) / (rx * rx) + (dy * dy) / (ry * ry));
                if (d > 1.0f)
                {
                    return false;
                }
                outWeight = ComputeFeatheredWeight(d, feather);
                return true;
            }

            if (settings.mode == RegionSelectMode::Rectangle)
            {
                const float nx = std::abs(dx) / rx;
                const float ny = std::abs(dy) / ry;
                const float d = std::max(nx, ny);
                if (d > 1.0f)
                {
                    return false;
                }
                outWeight = ComputeFeatheredWeight(d, feather);
                return true;
            }

            if (settings.mode == RegionSelectMode::Sector)
            {
                const float radial = std::sqrt((dx * dx) / (rx * rx) + (dy * dy) / (ry * ry));
                if (radial > 1.0f || radial < std::max(settings.innerRadius, 0.0f))
                {
                    return false;
                }
                const float angle = std::atan2(dy, dx);
                const float delta = std::abs(WrapAngleRadians(angle - settings.angleCenterRadians));
                const float halfExtent = std::max(settings.angleHalfExtentRadians, 1.0e-4f);
                if (delta > halfExtent)
                {
                    return false;
                }
                const float radialWeight = ComputeFeatheredWeight(radial, feather);
                const float angularWeight = ComputeFeatheredWeight(delta / halfExtent, feather);
                outWeight = std::sqrt(std::max(radialWeight * angularWeight, 0.0f));
                return true;
            }

            return false;
        }

        RegionSelectionResult ScaleSelectionWeights(const RegionSelectionResult& selection, float scale)
        {
            RegionSelectionResult scaled = selection;
            for (SelectedStrand& strand : scaled.strands)
            {
                strand.weight = Clamp01(strand.weight * scale);
            }
            scaled.strands.erase(std::remove_if(scaled.strands.begin(), scaled.strands.end(), [](const SelectedStrand& strand)
            {
                return strand.weight <= 1.0e-4f;
            }), scaled.strands.end());
            return scaled;
        }

        void AccumulateSelectionWeights(std::vector<float>& inOutWeights,
                                        const HairAsset& asset,
                                        const RegionSelectionResult& selection,
                                        float layerWeight)
        {
            if (inOutWeights.size() != asset.strands.size())
            {
                inOutWeights.assign(asset.strands.size(), 0.0f);
            }
            for (const SelectedStrand& strand : selection.strands)
            {
                if (strand.strandIndex >= inOutWeights.size())
                {
                    continue;
                }
                inOutWeights[strand.strandIndex] = std::max(inOutWeights[strand.strandIndex], Clamp01(strand.weight * layerWeight));
            }
        }


        Vec3 ComputeBoundaryTangent(const HairAsset& asset, const StrandRange& strand, uint32_t localIndex)
        {
            if (strand.pointCount == 0u)
            {
                return Vec3(0.0f, 0.0f, 0.0f);
            }
            const uint32_t clampedIndex = std::min<uint32_t>(localIndex, strand.pointCount - 1u);
            if (strand.pointCount == 1u)
            {
                return Vec3(0.0f, 1.0f, 0.0f);
            }
            const uint32_t prevIndex = (clampedIndex > 0u) ? (clampedIndex - 1u) : 0u;
            const uint32_t nextIndex = std::min<uint32_t>(clampedIndex + 1u, strand.pointCount - 1u);
            const Vec3 prevPoint = asset.restPoints[strand.pointOffset + prevIndex];
            const Vec3 nextPoint = asset.restPoints[strand.pointOffset + nextIndex];
            return Normalize(nextPoint - prevPoint);
        }

        void ApplyBoundarySmoothing(HairAsset& asset,
                                    const std::vector<float>& strandInfluence,
                                    float radiusUv,
                                    float smoothingStrength)
        {
            const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
            if (guideCount == 0u || strandInfluence.size() < guideCount || smoothingStrength <= 1.0e-4f)
            {
                return;
            }

            const float safeRadius = std::max(radiusUv, 1.0e-4f);
            std::vector<Vec2> guideUvs(guideCount);
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                guideUvs[guideIndex] = GetRootUv(asset, guideIndex);
            }

            std::vector<Vec3> originalPoints = asset.restPoints;
            std::vector<Vec3> smoothedPoints = asset.restPoints;
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const StrandRange& strand = asset.strands[guideIndex];
                if (strand.pointCount < 2u)
                {
                    continue;
                }

                const float influence = Clamp01(strandInfluence[guideIndex]);
                const float boundary = Clamp01(1.0f - std::abs(influence - 0.5f) * 2.0f);
                if (boundary <= 1.0e-3f)
                {
                    continue;
                }
                const Vec2 uv = guideUvs[guideIndex];
                std::vector<std::pair<float, uint32_t>> neighbors;
                for (uint32_t otherGuide = 0u; otherGuide < guideCount; ++otherGuide)
                {
                    if (otherGuide == guideIndex)
                    {
                        continue;
                    }
                    if (asset.strands[otherGuide].groupId != asset.strands[guideIndex].groupId)
                    {
                        continue;
                    }
                    const float du = guideUvs[otherGuide].x - uv.x;
                    const float dv = guideUvs[otherGuide].y - uv.y;
                    const float distance = std::sqrt(du * du + dv * dv);
                    if (distance > safeRadius)
                    {
                        continue;
                    }
                    neighbors.emplace_back(distance, otherGuide);
                }
                if (neighbors.empty())
                {
                    continue;
                }
                std::sort(neighbors.begin(), neighbors.end(), [](const auto& a, const auto& b)
                {
                    return a.first < b.first;
                });
                if (neighbors.size() > 6u)
                {
                    neighbors.resize(6u);
                }

                for (uint32_t localIndex = 1u; localIndex < strand.pointCount; ++localIndex)
                {
                    const float u = static_cast<float>(localIndex) / static_cast<float>(std::max<uint32_t>(strand.pointCount - 1u, 1u));
                    Vec3 weightedAverage = originalPoints[strand.pointOffset + localIndex];
                    Vec3 weightedTangent = ComputeBoundaryTangent(asset, strand, localIndex);
                    float totalWeight = 1.0f;
                    for (const auto& [distance, otherGuide] : neighbors)
                    {
                        const StrandRange& neighborStrand = asset.strands[otherGuide];
                        const uint32_t neighborLocalIndex = std::min<uint32_t>(localIndex, neighborStrand.pointCount - 1u);
                        const float neighborWeight = Clamp01(1.0f - distance / safeRadius);
                        weightedAverage += originalPoints[neighborStrand.pointOffset + neighborLocalIndex] * neighborWeight;
                        weightedTangent += ComputeBoundaryTangent(asset, neighborStrand, neighborLocalIndex) * neighborWeight;
                        totalWeight += neighborWeight;
                    }
                    weightedAverage = weightedAverage / std::max(totalWeight, 1.0e-4f);
                    const Vec3 tangent = Normalize(weightedTangent / std::max(totalWeight, 1.0e-4f));

                    const uint32_t pointIndex = strand.pointOffset + localIndex;
                    const uint32_t prevPointIndex = strand.pointOffset + localIndex - 1u;
                    const float restSegmentLength = Length(originalPoints[pointIndex] - originalPoints[prevPointIndex]);
                    Vec3 tangentTarget = weightedAverage;
                    if (Length(tangent) > 1.0e-6f && restSegmentLength > 1.0e-6f)
                    {
                        tangentTarget = smoothedPoints[prevPointIndex] + tangent * restSegmentLength;
                    }
                    Vec3 curvatureTarget = weightedAverage;
                    if (localIndex + 1u < strand.pointCount)
                    {
                        const Vec3 nextOriginal = originalPoints[strand.pointOffset + localIndex + 1u];
                        const Vec3 prevOriginal = originalPoints[prevPointIndex];
                        curvatureTarget = (weightedAverage + nextOriginal + prevOriginal) / 3.0f;
                    }

                    const float blend = Clamp01(smoothingStrength * boundary * (0.20f + 0.80f * u));
                    Vec3 smoothed = Lerp(originalPoints[pointIndex], weightedAverage, blend * 0.55f);
                    smoothed = Lerp(smoothed, tangentTarget, blend * 0.30f);
                    smoothed = Lerp(smoothed, curvatureTarget, blend * 0.15f);
                    smoothedPoints[pointIndex] = smoothed;
                }
            }

            asset.restPoints.swap(smoothedPoints);

            for (uint32_t strandIndex = guideCount; strandIndex < asset.strands.size(); ++strandIndex)
            {
                if (strandIndex >= asset.followData.size())
                {
                    continue;
                }
                const FollowData& follow = asset.followData[strandIndex];
                if (follow.parentGuide < 0 || static_cast<uint32_t>(follow.parentGuide) >= guideCount)
                {
                    continue;
                }
                const StrandRange& guide = asset.strands[static_cast<uint32_t>(follow.parentGuide)];
                const StrandRange& follower = asset.strands[strandIndex];
                const uint32_t pointCount = std::min(guide.pointCount, follower.pointCount);
                const float parentInfluence = Clamp01(strandInfluence[static_cast<uint32_t>(follow.parentGuide)]);
                const float boundary = Clamp01(1.0f - std::abs(parentInfluence - 0.5f) * 2.0f);
                const float blend = Clamp01((0.18f + 0.62f * boundary) * std::clamp(follow.followWeight, 0.0f, 1.0f));
                for (uint32_t localIndex = 0u; localIndex < pointCount; ++localIndex)
                {
                    const uint32_t gi = guide.pointOffset + localIndex;
                    const uint32_t si = follower.pointOffset + localIndex;
                    const float u = (pointCount > 1u) ? static_cast<float>(localIndex) / static_cast<float>(pointCount - 1u) : 0.0f;
                    const float localBlend = Clamp01(blend * (0.35f + 0.65f * u));
                    asset.restPoints[si] = Lerp(originalPoints[si], asset.restPoints[gi], localBlend);
                }
            }
        }

        Vec3 SamplePalette(const Palette& palette, float u, float& outWidthScale)
        {
            outWidthScale = 1.0f;
            if (palette.samples.empty())
            {
                return {};
            }
            if (palette.samples.size() == 1)
            {
                outWidthScale = palette.samples.front().widthScale;
                return palette.samples.front().offset;
            }

            const float t = Clamp01(u);
            size_t upper = 0;
            while (upper + 1 < palette.samples.size() && palette.samples[upper + 1].u < t)
            {
                ++upper;
            }
            const size_t next = std::min(upper + 1, palette.samples.size() - 1);
            const float u0 = palette.samples[upper].u;
            const float u1 = palette.samples[next].u;
            const float alpha = (std::abs(u1 - u0) > kEpsilon) ? Clamp01((t - u0) / (u1 - u0)) : 0.0f;
            outWidthScale = Lerp(palette.samples[upper].widthScale, palette.samples[next].widthScale, alpha);
            return Lerp(palette.samples[upper].offset, palette.samples[next].offset, alpha);
        }
    }

    RegionSelectionResult SelectRegion(const HairAsset& asset, const RegionSelectionSettings& settings)
    {
        RegionSelectionResult result;
        result.strands.reserve(asset.strands.size());
        for (uint32_t i = 0; i < asset.strands.size(); ++i)
        {
            float weight = 0.0f;
            if (StrandMatchesRegion(asset, i, settings, weight))
            {
                result.strands.push_back({i, std::clamp(weight, 0.05f, 1.0f)});
            }
        }
        return result;
    }

    bool ParseStrandSelectionSpec(const std::string& text, std::vector<uint32_t>& outIndices, std::string& outError)
    {
        return ParseStrandSelectionSpecLocal(text, outIndices, outError);
    }

    std::string FormatStrandSelectionSpec(const std::vector<uint32_t>& strandIndices)
    {
        return FormatStrandSelectionSpecLocal(strandIndices);
    }

    bool ApplyEditToRegion(HairAsset& asset, const RegionSelectionResult& selection, const EditSettings& settings, std::string& outError)
    {
        outError.clear();
        if (selection.strands.empty())
        {
            outError = "No strands selected.";
            return false;
        }

        const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
        const std::vector<Vec3> originalPoints = asset.restPoints;

        for (const SelectedStrand& selected : selection.strands)
        {
            if (selected.strandIndex >= asset.strands.size())
            {
                continue;
            }
            const StrandRange& strand = asset.strands[selected.strandIndex];
            if (strand.pointCount == 0u)
            {
                continue;
            }

            const Vec3 root = originalPoints[strand.pointOffset];
            const Vec3 tip = originalPoints[strand.pointOffset + strand.pointCount - 1u];
            const Vec3 rootToTip = MakeSafeAxis(tip - root, Vec3(0.0f, -1.0f, 0.0f));
            Vec3 lateral = Normalize(Cross(Vec3(0.0f, 1.0f, 0.0f), rootToTip));
            if (Length(lateral) <= 1.0e-6f)
            {
                lateral = Vec3(1.0f, 0.0f, 0.0f);
            }
            Vec3 binormal = Normalize(Cross(rootToTip, lateral));
            if (Length(binormal) <= 1.0e-6f)
            {
                binormal = Vec3(0.0f, 0.0f, 1.0f);
            }

            const Vec2 rootUv = GetRootUv(asset, selected.strandIndex);
            const float selectionWeight = Clamp01(selected.weight);
            const float partWidth = std::max(settings.partingWidthUv, 1.0e-4f);
            const float partDistance = std::fabs(rootUv.x - settings.partingCenterU);
            const float partLineWeight = Clamp01(1.0f - partDistance / partWidth);
            const float partDirection = (rootUv.x >= settings.partingCenterU) ? 1.0f : -1.0f;
            const float frontBias = Clamp01(1.15f - rootUv.y * 1.25f);
            const float strandPhase = 6.2831853f * Hash01(selected.strandIndex * 1664525u + 1013904223u);
            const float guideFamilyStrength = (selected.strandIndex < asset.followData.size()) ? Clamp(asset.followData[selected.strandIndex].clumpWeight, 0.0f, 1.0f) : 0.0f;

            for (uint32_t localIndex = 0u; localIndex < strand.pointCount; ++localIndex)
            {
                const float u = (strand.pointCount > 1u) ? static_cast<float>(localIndex) / static_cast<float>(strand.pointCount - 1u) : 0.0f;
                const float zoneEnvelope = EvaluateZoneEnvelope(settings.zone, u);
                const float baseEditGain = selectionWeight * zoneEnvelope;
                const float semanticCombBoost = EvaluateSemanticBoost(settings.semantic, GroomEditSemantic::Comb, 1.15f);
                const float semanticSmoothBoost = EvaluateSemanticBoost(settings.semantic, GroomEditSemantic::Smooth, 1.20f);
                const float semanticPartBoost = EvaluateSemanticBoost(settings.semantic, GroomEditSemantic::Part, 1.15f);
                const float semanticClumpBoost = EvaluateSemanticBoost(settings.semantic, GroomEditSemantic::Clump, 1.20f);
                const float semanticCurlBoost = EvaluateSemanticBoost(settings.semantic, GroomEditSemantic::Curl, 1.20f);
                const float combEditGain = baseEditGain * semanticCombBoost;
                const float smoothEditGain = baseEditGain * semanticSmoothBoost;
                const float partEditGain = baseEditGain * semanticPartBoost;
                const float clumpEditGain = baseEditGain * semanticClumpBoost;
                const float curlEditGain = baseEditGain * semanticCurlBoost;
                const float normalOffsetStrength =
                    (std::fabs(settings.normalOffsetStrength) > 1.0e-6f)
                    ? settings.normalOffsetStrength
                    : (settings.liftStrength - settings.pressStrength);

                Vec3 p = originalPoints[strand.pointOffset + localIndex] - root;
                p = ::hairgpu::RotateYaw(p, settings.yawRadians * baseEditGain);
                p = p * Lerp(1.0f, settings.lengthScale, u * baseEditGain);

                Vec3 additive = Lerp(settings.rootOffset, settings.tipOffset, u) * baseEditGain;
                Vec3 side = lateral * (settings.sideOffset * baseEditGain);
                side += binormal * (normalOffsetStrength * baseEditGain * (0.25f + 0.75f * (1.0f - u)));

                const float curlEnvelope = curlEditGain * settings.curlStrength * (0.20f + 0.80f * u) * u;
                const float curlPhase = strandPhase + u * (10.0f + 4.0f * settings.twistStrength);
                Vec3 curlOffset = (lateral * std::sin(curlPhase) + binormal * std::cos(curlPhase)) * curlEnvelope;
                if (settings.twistStrength != 0.0f)
                {
                    curlOffset += Normalize(Cross(rootToTip, lateral)) * (curlEnvelope * settings.twistStrength * 0.20f);
                }

                const float partingEnvelope = partEditGain * settings.partingStrength * partLineWeight * frontBias * (1.0f - 0.75f * u);
                const Vec3 partingOffset = lateral * (partDirection * partingEnvelope) + Vec3(0.0f, partingEnvelope * 0.18f, 0.0f);

                float clumpEnvelope = clumpEditGain * settings.clumpStrength * (0.10f + 0.90f * u) * (0.35f + 0.65f * guideFamilyStrength);
                Vec3 clumpOffset = {};
                if (selected.strandIndex >= guideCount && selected.strandIndex < asset.followData.size())
                {
                    const FollowData& follow = asset.followData[selected.strandIndex];
                    if (follow.parentGuide >= 0 && static_cast<uint32_t>(follow.parentGuide) < guideCount)
                    {
                        const StrandRange& guide = asset.strands[static_cast<uint32_t>(follow.parentGuide)];
                        const uint32_t guideLocalIndex = std::min<uint32_t>(localIndex, guide.pointCount - 1u);
                        const uint32_t guidePointIndex = guide.pointOffset + guideLocalIndex;
                        const Vec3 guidePoint = originalPoints[guidePointIndex];
                        const Vec3 familyDelta = guidePoint - originalPoints[strand.pointOffset + localIndex];
                        clumpEnvelope *= std::clamp(follow.followWeight, 0.0f, 1.0f);
                        clumpOffset = familyDelta * Clamp01(clumpEnvelope);
                    }
                }

                if (settings.flowAlignStrength > 0.0f && localIndex > 0u)
                {
                    const Vec3 currentDir = Normalize(p);
                    const float flowBlend = Clamp01(settings.flowAlignStrength * combEditGain);
                    const Vec3 targetDir = Normalize(Lerp(currentDir, rootToTip, flowBlend));
                    const float segLen = std::max(Length(p), 1.0e-5f);
                    p = targetDir * segLen;
                }
                if (settings.smoothStrength > 0.0f && localIndex > 0u && localIndex + 1u < strand.pointCount)
                {
                    const Vec3 prevRest = originalPoints[strand.pointOffset + localIndex - 1u] - root;
                    const Vec3 nextRest = originalPoints[strand.pointOffset + localIndex + 1u] - root;
                    const Vec3 smoothTarget = (prevRest + nextRest) * 0.5f;
                    p = Lerp(p, smoothTarget, Clamp01(settings.smoothStrength * smoothEditGain));
                }
                asset.restPoints[strand.pointOffset + localIndex] = root + p + additive + side + curlOffset + partingOffset + clumpOffset;
                if (strand.pointOffset + localIndex < asset.pointWidths.size())
                {
                    const float widthBlend = Clamp01(baseEditGain * (0.70f + 0.30f * (1.0f - std::clamp(settings.clumpStrength, 0.0f, 1.0f) * 0.35f)));
                    asset.pointWidths[strand.pointOffset + localIndex] *= Lerp(1.0f, settings.widthScale, widthBlend);
                }
            }
        }

        for (uint32_t strandIndex = guideCount; strandIndex < asset.strands.size(); ++strandIndex)
        {
            if (strandIndex >= asset.followData.size())
            {
                continue;
            }
            const FollowData& follow = asset.followData[strandIndex];
            if (follow.parentGuide < 0 || static_cast<uint32_t>(follow.parentGuide) >= guideCount)
            {
                continue;
            }
            auto it = std::find_if(selection.strands.begin(), selection.strands.end(), [&](const SelectedStrand& s)
            {
                return s.strandIndex == static_cast<uint32_t>(follow.parentGuide);
            });
            if (it == selection.strands.end())
            {
                continue;
            }

            const float familyBlend = Clamp01(it->weight * settings.followerInfluence * std::clamp(follow.followWeight, 0.0f, 1.0f));
            const float clumpBlend = Clamp01(familyBlend * (0.35f + 0.65f * settings.clumpStrength) * (0.45f + 0.55f * Clamp(follow.clumpWeight, 0.0f, 1.0f)));
            const StrandRange& guide = asset.strands[static_cast<uint32_t>(follow.parentGuide)];
            const StrandRange& strand = asset.strands[strandIndex];
            const uint32_t count = std::min(guide.pointCount, strand.pointCount);
            for (uint32_t localIndex = 0u; localIndex < count; ++localIndex)
            {
                const uint32_t gi = guide.pointOffset + localIndex;
                const uint32_t si = strand.pointOffset + localIndex;
                const float u = (count > 1u) ? static_cast<float>(localIndex) / static_cast<float>(count - 1u) : 0.0f;
                const Vec3 restOffset = originalPoints[si] - originalPoints[gi];
                const Vec3 guideDrivenTarget = asset.restPoints[gi] + restOffset * (1.0f - 0.55f * settings.clumpStrength);
                const float localBlend = Clamp01(Lerp(familyBlend, clumpBlend, u));
                asset.restPoints[si] = Lerp(asset.restPoints[si], guideDrivenTarget, localBlend);
                if (si < asset.pointWidths.size() && gi < asset.pointWidths.size())
                {
                    asset.pointWidths[si] = Lerp(asset.pointWidths[si], asset.pointWidths[gi], familyBlend);
                }
            }
        }
        return true;
    }

    bool LoadEditStackCsv(const std::filesystem::path& path, EditStack& outStack, std::string& outError)
    {
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open edit stack CSV: " + path.string();
            return false;
        }
        return LoadEditStackCsvFromStream(in, outStack, outError);
    }

    bool LoadEditStackCsvText(const std::string& text, EditStack& outStack, std::string& outError)
    {
        std::istringstream in(text);
        return LoadEditStackCsvFromStream(in, outStack, outError);
    }

    bool WriteEditStackCsv(const std::filesystem::path& path, const EditStack& stack, std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to open edit stack CSV for writing: " + path.string();
            return false;
        }

        out << std::fixed << std::setprecision(6);
        out << "# edit stack csv\n";
        out << "# stack,boundarySmoothing,boundaryRadiusUv\n";
        out << "stack," << stack.boundarySmoothing << "," << stack.boundaryRadiusUv << "\n";
        out << "# layer,name,enabled,layerWeight,mode,cx,cy,rx,ry,innerRadius,angleCenterRadians,angleHalfExtentRadians,feather,groupId,guidesOnly,lengthScale,widthScale,rootOffsetX,rootOffsetY,rootOffsetZ,tipOffsetX,tipOffsetY,tipOffsetZ,sideOffset,yawRadians,followerInfluence,clumpStrength,curlStrength,partingStrength,partingCenterU,partingWidthUv,zone,semantic,flowAlignStrength,smoothStrength,liftStrength,pressStrength,normalOffsetStrength,twistStrength,strandSelection,strandSelectionMode,strandSelectionWeight\n";
        for (const EditLayer& layer : stack.layers)
        {
            out << "layer,"
                << EscapeCsvToken(layer.name) << ","
                << (layer.enabled ? 1 : 0) << ","
                << layer.layerWeight << ","
                << RegionModeToToken(layer.region.mode) << ","
                << layer.region.centerUv.x << ","
                << layer.region.centerUv.y << ","
                << layer.region.radiusUv.x << ","
                << layer.region.radiusUv.y << ","
                << layer.region.innerRadius << ","
                << layer.region.angleCenterRadians << ","
                << layer.region.angleHalfExtentRadians << ","
                << layer.region.feather << ",";
            if (layer.region.groupId != UINT32_MAX)
            {
                out << layer.region.groupId;
            }
            out << ","
                << (layer.region.guidesOnly ? 1 : 0) << ","
                << layer.edit.lengthScale << ","
                << layer.edit.widthScale << ","
                << layer.edit.rootOffset.x << ","
                << layer.edit.rootOffset.y << ","
                << layer.edit.rootOffset.z << ","
                << layer.edit.tipOffset.x << ","
                << layer.edit.tipOffset.y << ","
                << layer.edit.tipOffset.z << ","
                << layer.edit.sideOffset << ","
                << layer.edit.yawRadians << ","
                << layer.edit.followerInfluence << ","
                << layer.edit.clumpStrength << ","
                << layer.edit.curlStrength << ","
                << layer.edit.partingStrength << ","
                << layer.edit.partingCenterU << ","
                << layer.edit.partingWidthUv << ","
                << EscapeCsvToken(FormatStrandSelectionSpecLocal(layer.strandSelection)) << ","
                << StrandSelectionModeToToken(layer.strandSelectionMode) << ","
                << layer.strandSelectionWeight << "\n";
        }

        outError.clear();
        return true;
    }

    bool WriteEditStackTemplateCsv(const std::filesystem::path& path, std::string& outError)
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
        out << "# layer,name,enabled,layerWeight,mode,cx,cy,rx,ry,innerRadius,angleCenterRadians,angleHalfExtentRadians,feather,groupId,guidesOnly,lengthScale,widthScale,rootOffsetX,rootOffsetY,rootOffsetZ,tipOffsetX,tipOffsetY,tipOffsetZ,sideOffset,yawRadians,followerInfluence,clumpStrength,curlStrength,partingStrength,partingCenterU,partingWidthUv,zone,semantic,flowAlignStrength,smoothStrength,liftStrength,pressStrength,normalOffsetStrength,twistStrength,strandSelection,strandSelectionMode,strandSelectionWeight\n";
        out << "layer,front_sector,1,1.0,sector,0.50,0.50,0.50,0.50,0.00,0.0000000,3.1415926,0.20,,1,1.05,1.00,0.00,-0.01,0.00,0.00,-0.03,0.00,0.00,0.05,1.00,0.18,0.00,0.012,0.50,0.14,,,1.00\n";
        out << "layer,tip_refine,1,0.85,ellipse,0.50,0.50,0.16,0.16,0.00,0.00,3.1415926,0.35,,1,0.97,1.00,0.00,0.00,0.00,0.00,0.02,0.00,0.01,-0.08,0.85,0.10,0.018,0.000,0.50,0.14,,,1.00\n";
        out << "layer,single_strand_fix,1,1.00,all,0.50,0.50,0.20,0.20,0.00,0.00,3.1415926,0.20,,0,1.00,1.00,0.00,0.00,0.00,0.00,0.01,0.00,0.00,0.10,0.00,0.00,0.00,0.000,0.50,0.14,42,replace,1.00\n";
        outError.clear();
        return true;
    }

    RuntimeEditState BuildRuntimeEditState(const HairAsset& asset, const EditStack& stack)
    {
        RuntimeEditState state = {};
        state.strandInfluence.assign(asset.strands.size(), 0.0f);
        for (const EditLayer& layer : stack.layers)
        {
            if (!layer.enabled || layer.layerWeight <= 1.0e-4f)
            {
                continue;
            }
            const RegionSelectionResult selection = BuildLayerSelection(asset, layer);
            AccumulateSelectionWeights(state.strandInfluence, asset, selection, layer.layerWeight);
        }

        const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
        state.guideInfluence.assign(guideCount, 0.0f);
        for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
        {
            state.guideInfluence[guideIndex] = state.strandInfluence[guideIndex];
            state.maxGuideInfluence = std::max(state.maxGuideInfluence, state.guideInfluence[guideIndex]);
            if (state.guideInfluence[guideIndex] > 1.0e-4f)
            {
                ++state.activeGuideCount;
            }
        }

        for (uint32_t strandIndex = guideCount; strandIndex < asset.strands.size(); ++strandIndex)
        {
            if (strandIndex < asset.followData.size())
            {
                const FollowData& follow = asset.followData[strandIndex];
                if (follow.parentGuide >= 0 && static_cast<uint32_t>(follow.parentGuide) < guideCount)
                {
                    const uint32_t parentGuide = static_cast<uint32_t>(follow.parentGuide);
                    if (state.strandInfluence[strandIndex] > 1.0e-4f)
                    {
                        state.guideInfluence[parentGuide] = std::max(state.guideInfluence[parentGuide], state.strandInfluence[strandIndex]);
                    }
                    state.strandInfluence[strandIndex] = std::max(state.strandInfluence[strandIndex], state.guideInfluence[parentGuide]);
                }
            }
        }
        state.maxGuideInfluence = 0.0f;
        state.activeGuideCount = 0u;
        for (float influence : state.guideInfluence)
        {
            state.maxGuideInfluence = std::max(state.maxGuideInfluence, influence);
            if (influence > 1.0e-4f)
            {
                ++state.activeGuideCount;
            }
        }
        return state;
    }

    bool ApplyEditStackToAsset(HairAsset& asset,
                               const HairAsset& baseAsset,
                               const EditStack& stack,
                               RuntimeEditState* outState,
                               std::string& outError)
    {
        outError.clear();
        asset = baseAsset;
        if (stack.layers.empty())
        {
            if (outState)
            {
                *outState = RuntimeEditState{};
                outState->strandInfluence.assign(asset.strands.size(), 0.0f);
                outState->guideInfluence.assign(std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size())), 0.0f);
            }
            return true;
        }

        bool appliedAnyLayer = false;
        for (const EditLayer& layer : stack.layers)
        {
            if (!layer.enabled || layer.layerWeight <= 1.0e-4f)
            {
                continue;
            }
            RegionSelectionResult selection = BuildLayerSelection(asset, layer);
            selection = ScaleSelectionWeights(selection, layer.layerWeight);
            if (selection.strands.empty())
            {
                continue;
            }
            if (!ApplyEditToRegion(asset, selection, layer.edit, outError))
            {
                return false;
            }
            appliedAnyLayer = true;
        }

        RuntimeEditState state = BuildRuntimeEditState(baseAsset, stack);
        if (appliedAnyLayer)
        {
            ApplyBoundarySmoothing(asset, state.strandInfluence, stack.boundaryRadiusUv, stack.boundarySmoothing);
        }

        if (outState)
        {
            *outState = std::move(state);
        }
        return true;
    }

    namespace
    {
        float SafeAcos(float v)
        {
            return std::acos(std::clamp(v, -1.0f, 1.0f));
        }

        void EnforceSegmentLengths(HairAsset& asset, const StrandRange& strand, const std::vector<float>& segmentLengths)
        {
            if (strand.pointCount < 2u)
            {
                return;
            }
            for (uint32_t localIndex = 1u; localIndex < strand.pointCount; ++localIndex)
            {
                const uint32_t prevIndex = strand.pointOffset + localIndex - 1u;
                const uint32_t currIndex = strand.pointOffset + localIndex;
                const float targetLength = segmentLengths[localIndex - 1u];
                Vec3 delta = asset.restPoints[currIndex] - asset.restPoints[prevIndex];
                const float length = Length(delta);
                if (length <= kEpsilon)
                {
                    delta = Vec3(0.0f, -targetLength, 0.0f);
                }
                else
                {
                    delta = delta * (targetLength / length);
                }
                asset.restPoints[currIndex] = asset.restPoints[prevIndex] + delta;
            }
        }
    }

    void ApplyConstraints(HairAsset& asset, const ConstraintSettings& settings)
    {
        for (const StrandRange& strand : asset.strands)
        {
            if (strand.pointCount == 0)
            {
                continue;
            }
            const Vec3 root = asset.restPoints[strand.pointOffset];
            float restSegmentLength = 0.0f;
            if (strand.pointCount > 1)
            {
                restSegmentLength = Length(asset.restPoints[strand.pointOffset + 1] - asset.restPoints[strand.pointOffset]);
            }
            for (uint32_t localIndex = 0; localIndex < strand.pointCount; ++localIndex)
            {
                Vec3& p = asset.restPoints[strand.pointOffset + localIndex];
                p.y = std::max(p.y, settings.floorY);
                Vec3 radial = p - root;
                const float radialLength = Length(radial);
                if (radialLength > settings.maxRootRadius)
                {
                    p = root + radial * (settings.maxRootRadius / std::max(radialLength, kEpsilon));
                }
                if (localIndex > 0)
                {
                    Vec3 prev = asset.restPoints[strand.pointOffset + localIndex - 1];
                    Vec3 delta = p - prev;
                    const float length = Length(delta);
                    const float maxLen = std::max(restSegmentLength, kEpsilon) * settings.maxStretchRatio;
                    if (length > maxLen)
                    {
                        p = prev + delta * (maxLen / std::max(length, kEpsilon));
                    }
                }
                if (localIndex > 1)
                {
                    const Vec3 a = Normalize(asset.restPoints[strand.pointOffset + localIndex - 1] - asset.restPoints[strand.pointOffset + localIndex - 2]);
                    const Vec3 b = Normalize(asset.restPoints[strand.pointOffset + localIndex] - asset.restPoints[strand.pointOffset + localIndex - 1]);
                    float d = std::clamp(Dot(a, b), -1.0f, 1.0f);
                    const float angle = std::acos(d);
                    if (angle > settings.maxBendRadians)
                    {
                        const float reduce = settings.maxBendRadians / std::max(angle, kEpsilon);
                        Vec3 dir = Normalize(Lerp(a, b, reduce));
                        const float len = Length(asset.restPoints[strand.pointOffset + localIndex] - asset.restPoints[strand.pointOffset + localIndex - 1]);
                        p = asset.restPoints[strand.pointOffset + localIndex - 1] + dir * len;
                    }
                }
            }
        }
    }

    bool LoadPaletteCsv(const std::filesystem::path& path, Palette& outPalette, std::string& outError)
    {
        outPalette.samples.clear();
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open palette csv: " + path.string();
            return false;
        }
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }
            std::stringstream ss(line);
            std::string tok;
            std::vector<std::string> cols;
            while (std::getline(ss, tok, ',')) cols.push_back(tok);
            if (cols.size() < 4)
            {
                continue;
            }
            PaletteSample sample;
            sample.u = std::stof(cols[0]);
            sample.offset.x = std::stof(cols[1]);
            sample.offset.y = std::stof(cols[2]);
            sample.offset.z = std::stof(cols[3]);
            if (cols.size() >= 5) sample.widthScale = std::stof(cols[4]);
            outPalette.samples.push_back(sample);
        }
        std::sort(outPalette.samples.begin(), outPalette.samples.end(), [](const PaletteSample& a, const PaletteSample& b) { return a.u < b.u; });
        if (outPalette.samples.empty())
        {
            outError = "Palette csv did not contain any valid samples.";
            return false;
        }
        return true;
    }

    void ApplyAngularBendRelaxation(HairAsset& asset, const AngularBendRelaxationSettings& settings)
    {
        const float maxBend = std::max(0.01f, settings.maxBendRadians);
        const float smoothing = Clamp01(settings.smoothing);
        for (const StrandRange& strand : asset.strands)
        {
            if (strand.pointCount < 3u || strand.pointOffset + strand.pointCount > asset.restPoints.size())
            {
                continue;
            }

            std::vector<float> segmentLengths;
            segmentLengths.reserve(strand.pointCount - 1u);
            for (uint32_t localIndex = 1u; localIndex < strand.pointCount; ++localIndex)
            {
                const Vec3 a = asset.restPoints[strand.pointOffset + localIndex - 1u];
                const Vec3 b = asset.restPoints[strand.pointOffset + localIndex];
                segmentLengths.push_back(std::max(Length(b - a), 1.0e-4f));
            }

            for (uint32_t iteration = 0u; iteration < std::max<uint32_t>(1u, settings.iterations); ++iteration)
            {
                std::vector<Vec3> original(strand.pointCount);
                for (uint32_t localIndex = 0u; localIndex < strand.pointCount; ++localIndex)
                {
                    original[localIndex] = asset.restPoints[strand.pointOffset + localIndex];
                }

                for (uint32_t localIndex = settings.affectRootAdjacentPoint ? 1u : 2u; localIndex + 1u < strand.pointCount; ++localIndex)
                {
                    const Vec3 prev = original[localIndex - 1u];
                    const Vec3 curr = original[localIndex];
                    const Vec3 next = original[localIndex + 1u];
                    const Vec3 dirPrev = Normalize(curr - prev);
                    const Vec3 dirNext = Normalize(next - curr);
                    if (Length(dirPrev) <= kEpsilon || Length(dirNext) <= kEpsilon)
                    {
                        continue;
                    }

                    const float angle = SafeAcos(Dot(dirPrev, dirNext));
                    if (angle <= maxBend)
                    {
                        continue;
                    }

                    const float excess = angle - maxBend;
                    const float weight = Clamp01((excess / 3.14159265f) * smoothing * 2.0f);
                    const Vec3 midpoint = (prev + next) * 0.5f;
                    asset.restPoints[strand.pointOffset + localIndex] = Lerp(curr, midpoint, weight);
                }

                if (settings.preserveSegmentLengths)
                {
                    EnforceSegmentLengths(asset, strand, segmentLengths);
                }
            }
        }
    }


    void ApplyAngularXpbdBendRelaxation(HairAsset& asset, const AngularXpbdBendSettings& settings)
    {
        const float targetBend = std::clamp(settings.targetBendRadians, 0.0f, 3.05f);
        const float compliance = std::max(settings.bendCompliance, 0.0f);
        const float damping = Clamp01(settings.damping);
        const uint32_t iterations = std::max<uint32_t>(1u, settings.iterations);

        for (const StrandRange& strand : asset.strands)
        {
            if (strand.pointCount < 3u || strand.pointOffset + strand.pointCount > asset.restPoints.size())
            {
                continue;
            }

            std::vector<float> segmentLengths;
            segmentLengths.reserve(strand.pointCount - 1u);
            for (uint32_t localIndex = 1u; localIndex < strand.pointCount; ++localIndex)
            {
                const Vec3 a = asset.restPoints[strand.pointOffset + localIndex - 1u];
                const Vec3 b = asset.restPoints[strand.pointOffset + localIndex];
                segmentLengths.push_back(std::max(Length(b - a), 1.0e-4f));
            }

            for (uint32_t iteration = 0u; iteration < iterations; ++iteration)
            {
                std::vector<Vec3> original(strand.pointCount);
                for (uint32_t localIndex = 0u; localIndex < strand.pointCount; ++localIndex)
                {
                    original[localIndex] = asset.restPoints[strand.pointOffset + localIndex];
                }

                for (uint32_t localIndex = settings.affectRootAdjacentPoint ? 1u : 2u; localIndex + 1u < strand.pointCount; ++localIndex)
                {
                    const Vec3 prev = original[localIndex - 1u];
                    const Vec3 curr = original[localIndex];
                    const Vec3 next = original[localIndex + 1u];
                    const Vec3 v0 = curr - prev;
                    const Vec3 v1 = next - curr;
                    const float l0 = Length(v0);
                    const float l1 = Length(v1);
                    if (l0 <= kEpsilon || l1 <= kEpsilon)
                    {
                        continue;
                    }

                    const Vec3 d0 = v0 / l0;
                    const Vec3 d1 = v1 / l1;
                    const float angle = SafeAcos(Dot(d0, d1));
                    const float constraint = angle - targetBend;
                    if (std::abs(constraint) <= 1.0e-4f)
                    {
                        continue;
                    }

                    Vec3 bisector = Normalize(d0 + d1);
                    if (Length(bisector) <= kEpsilon)
                    {
                        bisector = Normalize(Cross(d0, Vec3(0.0f, 1.0f, 0.0f)));
                        if (Length(bisector) <= kEpsilon)
                        {
                            bisector = Vec3(0.0f, 0.0f, 1.0f);
                        }
                    }

                    const float stiffness = 1.0f / (1.0f + compliance * static_cast<float>(iteration + 1u) * 1024.0f);
                    const float moveScale = std::clamp(constraint * stiffness * 0.35f, -0.35f, 0.35f);
                    const Vec3 target = (prev + next) * 0.5f + bisector * ((l0 + l1) * 0.18f * targetBend);
                    Vec3 corrected = Lerp(curr, target, std::abs(moveScale));
                    corrected = Lerp(corrected, curr, damping * 0.5f);
                    asset.restPoints[strand.pointOffset + localIndex] = corrected;
                }

                if (settings.preserveSegmentLengths)
                {
                    EnforceSegmentLengths(asset, strand, segmentLengths);
                }
            }
        }
    }


    namespace
    {
        Vec3 SampleCanonicalPoint(const HairAsset& asset, const StrandRange& strand, float t)
        {
            return SampleStrandPoint(asset, strand, t);
        }

        float SampleCanonicalWidth(const HairAsset& asset, const StrandRange& strand, float t)
        {
            return SampleStrandWidth(asset, strand, t, 0.004f);
        }

        bool SourceStrandIsGuide(const HairAsset& asset, uint32_t strandIndex)
        {
            const bool orderedGuide = strandIndex < asset.guideStrandCount;
            const bool flaggedGuide = strandIndex < asset.strands.size() && ((asset.strands[strandIndex].flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u);
            if (asset.guideStrandCount > 0u)
            {
                return orderedGuide || flaggedGuide;
            }
            return flaggedGuide;
        }

        uint32_t FindClosestGuide(const HairAsset& asset, const std::vector<uint32_t>& guideIndices, uint32_t strandIndex)
        {
            if (guideIndices.empty())
            {
                return 0u;
            }
            const StrandRange& strand = asset.strands[strandIndex];
            const Vec3 root = (strand.pointCount > 0u && strand.pointOffset < asset.restPoints.size()) ? asset.restPoints[strand.pointOffset] : Vec3{};
            uint32_t bestGuide = guideIndices.front();
            float bestDistanceSq = std::numeric_limits<float>::max();
            for (uint32_t guideIndex : guideIndices)
            {
                if (guideIndex >= asset.strands.size())
                {
                    continue;
                }
                const StrandRange& guide = asset.strands[guideIndex];
                if (guide.pointCount == 0u || guide.pointOffset >= asset.restPoints.size())
                {
                    continue;
                }
                const Vec3 delta = asset.restPoints[guide.pointOffset] - root;
                const float distanceSq = Dot(delta, delta);
                if (distanceSq < bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    bestGuide = guideIndex;
                }
            }
            return bestGuide;
        }

        void AppendCanonicalStrand(HairAsset& dst,
                                   const HairAsset& src,
                                   uint32_t srcStrandIndex,
                                   uint32_t targetPointCount,
                                   uint32_t groupId,
                                   uint32_t flags,
                                   int32_t parentGuide)
        {
            const StrandRange& srcStrand = src.strands[srcStrandIndex];
            StrandRange dstStrand = {};
            dstStrand.pointOffset = static_cast<uint32_t>(dst.restPoints.size());
            dstStrand.pointCount = targetPointCount;
            dstStrand.groupId = groupId;
            dstStrand.flags = flags;
            dst.strands.push_back(dstStrand);

            StrandMaterial material = (srcStrandIndex < src.materials.size()) ? src.materials[srcStrandIndex] : StrandMaterial{};
            dst.materials.push_back(material);

            RootBinding binding = (srcStrandIndex < src.rootBindings.size()) ? src.rootBindings[srcStrandIndex] : RootBinding{};
            if (!dst.scalpMesh.triangles.empty())
            {
                binding.triangleIndex = std::min<uint32_t>(binding.triangleIndex, static_cast<uint32_t>(dst.scalpMesh.triangles.size() - 1u));
            }
            else
            {
                binding.triangleIndex = 0u;
                binding.barycentric = {1.0f, 0.0f, 0.0f};
            }
            dst.rootBindings.push_back(binding);

            FollowData follow = (srcStrandIndex < src.followData.size()) ? src.followData[srcStrandIndex] : FollowData{};
            follow.parentGuide = parentGuide;
            if ((flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u)
            {
                follow.parentGuide = static_cast<int32_t>(groupId);
                follow.followWeight = 1.0f;
                follow.clumpWeight = 0.0f;
                follow.noiseWeight = 0.0f;
            }
            dst.followData.push_back(follow);

            for (uint32_t pointIndex = 0u; pointIndex < targetPointCount; ++pointIndex)
            {
                const float t = (targetPointCount > 1u) ? (static_cast<float>(pointIndex) / static_cast<float>(targetPointCount - 1u)) : 0.0f;
                dst.restPoints.push_back(SampleCanonicalPoint(src, srcStrand, t));
                dst.pointWidths.push_back(SampleCanonicalWidth(src, srcStrand, t));
            }
        }
    }

    bool CanonicalizeHairAssetToParticleChains(HairAsset& asset, const ParticleChainCanonicalizationSettings& settings, std::string& outError)
    {
        outError.clear();
        if (asset.strands.empty())
        {
            outError = "Asset has no strands to canonicalize.";
            return false;
        }
        if (asset.restPoints.empty())
        {
            outError = "Asset has no points to canonicalize.";
            return false;
        }
        if (asset.pointWidths.size() != asset.restPoints.size())
        {
            outError = "pointWidths must match restPoints before canonicalization.";
            return false;
        }

        uint32_t targetPointCount = settings.targetPointCount;
        if (targetPointCount == 0u)
        {
            targetPointCount = std::max<uint32_t>(asset.particlesPerStrand, 0u);
            for (const StrandRange& strand : asset.strands)
            {
                targetPointCount = std::max(targetPointCount, strand.pointCount);
            }
        }
        targetPointCount = std::max<uint32_t>(targetPointCount, 2u);

        std::vector<uint32_t> guideIndices;
        std::vector<uint32_t> followerIndices;
        guideIndices.reserve(asset.strands.size());
        followerIndices.reserve(asset.strands.size());

        for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
        {
            if (SourceStrandIsGuide(asset, strandIndex))
            {
                guideIndices.push_back(strandIndex);
            }
        }
        if (guideIndices.empty())
        {
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                const bool hasParentGuide = strandIndex < asset.followData.size() && asset.followData[strandIndex].parentGuide >= 0;
                if (!hasParentGuide)
                {
                    guideIndices.push_back(strandIndex);
                }
            }
        }
        if (guideIndices.empty())
        {
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                guideIndices.push_back(strandIndex);
            }
        }

        std::vector<uint8_t> guideMask(asset.strands.size(), 0u);
        for (uint32_t strandIndex : guideIndices)
        {
            if (strandIndex < guideMask.size())
            {
                guideMask[strandIndex] = 1u;
            }
        }
        for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
        {
            if (guideMask[strandIndex] == 0u)
            {
                followerIndices.push_back(strandIndex);
            }
        }

        HairAsset canonical = {};
        canonical.scalpMesh = asset.scalpMesh;
        canonical.particlesPerStrand = targetPointCount;
        canonical.guideStrandCount = static_cast<uint32_t>(guideIndices.size());
        canonical.followerStrandCount = static_cast<uint32_t>(followerIndices.size());
        canonical.strands.reserve(asset.strands.size());
        canonical.restPoints.reserve(asset.strands.size() * static_cast<size_t>(targetPointCount));
        canonical.pointWidths.reserve(asset.strands.size() * static_cast<size_t>(targetPointCount));
        canonical.materials.reserve(asset.strands.size());
        canonical.rootBindings.reserve(asset.strands.size());
        canonical.followData.reserve(asset.strands.size());

        std::unordered_map<uint32_t, uint32_t> oldGuideToNewGuide;
        oldGuideToNewGuide.reserve(guideIndices.size());
        for (uint32_t newGuideIndex = 0u; newGuideIndex < guideIndices.size(); ++newGuideIndex)
        {
            const uint32_t oldStrandIndex = guideIndices[newGuideIndex];
            oldGuideToNewGuide[oldStrandIndex] = newGuideIndex;
            AppendCanonicalStrand(canonical,
                                  asset,
                                  oldStrandIndex,
                                  targetPointCount,
                                  newGuideIndex,
                                  static_cast<uint32_t>(StrandFlags::Guide),
                                  static_cast<int32_t>(newGuideIndex));
        }

        for (uint32_t followerOrder = 0u; followerOrder < followerIndices.size(); ++followerOrder)
        {
            const uint32_t oldStrandIndex = followerIndices[followerOrder];
            int32_t parentGuide = -1;
            if (oldStrandIndex < asset.followData.size())
            {
                parentGuide = asset.followData[oldStrandIndex].parentGuide;
            }
            uint32_t resolvedSourceGuide = 0u;
            auto remapIt = (parentGuide >= 0) ? oldGuideToNewGuide.find(static_cast<uint32_t>(parentGuide)) : oldGuideToNewGuide.end();
            if (remapIt != oldGuideToNewGuide.end())
            {
                resolvedSourceGuide = remapIt->second;
            }
            else
            {
                const uint32_t closestOldGuide = FindClosestGuide(asset, guideIndices, oldStrandIndex);
                auto closestIt = oldGuideToNewGuide.find(closestOldGuide);
                resolvedSourceGuide = (closestIt != oldGuideToNewGuide.end()) ? closestIt->second : 0u;
            }

            AppendCanonicalStrand(canonical,
                                  asset,
                                  oldStrandIndex,
                                  targetPointCount,
                                  resolvedSourceGuide,
                                  asset.strands[oldStrandIndex].flags & ~static_cast<uint32_t>(StrandFlags::Guide),
                                  static_cast<int32_t>(resolvedSourceGuide));
        }

        if (settings.snapRootsToScalp && !canonical.scalpMesh.triangles.empty() && canonical.rootBindings.size() == canonical.strands.size())
        {
            RebuildRootRestPositionsFromScalp(canonical);
        }

        asset = std::move(canonical);
        return true;
    }

    bool RetessellateHairAsset(HairAsset& asset, const StrandRetessellationSettings& settings, std::string& outError)
    {
        ParticleChainCanonicalizationSettings canonicalSettings = {};
        canonicalSettings.targetPointCount = settings.targetPointCount;
        canonicalSettings.snapRootsToScalp = settings.snapRootsToScalp;
        canonicalSettings.reorderGuideFirst = settings.reorderGuideFirst;
        return CanonicalizeHairAssetToParticleChains(asset, canonicalSettings, outError);
    }

    bool ApplyPaletteToRegion(HairAsset& asset, const RegionSelectionResult& selection, const Palette& palette, float strength, std::string& outError)
    {
        outError.clear();
        if (selection.strands.empty())
        {
            outError = "No strands selected.";
            return false;
        }
        for (const SelectedStrand& selected : selection.strands)
        {
            if (selected.strandIndex >= asset.strands.size()) continue;
            const StrandRange& strand = asset.strands[selected.strandIndex];
            for (uint32_t localIndex = 0; localIndex < strand.pointCount; ++localIndex)
            {
                const float u = (strand.pointCount > 1) ? static_cast<float>(localIndex) / static_cast<float>(strand.pointCount - 1) : 0.0f;
                float widthScale = 1.0f;
                const Vec3 offset = SamplePalette(palette, u, widthScale);
                asset.restPoints[strand.pointOffset + localIndex] += offset * (strength * selected.weight);
                if (strand.pointOffset + localIndex < asset.pointWidths.size())
                {
                    asset.pointWidths[strand.pointOffset + localIndex] *= Lerp(1.0f, widthScale, strength * selected.weight);
                }
            }
        }
        return true;
    }


    bool ApplyEditStackToGuideFamiliesSparse(
        HairAsset& asset,
        const HairAsset& baseAsset,
        const EditStack& stack,
        const std::vector<uint32_t>& affectedGuideIndices,
        SparseEditResult* outResult,
        RuntimeEditState* outState,
        std::string& outError)
    {
        outError.clear();
        if (asset.strands.size() != baseAsset.strands.size() ||
            asset.restPoints.size() != baseAsset.restPoints.size() ||
            asset.pointWidths.size() != baseAsset.pointWidths.size())
        {
            outError = "Sparse edit requires topology-identical current and base assets.";
            return false;
        }
        const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
        std::vector<uint32_t> guides;
        guides.reserve(affectedGuideIndices.size());
        for (uint32_t guide : affectedGuideIndices)
        {
            if (guide < guideCount) guides.push_back(guide);
            else if (guide < asset.followData.size() && asset.followData[guide].parentGuide >= 0)
                guides.push_back(static_cast<uint32_t>(asset.followData[guide].parentGuide));
        }
        std::sort(guides.begin(), guides.end());
        guides.erase(std::unique(guides.begin(), guides.end()), guides.end());
        if (guides.empty())
        {
            outError = "Sparse edit did not resolve an affected guide.";
            return false;
        }
        std::vector<uint8_t> guideMask(guideCount, 0u);
        for (uint32_t guide : guides) guideMask[guide] = 1u;
        std::vector<uint32_t> familyStrands = guides;
        for (uint32_t strand = guideCount; strand < asset.strands.size() && strand < asset.followData.size(); ++strand)
        {
            const int32_t parent = asset.followData[strand].parentGuide;
            if (parent >= 0 && static_cast<uint32_t>(parent) < guideCount && guideMask[static_cast<uint32_t>(parent)] != 0u)
                familyStrands.push_back(strand);
        }
        for (uint32_t strandIndex : familyStrands)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            for (uint32_t local = 0u; local < strand.pointCount; ++local)
            {
                const uint32_t index = strand.pointOffset + local;
                asset.restPoints[index] = baseAsset.restPoints[index];
                if (index < asset.pointWidths.size()) asset.pointWidths[index] = baseAsset.pointWidths[index];
            }
        }
        RuntimeEditState state = {};
        state.guideInfluence.assign(guideCount, 0.0f);
        state.strandInfluence.assign(asset.strands.size(), 0.0f);

        auto selectedWeight = [](const EditLayer& layer, uint32_t guide) -> float
        {
            if (layer.strandSelection.empty()) return 1.0f;
            return std::find(layer.strandSelection.begin(), layer.strandSelection.end(), guide) != layer.strandSelection.end()
                ? Clamp01(layer.layerWeight * layer.strandSelectionWeight) : 0.0f;
        };

        for (const EditLayer& layer : stack.layers)
        {
            if (!layer.enabled || layer.layerWeight <= 1.0e-5f) continue;
            for (uint32_t guide : guides)
            {
                const float weight = selectedWeight(layer, guide);
                if (weight <= 1.0e-5f) continue;
                state.guideInfluence[guide] = std::max(state.guideInfluence[guide], weight);
                state.maxGuideInfluence = std::max(state.maxGuideInfluence, weight);
                const StrandRange& strand = asset.strands[guide];
                if (strand.pointCount == 0u) continue;
                const Vec3 root = asset.restPoints[strand.pointOffset];
                const Vec3 tip = asset.restPoints[strand.pointOffset + strand.pointCount - 1u];
                Vec3 axis = Normalize(tip - root);
                if (Length(axis) <= 1.0e-6f) axis = Vec3(0.0f, -1.0f, 0.0f);
                Vec3 lateral = Normalize(Cross(Vec3(0.0f, 1.0f, 0.0f), axis));
                if (Length(lateral) <= 1.0e-6f) lateral = Vec3(1.0f, 0.0f, 0.0f);
                for (uint32_t local = 0u; local < strand.pointCount; ++local)
                {
                    const uint32_t index = strand.pointOffset + local;
                    const float u = strand.pointCount > 1u ? static_cast<float>(local) / static_cast<float>(strand.pointCount - 1u) : 0.0f;
                    Vec3 relative = asset.restPoints[index] - root;
                    relative = RotateYaw(relative, layer.edit.yawRadians * weight);
                    relative = relative * Lerp(1.0f, layer.edit.lengthScale, u * weight);
                    const Vec3 offset = Lerp(layer.edit.rootOffset, layer.edit.tipOffset, u) * weight + lateral * (layer.edit.sideOffset * weight);
                    asset.restPoints[index] = root + relative + offset;
                    if (index < asset.pointWidths.size())
                        asset.pointWidths[index] *= Lerp(1.0f, layer.edit.widthScale, weight);
                }
                state.strandInfluence[guide] = std::max(state.strandInfluence[guide], weight);
                for (uint32_t follower : familyStrands)
                {
                    if (follower < guideCount || follower >= asset.followData.size() || asset.followData[follower].parentGuide != static_cast<int32_t>(guide)) continue;
                    const StrandRange& followerRange = asset.strands[follower];
                    const float followerWeight = Clamp01(weight * layer.edit.followerInfluence * asset.followData[follower].followWeight);
                    const uint32_t count = std::min(strand.pointCount, followerRange.pointCount);
                    for (uint32_t local = 0u; local < count; ++local)
                    {
                        const uint32_t gi = strand.pointOffset + local;
                        const uint32_t fi = followerRange.pointOffset + local;
                        const Vec3 guideDelta = asset.restPoints[gi] - baseAsset.restPoints[gi];
                        asset.restPoints[fi] = baseAsset.restPoints[fi] + guideDelta * followerWeight;
                        if (fi < asset.pointWidths.size())
                            asset.pointWidths[fi] = baseAsset.pointWidths[fi] * Lerp(1.0f, layer.edit.widthScale, followerWeight);
                    }
                    state.strandInfluence[follower] = std::max(state.strandInfluence[follower], followerWeight);
                }
            }
        }
        state.activeGuideCount = static_cast<uint32_t>(std::count_if(state.guideInfluence.begin(), state.guideInfluence.end(), [](float value) { return value > 1.0e-5f; }));
        if (outState) *outState = std::move(state);
        if (outResult)
        {
            outResult->affectedStrandIndices = familyStrands;
            outResult->changedPointIndices.clear();
            for (uint32_t strandIndex : familyStrands)
            {
                const StrandRange& strand = asset.strands[strandIndex];
                for (uint32_t local = 0u; local < strand.pointCount; ++local)
                    outResult->changedPointIndices.push_back(strand.pointOffset + local);
            }
        }
        return true;
    }

}
