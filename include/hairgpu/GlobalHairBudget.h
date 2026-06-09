#pragma once

#include <cstdint>
#include "hairgpu/HairNoveltyFlags.h"

namespace hairgpu
{
    struct GlobalHairFrameBudget
    {
        float targetFrameMs = 16.6f;
        float totalHairBudgetMs = 8.0f;

        float dataBudgetMs = 1.0f;
        float physicsBudgetMs = 3.0f;
        float renderBudgetMs = 4.0f;
        float editBudgetMs = 1.0f;
        float runtimeBudgetMs = 0.8f;

        uint32_t maxDecodedStrands = 65536u;
        uint32_t maxRiskyStrands = 16384u;
        uint32_t maxRepairTasks = 8192u;
        uint32_t maxIslandEdges = 32768u;
        uint32_t maxIslandMembers = 16384u;

        uint32_t maxRenderTasks = 65536u;
        uint32_t maxFullQualityRenderStrands = 2048u;
        uint32_t maxHighQualityShadowStrands = 4096u;
        uint32_t maxHighQualitySpecularStrands = 4096u;

        uint32_t maxEditOperations = 512u;
        uint32_t maxEditRepairTasks = 2048u;
        uint32_t maxEditPreviewFullQuality = 1024u;

        uint32_t cpuReadbackAllowed = 1u;
    };

    GlobalHairFrameBudget BuildGlobalHairFrameBudget(const HairNoveltyFeatureFlags& flags);
}
