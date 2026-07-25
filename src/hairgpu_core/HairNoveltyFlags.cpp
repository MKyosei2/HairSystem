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
        // The full 01 configuration is the default when the top-level
        // microtype switch is enabled. Individual experiment runs may
        // disable one or more of these flags after normalization.
        if (f.enableMicrotype)
        {
            f.enableMicrotypeCertificate = 1u;
            f.enableMicrotypeDirtyPropagation = 1u;
            f.enableMicrotypeBlockHierarchy = 1u;
            f.enableMicrotypeBoundaryRepair = 1u;
            f.enableMicrotypeWatchdog = 1u;
            f.enableMicrotypeHemisphereOwnership = 1u;
            f.enableMicrotypeIndirectDispatch = 1u;
        }

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
        f = NormalizeMicrotypeAblationFlags(f);
        return f;
    }

        HairNoveltyFeatureFlags NormalizeMicrotypeAblationFlags(
        const HairNoveltyFeatureFlags& inputFlags)
    {
        HairNoveltyFeatureFlags f = inputFlags;

        f.enableMicrotype =
            f.enableMicrotype ? 1u : 0u;

        f.enableMicrotypeCertificate =
            f.enableMicrotypeCertificate ? 1u : 0u;

        f.enableMicrotypeDirtyPropagation =
            f.enableMicrotypeDirtyPropagation ? 1u : 0u;

        f.enableMicrotypeBlockHierarchy =
            f.enableMicrotypeBlockHierarchy ? 1u : 0u;

        f.enableMicrotypeBoundaryRepair =
            f.enableMicrotypeBoundaryRepair ? 1u : 0u;

        f.enableMicrotypeWatchdog =
            f.enableMicrotypeWatchdog ? 1u : 0u;

        f.enableMicrotypeHemisphereOwnership =
            f.enableMicrotypeHemisphereOwnership ? 1u : 0u;

        f.enableMicrotypeIndirectDispatch =
            f.enableMicrotypeIndirectDispatch ? 1u : 0u;

        if (!f.enableMicrotype)
        {
            f.enableMicrotypeCertificate = 0u;
            f.enableMicrotypeDirtyPropagation = 0u;
            f.enableMicrotypeBlockHierarchy = 0u;
            f.enableMicrotypeBoundaryRepair = 0u;
            f.enableMicrotypeWatchdog = 0u;
            f.enableMicrotypeHemisphereOwnership = 0u;
            f.enableMicrotypeIndirectDispatch = 0u;
        }

        if (f.enableNoNoveltyBaseline)
        {
            f.enableMicrotype = 0u;
            f.enableMicrotypeCertificate = 0u;
            f.enableMicrotypeDirtyPropagation = 0u;
            f.enableMicrotypeBlockHierarchy = 0u;
            f.enableMicrotypeBoundaryRepair = 0u;
            f.enableMicrotypeWatchdog = 0u;
            f.enableMicrotypeHemisphereOwnership = 0u;
            f.enableMicrotypeIndirectDispatch = 0u;
        }

        // Boundary repair requires certificates because the repair path
        // must know why a cached group became invalid.
        if (f.enableMicrotypeBoundaryRepair &&
            !f.enableMicrotypeCertificate)
        {
            f.enableMicrotypeBoundaryRepair = 0u;
        }

        // Watchdog without certificates cannot validate static reuse.
        if (f.enableMicrotypeWatchdog &&
            !f.enableMicrotypeCertificate)
        {
            f.enableMicrotypeWatchdog = 0u;
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

        std::string BuildMicrotypeAblationBits(const HairNoveltyFeatureFlags& f)
    {
        std::string bits;
        bits.reserve(7);

        bits.push_back(
            f.enableMicrotypeCertificate ? '1' : '0');

        bits.push_back(
            f.enableMicrotypeDirtyPropagation ? '1' : '0');

        bits.push_back(
            f.enableMicrotypeBlockHierarchy ? '1' : '0');

        bits.push_back(
            f.enableMicrotypeBoundaryRepair ? '1' : '0');

        bits.push_back(
            f.enableMicrotypeWatchdog ? '1' : '0');

        bits.push_back(
            f.enableMicrotypeHemisphereOwnership ? '1' : '0');

        bits.push_back(
            f.enableMicrotypeIndirectDispatch ? '1' : '0');

        return bits;
    }
}
