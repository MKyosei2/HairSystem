#pragma once

#include <cstdint>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct SegmentRenderVertex
    {
        uint32_t particleIndex = 0;
        uint32_t strandIndex = 0;
    };

    struct HairSegmentRenderData
    {
        std::vector<SegmentRenderVertex> segmentVertices;
        uint32_t segmentCount = 0;
    };

    HairSegmentRenderData BuildSegmentRenderData(const HairAsset& asset);
}
