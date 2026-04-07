#include "hairgpu/Authoring.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <limits>
#include <unordered_map>
#include <cctype>


namespace hairgpu
{
    namespace
    {
        constexpr float kEpsilon = 1.0e-6f;
        constexpr float kPi = 3.1415926535f;

        std::string TrimCopy(const std::string& value)
        {
            const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c)
            {
                return std::isspace(c) != 0;
            });
            const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c)
            {
                return std::isspace(c) != 0;
            }).base();
            if (begin >= end)
            {
                return {};
            }
            return std::string(begin, end);
        }

        std::string ToLowerCopy(std::string value)
        {
            for (char& c : value)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return value;
        }

        std::vector<std::string> SplitCsvLine(const std::string& line)
        {
            std::vector<std::string> tokens;
            std::string current;
            for (char c : line)
            {
                if (c == ',')
                {
                    tokens.push_back(TrimCopy(current));
                    current.clear();
                }
                else
                {
                    current.push_back(c);
                }
            }
            tokens.push_back(TrimCopy(current));
            return tokens;
        }

        bool TryParseFloatValue(const std::string& text, float& outValue)
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

        bool TryParseUIntValue(const std::string& text, uint32_t& outValue)
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

        bool TryParseBoolValue(const std::string& text, bool& outValue)
        {
            const std::string lower = ToLowerCopy(TrimCopy(text));
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on")
            {
                outValue = true;
                return true;
            }
            if (lower == "0" || lower == "false" || lower == "no" || lower == "off")
            {
                outValue = false;
                return true;
            }
            return false;
        }

        float Clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
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

        Vec3 RotateYaw(const Vec3& p, float radians)
        {
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            return {p.x * c - p.z * s, p.y, p.x * s + p.z * c};
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
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }

        float ComputeBrushDabWeight(const Vec2& uv, const BrushDab& dab)
        {
            const float radius = std::max(dab.radiusUv, 1.0e-4f);
            const float dx = uv.x - dab.centerUv.x;
            const float dy = uv.y - dab.centerUv.y;
            const float normalizedDistance = std::sqrt(dx * dx + dy * dy) / radius;
            if (normalizedDistance > 1.0f)
            {
                return 0.0f;
            }
            return Clamp01(dab.opacity) * ComputeFeatheredWeight(normalizedDistance, Clamp01(dab.feather));
        }

        RegionSelectionResult SelectBrushInternal(const HairAsset& asset, const BrushStroke& stroke)
        {
            RegionSelectionResult result = {};
            if (stroke.dabs.empty())
            {
                return result;
            }

            std::vector<float> weights(asset.strands.size(), 0.0f);
            for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
            {
                const StrandRange& strand = asset.strands[strandIndex];
                if (stroke.groupId != UINT32_MAX && strand.groupId != stroke.groupId)
                {
                    continue;
                }
                if (stroke.guidesOnly && strandIndex >= asset.guideStrandCount)
                {
                    continue;
                }

                const Vec2 uv = GetRootUv(asset, strandIndex);
                float accumulated = 0.0f;
                for (const BrushDab& dab : stroke.dabs)
                {
                    const float dabWeight = ComputeBrushDabWeight(uv, dab);
                    if (dab.subtract)
                    {
                        accumulated = Clamp01(accumulated - dabWeight);
                    }
                    else
                    {
                        accumulated = Clamp01(accumulated + dabWeight * (1.0f - accumulated));
                    }
                }
                weights[strandIndex] = accumulated;
                if (accumulated > 1.0e-4f)
                {
                    result.strands.push_back({ strandIndex, std::clamp(accumulated, 0.05f, 1.0f) });
                }
            }
            return result;
        }

        EditSettings ScaleEditByStrength(EditSettings settings, float strength)
        {
            strength = Clamp01(strength);
            settings.lengthScale = Lerp(1.0f, settings.lengthScale, strength);
            settings.widthScale = Lerp(1.0f, settings.widthScale, strength);
            settings.rootOffset = settings.rootOffset * strength;
            settings.tipOffset = settings.tipOffset * strength;
            settings.sideOffset *= strength;
            settings.yawRadians *= strength;
            settings.followerInfluence = Lerp(1.0f, settings.followerInfluence, strength);
            return settings;
        }

        void AddPresetLayer(EditStack& stack,
                            const char* name,
                            const RegionSelectionSettings& region,
                            const EditSettings& targetEdit,
                            float strength)
        {
            EditLayer layer = {};
            layer.name = name;
            layer.region = region;
            layer.edit = ScaleEditByStrength(targetEdit, strength);
            layer.layerWeight = 1.0f;
            layer.enabled = true;
            stack.layers.push_back(layer);
        }

        bool AppendPresetInternal(EditStack& stack, const std::string& presetName, float strength, std::string& outError)
        {
            const std::string key = ToLowerCopy(TrimCopy(presetName));
            const float safeStrength = Clamp01(strength);
            if (key.empty())
            {
                outError = "Preset name is empty.";
                return false;
            }

            RegionSelectionSettings region = {};
            EditSettings edit = {};

            if (key == "front bangs" || key == "front_bangs" || key == "bangs")
            {
                region.mode = RegionSelectMode::Ellipse;
                region.centerUv = { 0.50f, 0.22f };
                region.radiusUv = { 0.18f, 0.14f };
                region.feather = 0.42f;
                edit.lengthScale = 0.92f;
                edit.rootOffset = { 0.0f, -0.012f, 0.0f };
                edit.tipOffset = { 0.0f, -0.10f, 0.06f };
                edit.widthScale = 1.04f;
                AddPresetLayer(stack, "Front Bangs", region, edit, safeStrength);
            }
            else if (key == "left sweep" || key == "left_sweep")
            {
                region.mode = RegionSelectMode::Sector;
                region.centerUv = { 0.52f, 0.30f };
                region.radiusUv = { 0.42f, 0.30f };
                region.angleCenterRadians = 3.14159265f;
                region.angleHalfExtentRadians = 1.05f;
                region.feather = 0.38f;
                edit.sideOffset = -0.08f;
                edit.tipOffset = { -0.02f, 0.02f, 0.03f };
                edit.yawRadians = 0.42f;
                edit.widthScale = 1.03f;
                AddPresetLayer(stack, "Left Sweep", region, edit, safeStrength);
            }
            else if (key == "right sweep" || key == "right_sweep")
            {
                region.mode = RegionSelectMode::Sector;
                region.centerUv = { 0.48f, 0.30f };
                region.radiusUv = { 0.42f, 0.30f };
                region.angleCenterRadians = 0.0f;
                region.angleHalfExtentRadians = 1.05f;
                region.feather = 0.38f;
                edit.sideOffset = 0.08f;
                edit.tipOffset = { 0.02f, 0.02f, 0.03f };
                edit.yawRadians = -0.42f;
                edit.widthScale = 1.03f;
                AddPresetLayer(stack, "Right Sweep", region, edit, safeStrength);
            }
            else if (key == "crown lift" || key == "crown_lift")
            {
                region.mode = RegionSelectMode::Ellipse;
                region.centerUv = { 0.50f, 0.48f };
                region.radiusUv = { 0.16f, 0.14f };
                region.feather = 0.46f;
                edit.lengthScale = 1.08f;
                edit.rootOffset = { 0.0f, 0.04f, 0.0f };
                edit.tipOffset = { 0.0f, 0.11f, 0.0f };
                AddPresetLayer(stack, "Crown Lift", region, edit, safeStrength);
            }
            else if (key == "side volume" || key == "side_volume")
            {
                region.mode = RegionSelectMode::Rectangle;
                region.centerUv = { 0.23f, 0.46f };
                region.radiusUv = { 0.12f, 0.24f };
                region.feather = 0.40f;
                edit.sideOffset = -0.06f;
                edit.tipOffset = { -0.02f, 0.05f, 0.0f };
                edit.widthScale = 1.07f;
                AddPresetLayer(stack, "Side Volume Left", region, edit, safeStrength);

                region.centerUv = { 0.77f, 0.46f };
                edit.sideOffset = 0.06f;
                edit.tipOffset = { 0.02f, 0.05f, 0.0f };
                AddPresetLayer(stack, "Side Volume Right", region, edit, safeStrength);
            }
            else if (key == "back tuck" || key == "back_tuck")
            {
                region.mode = RegionSelectMode::Sector;
                region.centerUv = { 0.50f, 0.72f };
                region.radiusUv = { 0.34f, 0.24f };
                region.angleCenterRadians = 1.57079632f;
                region.angleHalfExtentRadians = 1.15f;
                region.feather = 0.36f;
                edit.lengthScale = 0.93f;
                edit.rootOffset = { 0.0f, -0.015f, 0.0f };
                edit.tipOffset = { 0.0f, -0.04f, -0.07f };
                edit.yawRadians = 0.08f;
                AddPresetLayer(stack, "Back Tuck", region, edit, safeStrength);
            }
            else
            {
                outError = "Unknown edit preset: " + presetName;
                return false;
            }

            outError.clear();
            return true;
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
            if (settings.mode == RegionSelectMode::Brush)
            {
                const BrushDab dab = { settings.centerUv, std::max(settings.radiusUv.x, settings.radiusUv.y), 1.0f, settings.feather, false };
                outWeight = ComputeBrushDabWeight(uv, dab);
                return outWeight > 0.0f;
            }
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
                    Vec3 weightedAverage = asset.restPoints[strand.pointOffset + localIndex];
                    float totalWeight = 1.0f;
                    for (const auto& [distance, otherGuide] : neighbors)
                    {
                        const StrandRange& neighborStrand = asset.strands[otherGuide];
                        const uint32_t neighborLocalIndex = std::min<uint32_t>(localIndex, neighborStrand.pointCount - 1u);
                        const float neighborWeight = Clamp01(1.0f - distance / safeRadius);
                        weightedAverage += asset.restPoints[neighborStrand.pointOffset + neighborLocalIndex] * neighborWeight;
                        totalWeight += neighborWeight;
                    }
                    weightedAverage = weightedAverage / std::max(totalWeight, 1.0e-4f);
                    const float blend = Clamp01(smoothingStrength * boundary * (0.20f + 0.80f * u));
                    smoothedPoints[strand.pointOffset + localIndex] = Lerp(asset.restPoints[strand.pointOffset + localIndex], weightedAverage, blend);
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
                const float blend = Clamp01((0.25f + 0.55f * boundary) * std::clamp(follow.followWeight, 0.0f, 1.0f));
                for (uint32_t localIndex = 0u; localIndex < pointCount; ++localIndex)
                {
                    const uint32_t gi = guide.pointOffset + localIndex;
                    const uint32_t si = follower.pointOffset + localIndex;
                    asset.restPoints[si] = Lerp(asset.restPoints[si], asset.restPoints[gi], blend);
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

    RegionSelectionResult SelectBrushRegion(const HairAsset& asset, const BrushStroke& stroke)
    {
        return SelectBrushInternal(asset, stroke);
    }

    bool LoadBrushStrokeCsv(const std::filesystem::path& path, BrushStroke& outStroke, std::string& outError)
    {
        outStroke = {};
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open brush CSV: " + path.string();
            return false;
        }

        std::string line;
        uint32_t lineNumber = 0u;
        while (std::getline(in, line))
        {
            ++lineNumber;
            line = TrimCopy(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            std::vector<std::string> tokens = SplitCsvLine(line);
            if (tokens.empty())
            {
                continue;
            }

            const std::string key = ToLowerCopy(tokens[0]);
            if (key == "groupid" && tokens.size() >= 2u)
            {
                if (!TryParseUIntValue(tokens[1], outStroke.groupId))
                {
                    outError = "Malformed groupId row in brush CSV at line " + std::to_string(lineNumber);
                    return false;
                }
                continue;
            }
            if (key == "guidesonly" && tokens.size() >= 2u)
            {
                if (!TryParseBoolValue(tokens[1], outStroke.guidesOnly))
                {
                    outError = "Malformed guidesOnly row in brush CSV at line " + std::to_string(lineNumber);
                    return false;
                }
                continue;
            }

            const size_t base = (key == "dab") ? 1u : 0u;
            if (tokens.size() < base + 4u)
            {
                outError = "Brush CSV rows must be `dab,u,v,radius[,opacity,feather,subtract]` or `u,v,radius,opacity[,feather,subtract]`.";
                return false;
            }

            BrushDab dab = {};
            if (!TryParseFloatValue(tokens[base + 0u], dab.centerUv.x) ||
                !TryParseFloatValue(tokens[base + 1u], dab.centerUv.y) ||
                !TryParseFloatValue(tokens[base + 2u], dab.radiusUv) ||
                !TryParseFloatValue(tokens[base + 3u], dab.opacity))
            {
                outError = "Malformed brush dab row at line " + std::to_string(lineNumber);
                return false;
            }
            if (tokens.size() > base + 4u && !tokens[base + 4u].empty() && !TryParseFloatValue(tokens[base + 4u], dab.feather))
            {
                outError = "Malformed brush feather value at line " + std::to_string(lineNumber);
                return false;
            }
            if (tokens.size() > base + 5u && !tokens[base + 5u].empty() && !TryParseBoolValue(tokens[base + 5u], dab.subtract))
            {
                outError = "Malformed brush subtract value at line " + std::to_string(lineNumber);
                return false;
            }
            dab.radiusUv = std::max(dab.radiusUv, 1.0e-4f);
            dab.opacity = Clamp01(dab.opacity);
            dab.feather = Clamp01(dab.feather);
            outStroke.dabs.push_back(dab);
        }

        if (outStroke.dabs.empty())
        {
            outError = "Brush CSV did not contain any dabs.";
            return false;
        }
        outError.clear();
        return true;
    }

    bool SaveBrushStrokeCsv(const std::filesystem::path& path, const BrushStroke& stroke, std::string& outError)
    {
        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to open output brush CSV: " + path.string();
            return false;
        }
        out << "groupId," << stroke.groupId << "\n";
        out << "guidesOnly," << (stroke.guidesOnly ? 1 : 0) << "\n";
        out << "# dab,u,v,radius,opacity,feather,subtract\n";
        for (const BrushDab& dab : stroke.dabs)
        {
            out << "dab," << dab.centerUv.x << "," << dab.centerUv.y << "," << dab.radiusUv << ","
                << dab.opacity << "," << dab.feather << "," << (dab.subtract ? 1 : 0) << "\n";
        }
        outError.clear();
        return true;
    }

    bool AppendNamedEditPreset(EditStack& stack, const std::string& presetName, float strength, std::string& outError)
    {
        return AppendPresetInternal(stack, presetName, strength, outError);
    }

    bool BuildNamedEditPresetStack(const std::string& presetName, float strength, EditStack& outStack, std::string& outError)
    {
        outStack = {};
        outStack.boundarySmoothing = 0.42f;
        outStack.boundaryRadiusUv = 0.10f;
        return AppendPresetInternal(outStack, presetName, strength, outError);
    }

    bool ApplyEditToRegion(HairAsset& asset, const RegionSelectionResult& selection, const EditSettings& settings, std::string& outError)
    {
        outError.clear();
        if (selection.strands.empty())
        {
            outError = "No strands selected.";
            return false;
        }
        for (const SelectedStrand& selected : selection.strands)
        {
            if (selected.strandIndex >= asset.strands.size())
            {
                continue;
            }
            const StrandRange& strand = asset.strands[selected.strandIndex];
            if (strand.pointCount == 0)
            {
                continue;
            }
            const Vec3 root = asset.restPoints[strand.pointOffset];
            for (uint32_t localIndex = 0; localIndex < strand.pointCount; ++localIndex)
            {
                const float u = (strand.pointCount > 1) ? static_cast<float>(localIndex) / static_cast<float>(strand.pointCount - 1) : 0.0f;
                Vec3 p = asset.restPoints[strand.pointOffset + localIndex] - root;
                p = RotateYaw(p, settings.yawRadians * selected.weight);
                p = p * Lerp(1.0f, settings.lengthScale, u * selected.weight);
                const Vec3 additive = Lerp(settings.rootOffset, settings.tipOffset, u) * selected.weight;
                const Vec3 side = Vec3(settings.sideOffset * selected.weight, 0.0f, 0.0f);
                asset.restPoints[strand.pointOffset + localIndex] = root + p + additive + side;
                if (strand.pointOffset + localIndex < asset.pointWidths.size())
                {
                    asset.pointWidths[strand.pointOffset + localIndex] *= Lerp(1.0f, settings.widthScale, selected.weight);
                }
            }
        }

        const uint32_t guideCount = std::min<uint32_t>(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size()));
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
            const float appliedWeight = it->weight * settings.followerInfluence * std::clamp(follow.followWeight, 0.0f, 1.0f);
            const StrandRange& guide = asset.strands[static_cast<uint32_t>(follow.parentGuide)];
            const StrandRange& strand = asset.strands[strandIndex];
            const uint32_t count = std::min(guide.pointCount, strand.pointCount);
            for (uint32_t localIndex = 0; localIndex < count; ++localIndex)
            {
                const uint32_t gi = guide.pointOffset + localIndex;
                const uint32_t si = strand.pointOffset + localIndex;
                asset.restPoints[si] = Lerp(asset.restPoints[si], asset.restPoints[gi], appliedWeight);
                if (si < asset.pointWidths.size() && gi < asset.pointWidths.size())
                {
                    asset.pointWidths[si] = Lerp(asset.pointWidths[si], asset.pointWidths[gi], appliedWeight);
                }
            }
        }
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
            const RegionSelectionResult selection = SelectRegion(asset, layer.region);
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
                    state.strandInfluence[strandIndex] = std::max(state.strandInfluence[strandIndex], state.guideInfluence[static_cast<uint32_t>(follow.parentGuide)]);
                }
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
            RegionSelectionResult selection = SelectRegion(asset, layer.region);
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
            if (strand.pointCount == 0u || strand.pointOffset >= asset.restPoints.size())
            {
                return {};
            }
            if (strand.pointCount == 1u)
            {
                return asset.restPoints[strand.pointOffset];
            }
            const float scaled = Clamp(t, 0.0f, 1.0f) * static_cast<float>(strand.pointCount - 1u);
            const uint32_t i0 = static_cast<uint32_t>(std::floor(scaled));
            const uint32_t i1 = std::min(i0 + 1u, strand.pointCount - 1u);
            const float alpha = scaled - static_cast<float>(i0);
            return Lerp(asset.restPoints[strand.pointOffset + i0], asset.restPoints[strand.pointOffset + i1], alpha);
        }

        float SampleCanonicalWidth(const HairAsset& asset, const StrandRange& strand, float t)
        {
            if (strand.pointCount == 0u || strand.pointOffset >= asset.pointWidths.size())
            {
                return 0.004f;
            }
            if (strand.pointCount == 1u)
            {
                return asset.pointWidths[strand.pointOffset];
            }
            const float scaled = Clamp(t, 0.0f, 1.0f) * static_cast<float>(strand.pointCount - 1u);
            const uint32_t i0 = static_cast<uint32_t>(std::floor(scaled));
            const uint32_t i1 = std::min(i0 + 1u, strand.pointCount - 1u);
            const float alpha = scaled - static_cast<float>(i0);
            return Lerp(asset.pointWidths[strand.pointOffset + i0], asset.pointWidths[strand.pointOffset + i1], alpha);
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
}
