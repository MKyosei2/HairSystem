#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/HairCache.h"
#include "hairgpu/Types.h"

namespace hairgpu
{
    struct BonePoseFrame
    {
        std::vector<Mat4> boneMatrices;
    };

    struct BoneAnimationClip
    {
        uint32_t boneCount = 0;
        float frameRate = 30.0f;
        std::vector<BonePoseFrame> frames;

        [[nodiscard]] bool Empty() const
        {
            return boneCount == 0u || frames.empty();
        }
    };

    bool LoadBoneAnimationCsv(const std::filesystem::path& path, BoneAnimationClip& outClip, std::string& outError);
    bool SaveBoneAnimationCsv(const std::filesystem::path& path, const BoneAnimationClip& clip, std::string& outError);
    bool SaveBoneAnimationTemplateCsv(const std::filesystem::path& path, uint32_t boneCount, uint32_t frameCount, float frameRate, std::string& outError);
    bool EvaluateBoneAnimation(const BoneAnimationClip& clip, float timeSeconds, bool loop, std::vector<Mat4>& outMatrices);
    bool BuildScalpAnimatedHairCache(const HairAsset& asset,
                                     const BoneAnimationClip& clip,
                                     bool translateEntireStrands,
                                     HairCache& outCache,
                                     std::string& outError);
}
