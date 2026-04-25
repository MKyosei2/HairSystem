#include "hairgpu/AssetIO.h"

#include "hairgpu/Binding.h"
#include "hairgpu/UsdExport.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <string_view>
#include <unordered_map>

namespace hairgpu
{
    namespace
    {
#ifdef HAIRGPU_WITH_ALEMBIC
        namespace Abc = Alembic::Abc;
        namespace AbcG = Alembic::AbcGeom;

        bool BuildAssetFromAlembicCurves(const std::vector<Vec3>& positions,
                                         const std::vector<int32_t>& curveCounts,
                                         const std::vector<float>& widths,
                                         HairAsset& outAsset,
                                         std::string& outError)
        {
            if (positions.empty() || curveCounts.empty())
            {
                outError = "Alembic curves payload is empty.";
                return false;
            }
            std::vector<ImportedStrand> strands;
            strands.reserve(curveCounts.size());
            size_t cursor = 0u;
            for (size_t curveIndex = 0; curveIndex < curveCounts.size(); ++curveIndex)
            {
                const int32_t count = curveCounts[curveIndex];
                if (count < 2 || cursor + static_cast<size_t>(count) > positions.size())
                {
                    outError = "Alembic curve topology is inconsistent.";
                    return false;
                }
                ImportedStrand strand = {};
                strand.originalStrandId = static_cast<uint32_t>(curveIndex);
                strand.isGuide = true;
                strand.parentGuideOriginal = static_cast<int32_t>(curveIndex);
                strand.groupId = static_cast<uint32_t>(curveIndex);
                strand.points.reserve(static_cast<size_t>(count));
                for (int32_t pointIndex = 0; pointIndex < count; ++pointIndex)
                {
                    ImportedPoint point = {};
                    point.pointIndex = static_cast<uint32_t>(pointIndex);
                    point.position = positions[cursor];
                    if (!widths.empty())
                    {
                        point.width = (widths.size() == positions.size()) ? widths[cursor] : widths[std::min<size_t>(curveIndex, widths.size() - 1u)];
                    }
                    else
                    {
                        point.width = 0.004f;
                    }
                    strand.points.push_back(point);
                    ++cursor;
                }
                strands.push_back(std::move(strand));
            }

            HairAsset asset = {};
            asset.guideStrandCount = static_cast<uint32_t>(strands.size());
            asset.followerStrandCount = 0u;
            uint32_t maxPointCount = 0u;
            for (const ImportedStrand& strand : strands)
            {
                maxPointCount = std::max<uint32_t>(maxPointCount, static_cast<uint32_t>(strand.points.size()));
            }
            asset.particlesPerStrand = std::max<uint32_t>(4u, std::min<uint32_t>(64u, maxPointCount));
            std::vector<Vec3> roots;
            roots.reserve(strands.size());
            for (size_t strandIndex = 0; strandIndex < strands.size(); ++strandIndex)
            {
                const ImportedStrand& imported = strands[strandIndex];
                StrandRange range = {};
                range.pointOffset = static_cast<uint32_t>(asset.restPoints.size());
                range.pointCount = asset.particlesPerStrand;
                range.groupId = imported.groupId;
                range.flags = static_cast<uint32_t>(StrandFlags::Guide);
                asset.strands.push_back(range);
                asset.materials.push_back({});
                asset.rootBindings.push_back({});
                asset.followData.push_back({ static_cast<int32_t>(strandIndex), 1.0f, 0.0f, 0.0f });
                for (uint32_t pointIndex = 0; pointIndex < asset.particlesPerStrand; ++pointIndex)
                {
                    const float t = (asset.particlesPerStrand > 1u) ? static_cast<float>(pointIndex) / static_cast<float>(asset.particlesPerStrand - 1u) : 0.0f;
                    asset.restPoints.push_back(SamplePolylinePosition(imported.points, t));
                    asset.pointWidths.push_back(std::max(0.0005f, SamplePolylineWidth(imported.points, t)));
                }
                roots.push_back(asset.restPoints[range.pointOffset]);
            }
            BuildSyntheticScalpForRoots(asset, roots);
            outAsset = std::move(asset);
            outError.clear();
            return true;
        }

