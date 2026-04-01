#include "hairgpu/ReducedModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>

#include "hairgpu/AssetIO.h"

namespace hairgpu
{
    namespace
    {
        constexpr std::array<char, 8> kReducedMagic = { 'H', 'R', 'E', 'D', 'U', 'C', 'E', '1' };
        constexpr uint32_t kReducedVersion = 3u;
        constexpr const char* kSequenceManifestName = "sequence_manifest.txt";

        template <typename T>
        bool WriteValue(std::ofstream& out, const T& value)
        {
            out.write(reinterpret_cast<const char*>(&value), sizeof(T));
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadValue(std::ifstream& in, T& value)
        {
            in.read(reinterpret_cast<char*>(&value), sizeof(T));
            return static_cast<bool>(in);
        }

        template <typename T>
        bool WriteVector(std::ofstream& out, const std::vector<T>& values)
        {
            uint64_t count = static_cast<uint64_t>(values.size());
            if (!WriteValue(out, count))
            {
                return false;
            }
            if (!values.empty())
            {
                out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(sizeof(T) * values.size()));
            }
            return static_cast<bool>(out);
        }

        template <typename T>
        bool ReadVector(std::ifstream& in, std::vector<T>& values)
        {
            uint64_t count = 0u;
            if (!ReadValue(in, count))
            {
                return false;
            }
            values.resize(static_cast<size_t>(count));
            if (!values.empty())
            {
                in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(sizeof(T) * values.size()));
            }
            return static_cast<bool>(in);
        }

