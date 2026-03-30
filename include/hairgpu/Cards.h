#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct HairCardBuilderSettings
    {
        uint32_t cardsPerGuide = 1;
        uint32_t segmentsPerCard = 8;
        float widthScale = 1.15f;
        float widthBias = 0.0025f;
        bool includeGuideInCluster = true;
    };

    struct HairCardVertex
    {
        Vec3 position = {};
        Vec3 normal = { 0.0f, 0.0f, 1.0f };
        Vec2 uv = {};
    };

    struct HairCardMesh
    {
        std::vector<HairCardVertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t cardCount = 0;
    };

    HairCardMesh BuildHairCardMesh(const HairAsset& asset, const HairCardBuilderSettings& settings = {});
    bool SaveHairCardMeshToObj(const std::filesystem::path& filePath, const HairCardMesh& mesh, std::string& outError);
}
