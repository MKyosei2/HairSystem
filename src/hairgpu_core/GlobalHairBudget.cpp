#include "hairgpu/GlobalHairBudget.h"

#include <algorithm>

namespace hairgpu
{
    GlobalHairFrameBudget BuildGlobalHairFrameBudget(const HairNoveltyFeatureFlags& flags)
    {
        GlobalHairFrameBudget b = {};

        if (!flags.enableMicrotype)
        {
            b.dataBudgetMs = 0.0f;
            b.maxDecodedStrands = 0u;
        }

        if (!flags.enableBudgetedPhysics)
        {
            b.physicsBudgetMs = 0.0f;
            b.maxRiskyStrands = 0u;
            b.maxRepairTasks = 0u;
            b.maxIslandEdges = 0u;
            b.maxIslandMembers = 0u;
        }

        if (!flags.enableCertificateRender)
        {
            b.renderBudgetMs = 0.0f;
            b.maxRenderTasks = 0u;
            b.maxFullQualityRenderStrands = 0u;
            b.maxHighQualityShadowStrands = 0u;
            b.maxHighQualitySpecularStrands = 0u;
        }

        if (!flags.enablePerStrandEdit)
        {
            b.editBudgetMs = 0.0f;
            b.maxEditOperations = 0u;
            b.maxEditRepairTasks = 0u;
            b.maxEditPreviewFullQuality = 0u;
        }

        if (flags.enableGpuResident)
        {
            b.cpuReadbackAllowed = 0u;
        }

        if (flags.enableMicrotype &&
            flags.enableBudgetedPhysics &&
            flags.enableCertificateRender &&
            flags.enablePerStrandEdit &&
            flags.enableGpuResident)
        {
            b.physicsBudgetMs = std::min(b.physicsBudgetMs, 2.5f);
            b.renderBudgetMs = std::min(b.renderBudgetMs, 3.5f);
            b.editBudgetMs = std::min(b.editBudgetMs, 0.8f);

            b.maxRiskyStrands = std::min<uint32_t>(b.maxRiskyStrands, 8192u);
            b.maxRepairTasks = std::min<uint32_t>(b.maxRepairTasks, 4096u);
            b.maxIslandEdges = std::min<uint32_t>(b.maxIslandEdges, 16384u);
            b.maxIslandMembers = std::min<uint32_t>(b.maxIslandMembers, 8192u);
            b.maxFullQualityRenderStrands = std::min<uint32_t>(b.maxFullQualityRenderStrands, 1024u);
            b.maxEditPreviewFullQuality = std::min<uint32_t>(b.maxEditPreviewFullQuality, 512u);
        }

        return b;
    }
}
