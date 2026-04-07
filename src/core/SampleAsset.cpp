#include "hairgpu/Types.h"
#include "hairgpu/Wig.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace hairgpu
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTau = 6.28318530717958647692f;

        float Fract(float value)
        {
            return value - std::floor(value);
        }

        float Hash01(uint32_t value)
        {
            return Fract(std::sin(static_cast<float>(value) * 12.9898f + 78.233f) * 43758.5453f);
        }

        float WrapUnit(float v)
        {
            v = std::fmod(v, 1.0f);
            return (v < 0.0f) ? (v + 1.0f) : v;
        }

        float Smooth01(float v)
        {
            v = Clamp(v, 0.0f, 1.0f);
            return v * v * (3.0f - 2.0f * v);
        }

        float SignedSideFromU(float u)
        {
            const float angle = WrapUnit(u) * kTau;
            return (std::cos(angle) >= 0.0f) ? 1.0f : -1.0f;
        }

        float EvaluateProceduralPartingMap(float u, float v, float partingOffset)
        {
            const float wrapped = WrapUnit(u + partingOffset);
            float d = std::fabs(wrapped - 0.47f);
            d = std::min(d, 1.0f - d);
            const float centerBand = Smooth01(1.0f - Clamp(d / 0.085f, 0.0f, 1.0f));
            const float crownBand = Smooth01(1.0f - Clamp(std::fabs(v - 0.18f) / 0.16f, 0.0f, 1.0f));
            const float upperBand = Smooth01(1.0f - Clamp((v - 0.42f) / 0.24f, 0.0f, 1.0f));
            return centerBand * crownBand * upperBand;
        }

        Vec3 EvaluateProceduralScalpRegionMap(float u, float v)
        {
            const float angle = WrapUnit(u) * kTau;
            const float side = std::cos(angle);
            const float frontBack = std::sin(angle);
            const float crown = Smooth01(1.0f - Clamp(std::fabs(v - 0.16f) / 0.13f, 0.0f, 1.0f));
            const float temple = Smooth01(Clamp((std::fabs(side) - 0.36f) / 0.42f, 0.0f, 1.0f)) * Smooth01(1.0f - Clamp(std::fabs(v - 0.34f) / 0.18f, 0.0f, 1.0f));
            const float nape = Smooth01(Clamp((v - 0.58f) / 0.20f, 0.0f, 1.0f)) * Smooth01(Clamp((-frontBack + 0.10f) / 0.90f, 0.0f, 1.0f));
            return Vec3(crown, temple, nape);
        }

        struct MaskImage
        {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t channels = 0;
            std::vector<float> samples;

            bool valid() const
            {
                return width > 0 && height > 0 && channels > 0 && samples.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
            }
        };

        std::string g_partingMaskOverridePath;
        std::string g_regionMaskOverridePath;
        bool g_sampleMasksDirty = true;
        SampleMaskSourceMode g_sampleMaskSourceMode = SampleMaskSourceMode::Auto;

        bool LoadCsvMaskImage(const std::filesystem::path& path, uint32_t expectedChannels, MaskImage& outImage);

        bool EndsWithInsensitive(const std::filesystem::path& path, const char* ext)
        {
            std::string a = path.extension().string();
            std::string b = ext;
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            return a == b;
        }

        bool ReadPortableAnyMapToken(std::istream& input, std::string& out)
        {
            out.clear();
            while (input)
            {
                int c = input.peek();
                if (c == '#')
                {
                    std::string skip;
                    std::getline(input, skip);
                    continue;
                }
                if (!std::isspace(c))
                {
                    break;
                }
                input.get();
            }
            if (!(input >> out))
            {
                return false;
            }
            return true;
        }

        bool LoadPortableAnyMapImage(const std::filesystem::path& path, uint32_t expectedChannels, MaskImage& outImage)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                return false;
            }
            std::string magic, token;
            if (!ReadPortableAnyMapToken(input, magic))
            {
                return false;
            }
            const bool gray = (magic == "P2" || magic == "P5");
            const bool rgb = (magic == "P3" || magic == "P6");
            const bool binary = (magic == "P5" || magic == "P6");
            const uint32_t channels = gray ? 1u : (rgb ? 3u : 0u);
            if (channels == 0 || channels != expectedChannels)
            {
                return false;
            }
            if (!ReadPortableAnyMapToken(input, token)) return false; const uint32_t width = static_cast<uint32_t>(std::stoul(token));
            if (!ReadPortableAnyMapToken(input, token)) return false; const uint32_t height = static_cast<uint32_t>(std::stoul(token));
            if (!ReadPortableAnyMapToken(input, token)) return false; const float maxv = std::max(1.0f, std::stof(token));
            outImage.width = width; outImage.height = height; outImage.channels = channels; outImage.samples.assign(static_cast<size_t>(width) * height * channels, 0.0f);
            if (binary)
            {
                input.get();
                const size_t count = outImage.samples.size();
                std::vector<unsigned char> bytes(count);
                input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
                if (static_cast<size_t>(input.gcount()) != count) return false;
                for (size_t i = 0; i < count; ++i) outImage.samples[i] = Clamp(static_cast<float>(bytes[i]) / maxv, 0.0f, 1.0f);
            }
            else
            {
                for (float& v : outImage.samples)
                {
                    if (!ReadPortableAnyMapToken(input, token)) return false;
                    v = Clamp(std::stof(token) / maxv, 0.0f, 1.0f);
                }
            }
            return outImage.valid();
        }

        bool LoadBmpMaskImage(const std::filesystem::path& path, uint32_t expectedChannels, MaskImage& outImage)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return false;
            unsigned char fileHeader[14] = {};
            unsigned char dibHeader[40] = {};
            input.read(reinterpret_cast<char*>(fileHeader), 14);
            input.read(reinterpret_cast<char*>(dibHeader), 40);
            if (input.gcount() != 40 || fileHeader[0] != 'B' || fileHeader[1] != 'M') return false;
            auto read32 = [](const unsigned char* p) -> uint32_t { return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24); };
            auto read16 = [](const unsigned char* p) -> uint16_t { return uint16_t(p[0]) | (uint16_t(p[1])<<8); };
            const uint32_t dataOffset = read32(fileHeader + 10);
            const int32_t width = static_cast<int32_t>(read32(dibHeader + 4));
            const int32_t heightSigned = static_cast<int32_t>(read32(dibHeader + 8));
            const uint16_t planes = read16(dibHeader + 12);
            const uint16_t bitCount = read16(dibHeader + 14);
            const uint32_t compression = read32(dibHeader + 16);
            if (planes != 1 || compression != 0 || (bitCount != 24 && bitCount != 32) || width <= 0 || heightSigned == 0) return false;
            const uint32_t widthU = static_cast<uint32_t>(width);
            const uint32_t heightU = static_cast<uint32_t>(std::abs(heightSigned));
            const bool topDown = heightSigned < 0;
            const uint32_t srcChannels = (bitCount == 32) ? 4u : 3u;
            if (!(expectedChannels == 1u || expectedChannels == 3u)) return false;
            const uint32_t rowStride = ((widthU * srcChannels + 3u) / 4u) * 4u;
            input.seekg(static_cast<std::streamoff>(dataOffset), std::ios::beg);
            std::vector<unsigned char> bytes(static_cast<size_t>(rowStride) * heightU);
            input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (static_cast<size_t>(input.gcount()) != bytes.size()) return false;
            outImage.width = widthU; outImage.height = heightU; outImage.channels = expectedChannels; outImage.samples.assign(static_cast<size_t>(widthU) * heightU * expectedChannels, 0.0f);
            for (uint32_t y = 0; y < heightU; ++y)
            {
                const uint32_t srcY = topDown ? y : (heightU - 1u - y);
                const unsigned char* row = bytes.data() + static_cast<size_t>(srcY) * rowStride;
                for (uint32_t x = 0; x < widthU; ++x)
                {
                    const unsigned char* px = row + static_cast<size_t>(x) * srcChannels;
                    const float b = px[0] / 255.0f, g = px[1] / 255.0f, r = px[2] / 255.0f;
                    if (expectedChannels == 1u)
                    {
                        outImage.samples[static_cast<size_t>(y) * widthU + x] = Clamp(0.2126f * r + 0.7152f * g + 0.0722f * b, 0.0f, 1.0f);
                    }
                    else
                    {
                        const size_t idx = (static_cast<size_t>(y) * widthU + x) * 3u;
                        outImage.samples[idx + 0] = r;
                        outImage.samples[idx + 1] = g;
                        outImage.samples[idx + 2] = b;
                    }
                }
            }
            return outImage.valid();
        }

        bool LoadMaskImageAny(const std::filesystem::path& path, uint32_t expectedChannels, MaskImage& outImage)
        {
            if (EndsWithInsensitive(path, ".csv")) return LoadCsvMaskImage(path, expectedChannels, outImage);
            if (EndsWithInsensitive(path, ".pgm") || EndsWithInsensitive(path, ".ppm")) return LoadPortableAnyMapImage(path, expectedChannels, outImage);
            if (EndsWithInsensitive(path, ".bmp")) return LoadBmpMaskImage(path, expectedChannels, outImage);
            return false;
        }

        std::filesystem::path ResolveSampleMaskPath(const char* filename)
        {
            namespace fs = std::filesystem;
            std::vector<fs::path> roots;
            roots.emplace_back(fs::current_path());
            roots.emplace_back(fs::current_path() / "docs" / "sample_assets");
            for (int depth = 0; depth < 6; ++depth)
            {
                fs::path base = fs::current_path();
                for (int i = 0; i < depth; ++i)
                {
                    base = base.parent_path();
                }
                roots.emplace_back(base / "docs" / "sample_assets");
                roots.emplace_back(base / "sample_assets");
            }
            for (const fs::path& root : roots)
            {
                fs::path candidate = root / filename;
                if (fs::exists(candidate))
                {
                    return candidate;
                }
            }
            return {};
        }

        bool LoadCsvMaskImage(const std::filesystem::path& path, uint32_t expectedChannels, MaskImage& outImage)
        {
            std::ifstream input(path);
            if (!input)
            {
                return false;
            }

            std::vector<float> values;
            std::string line;
            uint32_t rowWidth = 0;
            uint32_t rowCount = 0;
            while (std::getline(input, line))
            {
                if (line.empty())
                {
                    continue;
                }
                std::replace(line.begin(), line.end(), ';', ',');
                std::stringstream ss(line);
                std::string cell;
                std::vector<float> rowValues;
                while (std::getline(ss, cell, ','))
                {
                    if (cell.empty())
                    {
                        continue;
                    }
                    rowValues.push_back(Clamp(std::stof(cell), 0.0f, 1.0f));
                }
                if (rowValues.empty())
                {
                    continue;
                }
                if (expectedChannels == 0)
                {
                    return false;
                }
                if (rowValues.size() % expectedChannels != 0)
                {
                    return false;
                }
                const uint32_t width = static_cast<uint32_t>(rowValues.size() / expectedChannels);
                if (rowWidth == 0)
                {
                    rowWidth = width;
                }
                else if (rowWidth != width)
                {
                    return false;
                }
                values.insert(values.end(), rowValues.begin(), rowValues.end());
                ++rowCount;
            }

            if (rowWidth == 0 || rowCount == 0)
            {
                return false;
            }

            outImage.width = rowWidth;
            outImage.height = rowCount;
            outImage.channels = expectedChannels;
            outImage.samples = std::move(values);
            return outImage.valid();
        }

        float SampleMaskChannel(const MaskImage& image, float u, float v, uint32_t channel)
        {
            if (!image.valid() || channel >= image.channels)
            {
                return 0.0f;
            }
            const float su = Clamp(WrapUnit(u) * static_cast<float>(image.width - 1), 0.0f, static_cast<float>(image.width - 1));
            const float sv = Clamp(v * static_cast<float>(image.height - 1), 0.0f, static_cast<float>(image.height - 1));
            const uint32_t x0 = static_cast<uint32_t>(std::floor(su));
            const uint32_t y0 = static_cast<uint32_t>(std::floor(sv));
            const uint32_t x1 = std::min(x0 + 1u, image.width - 1u);
            const uint32_t y1 = std::min(y0 + 1u, image.height - 1u);
            const float tx = su - static_cast<float>(x0);
            const float ty = sv - static_cast<float>(y0);
            auto at = [&](uint32_t x, uint32_t y) -> float
            {
                return image.samples[(static_cast<size_t>(y) * image.width + x) * image.channels + channel];
            };
            const float a = Lerp(at(x0, y0), at(x1, y0), tx);
            const float b = Lerp(at(x0, y1), at(x1, y1), tx);
            return Lerp(a, b, ty);
        }

        struct SampleMasks
        {
            MaskImage parting;
            MaskImage regions;
            bool loaded = false;
        };

        const SampleMasks& GetSampleMasks()
        {
            static SampleMasks masks;
            if (g_sampleMasksDirty)
            {
                g_sampleMasksDirty = false;
                masks = {};

                if (g_sampleMaskSourceMode == SampleMaskSourceMode::Procedural)
                {
                    masks.loaded = false;
                    return masks;
                }

                std::filesystem::path partPath;
                std::filesystem::path regionPath;
                if (!g_partingMaskOverridePath.empty())
                {
                    partPath = std::filesystem::path(g_partingMaskOverridePath);
                }
                if (!g_regionMaskOverridePath.empty())
                {
                    regionPath = std::filesystem::path(g_regionMaskOverridePath);
                }

                if (g_sampleMaskSourceMode == SampleMaskSourceMode::Auto)
                {
                    if (partPath.empty())
                    {
                        for (const char* name : {"scalp_parting_mask.bmp", "scalp_parting_mask.pgm", "scalp_parting_mask.ppm", "scalp_parting_mask.csv"})
                        {
                            partPath = ResolveSampleMaskPath(name);
                            if (!partPath.empty()) break;
                        }
                    }
                    if (regionPath.empty())
                    {
                        for (const char* name : {"scalp_region_mask.bmp", "scalp_region_mask.ppm", "scalp_region_mask.csv"})
                        {
                            regionPath = ResolveSampleMaskPath(name);
                            if (!regionPath.empty()) break;
                        }
                    }
                }

                const bool partOk = !partPath.empty() && LoadMaskImageAny(partPath, 1u, masks.parting);
                const bool regionOk = !regionPath.empty() && LoadMaskImageAny(regionPath, 3u, masks.regions);
                masks.loaded = partOk && regionOk;
            }
            return masks;
        }

        float EvaluatePartingMap(float u, float v, float partingOffset)
        {
            const SampleMasks& masks = GetSampleMasks();
            if (masks.loaded && masks.parting.valid())
            {
                return SampleMaskChannel(masks.parting, u + partingOffset, v, 0u);
            }
            return EvaluateProceduralPartingMap(u, v, partingOffset);
        }

        Vec3 EvaluateScalpRegionMap(float u, float v)
        {
            const SampleMasks& masks = GetSampleMasks();
            if (masks.loaded && masks.regions.valid())
            {
                return Vec3(
                    SampleMaskChannel(masks.regions, u, v, 0u),
                    SampleMaskChannel(masks.regions, u, v, 1u),
                    SampleMaskChannel(masks.regions, u, v, 2u));
            }
            return EvaluateProceduralScalpRegionMap(u, v);
        }

        Vec3 SafeNormalizeXZ(const Vec3& v)
        {
            Vec3 flattened(v.x, 0.0f, v.z);
            const float length = Length(flattened);
            if (length <= 1.0e-6f)
            {
                return Vec3(0.0f, 0.0f, 1.0f);
            }
            return flattened / length;
        }

        Vec3 EvaluateTriangleNormal(const ScalpMesh& scalp, const RootBinding& binding)
        {
            if (binding.triangleIndex >= scalp.triangles.size())
            {
                return Vec3(0.0f, 1.0f, 0.0f);
            }

            const TriangleIndices& tri = scalp.triangles[binding.triangleIndex];
            const Vec3& a = scalp.normals[tri.a];
            const Vec3& b = scalp.normals[tri.b];
            const Vec3& c = scalp.normals[tri.c];
            return Normalize(a * binding.barycentric.x + b * binding.barycentric.y + c * binding.barycentric.z);
        }

        Vec2 EvaluateTriangleUv(const ScalpMesh& scalp, const RootBinding& binding)
        {
            if (binding.triangleIndex >= scalp.triangles.size())
            {
                return {};
            }

            const TriangleIndices& tri = scalp.triangles[binding.triangleIndex];
            const Vec2& a = scalp.uvs[tri.a];
            const Vec2& b = scalp.uvs[tri.b];
            const Vec2& c = scalp.uvs[tri.c];
            return {
                a.x * binding.barycentric.x + b.x * binding.barycentric.y + c.x * binding.barycentric.z,
                a.y * binding.barycentric.x + b.y * binding.barycentric.y + c.y * binding.barycentric.z,
            };
        }

        RootBinding FindClosestBindingForUv(const ScalpMesh& scalp, float targetU, float targetV)
        {
            RootBinding best;
            best.barycentric = { 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f };
            best.rootUv = { targetU, targetV };

            float bestScore = FLT_MAX;
            for (uint32_t triIndex = 0; triIndex < static_cast<uint32_t>(scalp.triangles.size()); ++triIndex)
            {
                const TriangleIndices& tri = scalp.triangles[triIndex];
                const Vec2& uva = scalp.uvs[tri.a];
                const Vec2& uvb = scalp.uvs[tri.b];
                const Vec2& uvc = scalp.uvs[tri.c];
                const float centroidU = (uva.x + uvb.x + uvc.x) / 3.0f;
                const float centroidV = (uva.y + uvb.y + uvc.y) / 3.0f;
                float du = std::fabs(centroidU - targetU);
                du = std::min(du, 1.0f - du);
                const float dv = centroidV - targetV;
                const float score = du * du + dv * dv * 1.6f;
                if (score < bestScore)
                {
                    bestScore = score;
                    best.triangleIndex = triIndex;
                    best.rootUv = { centroidU, centroidV };
                }
            }

            return best;
        }

        Vec3 CubicBezier(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t)
        {
            const float u = 1.0f - t;
            const float uu = u * u;
            const float tt = t * t;
            return p0 * (uu * u)
                + p1 * (3.0f * uu * t)
                + p2 * (3.0f * u * tt)
                + p3 * (tt * t);
        }


        float ComputeGuideLength(const Vec3& root, const Vec3& scalpCenter, const Vec2& uv, float baseLength, const WigParameters& parameters)
        {
            const Vec3 rel = root - scalpCenter;
            const float lowerRing = Clamp((1.0f - Clamp((root.y - (scalpCenter.y - 0.02f)) / 0.26f, 0.0f, 1.0f)), 0.0f, 1.0f);
            const float sideBoost = Clamp(std::fabs(rel.x) / 0.22f, 0.0f, 1.0f) * 0.03f;
            const float crownReduction = Clamp((uv.y - 0.38f) / 0.62f, 0.0f, 1.0f) * 0.04f;
            return baseLength * parameters.overallLength * (1.0f + parameters.lowerRingLengthBias * lowerRing + sideBoost - crownReduction);
        }

        void AppendStaticWigStrand(
            HairAsset& asset,
            const RootBinding& binding,
            const Vec3& scalpCenter,
            const StrandMaterial& material,
            const FollowData& follow,
            uint32_t pointCount,
            uint32_t groupId,
            uint32_t flags,
            uint32_t seed,
            float segmentLength,
            const WigParameters& parameters,
            const Vec3& rootOffset)
        {
            const Vec3 root = EvaluateRootBindingPosition(asset.scalpMesh, binding) + rootOffset;
            const Vec3 normal = EvaluateTriangleNormal(asset.scalpMesh, binding);
            const Vec2 uv = EvaluateTriangleUv(asset.scalpMesh, binding);
            const Vec3 rel = root - scalpCenter;
            const Vec3 radial = SafeNormalizeXZ(rel);

            const float partLineX = parameters.partingOffset;
            const float centeredX = root.x - (scalpCenter.x + partLineX);
            const float sideSign = (centeredX >= 0.0f) ? 1.0f : -1.0f;

            Vec3 lateral = Normalize(Vec3(sideSign, 0.0f, 0.0f) + radial * 0.22f);
            lateral = lateral - normal * Dot(lateral, normal);
            lateral = Normalize(lateral);
            if (Length(lateral) <= 1.0e-6f)
            {
                lateral = Vec3(sideSign, 0.0f, 0.0f);
            }

            Vec3 capFlow = Vec3(0.0f, -1.0f, 0.0f) - normal * Dot(Vec3(0.0f, -1.0f, 0.0f), normal) * 0.25f;
            if (Length(capFlow) <= 1.0e-6f)
            {
                capFlow = Vec3(0.0f, -1.0f, 0.0f);
            }
            capFlow = Normalize(capFlow * 0.92f + radial * 0.08f);

            const float partDistance = std::fabs(centeredX);
            const float partInfluence = 1.0f - Clamp(partDistance / std::max(parameters.partWidth, 1.0e-4f), 0.0f, 1.0f);
            const float horizontalRadius = Clamp(std::sqrt(rel.x * rel.x + rel.z * rel.z) / 0.23f, 0.0f, 1.0f);
            const float baseChainLength = std::max(segmentLength * static_cast<float>(std::max(pointCount - 1u, 1u)), segmentLength);
            const float length = ComputeGuideLength(root, scalpCenter, uv, baseChainLength, parameters);
            const float partPush = partInfluence * (parameters.partWidth * 0.45f + 0.004f);
            const float curtainWidth = parameters.silhouetteWidth * (0.18f + 0.22f * horizontalRadius);

            const Vec3 p0 = root;
            const Vec3 p1 = root
                + normal * (parameters.rootLift + horizontalRadius * 0.003f)
                + lateral * (partPush * 0.65f)
                + capFlow * (length * (0.16f + parameters.scalpConform * 0.08f));

            const Vec3 p2 = root
                + normal * (parameters.rootLift * 0.20f)
                + lateral * (curtainWidth * 0.55f + partPush * 0.60f)
                + capFlow * (length * 0.56f);

            Vec3 tipTarget = root
                + lateral * (curtainWidth * 0.35f + partPush * 0.35f)
                + radial * parameters.backVolume * 0.18f
                + capFlow * length;
            tipTarget.x -= sideSign * parameters.tipInward * (0.18f + 0.22f * horizontalRadius);
            const Vec3 p3 = tipTarget;

            StrandRange range;
            range.pointOffset = static_cast<uint32_t>(asset.restPoints.size());
            range.pointCount = pointCount;
            range.groupId = groupId;
            range.flags = flags;
            asset.strands.push_back(range);
            asset.materials.push_back(material);
            asset.rootBindings.push_back(binding);
            asset.followData.push_back(follow);

            const Vec3 orthoA = Normalize(Cross(normal, lateral));
            const Vec3 orthoB = Normalize(Cross(lateral, orthoA));

            for (uint32_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
            {
                const float t = static_cast<float>(pointIndex) / static_cast<float>(std::max(pointCount - 1u, 1u));
                Vec3 p = CubicBezier(p0, p1, p2, p3, t);

                if (pointIndex > 2)
                {
                    const float atten = parameters.strandNoise * (0.08f + 0.32f * t) * (1.0f - 0.65f * partInfluence);
                    const float noiseA = std::sin((t * 4.1f + Hash01(seed + pointIndex * 11u) * 0.8f) * kTau);
                    const float noiseB = std::cos((t * 2.7f + Hash01(seed + pointIndex * 13u) * 0.8f) * kTau);
                    p += orthoA * (noiseA * atten * 0.30f);
                    p += orthoB * (noiseB * atten * 0.18f);
                }

                asset.restPoints.push_back(p);
                asset.pointWidths.push_back(Lerp(material.rootWidth, material.tipWidth, t));
            }
        }
    }

    ScalpMesh BuildSampleScalpMesh(uint32_t rows, uint32_t columns, const Vec3& center, float radiusX, float radiusY, float radiusZ)
    {
        ScalpMesh mesh;
        rows = std::max(rows, 2u);
        columns = std::max(columns, 3u);

        mesh.positions.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.normals.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.uvs.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.triangles.reserve(static_cast<size_t>(rows) * columns * 2u);
        mesh.skinWeights.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.boneCount = 2;

        for (uint32_t row = 0; row <= rows; ++row)
        {
            const float v = static_cast<float>(row) / static_cast<float>(rows);
            const float phi = (0.10f + 0.44f * v) * kPi;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            for (uint32_t col = 0; col <= columns; ++col)
            {
                const float u = static_cast<float>(col) / static_cast<float>(columns);
                const float theta = u * kTau;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);

                Vec3 normal(cosTheta * sinPhi, cosPhi, sinTheta * sinPhi);
                Vec3 position(
                    center.x + normal.x * radiusX,
                    center.y + normal.y * radiusY,
                    center.z + normal.z * radiusZ);

                mesh.positions.push_back(position);
                mesh.normals.push_back(Normalize(Vec3(normal.x / radiusX, normal.y / radiusY, normal.z / radiusZ)));
                mesh.uvs.push_back({ u, v });
                SkinWeight4 weights;
                const float headWeight = Lerp(0.18f, 1.0f, std::pow(v, 0.7f));
                weights.boneIndices = { 0u, 1u, 0u, 0u };
                weights.boneWeights = { 1.0f - headWeight, headWeight, 0.0f, 0.0f };
                mesh.skinWeights.push_back(weights);
            }
        }

        const uint32_t stride = columns + 1;
        for (uint32_t row = 0; row < rows; ++row)
        {
            for (uint32_t col = 0; col < columns; ++col)
            {
                const uint32_t i0 = row * stride + col;
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = i0 + stride;
                const uint32_t i3 = i2 + 1;
                mesh.triangles.push_back({ i0, i2, i1 });
                mesh.triangles.push_back({ i1, i2, i3 });
            }
        }

        return mesh;
    }

    Vec3 EvaluateRootBindingPosition(const ScalpMesh& scalpMesh, const RootBinding& binding)
    {
        if (binding.triangleIndex >= scalpMesh.triangles.size())
        {
            return {};
        }

        const TriangleIndices& tri = scalpMesh.triangles[binding.triangleIndex];
        const Vec3& a = scalpMesh.positions[tri.a];
        const Vec3& b = scalpMesh.positions[tri.b];
        const Vec3& c = scalpMesh.positions[tri.c];
        return a * binding.barycentric.x + b * binding.barycentric.y + c * binding.barycentric.z;
    }

    void RebuildRootRestPositionsFromScalp(HairAsset& asset)
    {
        if (asset.scalpMesh.positions.empty() || asset.strands.empty())
        {
            return;
        }

        for (size_t strandIndex = 0; strandIndex < asset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0 || strand.pointOffset >= asset.restPoints.size())
            {
                continue;
            }

            const Vec3 reboundRoot = EvaluateRootBindingPosition(asset.scalpMesh, asset.rootBindings[strandIndex]);
            Vec3 originalRoot = asset.restPoints[strand.pointOffset];
            Vec3 delta = reboundRoot - originalRoot;
            for (uint32_t i = 0; i < strand.pointCount; ++i)
            {
                asset.restPoints[strand.pointOffset + i] += delta;
            }
        }
    }

    WigParameters DefaultWigParameters()
    {
        WigParameters parameters;
        parameters.overallLength = 1.02f;
        parameters.partingOffset = -0.008f;
        parameters.partWidth = 0.014f;
        parameters.scalpConform = 0.78f;
        parameters.rootLift = 0.010f;
        parameters.silhouetteWidth = 0.020f;
        parameters.sideVolume = 0.008f;
        parameters.backVolume = 0.014f;
        parameters.lowerRingLengthBias = 0.10f;
        parameters.strandNoise = 0.004f;
        parameters.followerSpread = 0.0030f;
        parameters.tipInward = 0.006f;
        return parameters;
    }

    HairAsset BuildStaticWigHairAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand, float segmentLength, const WigParameters& parameters)
    {
        HairAsset asset;
        asset.guideStrandCount = guideCount;
        asset.followerStrandCount = guideCount * followersPerGuide;
        asset.particlesPerStrand = particlesPerStrand;
        asset.scalpMesh = BuildSampleScalpMesh(24, 40, Vec3(0.0f, 1.60f, 0.0f), 0.23f, 0.19f, 0.18f);

        const uint32_t totalStrands = asset.guideStrandCount + asset.followerStrandCount;
        asset.strands.reserve(totalStrands);
        asset.materials.reserve(totalStrands);
        asset.rootBindings.reserve(totalStrands);
        asset.followData.reserve(totalStrands);
        asset.restPoints.reserve(static_cast<size_t>(totalStrands) * particlesPerStrand);
        asset.pointWidths.reserve(static_cast<size_t>(totalStrands) * particlesPerStrand);

        const Vec3 scalpCenter(0.0f, 1.60f, 0.0f);
        const float golden = 0.61803398875f;

        for (uint32_t guideIndex = 0; guideIndex < guideCount; ++guideIndex)
        {
            const float seq = (static_cast<float>(guideIndex) + 0.5f) / static_cast<float>(std::max(guideCount, 1u));
            const float asymSeedA = Hash01(guideIndex * 11u + 1u) - 0.5f;
            const float asymSeedB = Hash01(guideIndex * 13u + 7u) - 0.5f;
            const float asymSeedC = Hash01(guideIndex * 17u + 19u) - 0.5f;
            const float lane = std::fmod(static_cast<float>(guideIndex) * golden, 1.0f);
            const float sideBias = (guideIndex % 9u < 5u) ? -1.0f : 1.0f;
            const float crownBias = Smooth01(1.0f - Clamp(seq / 0.28f, 0.0f, 1.0f));
            float u = WrapUnit(0.57f + golden * static_cast<float>(guideIndex) + asymSeedA * 0.26f + sideBias * (0.10f + 0.03f * crownBias) + (lane - 0.5f) * 0.08f);
            float v = Clamp(0.07f + 0.74f * std::sqrt(seq) + asymSeedB * 0.18f + sideBias * 0.030f - std::fabs(asymSeedC) * 0.05f - crownBias * 0.035f, 0.05f, 0.92f);

            const float side = SignedSideFromU(u);
            const float partMask = EvaluatePartingMap(u, v, 0.03f + parameters.partingOffset * 0.5f);
            const Vec3 scalpRegions = EvaluateScalpRegionMap(u, v);
            const float crownMask = scalpRegions.x;
            const float templeMask = scalpRegions.y;
            const float napeMask = scalpRegions.z;

            // Push roots away from the procedural parting map asymmetrically.
            u = WrapUnit(u + side * (0.105f + 0.075f * crownMask) * partMask + asymSeedC * 0.020f + templeMask * side * 0.032f - napeMask * side * 0.010f);

            // Shape the hairline/crown/nape regions from a scalp-region map instead of a uniform sphere distribution.
            v = Clamp(v + 0.125f * partMask + 0.080f * crownMask - 0.105f * templeMask + 0.060f * napeMask + asymSeedA * 0.026f, 0.05f, 0.93f);

            // Extra off-center parting and region asymmetry.
            if (u > 0.28f && u < 0.64f)
            {
                v = Clamp(v + 0.075f + 0.050f * side - 0.030f * templeMask + 0.022f * crownMask, 0.05f, 0.93f);
                u = WrapUnit(u + 0.018f * side + 0.010f * asymSeedB);
            }

            RootBinding binding = FindClosestBindingForUv(asset.scalpMesh, u, v);

            StrandMaterial material;
            material.rootWidth = 0.0092f;
            material.tipWidth = 0.00125f;
            material.stiffness = 0.92f;
            material.damping = 0.040f;

            FollowData guideFollow;
            guideFollow.parentGuide = static_cast<int32_t>(guideIndex);
            guideFollow.followWeight = 1.0f;
            guideFollow.clumpWeight = 0.0f;
            guideFollow.noiseWeight = 0.0f;

            AppendStaticWigStrand(
                asset,
                binding,
                scalpCenter,
                material,
                guideFollow,
                particlesPerStrand,
                0,
                static_cast<uint32_t>(StrandFlags::Guide),
                guideIndex + 1u,
                segmentLength,
                parameters,
                Vec3(0.0f, 0.0f, 0.0f));

            const Vec3 normal = EvaluateTriangleNormal(asset.scalpMesh, binding);
            Vec3 tangent = Normalize(Vec3(-normal.z, 0.0f, normal.x));
            if (Length(tangent) <= 1.0e-6f)
            {
                tangent = Vec3(1.0f, 0.0f, 0.0f);
            }
            Vec3 binormal = Normalize(Cross(normal, tangent));
            if (Length(binormal) <= 1.0e-6f)
            {
                binormal = Vec3(0.0f, 0.0f, 1.0f);
            }

            for (uint32_t followerIndex = 0; followerIndex < followersPerGuide; ++followerIndex)
            {
                const uint32_t seed = guideIndex * 97u + followerIndex * 17u + 13u;
                const float spreadSeed = Hash01(seed + 5u);
                const float ring = parameters.followerSpread * (0.45f + 0.55f * spreadSeed);
                const Vec3 bindingRootPosition = EvaluateRootBindingPosition(asset.scalpMesh, binding);
                const float asymGuideSide = (bindingRootPosition.x >= scalpCenter.x) ? 1.0f : -1.0f;
                const float angle = (Hash01(seed + 9u) * 0.37f + 0.07f * std::sin(float(guideIndex) * 0.37f) + 0.16f * asymGuideSide + 0.04f * binding.rootUv.y) * kTau;
                const float sideSkew = (Hash01(seed + 3u) - 0.5f) * 0.18f + asymGuideSide * 0.30f;
                const float outwardBias = 0.62f + 0.28f * Hash01(seed + 27u);
                const float verticalBias = (Hash01(seed + 31u) - 0.5f) * 0.07f + asymGuideSide * 0.02f;
                const Vec3 rootOffset =
                    tangent * ((std::cos(angle) * outwardBias + sideSkew * 0.20f) * ring) +
                    binormal * (std::sin(angle) * ring * (0.02f + 0.03f * Hash01(seed + 37u))) +
                    normal * (verticalBias * ring * 0.18f);

                StrandMaterial followerMaterial = material;
                followerMaterial.rootWidth *= 0.86f;
                followerMaterial.tipWidth *= 0.80f;

                FollowData followerFollow;
                followerFollow.parentGuide = static_cast<int32_t>(guideIndex);
                followerFollow.followWeight = 0.992f + Hash01(seed + 7u) * 0.006f;
                followerFollow.clumpWeight = 0.975f + Hash01(seed + 19u) * 0.020f;
                followerFollow.noiseWeight = 0.002f + Hash01(seed + 23u) * 0.008f;

                AppendStaticWigStrand(
                    asset,
                    binding,
                    scalpCenter,
                    followerMaterial,
                    followerFollow,
                    particlesPerStrand,
                    1,
                    static_cast<uint32_t>(StrandFlags::None),
                    seed,
                    segmentLength,
                    parameters,
                    rootOffset);
            }
        }

        RebuildRootRestPositionsFromScalp(asset);

        WigStyleSettings wigSettings;
        wigSettings.strength = 0.42f;
        wigSettings.capRegion = 0.26f;
        wigSettings.shellOffset = 0.0012f;
        wigSettings.flowBlend = 1.0f;
        wigSettings.outwardBlend = 0.02f;
        wigSettings.restoreOriginalTowardsTip = 0.08f;
        StyleAssetToScalpWig(asset, wigSettings);
        return asset;
    }

    HairAsset BuildSampleGuideHairAsset(uint32_t strandCount, uint32_t particlesPerStrand, float segmentLength)
    {
        return BuildStaticWigHairAsset(strandCount, 0, particlesPerStrand, segmentLength, DefaultWigParameters());
    }

    HairAsset BuildSampleHybridHairAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand, float segmentLength)
    {
        return BuildStaticWigHairAsset(guideCount, followersPerGuide, particlesPerStrand, segmentLength, DefaultWigParameters());
    }

    void SetSampleMaskOverridePaths(const char* partingMaskPath, const char* regionMaskPath)
    {
        g_partingMaskOverridePath = partingMaskPath ? partingMaskPath : "";
        g_regionMaskOverridePath = regionMaskPath ? regionMaskPath : "";
        g_sampleMasksDirty = true;
    }

    void ClearSampleMaskOverridePaths()
    {
        g_partingMaskOverridePath.clear();
        g_regionMaskOverridePath.clear();
        g_sampleMasksDirty = true;
    }

    void SetSampleMaskSourceMode(SampleMaskSourceMode mode)
    {
        g_sampleMaskSourceMode = mode;
        g_sampleMasksDirty = true;
    }

    SampleMaskSourceMode GetSampleMaskSourceMode()
    {
        return g_sampleMaskSourceMode;
    }
}
