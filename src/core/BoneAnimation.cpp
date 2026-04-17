#include "hairgpu/BoneAnimation.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "hairgpu/Binding.h"

namespace hairgpu
{
    namespace
    {
        bool ParseAnimationMatrixLine(const std::string& line, uint32_t& outFrameIndex, uint32_t& outBoneIndex, Mat4& outMatrix)
        {
            std::string normalized = line;
            for (char& c : normalized)
            {
                if (c == ',')
                {
                    c = ' ';
                }
            }

            std::stringstream ss(normalized);
            std::vector<float> values;
            float value = 0.0f;
            while (ss >> value)
            {
                values.push_back(value);
            }

            if (values.size() != 18u)
            {
                return false;
            }

            outFrameIndex = static_cast<uint32_t>(std::max(0.0f, values[0]));
            outBoneIndex = static_cast<uint32_t>(std::max(0.0f, values[1]));
            for (size_t i = 0; i < 16u; ++i)
            {
                outMatrix.m[i] = values[i + 2u];
            }
            return true;
        }

        Mat4 LerpMatrix(const Mat4& a, const Mat4& b, float t)
        {
            Mat4 result = {};
            for (size_t i = 0; i < 16u; ++i)
            {
                result.m[i] = Lerp(a.m[i], b.m[i], t);
            }
            return result;
        }
    }

    bool LoadBoneAnimationCsv(const std::filesystem::path& path, BoneAnimationClip& outClip, std::string& outError)
    {
        std::ifstream in(path);
        if (!in)
        {
            outError = "Failed to open bone animation CSV: " + path.string();
            return false;
        }

        BoneAnimationClip clip = {};
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty())
            {
                continue;
            }
            if (line[0] == '#')
            {
                const std::string key = "# frameRate=";
                if (line.rfind(key, 0u) == 0u)
                {
                    try { clip.frameRate = std::max(1.0f, std::stof(line.substr(key.size()))); } catch (...) {}
                }
                continue;
            }

            uint32_t frameIndex = 0u;
            uint32_t boneIndex = 0u;
            Mat4 matrix = Identity();
            if (!ParseAnimationMatrixLine(line, frameIndex, boneIndex, matrix))
            {
                continue;
            }

