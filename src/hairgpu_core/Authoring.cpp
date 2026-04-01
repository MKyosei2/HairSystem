#include "hairgpu/Authoring.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace hairgpu
{
    namespace
    {
        constexpr float kEpsilon = 1.0e-6f;

        float Clamp01(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
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

            if (settings.mode == RegionSelectMode::Ellipse)
            {
                const float d = (dx * dx) / (rx * rx) + (dy * dy) / (ry * ry);
                if (d > 1.0f)
                {
                    return false;
                }
                outWeight = 1.0f - std::sqrt(std::max(0.0f, d));
                return true;
            }

            if (settings.mode == RegionSelectMode::Rectangle)
            {
                if (std::abs(dx) > rx || std::abs(dy) > ry)
                {
                    return false;
                }
                const float wx = 1.0f - std::abs(dx) / rx;
                const float wy = 1.0f - std::abs(dy) / ry;
                outWeight = std::min(wx, wy);
                return true;
            }

            return false;
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