        bool LoadHairAssetFromAlembicSdk(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
        {
            Alembic::AbcCoreFactory::IFactory factory;
            Alembic::AbcCoreFactory::IFactory::CoreType coreType;
            AbcG::IArchive archive = factory.getArchive(filePath.string(), coreType);
            if (!archive.valid())
            {
                outError = "Alembic SDK failed to open archive: " + filePath.string();
                return false;
            }

            std::vector<Vec3> curvePositions;
            std::vector<int32_t> curveCounts;
            std::vector<float> curveWidths;
            ScalpMesh scalpMesh = {};

            std::function<void(const Abc::IObject&)> visitObject;
            visitObject = [&](const Abc::IObject& object)
            {
                if (AbcG::ICurves::matches(object.getHeader()) && curveCounts.empty())
                {
                    AbcG::ICurves curves(object, Abc::kWrapExisting);
                    AbcG::ICurvesSchema::Sample sample;
                    curves.getSchema().get(sample);
                    auto positionsPtr = sample.getPositions();
                    auto countsPtr = sample.getCurvesNumVertices();
                    if (positionsPtr && countsPtr)
                    {
                        curvePositions.reserve(positionsPtr->size());
                        for (size_t i = 0; i < positionsPtr->size(); ++i)
                        {
                            const auto& p = (*positionsPtr)[i];
                            curvePositions.push_back({ p.x, p.y, p.z });
                        }
                        curveCounts.assign(countsPtr->get(), countsPtr->get() + countsPtr->size());
                        if (curves.getSchema().getWidthsParam().valid())
                        {
                            AbcG::IFloatGeomParam::Sample widthSample;
                            curves.getSchema().getWidthsParam().getExpanded(widthSample);
                            auto widthPtr = widthSample.getVals();
                            if (widthPtr)
                            {
                                curveWidths.assign(widthPtr->get(), widthPtr->get() + widthPtr->size());
                            }
                        }
                    }
                }
                else if (AbcG::IPolyMesh::matches(object.getHeader()) && scalpMesh.positions.empty())
                {
                    AbcG::IPolyMesh mesh(object, Abc::kWrapExisting);
                    AbcG::IPolyMeshSchema::Sample sample;
                    mesh.getSchema().get(sample);
                    auto positionsPtr = sample.getPositions();
                    auto countsPtr = sample.getFaceCounts();
                    auto indicesPtr = sample.getFaceIndices();
                    if (positionsPtr)
                    {
                        scalpMesh.positions.reserve(positionsPtr->size());
                        scalpMesh.normals.resize(positionsPtr->size(), {0.0f, 1.0f, 0.0f});
                        scalpMesh.uvs.resize(positionsPtr->size(), {0.5f, 0.5f});
                        scalpMesh.skinWeights.resize(positionsPtr->size(), { {0u,0u,0u,0u}, {1.0f,0.0f,0.0f,0.0f} });
                        scalpMesh.boneCount = 1u;
                        for (size_t i = 0; i < positionsPtr->size(); ++i)
                        {
                            const auto& p = (*positionsPtr)[i];
                            scalpMesh.positions.push_back({ p.x, p.y, p.z });
                        }
                    }
                    if (countsPtr && indicesPtr)
                    {
                        size_t cursor = 0u;
                        for (size_t faceIndex = 0; faceIndex < countsPtr->size(); ++faceIndex)
                        {
                            const int32_t faceCount = (*countsPtr)[faceIndex];
                            if (faceCount < 3)
                            {
                                cursor += static_cast<size_t>(std::max(faceCount, 0));
                                continue;
                            }
                            const uint32_t first = static_cast<uint32_t>((*indicesPtr)[cursor]);
                            for (int32_t tri = 1; tri + 1 < faceCount; ++tri)
                            {
                                scalpMesh.triangles.push_back({ first,
                                                               static_cast<uint32_t>((*indicesPtr)[cursor + static_cast<size_t>(tri)]),
                                                               static_cast<uint32_t>((*indicesPtr)[cursor + static_cast<size_t>(tri + 1)]) });
                            }
                            cursor += static_cast<size_t>(faceCount);
                        }
                    }
                    Abc::ICompoundProperty meshUserProps = mesh.getSchema().getUserProperties();
                    if (meshUserProps.valid())
                    {
                        std::vector<float> normalValues;
                        std::vector<float> uvValues;
                        std::vector<uint32_t> boneIndices;
                        std::vector<float> boneWeights;
                        uint32_t boneCount = 0u;
                        readFloatArray(meshUserProps, "normals", normalValues);
                        readFloatArray(meshUserProps, "uvs", uvValues);
                        readUIntArray(meshUserProps, "skinBoneIndices", boneIndices);
                        readFloatArray(meshUserProps, "skinBoneWeights", boneWeights);
                        readUIntScalar(meshUserProps, "boneCount", boneCount);
                        if (normalValues.size() == scalpMesh.positions.size() * 3u)
                        {
                            scalpMesh.normals.resize(scalpMesh.positions.size());
                            for (size_t i = 0u; i < scalpMesh.positions.size(); ++i)
                            {
                                scalpMesh.normals[i] = { normalValues[i * 3u + 0u], normalValues[i * 3u + 1u], normalValues[i * 3u + 2u] };
                            }
                        }
                        if (uvValues.size() == scalpMesh.positions.size() * 2u)
                        {
                            scalpMesh.uvs.resize(scalpMesh.positions.size());
                            for (size_t i = 0u; i < scalpMesh.positions.size(); ++i)
                            {
                                scalpMesh.uvs[i] = { uvValues[i * 2u + 0u], uvValues[i * 2u + 1u] };
                            }
                        }
                        if (boneIndices.size() == scalpMesh.positions.size() * 4u && boneWeights.size() == scalpMesh.positions.size() * 4u)
                        {
                            scalpMesh.skinWeights.resize(scalpMesh.positions.size());
                            for (size_t i = 0u; i < scalpMesh.positions.size(); ++i)
                            {
                                for (size_t k = 0u; k < 4u; ++k)
                                {
                                    scalpMesh.skinWeights[i].boneIndices[k] = boneIndices[i * 4u + k];
                                    scalpMesh.skinWeights[i].boneWeights[k] = boneWeights[i * 4u + k];
                                }
                            }
                        }
                        if (boneCount > 0u)
                        {
                            scalpMesh.boneCount = boneCount;
                        }
                    }
                }
                for (size_t childIndex = 0u; childIndex < object.getNumChildren(); ++childIndex)
                {
                    visitObject(object.getChild(childIndex));
                }
            };
            visitObject(archive.getTop());

            if (!BuildAssetFromAlembicCurves(curvePositions, curveCounts, curveWidths, outAsset, outError))
            {
                return false;
            }
            if (!scalpMesh.positions.empty() && !scalpMesh.triangles.empty())
            {
                outAsset.scalpMesh = std::move(scalpMesh);
                RepairHairAssetSkinning(outAsset, false, nullptr);
            }
            return true;
        }

        bool SaveHairAssetToAlembicSdk(const std::filesystem::path& filePath,
                                       const HairAsset& asset,
                                       const HairAlembicSettings& settings,
                                       std::string& outError)
        {
            AbcG::OArchive archive(Alembic::AbcCoreOgawa::WriteArchive(), filePath.string());
            Abc::OObject top = archive.getTop();
            AbcG::OCurves curves(top, "hair");
            std::vector<AbcG::V3f> positions;
            std::vector<Abc::int32_t> counts;
            std::vector<float> widths;
            for (const StrandRange& strand : asset.strands)
            {
                counts.push_back(static_cast<Abc::int32_t>(strand.pointCount));
                for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
                {
                    const Vec3 p = asset.restPoints[strand.pointOffset + pointIndex];
                    positions.emplace_back(p.x, p.y, p.z);
                    widths.push_back(settings.includeWidths && strand.pointOffset + pointIndex < asset.pointWidths.size() ? asset.pointWidths[strand.pointOffset + pointIndex] : 0.004f);
                }
            }
            AbcG::OCurvesSchema::Sample sample(Abc::V3fArraySample(positions), Abc::Int32ArraySample(counts), AbcG::kNoBasis, AbcG::kNonPeriodic, AbcG::kLinear);
            curves.getSchema().set(sample);
            if (settings.includeWidths)
            {
                AbcG::OFloatGeomParam::Sample widthSample(Abc::FloatArraySample(widths), AbcG::kVertexScope);
                curves.getSchema().getWidthsParam().set(widthSample);
            }
            if (settings.includeScalp && !asset.scalpMesh.positions.empty() && !asset.scalpMesh.triangles.empty())
            {
                AbcG::OPolyMesh scalp(top, "scalp");
                std::vector<AbcG::V3f> scalpPositions;
                scalpPositions.reserve(asset.scalpMesh.positions.size());
                for (const Vec3& p : asset.scalpMesh.positions)
                {
                    scalpPositions.emplace_back(p.x, p.y, p.z);
                }
                std::vector<Abc::int32_t> faceCounts(asset.scalpMesh.triangles.size(), 3);
                std::vector<Abc::int32_t> faceIndices;
                faceIndices.reserve(asset.scalpMesh.triangles.size() * 3u);
                for (const TriangleIndices& tri : asset.scalpMesh.triangles)
                {
                    faceIndices.push_back(static_cast<Abc::int32_t>(tri.a));
                    faceIndices.push_back(static_cast<Abc::int32_t>(tri.b));
                    faceIndices.push_back(static_cast<Abc::int32_t>(tri.c));
                }
                AbcG::OPolyMeshSchema::Sample scalpSample(Abc::V3fArraySample(scalpPositions),
                                                          Abc::Int32ArraySample(faceIndices),
                                                          Abc::Int32ArraySample(faceCounts));
                scalp.getSchema().set(scalpSample);

                std::vector<float> normalValues;
                std::vector<float> uvValues;
                std::vector<Abc::uint32_t> boneIndices;
                std::vector<float> boneWeights;
                normalValues.reserve(asset.scalpMesh.normals.size() * 3u);
                uvValues.reserve(asset.scalpMesh.uvs.size() * 2u);
                boneIndices.reserve(asset.scalpMesh.skinWeights.size() * 4u);
                boneWeights.reserve(asset.scalpMesh.skinWeights.size() * 4u);
                for (const Vec3& n : asset.scalpMesh.normals)
                {
                    normalValues.push_back(n.x);
                    normalValues.push_back(n.y);
                    normalValues.push_back(n.z);
                }
                for (const Vec2& uv : asset.scalpMesh.uvs)
                {
                    uvValues.push_back(uv.x);
                    uvValues.push_back(uv.y);
                }
                for (const SkinWeight4& sw : asset.scalpMesh.skinWeights)
                {
                    for (size_t k = 0u; k < 4u; ++k)
                    {
                        boneIndices.push_back(static_cast<Abc::uint32_t>(sw.boneIndices[k]));
                        boneWeights.push_back(sw.boneWeights[k]);
                    }
                }
                Abc::OCompoundProperty scalpUserProps = scalp.getSchema().getUserProperties();
                if (!normalValues.empty())
                {
                    Abc::OFloatArrayProperty(scalpUserProps, "normals").set(Abc::FloatArraySample(normalValues));
                }
                if (!uvValues.empty())
                {
                    Abc::OFloatArrayProperty(scalpUserProps, "uvs").set(Abc::FloatArraySample(uvValues));
                }
                if (!boneIndices.empty())
                {
                    Abc::OUInt32ArrayProperty(scalpUserProps, "skinBoneIndices").set(Abc::UInt32ArraySample(boneIndices));
                }
                if (!boneWeights.empty())
                {
                    Abc::OFloatArrayProperty(scalpUserProps, "skinBoneWeights").set(Abc::FloatArraySample(boneWeights));
                }
                Abc::OUInt32Property(scalpUserProps, "boneCount").set(static_cast<Abc::uint32_t>(asset.scalpMesh.boneCount));
            }
            outError.clear();
            return true;
        }
#endif
        struct ImportedPoint
        {
            uint32_t pointIndex = 0;
            Vec3 position = {};
            float width = 0.0f;
        };

        struct ImportedStrand
        {
            uint32_t originalStrandId = 0;
            std::vector<ImportedPoint> points;
            bool isGuide = true;
            int32_t parentGuideOriginal = -1;
            uint32_t groupId = 0;
            float followWeight = 1.0f;
            float clumpWeight = 0.0f;
            float noiseWeight = 0.0f;
        };

        std::string Trim(std::string value)
        {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        std::string ToLower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }

        std::vector<std::string> SplitCsvLine(const std::string& line)
        {
            std::vector<std::string> values;
            std::string current;
            bool inQuotes = false;
            for (size_t i = 0; i < line.size(); ++i)
            {
                const char c = line[i];
                if (c == '"')
                {
                    if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
                    {
                        current.push_back('"');
                        ++i;
                    }
                    else
                    {
                        inQuotes = !inQuotes;
                    }
                }
                else if (c == ',' && !inQuotes)
                {
                    values.push_back(Trim(current));
                    current.clear();
                }
                else
                {
                    current.push_back(c);
                }
            }
            values.push_back(Trim(current));
            return values;
        }

        bool TryGetField(const std::unordered_map<std::string, size_t>& columns, const std::vector<std::string>& values, std::initializer_list<const char*> names, std::string& outValue)
        {
            for (const char* name : names)
            {
                auto it = columns.find(ToLower(name));
                if (it != columns.end() && it->second < values.size())
                {
                    outValue = values[it->second];
                    return true;
                }
            }
            return false;
        }

        float ParseFloatOr(const std::string& text, float fallback)
        {
            try
            {
                return std::stof(text);
            }
            catch (...)
            {
                return fallback;
            }
        }

        int32_t ParseIntOr(const std::string& text, int32_t fallback)
        {
            try
            {
                return static_cast<int32_t>(std::stoi(text));
            }
            catch (...)
            {
                return fallback;
            }
        }

        uint32_t ParseUIntOr(const std::string& text, uint32_t fallback)
        {
            try
            {
                return static_cast<uint32_t>(std::stoul(text));
            }
            catch (...)
            {
                return fallback;
            }
        }

        bool ParseBoolText(const std::string& text, bool fallback)
        {
            const std::string lowered = ToLower(text);
            if (lowered == "1" || lowered == "true" || lowered == "yes")
            {
                return true;
            }
            if (lowered == "0" || lowered == "false" || lowered == "no")
            {
                return false;
            }
            return fallback;
        }

        template <typename T, typename Getter>
        T SamplePolylineValue(const std::vector<ImportedPoint>& points,
                              float t,
                              const T& fallback,
                              Getter getter)
        {
            if (points.empty())
            {
                return fallback;
            }
            if (points.size() == 1)
            {
                return getter(points.front());
            }

            const float scaled = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(points.size() - 1);
            const size_t i0 = static_cast<size_t>(std::floor(scaled));
            const size_t i1 = std::min(i0 + 1, points.size() - 1);
            const float localT = scaled - static_cast<float>(i0);
            return Lerp(getter(points[i0]), getter(points[i1]), localT);
        }

        Vec3 SamplePolylinePosition(const std::vector<ImportedPoint>& points, float t)
        {
            return SamplePolylineValue<Vec3>(
                points,
                t,
                Vec3{},
                [](const ImportedPoint& point) { return point.position; });
        }

        float SamplePolylineWidth(const std::vector<ImportedPoint>& points, float t)
        {
            return SamplePolylineValue<float>(
                points,
                t,
                0.004f,
                [](const ImportedPoint& point) { return point.width; });
        }

        void BuildSyntheticScalpForRoots(HairAsset& asset, const std::vector<Vec3>& roots)
        {
            asset.scalpMesh.positions.clear();
            asset.scalpMesh.normals.clear();
            asset.scalpMesh.uvs.clear();
            asset.scalpMesh.triangles.clear();
            asset.scalpMesh.skinWeights.clear();
            asset.scalpMesh.boneCount = 1;

            asset.scalpMesh.positions.reserve(roots.size() * 3);
            asset.scalpMesh.normals.reserve(roots.size() * 3);
            asset.scalpMesh.uvs.reserve(roots.size() * 3);
            asset.scalpMesh.triangles.reserve(roots.size());
            asset.scalpMesh.skinWeights.reserve(roots.size() * 3);

            for (size_t strandIndex = 0; strandIndex < roots.size(); ++strandIndex)
            {
                const Vec3& root = roots[strandIndex];
                const float eps = 0.0025f;
                const uint32_t baseIndex = static_cast<uint32_t>(asset.scalpMesh.positions.size());
                asset.scalpMesh.positions.push_back(root);
                asset.scalpMesh.positions.push_back(root + Vec3(eps, 0.0f, 0.0f));
                asset.scalpMesh.positions.push_back(root + Vec3(0.0f, 0.0f, eps));
                asset.scalpMesh.normals.push_back({ 0.0f, 1.0f, 0.0f });
                asset.scalpMesh.normals.push_back({ 0.0f, 1.0f, 0.0f });
                asset.scalpMesh.normals.push_back({ 0.0f, 1.0f, 0.0f });
                const Vec2 uv = { 0.5f + root.x * 0.5f, 0.5f + root.z * 0.5f };
                asset.scalpMesh.uvs.push_back(uv);
                asset.scalpMesh.uvs.push_back(uv);
                asset.scalpMesh.uvs.push_back(uv);
                asset.scalpMesh.triangles.push_back({ baseIndex, baseIndex + 1u, baseIndex + 2u });
                asset.scalpMesh.skinWeights.push_back({ {0u, 0u, 0u, 0u}, {1.0f, 0.0f, 0.0f, 0.0f} });
                asset.scalpMesh.skinWeights.push_back({ {0u, 0u, 0u, 0u}, {1.0f, 0.0f, 0.0f, 0.0f} });
                asset.scalpMesh.skinWeights.push_back({ {0u, 0u, 0u, 0u}, {1.0f, 0.0f, 0.0f, 0.0f} });
            }
        }

        size_t FindMatchingDelimiter(const std::string& text, size_t openPos, char openChar, char closeChar)
        {
            if (openPos >= text.size() || text[openPos] != openChar)
            {
                return std::string::npos;
            }
            int depth = 0;
            for (size_t i = openPos; i < text.size(); ++i)
            {
                if (text[i] == openChar)
                {
                    ++depth;
                }
                else if (text[i] == closeChar)
                {
                    --depth;
                    if (depth == 0)
                    {
                        return i;
                    }
                }
            }
            return std::string::npos;
        }

        bool ExtractUsdDefBlock(const std::string& text, const char* defType, const char* defName, std::string& outBlock)
        {
            const std::string signature = std::string("def ") + defType + " \"" + defName + "\"";
            const size_t defPos = text.find(signature);
            if (defPos == std::string::npos)
            {
                return false;
            }
            const size_t openBrace = text.find('{', defPos + signature.size());
            if (openBrace == std::string::npos)
            {
                return false;
            }
            const size_t closeBrace = FindMatchingDelimiter(text, openBrace, '{', '}');
            if (closeBrace == std::string::npos)
            {
                return false;
            }
            outBlock = text.substr(openBrace + 1u, closeBrace - openBrace - 1u);
            return true;
        }

        bool ExtractUsdArrayText(const std::string& block, const char* key, std::string& outArrayText)
        {
            const size_t keyPos = block.find(key);
            if (keyPos == std::string::npos)
            {
                return false;
            }
            const size_t equalsPos = block.find('=', keyPos + std::char_traits<char>::length(key));
            if (equalsPos == std::string::npos)
            {
                return false;
            }
            const size_t openBracket = block.find('[', equalsPos + 1u);
            if (openBracket == std::string::npos)
            {
                return false;
            }
            const size_t closeBracket = FindMatchingDelimiter(block, openBracket, '[', ']');
            if (closeBracket == std::string::npos)
            {
                return false;
            }
            outArrayText = block.substr(openBracket + 1u, closeBracket - openBracket - 1u);
            return true;
        }

        std::vector<float> ParseFloatArrayText(const std::string& text)
        {
            std::vector<float> values;
            const char* ptr = text.c_str();
            while (*ptr != '\0')
            {
                while (*ptr != '\0' && !(std::isdigit(static_cast<unsigned char>(*ptr)) || *ptr == '-' || *ptr == '+' || *ptr == '.'))
                {
                    ++ptr;
                }
                if (*ptr == '\0')
                {
                    break;
                }
                char* endPtr = nullptr;
                errno = 0;
                const float value = std::strtof(ptr, &endPtr);
                if (endPtr == ptr)
                {
                    ++ptr;
                    continue;
                }
                if (errno == 0)
                {
                    values.push_back(value);
                }
                ptr = endPtr;
            }
            return values;
        }

        std::vector<int32_t> ParseIntArrayText(const std::string& text)
        {
            std::vector<int32_t> values;
            const char* ptr = text.c_str();
            while (*ptr != '\0')
            {
                while (*ptr != '\0' && !(std::isdigit(static_cast<unsigned char>(*ptr)) || *ptr == '-' || *ptr == '+'))
                {
                    ++ptr;
                }
                if (*ptr == '\0')
                {
                    break;
                }
                char* endPtr = nullptr;
                errno = 0;
                const long value = std::strtol(ptr, &endPtr, 10);
                if (endPtr == ptr)
                {
                    ++ptr;
                    continue;
                }
                if (errno == 0)
                {
                    values.push_back(static_cast<int32_t>(value));
                }
                ptr = endPtr;
            }
            return values;
        }

        ScalpMesh ParseUsdaScalpMesh(const std::string& text)
        {
            ScalpMesh scalp;
            std::string block;
            if (!ExtractUsdDefBlock(text, "Mesh", "Scalp", block))
            {
                return scalp;
            }

            std::string pointsText;
            if (ExtractUsdArrayText(block, "point3f[] points", pointsText))
            {
                const std::vector<float> values = ParseFloatArrayText(pointsText);
                scalp.positions.reserve(values.size() / 3u);
                for (size_t i = 0; i + 2u < values.size(); i += 3u)
                {
                    scalp.positions.push_back({ values[i], values[i + 1u], values[i + 2u] });
                }
            }

            std::string normalsText;
            if (ExtractUsdArrayText(block, "normal3f[] normals", normalsText))
            {
                const std::vector<float> values = ParseFloatArrayText(normalsText);
                scalp.normals.reserve(values.size() / 3u);
                for (size_t i = 0; i + 2u < values.size(); i += 3u)
                {
                    scalp.normals.push_back({ values[i], values[i + 1u], values[i + 2u] });
                }
            }

            std::string uvsText;
            if (ExtractUsdArrayText(block, "texCoord2f[] primvars:st", uvsText))
            {
                const std::vector<float> values = ParseFloatArrayText(uvsText);
                scalp.uvs.reserve(values.size() / 2u);
                for (size_t i = 0; i + 1u < values.size(); i += 2u)
                {
                    scalp.uvs.push_back({ values[i], values[i + 1u] });
                }
            }

            std::string faceCountsText;
            std::string faceIndicesText;
            if (ExtractUsdArrayText(block, "int[] faceVertexCounts", faceCountsText) && ExtractUsdArrayText(block, "int[] faceVertexIndices", faceIndicesText))
            {
                const std::vector<int32_t> faceCounts = ParseIntArrayText(faceCountsText);
                const std::vector<int32_t> faceIndices = ParseIntArrayText(faceIndicesText);
                size_t cursor = 0u;
                for (int32_t count : faceCounts)
                {
                    if (count < 3 || cursor + static_cast<size_t>(count) > faceIndices.size())
                    {
                        break;
                    }
                    const uint32_t root = static_cast<uint32_t>(std::max(faceIndices[cursor], 0));
                    for (int32_t local = 1; local + 1 < count; ++local)
                    {
                        scalp.triangles.push_back({
                            root,
                            static_cast<uint32_t>(std::max(faceIndices[cursor + static_cast<size_t>(local)], 0)),
                            static_cast<uint32_t>(std::max(faceIndices[cursor + static_cast<size_t>(local + 1)], 0))
                        });
                    }
                    cursor += static_cast<size_t>(count);
                }
            }

            scalp.skinWeights.assign(scalp.positions.size(), SkinWeight4{});
            scalp.boneCount = scalp.positions.empty() ? 0u : 1u;
            return scalp;
        }
    }