        std::string LowerExtension(const std::filesystem::path& path)
        {
            std::string value = path.extension().string();
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        const Vec3* FramePoints(const ReducedModelTrainingSequence& sequence, uint32_t frameIndex)
        {
            const uint32_t pointsPerFrame = sequence.PointCountPerFrame();
            if (pointsPerFrame == 0u || frameIndex >= sequence.FrameCount())
            {
                return nullptr;
            }
            return sequence.frames.data() + static_cast<size_t>(frameIndex) * pointsPerFrame;
        }

        std::vector<float> BuildStrandSignature(const ReducedModelTrainingSequence& sequence,
                                                uint32_t strandIndex,
                                                uint32_t sampleCount)
        {
            std::vector<float> signature;
            if (strandIndex >= sequence.referenceAsset.strands.size() || sequence.Empty())
            {
                return signature;
            }

            const StrandRange& strand = sequence.referenceAsset.strands[strandIndex];
            if (strand.pointCount == 0u)
            {
                return signature;
            }

            const uint32_t tipIndex = strand.pointOffset + (strand.pointCount - 1u);
            const uint32_t midIndex = strand.pointOffset + (strand.pointCount / 2u);
            const Vec3 restRoot = sequence.referenceAsset.restPoints[strand.pointOffset];
            const Vec3 restTip = sequence.referenceAsset.restPoints[tipIndex];
            const Vec3 restMid = sequence.referenceAsset.restPoints[midIndex];

            const uint32_t frameCount = sequence.FrameCount();
            sampleCount = std::max<uint32_t>(1u, std::min(sampleCount, frameCount));
            signature.reserve(static_cast<size_t>(sampleCount) * 9u);

            for (uint32_t sampleIndex = 0u; sampleIndex < sampleCount; ++sampleIndex)
            {
                const uint32_t frameIndex = (sampleCount == 1u)
                    ? 0u
                    : std::min<uint32_t>(static_cast<uint32_t>(std::llround(static_cast<double>(sampleIndex) * static_cast<double>(frameCount - 1u) / static_cast<double>(sampleCount - 1u))), frameCount - 1u);
                const Vec3* frame = FramePoints(sequence, frameIndex);
                const Vec3 root = frame[strand.pointOffset];
                const Vec3 tip = frame[tipIndex];
                const Vec3 mid = frame[midIndex];
                const Vec3 tipDelta = tip - restTip;
                const Vec3 midDelta = mid - restMid;
                const Vec3 tangent = Normalize(tip - root);
                const Vec3 restTangent = Normalize(restTip - restRoot);
                const Vec3 tangentDelta = tangent - restTangent;

                signature.push_back(tipDelta.x);
                signature.push_back(tipDelta.y);
                signature.push_back(tipDelta.z);
                signature.push_back(midDelta.x);
                signature.push_back(midDelta.y);
                signature.push_back(midDelta.z);
                signature.push_back(tangentDelta.x);
                signature.push_back(tangentDelta.y);
                signature.push_back(tangentDelta.z);
            }
            return signature;
        }

        float SignatureDistanceSquared(const std::vector<float>& a, const std::vector<float>& b)
        {
            const size_t count = std::min(a.size(), b.size());
            float sum = 0.0f;
            for (size_t i = 0; i < count; ++i)
            {
                const float d = a[i] - b[i];
                sum += d * d;
            }
            return sum;
        }

        Vec3 StrandRootPosition(const HairAsset& asset, uint32_t strandIndex)
        {
            if (strandIndex >= asset.strands.size())
            {
                return {};
            }
            const StrandRange& strand = asset.strands[strandIndex];
            if (strand.pointCount == 0u || strand.pointOffset >= asset.restPoints.size())
            {
                return {};
            }
            return asset.restPoints[strand.pointOffset];
        }

        Vec2 StrandRootUv(const HairAsset& asset, uint32_t strandIndex)
        {
            if (strandIndex < asset.rootBindings.size())
            {
                return asset.rootBindings[strandIndex].rootUv;
            }
            return {};
        }

        Vec3 EvaluateBindingNormal(const HairAsset& asset, uint32_t strandIndex)
        {
            if (strandIndex >= asset.rootBindings.size() || asset.scalpMesh.triangles.empty())
            {
                return {0.0f, 1.0f, 0.0f};
            }
            const RootBinding& binding = asset.rootBindings[strandIndex];
            if (binding.triangleIndex >= asset.scalpMesh.triangles.size())
            {
                return {0.0f, 1.0f, 0.0f};
            }
            const TriangleIndices& tri = asset.scalpMesh.triangles[binding.triangleIndex];
            if (tri.a < asset.scalpMesh.normals.size() && tri.b < asset.scalpMesh.normals.size() && tri.c < asset.scalpMesh.normals.size())
            {
                const Vec3 n = asset.scalpMesh.normals[tri.a] * binding.barycentric.x +
                               asset.scalpMesh.normals[tri.b] * binding.barycentric.y +
                               asset.scalpMesh.normals[tri.c] * binding.barycentric.z;
                const float len = Length(n);
                if (len > 1.0e-6f)
                {
                    return n / len;
                }
            }
            if (tri.a < asset.scalpMesh.positions.size() && tri.b < asset.scalpMesh.positions.size() && tri.c < asset.scalpMesh.positions.size())
            {
                return Normalize(Cross(asset.scalpMesh.positions[tri.b] - asset.scalpMesh.positions[tri.a],
                                       asset.scalpMesh.positions[tri.c] - asset.scalpMesh.positions[tri.a]));
            }
            return {0.0f, 1.0f, 0.0f};
        }

        Vec3 EvaluateBindingPosition(const HairAsset& asset, uint32_t strandIndex)
        {
            if (strandIndex >= asset.rootBindings.size() || asset.scalpMesh.triangles.empty())
            {
                return StrandRootPosition(asset, strandIndex);
            }
            const RootBinding& binding = asset.rootBindings[strandIndex];
            if (binding.triangleIndex >= asset.scalpMesh.triangles.size())
            {
                return StrandRootPosition(asset, strandIndex);
            }
            const TriangleIndices& tri = asset.scalpMesh.triangles[binding.triangleIndex];
            if (tri.a >= asset.scalpMesh.positions.size() || tri.b >= asset.scalpMesh.positions.size() || tri.c >= asset.scalpMesh.positions.size())
            {
                return StrandRootPosition(asset, strandIndex);
            }
            return asset.scalpMesh.positions[tri.a] * binding.barycentric.x +
                   asset.scalpMesh.positions[tri.b] * binding.barycentric.y +
                   asset.scalpMesh.positions[tri.c] * binding.barycentric.z;
        }

        float RootLocalityDistanceSquared(const HairAsset& asset,
                                          uint32_t strandIndex,
                                          uint32_t guideStrand,
                                          const ReducedModelSettings& settings)
        {
            const Vec2 uvA = StrandRootUv(asset, strandIndex);
            const Vec2 uvB = StrandRootUv(asset, guideStrand);
            const Vec2 uvD = { uvA.x - uvB.x, uvA.y - uvB.y };
            const Vec3 posD = StrandRootPosition(asset, strandIndex) - StrandRootPosition(asset, guideStrand);
            return settings.rootUvWeight * Dot(Vec3{uvD.x, uvD.y, 0.0f}, Vec3{uvD.x, uvD.y, 0.0f}) +
                   settings.rootPositionWeight * Dot(posD, posD);
        }

        std::vector<uint32_t> SelectGuideStrands(const ReducedModelTrainingSequence& sequence,
                                                 const ReducedModelSettings& settings,
                                                 const std::vector<std::vector<float>>& signatures)
        {
            std::vector<uint32_t> candidates;
            candidates.reserve(sequence.referenceAsset.strands.size());
            for (uint32_t strandIndex = 0u; strandIndex < sequence.referenceAsset.strands.size(); ++strandIndex)
            {
                const bool isOriginalGuide = strandIndex < sequence.referenceAsset.guideStrandCount ||
                    ((sequence.referenceAsset.strands[strandIndex].flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u);
                if (!settings.restrictGuideCandidatesToOriginalGuides || isOriginalGuide)
                {
                    candidates.push_back(strandIndex);
                }
            }
            if (candidates.empty())
            {
                return {};
            }

            const uint32_t requestedGuideCount = (settings.guideCount > 0u)
                ? std::min<uint32_t>(settings.guideCount, static_cast<uint32_t>(candidates.size()))
                : std::min<uint32_t>(std::max<uint32_t>(1u, sequence.referenceAsset.guideStrandCount > 0u ? sequence.referenceAsset.guideStrandCount : static_cast<uint32_t>(std::sqrt(static_cast<float>(candidates.size())) + 0.5f)), static_cast<uint32_t>(candidates.size()));

            std::vector<uint32_t> selected;
            selected.reserve(requestedGuideCount);
            std::vector<float> minDistances(candidates.size(), std::numeric_limits<float>::max());

            auto candidateBonus = [&](uint32_t strandIndex) -> float {
                const bool isOriginalGuide = strandIndex < sequence.referenceAsset.guideStrandCount ||
                    ((sequence.referenceAsset.strands[strandIndex].flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u);
                return (settings.preferOriginalGuideFlags && isOriginalGuide) ? settings.guidePreference : 0.0f;
            };

            uint32_t firstGuide = candidates.front();
            float bestNorm = -std::numeric_limits<float>::max();
            for (uint32_t strandIndex : candidates)
            {
                const std::vector<float>& signature = signatures[strandIndex];
                float normSq = 0.0f;
                for (float value : signature)
                {
                    normSq += value * value;
                }
                normSq += candidateBonus(strandIndex);
                if (normSq > bestNorm)
                {
                    bestNorm = normSq;
                    firstGuide = strandIndex;
                }
            }
            selected.push_back(firstGuide);

            for (uint32_t selectionIndex = 1u; selectionIndex < requestedGuideCount; ++selectionIndex)
            {
                uint32_t bestCandidate = candidates.front();
                float bestScore = -1.0f;
                for (size_t candidateIndex = 0u; candidateIndex < candidates.size(); ++candidateIndex)
                {
                    const uint32_t strandIndex = candidates[candidateIndex];
                    if (std::find(selected.begin(), selected.end(), strandIndex) != selected.end())
                    {
                        continue;
                    }

                    float nearestDistance = minDistances[candidateIndex];
                    for (uint32_t selectedIndex : selected)
                    {
                        nearestDistance = std::min(nearestDistance, SignatureDistanceSquared(signatures[strandIndex], signatures[selectedIndex]));
                    }
                    minDistances[candidateIndex] = nearestDistance;
                    const float score = nearestDistance + candidateBonus(strandIndex);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestCandidate = strandIndex;
                    }
                }
                if (std::find(selected.begin(), selected.end(), bestCandidate) == selected.end())
                {
                    selected.push_back(bestCandidate);
                }
            }
            std::sort(selected.begin(), selected.end());
            return selected;
        }

        std::vector<uint32_t> FindNearestGuideCandidates(const ReducedModelTrainingSequence& sequence,
                                                         uint32_t strandIndex,
                                                         const std::vector<uint32_t>& selectedGuideStrands,
                                                         const std::vector<std::vector<float>>& signatures,
                                                         const ReducedModelSettings& settings,
                                                         uint32_t maxCount)
        {
            struct DistancePair
            {
                uint32_t strandIndex = 0u;
                float signatureDistanceSq = 0.0f;
                float localityDistanceSq = 0.0f;
            };
            std::vector<DistancePair> distances;
            distances.reserve(selectedGuideStrands.size());
            for (uint32_t guideStrand : selectedGuideStrands)
            {
                distances.push_back({
                    guideStrand,
                    SignatureDistanceSquared(signatures[strandIndex], signatures[guideStrand]),
                    RootLocalityDistanceSquared(sequence.referenceAsset, strandIndex, guideStrand, settings)
                });
            }
            const uint32_t desiredCount = std::max<uint32_t>(1u, std::min<uint32_t>(maxCount, static_cast<uint32_t>(distances.size())));
            const uint32_t localPoolCount = std::max<uint32_t>(desiredCount, std::min<uint32_t>(settings.localGuideCandidateCount, static_cast<uint32_t>(distances.size())));
            if (settings.localGuideCandidateCount > 0u && localPoolCount < static_cast<uint32_t>(distances.size()))
            {
                std::nth_element(distances.begin(), distances.begin() + localPoolCount, distances.end(), [](const DistancePair& a, const DistancePair& b) {
                    if (a.localityDistanceSq == b.localityDistanceSq)
                    {
                        return a.strandIndex < b.strandIndex;
                    }
                    return a.localityDistanceSq < b.localityDistanceSq;
                });
                distances.resize(localPoolCount);
            }
            std::sort(distances.begin(), distances.end(), [](const DistancePair& a, const DistancePair& b) {
                if (a.signatureDistanceSq == b.signatureDistanceSq)
                {
                    if (a.localityDistanceSq == b.localityDistanceSq)
                    {
                        return a.strandIndex < b.strandIndex;
                    }
                    return a.localityDistanceSq < b.localityDistanceSq;
                }
                return a.signatureDistanceSq < b.signatureDistanceSq;
            });
            std::vector<uint32_t> result;
            result.reserve(desiredCount);
            for (uint32_t i = 0u; i < desiredCount; ++i)
            {
                result.push_back(distances[i].strandIndex);
            }
            return result;
        }

        std::vector<float> SolveWeights(const ReducedModelTrainingSequence& sequence,
                                        uint32_t strandIndex,
                                        const std::vector<uint32_t>& guideStrands,
                                        float regularization,
                                        float affineConstraintWeight)
        {
            const StrandRange& strand = sequence.referenceAsset.strands[strandIndex];
            const uint32_t pointCount = strand.pointCount;
            const uint32_t frameCount = sequence.FrameCount();
            const uint32_t k = static_cast<uint32_t>(guideStrands.size());

            std::vector<double> ata(static_cast<size_t>(k) * k, 0.0);
            std::vector<double> atb(k, 0.0);

            for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
            {
                const Vec3* frame = FramePoints(sequence, frameIndex);
                for (uint32_t pointIndex = 0u; pointIndex < pointCount; ++pointIndex)
                {
                    const uint32_t targetPointIndex = strand.pointOffset + pointIndex;
                    const Vec3& target = frame[targetPointIndex];
                    for (int dim = 0; dim < 3; ++dim)
                    {
                        double b = (dim == 0) ? target.x : (dim == 1 ? target.y : target.z);
                        for (uint32_t row = 0u; row < k; ++row)
                        {
                            const StrandRange& guideStrand = sequence.referenceAsset.strands[guideStrands[row]];
                            const Vec3& guidePoint = frame[guideStrand.pointOffset + std::min<uint32_t>(pointIndex, guideStrand.pointCount - 1u)];
                            const double aRow = (dim == 0) ? guidePoint.x : (dim == 1 ? guidePoint.y : guidePoint.z);
                            atb[row] += aRow * b;
                            for (uint32_t col = 0u; col < k; ++col)
                            {
                                const StrandRange& guideStrandCol = sequence.referenceAsset.strands[guideStrands[col]];
                                const Vec3& guidePointCol = frame[guideStrandCol.pointOffset + std::min<uint32_t>(pointIndex, guideStrandCol.pointCount - 1u)];
                                const double aCol = (dim == 0) ? guidePointCol.x : (dim == 1 ? guidePointCol.y : guidePointCol.z);
                                ata[static_cast<size_t>(row) * k + col] += aRow * aCol;
                            }
                        }
                    }
                }
            }

            for (uint32_t row = 0u; row < k; ++row)
            {
                ata[static_cast<size_t>(row) * k + row] += static_cast<double>(regularization);
                atb[row] += static_cast<double>(affineConstraintWeight);
                for (uint32_t col = 0u; col < k; ++col)
                {
                    ata[static_cast<size_t>(row) * k + col] += static_cast<double>(affineConstraintWeight);
                }
            }

            std::vector<double> augmented(static_cast<size_t>(k) * (k + 1u), 0.0);
            for (uint32_t row = 0u; row < k; ++row)
            {
                for (uint32_t col = 0u; col < k; ++col)
                {
                    augmented[static_cast<size_t>(row) * (k + 1u) + col] = ata[static_cast<size_t>(row) * k + col];
                }
                augmented[static_cast<size_t>(row) * (k + 1u) + k] = atb[row];
            }

            for (uint32_t pivot = 0u; pivot < k; ++pivot)
            {
                uint32_t bestRow = pivot;
                double bestValue = std::fabs(augmented[static_cast<size_t>(pivot) * (k + 1u) + pivot]);
                for (uint32_t row = pivot + 1u; row < k; ++row)
                {
                    const double value = std::fabs(augmented[static_cast<size_t>(row) * (k + 1u) + pivot]);
                    if (value > bestValue)
                    {
                        bestValue = value;
                        bestRow = row;
                    }
                }
                if (bestRow != pivot)
                {
                    for (uint32_t col = pivot; col <= k; ++col)
                    {
                        std::swap(augmented[static_cast<size_t>(pivot) * (k + 1u) + col], augmented[static_cast<size_t>(bestRow) * (k + 1u) + col]);
                    }
                }
                const double diagonal = augmented[static_cast<size_t>(pivot) * (k + 1u) + pivot];
                if (std::fabs(diagonal) < 1.0e-12)
                {
                    continue;
                }
                for (uint32_t col = pivot; col <= k; ++col)
                {
                    augmented[static_cast<size_t>(pivot) * (k + 1u) + col] /= diagonal;
                }
                for (uint32_t row = 0u; row < k; ++row)
                {
                    if (row == pivot)
                    {
                        continue;
                    }
                    const double factor = augmented[static_cast<size_t>(row) * (k + 1u) + pivot];
                    if (std::fabs(factor) < 1.0e-12)
                    {
                        continue;
                    }
                    for (uint32_t col = pivot; col <= k; ++col)
                    {
                        augmented[static_cast<size_t>(row) * (k + 1u) + col] -= factor * augmented[static_cast<size_t>(pivot) * (k + 1u) + col];
                    }
                }
            }

            std::vector<float> weights(k, 0.0f);
            float sum = 0.0f;
            for (uint32_t row = 0u; row < k; ++row)
            {
                const double raw = augmented[static_cast<size_t>(row) * (k + 1u) + k];
                weights[row] = static_cast<float>(std::max(0.0, raw));
                sum += weights[row];
            }
            if (sum <= 1.0e-8f)
            {
                weights.assign(k, 0.0f);
                weights[0] = 1.0f;
                return weights;
            }
            for (float& weight : weights)
            {
                weight /= sum;
            }
            return weights;
        }

        void ComputeBiases(const ReducedModelTrainingSequence& sequence,
                           uint32_t strandIndex,
                           const std::vector<uint32_t>& guideStrands,
                           const std::vector<float>& weights,
                           bool useGuideDisplacements,
                           std::vector<Vec3>& outBiases)
        {
            const StrandRange& strand = sequence.referenceAsset.strands[strandIndex];
            outBiases.assign(strand.pointCount, {});
            const uint32_t frameCount = sequence.FrameCount();
            if (frameCount == 0u)
            {
                return;
            }
            for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
            {
                const Vec3* frame = FramePoints(sequence, frameIndex);
                for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
                {
                    Vec3 reconstructed = useGuideDisplacements
                        ? sequence.referenceAsset.restPoints[strand.pointOffset + pointIndex]
                        : Vec3{};
                    for (size_t influenceIndex = 0u; influenceIndex < guideStrands.size(); ++influenceIndex)
                    {
                        const StrandRange& guideStrand = sequence.referenceAsset.strands[guideStrands[influenceIndex]];
                        const uint32_t clampedPoint = std::min<uint32_t>(pointIndex, guideStrand.pointCount - 1u);
                        const Vec3& guidePoint = frame[guideStrand.pointOffset + clampedPoint];
                        if (useGuideDisplacements)
                        {
                            const Vec3& guideRest = sequence.referenceAsset.restPoints[guideStrand.pointOffset + clampedPoint];
                            reconstructed += (guidePoint - guideRest) * weights[influenceIndex];
                        }
                        else
                        {
                            reconstructed += guidePoint * weights[influenceIndex];
                        }
                    }
                    const Vec3& target = frame[strand.pointOffset + pointIndex];
                    outBiases[pointIndex] += (target - reconstructed);
                }
            }
            const float invFrameCount = 1.0f / static_cast<float>(frameCount);
            for (Vec3& bias : outBiases)
            {
                bias = bias * invFrameCount;
            }
        }

        ReducedModelTrainingSequence BuildSubsequenceFromFrameIndices(const ReducedModelTrainingSequence& sequence,
                                                                      const std::vector<uint32_t>& frameIndices)
        {
            ReducedModelTrainingSequence result = {};
            result.referenceAsset = sequence.referenceAsset;
            result.frameRate = sequence.frameRate;
            result.frames.reserve(static_cast<size_t>(frameIndices.size()) * sequence.PointCountPerFrame());
            for (uint32_t frameIndex : frameIndices)
            {
                const Vec3* frame = FramePoints(sequence, frameIndex);
                if (frame != nullptr)
                {
                    result.frames.insert(result.frames.end(), frame, frame + sequence.PointCountPerFrame());
                }
            }
            return result;
        }

        void SplitFrameIndices(const ReducedModelTrainingSequence& sequence,
                               uint32_t validationStride,
                               std::vector<uint32_t>& outTrainingFrames,
                               std::vector<uint32_t>& outValidationFrames)
        {
            outTrainingFrames.clear();
            outValidationFrames.clear();
            const uint32_t frameCount = sequence.FrameCount();
            if (validationStride < 2u || frameCount < 4u)
            {
                for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
                {
                    outTrainingFrames.push_back(frameIndex);
                }
                return;
            }
            for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
            {
                if ((frameIndex % validationStride) == (validationStride - 1u))
                {
                    outValidationFrames.push_back(frameIndex);
                }
                else
                {
                    outTrainingFrames.push_back(frameIndex);
                }
            }
            if (outTrainingFrames.empty())
            {
                outTrainingFrames = outValidationFrames;
                outValidationFrames.clear();
            }
        }

        void ApplyReducedRuntimePostConstraints(const ReducedModel& model,
                                                uint32_t strandIndex,
                                                const StrandRange& strand,
                                                std::vector<Vec3>& points)
        {
            if (strand.pointCount == 0u)
            {
                return;
            }
            const uint32_t rootIndex = strand.pointOffset;
            points[rootIndex] = Lerp(points[rootIndex], model.referenceAsset.restPoints[rootIndex], Clamp(model.settings.rootLockBlend, 0.0f, 1.0f));

            if (model.settings.enableScalpCollision && strandIndex < model.referenceAsset.rootBindings.size() && !model.referenceAsset.scalpMesh.triangles.empty())
            {
                const Vec3 scalpRoot = EvaluateBindingPosition(model.referenceAsset, strandIndex);
                const Vec3 scalpNormal = EvaluateBindingNormal(model.referenceAsset, strandIndex);
                const uint32_t lockedCount = std::min<uint32_t>(strand.pointCount, std::max<uint32_t>(1u, model.settings.scalpCollisionLockedPoints));
                const float blend = Clamp(model.settings.scalpCollisionBlend, 0.0f, 1.0f);
                const float clearance = std::max(0.0f, model.settings.scalpCollisionOffset);
                for (uint32_t pointIndex = 0u; pointIndex < lockedCount; ++pointIndex)
                {
                    Vec3& point = points[strand.pointOffset + pointIndex];
                    const Vec3 planeOrigin = scalpRoot + scalpNormal * clearance;
                    const float signedDistance = Dot(point - planeOrigin, scalpNormal);
                    if (signedDistance < 0.0f)
                    {
                        point = Lerp(point, point - scalpNormal * signedDistance, blend);
                    }
                }
            }

            if (model.settings.enableFloorCollision)
            {
                for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
                {
                    Vec3& p = points[strand.pointOffset + pointIndex];
                    p.y = std::max(p.y, model.settings.floorY);
                }
            }
        }

        std::vector<float> ComputeRestSegmentLengths(const HairAsset& asset, const StrandRange& strand)
        {
            std::vector<float> lengths;
            if (strand.pointCount < 2u)
            {
                return lengths;
            }
            lengths.reserve(strand.pointCount - 1u);
            for (uint32_t pointIndex = 0u; pointIndex + 1u < strand.pointCount; ++pointIndex)
            {
                const Vec3& a = asset.restPoints[strand.pointOffset + pointIndex];
                const Vec3& b = asset.restPoints[strand.pointOffset + pointIndex + 1u];
                lengths.push_back(Length(b - a));
            }
            return lengths;
        }

        void ApplyLengthProjection(const HairAsset& referenceAsset,
                                   const StrandRange& strand,
                                   float blend,
                                   std::vector<Vec3>& points)
        {
            if (strand.pointCount < 2u || blend <= 0.0f)
            {
                return;
            }
            const std::vector<float> restLengths = ComputeRestSegmentLengths(referenceAsset, strand);
            for (uint32_t pointIndex = 1u; pointIndex < strand.pointCount; ++pointIndex)
            {
                const uint32_t localPrev = pointIndex - 1u;
                const uint32_t globalPrev = strand.pointOffset + localPrev;
                const uint32_t globalCurr = strand.pointOffset + pointIndex;
                const Vec3 prev = points[globalPrev];
                Vec3 dir = points[globalCurr] - prev;
                const float len = Length(dir);
                if (len > 1.0e-6f)
                {
                    dir = dir / len;
                }
                else
                {
                    const Vec3& restPrev = referenceAsset.restPoints[globalPrev];
                    const Vec3& restCurr = referenceAsset.restPoints[globalCurr];
                    dir = Normalize(restCurr - restPrev);
                    if (Length(dir) < 1.0e-6f)
                    {
                        dir = Vec3(0.0f, -1.0f, 0.0f);
                    }
                }
                const Vec3 projected = prev + dir * restLengths[localPrev];
                points[globalCurr] = Lerp(points[globalCurr], projected, Clamp(blend, 0.0f, 1.0f));
            }
        }

        void CopySelectedGuideFrame(const ReducedModel& model,
                                    const Vec3* framePoints,
                                    std::vector<Vec3>& outGuideFramePoints)
        {
            outGuideFramePoints.clear();
            outGuideFramePoints.reserve(model.selectedGuideStrandIndices.size() * model.referenceAsset.particlesPerStrand);
            for (uint32_t guideStrandIndex : model.selectedGuideStrandIndices)
            {
                const StrandRange& strand = model.referenceAsset.strands[guideStrandIndex];
                for (uint32_t pointIndex = 0u; pointIndex < model.referenceAsset.particlesPerStrand; ++pointIndex)
                {
                    const uint32_t clampedPoint = std::min<uint32_t>(pointIndex, strand.pointCount - 1u);
                    outGuideFramePoints.push_back(framePoints[strand.pointOffset + clampedPoint]);
                }
            }
        }

        bool ValidateFrameTopology(const HairAsset& referenceAsset, const HairAsset& frameAsset, std::string& outError)
        {
            if (referenceAsset.strands.size() != frameAsset.strands.size())
            {
                outError = "Training frame strand count does not match the reference asset.";
                return false;
            }
            if (referenceAsset.restPoints.size() != frameAsset.restPoints.size())
            {
                outError = "Training frame point count does not match the reference asset.";
                return false;
            }
            if (referenceAsset.particlesPerStrand != frameAsset.particlesPerStrand)
            {
                outError = "Training frame particles-per-strand does not match the reference asset.";
                return false;
            }
            return true;
        }
    }

    bool SaveReducedModelToFile(const std::filesystem::path& filePath, const ReducedModel& model, std::string& outError)
    {
        std::ofstream out(filePath, std::ios::binary);
        if (!out)
        {
            outError = "Failed to open reduced model output file.";
            return false;
        }

        out.write(kReducedMagic.data(), static_cast<std::streamsize>(kReducedMagic.size()));
        if (!WriteValue(out, kReducedVersion) ||
            !WriteValue(out, model.settings) ||
            !WriteValue(out, model.trainingRmsError) ||
            !WriteValue(out, model.trainingMaxError) ||
            !WriteValue(out, model.trainingTipRmsError) ||
            !WriteValue(out, model.trainingRootRmsError) ||
            !WriteValue(out, model.validationRmsError) ||
            !WriteValue(out, model.validationMaxError) ||
            !WriteValue(out, model.validationTipRmsError) ||
            !WriteValue(out, model.validationRootRmsError) ||
            !WriteValue(out, model.trainingFrameCount) ||
            !WriteValue(out, model.validationFrameCount))
        {
            outError = "Failed to write reduced model header.";
            return false;
        }

        std::string assetError;
        const std::filesystem::path tempAssetPath = filePath.string() + ".asset.tmp";
        if (!SaveHairAssetToHairBinary(tempAssetPath, model.referenceAsset, assetError))
        {
            outError = "Failed to serialize reduced model reference asset: " + assetError;
            return false;
        }
        std::ifstream tempAsset(tempAssetPath, std::ios::binary);
        if (!tempAsset)
        {
            outError = "Failed to reopen temporary reference asset file.";
            std::error_code ec;
            std::filesystem::remove(tempAssetPath, ec);
            return false;
        }
        std::vector<char> assetBytes((std::istreambuf_iterator<char>(tempAsset)), std::istreambuf_iterator<char>());
        std::error_code ec;
        std::filesystem::remove(tempAssetPath, ec);

        if (!WriteVector(out, assetBytes) ||
            !WriteVector(out, model.selectedGuideStrandIndices) ||
            !WriteVector(out, model.strandRecords) ||
            !WriteVector(out, model.influences) ||
            !WriteVector(out, model.pointBiases))
        {
            outError = "Failed to write reduced model body.";
            return false;
        }
        return true;
    }

    bool LoadReducedModelFromFile(const std::filesystem::path& filePath, ReducedModel& outModel, std::string& outError)
    {
        std::ifstream in(filePath, std::ios::binary);
        if (!in)
        {
            outError = "Failed to open reduced model file.";
            return false;
        }

        std::array<char, 8> magic = {};
        in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!in || magic != kReducedMagic)
        {
            outError = "Invalid reduced model magic.";
            return false;
        }

        ReducedModel model = {};
        uint32_t version = 0u;
        if (!ReadValue(in, version) || version != kReducedVersion ||
            !ReadValue(in, model.settings) ||
            !ReadValue(in, model.trainingRmsError) ||
            !ReadValue(in, model.trainingMaxError) ||
            !ReadValue(in, model.trainingTipRmsError) ||
            !ReadValue(in, model.trainingRootRmsError) ||
            !ReadValue(in, model.validationRmsError) ||
            !ReadValue(in, model.validationMaxError) ||
            !ReadValue(in, model.validationTipRmsError) ||
            !ReadValue(in, model.validationRootRmsError) ||
            !ReadValue(in, model.trainingFrameCount) ||
            !ReadValue(in, model.validationFrameCount))
        {
            outError = "Unsupported or truncated reduced model header.";
            return false;
        }

        std::vector<char> assetBytes;
        if (!ReadVector(in, assetBytes) ||
            !ReadVector(in, model.selectedGuideStrandIndices) ||
            !ReadVector(in, model.strandRecords) ||
            !ReadVector(in, model.influences) ||
            !ReadVector(in, model.pointBiases))
        {
            outError = "Reduced model body is truncated.";
            return false;
        }

        const std::filesystem::path tempAssetPath = filePath.string() + ".asset.load.tmp";
        {
            std::ofstream tempOut(tempAssetPath, std::ios::binary);
            if (!tempOut)
            {
                outError = "Failed to unpack reduced model reference asset.";
                return false;
            }
            tempOut.write(assetBytes.data(), static_cast<std::streamsize>(assetBytes.size()));
        }
        std::string assetError;
        if (!LoadHairAssetFromHairBinary(tempAssetPath, model.referenceAsset, assetError))
        {
            outError = "Failed to read reduced model reference asset: " + assetError;
            std::error_code ec;
            std::filesystem::remove(tempAssetPath, ec);
            return false;
        }
        std::error_code ec;
        std::filesystem::remove(tempAssetPath, ec);

        outModel = std::move(model);
        return true;
    }

