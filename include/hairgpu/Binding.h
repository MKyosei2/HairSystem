#pragma once

#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct RootBindingQualityReport
    {
        float averageRootDistance = 0.0f;
        float maxRootDistance = 0.0f;
        uint32_t invalidStrandCount = 0;
    };

    bool GenerateRootBindingsFromScalp(HairAsset& asset, bool snapRootsToScalp, RootBindingQualityReport* outReport = nullptr);
    std::vector<Vec3> ComputeSkinnedScalpPositions(const ScalpMesh& scalpMesh, const std::vector<Mat4>& boneMatrices);
    void RebuildRootRestPositionsFromScalpPositions(HairAsset& asset, const std::vector<Vec3>& scalpPositions, bool translateEntireStrands);
}
