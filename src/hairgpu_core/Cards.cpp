#include "hairgpu/Cards.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <vector>

#include "hairgpu/Math.h"

#include "hairgpu/StrandSampling.h"

#include "hairgpu/Geometry.h"
namespace hairgpu
{
    namespace
    {
        Vec3 EvaluateRootNormal(const HairAsset& asset, uint32_t strandIndex)
        {
            if (strandIndex < asset.rootBindings.size())
            {
                const RootBinding& binding = asset.rootBindings[strandIndex];
                if (binding.triangleIndex < asset.scalpMesh.triangles.size())
                {
                    const TriangleIndices& tri = asset.scalpMesh.triangles[binding.triangleIndex];
                    const Vec3 na = (tri.a < asset.scalpMesh.normals.size()) ? asset.scalpMesh.normals[tri.a] : Vec3{ 0.0f, 1.0f, 0.0f };
                    const Vec3 nb = (tri.b < asset.scalpMesh.normals.size()) ? asset.scalpMesh.normals[tri.b] : Vec3{ 0.0f, 1.0f, 0.0f };
                    const Vec3 nc = (tri.c < asset.scalpMesh.normals.size()) ? asset.scalpMesh.normals[tri.c] : Vec3{ 0.0f, 1.0f, 0.0f };
                    return EvaluateBarycentricNormal(na, nb, nc, binding.barycentric);
                }
            }
            return Vec3(0.0f, 1.0f, 0.0f);
        }

        std::vector<std::vector<uint32_t>> BuildGuideClusters(const HairAsset& asset, uint32_t guideCount)
        {
            std::vector<std::vector<uint32_t>> clusters;
            clusters.resize(guideCount);
            for (uint32_t guideIndex = 0; guideIndex < guideCount; ++guideIndex)
            {
                clusters[guideIndex].push_back(guideIndex);
            }

            for (uint32_t strandIndex = guideCount; strandIndex < static_cast<uint32_t>(asset.strands.size()); ++strandIndex)
            {
                uint32_t parentGuide = 0u;
                if (strandIndex < asset.followData.size() && asset.followData[strandIndex].parentGuide >= 0)
                {
                    parentGuide = static_cast<uint32_t>(asset.followData[strandIndex].parentGuide);
                }
                if (parentGuide >= guideCount)
                {
                    parentGuide = guideCount > 0u ? (guideCount - 1u) : 0u;
                }
                if (!clusters.empty())
                {
                    clusters[parentGuide].push_back(strandIndex);
                }
            }
            return clusters;
        }
    }