    std::string BuildReducedModelSummary(const ReducedModel& model)
    {
        std::ostringstream out;
        out << "reducedModel guides=" << model.selectedGuideStrandIndices.size()
            << " strands=" << model.referenceAsset.strands.size()
            << " pps=" << model.referenceAsset.particlesPerStrand
            << " maxInfluences=" << model.settings.maxInfluencesPerFollower
            << " localPool=" << model.settings.localGuideCandidateCount
            << " displacementSpace=" << (model.settings.useGuideDisplacements ? 1 : 0)
            << " scalpCollision=" << (model.settings.enableScalpCollision ? 1 : 0)
            << " scalpLockPts=" << model.settings.scalpCollisionLockedPoints
            << " trainFrames=" << model.trainingFrameCount
            << " valFrames=" << model.validationFrameCount
            << " trainRms=" << model.trainingRmsError
            << " trainMax=" << model.trainingMaxError
            << " valRms=" << model.validationRmsError
            << " valMax=" << model.validationMaxError
            << " trainTipRms=" << model.trainingTipRmsError
            << " valTipRms=" << model.validationTipRmsError
            << " trainRootRms=" << model.trainingRootRmsError
            << " valRootRms=" << model.validationRootRmsError;
        return out.str();
    }

    bool SaveReducedTrainingSequenceManifest(const std::filesystem::path& directoryPath,
                                            const ReducedModelTrainingSequence& sequence,
                                            std::string& outError)
    {
        std::ofstream out(directoryPath / kSequenceManifestName);
        if (!out)
        {
            outError = "Failed to write training sequence manifest.";
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "frame_rate=" << sequence.frameRate << "\n";
        out << "frame_count=" << sequence.FrameCount() << "\n";
        out << "point_count=" << sequence.PointCountPerFrame() << "\n";
        out << "strand_count=" << sequence.referenceAsset.strands.size() << "\n";
        return true;
    }

    bool LoadReducedTrainingSequenceFromDirectory(const std::filesystem::path& directoryPath,
                                                  ReducedModelTrainingSequence& outSequence,
                                                  std::string& outError)
    {
        if (!std::filesystem::exists(directoryPath) || !std::filesystem::is_directory(directoryPath))
        {
            outError = "Training sequence directory does not exist.";
            return false;
        }

        std::vector<std::filesystem::path> frameFiles;
        for (const auto& entry : std::filesystem::directory_iterator(directoryPath))
        {
            if (!entry.is_regular_file())
            {
                continue;
            }
            const std::string ext = LowerExtension(entry.path());
            if (ext == ".hairbin" || ext == ".hair")
            {
                frameFiles.push_back(entry.path());
            }
        }
        std::sort(frameFiles.begin(), frameFiles.end());
        if (frameFiles.empty())
        {
            outError = "No .hairbin/.hair frames were found in the training sequence directory.";
            return false;
        }

        ReducedModelTrainingSequence sequence = {};
        const std::filesystem::path manifestPath = directoryPath / kSequenceManifestName;
        if (std::filesystem::exists(manifestPath))
        {
            std::ifstream manifest(manifestPath);
            std::string line;
            while (std::getline(manifest, line))
            {
                const size_t eq = line.find('=');
                if (eq == std::string::npos)
                {
                    continue;
                }
                const std::string key = line.substr(0u, eq);
                const std::string value = line.substr(eq + 1u);
                if (key == "frame_rate")
                {
                    try { sequence.frameRate = std::stof(value); } catch (...) {}
                }
            }
        }

        std::string loadError;
        for (size_t fileIndex = 0u; fileIndex < frameFiles.size(); ++fileIndex)
        {
            HairAsset frameAsset;
            if (!LoadHairAssetFromHairBinary(frameFiles[fileIndex], frameAsset, loadError))
            {
                outError = "Failed to load training frame '" + frameFiles[fileIndex].string() + "': " + loadError;
                return false;
            }
            if (fileIndex == 0u)
            {
                sequence.referenceAsset = frameAsset;
            }
            else if (!ValidateFrameTopology(sequence.referenceAsset, frameAsset, outError))
            {
                outError = "Training sequence topology mismatch in '" + frameFiles[fileIndex].string() + "': " + outError;
                return false;
            }
            sequence.frames.insert(sequence.frames.end(), frameAsset.restPoints.begin(), frameAsset.restPoints.end());
        }

        outSequence = std::move(sequence);
        return true;
    }

    bool SynthesizeReducedTrainingSequence(const HairAsset& asset,
                                          const std::filesystem::path& outputDirectory,
                                          uint32_t frameCount,
                                          float frameRate,
                                          float swayAmplitude,
                                          std::string& outError)
    {
        if (asset.strands.empty() || asset.restPoints.empty() || asset.particlesPerStrand == 0u)
        {
            outError = "Input asset is empty.";
            return false;
        }
        frameCount = std::max<uint32_t>(frameCount, 2u);
        frameRate = std::max(frameRate, 1.0f);
        swayAmplitude = std::max(swayAmplitude, 0.0f);

        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create training sequence output directory.";
            return false;
        }

        ReducedModelTrainingSequence sequence = {};
        sequence.referenceAsset = asset;
        sequence.frameRate = frameRate;
        sequence.frames.reserve(static_cast<size_t>(frameCount) * asset.restPoints.size());

        for (uint32_t frameIndex = 0u; frameIndex < frameCount; ++frameIndex)
        {
            HairAsset frameAsset = asset;
            const float timeSeconds = static_cast<float>(frameIndex) / frameRate;
            for (uint32_t strandIndex = 0u; strandIndex < frameAsset.strands.size(); ++strandIndex)
            {
                const StrandRange& strand = frameAsset.strands[strandIndex];
                if (strand.pointCount < 2u)
                {
                    continue;
                }
                const Vec3 root = asset.restPoints[strand.pointOffset];
                const float phase = static_cast<float>(strandIndex) * 0.37f + root.x * 3.1f - root.z * 2.3f;
                const bool isGuide = strandIndex < asset.guideStrandCount || ((strand.flags & static_cast<uint32_t>(StrandFlags::Guide)) != 0u);
                const float stiffness = isGuide ? 1.0f : 0.82f;
                for (uint32_t pointIndex = 1u; pointIndex < strand.pointCount; ++pointIndex)
                {
                    const float t = static_cast<float>(pointIndex) / static_cast<float>(strand.pointCount - 1u);
                    const float envelope = std::pow(t, 1.18f);
                    const float swayX = std::sin(timeSeconds * 2.2f + phase) * swayAmplitude * envelope * stiffness;
                    const float swayZ = std::cos(timeSeconds * 1.5f + phase * 0.7f) * swayAmplitude * 0.65f * envelope * stiffness;
                    const float liftY = std::max(0.0f, std::sin(timeSeconds * 0.9f + phase * 0.35f)) * swayAmplitude * 0.18f * envelope;
                    const float curl = std::sin(timeSeconds * 3.4f + t * 6.5f + phase) * swayAmplitude * 0.12f * envelope;
                    Vec3 delta = { swayX + curl * root.z, liftY, swayZ - curl * root.x };
                    if (!isGuide)
                    {
                        delta = delta * 0.96f + Vec3(0.004f * std::sin(phase + t * 8.0f), 0.0f, 0.004f * std::cos(phase * 0.8f + t * 5.0f));
                    }
                    frameAsset.restPoints[strand.pointOffset + pointIndex] += delta;
                }
            }

            const std::ostringstream fileNameBuilder = [&]() {
                std::ostringstream ss;
                ss << "frame_" << std::setw(4) << std::setfill('0') << frameIndex << ".hairbin";
                return ss;
            }();
            const std::filesystem::path framePath = outputDirectory / fileNameBuilder.str();
            std::string saveError;
            if (!SaveHairAssetToHairBinary(framePath, frameAsset, saveError))
            {
                outError = "Failed to save synthesized frame: " + saveError;
                return false;
            }
            sequence.frames.insert(sequence.frames.end(), frameAsset.restPoints.begin(), frameAsset.restPoints.end());
        }

        return SaveReducedTrainingSequenceManifest(outputDirectory, sequence, outError);
    }