            clip.boneCount = std::max<uint32_t>(clip.boneCount, boneIndex + 1u);
            if (frameIndex >= clip.frames.size())
            {
                clip.frames.resize(frameIndex + 1u);
            }
            BonePoseFrame& frame = clip.frames[frameIndex];
            if (frame.boneMatrices.size() < clip.boneCount)
            {
                frame.boneMatrices.resize(clip.boneCount, Identity());
            }
            frame.boneMatrices[boneIndex] = matrix;
        }

        if (clip.Empty())
        {
            outError = "Bone animation CSV did not contain any valid frame,bone,matrix rows.";
            return false;
        }

        for (BonePoseFrame& frame : clip.frames)
        {
            frame.boneMatrices.resize(clip.boneCount, Identity());
        }
        outClip = std::move(clip);
        outError.clear();
        return true;
    }

    bool SaveBoneAnimationCsv(const std::filesystem::path& path, const BoneAnimationClip& clip, std::string& outError)
    {
        if (clip.Empty())
        {
            outError = "Bone animation clip is empty.";
            return false;
        }

        std::ofstream out(path);
        if (!out)
        {
            outError = "Failed to open output bone animation CSV: " + path.string();
            return false;
        }

        out << "# frameRate=" << clip.frameRate << "\n";
        out << "# frameIndex,boneIndex,m00,m01,m02,m03,m10,m11,m12,m13,m20,m21,m22,m23,m30,m31,m32,m33\n";
        for (uint32_t frameIndex = 0u; frameIndex < clip.frames.size(); ++frameIndex)
        {
            const BonePoseFrame& frame = clip.frames[frameIndex];
            for (uint32_t boneIndex = 0u; boneIndex < clip.boneCount; ++boneIndex)
            {
                out << frameIndex << ',' << boneIndex;
                for (float m : frame.boneMatrices[boneIndex].m)
                {
                    out << ',' << m;
                }
                out << '\n';
            }
        }

        outError.clear();
        return true;
    }

    bool SaveBoneAnimationTemplateCsv(const std::filesystem::path& path, uint32_t boneCount, uint32_t frameCount, float frameRate, std::string& outError)
    {
        BoneAnimationClip clip = {};
        clip.boneCount = std::max<uint32_t>(1u, boneCount);
        clip.frameRate = std::max(frameRate, 1.0f);
        clip.frames.resize(std::max<uint32_t>(1u, frameCount));
        for (BonePoseFrame& frame : clip.frames)
        {
            frame.boneMatrices.resize(clip.boneCount, Identity());
        }
        return SaveBoneAnimationCsv(path, clip, outError);
    }

    bool EvaluateBoneAnimation(const BoneAnimationClip& clip, float timeSeconds, bool loop, std::vector<Mat4>& outMatrices)
    {
        if (clip.Empty() || clip.frameRate <= 0.0f)
        {
            outMatrices.clear();
            return false;
        }

        const uint32_t frameCount = static_cast<uint32_t>(clip.frames.size());
        const float frameTime = 1.0f / clip.frameRate;
        const float totalDuration = frameTime * static_cast<float>(frameCount);
        float sampleTime = std::max(timeSeconds, 0.0f);
        if (loop && totalDuration > 1.0e-6f)
        {
            sampleTime = std::fmod(sampleTime, totalDuration);
            if (sampleTime < 0.0f)
            {
                sampleTime += totalDuration;
            }
        }
        else
        {
            sampleTime = std::min(sampleTime, std::max(totalDuration - frameTime, 0.0f));
        }

        const float frameIndexF = sampleTime / frameTime;
        const uint32_t frame0 = std::min<uint32_t>(static_cast<uint32_t>(std::floor(frameIndexF)), frameCount - 1u);
        const uint32_t frame1 = loop ? ((frame0 + 1u) % frameCount) : std::min<uint32_t>(frame0 + 1u, frameCount - 1u);
        const float t = std::clamp(frameIndexF - std::floor(frameIndexF), 0.0f, 1.0f);

        outMatrices.resize(clip.boneCount, Identity());
        for (uint32_t boneIndex = 0u; boneIndex < clip.boneCount; ++boneIndex)
        {
            outMatrices[boneIndex] = LerpMatrix(clip.frames[frame0].boneMatrices[boneIndex], clip.frames[frame1].boneMatrices[boneIndex], t);
        }
        return true;
    }

    bool BuildScalpAnimatedHairCache(const HairAsset& asset,
                                     const BoneAnimationClip& clip,
                                     bool translateEntireStrands,
                                     HairCache& outCache,
                                     std::string& outError)
    {
        if (clip.Empty())
        {
            outError = "Bone animation clip is empty.";
            return false;
        }
        if (asset.scalpMesh.positions.empty() || asset.scalpMesh.triangles.empty())
        {
            outError = "Asset does not contain a skinnable scalp mesh.";
            return false;
        }
        if (asset.rootBindings.size() < asset.strands.size())
        {
            outError = "Asset does not contain root bindings for all strands.";
            return false;
        }

        if (!InitializeHairCacheForAsset(asset, clip.frameRate, outCache, outError))
        {
            return false;
        }

        HairAsset posed = asset;
        for (const BonePoseFrame& frame : clip.frames)
        {
            posed = asset;
            const std::vector<Vec3> scalpPositions = ComputeSkinnedScalpPositions(asset.scalpMesh, frame.boneMatrices);
            RebuildRootRestPositionsFromScalpPositions(posed, scalpPositions, translateEntireStrands);
            if (!AppendHairCacheFrame(outCache, posed, posed.restPoints, outError))
            {
                return false;
            }
        }

        outError.clear();
        return true;
    }
}