    bool LoadHairAssetFromCurvesCsv(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        std::ifstream in(filePath);
        if (!in)
        {
            outError = "Failed to open curves CSV: " + filePath.string();
            return false;
        }

        std::string headerLine;
        if (!std::getline(in, headerLine))
        {
            outError = "Curves CSV is empty.";
            return false;
        }

        const std::vector<std::string> headers = SplitCsvLine(headerLine);
        std::unordered_map<std::string, size_t> columns;
        for (size_t i = 0; i < headers.size(); ++i)
        {
            columns[ToLower(headers[i])] = i;
        }

        if (columns.find("x") == columns.end() || columns.find("y") == columns.end() || columns.find("z") == columns.end())
        {
            outError = "Curves CSV must contain x,y,z columns.";
            return false;
        }

        std::unordered_map<uint32_t, ImportedStrand> strandMap;
        std::string line;
        while (std::getline(in, line))
        {
            if (Trim(line).empty())
            {
                continue;
            }

            const std::vector<std::string> values = SplitCsvLine(line);
            std::string strandText;
            uint32_t strandId = 0;
            if (TryGetField(columns, values, { "strandindex", "strand_id", "strand", "strandid" }, strandText))
            {
                strandId = ParseUIntOr(strandText, 0u);
            }
            else
            {
                outError = "Curves CSV must contain strand index column (strandIndex / strand_id).";
                return false;
            }

            ImportedStrand& strand = strandMap[strandId];
            strand.originalStrandId = strandId;

            std::string pointText;
            const uint32_t pointId = TryGetField(columns, values, { "pointindex", "point_id", "point", "pointid" }, pointText)
                ? ParseUIntOr(pointText, static_cast<uint32_t>(strand.points.size()))
                : static_cast<uint32_t>(strand.points.size());

            ImportedPoint point = {};
            point.pointIndex = pointId;
            point.position.x = ParseFloatOr(values[columns["x"]], 0.0f);
            point.position.y = ParseFloatOr(values[columns["y"]], 0.0f);
            point.position.z = ParseFloatOr(values[columns["z"]], 0.0f);

            std::string widthText;
            const bool hasWidth = TryGetField(columns, values, { "width", "radius" }, widthText);
            point.width = hasWidth ? ParseFloatOr(widthText, 0.004f) : 0.004f;

            std::string groupText;
            if (TryGetField(columns, values, { "groupid", "group_id", "group" }, groupText))
            {
                strand.groupId = ParseUIntOr(groupText, strand.groupId);
            }

            std::string guideText;
            if (TryGetField(columns, values, { "isguide", "guide", "is_guide" }, guideText))
            {
                strand.isGuide = ParseBoolText(guideText, strand.isGuide);
            }

            std::string parentText;
            if (TryGetField(columns, values, { "parentguide", "parent_guide", "parentguideindex" }, parentText))
            {
                strand.parentGuideOriginal = ParseIntOr(parentText, strand.parentGuideOriginal);
            }

            std::string followWeightText;
            if (TryGetField(columns, values, { "followweight", "follow_weight" }, followWeightText))
            {
                strand.followWeight = ParseFloatOr(followWeightText, strand.followWeight);
            }

            std::string clumpWeightText;
            if (TryGetField(columns, values, { "clumpweight", "clump_weight" }, clumpWeightText))
            {
                strand.clumpWeight = ParseFloatOr(clumpWeightText, strand.clumpWeight);
            }

            std::string noiseWeightText;
            if (TryGetField(columns, values, { "noiseweight", "noise_weight" }, noiseWeightText))
            {
                strand.noiseWeight = ParseFloatOr(noiseWeightText, strand.noiseWeight);
            }

            strand.points.push_back(point);
        }

        if (strandMap.empty())
        {
            outError = "Curves CSV did not contain any strand points.";
            return false;
        }

        std::vector<ImportedStrand> inputStrands;
        inputStrands.reserve(strandMap.size());
        for (auto& pair : strandMap)
        {
            ImportedStrand strand = std::move(pair.second);
            std::sort(strand.points.begin(), strand.points.end(), [](const ImportedPoint& a, const ImportedPoint& b) { return a.pointIndex < b.pointIndex; });
            if (strand.points.size() < 2)
            {
                continue;
            }
            if (strand.parentGuideOriginal < 0)
            {
                strand.parentGuideOriginal = strand.isGuide ? static_cast<int32_t>(strand.originalStrandId) : -1;
            }
            if (strand.groupId == 0u)
            {
                strand.groupId = strand.isGuide ? strand.originalStrandId : static_cast<uint32_t>(std::max(strand.parentGuideOriginal, 0));
            }
            inputStrands.push_back(std::move(strand));
        }

        if (inputStrands.empty())
        {
            outError = "Curves CSV must contain at least one strand with 2 or more points.";
            return false;
        }

        std::sort(inputStrands.begin(), inputStrands.end(), [](const ImportedStrand& a, const ImportedStrand& b) {
            if (a.isGuide != b.isGuide)
            {
                return a.isGuide && !b.isGuide;
            }
            return a.originalStrandId < b.originalStrandId;
        });

        uint32_t guideCount = 0;
        uint32_t maxPointCount = 0;
        for (const ImportedStrand& strand : inputStrands)
        {
            guideCount += strand.isGuide ? 1u : 0u;
            maxPointCount = std::max<uint32_t>(maxPointCount, static_cast<uint32_t>(strand.points.size()));
        }
        if (guideCount == 0)
        {
            guideCount = static_cast<uint32_t>(inputStrands.size());
            for (ImportedStrand& strand : inputStrands)
            {
                strand.isGuide = true;
                strand.parentGuideOriginal = static_cast<int32_t>(strand.originalStrandId);
            }
        }

        const uint32_t particlesPerStrand = std::max<uint32_t>(4u, std::min<uint32_t>(maxPointCount, 64u));
        HairAsset asset;
        asset.guideStrandCount = guideCount;
        asset.followerStrandCount = static_cast<uint32_t>(inputStrands.size()) - guideCount;
        asset.particlesPerStrand = particlesPerStrand;
        asset.strands.reserve(inputStrands.size());
        asset.materials.reserve(inputStrands.size());
        asset.rootBindings.reserve(inputStrands.size());
        asset.followData.reserve(inputStrands.size());
        asset.restPoints.reserve(static_cast<size_t>(inputStrands.size()) * particlesPerStrand);
        asset.pointWidths.reserve(static_cast<size_t>(inputStrands.size()) * particlesPerStrand);

        std::unordered_map<uint32_t, uint32_t> originalGuideIdToNewIndex;
        std::vector<Vec3> strandRoots;
        strandRoots.reserve(inputStrands.size());

        for (size_t strandIndex = 0; strandIndex < inputStrands.size(); ++strandIndex)
        {
            const ImportedStrand& imported = inputStrands[strandIndex];
            if (imported.isGuide)
            {
                originalGuideIdToNewIndex[imported.originalStrandId] = static_cast<uint32_t>(strandIndex);
            }
        }

        for (size_t strandIndex = 0; strandIndex < inputStrands.size(); ++strandIndex)
        {
            const ImportedStrand& imported = inputStrands[strandIndex];
            StrandRange strandRange = {};
            strandRange.pointOffset = static_cast<uint32_t>(asset.restPoints.size());
            strandRange.pointCount = particlesPerStrand;
            strandRange.groupId = imported.groupId;
            strandRange.flags = imported.isGuide ? static_cast<uint32_t>(StrandFlags::Guide) : static_cast<uint32_t>(StrandFlags::None);
            asset.strands.push_back(strandRange);

            float rootWidth = 0.0f;
            float tipWidth = 0.0f;
            for (uint32_t pointIndex = 0; pointIndex < particlesPerStrand; ++pointIndex)
            {
                const float t = (particlesPerStrand > 1u) ? (static_cast<float>(pointIndex) / static_cast<float>(particlesPerStrand - 1u)) : 0.0f;
                const Vec3 p = SamplePolylinePosition(imported.points, t);
                const float width = std::max(SamplePolylineWidth(imported.points, t), 0.0005f);
                asset.restPoints.push_back(p);
                asset.pointWidths.push_back(width);
                if (pointIndex == 0u)
                {
                    rootWidth = width;
                    strandRoots.push_back(p);
                }
                if (pointIndex + 1u == particlesPerStrand)
                {
                    tipWidth = width;
                }
            }

            StrandMaterial material = {};
            material.rootWidth = std::max(rootWidth, 0.0008f);
            material.tipWidth = std::max(std::min(tipWidth, material.rootWidth), 0.0003f);
            material.stiffness = imported.isGuide ? 0.82f : 0.70f;
            material.damping = 0.03f;
            asset.materials.push_back(material);

            RootBinding binding = {};
            binding.triangleIndex = static_cast<uint32_t>(strandIndex);
            binding.barycentric = { 1.0f, 0.0f, 0.0f };
            binding.rootUv = { 0.5f, 0.5f };
            asset.rootBindings.push_back(binding);

            FollowData follow = {};
            if (imported.isGuide)
            {
                follow.parentGuide = static_cast<int32_t>(strandIndex);
                follow.followWeight = 1.0f;
            }
            else
            {
                const auto guideIt = originalGuideIdToNewIndex.find(static_cast<uint32_t>(std::max(imported.parentGuideOriginal, 0)));
                follow.parentGuide = (guideIt != originalGuideIdToNewIndex.end()) ? static_cast<int32_t>(guideIt->second) : 0;
                follow.followWeight = imported.followWeight;
            }
            follow.clumpWeight = imported.clumpWeight;
            follow.noiseWeight = imported.noiseWeight;
            asset.followData.push_back(follow);
        }

        BuildSyntheticScalpForRoots(asset, strandRoots);
        outAsset = std::move(asset);
        outError.clear();
        return true;
    }