    bool TrainReducedModel(const ReducedModelTrainingSequence& sequence,
                           const ReducedModelSettings& settings,
                           ReducedModel& outModel,
                           std::string& outError)
    {
        if (sequence.Empty())
        {
            outError = "Training sequence is empty.";
            return false;
        }

        std::vector<uint32_t> trainingFrames;
        std::vector<uint32_t> validationFrames;
        SplitFrameIndices(sequence, settings.validationStride, trainingFrames, validationFrames);
        const ReducedModelTrainingSequence fittingSequence = BuildSubsequenceFromFrameIndices(sequence, trainingFrames);
        const ReducedModelTrainingSequence validationSequence = BuildSubsequenceFromFrameIndices(sequence, validationFrames);
        const ReducedModelTrainingSequence& fitting = fittingSequence.Empty() ? sequence : fittingSequence;

        const uint32_t strandCount = static_cast<uint32_t>(sequence.referenceAsset.strands.size());
        std::vector<std::vector<float>> signatures(strandCount);
        for (uint32_t strandIndex = 0u; strandIndex < strandCount; ++strandIndex)
        {
            signatures[strandIndex] = BuildStrandSignature(fitting, strandIndex, settings.signatureFrameSamples);
        }

        const std::vector<uint32_t> selectedGuides = SelectGuideStrands(fitting, settings, signatures);
        if (selectedGuides.empty())
        {
            outError = "Failed to select any guide strands for the reduced model.";
            return false;
        }

        ReducedModel model = {};
        model.referenceAsset = sequence.referenceAsset;
        model.settings = settings;
        model.selectedGuideStrandIndices = selectedGuides;
        model.strandRecords.resize(strandCount);
        model.trainingFrameCount = fitting.FrameCount();
        model.validationFrameCount = validationSequence.FrameCount();

        std::vector<uint32_t> strandToGuideSlot(strandCount, UINT32_MAX);
        for (uint32_t guideSlot = 0u; guideSlot < selectedGuides.size(); ++guideSlot)
        {
            strandToGuideSlot[selectedGuides[guideSlot]] = guideSlot;
        }

        for (uint32_t strandIndex = 0u; strandIndex < strandCount; ++strandIndex)
        {
            const StrandRange& strand = sequence.referenceAsset.strands[strandIndex];
            ReducedModelStrandRecord record = {};
            record.sourceStrandIndex = strandIndex;
            record.pointCount = strand.pointCount;
            record.biasOffset = static_cast<uint32_t>(model.pointBiases.size());
            record.influenceOffset = static_cast<uint32_t>(model.influences.size());
            record.guideSlot = strandToGuideSlot[strandIndex];
            record.isGuide = record.guideSlot != UINT32_MAX;

            if (record.isGuide)
            {
                record.influenceCount = 1u;
                model.influences.push_back({ record.guideSlot, 1.0f });
                for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
                {
                    model.pointBiases.push_back({});
                }
            }
            else
            {
                const std::vector<uint32_t> guideCandidates = FindNearestGuideCandidates(
                    fitting,
                    strandIndex,
                    selectedGuides,
                    signatures,
                    settings,
                    std::max<uint32_t>(1u, settings.maxInfluencesPerFollower));
                const std::vector<float> weights = SolveWeights(fitting,
                                                                strandIndex,
                                                                guideCandidates,
                                                                std::max(settings.regularization, 1.0e-8f),
                                                                std::max(settings.affineConstraintWeight, 0.0f));
                std::vector<Vec3> biases;
                ComputeBiases(fitting, strandIndex, guideCandidates, weights, settings.useGuideDisplacements, biases);
                record.influenceCount = static_cast<uint32_t>(guideCandidates.size());
                for (size_t influenceIndex = 0u; influenceIndex < guideCandidates.size(); ++influenceIndex)
                {
                    model.influences.push_back({ strandToGuideSlot[guideCandidates[influenceIndex]], weights[influenceIndex] });
                }
                model.pointBiases.insert(model.pointBiases.end(), biases.begin(), biases.end());
            }
            model.strandRecords[strandIndex] = record;
        }

        ReducedModelEvaluationReport trainReport = {};
        if (!EvaluateReducedModel(model, fitting, trainReport, outError))
        {
            return false;
        }
        model.trainingRmsError = trainReport.rmsPositionError;
        model.trainingMaxError = trainReport.maxPositionError;
        model.trainingTipRmsError = trainReport.rmsTipError;
        model.trainingRootRmsError = trainReport.rmsRootError;

        if (!validationSequence.Empty())
        {
            ReducedModelEvaluationReport validationReport = {};
            if (!EvaluateReducedModel(model, validationSequence, validationReport, outError))
            {
                return false;
            }
            model.validationRmsError = validationReport.rmsPositionError;
            model.validationMaxError = validationReport.maxPositionError;
            model.validationTipRmsError = validationReport.rmsTipError;
            model.validationRootRmsError = validationReport.rmsRootError;
        }
        else
        {
            model.validationRmsError = model.trainingRmsError;
            model.validationMaxError = model.trainingMaxError;
            model.validationTipRmsError = model.trainingTipRmsError;
            model.validationRootRmsError = model.trainingRootRmsError;
        }

        outModel = std::move(model);
        return true;
    }

bool AutoTuneReducedModel(const ReducedModelTrainingSequence& sequence,
                              uint32_t minGuideCount,
                              uint32_t maxGuideCount,
                              uint32_t guideStep,
                              const ReducedModelSettings& baseSettings,
                              ReducedModel& outBestModel,
                              std::string& outSummary,
                              std::string& outError)
    {
        if (sequence.Empty())
        {
            outError = "Training sequence is empty.";
            return false;
        }
        guideStep = std::max<uint32_t>(1u, guideStep);
        minGuideCount = std::max<uint32_t>(1u, minGuideCount);
        maxGuideCount = std::max<uint32_t>(minGuideCount, maxGuideCount);

        bool found = false;
        float bestScore = std::numeric_limits<float>::max();
        std::ostringstream summary;
        for (uint32_t guideCount = minGuideCount; guideCount <= maxGuideCount; guideCount += guideStep)
        {
            ReducedModelSettings tuned = baseSettings;
            tuned.guideCount = guideCount;
            ReducedModel candidate;
            std::string localError;
            if (!TrainReducedModel(sequence, tuned, candidate, localError))
            {
                summary << "guides=" << guideCount << " status=failed reason=" << localError << "\n";
                continue;
            }
            const float score = (candidate.validationFrameCount > 0u) ? candidate.validationRmsError : candidate.trainingRmsError;
            summary << "guides=" << guideCount
                    << " score=" << score
                    << " trainRms=" << candidate.trainingRmsError
                    << " valRms=" << candidate.validationRmsError
                    << "\n";
            if (!found || score < bestScore)
            {
                found = true;
                bestScore = score;
                outBestModel = std::move(candidate);
            }
            if (maxGuideCount - guideCount < guideStep)
            {
                break;
            }
        }
        if (!found)
        {
            outError = "Failed to train any reduced model candidate during auto-tune.";
            return false;
        }
        outSummary = summary.str();
        return true;
    }

