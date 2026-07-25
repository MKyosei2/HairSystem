#include "hairgpu/Broadphase.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hairgpu
{
    namespace
    {
        struct Int3
        {
            int x = 0;
            int y = 0;
            int z = 0;

            bool operator==(const Int3& rhs) const
            {
                return x == rhs.x && y == rhs.y && z == rhs.z;
            }
        };

        struct Int3Hash
        {
            size_t operator()(const Int3& value) const noexcept
            {
                const uint32_t x = static_cast<uint32_t>(value.x * 73856093);
                const uint32_t y = static_cast<uint32_t>(value.y * 19349663);
                const uint32_t z = static_cast<uint32_t>(value.z * 83492791);
                return static_cast<size_t>(x ^ y ^ z);
            }
        };

        struct ParticleEntry
        {
            uint32_t particleIndex = 0u;
            uint32_t strandIndex = 0u;
            uint32_t localIndex = 0u;
            Int3 cell = {};
        };

        struct CandidatePair
        {
            uint32_t a = 0u;
            uint32_t b = 0u;
        };

        bool IsFinite(const Vec3& v)
        {
            return std::isfinite(v.x) != 0 &&
                   std::isfinite(v.y) != 0 &&
                   std::isfinite(v.z) != 0;
        }

        Int3 ComputeCell(const Vec3& p, float cellSize)
        {
            const float inv = 1.0f / std::max(cellSize, 1.0e-6f);
            return {
                static_cast<int>(std::floor(p.x * inv)),
                static_cast<int>(std::floor(p.y * inv)),
                static_cast<int>(std::floor(p.z * inv))
            };
        }

        uint32_t ComputeStrandIndex(
            const HairAsset& asset,
            uint32_t particleIndex,
            uint32_t fallback)
        {
            if (asset.particlesPerStrand > 0u)
            {
                return particleIndex / asset.particlesPerStrand;
            }
            return fallback;
        }

        bool AreAdjacentOnSameStrand(
            const ParticleEntry& a,
            const ParticleEntry& b)
        {
            if (a.strandIndex != b.strandIndex)
            {
                return false;
            }
            const uint32_t delta =
                a.localIndex > b.localIndex
                    ? a.localIndex - b.localIndex
                    : b.localIndex - a.localIndex;
            return delta <= 2u;
        }

        bool ShareRootTriangleNearRoot(
            const HairAsset& asset,
            const ParticleEntry& a,
            const ParticleEntry& b)
        {
            if (a.strandIndex == b.strandIndex ||
                a.strandIndex >= asset.rootBindings.size() ||
                b.strandIndex >= asset.rootBindings.size() ||
                a.localIndex > 2u ||
                b.localIndex > 2u)
            {
                return false;
            }
            return asset.rootBindings[a.strandIndex].triangleIndex ==
                   asset.rootBindings[b.strandIndex].triangleIndex;
        }

        uint64_t PairKey(uint32_t a, uint32_t b)
        {
            const uint32_t lo = std::min(a, b);
            const uint32_t hi = std::max(a, b);
            return (static_cast<uint64_t>(lo) << 32u) | hi;
        }

        const std::vector<Vec3>& SelectPositions(
            const HairAsset& asset,
            const std::vector<Vec3>& currentPositions,
            BroadphaseStats& stats)
        {
            if (currentPositions.size() == asset.restPoints.size() &&
                !currentPositions.empty())
            {
                return currentPositions;
            }
            ++stats.positionFallbackCount;
            return asset.restPoints;
        }

        bool BuildBroadphase(
            const HairAsset& asset,
            const std::vector<Vec3>& positions,
            float cellSize,
            uint32_t hashBucketCount,
            uint32_t candidatePairCapacity,
            BroadphaseStats& stats,
            std::vector<CandidatePair>* outPairs,
            std::string& outError)
        {
            outError.clear();
            if (cellSize <= 0.0f || !std::isfinite(cellSize))
            {
                outError = "cellSize must be finite and greater than zero.";
                return false;
            }
            if (positions.size() != asset.restPoints.size())
            {
                outError = "Position count does not match the asset point count.";
                return false;
            }

            stats.strandCount = static_cast<uint32_t>(asset.strands.size());
            stats.particleCount = static_cast<uint32_t>(positions.size());
            stats.hashBucketCount = hashBucketCount;
            stats.candidatePairCapacity = candidatePairCapacity;
            stats.strandAabbCount = static_cast<uint32_t>(asset.strands.size());
            stats.segmentGroupBlockAabbCount =
                static_cast<uint32_t>(asset.segmentGroupTopology.blocks.size());

            std::vector<ParticleEntry> entries;
            entries.reserve(positions.size());

            for (uint32_t strandIndex = 0u;
                 strandIndex < static_cast<uint32_t>(asset.strands.size());
                 ++strandIndex)
            {
                const StrandRange& strand = asset.strands[strandIndex];
                const uint64_t end =
                    static_cast<uint64_t>(strand.pointOffset) + strand.pointCount;
                if (end > positions.size())
                {
                    outError = "A strand references points outside the supplied position array.";
                    return false;
                }

                // Roots are fixed and do not participate in self-collision insertion.
                for (uint32_t local = 1u; local < strand.pointCount; ++local)
                {
                    const uint32_t particleIndex = strand.pointOffset + local;
                    if (!IsFinite(positions[particleIndex]))
                    {
                        ++stats.nonFinitePositionCount;
                        continue;
                    }

                    ParticleEntry entry = {};
                    entry.particleIndex = particleIndex;
                    entry.strandIndex = ComputeStrandIndex(asset, particleIndex, strandIndex);
                    entry.localIndex = local;
                    entry.cell = ComputeCell(positions[particleIndex], cellSize);
                    entries.push_back(entry);
                }
            }

            stats.insertedParticles = static_cast<uint32_t>(entries.size());

            std::unordered_map<Int3, std::vector<uint32_t>, Int3Hash> bins;
            bins.reserve(entries.size() * 2u + 1u);
            for (uint32_t i = 0u; i < static_cast<uint32_t>(entries.size()); ++i)
            {
                bins[entries[i].cell].push_back(i);
            }

            stats.occupiedBins = static_cast<uint32_t>(bins.size());
            stats.sortedCellRangeCount = stats.occupiedBins;

            uint64_t occupancySum = 0u;
            std::vector<uint32_t> bucketOccupancy(hashBucketCount, 0u);
            for (const auto& item : bins)
            {
                const uint32_t occupancy = static_cast<uint32_t>(item.second.size());
                occupancySum += occupancy;
                stats.maxBinOccupancy = std::max(stats.maxBinOccupancy, occupancy);

                if (hashBucketCount > 0u)
                {
                    const uint32_t bucket =
                        static_cast<uint32_t>(Int3Hash{}(item.first) % hashBucketCount);
                    bucketOccupancy[bucket] += occupancy;
                }
            }

            if (stats.occupiedBins > 0u)
            {
                stats.averageBinOccupancy =
                    static_cast<float>(occupancySum) /
                    static_cast<float>(stats.occupiedBins);
            }

            uint64_t usedBucketOccupancy = 0u;
            for (uint32_t occupancy : bucketOccupancy)
            {
                if (occupancy > 0u)
                {
                    ++stats.usedHashBuckets;
                    usedBucketOccupancy += occupancy;
                    stats.maxHashBucketOccupancy =
                        std::max(stats.maxHashBucketOccupancy, occupancy);
                }
            }
            if (stats.usedHashBuckets > 0u)
            {
                stats.averageHashBucketOccupancy =
                    static_cast<float>(usedBucketOccupancy) /
                    static_cast<float>(stats.usedHashBuckets);
            }

            std::set<uint64_t> uniquePairs;
            static const int offsets[27][3] = {
                {-1,-1,-1},{0,-1,-1},{1,-1,-1},
                {-1, 0,-1},{0, 0,-1},{1, 0,-1},
                {-1, 1,-1},{0, 1,-1},{1, 1,-1},
                {-1,-1, 0},{0,-1, 0},{1,-1, 0},
                {-1, 0, 0},{0, 0, 0},{1, 0, 0},
                {-1, 1, 0},{0, 1, 0},{1, 1, 0},
                {-1,-1, 1},{0,-1, 1},{1,-1, 1},
                {-1, 0, 1},{0, 0, 1},{1, 0, 1},
                {-1, 1, 1},{0, 1, 1},{1, 1, 1}
            };

            for (uint32_t indexA = 0u;
                 indexA < static_cast<uint32_t>(entries.size());
                 ++indexA)
            {
                const ParticleEntry& a = entries[indexA];

                for (const auto& offset : offsets)
                {
                    const Int3 neighbor = {
                        a.cell.x + offset[0],
                        a.cell.y + offset[1],
                        a.cell.z + offset[2]
                    };
                    const auto binIt = bins.find(neighbor);
                    if (binIt == bins.end())
                    {
                        continue;
                    }

                    for (uint32_t indexB : binIt->second)
                    {
                        if (indexB <= indexA)
                        {
                            continue;
                        }

                        const ParticleEntry& b = entries[indexB];
                        if (AreAdjacentOnSameStrand(a, b))
                        {
                            ++stats.skippedSameStrandAdjacentPairs;
                            continue;
                        }
                        if (ShareRootTriangleNearRoot(asset, a, b))
                        {
                            ++stats.skippedSameRootTrianglePairs;
                            continue;
                        }

                        const uint64_t key = PairKey(a.particleIndex, b.particleIndex);
                        if (!uniquePairs.insert(key).second)
                        {
                            ++stats.duplicateCandidatePairsRemoved;
                            continue;
                        }

                        if (candidatePairCapacity > 0u &&
                            uniquePairs.size() > candidatePairCapacity)
                        {
                            ++stats.candidatePairOverflowCount;
                            uniquePairs.erase(key);
                            continue;
                        }

                        if (outPairs)
                        {
                            outPairs->push_back({ a.particleIndex, b.particleIndex });
                        }
                    }
                }
            }

            stats.candidatePairs = static_cast<uint32_t>(uniquePairs.size());
            return true;
        }
    }

    bool EvaluateBroadphaseStats(
        const HairAsset& asset,
        float cellSize,
        SelfCollisionBroadphaseMode mode,
        uint32_t hashBucketCount,
        BroadphaseStats& outStats,
        std::string& outError)
    {
        return EvaluateBroadphaseStats(
            asset,
            asset.restPoints,
            cellSize,
            mode,
            hashBucketCount,
            0u,
            outStats,
            outError);
    }

    bool EvaluateBroadphaseStats(
        const HairAsset& asset,
        const std::vector<Vec3>& currentPositions,
        float cellSize,
        SelfCollisionBroadphaseMode mode,
        uint32_t hashBucketCount,
        uint32_t candidatePairCapacity,
        BroadphaseStats& outStats,
        std::string& outError)
    {
        outStats = {};
        outStats.mode = mode;
        outStats.cellSize = cellSize;

        const std::vector<Vec3>& positions =
            SelectPositions(asset, currentPositions, outStats);

        return BuildBroadphase(
            asset,
            positions,
            cellSize,
            hashBucketCount,
            candidatePairCapacity,
            outStats,
            nullptr,
            outError);
    }

    bool ExportBroadphaseDebugCsv(
        const HairAsset& asset,
        float cellSize,
        SelfCollisionBroadphaseMode mode,
        uint32_t hashBucketCount,
        const std::filesystem::path& outputCsv,
        uint32_t maxPairsToWrite,
        std::string& outError)
    {
        return ExportBroadphaseDebugCsv(
            asset,
            asset.restPoints,
            cellSize,
            mode,
            hashBucketCount,
            maxPairsToWrite,
            outputCsv,
            maxPairsToWrite,
            outError);
    }

    bool ExportBroadphaseDebugCsv(
        const HairAsset& asset,
        const std::vector<Vec3>& currentPositions,
        float cellSize,
        SelfCollisionBroadphaseMode mode,
        uint32_t hashBucketCount,
        uint32_t candidatePairCapacity,
        const std::filesystem::path& outputCsv,
        uint32_t maxPairsToWrite,
        std::string& outError)
    {
        BroadphaseStats stats = {};
        stats.mode = mode;
        stats.cellSize = cellSize;

        const std::vector<Vec3>& positions =
            SelectPositions(asset, currentPositions, stats);

        std::vector<CandidatePair> pairs;
        pairs.reserve(maxPairsToWrite);

        if (!BuildBroadphase(
                asset,
                positions,
                cellSize,
                hashBucketCount,
                candidatePairCapacity,
                stats,
                &pairs,
                outError))
        {
            return false;
        }

        std::error_code ec;
        if (outputCsv.has_parent_path())
        {
            std::filesystem::create_directories(outputCsv.parent_path(), ec);
            if (ec)
            {
                outError = "Failed to create the broadphase CSV output directory.";
                return false;
            }
        }

        std::ofstream stream(outputCsv);
        if (!stream)
        {
            outError = "Failed to open the broadphase CSV output file.";
            return false;
        }

        stream << "particleA,particleB\n";
        const size_t writeCount = std::min<size_t>(pairs.size(), maxPairsToWrite);
        for (size_t i = 0u; i < writeCount; ++i)
        {
            stream << pairs[i].a << ',' << pairs[i].b << '\n';
        }

        stream << "#mode," << static_cast<uint32_t>(mode) << '\n';
        stream << "#cellSize," << cellSize << '\n';
        stream << "#insertedParticles," << stats.insertedParticles << '\n';
        stream << "#occupiedBins," << stats.occupiedBins << '\n';
        stream << "#candidatePairs," << stats.candidatePairs << '\n';
        stream << "#candidatePairOverflowCount," << stats.candidatePairOverflowCount << '\n';
        stream << "#nonFinitePositionCount," << stats.nonFinitePositionCount << '\n';
        stream << "#positionFallbackCount," << stats.positionFallbackCount << '\n';

        if (!stream.good())
        {
            outError = "Failed while writing broadphase CSV.";
            return false;
        }
        return true;
    }
}
