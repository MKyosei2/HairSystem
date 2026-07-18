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
        Sector = 3,
    };

    struct RegionSelectionSettings
    {
        RegionSelectMode mode = RegionSelectMode::All;
        Vec2 centerUv = {0.5f, 0.5f};
        Vec2 radiusUv = {0.2f, 0.2f};
        float innerRadius = 0.0f;
        float angleCenterRadians = 0.0f;
        float angleHalfExtentRadians = 3.14159265f;
        float feather = 0.20f;
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

    enum class EditFalloffZone : uint32_t
    {
        Full = 0,
        Root = 1,
        Mid = 2,
        Tip = 3,
    };

    enum class GroomEditSemantic : uint32_t
    {
        Generic = 0,
        Comb = 1,
        Grab = 2,
        Smooth = 3,
        Length = 4,
        Volume = 5,
        Part = 6,
        Clump = 7,
        Curl = 8,
        Freeze = 9,
        StrandFix = 10,
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
        float clumpStrength = 0.0f;
        float curlStrength = 0.0f;
        float partingStrength = 0.0f;
        float partingCenterU = 0.50f;
        float partingWidthUv = 0.14f;
        EditFalloffZone zone = EditFalloffZone::Full;
        GroomEditSemantic semantic = GroomEditSemantic::Generic;
        float flowAlignStrength = 0.0f;
        float smoothStrength = 0.0f;
        float liftStrength = 0.0f;
        float pressStrength = 0.0f;
        float normalOffsetStrength = 0.0f;
        float twistStrength = 0.0f;
    };

    enum class StrandSelectionMode : uint32_t
    {
        None = 0,
        Union = 1,
        Replace = 2,
        GuideFamily = 3,
    };

    struct EditLayer
    {
        std::string name;
        RegionSelectionSettings region = {};
        EditSettings edit = {};
        float layerWeight = 1.0f;
        bool enabled = true;
        std::vector<uint32_t> strandSelection;
        StrandSelectionMode strandSelectionMode = StrandSelectionMode::None;
        float strandSelectionWeight = 1.0f;
    };

    struct EditStack
    {
        std::vector<EditLayer> layers;
        float boundarySmoothing = 0.42f;
        float boundaryRadiusUv = 0.10f;
    };

    struct RuntimeEditState
    {
        std::vector<float> guideInfluence;
        std::vector<float> strandInfluence;
        float maxGuideInfluence = 0.0f;
        uint32_t activeGuideCount = 0u;
    };

    struct ConstraintSettings
    {
        float maxStretchRatio = 1.10f;
        float maxBendRadians = 1.25f;
        float floorY = -10.0f;
        float maxRootRadius = 10.0f;
    };

    struct AngularBendRelaxationSettings
    {
        float maxBendRadians = 1.10f;
        uint32_t iterations = 4u;
        float smoothing = 0.65f;
        bool preserveSegmentLengths = true;
        bool affectRootAdjacentPoint = true;
    };

    struct AngularXpbdBendSettings
    {
        float targetBendRadians = 0.20f;
        float bendCompliance = 5.0e-5f;
        uint32_t iterations = 8u;
        bool preserveSegmentLengths = true;
        bool affectRootAdjacentPoint = false;
        float damping = 0.08f;
    };

    struct ParticleChainCanonicalizationSettings
    {
        uint32_t targetPointCount = 0u;
        bool snapRootsToScalp = true;
        bool reorderGuideFirst = true;
    };


    struct StrandRetessellationSettings
    {
        uint32_t targetPointCount = 0u;
        bool snapRootsToScalp = true;
        bool reorderGuideFirst = true;
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
    bool ParseStrandSelectionSpec(const std::string& text, std::vector<uint32_t>& outIndices, std::string& outError);
    std::string FormatStrandSelectionSpec(const std::vector<uint32_t>& strandIndices);
    bool ApplyEditToRegion(HairAsset& asset, const RegionSelectionResult& selection, const EditSettings& settings, std::string& outError);
    RuntimeEditState BuildRuntimeEditState(const HairAsset& asset, const EditStack& stack);
    bool ApplyEditStackToAsset(HairAsset& asset,
                               const HairAsset& baseAsset,
                               const EditStack& stack,
                               RuntimeEditState* outState,
                               std::string& outError);
    bool LoadEditStackCsv(const std::filesystem::path& path, EditStack& outStack, std::string& outError);
    bool LoadEditStackCsvText(const std::string& text, EditStack& outStack, std::string& outError);
    bool WriteEditStackCsv(const std::filesystem::path& path, const EditStack& stack, std::string& outError);
    bool WriteEditStackTemplateCsv(const std::filesystem::path& path, std::string& outError);
    void ApplyConstraints(HairAsset& asset, const ConstraintSettings& settings);
    void ApplyAngularBendRelaxation(HairAsset& asset, const AngularBendRelaxationSettings& settings);
    void ApplyAngularXpbdBendRelaxation(HairAsset& asset, const AngularXpbdBendSettings& settings);
    bool LoadPaletteCsv(const std::filesystem::path& path, Palette& outPalette, std::string& outError);
    bool ApplyPaletteToRegion(HairAsset& asset, const RegionSelectionResult& selection, const Palette& palette, float strength, std::string& outError);
    bool CanonicalizeHairAssetToParticleChains(HairAsset& asset, const ParticleChainCanonicalizationSettings& settings, std::string& outError);
    bool RetessellateHairAsset(HairAsset& asset, const StrandRetessellationSettings& settings, std::string& outError);

    struct SparseEditResult
    {
        std::vector<uint32_t> affectedStrandIndices;
        std::vector<uint32_t> changedPointIndices;
    };

    bool ApplyEditStackToGuideFamiliesSparse(
        HairAsset& asset,
        const HairAsset& baseAsset,
        const EditStack& stack,
        const std::vector<uint32_t>& affectedGuideIndices,
        SparseEditResult* outResult,
        RuntimeEditState* outState,
        std::string& outError);
}

