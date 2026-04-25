#include "hairgpu/MeshIO.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "hairgpu/CsvUtil.h"
namespace hairgpu
{
    namespace
    {
        struct ObjIndex
        {
            int position = 0;
            int texcoord = 0;
            int normal = 0;
        };

        int ResolveObjIndex(int rawIndex, size_t count)
        {
            if (rawIndex > 0)
            {
                return rawIndex - 1;
            }
            if (rawIndex < 0)
            {
                const int resolved = static_cast<int>(count) + rawIndex;
                return resolved;
            }
            return -1;
        }

        bool ParseObjIndex(const std::string& token, ObjIndex& outIndex)
        {
            outIndex = {};
            std::stringstream ss(token);
            std::string item;
            int part = 0;
            while (std::getline(ss, item, '/'))
            {
                if (!item.empty())
                {
                    try
                    {
                        const int value = std::stoi(item);
                        if (part == 0) outIndex.position = value;
                        else if (part == 1) outIndex.texcoord = value;
                        else if (part == 2) outIndex.normal = value;
                    }
                    catch (...)
                    {
                        return false;
                    }
                }
                ++part;
            }
            return outIndex.position != 0;
        }

        void AccumulateTriangleNormal(const Vec3& a, const Vec3& b, const Vec3& c, Vec3& outNormal)
        {
            outNormal += Cross(b - a, c - a);
        }
    }

    bool LoadScalpMeshFromObj(const std::filesystem::path& filePath, ScalpMesh& outMesh, std::string& outError)
    {
        std::ifstream in(filePath);
        if (!in)
        {
            outError = "Failed to open OBJ: " + filePath.string();
            return false;
        }

        std::vector<Vec3> positions;
        std::vector<Vec3> normals;
        std::vector<Vec2> uvs;
        std::vector<TriangleIndices> triangles;

        std::string line;
        while (std::getline(in, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            std::istringstream ss(line);
            std::string token;
            ss >> token;
            if (token == "v")
            {
                Vec3 p = {};
                if (!(ss >> p.x >> p.y >> p.z))
                {
                    outError = "Malformed vertex row in OBJ.";
                    return false;
                }
                positions.push_back(p);
            }
            else if (token == "vn")
            {
                Vec3 n = {};
                if (!(ss >> n.x >> n.y >> n.z))
                {
                    outError = "Malformed normal row in OBJ.";
                    return false;
                }
                normals.push_back(Normalize(n));
            }
            else if (token == "vt")
            {
                Vec2 uv = {};
                if (!(ss >> uv.x >> uv.y))
                {
                    outError = "Malformed texcoord row in OBJ.";
                    return false;
                }
                uvs.push_back(uv);
            }
            else if (token == "f")
            {
                std::vector<ObjIndex> face;
                std::string indexToken;
                while (ss >> indexToken)
                {
                    ObjIndex idx;
                    if (!ParseObjIndex(indexToken, idx))
                    {
                        outError = "Malformed face row in OBJ.";
                        return false;
                    }
                    face.push_back(idx);
                }

                if (face.size() < 3)
                {
                    continue;
                }

                for (size_t i = 1; i + 1 < face.size(); ++i)
                {
                    TriangleIndices tri = {};
                    tri.a = static_cast<uint32_t>(ResolveObjIndex(face[0].position, positions.size()));
                    tri.b = static_cast<uint32_t>(ResolveObjIndex(face[i].position, positions.size()));
                    tri.c = static_cast<uint32_t>(ResolveObjIndex(face[i + 1].position, positions.size()));
                    if (tri.a >= positions.size() || tri.b >= positions.size() || tri.c >= positions.size())
                    {
                        outError = "OBJ face references an invalid vertex index.";
                        return false;
                    }
                    triangles.push_back(tri);
                }
            }
        }

        if (positions.empty() || triangles.empty())
        {
            outError = "OBJ did not contain any usable triangle mesh data.";
            return false;
        }

        outMesh = {};
        outMesh.positions = std::move(positions);
        outMesh.triangles = std::move(triangles);
        outMesh.normals.resize(outMesh.positions.size(), { 0.0f, 0.0f, 0.0f });
        outMesh.uvs.resize(outMesh.positions.size(), { 0.5f, 0.5f });
        outMesh.skinWeights.resize(outMesh.positions.size(), { {0u, 0u, 0u, 0u}, {1.0f, 0.0f, 0.0f, 0.0f} });
        outMesh.boneCount = 1;

        if (!uvs.empty())
        {
            const size_t copyCount = std::min(uvs.size(), outMesh.uvs.size());
            std::copy_n(uvs.begin(), copyCount, outMesh.uvs.begin());
        }

        if (!normals.empty())
        {
            const size_t copyCount = std::min(normals.size(), outMesh.normals.size());
            std::copy_n(normals.begin(), copyCount, outMesh.normals.begin());
        }
        else
        {
            for (const TriangleIndices& tri : outMesh.triangles)
            {
                const Vec3& a = outMesh.positions[tri.a];
                const Vec3& b = outMesh.positions[tri.b];
                const Vec3& c = outMesh.positions[tri.c];
                AccumulateTriangleNormal(a, b, c, outMesh.normals[tri.a]);
                AccumulateTriangleNormal(a, b, c, outMesh.normals[tri.b]);
                AccumulateTriangleNormal(a, b, c, outMesh.normals[tri.c]);
            }
            for (Vec3& normal : outMesh.normals)
            {
                normal = Normalize(normal);
                if (Length(normal) <= 1.0e-6f)
                {
                    normal = Vec3(0.0f, 1.0f, 0.0f);
                }
            }
        }

        outError.clear();
        return true;
    }

    bool SaveScalpMeshToObj(const std::filesystem::path& filePath, const ScalpMesh& mesh, std::string& outError)
    {
        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to write OBJ: " + filePath.string();
            return false;
        }

        for (const Vec3& p : mesh.positions)
        {
            out << "v " << p.x << ' ' << p.y << ' ' << p.z << "\n";
        }
        for (const Vec2& uv : mesh.uvs)
        {
            out << "vt " << uv.x << ' ' << uv.y << "\n";
        }
        for (const Vec3& n : mesh.normals)
        {
            out << "vn " << n.x << ' ' << n.y << ' ' << n.z << "\n";
        }
        for (const TriangleIndices& tri : mesh.triangles)
        {
            const uint32_t a = tri.a + 1u;
            const uint32_t b = tri.b + 1u;
            const uint32_t c = tri.c + 1u;
            out << "f "
                << a << '/' << a << '/' << a << ' '
                << b << '/' << b << '/' << b << ' '
                << c << '/' << c << '/' << c << "\n";
        }

        outError.clear();
        return true;
    }
}
