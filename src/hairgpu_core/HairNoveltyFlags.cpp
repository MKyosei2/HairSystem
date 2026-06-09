#include "hairgpu/HairNoveltyFlags.h"

namespace hairgpu
{
    HairNoveltyFeatureFlags NormalizeNoveltyFlags(
        uint32_t enableMicrotype,
        uint32_t enableBudgetedPhysics,
        uint32_t enableCertificateRender,
        uint32_t enablePerStrandEdit,
        uint32_t enableGpuResident,
        uint32_t enableCBMA,
        uint32_t enablePhysicsIslands,
        uint32_t enableRodRepair,
        uint32_t enableNoNoveltyBaseline)
    {
        HairNoveltyFeatureFlags f = {};
        f.enableMicrotype = enableMicrotype ? 1u : 0u;
        f.enableBudgetedPhysics = enableBudgetedPhysics ? 1u : 0u;
        f.enableCertificateRender = enableCertificateRender ? 1u : 0u;
        f.enablePerStrandEdit = enablePerStrandEdit ? 1u : 0u;
        f.enableGpuResident = enableGpuResident ? 1u : 0u;
        f.enableCBMA = enableCBMA ? 1u : 0u;
        f.enablePhysicsIslands = enablePhysicsIslands ? 1u : 0u;
        f.enableRodRepair = enableRodRepair ? 1u : 0u;
        f.enableNoNoveltyBaseline = enableNoNoveltyBaseline ? 1u : 0u;

        if (f.enableCBMA || f.enablePhysicsIslands || f.enableRodRepair)
        {
            f.enableBudgetedPhysics = 1u;
        }

        if (f.enableBudgetedPhysics)
        {
            if (!f.enableCBMA) f.enableCBMA = 1u;
            if (!f.enablePhysicsIslands) f.enablePhysicsIslands = 1u;
            if (!f.enableRodRepair) f.enableRodRepair = 1u;
        }

        if (f.enableNoNoveltyBaseline)
        {
            f = {};
            f.enableNoNoveltyBaseline = 1u;
        }

        return f;
    }

    uint32_t BuildTopLevelNoveltyMask(const HairNoveltyFeatureFlags& f)
    {
        uint32_t mask = 0u;
        if (f.enableMicrotype) mask |= 1u << 0;
        if (f.enableBudgetedPhysics) mask |= 1u << 1;
        if (f.enableCertificateRender) mask |= 1u << 2;
        if (f.enablePerStrandEdit) mask |= 1u << 3;
        if (f.enableGpuResident) mask |= 1u << 4;
        return mask;
    }

    std::string BuildTopLevelNoveltyBits(const HairNoveltyFeatureFlags& f)
    {
        std::string bits;
        bits.reserve(5);
        bits.push_back(f.enableMicrotype ? '1' : '0');
        bits.push_back(f.enableBudgetedPhysics ? '1' : '0');
        bits.push_back(f.enableCertificateRender ? '1' : '0');
        bits.push_back(f.enablePerStrandEdit ? '1' : '0');
        bits.push_back(f.enableGpuResident ? '1' : '0');
        return bits;
    }
}
