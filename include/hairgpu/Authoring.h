#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    enum class RegionSelectMode : uint32_t
    {
        All = 0,
        Ellipse = 1,
        Rectangle = 2,
    };

    struct RegionSelectionSettings
    {
        RegionSelectMode mode = RegionSelectMode::All;
        Vec2 centerUv = {0.5f, 0.5f};
        Vec2 radiusUv = {0.2f, 0.2f};
        uint32_t groupId = UINT32_MAX;
        bool guidesOnly = false;
    };

    struct SelectedStrand
    {
        uint32_t strandIndex = 0;
        float weight = 1.0f;
    };

    struct RegionSelectionResult
    {
        std::vector<SelectedStrand> strands;
    };

    struct EditSettings
    {
        float lengthScale = 1.0f;
        float widthScale = 1.0f;
        Vec3 rootOffset = {0.0f, 0.0f, 0.0f};
        Vec3 tipOffset = {0.0f, 0.0f, 0.0f};
        float sideOffset = 0.0f;
        float yawRadians = 0.0f;
        float followerInfluence = 1.0f;
    };

    struct ConstraintSettings
    {
        float maxStretchRatio = 1.10f;
        float maxBendRadians = 1.25f;
        float floorY = -10.0f;
        float maxRootRadius = 10.0f;
    };

    struct PaletteSample
    {
        float u = 0.0f;
        Vec3 offset = {0.0f, 0.0f, 0.0f};
        float widthScale = 1.0f;
    };

    struct Palette
    {
        std::vector<PaletteSample> samples;
    };

    RegionSelectionResult SelectRegion(const HairAsset& asset, const RegionSelectionSettings& settings);
    bool ApplyEditToRegion(HairAsset& asset, const RegionSelectionResult& selection, const EditSettings& settings, std::string& outError);
    void ApplyConstraints(HairAsset& asset, const ConstraintSettings& settings);
    bool LoadPaletteCsv(const std::filesystem::path& path, Palette& outPalette, std::string& outError);
    bool ApplyPaletteToRegion(HairAsset& asset, const RegionSelectionResult& selection, const Palette& palette, float strength, std::string& outError);
}
