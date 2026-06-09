#pragma once

#include <cstdint>
#include <string>

namespace hairgpu
{
    struct HairNoveltyFeatureFlags
    {
        uint32_t enableMicrotype = 0u;          // 01 Physics-Certified Strand Data Type
        uint32_t enableBudgetedPhysics = 0u;    // 02 Budgeted Self-Materializing Strand Physics
        uint32_t enableCertificateRender = 0u;  // 03 Budgeted Certificate-Aware Strand Rendering
        uint32_t enablePerStrandEdit = 0u;      // 04 Certificate-Preserving Per-Strand Editing
        uint32_t enableGpuResident = 0u;        // 05 GPU-Resident Strand Runtime

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

    uint32_t BuildTopLevelNoveltyMask(const HairNoveltyFeatureFlags& flags);
    std::string BuildTopLevelNoveltyBits(const HairNoveltyFeatureFlags& flags);
}