    bool RunReducedModelStudy(const ReducedModelTrainingSequence& sequence,
                              uint32_t minGuideCount,
                              uint32_t maxGuideCount,
                              uint32_t guideStep,
                              const ReducedModelSettings& baseSettings,
                              bool compareDisplacementModes,
                              std::vector<ReducedModelStudyEntry>& outEntries,
                              ReducedModel& outBestModel,
                              std::string& outSummary,
                              std::string& outError)
    {
        if (sequence.Empty())
        {
            outError = "Training sequence is empty.";
            return false;
        }
        guideStep = std::max<uint32_t>(1u, guideStep);
        minGuideCount = std::max<uint32_t>(1u, minGuideCount);
        maxGuideCount = std::max<uint32_t>(minGuideCount, maxGuideCount);

        outEntries.clear();
        bool found = false;
        float bestScore = std::numeric_limits<float>::max();
        std::ostringstream summary;
        const std::array<bool, 2> displacementModes = { true, false };
        const uint32_t modeCount = compareDisplacementModes ? 2u : 1u;
        for (uint32_t modeIndex = 0u; modeIndex < modeCount; ++modeIndex)
        {
            const bool useDisplacements = displacementModes[modeIndex];
            for (uint32_t guideCount = minGuideCount; guideCount <= maxGuideCount; guideCount += guideStep)
            {
                ReducedModelSettings tuned = baseSettings;
                tuned.guideCount = guideCount;
                tuned.useGuideDisplacements = useDisplacements;
                ReducedModel candidate;
                std::string localError;
                if (!TrainReducedModel(sequence, tuned, candidate, localError))
                {
                    summary << "guides=" << guideCount << " displacement=" << (useDisplacements ? 1 : 0) << " status=failed reason=" << localError << "\n";
                    if (maxGuideCount - guideCount < guideStep)
                    {
                        break;
                    }
                    continue;
                }
                const float score = (candidate.validationFrameCount > 0u) ? candidate.validationRmsError : candidate.trainingRmsError;
                outEntries.push_back({
                    guideCount,
                    candidate.settings.maxInfluencesPerFollower,
                    candidate.settings.useGuideDisplacements,
                    candidate.trainingRmsError,
                    candidate.validationRmsError,
                    candidate.trainingTipRmsError,
                    candidate.validationTipRmsError,
                    candidate.validationMaxError,
                    score
                });
                summary << "guides=" << guideCount
                        << " displacement=" << (useDisplacements ? 1 : 0)
                        << " score=" << score
                        << " trainRms=" << candidate.trainingRmsError
                        << " valRms=" << candidate.validationRmsError
                        << "\n";
                if (!found || score < bestScore)
                {
                    found = true;
                    bestScore = score;
                    outBestModel = std::move(candidate);
                }
                if (maxGuideCount - guideCount < guideStep)
                {
                    break;
                }
            }
        }

        if (!found)
        {
            outError = "Failed to train any reduced model candidate during study.";
            return false;
        }
        outSummary = summary.str();
        return true;
    }

