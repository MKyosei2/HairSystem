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

        float EvaluateFaceOpeningMask(float u, float v)
        {
            float du = std::fabs(WrapUnit(u) - 0.25f);
            du = std::min(du, 1.0f - du);
            const float frontCenter = Smooth01(1.0f - Clamp(du / 0.115f, 0.0f, 1.0f));
            const float foreheadBand = Smooth01(Clamp((v - 0.10f) / 0.10f, 0.0f, 1.0f)) *
                                       Smooth01(1.0f - Clamp((v - 0.64f) / 0.14f, 0.0f, 1.0f));
            const float cheekTaper = Smooth01(1.0f - Clamp(std::fabs(v - 0.34f) / 0.24f, 0.0f, 1.0f));
            return frontCenter * foreheadBand * cheekTaper;
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


        float WrapSignedUnitDelta(float v)
        {
            while (v > 0.5f)
            {
                v -= 1.0f;
            }
            while (v < -0.5f)
            {
                v += 1.0f;
            }
            return v;
        }

        void EvaluateScalpFrameFromBinding(const ScalpMesh& scalpMesh, const RootBinding& binding, Vec3& outNormal, Vec3& outTangent, Vec3& outBitangent)
        {
            outNormal = EvaluateTriangleNormal(scalpMesh, binding);
            outTangent = Vec3(-outNormal.z, 0.0f, outNormal.x);
            if (Length(outTangent) <= 1.0e-6f)
            {
                outTangent = Vec3(1.0f, 0.0f, 0.0f);
            }
            outTangent = Normalize(outTangent - outNormal * Dot(outTangent, outNormal));
            outBitangent = Normalize(Cross(outNormal, outTangent));

            if (binding.triangleIndex >= scalpMesh.triangles.size() ||
                scalpMesh.positions.empty() ||
                scalpMesh.uvs.size() != scalpMesh.positions.size())
            {
                return;
            }

            const TriangleIndices& tri = scalpMesh.triangles[binding.triangleIndex];
            const Vec3& p0 = scalpMesh.positions[tri.a];
            const Vec3& p1 = scalpMesh.positions[tri.b];
            const Vec3& p2 = scalpMesh.positions[tri.c];
            const Vec2& uv0 = scalpMesh.uvs[tri.a];
            const Vec2& uv1 = scalpMesh.uvs[tri.b];
            const Vec2& uv2 = scalpMesh.uvs[tri.c];

            const Vec3 dp1 = p1 - p0;
            const Vec3 dp2 = p2 - p0;
            const float du1 = WrapSignedUnitDelta(uv1.x - uv0.x);
            const float dv1 = uv1.y - uv0.y;
            const float du2 = WrapSignedUnitDelta(uv2.x - uv0.x);
            const float dv2 = uv2.y - uv0.y;
            const float det = du1 * dv2 - du2 * dv1;

            if (std::fabs(det) > 1.0e-7f)
            {
                const float invDet = 1.0f / det;
                Vec3 tangent = (dp1 * dv2 - dp2 * dv1) * invDet;
                tangent = tangent - outNormal * Dot(tangent, outNormal);
                if (Length(tangent) > 1.0e-6f)
                {
                    outTangent = Normalize(tangent);
                    Vec3 bitangent = Cross(outNormal, outTangent);
                    if (Length(bitangent) > 1.0e-6f)
                    {
                        outBitangent = Normalize(bitangent);
                    }
                }
            }
        }


        float ComputeGuideLength(const Vec3& root, const Vec3& scalpCenter, const Vec2& uv, float baseLength, const WigParameters& parameters)
        {
            const float lowerRing = Clamp((1.0f - Clamp((root.y - (scalpCenter.y - 0.02f)) / 0.26f, 0.0f, 1.0f)), 0.0f, 1.0f);
            const float crownReduction = Clamp((uv.y - 0.40f) / 0.60f, 0.0f, 1.0f) * 0.025f;
            const float faceOpening = EvaluateFaceOpeningMask(uv.x, uv.y);
            const float frontTrim = 0.03f * faceOpening;
            return baseLength * parameters.overallLength * (1.0f + parameters.lowerRingLengthBias * lowerRing - crownReduction - frontTrim);
        }

        Vec3 SampleCubicBezierByApproxArcLength(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t)
        {
            constexpr uint32_t kSamples = 24u;
            t = Clamp(t, 0.0f, 1.0f);

            Vec3 samplePoints[kSamples + 1u];
            float cumulative[kSamples + 1u];
            samplePoints[0] = CubicBezier(p0, p1, p2, p3, 0.0f);
            cumulative[0] = 0.0f;

            for (uint32_t i = 1u; i <= kSamples; ++i)
            {
                const float u = static_cast<float>(i) / static_cast<float>(kSamples);
                samplePoints[i] = CubicBezier(p0, p1, p2, p3, u);
                cumulative[i] = cumulative[i - 1u] + Length(samplePoints[i] - samplePoints[i - 1u]);
            }

            const float totalLength = cumulative[kSamples];
            if (totalLength <= 1.0e-6f)
            {
                return CubicBezier(p0, p1, p2, p3, t);
            }

            const float target = totalLength * t;
            for (uint32_t i = 1u; i <= kSamples; ++i)
            {
                if (target <= cumulative[i])
                {
                    const float span = cumulative[i] - cumulative[i - 1u];
                    const float local = (span > 1.0e-6f) ? ((target - cumulative[i - 1u]) / span) : 0.0f;
                    return Lerp(samplePoints[i - 1u], samplePoints[i], local);
                }
            }

            return samplePoints[kSamples];
        }

        Vec3 LimitControlPoint(const Vec3& root, const Vec3& point, const Vec3& flowDir, float minAlong, float maxLateral)
        {
            Vec3 offset = point - root;
            float along = Dot(offset, flowDir);
            Vec3 lateral = offset - flowDir * along;

            if (along < minAlong)
            {
                along = minAlong;
            }

            const float lateralLength = Length(lateral);
            if (lateralLength > maxLateral && lateralLength > 1.0e-6f)
            {
                lateral = lateral * (maxLateral / lateralLength);
            }

            return root + flowDir * along + lateral;
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

            const float angle = WrapUnit(uv.x) * kTau;
            const float sideSign = (std::cos(angle) >= 0.0f) ? 1.0f : -1.0f;
            const float frontFactor = std::max(std::sin(angle), 0.0f);
            const float backFactor = std::max(-std::sin(angle), 0.0f);
            const float sideFactor = std::fabs(std::cos(angle));
            const float faceOpening = EvaluateFaceOpeningMask(uv.x, uv.y);
            const float crownFactor = Smooth01(1.0f - Clamp(std::fabs(uv.y - 0.15f) / 0.16f, 0.0f, 1.0f));
            const float napeFactor = Smooth01(Clamp((uv.y - 0.56f) / 0.26f, 0.0f, 1.0f));

            Vec3 lateral = Normalize(Vec3(sideSign, 0.0f, 0.0f) + radial * 0.08f);
            lateral = Normalize(lateral - normal * Dot(lateral, normal));
            if (Length(lateral) <= 1.0e-6f)
            {
                lateral = Vec3(sideSign, 0.0f, 0.0f);
            }

            Vec3 flowDir = Normalize(
                Vec3(0.0f, -1.0f, 0.0f) * (0.990f + 0.004f * crownFactor) +
                Vec3(0.0f, 0.0f, -1.0f) * (0.030f + 0.115f * frontFactor + 0.020f * faceOpening) +
                radial * (0.010f + 0.018f * backFactor));
            if (Length(flowDir) <= 1.0e-6f)
            {
                flowDir = Vec3(0.0f, -1.0f, 0.0f);
            }

            Vec3 scalpFlow = flowDir - normal * Dot(flowDir, normal);
            if (Length(scalpFlow) <= 1.0e-6f)
            {
                scalpFlow = lateral * 0.25f + Vec3(0.0f, -1.0f, 0.0f);
            }
            scalpFlow = Normalize(scalpFlow);

            const float centeredX = root.x - (scalpCenter.x + parameters.partingOffset);
            const float partDistance = std::fabs(centeredX);
            const float partInfluence = 1.0f - Clamp(partDistance / std::max(parameters.partWidth * 1.15f, 1.0e-4f), 0.0f, 1.0f);
            const float baseChainLength = std::max(segmentLength * static_cast<float>(std::max(pointCount - 1u, 1u)), segmentLength);
            const float length = ComputeGuideLength(root, scalpCenter, uv, baseChainLength, parameters);

            const float strandLengthJitter = 0.94f + 0.12f * Hash01(seed + 41u);
            const float strandSpreadJitter = 0.88f + 0.22f * Hash01(seed + 43u);
            const float strandFrontJitter = 0.92f + 0.16f * Hash01(seed + 47u);
            const float strandLiftJitter = 0.88f + 0.20f * Hash01(seed + 53u);
            const float strandSplay = (Hash01(seed + 59u) - 0.5f) * 2.0f;
            const float strandRoll = (Hash01(seed + 61u) - 0.5f) * 2.0f;

            const float bangMask = frontFactor * Smooth01(1.0f - Clamp(std::fabs(uv.y - 0.27f) / 0.18f, 0.0f, 1.0f));
            const float backCurtainMask = backFactor * Smooth01(Clamp((uv.y - 0.32f) / 0.38f, 0.0f, 1.0f));
            const float templeTailMask = sideFactor * Smooth01(1.0f - Clamp(std::fabs(uv.y - 0.42f) / 0.24f, 0.0f, 1.0f)) * (1.0f - 0.48f * faceOpening);
            const float faceFrameMask = frontFactor * sideFactor * Smooth01(1.0f - Clamp(std::fabs(uv.y - 0.34f) / 0.20f, 0.0f, 1.0f));
            const float sideSweepMask = ((sideSign > 0.0f) ? 1.0f : 0.0f) * frontFactor * Smooth01(1.0f - Clamp(std::fabs(uv.y - 0.32f) / 0.18f, 0.0f, 1.0f));
            const float capHugMask = parameters.scalpConform * Smooth01(1.0f - Clamp((uv.y - 0.12f) / 0.26f, 0.0f, 1.0f));

            const float orientationLengthScale = Clamp(
                1.0f
                - 0.42f * bangMask
                - 0.10f * faceOpening
                + 0.30f * backCurtainMask
                + 0.14f * templeTailMask
                + 0.08f * faceFrameMask
                + 0.08f * sideSweepMask,
                0.52f,
                1.52f);

            const float strandLength = length * strandLengthJitter * orientationLengthScale;
            const float partPush = (0.0052f + parameters.partWidth * 0.78f + 0.0032f * crownFactor) * partInfluence * (0.75f + 0.25f * parameters.scalpConform) * strandSpreadJitter;
            const float sideSpreadMid = parameters.silhouetteWidth * (0.18f + 0.34f * sideFactor + 0.18f * napeFactor + 0.16f * templeTailMask + 0.10f * faceFrameMask + 0.14f * sideSweepMask) * (1.0f - 0.90f * faceOpening) * strandSpreadJitter;
            const float sideSpreadTip = parameters.silhouetteWidth * (0.08f + 0.18f * sideFactor + 0.12f * napeFactor + 0.18f * templeTailMask + 0.16f * faceFrameMask + 0.18f * sideSweepMask) * (1.0f - 0.84f * faceOpening) * (0.92f + 0.18f * Hash01(seed + 67u));
            const float frontRetreat = (0.012f + 0.020f * faceOpening + 0.018f * bangMask + 0.010f * faceFrameMask) * frontFactor * strandFrontJitter;
            const float rootLift = parameters.rootLift * (0.16f + 0.10f * crownFactor - 0.10f * bangMask - 0.08f * faceFrameMask) * strandLiftJitter;

            const Vec3 orthoLift = Normalize(Cross(normal, lateral));
            const Vec3 splayOffset = lateral * (parameters.silhouetteWidth * (0.38f + 0.16f * templeTailMask) * strandSplay) + orthoLift * (parameters.silhouetteWidth * 0.12f * strandRoll);
            const Vec3 tipTwistOffset = lateral * (parameters.tipInward * (0.44f + 0.18f * sideSweepMask) * strandSplay) + orthoLift * (parameters.tipInward * 0.20f * strandRoll);

            const float bangDrop = strandLength * (0.16f * bangMask);
            const float backDrop = strandLength * (0.18f * backCurtainMask);
            const float faceFramePull = parameters.silhouetteWidth * (0.30f * faceFrameMask);
            const float sideSweepOffset = parameters.silhouetteWidth * (0.46f * sideSweepMask);
            const float capSlide = strandLength * (0.08f + 0.04f * capHugMask + 0.03f * bangMask);
            const float midSlide = strandLength * (0.38f + 0.10f * backCurtainMask + 0.06f * faceFrameMask);

            const Vec3 p0 = root;
            Vec3 p1 = root
                + normal * (rootLift * (1.0f - 0.70f * capHugMask))
                + lateral * (partPush * 0.14f + sideSweepOffset * 0.14f + faceFramePull * 0.08f)
                + splayOffset * 0.10f
                + scalpFlow * capSlide
                + flowDir * (bangDrop * 0.14f);

            Vec3 p2 = root
                + normal * (rootLift * 0.05f * (1.0f - 0.65f * capHugMask))
                + lateral * (sideSpreadMid + partPush * 0.42f + sideSweepOffset * 0.50f + faceFramePull * 0.42f)
                + splayOffset * 0.32f
                + Vec3(0.0f, 0.0f, -1.0f) * frontRetreat
                + flowDir * (midSlide + bangDrop * 0.42f);

            Vec3 p3 = root
                + lateral * (sideSpreadTip + partPush * 0.16f + sideSweepOffset * 0.70f + faceFramePull * 0.55f)
                + splayOffset * 0.44f
                + tipTwistOffset * 0.70f
                + Vec3(0.0f, 0.0f, -1.0f) * (frontRetreat * 1.04f)
                + flowDir * (strandLength + bangDrop + backDrop);

            p1 = LimitControlPoint(root, p1, flowDir, strandLength * 0.08f, strandLength * (0.10f + 0.03f * sideFactor));
            p2 = LimitControlPoint(root, p2, flowDir, strandLength * 0.24f, strandLength * (0.22f + 0.05f * sideFactor + 0.04f * backCurtainMask));
            p3 = LimitControlPoint(root, p3, flowDir, strandLength * 0.76f, strandLength * (0.28f + 0.08f * sideFactor + 0.08f * backCurtainMask));

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
                Vec3 p = SampleCubicBezierByApproxArcLength(p0, p1, p2, p3, t);

                if (pointIndex > pointCount / 2u)
                {
                    const float weirdHairSuppression = 1.0f - 0.72f * bangMask - 0.48f * backCurtainMask - 0.20f * faceFrameMask;
                    const float atten = parameters.strandNoise * (0.007f + 0.010f * t) * (1.0f - 0.88f * partInfluence) * weirdHairSuppression;
                    const float noiseA = std::sin((t * 2.0f + Hash01(seed + pointIndex * 11u) * 0.35f) * kTau);
                    const float noiseB = std::cos((t * 1.3f + Hash01(seed + pointIndex * 13u) * 0.35f) * kTau);
                    p += orthoA * (noiseA * atten * 0.05f);
                    p += orthoB * (noiseB * atten * 0.025f);
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
        parameters.overallLength = 1.12f;
        parameters.partingOffset = -0.012f;
        parameters.partWidth = 0.016f;
        parameters.scalpConform = 0.965f;
        parameters.rootLift = 0.0014f;
        parameters.silhouetteWidth = 0.0040f;
        parameters.sideVolume = 0.0010f;
        parameters.backVolume = 0.0018f;
        parameters.lowerRingLengthBias = 0.16f;
        parameters.strandNoise = 0.00020f;
        parameters.followerSpread = 0.00045f;
        parameters.tipInward = 0.0008f;
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

        const uint32_t guideCols = std::max(8u, static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(guideCount) * 1.12f))));
        const uint32_t guideRows = std::max(1u, (guideCount + guideCols - 1u) / guideCols);

        for (uint32_t guideIndex = 0; guideIndex < guideCount; ++guideIndex)
        {
            const uint32_t row = guideIndex / guideCols;
            const uint32_t col = guideIndex % guideCols;
            const float jx = Hash01(guideIndex * 11u + 1u) - 0.5f;
            const float jy = Hash01(guideIndex * 13u + 7u) - 0.5f;
            const float jz = Hash01(guideIndex * 17u + 19u) - 0.5f;

            const float fx = (static_cast<float>(col) + 0.5f + jx * 0.62f) / static_cast<float>(guideCols);
            const float fy = (static_cast<float>(row) + 0.5f + jy * 0.74f) / static_cast<float>(guideRows);
            const float sideBase = (fx < 0.5f) ? -1.0f : 1.0f;
            const float crownBias = Smooth01(1.0f - Clamp(fy / 0.24f, 0.0f, 1.0f));

            float u = WrapUnit(fx + sideBase * (0.008f + 0.012f * crownBias) + jz * 0.010f);
            float v = Clamp(0.035f + std::pow(Clamp(fy, 0.0f, 1.0f), 0.88f) * 0.83f + jy * 0.018f, 0.035f, 0.93f);

            const float side = SignedSideFromU(u);
            const float partMask = EvaluatePartingMap(u, v, 0.02f + parameters.partingOffset * 0.5f);
            const Vec3 scalpRegions = EvaluateScalpRegionMap(u, v);
            const float crownMask = scalpRegions.x;
            const float templeMask = scalpRegions.y;
            const float napeMask = scalpRegions.z;
            const float faceOpening = EvaluateFaceOpeningMask(u, v);

            u = WrapUnit(u + side * (0.055f + 0.055f * crownMask) * partMask + templeMask * side * 0.014f + jz * 0.004f);
            v = Clamp(v + 0.042f * partMask + 0.020f * crownMask - 0.032f * templeMask + 0.020f * napeMask, 0.035f, 0.93f);

            if (faceOpening > 0.0f)
            {
                const float pushSign = ((WrapUnit(u) - 0.25f) < 0.0f) ? -1.0f : 1.0f;
                u = WrapUnit(u + pushSign * (0.10f + 0.14f * faceOpening));
                v = Clamp(v + 0.018f + 0.040f * faceOpening, 0.035f, 0.93f);
            }

            RootBinding binding = FindClosestBindingForUv(asset.scalpMesh, u, v);

            StrandMaterial material;
            material.rootWidth = 0.0032f;
            material.tipWidth = 0.00016f;
            material.stiffness = 0.95f;
            material.damping = 0.055f;

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

            Vec3 normal;
            Vec3 tangent;
            Vec3 binormal;
            EvaluateScalpFrameFromBinding(asset.scalpMesh, binding, normal, tangent, binormal);

            const Vec2 bindingUv = EvaluateTriangleUv(asset.scalpMesh, binding);
            const Vec3 scalpRegionsLocal = EvaluateScalpRegionMap(bindingUv.x, bindingUv.y);
            const float crownMaskLocal = scalpRegionsLocal.x;
            const float templeMaskLocal = scalpRegionsLocal.y;
            const float napeMaskLocal = scalpRegionsLocal.z;
            const float faceOpeningLocal = EvaluateFaceOpeningMask(bindingUv.x, bindingUv.y);
            const Vec3 bindingRootPosition = EvaluateRootBindingPosition(asset.scalpMesh, binding);
            const Vec3 radialDir = SafeNormalizeXZ(bindingRootPosition - scalpCenter);
            Vec3 rimDir = Normalize(Cross(normal, radialDir));
            if (Length(rimDir) <= 1.0e-6f)
            {
                rimDir = tangent;
            }
            const float edgeRisk = Clamp(
                templeMaskLocal * 0.78f +
                napeMaskLocal * 0.52f +
                faceOpeningLocal * 0.92f,
                0.0f,
                1.0f);
            const float followerSpreadScale =
                (1.0f - 0.54f * templeMaskLocal) *
                (1.0f - 0.62f * napeMaskLocal) *
                (1.0f - 0.64f * faceOpeningLocal) *
                (0.86f + 0.10f * crownMaskLocal);
            const float guideRotation = Hash01(guideIndex * 29u + 3u) * kTau;
            const float goldenAngle = 2.39996323f;

            for (uint32_t followerIndex = 0; followerIndex < followersPerGuide; ++followerIndex)
            {
                const uint32_t seed = guideIndex * 97u + followerIndex * 17u + 13u;
                const float ring01 = std::sqrt((static_cast<float>(followerIndex) + 0.5f) / static_cast<float>(std::max(followersPerGuide, 1u)));
                const float ring = parameters.followerSpread * followerSpreadScale * Lerp(0.08f, 0.42f, ring01);
                const float angle = guideRotation + goldenAngle * static_cast<float>(followerIndex) + (Hash01(seed + 9u) - 0.5f) * 0.16f;
                const float radialJitter = 0.94f + 0.10f * Hash01(seed + 5u);
                const float tangentJitter = (Hash01(seed + 3u) - 0.5f) * 0.05f;
                const float bitangentScale = 0.82f + 0.08f * Hash01(seed + 37u);
                const float verticalBias = (Hash01(seed + 31u) - 0.5f) * 0.010f;

                Vec3 planarOffset =
                    tangent * ((std::cos(angle) + tangentJitter) * ring * radialJitter) +
                    binormal * (std::sin(angle) * ring * bitangentScale);

                const float outwardAmount = Dot(planarOffset, radialDir);
                if (outwardAmount > 0.0f)
                {
                    planarOffset = planarOffset - radialDir * (outwardAmount * (0.82f + 0.12f * edgeRisk));
                    planarOffset += rimDir * ((Hash01(seed + 83u) - 0.5f) * outwardAmount * 0.08f * edgeRisk);
                }

                Vec3 rootOffset = planarOffset + normal * (verticalBias * ring * (1.0f - 0.75f * edgeRisk));
                const float maxOffset = parameters.followerSpread * Lerp(0.24f, 0.44f, 1.0f - edgeRisk);
                const float rootOffsetLength = Length(rootOffset);
                if (rootOffsetLength > maxOffset && rootOffsetLength > 1.0e-6f)
                {
                    rootOffset = rootOffset * (maxOffset / rootOffsetLength);
                }

                StrandMaterial followerMaterial = material;
                const float widthJitter = 0.88f + 0.16f * Hash01(seed + 71u);
                followerMaterial.rootWidth *= 0.48f * widthJitter;
                followerMaterial.tipWidth *= 0.40f * widthJitter;

                FollowData followerFollow;
                followerFollow.parentGuide = static_cast<int32_t>(guideIndex);
                followerFollow.followWeight = 0.972f + Hash01(seed + 7u) * 0.008f - edgeRisk * 0.018f;
                followerFollow.clumpWeight = 0.84f + Hash01(seed + 19u) * 0.040f - edgeRisk * 0.08f;
                followerFollow.noiseWeight = 0.00006f + Hash01(seed + 23u) * 0.00010f + edgeRisk * 0.00004f;

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
        wigSettings.strength = 0.12f;
        wigSettings.capRegion = 0.18f;
        wigSettings.shellOffset = 0.00025f;
        wigSettings.flowBlend = 0.96f;
        wigSettings.outwardBlend = 0.02f;
        wigSettings.restoreOriginalTowardsTip = 0.18f;
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