    bool LoadHairAssetFromUsda(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        std::ifstream in(filePath);
        if (!in)
        {
            outError = "Failed to open USDA file: " + filePath.string();
            return false;
        }

        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.empty())
        {
            outError = "USDA file is empty.";
            return false;
        }

        std::string curvesBlock;
        if (!ExtractUsdDefBlock(text, "BasisCurves", "HairStrands", curvesBlock))
        {
            outError = "USDA file does not contain def BasisCurves \"HairStrands\".";
            return false;
        }

        std::string countsText;
        std::string pointsText;
        if (!ExtractUsdArrayText(curvesBlock, "int[] curveVertexCounts", countsText) || !ExtractUsdArrayText(curvesBlock, "point3f[] points", pointsText))
        {
            outError = "USDA HairStrands block is missing curveVertexCounts or points arrays.";
            return false;
        }

        const std::vector<int32_t> curveCounts = ParseIntArrayText(countsText);
        const std::vector<float> pointValues = ParseFloatArrayText(pointsText);
        if (curveCounts.empty() || (pointValues.size() % 3u) != 0u)
        {
            outError = "USDA HairStrands arrays are malformed.";
            return false;
        }

        std::vector<Vec3> points;
        points.reserve(pointValues.size() / 3u);
        for (size_t i = 0; i + 2u < pointValues.size(); i += 3u)
        {
            points.push_back({ pointValues[i], pointValues[i + 1u], pointValues[i + 2u] });
        }