    bool WriteReducedModelStudyJson(const std::filesystem::path& filePath,
                                    const std::vector<ReducedModelStudyEntry>& entries,
                                    const ReducedModel& bestModel,
                                    std::string& outError)
    {
        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to open reduced study JSON output.";
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"bestModel\": {\n";
        out << "    \"guideCount\": " << bestModel.selectedGuideStrandIndices.size() << ",\n";
        out << "    \"maxInfluences\": " << bestModel.settings.maxInfluencesPerFollower << ",\n";
        out << "    \"useGuideDisplacements\": " << (bestModel.settings.useGuideDisplacements ? "true" : "false") << ",\n";
        out << "    \"trainingRmsError\": " << bestModel.trainingRmsError << ",\n";
        out << "    \"validationRmsError\": " << bestModel.validationRmsError << ",\n";
        out << "    \"validationMaxError\": " << bestModel.validationMaxError << "\n";
        out << "  },\n";
        out << "  \"entries\": [\n";
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const ReducedModelStudyEntry& e = entries[i];
            out << "    {\n";
            out << "      \"guideCount\": " << e.guideCount << ",\n";
            out << "      \"maxInfluences\": " << e.maxInfluences << ",\n";
            out << "      \"useGuideDisplacements\": " << (e.useGuideDisplacements ? "true" : "false") << ",\n";
            out << "      \"trainingRmsError\": " << e.trainingRmsError << ",\n";
            out << "      \"validationRmsError\": " << e.validationRmsError << ",\n";
            out << "      \"trainingTipRmsError\": " << e.trainingTipRmsError << ",\n";
            out << "      \"validationTipRmsError\": " << e.validationTipRmsError << ",\n";
            out << "      \"validationMaxError\": " << e.validationMaxError << ",\n";
            out << "      \"score\": " << e.score << "\n";
            out << "    }" << ((i + 1u < entries.size()) ? "," : "") << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return true;
    }

