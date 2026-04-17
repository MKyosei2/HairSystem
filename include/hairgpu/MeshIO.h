#pragma once

#include <filesystem>
#include <string>

#include "hairgpu/Types.h"

namespace hairgpu
{
    bool LoadScalpMeshFromObj(const std::filesystem::path& filePath, ScalpMesh& outMesh, std::string& outError);
    bool SaveScalpMeshToObj(const std::filesystem::path& filePath, const ScalpMesh& mesh, std::string& outError);
}