    HairCardMesh BuildHairCardMesh(const HairAsset& asset, const HairCardBuilderSettings& settings)
    {
        HairCardMesh mesh;
        if (asset.strands.empty() || asset.restPoints.empty())
        {
            return mesh;
        }

        const uint32_t guideCount = std::max(1u, std::min(asset.guideStrandCount, static_cast<uint32_t>(asset.strands.size())));
        const uint32_t cardsPerGuide = std::max(1u, settings.cardsPerGuide);
        const uint32_t segments = std::max(1u, settings.segmentsPerCard);
        const auto clusters = BuildGuideClusters(asset, guideCount);

        for (uint32_t guideIndex = 0; guideIndex < guideCount; ++guideIndex)
        {
            const std::vector<uint32_t>& cluster = clusters[guideIndex];
            const uint32_t strandCount = static_cast<uint32_t>(cluster.size());
            if (strandCount == 0u)
            {
                continue;
            }

            for (uint32_t cardIndex = 0; cardIndex < cardsPerGuide; ++cardIndex)
            {
                const uint32_t begin = (cardIndex * strandCount) / cardsPerGuide;
                const uint32_t end = ((cardIndex + 1u) * strandCount) / cardsPerGuide;
                if (end <= begin)
                {
                    continue;
                }

                const uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
                std::vector<Vec3> centers(segments + 1u);
                std::vector<Vec3> tangents(segments + 1u);
                std::vector<Vec3> laterals(segments + 1u);
                std::vector<Vec3> normals(segments + 1u);
                std::vector<float> widths(segments + 1u, 0.0f);

                const Vec3 rootNormal = EvaluateRootNormal(asset, guideIndex);

                for (uint32_t sampleIndex = 0; sampleIndex <= segments; ++sampleIndex)
                {
                    const float t = static_cast<float>(sampleIndex) / static_cast<float>(segments);
                    Vec3 center = {};
                    float avgWidth = 0.0f;
                    uint32_t count = 0u;
                    for (uint32_t i = begin; i < end; ++i)
                    {
                        const StrandRange& strand = asset.strands[cluster[i]];
                        center += SampleStrandPoint(asset, strand, t);
                        avgWidth += SampleStrandWidth(asset, strand, t, 0.0035f);
                        ++count;
                    }
                    if (count == 0u)
                    {
                        continue;
                    }
                    centers[sampleIndex] = center / static_cast<float>(count);
                    avgWidth /= static_cast<float>(count);

                    const float tPrev = (sampleIndex > 0u) ? static_cast<float>(sampleIndex - 1u) / static_cast<float>(segments) : t;
                    const float tNext = (sampleIndex < segments) ? static_cast<float>(sampleIndex + 1u) / static_cast<float>(segments) : t;
                    Vec3 pPrev = {};
                    Vec3 pNext = {};
                    for (uint32_t i = begin; i < end; ++i)
                    {
                        const StrandRange& strand = asset.strands[cluster[i]];
                        pPrev += SampleStrandPoint(asset, strand, tPrev);
                        pNext += SampleStrandPoint(asset, strand, tNext);
                    }
                    pPrev = pPrev / static_cast<float>(count);
                    pNext = pNext / static_cast<float>(count);
                    Vec3 tangent = Normalize(pNext - pPrev);
                    if (Length(tangent) <= 1.0e-5f)
                    {
                        tangent = Vec3(0.0f, -1.0f, 0.0f);
                    }
                    tangents[sampleIndex] = tangent;

                    Vec3 normal = Normalize(rootNormal - tangent * Dot(rootNormal, tangent));
                    if (Length(normal) <= 1.0e-5f)
                    {
                        normal = (std::fabs(tangent.y) < 0.9f) ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
                    }
                    Vec3 lateral = Normalize(Cross(normal, tangent));
                    if (Length(lateral) <= 1.0e-5f)
                    {
                        lateral = Normalize(Cross(Vec3(0.0f, 0.0f, 1.0f), tangent));
                    }
                    normal = Normalize(Cross(tangent, lateral));
                    laterals[sampleIndex] = lateral;
                    normals[sampleIndex] = normal;

                    float radius = std::max(avgWidth * settings.widthScale, settings.widthBias);
                    for (uint32_t i = begin; i < end; ++i)
                    {
                        const StrandRange& strand = asset.strands[cluster[i]];
                        const Vec3 p = SampleStrandPoint(asset, strand, t);
                        const float lateralDistance = std::fabs(Dot(p - centers[sampleIndex], lateral));
                        radius = std::max(radius, lateralDistance * settings.widthScale + settings.widthBias);
                    }
                    widths[sampleIndex] = radius;
                }

                for (uint32_t sampleIndex = 0; sampleIndex <= segments; ++sampleIndex)
                {
                    const float v = static_cast<float>(sampleIndex) / static_cast<float>(segments);
                    const Vec3 leftPos = centers[sampleIndex] - laterals[sampleIndex] * widths[sampleIndex];
                    const Vec3 rightPos = centers[sampleIndex] + laterals[sampleIndex] * widths[sampleIndex];
                    mesh.vertices.push_back({ leftPos, normals[sampleIndex], {0.0f, v} });
                    mesh.vertices.push_back({ rightPos, normals[sampleIndex], {1.0f, v} });
                }

                for (uint32_t sampleIndex = 0; sampleIndex < segments; ++sampleIndex)
                {
                    const uint32_t i0 = baseVertex + sampleIndex * 2u;
                    const uint32_t i1 = i0 + 1u;
                    const uint32_t i2 = i0 + 2u;
                    const uint32_t i3 = i0 + 3u;
                    mesh.indices.push_back(i0);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i1);
                    mesh.indices.push_back(i2);
                    mesh.indices.push_back(i3);
                }

                ++mesh.cardCount;
            }
        }

        return mesh;
    }

    bool SaveHairCardMeshToObj(const std::filesystem::path& filePath, const HairCardMesh& mesh, std::string& outError)
    {
        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to write hair cards OBJ: " + filePath.string();
            return false;
        }

        out << "# hairgpu card mesh\n";
        for (const HairCardVertex& v : mesh.vertices)
        {
            out << "v " << v.position.x << ' ' << v.position.y << ' ' << v.position.z << "\n";
        }
        for (const HairCardVertex& v : mesh.vertices)
        {
            out << "vt " << v.uv.x << ' ' << v.uv.y << "\n";
        }
        for (const HairCardVertex& v : mesh.vertices)
        {
            out << "vn " << v.normal.x << ' ' << v.normal.y << ' ' << v.normal.z << "\n";
        }
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            const uint32_t a = mesh.indices[i] + 1u;
            const uint32_t b = mesh.indices[i + 1u] + 1u;
            const uint32_t c = mesh.indices[i + 2u] + 1u;
            out << "f " << a << '/' << a << '/' << a << ' '
                << b << '/' << b << '/' << b << ' '
                << c << '/' << c << '/' << c << "\n";
        }

        outError.clear();
        return true;
    }
}
