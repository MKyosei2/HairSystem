#pragma once

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct ObjMeshView
    {
        const std::vector<Vec3>* positions = nullptr;
        const std::vector<Vec2>* uvs = nullptr;
        const std::vector<Vec3>* normals = nullptr;
        const std::vector<uint32_t>* indices = nullptr;
    };

    inline bool WriteObjMesh(const std::filesystem::path& filePath,
                             const ObjMeshView& mesh,
                             const std::string& comment,
                             std::string& outError)
    {
        if (mesh.positions == nullptr || mesh.indices == nullptr)
        {
            outError = "Invalid OBJ mesh view";
            return false;
        }

        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to write OBJ: " + filePath.string();
            return false;
        }

        if (!comment.empty())
        {
            out << "# " << comment << "\n";
        }

        for (const Vec3& p : *mesh.positions)
        {
            out << "v " << p.x << ' ' << p.y << ' ' << p.z << "\n";
        }
        if (mesh.uvs != nullptr)
        {
            for (const Vec2& uv : *mesh.uvs)
            {
                out << "vt " << uv.x << ' ' << uv.y << "\n";
            }
        }
        if (mesh.normals != nullptr)
        {
            for (const Vec3& n : *mesh.normals)
            {
                out << "vn " << n.x << ' ' << n.y << ' ' << n.z << "\n";
            }
        }

        const bool hasUvs = mesh.uvs != nullptr && mesh.uvs->size() >= mesh.positions->size();
        const bool hasNormals = mesh.normals != nullptr && mesh.normals->size() >= mesh.positions->size();
        for (size_t i = 0u; i + 2u < mesh.indices->size(); i += 3u)
        {
            const uint32_t a = (*mesh.indices)[i] + 1u;
            const uint32_t b = (*mesh.indices)[i + 1u] + 1u;
            const uint32_t c = (*mesh.indices)[i + 2u] + 1u;

            auto writeIndex = [&](uint32_t index)
            {
                if (hasUvs && hasNormals)
                {
                    out << index << '/' << index << '/' << index;
                }
                else if (hasUvs)
                {
                    out << index << '/' << index;
                }
                else if (hasNormals)
                {
                    out << index << "//" << index;
                }
                else
                {
                    out << index;
                }
            };

            out << "f ";
            writeIndex(a);
            out << ' ';
            writeIndex(b);
            out << ' ';
            writeIndex(c);
            out << "\n";
        }

        outError.clear();
        return true;
    }
}
