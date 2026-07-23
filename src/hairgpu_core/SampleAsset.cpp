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

#include "hairgpu/Geometry.h"
#include "hairgpu/ProceduralMath.h"
#include "hairgpu/ScalpGeometry.h"
namespace hairgpu
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTau = 6.28318530717958647692f;

        float Fract(float value)
        {
            return ::hairgpu::Fract(value);
        }

        float Hash01(uint32_t value)
        {
            return ::hairgpu::Hash01Sine(value);
        }

        float WrapUnit(float v)
        {
            return ::hairgpu::WrapUnit(v);
        }

        float Smooth01(float v)
        {
            return ::hairgpu::SmoothStep01(v);
        }

        float SmoothFalloff01(float x)
        {
            return Smooth01(1.0f - Clamp(x, 0.0f, 1.0f));
        }

        float SmoothDistanceBand(float distance, float width)
        {
            return SmoothFalloff01(distance / std::max(width, 1.0e-6f));
        }

        float SmoothBand(float value, float center, float width)
        {
            return SmoothFalloff01(std::fabs(value - center) / std::max(width, 1.0e-6f));
        }

        float SmoothRamp(float value, float start, float width)
        {
            return Smooth01(Clamp((value - start) / std::max(width, 1.0e-6f), 0.0f, 1.0f));
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
            const float centerBand = SmoothDistanceBand(d, 0.085f);
            const float crownBand = SmoothBand(v, 0.18f, 0.16f);
            const float upperBand = SmoothFalloff01((v - 0.42f) / 0.24f);
            return centerBand * crownBand * upperBand;
        }

        Vec3 EvaluateProceduralScalpRegionMap(float u, float v)
        {
            const float angle = WrapUnit(u) * kTau;
            const float side = std::cos(angle);
            const float frontBack = std::sin(angle);
            const float crown = SmoothBand(v, 0.16f, 0.13f);
            const float temple = Smooth01(Clamp((std::fabs(side) - 0.36f) / 0.42f, 0.0f, 1.0f)) * SmoothBand(v, 0.34f, 0.18f);
            const float nape = SmoothRamp(v, 0.58f, 0.20f) * SmoothRamp(-frontBack, -0.10f, 0.90f);
            return Vec3(crown, temple, nape);
        }

        float EvaluateFaceOpeningMask(float u, float v)
        {
            float du = std::fabs(WrapUnit(u) - 0.25f);
            du = std::min(du, 1.0f - du);
            const float frontCenter = SmoothDistanceBand(du, 0.074f);
            const float foreheadBand = SmoothRamp(v, 0.12f, 0.08f) *
                                       SmoothFalloff01((v - 0.48f) / 0.10f);
            const float cheekTaper = SmoothBand(v, 0.30f, 0.18f);
            return frontCenter * foreheadBand * cheekTaper * 0.72f;
        }

        float EvaluatePartLineCenterMask(float u, float v, float partingOffset = 0.0f)
        {
            return EvaluateProceduralPartingMap(u, v, partingOffset);
        }

        float EvaluatePartShoulderMask(float u, float v, float partingOffset = 0.0f)
        {
            const float center = EvaluateProceduralPartingMap(u, v, partingOffset);
            const float left = EvaluateProceduralPartingMap(WrapUnit(u + 0.030f), v, partingOffset);
            const float right = EvaluateProceduralPartingMap(WrapUnit(u - 0.030f), v, partingOffset);
            const float shoulder = std::max(left, right) - center * 0.55f;
            return Clamp(shoulder, 0.0f, 1.0f);
        }


        constexpr float kHeadRadiusX = 0.23f;
        constexpr float kHeadRadiusY = 0.20f;
        constexpr float kHeadRadiusZ = 0.19f;
        constexpr uint32_t kLengthResampleMinSamples = 64u;
        constexpr float kTargetAverageStrandLengthMeters = 0.44f;

        float ComputeReferenceStrandLength(float segmentLength)
        {
            (void)segmentLength;
            return kTargetAverageStrandLengthMeters;
        }

        float EvaluateAllowedRootMinV(float u)
        {
            (void)u;
            // v=0 is the pole of the scalp mesh. Excluding the first two
            // latitude rings created the circular bald patch seen from above.
            return 0.0f;
        }

        float EvaluateAllowedRootMaxV(float u)
        {
            const float angle = WrapUnit(u) * kTau;
            const float front = std::max(std::sin(angle), 0.0f);
            const float back = std::max(-std::sin(angle), 0.0f);
            const float side = std::fabs(std::cos(angle));

            // The old front limit was v=0.16, which placed the hairline far
            // above the forehead and produced a wide exposed scalp band.
            // Keep the face open, but allow a complete front hairline.
            float maxV =
                0.30f +
                0.12f * side +
                0.18f * back -
                0.02f * front;

            float du = std::fabs(WrapUnit(u) - 0.25f);
            du = std::min(du, 1.0f - du);
            const float fringe = SmoothDistanceBand(du, 0.12f);
            maxV = std::min(maxV, Lerp(maxV, 0.32f, fringe));
            return Clamp(maxV, 0.28f, 0.58f);
        }

        Vec3 ProjectOntoPlaneDir(const Vec3& v, const Vec3& n)
        {
            return Normalize(v - n * Dot(v, n));
        }

        Vec3 ProjectPointOutsideHeadVolume(const Vec3& point, const Vec3& center, float shell)
        {
            const float rx = kHeadRadiusX * 0.98f;
            const float ry = kHeadRadiusY * 1.02f;
            const float rz = kHeadRadiusZ * 0.98f;
            const float shellScale = 1.0f + shell / ((rx + ry + rz) / 3.0f);

            Vec3 local = point - center;
            float nx = local.x / rx;
            float ny = local.y / ry;
            float nz = local.z / rz;
            float ellipsoidLength = std::sqrt(nx * nx + ny * ny + nz * nz);
            Vec3 result = point;
            if (ellipsoidLength < shellScale)
            {
                const float scale = shellScale / std::max(ellipsoidLength, 1.0e-4f);
                result = center + Vec3(nx * scale * rx, ny * scale * ry, nz * scale * rz);
            }

            if (result.z > center.z + 0.015f && result.y < center.y - 0.050f)
            {
                const float frontMask = Clamp((result.z - (center.z + 0.015f)) / 0.10f, 0.0f, 1.0f);
                const float lowerMask = Clamp(((center.y - 0.050f) - result.y) / 0.14f, 0.0f, 1.0f);
                const float faceLift = frontMask * lowerMask;
                result.z += 0.030f * faceLift;
                result.y += 0.020f * faceLift;
            }

            return result;
        }

        Vec3 DeformMannequinHeadPoint(const Vec3& unit, const Vec3& center, float radiusX, float radiusY, float radiusZ)
        {
            Vec3 p(
                center.x + unit.x * radiusX,
                center.y + unit.y * radiusY,
                center.z + unit.z * radiusZ);

            const float front = std::max(unit.z, 0.0f);
            const float back = std::max(-unit.z, 0.0f);
            const float midFace = Smooth01(1.0f - Clamp(std::fabs(unit.y + 0.02f) / 0.28f, 0.0f, 1.0f));
            const float lowerFace = Smooth01(1.0f - Clamp(std::fabs(unit.y + 0.32f) / 0.20f, 0.0f, 1.0f));
            const float browBand = Smooth01(1.0f - Clamp(std::fabs(unit.y - 0.18f) / 0.16f, 0.0f, 1.0f));
            const float neckMask = Smooth01(Clamp((-unit.y - 0.54f) / 0.26f, 0.0f, 1.0f));

            p.z += front * (0.010f * browBand + 0.022f * midFace + 0.012f * lowerFace);
            p.x = center.x + (p.x - center.x) * (1.0f - front * (0.10f * midFace + 0.06f * lowerFace));
            p.y -= front * (0.010f * midFace + 0.020f * lowerFace);
            p.z -= back * 0.008f * Smooth01(1.0f - Clamp(std::fabs(unit.y - 0.08f) / 0.38f, 0.0f, 1.0f));
            p.x = center.x + (p.x - center.x) * (1.0f - 0.16f * neckMask);
            p.z = center.z + (p.z - center.z) * (1.0f - 0.10f * neckMask);
            p.y -= 0.030f * neckMask;
            return p;
        }

        bool ReorderGeneratedGuideFollowerLayout(
            HairAsset& asset,
            uint32_t guideCount,
            uint32_t followersPerGuide)
        {
            const uint64_t expectedStrands =
                static_cast<uint64_t>(guideCount) *
                static_cast<uint64_t>(followersPerGuide + 1u);
            if (asset.strands.size() != expectedStrands ||
                asset.materials.size() != expectedStrands ||
                asset.rootBindings.size() != expectedStrands ||
                asset.followData.size() != expectedStrands ||
                asset.pointWidths.size() != asset.restPoints.size())
            {
                return false;
            }

            std::vector<StrandRange> reorderedStrands;
            std::vector<Vec3> reorderedRestPoints;
            std::vector<float> reorderedPointWidths;
            std::vector<StrandMaterial> reorderedMaterials;
            std::vector<RootBinding> reorderedRootBindings;
            std::vector<FollowData> reorderedFollowData;

            reorderedStrands.reserve(asset.strands.size());
            reorderedRestPoints.reserve(asset.restPoints.size());
            reorderedPointWidths.reserve(asset.pointWidths.size());
            reorderedMaterials.reserve(asset.materials.size());
            reorderedRootBindings.reserve(asset.rootBindings.size());
            reorderedFollowData.reserve(asset.followData.size());

            auto appendOldStrand = [&](uint32_t oldStrandIndex)
            {
                if (oldStrandIndex >= asset.strands.size())
                {
                    return false;
                }
                const StrandRange& oldRange = asset.strands[oldStrandIndex];
                const uint64_t oldEnd =
                    static_cast<uint64_t>(oldRange.pointOffset) +
                    oldRange.pointCount;
                if (oldEnd > asset.restPoints.size() ||
                    oldEnd > asset.pointWidths.size())
                {
                    return false;
                }

                StrandRange newRange = oldRange;
                newRange.pointOffset =
                    static_cast<uint32_t>(reorderedRestPoints.size());
                reorderedStrands.push_back(newRange);
                reorderedRestPoints.insert(
                    reorderedRestPoints.end(),
                    asset.restPoints.begin() + oldRange.pointOffset,
                    asset.restPoints.begin() + oldEnd);
                reorderedPointWidths.insert(
                    reorderedPointWidths.end(),
                    asset.pointWidths.begin() + oldRange.pointOffset,
                    asset.pointWidths.begin() + oldEnd);
                reorderedMaterials.push_back(asset.materials[oldStrandIndex]);
                reorderedRootBindings.push_back(asset.rootBindings[oldStrandIndex]);
                reorderedFollowData.push_back(asset.followData[oldStrandIndex]);
                return true;
            };

            // AppendStaticWigStrand historically emitted
            // [guide0, followers0..., guide1, followers1...]. The runtime,
            // FollowCS, rendering LOD and telemetry all require
            // [all guides][all followers]. Keep authoring local and normalize
            // the layout once before the asset is exposed to the runtime.
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const uint32_t oldGuideIndex =
                    guideIndex * (followersPerGuide + 1u);
                if (!appendOldStrand(oldGuideIndex))
                {
                    return false;
                }
            }
            for (uint32_t guideIndex = 0u; guideIndex < guideCount; ++guideIndex)
            {
                const uint32_t oldGuideIndex =
                    guideIndex * (followersPerGuide + 1u);
                for (uint32_t followerIndex = 0u;
                     followerIndex < followersPerGuide;
                     ++followerIndex)
                {
                    if (!appendOldStrand(
                            oldGuideIndex + 1u + followerIndex))
                    {
                        return false;
                    }
                }
            }

            asset.strands = std::move(reorderedStrands);
            asset.restPoints = std::move(reorderedRestPoints);
            asset.pointWidths = std::move(reorderedPointWidths);
            asset.materials = std::move(reorderedMaterials);
            asset.rootBindings = std::move(reorderedRootBindings);
            asset.followData = std::move(reorderedFollowData);
            asset.segmentGroupTopology = {};
            return true;
        }

        void RecomputeScalpNormals(ScalpMesh& mesh, const Vec3& center)
        {
            mesh.normals.assign(mesh.positions.size(), Vec3(0.0f, 0.0f, 0.0f));
            for (const TriangleIndices& tri : mesh.triangles)
            {
                if (tri.a >= mesh.positions.size() || tri.b >= mesh.positions.size() || tri.c >= mesh.positions.size())
                {
                    continue;
                }
                const Vec3 a = mesh.positions[tri.a];
                const Vec3 b = mesh.positions[tri.b];
                const Vec3 c = mesh.positions[tri.c];
                const Vec3 faceNormal = Normalize(Cross(b - a, c - a));
                mesh.normals[tri.a] += faceNormal;
                mesh.normals[tri.b] += faceNormal;
                mesh.normals[tri.c] += faceNormal;
            }
            for (size_t i = 0; i < mesh.normals.size(); ++i)
            {
                if (Length(mesh.normals[i]) <= 1.0e-6f)
                {
                    mesh.normals[i] = Normalize(mesh.positions[i] - center);
                }
                else
                {
                    mesh.normals[i] = Normalize(mesh.normals[i]);
                }
            }
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
            return EvaluateScalpNormal(scalp, binding);
        }

        Vec2 EvaluateTriangleUv(const ScalpMesh& scalp, const RootBinding& binding)
        {
            return EvaluateScalpUv(scalp, binding);
        }

        Vec3 ComputeWrappedUvBarycentric(const Vec2& uvA, const Vec2& uvB, const Vec2& uvC, float targetU, float targetV)
        {
            const Vec2 a{ targetU + WrapSignedUnitDelta(uvA.x - targetU), uvA.y };
            const Vec2 b{ targetU + WrapSignedUnitDelta(uvB.x - targetU), uvB.y };
            const Vec2 c{ targetU + WrapSignedUnitDelta(uvC.x - targetU), uvC.y };
            const Vec2 p{ targetU, targetV };

            const Vec2 v0{ b.x - a.x, b.y - a.y };
            const Vec2 v1{ c.x - a.x, c.y - a.y };
            const Vec2 v2{ p.x - a.x, p.y - a.y };
            const float det = v0.x * v1.y - v1.x * v0.y;
            if (std::fabs(det) <= 1.0e-7f)
            {
                return Vec3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);
            }

            const float invDet = 1.0f / det;
            float w1 = (v2.x * v1.y - v1.x * v2.y) * invDet;
            float w2 = (v0.x * v2.y - v2.x * v0.y) * invDet;
            float w0 = 1.0f - w1 - w2;

            w0 = std::max(w0, 0.0f);
            w1 = std::max(w1, 0.0f);
            w2 = std::max(w2, 0.0f);
            const float weightSum = w0 + w1 + w2;
            if (weightSum <= 1.0e-6f)
            {
                return Vec3(1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f);
            }
            return Vec3(w0 / weightSum, w1 / weightSum, w2 / weightSum);
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
                const float centroidU = WrapUnit((uva.x + uvb.x + uvc.x) / 3.0f);
                const float centroidV = (uva.y + uvb.y + uvc.y) / 3.0f;
                float du = std::fabs(centroidU - targetU);
                du = std::min(du, 1.0f - du);
                const float dv = centroidV - targetV;

                const float minAllowedV = EvaluateAllowedRootMinV(centroidU);
                const float maxAllowedV = EvaluateAllowedRootMaxV(centroidU);
                float penalty = 0.0f;
                if (centroidV < minAllowedV)
                {
                    penalty += (minAllowedV - centroidV) * (minAllowedV - centroidV) * 12.0f;
                }
                if (centroidV > maxAllowedV)
                {
                    penalty += (centroidV - maxAllowedV) * (centroidV - maxAllowedV) * 28.0f;
                }

                float frontDu = std::fabs(WrapUnit(centroidU) - 0.25f);
                frontDu = std::min(frontDu, 1.0f - frontDu);
                const float frontCenter = Smooth01(1.0f - Clamp(frontDu / 0.11f, 0.0f, 1.0f));
                penalty += frontCenter * Clamp((centroidV - 0.28f) / 0.12f, 0.0f, 1.0f) * 16.0f;

                const float score = du * du + dv * dv * 1.6f + penalty;
                if (score < bestScore)
                {
                    bestScore = score;
                    best.triangleIndex = triIndex;
                    best.barycentric = ComputeWrappedUvBarycentric(uva, uvb, uvc, targetU, targetV);
                    best.rootUv = { targetU, targetV };
                }
            }

            return best;
        }


        float EvaluateTriangleArea3D(const ScalpMesh& scalp, uint32_t triangleIndex)
        {
            if (triangleIndex >= scalp.triangles.size())
            {
                return 0.0f;
            }

            const TriangleIndices& tri = scalp.triangles[triangleIndex];
            const Vec3& a = scalp.positions[tri.a];
            const Vec3& b = scalp.positions[tri.b];
            const Vec3& c = scalp.positions[tri.c];
            return 0.5f * Length(Cross(b - a, c - a));
        }

        float EvaluateRootCoverageWeight(float u, float v)
        {
            const float minAllowedV = EvaluateAllowedRootMinV(u);
            const float maxAllowedV = EvaluateAllowedRootMaxV(u);
            if (v < minAllowedV || v > maxAllowedV)
            {
                return 0.0f;
            }

            const float dx = WrapSignedUnitDelta(u - 0.5f);
            const float crownDy = v - 0.075f;
            const float normalizedBand = Clamp(
                (v - minAllowedV) /
                    std::max(maxAllowedV - minAllowedV, 1.0e-5f),
                0.0f,
                1.0f);
            const float bandWindow =
                0.82f +
                0.18f * Smooth01(
                    1.0f -
                    std::fabs(normalizedBand - 0.48f) / 0.52f);

            const float faceOpening = EvaluateFaceOpeningMask(u, v);
            const Vec3 regions = EvaluateProceduralScalpRegionMap(u, v);
            const float crownMask = regions.x;
            const float templeMask = regions.y;
            const float napeMask = regions.z;

            // Explicitly weight the pole and first latitude rings. The former
            // distribution was centred at v=0.20 while minV=0.06 removed the
            // actual crown cap, leaving a stable circular hole.
            const float topCap = std::exp(-(v * v) / 0.0180f);
            const float crown = std::exp(
                -(dx * dx * 2.0f + crownDy * crownDy * 1.5f) /
                0.036f);
            const float crownCoreFill = std::exp(
                -(dx * dx * 2.6f +
                  crownDy * crownDy * 1.8f) /
                0.022f);

            const float partLine = std::exp(-(dx * dx) / 0.0008f) *
                                   std::exp(
                                       -((v - 0.12f) * (v - 0.12f)) /
                                       0.030f);
            const float partPenaltyEnable = Clamp(
                (v - 0.08f) / 0.16f,
                0.0f,
                1.0f);
            const float shoulderL = std::exp(
                -((dx + 0.028f) * (dx + 0.028f)) / 0.0018f) *
                std::exp(
                    -((v - 0.12f) * (v - 0.12f)) /
                    0.045f);
            const float shoulderR = std::exp(
                -((dx - 0.028f) * (dx - 0.028f)) / 0.0018f) *
                std::exp(
                    -((v - 0.12f) * (v - 0.12f)) /
                    0.045f);

            // v grows from the scalp pole toward the neck. The old centre
            // v=0.86 was outside the allowed front-root range and therefore
            // never increased front-hairline density.
            const float frontHairline = std::exp(
                -((v - 0.24f) * (v - 0.24f)) /
                0.018f);
            float frontDu = std::fabs(WrapUnit(u) - 0.25f);
            frontDu = std::min(frontDu, 1.0f - frontDu);
            const float frontSector =
                SmoothDistanceBand(frontDu, 0.14f);

            const float crownBoost =
                1.35f * topCap +
                1.00f * crown +
                0.40f * crownMask +
                0.70f * crownCoreFill;
            const float partLinePenalty =
                0.008f * partLine * partPenaltyEnable;
            const float partShoulderBoost =
                0.36f * (shoulderL + shoulderR);
            const float templeBoost = 0.16f * templeMask;
            const float napeBoost = 0.10f * napeMask;
            const float frontBoost =
                1.10f * frontHairline * frontSector *
                (1.0f - 0.10f * faceOpening);

            float coverage =
                1.0f +
                crownBoost -
                partLinePenalty +
                partShoulderBoost +
                templeBoost +
                napeBoost +
                frontBoost;
            coverage *= bandWindow;
            coverage *= (1.0f - 0.02f * faceOpening);
            return Clamp(coverage, 0.85f, 4.60f);
        }

        std::vector<float> BuildScalpAreaDistributionCdf(const ScalpMesh& scalp)
        {
            std::vector<float> cdf;
            cdf.reserve(scalp.triangles.size());

            float cumulative = 0.0f;
            for (uint32_t triIndex = 0; triIndex < static_cast<uint32_t>(scalp.triangles.size()); ++triIndex)
            {
                const TriangleIndices& tri = scalp.triangles[triIndex];
                const Vec2& uva = scalp.uvs[tri.a];
                const Vec2& uvb = scalp.uvs[tri.b];
                const Vec2& uvc = scalp.uvs[tri.c];

                float wrappedU = uva.x + WrapSignedUnitDelta(uvb.x - uva.x) * (1.0f / 3.0f)
                               + WrapSignedUnitDelta(uvc.x - uva.x) * (1.0f / 3.0f);
                wrappedU = WrapUnit(wrappedU);
                const float centroidV = (uva.y + uvb.y + uvc.y) / 3.0f;
                const float area = EvaluateTriangleArea3D(scalp, triIndex);
                const float rootWeight = EvaluateRootCoverageWeight(wrappedU, centroidV);
                const float triangleWeight = area * rootWeight;
                cumulative += triangleWeight;
                cdf.push_back(cumulative);
            }

            if (!cdf.empty() && cumulative <= 1.0e-8f)
            {
                cdf.clear();
                cumulative = 0.0f;
                for (uint32_t triIndex = 0; triIndex < static_cast<uint32_t>(scalp.triangles.size()); ++triIndex)
                {
                    cumulative += std::max(EvaluateTriangleArea3D(scalp, triIndex), 1.0e-6f);
                    cdf.push_back(cumulative);
                }
            }

            return cdf;
        }

        uint32_t SampleTriangleIndexFromCdf(const std::vector<float>& cdf, float sample01)
        {
            if (cdf.empty())
            {
                return 0u;
            }

            const float total = cdf.back();
            if (total <= 1.0e-8f)
            {
                return static_cast<uint32_t>(std::min<size_t>(cdf.size() - 1u, static_cast<size_t>(sample01 * static_cast<float>(cdf.size()))));
            }

            const float target = Clamp(sample01, 0.0f, 0.999999f) * total;
            const auto it = std::lower_bound(cdf.begin(), cdf.end(), target);
            if (it == cdf.end())
            {
                return static_cast<uint32_t>(cdf.size() - 1u);
            }
            return static_cast<uint32_t>(std::distance(cdf.begin(), it));
        }

        Vec3 JitterBarycentric(const Vec3& barycentric, float amount, uint32_t seed)
        {
            Vec3 out = barycentric;
            out.x += (Hash01(seed + 1u) - 0.5f) * amount;
            out.y += (Hash01(seed + 2u) - 0.5f) * amount;
            out.z = 1.0f - out.x - out.y;
            out.x = std::max(out.x, 0.001f);
            out.y = std::max(out.y, 0.001f);
            out.z = std::max(out.z, 0.001f);
            const float sum = out.x + out.y + out.z;
            if (sum > 1.0e-6f)
            {
                out = out / sum;
            }
            return out;
        }

        RootBinding OffsetBindingWithinTriangle(
            const ScalpMesh& scalp,
            const RootBinding& guideBinding,
            float angle,
            float radius)
        {
            RootBinding followerBinding = guideBinding;

            // Three phase-shifted cosine terms sum to zero, so the offset moves
            // the barycentric point around the guide root without changing the
            // total barycentric weight. This gives every follower its own scalp
            // root instead of stacking all 24 followers on the guide root.
            Vec3 barycentric = guideBinding.barycentric;
            barycentric.x += std::cos(angle) * radius;
            barycentric.y += std::cos(angle + kTau / 3.0f) * radius;
            barycentric.z += std::cos(angle + 2.0f * kTau / 3.0f) * radius;

            barycentric.x = std::max(barycentric.x, 0.002f);
            barycentric.y = std::max(barycentric.y, 0.002f);
            barycentric.z = std::max(barycentric.z, 0.002f);
            const float sum = barycentric.x + barycentric.y + barycentric.z;
            followerBinding.barycentric = (sum > 1.0e-6f)
                ? barycentric / sum
                : guideBinding.barycentric;
            followerBinding.rootUv = EvaluateTriangleUv(scalp, followerBinding);
            followerBinding.rootUv.x = WrapUnit(followerBinding.rootUv.x);
            return followerBinding;
        }

        RootBinding BuildBindingFromTriangleSample(const ScalpMesh& scalp, uint32_t triangleIndex, float sampleA, float sampleB, uint32_t seed = 0u)
        {
            RootBinding binding;
            binding.triangleIndex = std::min<uint32_t>(triangleIndex, static_cast<uint32_t>(std::max<size_t>(scalp.triangles.size(), 1u)) - 1u);

            const float su = std::sqrt(Clamp(sampleA, 0.0f, 1.0f));
            const float b0 = 1.0f - su;
            const float b1 = su * (1.0f - Clamp(sampleB, 0.0f, 1.0f));
            const float b2 = su * Clamp(sampleB, 0.0f, 1.0f);
            binding.barycentric = JitterBarycentric(Vec3(b0, b1, b2), 0.05f, seed);
            binding.rootUv = EvaluateTriangleUv(scalp, binding);
            binding.rootUv.x = WrapUnit(binding.rootUv.x);
            return binding;
        }

        RootBinding SampleAreaDistributedRootBinding(const ScalpMesh& scalp, const std::vector<float>& areaCdf, uint32_t strandIndex)
        {
            const float base = Fract((static_cast<float>(strandIndex) + 0.5f) * 0.754877666f);
            const uint32_t seed = strandIndex * 1664525u + 1013904223u;

            RootBinding fallback = FindClosestBindingForUv(scalp, Fract(base + Hash01(seed + 3u) * 0.17f), 0.32f + (Hash01(seed + 5u) - 0.5f) * 0.10f);
            float bestWeight = EvaluateRootCoverageWeight(fallback.rootUv.x, fallback.rootUv.y);

            for (uint32_t attempt = 0; attempt < 16u; ++attempt)
            {
                const float triSample = Fract(base + static_cast<float>(attempt) * 0.38196601125f + Hash01(seed + attempt * 11u) * 0.05f);
                const float baryA = Hash01(seed + 31u + attempt * 17u);
                const float baryB = Hash01(seed + 67u + attempt * 23u);
                RootBinding candidate = BuildBindingFromTriangleSample(scalp, SampleTriangleIndexFromCdf(areaCdf, triSample), baryA, baryB, seed + 97u + attempt * 13u);
                candidate.rootUv.x = WrapUnit(candidate.rootUv.x);
                const float weight = EvaluateRootCoverageWeight(candidate.rootUv.x, candidate.rootUv.y);
                if (weight > 0.18f)
                {
                    return candidate;
                }
                if (weight > bestWeight)
                {
                    bestWeight = weight;
                    fallback = candidate;
                }
            }

            return fallback;
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
            const float angle = WrapUnit(uv.x) * kTau;
            const float front = std::max(std::sin(angle), 0.0f);
            const float back = std::max(-std::sin(angle), 0.0f);
            const float side = std::fabs(std::cos(angle));
            const float lowerRing = Clamp(
                1.0f - Clamp((root.y - (scalpCenter.y - 0.02f)) / 0.26f, 0.0f, 1.0f),
                0.0f,
                1.0f);
            const float faceOpening = EvaluateFaceOpeningMask(uv.x, uv.y);

            // Orientation is encoded by the silhouette itself: fringe/front guides
            // are shorter, side guides are medium, and back/nape guides are longer.
            const float frontTrim = front * Lerp(0.14f, 0.26f, faceOpening);
            const float backExtension = back * 0.18f;
            const float sideExtension = side * 0.035f;
            const float lowerExtension = parameters.lowerRingLengthBias * lowerRing;
            const float regionalScale = Clamp(
                0.94f + backExtension + sideExtension + lowerExtension - frontTrim,
                0.56f,
                1.26f);
            return baseLength * parameters.overallLength * regionalScale;
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
            const Vec3 rootSurface = EvaluateRootBindingPosition(asset.scalpMesh, binding);
            Vec3 normal;
            Vec3 scalpTangent;
            Vec3 scalpBitangent;
            EvaluateScalpFrameFromBinding(
                asset.scalpMesh,
                binding,
                normal,
                scalpTangent,
                scalpBitangent);
            const Vec2 uv = EvaluateTriangleUv(asset.scalpMesh, binding);
            const Vec3 scalpRegions = EvaluateScalpRegionMap(uv.x, uv.y);
            const float crownMask = scalpRegions.x;
            const float templeMask = scalpRegions.y;
            const float napeMask = scalpRegions.z;
            const float partLineCenter = EvaluatePartLineCenterMask(uv.x, uv.y, parameters.partingOffset);
            const float partShoulder = EvaluatePartShoulderMask(uv.x, uv.y, parameters.partingOffset);
            const float faceOpening = EvaluateFaceOpeningMask(uv.x, uv.y);
            const Vec3 rel = rootSurface - scalpCenter;
            const Vec3 radial = SafeNormalizeXZ(rel);

            Vec3 tangentDown = ProjectOntoPlaneDir(Vec3(0.0f, -1.0f, 0.0f), normal);
            if (Length(tangentDown) <= 1.0e-6f)
            {
                tangentDown = ProjectOntoPlaneDir(radial + Vec3(0.0f, -0.35f, 0.0f), normal);
            }
            if (Length(tangentDown) <= 1.0e-6f)
            {
                tangentDown = Vec3(0.0f, -1.0f, 0.0f);
            }

            const float angle = WrapUnit(uv.x) * kTau;
            const float sideSign = (std::cos(angle) >= 0.0f) ? 1.0f : -1.0f;
            const float frontFactor = std::max(std::sin(angle), 0.0f);
            const float backFactor = std::max(-std::sin(angle), 0.0f);
            const float sideFactor = std::fabs(std::cos(angle));

            // Use a stable side-part field instead of an azimuthal crown whorl.
            // The old circular flow produced a spherical shell and forced many
            // strands into the same vertical curtain. Hair now leaves the part
            // toward opposite sides before gravity takes over.
            const float partCenterU = WrapUnit(0.47f - parameters.partingOffset);
            const float signedPartDistance = WrapSignedUnitDelta(uv.x - partCenterU);
            const float partSideSign = (signedPartDistance >= 0.0f) ? 1.0f : -1.0f;
            Vec3 partFlow = ProjectOntoPlaneDir(
                scalpTangent * partSideSign,
                normal);
            if (Length(partFlow) <= 1.0e-6f)
            {
                partFlow = ProjectOntoPlaneDir(
                    Vec3(partSideSign, -0.08f, 0.0f),
                    normal);
            }
            const float partDistanceMask = Smooth01(
                1.0f - Clamp(std::fabs(signedPartDistance) / 0.24f, 0.0f, 1.0f));
            const float upperScalpMask = Smooth01(
                1.0f - Clamp(uv.y / 0.46f, 0.0f, 1.0f));
            const float partFlowMask = partDistanceMask * upperScalpMask;
            if (Length(partFlow) > 1.0e-6f && partFlowMask > 0.0f)
            {
                const float partFlowBlend = 0.54f * partFlowMask;
                tangentDown = Normalize(
                    tangentDown * (1.0f - partFlowBlend) +
                    partFlow * partFlowBlend);
            }

            const bool isGuideStrand = (flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u;
            (void)isGuideStrand;
            // The formal benchmark uses one coherent length distribution. Randomly
            // inserting short support followers created the visible scalp halo and
            // made quality comparisons depend on authoring noise.
            const float supportMix = 0.0f;
            float fringeDu = std::fabs(WrapUnit(uv.x) - 0.25f);
            fringeDu = std::min(fringeDu, 1.0f - fringeDu);
            const float fringeFactor = frontFactor * Smooth01(1.0f - Clamp(fringeDu / 0.11f, 0.0f, 1.0f));

            Vec3 lateral = ProjectOntoPlaneDir(Vec3(sideSign, 0.0f, 0.0f), normal);
            if (Length(lateral) <= 1.0e-6f)
            {
                lateral = ProjectOntoPlaneDir(Cross(Vec3(0.0f, 1.0f, 0.0f), normal), normal);
            }
            if (Length(lateral) <= 1.0e-6f)
            {
                lateral = Vec3(sideSign, 0.0f, 0.0f);
            }

            Vec3 frontDir = ProjectOntoPlaneDir(Vec3(0.0f, 0.0f, 1.0f), normal);
            if (Length(frontDir) <= 1.0e-6f)
            {
                frontDir = Vec3(0.0f, 0.0f, 1.0f);
            }

            const Vec3 tangentOffset = rootOffset - normal * Dot(rootOffset, normal);
            const float authoredRootLift =
                0.0015f + parameters.rootLift * 0.10f;
            Vec3 root =
                rootSurface +
                normal * authoredRootLift +
                tangentOffset * 0.72f;
            root = ProjectPointOutsideHeadVolume(
                root,
                scalpCenter,
                0.0030f);

            const float baseStrandLength = ComputeReferenceStrandLength(segmentLength);
            const float regionalLength = ComputeGuideLength(root, scalpCenter, uv, baseStrandLength, parameters);
            const float lengthJitter = 0.93f + 0.14f * Hash01(seed + 41u);
            const float lengthScale = Clamp(0.98f + 0.025f * sideFactor + 0.035f * backFactor - 0.020f * fringeFactor + 0.015f * crownMask, 0.94f, 1.07f);
            const float baseStrandLengthMeters = regionalLength * lengthScale * lengthJitter;
            const float supportLengthScale = 0.28f + 0.18f * Hash01(seed + 157u);
            const float strandLength = Lerp(baseStrandLengthMeters, baseStrandLengthMeters * supportLengthScale, supportMix);
            const float rootTravel = strandLength * Lerp(0.12f, 0.07f, supportMix) * (0.86f + 0.12f * sideFactor + 0.10f * backFactor);
            const float sideDrift = (0.0040f + 0.0065f * sideFactor + 0.0035f * backFactor) * (0.92f + 0.12f * Hash01(seed + 67u)) * (1.0f - 0.72f * supportMix);
            const float forwardDrift = 0.012f * fringeFactor * (0.90f + 0.14f * Hash01(seed + 73u)) * (1.0f - 0.70f * supportMix);
            const float curlNoise = parameters.strandNoise * Lerp(0.20f, 0.11f, supportMix);
            const float rootWidthBoost =
                1.0f +
                0.46f * crownMask +
                0.30f * partShoulder +
                0.20f * faceOpening +
                0.22f * supportMix;

            const Vec3 orthoA = Normalize(Cross(normal, lateral));
            const Vec3 orthoB = Normalize(Cross(lateral, orthoA));

            const uint32_t denseSampleCount = std::max<uint32_t>(kLengthResampleMinSamples, std::max<uint32_t>(pointCount * 4u, 2u));
            std::vector<Vec3> denseSamples;
            std::vector<float> denseWidths;
            std::vector<float> cumulativeLengths;
            denseSamples.reserve(denseSampleCount);
            denseWidths.reserve(denseSampleCount);
            cumulativeLengths.reserve(denseSampleCount);

            auto evaluatePointAt = [&](float t, uint32_t noiseIndex)
            {
                const float rootPhase = Clamp(t / 0.22f, 0.0f, 1.0f);
                const float fallPhase = Clamp((t - 0.22f) / 0.78f, 0.0f, 1.0f);

                Vec3 p = root;
                p += tangentDown * (rootTravel * 0.88f * Smooth01(rootPhase));
                const float crownLiftBell =
                    std::sin(kPi * rootPhase) * (1.0f - fallPhase);
                p += normal * (
                    strandLength *
                    (0.010f + 0.016f * crownMask) *
                    crownLiftBell);
                p += Vec3(0.0f, -1.0f, 0.0f) * (
                    strandLength *
                    Lerp(0.72f, 0.66f, supportMix) *
                    Smooth01(fallPhase));
                p += tangentDown * (
                    strandLength *
                    0.095f *
                    (1.0f - fallPhase) *
                    fallPhase *
                    (1.0f - 0.50f * supportMix));
                p += partFlow * (
                    strandLength *
                    0.050f *
                    partFlowMask *
                    Smooth01(fallPhase) *
                    (1.0f - 0.35f * fallPhase));
                p += lateral * (sideDrift * (0.35f + 0.65f * Smooth01(fallPhase)));
                p += frontDir * (forwardDrift * (1.0f - 0.35f * fallPhase));
                const float rootShellBlend =
                    1.0f - Smooth01(Clamp(t / 0.22f, 0.0f, 1.0f));
                p += normal * (0.0020f * rootShellBlend);

                const float silhouetteBell = std::sin(kPi * Clamp(t, 0.0f, 1.0f));
                const float radialVolume =
                    parameters.silhouetteWidth * (0.30f + 0.45f * sideFactor) +
                    parameters.sideVolume * sideFactor * 0.55f +
                    parameters.backVolume * backFactor * 0.70f;
                const float faceVolumeAttenuation = 1.0f - 0.72f * frontFactor;
                p += radial * (radialVolume * silhouetteBell * faceVolumeAttenuation);
                p += lateral * (parameters.silhouetteWidth * 0.28f * frontFactor * silhouetteBell);
                p = p - radial * (parameters.tipInward * t * t * t);

                if (t > 0.35f)
                {
                    const float noise = curlNoise * (0.35f + 0.65f * t);
                    const float noiseA = std::sin((t * 1.7f + Hash01(seed + noiseIndex * 11u)) * kTau);
                    const float noiseB = std::cos((t * 1.3f + Hash01(seed + noiseIndex * 13u)) * kTau);
                    p += orthoA * (noiseA * noise);
                    p += orthoB * (noiseB * noise * 0.5f);
                }

                return ProjectPointOutsideHeadVolume(
                    p,
                    scalpCenter,
                    Lerp(0.0030f, 0.0015f, t));
            };

            for (uint32_t sampleIndex = 0; sampleIndex < denseSampleCount; ++sampleIndex)
            {
                const float t = static_cast<float>(sampleIndex) / static_cast<float>(std::max(denseSampleCount - 1u, 1u));
                denseSamples.push_back(evaluatePointAt(t, sampleIndex + 1u));
                const float widthBlend = 1.0f + (rootWidthBoost - 1.0f) * (1.0f - t) * (1.0f - t);
                denseWidths.push_back(Lerp(material.rootWidth * widthBlend, material.tipWidth, t));
                if (sampleIndex == 0u)
                {
                    cumulativeLengths.push_back(0.0f);
                }
                else
                {
                    const float segmentArc = Length(denseSamples[sampleIndex] - denseSamples[sampleIndex - 1u]);
                    cumulativeLengths.push_back(cumulativeLengths.back() + segmentArc);
                }
            }

            const float totalArcLength = std::max(cumulativeLengths.back(), 1.0e-6f);
            uint32_t denseCursor = 0u;
            for (uint32_t pointIndex = 0; pointIndex < pointCount; ++pointIndex)
            {
                const float pointAlpha = static_cast<float>(pointIndex) / static_cast<float>(std::max(pointCount - 1u, 1u));
                const float targetArcLength = pointAlpha * totalArcLength;
                while ((denseCursor + 1u) < denseSampleCount && cumulativeLengths[denseCursor + 1u] < targetArcLength)
                {
                    ++denseCursor;
                }

                const uint32_t nextCursor = std::min(denseCursor + 1u, denseSampleCount - 1u);
                const float arcA = cumulativeLengths[denseCursor];
                const float arcB = cumulativeLengths[nextCursor];
                const float arcDenom = std::max(arcB - arcA, 1.0e-6f);
                const float localT = Clamp((targetArcLength - arcA) / arcDenom, 0.0f, 1.0f);
                const Vec3 p = Lerp(denseSamples[denseCursor], denseSamples[nextCursor], localT);
                const float width = Lerp(denseWidths[denseCursor], denseWidths[nextCursor], localT);
                asset.restPoints.push_back(p);
                asset.pointWidths.push_back(width);
            }

            StrandRange range;
            range.pointOffset = static_cast<uint32_t>(asset.restPoints.size()) - pointCount;
            range.pointCount = pointCount;
            range.groupId = groupId;
            range.flags = flags;
            asset.strands.push_back(range);
            asset.materials.push_back(material);
            asset.rootBindings.push_back(binding);
            asset.followData.push_back(follow);
        }
    }

    ScalpMesh BuildSampleScalpMesh(uint32_t rows, uint32_t columns, const Vec3& center, float radiusX, float radiusY, float radiusZ)
    {
        ScalpMesh mesh;
        rows = std::max(rows, 10u);
        columns = std::max(columns, 12u);

        mesh.positions.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.normals.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.uvs.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.triangles.reserve(static_cast<size_t>(rows) * columns * 2u);
        mesh.skinWeights.reserve(static_cast<size_t>(rows + 1) * (columns + 1));
        mesh.boneCount = 2;

        for (uint32_t row = 0; row <= rows; ++row)
        {
            const float v = static_cast<float>(row) / static_cast<float>(rows);
            const float phi = v * kPi;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            for (uint32_t col = 0; col <= columns; ++col)
            {
                const float u = static_cast<float>(col) / static_cast<float>(columns);
                const float theta = u * kTau;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);

                const Vec3 unit(cosTheta * sinPhi, cosPhi, sinTheta * sinPhi);
                const Vec3 position = DeformMannequinHeadPoint(unit, center, radiusX, radiusY, radiusZ);

                mesh.positions.push_back(position);
                mesh.normals.push_back(Normalize(position - center));
                mesh.uvs.push_back({ u, v });

                SkinWeight4 weights;
                const float headWeight = Lerp(0.12f, 1.0f, std::pow(v, 0.72f));
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

        RecomputeScalpNormals(mesh, center);
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

            const Vec3 rootSurface = EvaluateRootBindingPosition(asset.scalpMesh, asset.rootBindings[strandIndex]);
            const Vec3 rootNormal = EvaluateTriangleNormal(asset.scalpMesh, asset.rootBindings[strandIndex]);
            constexpr float kVisualRootLiftMeters = 0.0025f;
            const Vec3 reboundRoot = ProjectPointOutsideHeadVolume(
                rootSurface + rootNormal * kVisualRootLiftMeters,
                Vec3(0.0f, 1.60f, 0.0f),
                0.0030f);
            Vec3 originalRoot = asset.restPoints[strand.pointOffset];
            Vec3 delta = reboundRoot - originalRoot;
            for (uint32_t i = 0; i < strand.pointCount; ++i)
            {
                const uint32_t pointIndex = strand.pointOffset + i;
                const float t = static_cast<float>(i) / static_cast<float>(std::max(strand.pointCount - 1u, 1u));
                asset.restPoints[pointIndex] += delta;
                const float shell = Lerp(0.0030f, 0.0015f, t);
                asset.restPoints[pointIndex] = ProjectPointOutsideHeadVolume(
                    asset.restPoints[pointIndex],
                    Vec3(0.0f, 1.60f, 0.0f),
                    shell);
            }
        }
    }

    WigParameters DefaultWigParameters()
    {
        WigParameters parameters;
        parameters.overallLength = 0.96f;
        parameters.partingOffset = 0.0f;
        parameters.partWidth = 0.026f;
        parameters.scalpConform = 1.0f;
        parameters.rootLift = 0.0100f;
        parameters.silhouetteWidth = 0.050f;
        parameters.sideVolume = 0.032f;
        parameters.backVolume = 0.048f;
        parameters.lowerRingLengthBias = 0.14f;
        parameters.strandNoise = 0.0022f;
        parameters.followerSpread = 0.0060f;
        parameters.tipInward = 0.009f;
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
        const std::vector<float> scalpAreaCdf = BuildScalpAreaDistributionCdf(asset.scalpMesh);

        // Keep dedicated coverage bands, but never let them dominate the wig.
        // The old 50% crown + 25% front allocation concentrated roughly 75,000
        // displayed strands into two small scalp regions and produced a flat cap
        // plus a curtain-like front wall. The remainder is now area-distributed.
        const uint32_t crownGuideQuota =
            std::min<uint32_t>(
                guideCount,
                std::max<uint32_t>(320u, (guideCount * 18u) / 100u));
        const uint32_t remainingAfterCrown =
            guideCount > crownGuideQuota
                ? guideCount - crownGuideQuota
                : 0u;
        const uint32_t frontHairlineGuideQuota =
            std::min<uint32_t>(
                remainingAfterCrown,
                std::max<uint32_t>(240u, (guideCount * 14u) / 100u));

        for (uint32_t guideIndex = 0; guideIndex < guideCount; ++guideIndex)
        {
            RootBinding binding;
            if (guideIndex < crownGuideQuota)
            {
                const float crownIndex =
                    static_cast<float>(guideIndex) + 0.5f;
                const float crownCount =
                    static_cast<float>(
                        std::max<uint32_t>(crownGuideQuota, 1u));
                const float u = ::hairgpu::Fract(crownIndex * 0.61803398875f);
                const float radial01 =
                    std::sqrt(crownIndex / crownCount);
                // Spread crown roots through the upper scalp instead of
                // packing half the guides into the top 10% UV band.
                const float v = 0.008f + 0.180f * radial01;
                binding = FindClosestBindingForUv(
                    asset.scalpMesh,
                    u,
                    v);
            }
            else if (
                guideIndex <
                crownGuideQuota + frontHairlineGuideQuota)
            {
                const uint32_t localIndex =
                    guideIndex - crownGuideQuota;
                const float frontIndex =
                    static_cast<float>(localIndex) + 0.5f;
                const float frontCount =
                    static_cast<float>(
                        std::max<uint32_t>(
                            frontHairlineGuideQuota,
                            1u));
                const float u = ::hairgpu::WrapUnit(
                    0.25f +
                    (frontIndex / frontCount - 0.5f) *
                        0.24f);
                const float v =
                    0.19f +
                    0.12f *
                        ::hairgpu::Fract(frontIndex * 0.754877666f);
                binding = FindClosestBindingForUv(
                    asset.scalpMesh,
                    u,
                    v);
            }
            else
            {
                binding = SampleAreaDistributedRootBinding(
                    asset.scalpMesh,
                    scalpAreaCdf,
                    guideIndex);
            }

            StrandMaterial material;
            // Physical-looking authored widths. The previous 1.6 mm root
            // width was an order of magnitude above real hair and became an
            // opaque surface when multiplied by 100,000 strands.
            material.rootWidth = 0.00032f;
            material.tipWidth = 0.000050f;
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
            const float partLineCenterLocal = EvaluatePartLineCenterMask(bindingUv.x, bindingUv.y, parameters.partingOffset);
            const float partShoulderLocal = EvaluatePartShoulderMask(bindingUv.x, bindingUv.y, parameters.partingOffset);
            const float edgeRisk = Clamp(
                templeMaskLocal * 0.55f +
                napeMaskLocal * 0.34f +
                faceOpeningLocal * 0.40f,
                0.0f,
                1.0f);
            const float followerSpreadScale = Clamp(
                (1.0f - 0.08f * templeMaskLocal) *
                (1.0f - 0.06f * napeMaskLocal) *
                (1.0f - 0.02f * faceOpeningLocal) *
                (0.94f +
                 0.04f * crownMaskLocal +
                 0.06f * partShoulderLocal -
                 0.03f * partLineCenterLocal),
                0.78f,
                1.08f);
            const float guideRotation = Hash01(guideIndex * 29u + 3u) * kTau;
            const float goldenAngle = 2.39996323f;

            for (uint32_t followerIndex = 0; followerIndex < followersPerGuide; ++followerIndex)
            {
                const uint32_t seed = guideIndex * 97u + followerIndex * 17u + 13u;
                const float ring01 = std::sqrt((static_cast<float>(followerIndex) + 0.5f) / static_cast<float>(std::max(followersPerGuide, 1u)));
                const bool isSupportHair = false;
                const float ring = parameters.followerSpread * followerSpreadScale * Lerp(0.10f, 0.38f, ring01);
                const float angle = guideRotation + goldenAngle * static_cast<float>(followerIndex) + (Hash01(seed + 9u) - 0.5f) * 0.12f;

                // The previous asset stored the same RootBinding for the guide and
                // all 24 followers. RebuildRootRestPositionsFromScalp therefore
                // collapsed every follower root back onto the guide root, leaving
                // only 4,000 effective scalp roots. Spread the follower bindings
                // deterministically inside the same scalp triangle so all 100,000
                // strands contribute to root coverage without changing strand count.
                const float rootDistributionRadius =
                    Lerp(0.045f, 0.180f, ring01) *
                    Clamp(
                        1.0f +
                        0.08f * crownMaskLocal +
                        0.04f * partShoulderLocal -
                        0.04f * partLineCenterLocal,
                        0.88f,
                        1.12f);
                RootBinding followerBinding = OffsetBindingWithinTriangle(
                    asset.scalpMesh,
                    binding,
                    angle,
                    rootDistributionRadius);
                const float radialJitter = 0.94f + 0.10f * Hash01(seed + 5u);
                const float tangentJitter = (Hash01(seed + 3u) - 0.5f) * 0.10f;
                const float bitangentScale = 0.74f + 0.12f * Hash01(seed + 37u);
                const float verticalBias = (Hash01(seed + 31u) - 0.5f) * 0.02f;

                Vec3 planarOffset =
                    tangent * ((std::cos(angle) + tangentJitter) * ring * radialJitter) +
                    binormal * (std::sin(angle) * ring * bitangentScale);

                const float outwardAmount = Dot(planarOffset, radialDir);
                if (outwardAmount > 0.0f)
                {
                    planarOffset = planarOffset - radialDir * (outwardAmount * (0.72f + 0.22f * edgeRisk));
                    planarOffset += rimDir * ((Hash01(seed + 83u) - 0.5f) * outwardAmount * 0.18f * edgeRisk);
                }

                Vec3 rootOffset = planarOffset + normal * (verticalBias * ring * (1.0f - 0.70f * edgeRisk));
                rootOffset += radialDir * (
                    ring *
                    (-0.06f * crownMaskLocal +
                      0.08f * partShoulderLocal -
                      0.08f * partLineCenterLocal));
                if (isSupportHair)
                {
                    rootOffset += normal * (parameters.followerSpread * 0.16f);
                    rootOffset = rootOffset - radialDir * (ring * 0.04f);
                }
                const float maxOffset =
                    parameters.followerSpread *
                    Lerp(0.54f, 0.88f, 1.0f - edgeRisk) *
                    Clamp(
                        1.0f -
                        0.18f * crownMaskLocal -
                        0.10f * partLineCenterLocal +
                        0.05f * partShoulderLocal,
                        0.68f,
                        1.05f);
                const float rootOffsetLength = Length(rootOffset);
                if (rootOffsetLength > maxOffset && rootOffsetLength > 1.0e-6f)
                {
                    rootOffset = rootOffset * (maxOffset / rootOffsetLength);
                }

                StrandMaterial followerMaterial = material;
                const float widthJitter = 0.88f + 0.16f * Hash01(seed + 71u);
                const float rootCoverageWidthScale = Clamp(
                    0.82f +
                    0.24f * crownMaskLocal +
                    0.12f * partLineCenterLocal +
                    0.10f * partShoulderLocal +
                    0.12f * faceOpeningLocal,
                    0.80f,
                    1.12f);
                followerMaterial.rootWidth *= rootCoverageWidthScale * widthJitter;
                followerMaterial.tipWidth *= 0.34f * widthJitter;

                FollowData followerFollow;
                followerFollow.parentGuide = static_cast<int32_t>(guideIndex);
                followerFollow.followWeight = 0.955f + Hash01(seed + 7u) * 0.025f - edgeRisk * 0.015f;
                followerFollow.clumpWeight = 0.740f + Hash01(seed + 19u) * 0.120f - edgeRisk * 0.030f;
                followerFollow.noiseWeight = 0.080f + Hash01(seed + 23u) * 0.100f;

                AppendStaticWigStrand(
                    asset,
                    followerBinding,
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

        if (!ReorderGeneratedGuideFollowerLayout(
                asset,
                guideCount,
                followersPerGuide))
        {
            return {};
        }

        RebuildRootRestPositionsFromScalp(asset);
        // AppendStaticWigStrand already creates the benchmark silhouette. A second
        // strong normal-directed cap styling pass made roots stand away from the
        // scalp and created the crown halo seen in the Viewer.
        return asset;
    }

    HairAsset BuildSampleGuideHairAsset(uint32_t strandCount, uint32_t particlesPerStrand, float segmentLength)
    {
        return BuildStaticWigHairAsset(
            strandCount,
            0,
            particlesPerStrand,
            segmentLength,
            DefaultWigParameters());
    }

    HairAsset BuildSampleHybridHairAsset(uint32_t guideCount, uint32_t followersPerGuide, uint32_t particlesPerStrand, float segmentLength)
    {
        return BuildStaticWigHairAsset(
            guideCount,
            followersPerGuide,
            particlesPerStrand,
            segmentLength,
            DefaultWigParameters());
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