        std::vector<float> widths;
        std::string widthsText;
        if (ExtractUsdArrayText(curvesBlock, "float[] widths", widthsText))
        {
            widths = ParseFloatArrayText(widthsText);
        }

        std::vector<int32_t> guideFlags;
        std::string guidesText;
        if (ExtractUsdArrayText(curvesBlock, "int[] primvars:guide", guidesText))
        {
            guideFlags = ParseIntArrayText(guidesText);
        }

        std::vector<int32_t> groupIds;
        std::string groupsText;
        if (ExtractUsdArrayText(curvesBlock, "int[] primvars:groupId", groupsText))
        {
            groupIds = ParseIntArrayText(groupsText);
        }

        size_t expectedPointCount = 0u;
        for (int32_t count : curveCounts)
        {
            if (count < 2)
            {
                outError = "USDA contains a curve with fewer than 2 points.";
                return false;
            }
            expectedPointCount += static_cast<size_t>(count);
        }
        if (expectedPointCount != points.size())
        {
            outError = "USDA point count does not match curveVertexCounts.";
            return false;
        }

        std::vector<ImportedStrand> inputStrands;
        inputStrands.reserve(curveCounts.size());
        size_t pointCursor = 0u;
        for (size_t curveIndex = 0; curveIndex < curveCounts.size(); ++curveIndex)
        {
            ImportedStrand strand = {};
            strand.originalStrandId = static_cast<uint32_t>(curveIndex);
            strand.isGuide = guideFlags.empty() ? true : (curveIndex < guideFlags.size() ? (guideFlags[curveIndex] != 0) : true);
            strand.groupId = (curveIndex < groupIds.size()) ? static_cast<uint32_t>(std::max(groupIds[curveIndex], 0)) : static_cast<uint32_t>(curveIndex);
            strand.parentGuideOriginal = strand.isGuide ? static_cast<int32_t>(strand.originalStrandId) : -1;
            const size_t count = static_cast<size_t>(curveCounts[curveIndex]);
            strand.points.reserve(count);
            for (size_t localIndex = 0; localIndex < count; ++localIndex)
            {
                ImportedPoint point = {};
                point.pointIndex = static_cast<uint32_t>(localIndex);
                point.position = points[pointCursor];
                if (widths.size() == points.size())
                {
                    point.width = std::max(widths[pointCursor], 0.0005f);
                }
                else if (curveIndex < widths.size())
                {
                    point.width = std::max(widths[curveIndex], 0.0005f);
                }
                else
                {
                    point.width = 0.004f;
                }
                strand.points.push_back(point);
                ++pointCursor;
            }
            inputStrands.push_back(std::move(strand));
        }

        std::unordered_map<uint32_t, uint32_t> groupToGuideOriginal;
        for (const ImportedStrand& strand : inputStrands)
        {
            if (strand.isGuide && groupToGuideOriginal.find(strand.groupId) == groupToGuideOriginal.end())
            {
                groupToGuideOriginal[strand.groupId] = strand.originalStrandId;
            }
        }
        for (ImportedStrand& strand : inputStrands)
        {
            if (strand.isGuide)
            {
                strand.parentGuideOriginal = static_cast<int32_t>(strand.originalStrandId);
                continue;
            }
            auto guideIt = groupToGuideOriginal.find(strand.groupId);
            if (guideIt != groupToGuideOriginal.end())
            {
                strand.parentGuideOriginal = static_cast<int32_t>(guideIt->second);
                continue;
            }
            float bestDistanceSq = std::numeric_limits<float>::max();
            int32_t bestGuide = -1;
            const Vec3 root = strand.points.empty() ? Vec3{} : strand.points.front().position;
            for (const ImportedStrand& candidate : inputStrands)
            {
                if (!candidate.isGuide || candidate.points.empty())
                {
                    continue;
                }
                const Vec3 delta = candidate.points.front().position - root;
                const float distanceSq = Dot(delta, delta);
                if (distanceSq < bestDistanceSq)
                {
                    bestDistanceSq = distanceSq;
                    bestGuide = static_cast<int32_t>(candidate.originalStrandId);
                }
            }
            strand.parentGuideOriginal = (bestGuide >= 0) ? bestGuide : 0;
        }

        std::sort(inputStrands.begin(), inputStrands.end(), [](const ImportedStrand& a, const ImportedStrand& b) {
            if (a.isGuide != b.isGuide)
            {
                return a.isGuide && !b.isGuide;
            }
            if (a.groupId != b.groupId)
            {
                return a.groupId < b.groupId;
            }
            return a.originalStrandId < b.originalStrandId;
        });

        uint32_t guideCount = 0u;
        uint32_t maxPointCount = 0u;
        for (const ImportedStrand& strand : inputStrands)
        {
            guideCount += strand.isGuide ? 1u : 0u;
            maxPointCount = std::max<uint32_t>(maxPointCount, static_cast<uint32_t>(strand.points.size()));
        }
        if (guideCount == 0u)
        {
            guideCount = static_cast<uint32_t>(inputStrands.size());
            for (ImportedStrand& strand : inputStrands)
            {
                strand.isGuide = true;
                strand.parentGuideOriginal = static_cast<int32_t>(strand.originalStrandId);
            }
        }

