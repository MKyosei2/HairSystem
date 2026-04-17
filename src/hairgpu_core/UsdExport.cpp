#include "hairgpu/UsdExport.h"

#include <fstream>
#include <sstream>

namespace hairgpu
{
    namespace
    {
        void WriteVec3Array(std::ofstream& out, const std::vector<Vec3>& values, const char* indent)
        {
            out << indent << "[\n";
            for (size_t i = 0; i < values.size(); ++i)
            {
                const Vec3& v = values[i];
                out << indent << "    (" << v.x << ", " << v.y << ", " << v.z << ")";
                if (i + 1 < values.size()) out << ",";
                out << "\n";
            }
            out << indent << "]\n";
        }

        void WriteVec2Array(std::ofstream& out, const std::vector<Vec2>& values, const char* indent)
        {
            out << indent << "[\n";
            for (size_t i = 0; i < values.size(); ++i)
            {
                const Vec2& v = values[i];
                out << indent << "    (" << v.x << ", " << v.y << ")";
                if (i + 1 < values.size()) out << ",";
                out << "\n";
            }
            out << indent << "]\n";
        }

        template <typename T>
        void WriteScalarArray(std::ofstream& out, const std::vector<T>& values, const char* indent)
        {
            out << indent << "[";
            for (size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0) out << ", ";
                out << values[i];
            }
            out << "]\n";
        }

        void WriteScalpMesh(std::ofstream& out, const ScalpMesh& scalp)
        {
            if (scalp.positions.empty() || scalp.triangles.empty())
            {
                return;
            }

            std::vector<int> faceCounts(scalp.triangles.size(), 3);
            std::vector<int> faceIndices;
            faceIndices.reserve(scalp.triangles.size() * 3u);
            for (const TriangleIndices& tri : scalp.triangles)
            {
                faceIndices.push_back(static_cast<int>(tri.a));
                faceIndices.push_back(static_cast<int>(tri.b));
                faceIndices.push_back(static_cast<int>(tri.c));
            }

            out << "    def Mesh \"Scalp\"\n    {\n";
            out << "        int[] faceVertexCounts = ";
            WriteScalarArray(out, faceCounts, "");
            out << "        int[] faceVertexIndices = ";
            WriteScalarArray(out, faceIndices, "");
            out << "        point3f[] points = ";
            WriteVec3Array(out, scalp.positions, "        ");
            if (!scalp.normals.empty())
            {
                out << "        normal3f[] normals = ";
                WriteVec3Array(out, scalp.normals, "        ");
                out << "        uniform token subdivisionScheme = \"none\"\n";
            }
            else
            {
                out << "        uniform token subdivisionScheme = \"none\"\n";
            }
            if (!scalp.uvs.empty())
            {
                out << "        texCoord2f[] primvars:st = ";
                WriteVec2Array(out, scalp.uvs, "        ");
                out << "        uniform token primvars:st:interpolation = \"vertex\"\n";
            }
            out << "    }\n";
        }

        void WriteHairCurves(std::ofstream& out, const HairAsset& asset, bool includeWidths, bool includeGuidePrimvar, bool includeGroupPrimvar)
        {
            std::vector<int> counts;
            counts.reserve(asset.strands.size());
            std::vector<int> guideFlags;
            std::vector<int> groups;
            if (includeGuidePrimvar) guideFlags.reserve(asset.strands.size());
            if (includeGroupPrimvar) groups.reserve(asset.strands.size());
            for (const StrandRange& strand : asset.strands)
            {
                counts.push_back(static_cast<int>(strand.pointCount));
                if (includeGuidePrimvar)
                {
                    const bool isGuide = (strand.flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u;
                    guideFlags.push_back(isGuide ? 1 : 0);
                }
                if (includeGroupPrimvar)
                {
                    groups.push_back(static_cast<int>(strand.groupId));
                }
            }

            out << "    def BasisCurves \"HairStrands\"\n    {\n";
            out << "        uniform token type = \"linear\"\n";
            out << "        uniform token wrap = \"nonperiodic\"\n";
            out << "        int[] curveVertexCounts = ";
            WriteScalarArray(out, counts, "");
            out << "        point3f[] points = ";
            WriteVec3Array(out, asset.restPoints, "        ");
            if (includeWidths && !asset.pointWidths.empty())
            {
                out << "        float[] widths = ";
                WriteScalarArray(out, asset.pointWidths, "");
            }
            if (includeGuidePrimvar)
            {
                out << "        int[] primvars:guide = ";
                WriteScalarArray(out, guideFlags, "");
                out << "        uniform token primvars:guide:interpolation = \"uniform\"\n";
            }
            if (includeGroupPrimvar)
            {
                out << "        int[] primvars:groupId = ";
                WriteScalarArray(out, groups, "");
                out << "        uniform token primvars:groupId:interpolation = \"uniform\"\n";
            }
            out << "    }\n";
        }

        void WriteCardMesh(std::ofstream& out, const HairCardMesh& mesh)
        {
            if (mesh.vertices.empty() || mesh.indices.empty())
            {
                return;
            }

            std::vector<int> faceCounts(mesh.indices.size() / 3u, 3);
            std::vector<int> faceIndices;
            faceIndices.reserve(mesh.indices.size());
            std::vector<Vec3> positions;
            std::vector<Vec3> normals;
            std::vector<Vec2> uvs;
            positions.reserve(mesh.vertices.size());
            normals.reserve(mesh.vertices.size());
            uvs.reserve(mesh.vertices.size());
            for (const HairCardVertex& v : mesh.vertices)
            {
                positions.push_back(v.position);
                normals.push_back(v.normal);
                uvs.push_back(v.uv);
            }
            for (uint32_t index : mesh.indices)
            {
                faceIndices.push_back(static_cast<int>(index));
            }

            out << "    def Mesh \"HairCards\"\n    {\n";
            out << "        int[] faceVertexCounts = ";
            WriteScalarArray(out, faceCounts, "");
            out << "        int[] faceVertexIndices = ";
            WriteScalarArray(out, faceIndices, "");
            out << "        point3f[] points = ";
            WriteVec3Array(out, positions, "        ");
            out << "        normal3f[] normals = ";
            WriteVec3Array(out, normals, "        ");
            out << "        texCoord2f[] primvars:st = ";
            WriteVec2Array(out, uvs, "        ");
            out << "        uniform token primvars:st:interpolation = \"vertex\"\n";
            out << "        uniform token subdivisionScheme = \"none\"\n";
            out << "    }\n";
        }
    }

    bool SaveHairAssetToUsda(const std::filesystem::path& filePath,
                             const HairAsset& asset,
                             const HairUsdExportSettings& settings,
                             std::string& outError)
    {
        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to write USDA: " + filePath.string();
            return false;
        }

        out << "#usda 1.0\n";
        out << "(\n    upAxis = \"Y\"\n)\n\n";
        out << "def Xform \"HairScene\"\n{\n";
        WriteHairCurves(out, asset, settings.includeWidths, settings.includeGuidePrimvar, settings.includeGroupPrimvar);
        if (settings.includeScalp)
        {
            WriteScalpMesh(out, asset.scalpMesh);
        }
        if (settings.includeCards)
        {
            const HairCardMesh cards = BuildHairCardMesh(asset, settings.cardSettings);
            WriteCardMesh(out, cards);
        }
        out << "}\n";

        outError.clear();
        return true;
    }
}
