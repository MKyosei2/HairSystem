#pragma once

#include <cstdint>
#include <string>

namespace hairgpu
{
    struct HairNoveltyFeatureFlags
    {
        // Top-level novelty switches.
        uint32_t enableMicrotype = 0u;
        uint32_t enableBudgetedPhysics = 0u;
        uint32_t enableCertificateRender = 0u;
        uint32_t enablePerStrandEdit = 0u;
        uint32_t enableGpuResident = 0u;

        // 01 internal ablation switches.
        uint32_t enableMicrotypeCertificate = 0u;
        uint32_t enableMicrotypeDirtyPropagation = 0u;
        uint32_t enableMicrotypeBlockHierarchy = 0u;
        uint32_t enableMicrotypeBoundaryRepair = 0u;
        uint32_t enableMicrotypeWatchdog = 0u;
        uint32_t enableMicrotypeHemisphereOwnership = 0u;
        uint32_t enableMicrotypeIndirectDispatch = 0u;

        // 02 internal switches.
        uint32_t enableCBMA = 0u;
        uint32_t enablePhysicsIslands = 0u;
        uint32_t enableRodRepair = 0u;

        uint32_t enableNoNoveltyBaseline = 0u;
    };

    HairNoveltyFeatureFlags NormalizeNoveltyFlags(
        uint32_t enableMicrotype,
        uint32_t enableBudgetedPhysics,
        uint32_t enableCertificateRender,
        uint32_t enablePerStrandEdit,
        uint32_t enableGpuResident,
        uint32_t enableCBMA,
        uint32_t enablePhysicsIslands,
        uint32_t enableRodRepair,
        uint32_t enableNoNoveltyBaseline);
    HairNoveltyFeatureFlags NormalizeMicrotypeAblationFlags(
        const HairNoveltyFeatureFlags& inputFlags);
    std::string BuildMicrotypeAblationBits(
        const HairNoveltyFeatureFlags& flags);
    uint32_t BuildTopLevelNoveltyMask(const HairNoveltyFeatureFlags& flags);
    std::string BuildTopLevelNoveltyBits(const HairNoveltyFeatureFlags& flags);
}