        const uint32_t particlesPerStrand = std::max<uint32_t>(4u, std::min<uint32_t>(maxPointCount, 64u));
        HairAsset asset;
        asset.guideStrandCount = guideCount;
        asset.followerStrandCount = static_cast<uint32_t>(inputStrands.size()) - guideCount;
        asset.particlesPerStrand = particlesPerStrand;
        asset.strands.reserve(inputStrands.size());
        asset.materials.reserve(inputStrands.size());
        asset.rootBindings.reserve(inputStrands.size());
        asset.followData.reserve(inputStrands.size());
        asset.restPoints.reserve(static_cast<size_t>(inputStrands.size()) * particlesPerStrand);
        asset.pointWidths.reserve(static_cast<size_t>(inputStrands.size()) * particlesPerStrand);

        std::unordered_map<uint32_t, uint32_t> originalGuideIdToNewIndex;
        std::vector<Vec3> strandRoots;
        strandRoots.reserve(inputStrands.size());
        for (size_t strandIndex = 0; strandIndex < inputStrands.size(); ++strandIndex)
        {
            if (inputStrands[strandIndex].isGuide)
            {
                originalGuideIdToNewIndex[inputStrands[strandIndex].originalStrandId] = static_cast<uint32_t>(strandIndex);
            }
        }

        for (size_t strandIndex = 0; strandIndex < inputStrands.size(); ++strandIndex)
        {
            const ImportedStrand& imported = inputStrands[strandIndex];
            StrandRange strandRange = {};
            strandRange.pointOffset = static_cast<uint32_t>(asset.restPoints.size());
            strandRange.pointCount = particlesPerStrand;
            strandRange.groupId = imported.groupId;
            strandRange.flags = imported.isGuide ? static_cast<uint32_t>(StrandFlags::Guide) : static_cast<uint32_t>(StrandFlags::None);
            asset.strands.push_back(strandRange);

            float rootWidth = 0.0f;
            float tipWidth = 0.0f;
            for (uint32_t pointIndex = 0; pointIndex < particlesPerStrand; ++pointIndex)
            {
                const float t = (particlesPerStrand > 1u) ? (static_cast<float>(pointIndex) / static_cast<float>(particlesPerStrand - 1u)) : 0.0f;
                const Vec3 p = SamplePolylinePosition(imported.points, t);
                const float width = std::max(SamplePolylineWidth(imported.points, t), 0.0005f);
                asset.restPoints.push_back(p);
                asset.pointWidths.push_back(width);
                if (pointIndex == 0u)
                {
                    rootWidth = width;
                    strandRoots.push_back(p);
                }
                if (pointIndex + 1u == particlesPerStrand)
                {
                    tipWidth = width;
                }
            }

            StrandMaterial material = {};
            material.rootWidth = std::max(rootWidth, 0.0008f);
            material.tipWidth = std::max(std::min(tipWidth, material.rootWidth), 0.0003f);
            material.stiffness = imported.isGuide ? 0.82f : 0.70f;
            material.damping = 0.03f;
            asset.materials.push_back(material);
            asset.rootBindings.push_back({});

            FollowData follow = {};
            if (imported.isGuide)
            {
                follow.parentGuide = static_cast<int32_t>(strandIndex);
                follow.followWeight = 1.0f;
            }
            else
            {
                const auto guideIt = originalGuideIdToNewIndex.find(static_cast<uint32_t>(std::max(imported.parentGuideOriginal, 0)));
                follow.parentGuide = (guideIt != originalGuideIdToNewIndex.end()) ? static_cast<int32_t>(guideIt->second) : 0;
                follow.followWeight = imported.followWeight;
            }
            follow.clumpWeight = imported.clumpWeight;
            follow.noiseWeight = imported.noiseWeight;
            asset.followData.push_back(follow);
        }

        const ScalpMesh scalp = ParseUsdaScalpMesh(text);
        if (!scalp.positions.empty() && !scalp.triangles.empty())
        {
            asset.scalpMesh = scalp;
            RootBindingQualityReport report = {};
            if (!GenerateRootBindingsFromScalp(asset, false, &report))
            {
                BuildSyntheticScalpForRoots(asset, strandRoots);
            }
        }
        else
        {
            BuildSyntheticScalpForRoots(asset, strandRoots);
        }

        outAsset = std::move(asset);
        outError.clear();
        return true;
    }

    const char* GetAlembicBackendDescription()
    {
#ifdef HAIRGPU_WITH_ALEMBIC
        return "alembic_sdk";
#else
        return "compatibility_usda_sidecar";
#endif
    }

    bool LoadHairAssetFromAlembic(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        std::ifstream sniff(filePath, std::ios::binary);
        if (sniff)
        {
            std::string prefix(256, '\0');
            sniff.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
            prefix.resize(static_cast<size_t>(std::max<std::streamsize>(0, sniff.gcount())));
            const bool looksUsda = prefix.find("#usda") != std::string::npos ||
                                   prefix.find("def BasisCurves") != std::string::npos ||
                                   prefix.find("def Mesh") != std::string::npos;
            const bool looksHairBin = prefix.size() >= 8u && std::equal(prefix.begin(), prefix.begin() + 8, "HGPUHBIN");
            const bool looksCsv = prefix.find(',') != std::string::npos || prefix.find(';') != std::string::npos;
            if (looksUsda)
            {
                return LoadHairAssetFromUsda(filePath, outAsset, outError);
            }
            if (looksHairBin)
            {
                return LoadHairAssetFromHairBinary(filePath, outAsset, outError);
            }
            if (looksCsv)
            {
                return LoadHairAssetFromCurvesCsv(filePath, outAsset, outError);
            }
        }
#ifdef HAIRGPU_WITH_ALEMBIC
        return LoadHairAssetFromAlembicSdk(filePath, outAsset, outError);
#else
        (void)outAsset;
        outError = "This build does not have the Alembic SDK linked. The loader will still sniff compatibility .abc files that contain USDA / hairbin / curves-CSV payloads, but real binary Alembic requires HAIRGPU_WITH_ALEMBIC=ON and a valid Alembic SDK (set ALEMBIC_ROOT / ALEMBIC_DIR).";
        return false;
#endif
    }
}

namespace hairgpu
{
    namespace
    {
#ifdef HAIRGPU_WITH_ALEMBIC
        namespace Abc = Alembic::Abc;
        namespace AbcG = Alembic::AbcGeom;

        bool BuildAssetFromAlembicCurves(const std::vector<Vec3>& positions,
                                         const std::vector<int32_t>& curveCounts,
                                         const std::vector<float>& widths,
                                         HairAsset& outAsset,
                                         std::string& outError)
        {
            if (positions.empty() || curveCounts.empty())
            {
                outError = "Alembic curves payload is empty.";
                return false;
            }
            std::vector<ImportedStrand> strands;
            strands.reserve(curveCounts.size());
            size_t cursor = 0u;
            for (size_t curveIndex = 0; curveIndex < curveCounts.size(); ++curveIndex)
            {
                const int32_t count = curveCounts[curveIndex];
                if (count < 2 || cursor + static_cast<size_t>(count) > positions.size())
                {
                    outError = "Alembic curve topology is inconsistent.";
                    return false;
                }
                ImportedStrand strand = {};
                strand.originalStrandId = static_cast<uint32_t>(curveIndex);
                strand.isGuide = true;
                strand.parentGuideOriginal = static_cast<int32_t>(curveIndex);
                strand.groupId = static_cast<uint32_t>(curveIndex);
                strand.points.reserve(static_cast<size_t>(count));
                for (int32_t pointIndex = 0; pointIndex < count; ++pointIndex)
                {
                    ImportedPoint point = {};
                    point.pointIndex = static_cast<uint32_t>(pointIndex);
                    point.position = positions[cursor];
                    if (!widths.empty())
                    {
                        point.width = (widths.size() == positions.size()) ? widths[cursor] : widths[std::min<size_t>(curveIndex, widths.size() - 1u)];
                    }
                    else
                    {
                        point.width = 0.004f;
                    }
                    strand.points.push_back(point);
                    ++cursor;
                }
                strands.push_back(std::move(strand));
            }

            HairAsset asset = {};
            asset.guideStrandCount = static_cast<uint32_t>(strands.size());
            asset.followerStrandCount = 0u;
            uint32_t maxPointCount = 0u;
            for (const ImportedStrand& strand : strands)
            {
                maxPointCount = std::max<uint32_t>(maxPointCount, static_cast<uint32_t>(strand.points.size()));
            }
            asset.particlesPerStrand = std::max<uint32_t>(4u, std::min<uint32_t>(64u, maxPointCount));
            std::vector<Vec3> roots;
            roots.reserve(strands.size());
            for (size_t strandIndex = 0; strandIndex < strands.size(); ++strandIndex)
            {
                const ImportedStrand& imported = strands[strandIndex];
                StrandRange range = {};
                range.pointOffset = static_cast<uint32_t>(asset.restPoints.size());
                range.pointCount = asset.particlesPerStrand;
                range.groupId = imported.groupId;
                range.flags = static_cast<uint32_t>(StrandFlags::Guide);
                asset.strands.push_back(range);
                asset.materials.push_back({});
                asset.rootBindings.push_back({});
                asset.followData.push_back({ static_cast<int32_t>(strandIndex), 1.0f, 0.0f, 0.0f });
                for (uint32_t pointIndex = 0; pointIndex < asset.particlesPerStrand; ++pointIndex)
                {
                    const float t = (asset.particlesPerStrand > 1u) ? static_cast<float>(pointIndex) / static_cast<float>(asset.particlesPerStrand - 1u) : 0.0f;
                    asset.restPoints.push_back(SamplePolylinePosition(imported.points, t));
                    asset.pointWidths.push_back(std::max(0.0005f, SamplePolylineWidth(imported.points, t)));
                }
                roots.push_back(asset.restPoints[range.pointOffset]);
            }
            BuildSyntheticScalpForRoots(asset, roots);
            outAsset = std::move(asset);
            outError.clear();
            return true;
        }

