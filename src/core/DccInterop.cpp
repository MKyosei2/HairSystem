#include "hairgpu/DccInterop.h"

#include "hairgpu/Binding.h"

#include <fstream>
#include <sstream>

namespace hairgpu
{
    std::string ToString(DccInteropFlavor flavor)
    {
        switch (flavor)
        {
        case DccInteropFlavor::BlenderCurves: return "blender_curves";
        case DccInteropFlavor::MayaNurbsCurves: return "maya_nurbs_curves";
        case DccInteropFlavor::HoudiniCurves: return "houdini_curves";
        default: return "unknown";
        }
    }

    bool ParseDccInteropFlavor(const std::string& text, DccInteropFlavor& outFlavor)
    {
        if (text == "blender" || text == "blender_curves")
        {
            outFlavor = DccInteropFlavor::BlenderCurves;
            return true;
        }
        if (text == "maya" || text == "maya_nurbs" || text == "maya_nurbs_curves")
        {
            outFlavor = DccInteropFlavor::MayaNurbsCurves;
            return true;
        }
        if (text == "houdini" || text == "houdini_curves")
        {
            outFlavor = DccInteropFlavor::HoudiniCurves;
            return true;
        }
        return false;
    }

    namespace
    {
        void WriteJsonString(std::ostream& out, const std::string& value)
        {
            out << '"';
            for (char c : value)
            {
                switch (c)
                {
                case '\\': out << "\\\\"; break;
                case '"': out << "\\\""; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default: out << c; break;
                }
            }
            out << '"';
        }
    }

    bool SaveHairAssetToDccInteropJson(const std::filesystem::path& filePath,
                                       const HairAsset& inputAsset,
                                       const DccInteropExportSettings& settings,
                                       std::string& outError)
    {
        HairAsset asset = inputAsset;
        NormalizeScalpSkinWeights(asset.scalpMesh, true);
        std::error_code ec;
        if (filePath.has_parent_path())
        {
            std::filesystem::create_directories(filePath.parent_path(), ec);
        }
        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to open DCC interop JSON for writing: " + filePath.string();
            return false;
        }
        out.setf(std::ios::fixed);
        out.precision(9);

        std::vector<HairLodAsset> lods;
        if (settings.includeLods)
        {
            HairLodBuildSettings lodSettings = settings.lodSettings;
            lodSettings.levelCount = std::max<uint32_t>(1u, settings.lodCount);
            lods = BuildAdaptiveHairLodChain(asset, lodSettings);
        }

        out << "{\n  \"schema\": \"hairgpu_dcc_interop_v1\",\n  \"flavor\": ";
        WriteJsonString(out, ToString(settings.flavor));
        out << ",\n  \"guideStrandCount\": " << asset.guideStrandCount
            << ",\n  \"followerStrandCount\": " << asset.followerStrandCount
            << ",\n  \"particlesPerStrand\": " << asset.particlesPerStrand
            << ",\n  \"strands\": [\n";
        for (size_t strandIndex = 0; strandIndex < asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            const FollowData follow = (strandIndex < asset.followData.size()) ? asset.followData[strandIndex] : FollowData{};
            const RootBinding binding = (strandIndex < asset.rootBindings.size()) ? asset.rootBindings[strandIndex] : RootBinding{};
            out << "    {\n      \"index\": " << strandIndex
                << ",\n      \"groupId\": " << strand.groupId
                << ",\n      \"flags\": " << strand.flags
                << ",\n      \"parentGuide\": " << follow.parentGuide
                << ",\n      \"points\": [";
            for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
            {
                const Vec3 p = asset.restPoints[strand.pointOffset + pointIndex];
                if (pointIndex > 0u) out << ',';
                out << '[' << p.x << ',' << p.y << ',' << p.z << ']';
            }
            out << "]";
            if (settings.includeRootBindings)
            {
                out << ",\n      \"rootBinding\": {\"triangleIndex\": " << binding.triangleIndex
                    << ", \"barycentric\": [" << binding.barycentric.x << ',' << binding.barycentric.y << ',' << binding.barycentric.z
                    << "], \"uv\": [" << binding.rootUv.x << ',' << binding.rootUv.y << "]}";
            }
            out << "\n    }" << (strandIndex + 1u < asset.strands.size() ? "," : "") << "\n";
        }
        out << "  ]";

        if (settings.includeScalp)
        {
            out << ",\n  \"scalp\": {\n    \"positions\": [";
            for (size_t i = 0; i < asset.scalpMesh.positions.size(); ++i)
            {
                const Vec3 p = asset.scalpMesh.positions[i];
                if (i > 0u) out << ',';
                out << '[' << p.x << ',' << p.y << ',' << p.z << ']';
            }
            out << "],\n    \"triangles\": [";
            for (size_t i = 0; i < asset.scalpMesh.triangles.size(); ++i)
            {
                const TriangleIndices& t = asset.scalpMesh.triangles[i];
                if (i > 0u) out << ',';
                out << '[' << t.a << ',' << t.b << ',' << t.c << ']';
            }
            out << ']';
            if (settings.includeSkinning)
            {
                out << ",\n    \"skinWeights\": [";
                for (size_t i = 0; i < asset.scalpMesh.skinWeights.size(); ++i)
                {
                    const SkinWeight4& w = asset.scalpMesh.skinWeights[i];
                    if (i > 0u) out << ',';
                    out << "{\"bones\":[" << w.boneIndices[0] << ',' << w.boneIndices[1] << ',' << w.boneIndices[2] << ',' << w.boneIndices[3]
                        << "],\"weights\":[" << w.boneWeights[0] << ',' << w.boneWeights[1] << ',' << w.boneWeights[2] << ',' << w.boneWeights[3] << "]}";
                }
                out << ']';
            }
            out << "\n  }";
        }

        if (settings.includeLods)
        {
            out << ",\n  \"lods\": [\n";
            for (size_t i = 0; i < lods.size(); ++i)
            {
                const HairLodDescriptor& d = lods[i].descriptor;
                out << "    {\"name\":";
                WriteJsonString(out, d.name);
                out << ",\"strandFraction\": " << d.strandFraction
                    << ", \"particleFraction\": " << d.particleFraction
                    << ", \"switchDistance\": " << d.switchDistance
                    << ", \"widthScale\": " << d.widthScale
                    << ", \"strandCount\": " << lods[i].asset.strands.size()
                    << ", \"guideCount\": " << lods[i].asset.guideStrandCount
                    << ", \"followerCount\": " << lods[i].asset.followerStrandCount << "}";
                out << (i + 1u < lods.size() ? "," : "") << "\n";
            }
            out << "  ]";
        }

        out << "\n}\n";
        outError.clear();
        return true;
    }
}