    bool EvaluateReducedModel(const ReducedModel& model,
                              const ReducedModelTrainingSequence& sequence,
                              ReducedModelEvaluationReport& outReport,
                              std::string& outError)
    {
        if (sequence.referenceAsset.strands.size() != model.referenceAsset.strands.size() ||
            sequence.referenceAsset.restPoints.size() != model.referenceAsset.restPoints.size())
        {
            outError = "Training sequence layout does not match the reduced model reference asset.";
            return false;
        }

        ReducedModelEvaluationReport report = {};
        double sumSq = 0.0;
        double tipSq = 0.0;
        double rootSq = 0.0;
        uint64_t pointSamples = 0u;
        uint64_t strandSamples = 0u;
        std::vector<Vec3> guideFramePoints;

        for (uint32_t frameIndex = 0u; frameIndex < sequence.FrameCount(); ++frameIndex)
        {
            const Vec3* frame = FramePoints(sequence, frameIndex);
            CopySelectedGuideFrame(model, frame, guideFramePoints);
            HairAsset reconstructed;
            if (!ReconstructReducedModelFrame(model, guideFramePoints, reconstructed, outError))
            {
                return false;
            }

            for (uint32_t strandIndex = 0u; strandIndex < model.referenceAsset.strands.size(); ++strandIndex)
            {
                const StrandRange& strand = model.referenceAsset.strands[strandIndex];
                const uint32_t tipIndex = strand.pointOffset + (strand.pointCount - 1u);
                const uint32_t rootIndex = strand.pointOffset;

                const float rootError = Length(reconstructed.restPoints[rootIndex] - frame[rootIndex]);
                const float tipError = Length(reconstructed.restPoints[tipIndex] - frame[tipIndex]);
                rootSq += static_cast<double>(rootError) * rootError;
                tipSq += static_cast<double>(tipError) * tipError;
                report.maxRootError = std::max(report.maxRootError, rootError);
                report.maxTipError = std::max(report.maxTipError, tipError);
                ++strandSamples;

                for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
                {
                    const uint32_t globalPoint = strand.pointOffset + pointIndex;
                    const float error = Length(reconstructed.restPoints[globalPoint] - frame[globalPoint]);
                    sumSq += static_cast<double>(error) * error;
                    report.maxPositionError = std::max(report.maxPositionError, error);
                    ++pointSamples;
                }
            }
        }

        report.frameCount = sequence.FrameCount();
        report.pointSamples = static_cast<uint32_t>(std::min<uint64_t>(pointSamples, std::numeric_limits<uint32_t>::max()));
        report.strandSamples = static_cast<uint32_t>(std::min<uint64_t>(strandSamples, std::numeric_limits<uint32_t>::max()));
        report.rmsPositionError = (pointSamples > 0u) ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(pointSamples))) : 0.0f;
        report.rmsTipError = (strandSamples > 0u) ? static_cast<float>(std::sqrt(tipSq / static_cast<double>(strandSamples))) : 0.0f;
        report.rmsRootError = (strandSamples > 0u) ? static_cast<float>(std::sqrt(rootSq / static_cast<double>(strandSamples))) : 0.0f;
        outReport = report;
        return true;
    }

