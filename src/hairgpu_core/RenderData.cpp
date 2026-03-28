#include "hairgpu/RenderData.h"

namespace hairgpu
{
    HairSegmentRenderData BuildSegmentRenderData(const HairAsset& asset)
    {
        HairSegmentRenderData data;
        if (asset.particlesPerStrand < 2u || asset.strands.empty())
        {
            return data;
        }

        data.segmentVertices.reserve(asset.strands.size() * (asset.particlesPerStrand - 1u) * 2ull);
        for (uint32_t strandIndex = 0; strandIndex < static_cast<uint32_t>(asset.strands.size()); ++strandIndex)
        {
            const StrandRange& range = asset.strands[strandIndex];
            for (uint32_t i = 0; i + 1u < range.pointCount; ++i)
            {
                data.segmentVertices.push_back({ range.pointOffset + i, strandIndex });
                data.segmentVertices.push_back({ range.pointOffset + i + 1u, strandIndex });
                ++data.segmentCount;
            }
        }
        return data;
    }
}