        bool LoadHairAssetFromAlembicSdk(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
        {
            Alembic::AbcCoreFactory::IFactory factory;
            Alembic::AbcCoreFactory::IFactory::CoreType coreType;
            AbcG::IArchive archive = factory.getArchive(filePath.string(), coreType);
            if (!archive.valid())
            {
                outError = "Alembic SDK failed to open archive: " + filePath.string();
                return false;
            }

            std::vector<Vec3> curvePositions;
            std::vector<int32_t> curveCounts;
            std::vector<float> curveWidths;
            ScalpMesh scalpMesh = {};

            std::function<void(const Abc::IObject&)> visitObject;
            visitObject = [&](const Abc::IObject& object)
            {
                if (AbcG::ICurves::matches(object.getHeader()) && curveCounts.empty())
                {
                    AbcG::ICurves curves(object, Abc::kWrapExisting);
                    AbcG::ICurvesSchema::Sample sample;
                    curves.getSchema().get(sample);
                    auto positionsPtr = sample.getPositions();
                    auto countsPtr = sample.getCurvesNumVertices();
                    if (positionsPtr && countsPtr)
                    {
                        curvePositions.reserve(positionsPtr->size());
                        for (size_t i = 0; i < positionsPtr->size(); ++i)
                        {
                            const auto& p = (*positionsPtr)[i];
                            curvePositions.push_back({ p.x, p.y, p.z });
                        }
                        curveCounts.assign(countsPtr->get(), countsPtr->get() + countsPtr->size());
                        if (curves.getSchema().getWidthsParam().valid())
                        {
                            AbcG::IFloatGeomParam::Sample widthSample;
                            curves.getSchema().getWidthsParam().getExpanded(widthSample);
                            auto widthPtr = widthSample.getVals();
                            if (widthPtr)
                            {
                                curveWidths.assign(widthPtr->get(), widthPtr->get() + widthPtr->size());
                            }
                        }
                    }
                }
                else if (AbcG::IPolyMesh::matches(object.getHeader()) && scalpMesh.positions.empty())
                {
                    AbcG::IPolyMesh mesh(object, Abc::kWrapExisting);
                    AbcG::IPolyMeshSchema::Sample sample;
                    mesh.getSchema().get(sample);
                    auto positionsPtr = sample.getPositions();
                    auto countsPtr = sample.getFaceCounts();
                    auto indicesPtr = sample.getFaceIndices();
                    if (positionsPtr)
                    {
                        scalpMesh.positions.reserve(positionsPtr->size());
                        scalpMesh.normals.resize(positionsPtr->size(), {0.0f, 1.0f, 0.0f});
                        scalpMesh.uvs.resize(positionsPtr->size(), {0.5f, 0.5f});
                        scalpMesh.skinWeights.resize(positionsPtr->size(), { {0u,0u,0u,0u}, {1.0f,0.0f,0.0f,0.0f} });
                        scalpMesh.boneCount = 1u;
                        for (size_t i = 0; i < positionsPtr->size(); ++i)
                        {
                            const auto& p = (*positionsPtr)[i];
                            scalpMesh.positions.push_back({ p.x, p.y, p.z });
                        }
                    }
                    if (countsPtr && indicesPtr)
                    {
                        size_t cursor = 0u;
                        for (size_t faceIndex = 0; faceIndex < countsPtr->size(); ++faceIndex)
                        {
                            const int32_t faceCount = (*countsPtr)[faceIndex];
                            if (faceCount < 3)
                            {
                                cursor += static_cast<size_t>(std::max(faceCount, 0));
                                continue;
                            }
                            const uint32_t first = static_cast<uint32_t>((*indicesPtr)[cursor]);
                            for (int32_t tri = 1; tri + 1 < faceCount; ++tri)
                            {
                                scalpMesh.triangles.push_back({ first,
                                                               static_cast<uint32_t>((*indicesPtr)[cursor + static_cast<size_t>(tri)]),
                                                               static_cast<uint32_t>((*indicesPtr)[cursor + static_cast<size_t>(tri + 1)]) });
                            }
                            cursor += static_cast<size_t>(faceCount);
                        }
                    }
                }
                for (size_t childIndex = 0u; childIndex < object.getNumChildren(); ++childIndex)
                {
                    visitObject(object.getChild(childIndex));
                }
            };
            visitObject(archive.getTop());

            if (!BuildAssetFromAlembicCurves(curvePositions, curveCounts, curveWidths, outAsset, outError))
            {
                return false;
            }
            if (!scalpMesh.positions.empty() && !scalpMesh.triangles.empty())
            {
                outAsset.scalpMesh = std::move(scalpMesh);
                RepairHairAssetSkinning(outAsset, false, nullptr);
            }
            return true;
        }

        bool SaveHairAssetToAlembicSdk(const std::filesystem::path& filePath,
                                       const HairAsset& asset,
                                       const HairAlembicSettings& settings,
                                       std::string& outError)
        {
            AbcG::OArchive archive(Alembic::AbcCoreOgawa::WriteArchive(), filePath.string());
            Abc::OObject top = archive.getTop();
            AbcG::OCurves curves(top, "hair");
            std::vector<AbcG::V3f> positions;
            std::vector<Abc::int32_t> counts;
            std::vector<float> widths;
            for (const StrandRange& strand : asset.strands)
            {
                counts.push_back(static_cast<Abc::int32_t>(strand.pointCount));
                for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
                {
                    const Vec3 p = asset.restPoints[strand.pointOffset + pointIndex];
                    positions.emplace_back(p.x, p.y, p.z);
                    widths.push_back(settings.includeWidths && strand.pointOffset + pointIndex < asset.pointWidths.size() ? asset.pointWidths[strand.pointOffset + pointIndex] : 0.004f);
                }
            }
            AbcG::OCurvesSchema::Sample sample(Abc::V3fArraySample(positions), Abc::Int32ArraySample(counts), AbcG::kNoBasis, AbcG::kNonPeriodic, AbcG::kLinear);
            curves.getSchema().set(sample);
            if (settings.includeWidths)
            {
                AbcG::OFloatGeomParam::Sample widthSample(Abc::FloatArraySample(widths), AbcG::kVertexScope);
                curves.getSchema().getWidthsParam().set(widthSample);
            }
            if (settings.includeScalp && !asset.scalpMesh.positions.empty() && !asset.scalpMesh.triangles.empty())
            {
                AbcG::OPolyMesh scalp(top, "scalp");
                std::vector<AbcG::V3f> scalpPositions;
                scalpPositions.reserve(asset.scalpMesh.positions.size());
                for (const Vec3& p : asset.scalpMesh.positions)
                {
                    scalpPositions.emplace_back(p.x, p.y, p.z);
                }
                std::vector<Abc::int32_t> faceCounts(asset.scalpMesh.triangles.size(), 3);
                std::vector<Abc::int32_t> faceIndices;
                faceIndices.reserve(asset.scalpMesh.triangles.size() * 3u);
                for (const TriangleIndices& tri : asset.scalpMesh.triangles)
                {
                    faceIndices.push_back(static_cast<Abc::int32_t>(tri.a));
                    faceIndices.push_back(static_cast<Abc::int32_t>(tri.b));
                    faceIndices.push_back(static_cast<Abc::int32_t>(tri.c));
                }
                AbcG::OPolyMeshSchema::Sample scalpSample(Abc::V3fArraySample(scalpPositions),
                                                          Abc::Int32ArraySample(faceIndices),
                                                          Abc::Int32ArraySample(faceCounts));
                scalp.getSchema().set(scalpSample);
            }
            outError.clear();
            return true;
        }
#endif
        template <typename T>
        bool WriteRaw(std::ofstream& out, const T& value)
        {
            out.write(reinterpret_cast<const char*>(&value), sizeof(T));
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadRaw(std::ifstream& in, T& value)
        {
            in.read(reinterpret_cast<char*>(&value), sizeof(T));
            return static_cast<bool>(in);
        }

        template <typename T>
        bool WriteVector(std::ofstream& out, const std::vector<T>& values)
        {
            uint64_t count = static_cast<uint64_t>(values.size());
            if (!WriteRaw(out, count)) return false;
            if (!values.empty()) out.write(reinterpret_cast<const char*>(values.data()), sizeof(T) * values.size());
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadVector(std::ifstream& in, std::vector<T>& values)
        {
            uint64_t count = 0;
            if (!ReadRaw(in, count)) return false;
            values.resize(static_cast<size_t>(count));
            if (!values.empty()) in.read(reinterpret_cast<char*>(values.data()), sizeof(T) * values.size());
            return static_cast<bool>(in);
        }
    }

    bool SaveHairAssetToAlembic(const std::filesystem::path& filePath,
                                const HairAsset& asset,
                                const HairAlembicSettings& settings,
                                std::string& outError)
    {
#ifdef HAIRGPU_WITH_ALEMBIC
        return SaveHairAssetToAlembicSdk(filePath, asset, settings, outError);
#else
        std::error_code ec;
        if (filePath.has_parent_path())
        {
            std::filesystem::create_directories(filePath.parent_path(), ec);
        }

        HairUsdExportSettings usdSettings = {};
        usdSettings.includeScalp = settings.includeScalp;
        usdSettings.includeCards = false;
        usdSettings.includeWidths = settings.includeWidths;
        usdSettings.includeGuidePrimvar = settings.includeGuidePrimvar;
        usdSettings.includeGroupPrimvar = settings.includeGroupPrimvar;
        if (!SaveHairAssetToUsda(filePath, asset, usdSettings, outError))
        {
            return false;
        }

        const std::filesystem::path hairBinPath = filePath.string() + ".hairbin";
        std::string sidecarError;
        if (!SaveHairAssetToHairBinary(hairBinPath, asset, sidecarError))
        {
            outError = "ABC compatibility USDA was written, but hairbin sidecar export failed: " + sidecarError;
            return false;
        }

        std::ofstream manifest(filePath.string() + ".compat.json");
        if (manifest)
        {
            manifest << "{\n";
            manifest << "  \"format\": \"abc_compat_usda\",\n";
            manifest << "  \"primary\": \"" << filePath.filename().string() << "\",\n";
            manifest << "  \"sidecarHairBin\": \"" << hairBinPath.filename().string() << "\",\n";
            manifest << "  \"settings\": {\n";
            manifest << "    \"writeScalpMesh\": " << (settings.includeScalp ? "true" : "false") << ",\n";
            manifest << "    \"writeVelocities\": " << (settings.includeGuidePrimvar ? "true" : "false") << ",\n";
            manifest << "    \"writeGroups\": " << (settings.includeGroupPrimvar ? "true" : "false") << ",\n";
            manifest << "    \"writeWidths\": " << (settings.includeWidths ? "true" : "false") << "\n";
            manifest << "  },\n";
            manifest << "  \"note\": \"This build has no Alembic SDK. The .abc file contains USDA text and can still round-trip through the compatibility loader.\"\n";
            manifest << "}\n";
        }

        outError.clear();
        return true;
#endif
    }

    bool SaveHairAssetToAlembic(const std::filesystem::path& filePath, const HairAsset& asset, std::string& outError)
    {
        HairAlembicSettings defaultSettings = {};
        return SaveHairAssetToAlembic(filePath, asset, defaultSettings, outError);
    }

    bool RoundTripHairAssetThroughAlembic(const HairAsset& asset,
                                        const std::filesystem::path& filePath,
                                        const HairAlembicSettings& settings,
                                        AlembicRoundTripReport& outReport,
                                        std::string& outError)
    {
        outReport = {};
        outReport.usedAlembicSdk = false;
#ifdef HAIRGPU_WITH_ALEMBIC
        outReport.usedAlembicSdk = true;
#endif
        outReport.sourceStrandCount = static_cast<uint32_t>(asset.strands.size());
        outReport.sourcePointCount = static_cast<uint32_t>(asset.restPoints.size());
        if (!SaveHairAssetToAlembic(filePath, asset, settings, outError))
        {
            return false;
        }
        HairAsset reloaded = {};
        if (!LoadHairAssetFromAlembic(filePath, reloaded, outError))
        {
            return false;
        }
        outReport.roundTripStrandCount = static_cast<uint32_t>(reloaded.strands.size());
        outReport.roundTripPointCount = static_cast<uint32_t>(reloaded.restPoints.size());
        const size_t pointCount = std::min(asset.restPoints.size(), reloaded.restPoints.size());
        double pointSq = 0.0;
        float pointMax = 0.0f;
        for (size_t i = 0u; i < pointCount; ++i)
        {
            const float err = Length(asset.restPoints[i] - reloaded.restPoints[i]);
            pointSq += static_cast<double>(err) * static_cast<double>(err);
            pointMax = std::max(pointMax, err);
        }
        if (pointCount > 0u)
        {
            outReport.rmsRestPointError = static_cast<float>(std::sqrt(pointSq / static_cast<double>(pointCount)));
            outReport.maxRestPointError = pointMax;
        }
        const size_t widthCount = std::min(asset.pointWidths.size(), reloaded.pointWidths.size());
        double widthSq = 0.0;
        float widthMax = 0.0f;
        for (size_t i = 0u; i < widthCount; ++i)
        {
            const float err = std::fabs(asset.pointWidths[i] - reloaded.pointWidths[i]);
            widthSq += static_cast<double>(err) * static_cast<double>(err);
            widthMax = std::max(widthMax, err);
        }
        if (widthCount > 0u)
        {
            outReport.rmsWidthError = static_cast<float>(std::sqrt(widthSq / static_cast<double>(widthCount)));
            outReport.maxWidthError = widthMax;
        }
        outReport.scalpRoundTripOk = asset.scalpMesh.positions.size() == reloaded.scalpMesh.positions.size() &&
                                     asset.scalpMesh.triangles.size() == reloaded.scalpMesh.triangles.size() &&
                                     asset.scalpMesh.skinWeights.size() == reloaded.scalpMesh.skinWeights.size();
        outError.clear();
        return true;
    }

    bool LoadHairAssetFromHairFile(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        return LoadHairAssetFromHairBinary(filePath, outAsset, outError);
    }

    bool SaveHairAssetToHairBinary(const std::filesystem::path& filePath, const HairAsset& asset, std::string& outError)
    {
        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open output file: " + filePath.string();
            return false;
        }
        const char magic[8] = {'H','G','P','U','H','B','I','N'};
        out.write(magic, sizeof(magic));
        uint32_t version = 1;
        WriteRaw(out, version);
        WriteRaw(out, asset.guideStrandCount);
        WriteRaw(out, asset.followerStrandCount);
        WriteRaw(out, asset.particlesPerStrand);
        if (!WriteVector(out, asset.strands) || !WriteVector(out, asset.restPoints) || !WriteVector(out, asset.pointWidths) ||
            !WriteVector(out, asset.materials) || !WriteVector(out, asset.rootBindings) || !WriteVector(out, asset.followData) ||
            !WriteVector(out, asset.scalpMesh.positions) || !WriteVector(out, asset.scalpMesh.normals) || !WriteVector(out, asset.scalpMesh.uvs) ||
            !WriteVector(out, asset.scalpMesh.triangles) || !WriteVector(out, asset.scalpMesh.skinWeights) || !WriteRaw(out, asset.scalpMesh.boneCount))
        {
            outError = "Failed while writing binary hair asset.";
            return false;
        }
        return true;
    }

    bool LoadHairAssetFromHairBinary(const std::filesystem::path& filePath, HairAsset& outAsset, std::string& outError)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open binary hair asset: " + filePath.string();
            return false;
        }
        char magic[8] = {};
        in.read(magic, sizeof(magic));
        const std::string got(magic, magic + sizeof(magic));
        if (got != "HGPUHBIN")
        {
            outError = "Invalid hair binary header.";
            return false;
        }
        uint32_t version = 0;
        if (!ReadRaw(in, version) || version != 1)
        {
            outError = "Unsupported hair binary version.";
            return false;
        }
        if (!ReadRaw(in, outAsset.guideStrandCount) || !ReadRaw(in, outAsset.followerStrandCount) || !ReadRaw(in, outAsset.particlesPerStrand) ||
            !ReadVector(in, outAsset.strands) || !ReadVector(in, outAsset.restPoints) || !ReadVector(in, outAsset.pointWidths) ||
            !ReadVector(in, outAsset.materials) || !ReadVector(in, outAsset.rootBindings) || !ReadVector(in, outAsset.followData) ||
            !ReadVector(in, outAsset.scalpMesh.positions) || !ReadVector(in, outAsset.scalpMesh.normals) || !ReadVector(in, outAsset.scalpMesh.uvs) ||
            !ReadVector(in, outAsset.scalpMesh.triangles) || !ReadVector(in, outAsset.scalpMesh.skinWeights) || !ReadRaw(in, outAsset.scalpMesh.boneCount))
        {
            outError = "Failed while reading binary hair asset.";
            return false;
        }
        return true;
    }
}