bool WriteReducedEvaluationReportJson(const std::filesystem::path& filePath,
                                          const ReducedModelEvaluationReport& report,
                                          std::string& outError)
    {
        std::ofstream out(filePath);
        if (!out)
        {
            outError = "Failed to open reduced evaluation JSON output.";
            return false;
        }
        out << std::fixed << std::setprecision(6);
        out << "{\n";
        out << "  \"frameCount\": " << report.frameCount << ",\n";
        out << "  \"pointSamples\": " << report.pointSamples << ",\n";
        out << "  \"strandSamples\": " << report.strandSamples << ",\n";
        out << "  \"rmsPositionError\": " << report.rmsPositionError << ",\n";
        out << "  \"maxPositionError\": " << report.maxPositionError << ",\n";
        out << "  \"rmsTipError\": " << report.rmsTipError << ",\n";
        out << "  \"maxTipError\": " << report.maxTipError << ",\n";
        out << "  \"rmsRootError\": " << report.rmsRootError << ",\n";
        out << "  \"maxRootError\": " << report.maxRootError << "\n";
        out << "}\n";
        return true;
    }

    bool ExportReducedModelGuideCache(const ReducedModel& model,
                                      const ReducedModelTrainingSequence& sequence,
                                      GuideCache& outGuideCache,
                                      std::string& outError)
    {
        if (model.selectedGuideStrandIndices.empty())
        {
            outError = "Reduced model does not contain any selected guide strands.";
            return false;
        }
        GuideCache cache = {};
        cache.guideStrandCount = static_cast<uint32_t>(model.selectedGuideStrandIndices.size());
        cache.particlesPerStrand = model.referenceAsset.particlesPerStrand;
        cache.frameRate = std::max(sequence.frameRate, 1.0f);

        std::vector<Vec3> guideFramePoints;
        for (uint32_t frameIndex = 0u; frameIndex < sequence.FrameCount(); ++frameIndex)
        {
            const Vec3* frame = FramePoints(sequence, frameIndex);
            CopySelectedGuideFrame(model, frame, guideFramePoints);
            if (!AppendGuideCacheGuidePoints(cache, guideFramePoints, outError))
            {
                return false;
            }
        }
        outGuideCache = std::move(cache);
        return true;
    }

    bool ReconstructReducedModelFrame(const ReducedModel& model,
                                      const std::vector<Vec3>& guideFramePoints,
                                      HairAsset& outAsset,
                                      std::string& outError)
    {
        const uint32_t expectedGuidePoints = static_cast<uint32_t>(model.selectedGuideStrandIndices.size()) * model.referenceAsset.particlesPerStrand;
        if (guideFramePoints.size() != expectedGuidePoints)
        {
            outError = "Guide frame point count does not match the reduced model guide layout.";
            return false;
        }

        HairAsset asset = model.referenceAsset;
        asset.restPoints = model.referenceAsset.restPoints;
        for (uint32_t strandIndex = 0u; strandIndex < model.referenceAsset.strands.size(); ++strandIndex)
        {
            const StrandRange& strand = model.referenceAsset.strands[strandIndex];
            const ReducedModelStrandRecord& record = model.strandRecords[strandIndex];
            if (record.isGuide)
            {
                const uint32_t guideBase = record.guideSlot * model.referenceAsset.particlesPerStrand;
                for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
                {
                    asset.restPoints[strand.pointOffset + pointIndex] = guideFramePoints[guideBase + pointIndex];
                }
                ApplyReducedRuntimePostConstraints(model, strandIndex, strand, asset.restPoints);
                continue;
            }

            for (uint32_t pointIndex = 0u; pointIndex < strand.pointCount; ++pointIndex)
            {
                Vec3 reconstructed = model.settings.useGuideDisplacements
                    ? model.referenceAsset.restPoints[strand.pointOffset + pointIndex]
                    : Vec3{};
                for (uint32_t influenceLocal = 0u; influenceLocal < record.influenceCount; ++influenceLocal)
                {
                    const ReducedModelInfluence& influence = model.influences[record.influenceOffset + influenceLocal];
                    const uint32_t guidePoint = influence.guideSlot * model.referenceAsset.particlesPerStrand + pointIndex;
                    if (model.settings.useGuideDisplacements)
                    {
                        const uint32_t guideStrandIndex = model.selectedGuideStrandIndices[influence.guideSlot];
                        const StrandRange& guideStrand = model.referenceAsset.strands[guideStrandIndex];
                        const uint32_t clampedPoint = std::min<uint32_t>(pointIndex, guideStrand.pointCount - 1u);
                        const Vec3& guideRest = model.referenceAsset.restPoints[guideStrand.pointOffset + clampedPoint];
                        reconstructed += (guideFramePoints[guidePoint] - guideRest) * influence.weight;
                    }
                    else
                    {
                        reconstructed += guideFramePoints[guidePoint] * influence.weight;
                    }
                }
                reconstructed += model.pointBiases[record.biasOffset + pointIndex];
                asset.restPoints[strand.pointOffset + pointIndex] = reconstructed;
            }
            ApplyLengthProjection(model.referenceAsset, strand, model.settings.lengthProjectionBlend, asset.restPoints);
            ApplyReducedRuntimePostConstraints(model, strandIndex, strand, asset.restPoints);
        }

        outAsset = std::move(asset);
        return true;
    }

bool ReconstructReducedModelSequence(const ReducedModel& model,
                                         const ReducedModelTrainingSequence& sequence,
                                         const std::filesystem::path& outputDirectory,
                                         std::string& outError)
    {
        std::error_code ec;
        std::filesystem::create_directories(outputDirectory, ec);
        if (ec)
        {
            outError = "Failed to create reduced reconstruction output directory.";
            return false;
        }

        ReducedModelTrainingSequence reconstructedSequence = {};
        reconstructedSequence.referenceAsset = model.referenceAsset;
        reconstructedSequence.frameRate = sequence.frameRate;
        reconstructedSequence.frames.reserve(static_cast<size_t>(sequence.FrameCount()) * model.referenceAsset.restPoints.size());

        for (uint32_t frameIndex = 0u; frameIndex < sequence.FrameCount(); ++frameIndex)
        {
            HairAsset reconstructed;
            if (!ReconstructReducedModelFrameFromSequence(model, sequence, frameIndex, reconstructed, outError))
            {
                return false;
            }
            std::ostringstream fileNameBuilder;
            fileNameBuilder << "frame_" << std::setw(4) << std::setfill('0') << frameIndex << ".hairbin";
            if (!SaveHairAssetToHairBinary(outputDirectory / fileNameBuilder.str(), reconstructed, outError))
            {
                return false;
            }
            reconstructedSequence.frames.insert(reconstructedSequence.frames.end(), reconstructed.restPoints.begin(), reconstructed.restPoints.end());
        }
        return SaveReducedTrainingSequenceManifest(outputDirectory, reconstructedSequence, outError);
    }

        bool ReconstructReducedModelFrameFromSequence(const ReducedModel& model,
                                                  const ReducedModelTrainingSequence& sequence,
                                                  uint32_t frameIndex,
                                                  HairAsset& outAsset,
                                                  std::string& outError)
    {
        if (frameIndex >= sequence.FrameCount())
        {
            outError = "Frame index is out of range for the training sequence.";
            return false;
        }
        const Vec3* frame = FramePoints(sequence, frameIndex);
        std::vector<Vec3> guideFramePoints;
        CopySelectedGuideFrame(model, frame, guideFramePoints);
        return ReconstructReducedModelFrame(model, guideFramePoints, outAsset, outError);
    }
}
