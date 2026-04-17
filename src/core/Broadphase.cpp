#include "hairgpu/Broadphase.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace hairgpu
{
    namespace
    {
        struct Int3Key
        {
            int x = 0;
            int y = 0;
            int z = 0;

            bool operator==(const Int3Key& rhs) const
            {
                return x == rhs.x && y == rhs.y && z == rhs.z;
            }
        };

        struct CellLookupKey
        {
            uint32_t level = 0u;
            Int3Key coord = {};

            bool operator==(const CellLookupKey& rhs) const
            {
                return level == rhs.level && coord == rhs.coord;
            }
        };

        struct Int3Hasher
        {
            size_t operator()(const Int3Key& key) const noexcept
            {
                const uint32_t hx = static_cast<uint32_t>(key.x * 73856093);
                const uint32_t hy = static_cast<uint32_t>(key.y * 19349663);
                const uint32_t hz = static_cast<uint32_t>(key.z * 83492791);
                return static_cast<size_t>(hx ^ hy ^ hz);
            }
        };

        struct CellLookupHasher
        {
            size_t operator()(const CellLookupKey& key) const noexcept
            {
                return Int3Hasher{}(key.coord) ^ (static_cast<size_t>(key.level) * 0x9E3779B1u);
            }
        };

        struct ParticleEntry
        {
            uint32_t particleIndex = 0u;
            uint32_t strandIndex = 0u;
            uint32_t localIndex = 0u;
            uint32_t level = 0u;
            uint32_t hashBucket = 0u;
            Int3Key coord = {};
            uint64_t sortKey = 0u;
        };

        struct CellRange
        {
            uint64_t sortKey = 0u;
            uint32_t level = 0u;
            Int3Key coord = {};
            uint32_t hashBucket = 0u;
            uint32_t start = 0u;
            uint32_t count = 0u;
        };

        struct PairRecord
        {
            uint32_t a = 0u;
            uint32_t b = 0u;
            uint32_t levelA = 0u;
            uint32_t levelB = 0u;
            int ax = 0;
            int ay = 0;
            int az = 0;
            int bx = 0;
            int by = 0;
            int bz = 0;
            float distance = 0.0f;
        };

        constexpr uint32_t kAdaptiveLevelCount = 3u;

        Int3Key ComputeCellCoord(const Vec3& p, float cellSize)
        {
            const float invCell = 1.0f / std::max(cellSize, 1.0e-5f);
            return {
                static_cast<int>(std::floor(p.x * invCell)),
                static_cast<int>(std::floor(p.y * invCell)),
                static_cast<int>(std::floor(p.z * invCell))
            };
        }

        uint32_t HashCellCoord(const Int3Key& key, uint32_t bucketCount, uint32_t level)
        {
            if (bucketCount == 0u)
            {
                return 0u;
            }
            const uint32_t hx = static_cast<uint32_t>(key.x * 73856093);
            const uint32_t hy = static_cast<uint32_t>(key.y * 19349663);
            const uint32_t hz = static_cast<uint32_t>(key.z * 83492791);
            const uint32_t hl = (level + 1u) * 2654435761u;
            return (hx ^ hy ^ hz ^ hl) % bucketCount;
        }

        uint64_t PackSigned21(int value)
        {
            constexpr int kMin = -(1 << 20);
            constexpr int kMax = (1 << 20) - 1;
            const int clamped = std::max(kMin, std::min(kMax, value));
            return static_cast<uint64_t>(clamped - kMin);
        }

        uint64_t ComputeSortKey(const Int3Key& coord, uint32_t level)
        {
            return (static_cast<uint64_t>(level & 0x3u) << 63ull) |
                   (PackSigned21(coord.x) << 42ull) |
                   (PackSigned21(coord.y) << 21ull) |
                   PackSigned21(coord.z);
        }

        float ComputeAdaptiveCellSize(float baseCellSize, uint32_t level)
        {
            return baseCellSize * static_cast<float>(1u << std::min(level, 8u));
        }

        uint32_t SelectAdaptiveLevel(const HairAsset& asset, uint32_t particleIndex, uint32_t localIndex)
        {
            float width = 0.0f;
            if (particleIndex < asset.pointWidths.size())
            {
                width = asset.pointWidths[particleIndex];
            }

            const float tipT = (asset.particlesPerStrand > 1u)
                ? (static_cast<float>(localIndex) / static_cast<float>(asset.particlesPerStrand - 1u))
                : 0.0f;

            uint32_t level = 0u;
            if (tipT > 0.35f || width > 0.0065f)
            {
                level = 1u;
            }
            if (tipT > 0.72f || width > 0.0125f)
            {
                level = 2u;
            }
            return std::min(level, kAdaptiveLevelCount - 1u);
        }

        bool UsesHashBuckets(SelfCollisionBroadphaseMode mode)
        {
            return mode == SelfCollisionBroadphaseMode::SpatialHash || mode == SelfCollisionBroadphaseMode::AdaptiveSpatialHash;
        }

        bool UsesAdaptiveLevels(SelfCollisionBroadphaseMode mode)
        {
            return mode == SelfCollisionBroadphaseMode::AdaptiveSpatialHash;
        }

        bool UsesRadixSortedRanges(SelfCollisionBroadphaseMode mode)
        {
            return mode == SelfCollisionBroadphaseMode::SortedGrid || mode == SelfCollisionBroadphaseMode::AdaptiveSpatialHash;
        }

        bool IsSameStrandWindow(const HairAsset& asset, uint32_t a, uint32_t b, uint32_t maxLocalDelta)
        {
            if (asset.particlesPerStrand == 0u)
            {
                return false;
            }
            const uint32_t strandA = a / asset.particlesPerStrand;
            const uint32_t strandB = b / asset.particlesPerStrand;
            if (strandA != strandB)
            {
                return false;
            }
            const uint32_t localA = a % asset.particlesPerStrand;
            const uint32_t localB = b % asset.particlesPerStrand;
            const uint32_t delta = (localA > localB) ? (localA - localB) : (localB - localA);
            return delta <= maxLocalDelta;
        }

        bool IsSameRootTriangleNearRoot(const HairAsset& asset, uint32_t a, uint32_t b)
        {
            if (asset.particlesPerStrand == 0u || asset.rootBindings.empty())
            {
                return false;
            }

            const uint32_t strandA = a / asset.particlesPerStrand;
            const uint32_t strandB = b / asset.particlesPerStrand;
            if (strandA == strandB || strandA >= asset.rootBindings.size() || strandB >= asset.rootBindings.size())
            {
                return false;
            }

            const uint32_t localA = a % asset.particlesPerStrand;
            const uint32_t localB = b % asset.particlesPerStrand;
            if (localA > 2u || localB > 2u)
            {
                return false;
            }

            return asset.rootBindings[strandA].triangleIndex == asset.rootBindings[strandB].triangleIndex;
        }

        uint32_t ComputeInsertedParticleCount(const HairAsset& asset)
        {
            if (asset.guideStrandCount == 0u || asset.particlesPerStrand <= 1u)
            {
                return 0u;
            }
            return asset.guideStrandCount * (asset.particlesPerStrand - 1u);
        }

        void RadixSortParticleEntries(std::vector<ParticleEntry>& entries)
        {
            if (entries.size() <= 1u)
            {
                return;
            }

            std::vector<ParticleEntry> scratch(entries.size());
            std::array<size_t, 256u> counts = {};

            for (uint32_t shift = 0u; shift < 64u; shift += 8u)
            {
                counts.fill(0u);
                for (const ParticleEntry& entry : entries)
                {
                    const uint32_t byteValue = static_cast<uint32_t>((entry.sortKey >> shift) & 0xFFull);
                    counts[byteValue] += 1u;
                }

                size_t prefix = 0u;
                for (size_t i = 0u; i < counts.size(); ++i)
                {
                    const size_t current = counts[i];
                    counts[i] = prefix;
                    prefix += current;
                }

                for (const ParticleEntry& entry : entries)
                {
                    const uint32_t byteValue = static_cast<uint32_t>((entry.sortKey >> shift) & 0xFFull);
                    scratch[counts[byteValue]++] = entry;
                }

                entries.swap(scratch);
            }
        }

        std::vector<ParticleEntry> BuildEntries(const HairAsset& asset,
                                                float cellSize,
                                                SelfCollisionBroadphaseMode mode,
                                                uint32_t hashBucketCount,
                                                BroadphaseStats& ioStats)
        {
            std::vector<ParticleEntry> entries;
            entries.reserve(ComputeInsertedParticleCount(asset));
            std::array<bool, kAdaptiveLevelCount> levelMask = {};

            for (uint32_t strandIndex = 0u; strandIndex < asset.guideStrandCount; ++strandIndex)
            {
                const uint32_t strandBase = strandIndex * asset.particlesPerStrand;
                for (uint32_t localIndex = 1u; localIndex < asset.particlesPerStrand; ++localIndex)
                {
                    const uint32_t particleIndex = strandBase + localIndex;
                    const uint32_t level = UsesAdaptiveLevels(mode) ? SelectAdaptiveLevel(asset, particleIndex, localIndex) : 0u;
                    const float effectiveCellSize = ComputeAdaptiveCellSize(cellSize, level);
                    const Int3Key coord = ComputeCellCoord(asset.restPoints[particleIndex], effectiveCellSize);
                    const uint32_t hashBucket = UsesHashBuckets(mode) ? HashCellCoord(coord, hashBucketCount, level) : 0u;

                    ParticleEntry entry = {};
                    entry.particleIndex = particleIndex;
                    entry.strandIndex = strandIndex;
                    entry.localIndex = localIndex;
                    entry.level = level;
                    entry.hashBucket = hashBucket;
                    entry.coord = coord;
                    entry.sortKey = ComputeSortKey(coord, level);
                    entries.push_back(entry);
                    levelMask[level] = true;
                    ioStats.maxAdaptiveLevel = std::max(ioStats.maxAdaptiveLevel, level);
                }
            }

            ioStats.activeAdaptiveLevels = 0u;
            for (bool used : levelMask)
            {
                if (used)
                {
                    ioStats.activeAdaptiveLevels += 1u;
                }
            }

            return entries;
        }

        std::unordered_map<CellLookupKey, std::vector<uint32_t>, CellLookupHasher> BuildBinMap(const std::vector<ParticleEntry>& entries)
        {
            std::unordered_map<CellLookupKey, std::vector<uint32_t>, CellLookupHasher> bins;
            bins.reserve(entries.size() * 2u + 1u);
            for (uint32_t entryIndex = 0u; entryIndex < static_cast<uint32_t>(entries.size()); ++entryIndex)
            {
                const ParticleEntry& entry = entries[entryIndex];
                bins[{ entry.level, entry.coord }].push_back(entryIndex);
            }
            return bins;
        }

        std::vector<CellRange> BuildCellRanges(std::vector<ParticleEntry> entries)
        {
            RadixSortParticleEntries(entries);
            std::vector<CellRange> ranges;
            if (entries.empty())
            {
                return ranges;
            }

            uint32_t runStart = 0u;
            while (runStart < entries.size())
            {
                const ParticleEntry& first = entries[runStart];
                uint32_t runEnd = runStart + 1u;
                while (runEnd < entries.size() && entries[runEnd].sortKey == first.sortKey)
                {
                    ++runEnd;
                }

                CellRange range = {};
                range.sortKey = first.sortKey;
                range.level = first.level;
                range.coord = first.coord;
                range.hashBucket = first.hashBucket;
                range.start = runStart;
                range.count = runEnd - runStart;
                ranges.push_back(range);
                runStart = runEnd;
            }
            return ranges;
        }

        Vec3 LoadPoint(const HairAsset& asset, uint32_t particleIndex)
        {
            if (particleIndex < asset.restPoints.size())
            {
                return asset.restPoints[particleIndex];
            }
            return {};
        }

        float Distance(const Vec3& a, const Vec3& b)
        {
            const float dx = a.x - b.x;
            const float dy = a.y - b.y;
            const float dz = a.z - b.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        bool AcceptPair(const HairAsset& asset,
                        uint32_t a,
                        uint32_t b,
                        BroadphaseStats& ioStats)
        {
            if (a == b)
            {
                ioStats.duplicateCandidatePairsRemoved += 1u;
                return false;
            }
            if (a > b)
            {
                ioStats.duplicateCandidatePairsRemoved += 1u;
                return false;
            }
            if (IsSameStrandWindow(asset, a, b, 2u))
            {
                ioStats.skippedSameStrandAdjacentPairs += 1u;
                return false;
            }
            if (IsSameRootTriangleNearRoot(asset, a, b))
            {
                ioStats.skippedSameRootTrianglePairs += 1u;
                return false;
            }
            return true;
        }

        void EvaluateCandidatePairs(const HairAsset& asset,
                                    const std::vector<ParticleEntry>& entries,
                                    const std::unordered_map<CellLookupKey, std::vector<uint32_t>, CellLookupHasher>& bins,
                                    SelfCollisionBroadphaseMode mode,
                                    BroadphaseStats& ioStats,
                                    std::vector<PairRecord>* outPairs)
        {
            std::unordered_map<uint64_t, uint8_t> seenPairs;
            seenPairs.reserve(entries.size() * 8u + 1u);

            for (const ParticleEntry& entry : entries)
            {
                const Vec3 p = LoadPoint(asset, entry.particleIndex);
                const uint32_t levelBegin = UsesAdaptiveLevels(mode) ? 0u : entry.level;
                const uint32_t levelEnd = UsesAdaptiveLevels(mode) ? (kAdaptiveLevelCount - 1u) : entry.level;

                for (uint32_t queryLevel = levelBegin; queryLevel <= levelEnd; ++queryLevel)
                {
                    const float queryCellSize = ComputeAdaptiveCellSize(ioStats.cellSize, queryLevel);
                    const Int3Key queryBase = ComputeCellCoord(p, queryCellSize);
                    for (int dz = -1; dz <= 1; ++dz)
                    {
                        for (int dy = -1; dy <= 1; ++dy)
                        {
                            for (int dx = -1; dx <= 1; ++dx)
                            {
                                const CellLookupKey lookup = {
                                    queryLevel,
                                    { queryBase.x + dx, queryBase.y + dy, queryBase.z + dz }
                                };
                                const auto it = bins.find(lookup);
                                if (it == bins.end())
                                {
                                    continue;
                                }

                                for (uint32_t otherEntryIndex : it->second)
                                {
                                    const ParticleEntry& other = entries[otherEntryIndex];
                                    if (!AcceptPair(asset, entry.particleIndex, other.particleIndex, ioStats))
                                    {
                                        continue;
                                    }

                                    const uint64_t pairKey = (static_cast<uint64_t>(entry.particleIndex) << 32ull) | static_cast<uint64_t>(other.particleIndex);
                                    if (!seenPairs.emplace(pairKey, 1u).second)
                                    {
                                        ioStats.duplicateCandidatePairsRemoved += 1u;
                                        continue;
                                    }

                                    ioStats.candidatePairs += 1u;
                                    if (outPairs != nullptr)
                                    {
                                        PairRecord record = {};
                                        record.a = entry.particleIndex;
                                        record.b = other.particleIndex;
                                        record.levelA = entry.level;
                                        record.levelB = other.level;
                                        record.ax = entry.coord.x;
                                        record.ay = entry.coord.y;
                                        record.az = entry.coord.z;
                                        record.bx = other.coord.x;
                                        record.by = other.coord.y;
                                        record.bz = other.coord.z;
                                        record.distance = Distance(LoadPoint(asset, entry.particleIndex), LoadPoint(asset, other.particleIndex));
                                        outPairs->push_back(record);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    bool EvaluateBroadphaseStats(const HairAsset& asset,
                                 float cellSize,
                                 SelfCollisionBroadphaseMode mode,
                                 uint32_t hashBucketCount,
                                 BroadphaseStats& outStats,
                                 std::string& outError)
    {
        outStats = BroadphaseStats{};
        outStats.mode = mode;
        outStats.cellSize = cellSize;
        outStats.strandCount = asset.guideStrandCount;
        outStats.particleCount = static_cast<uint32_t>(asset.restPoints.size());
        outStats.insertedParticles = ComputeInsertedParticleCount(asset);
        outStats.hashBucketCount = hashBucketCount;

        if (asset.guideStrandCount == 0u || asset.particlesPerStrand == 0u)
        {
            outError = "Asset has no guide strands.";
            return false;
        }
        if (asset.restPoints.size() < static_cast<size_t>(asset.guideStrandCount) * asset.particlesPerStrand)
        {
            outError = "Asset point count is smaller than guideStrandCount * particlesPerStrand.";
            return false;
        }
        if (cellSize <= 1.0e-6f)
        {
            outError = "cellSize must be > 0.";
            return false;
        }
        if (UsesHashBuckets(mode) && hashBucketCount == 0u)
        {
            outError = "hashBucketCount must be > 0 for hash-based broadphase modes.";
            return false;
        }

        std::vector<ParticleEntry> entries = BuildEntries(asset, cellSize, mode, hashBucketCount, outStats);
        const auto bins = BuildBinMap(entries);
        outStats.occupiedBins = static_cast<uint32_t>(bins.size());

        if (!bins.empty())
        {
            uint64_t totalOccupancy = 0u;
            for (const auto& entry : bins)
            {
                const uint32_t occupancy = static_cast<uint32_t>(entry.second.size());
                outStats.maxBinOccupancy = std::max(outStats.maxBinOccupancy, occupancy);
                totalOccupancy += occupancy;
            }
            outStats.averageBinOccupancy = static_cast<float>(totalOccupancy) / static_cast<float>(bins.size());
        }

        if (UsesHashBuckets(mode))
        {
            std::vector<uint32_t> bucketOccupancy(hashBucketCount, 0u);
            for (const ParticleEntry& entry : entries)
            {
                if (entry.hashBucket < bucketOccupancy.size())
                {
                    bucketOccupancy[entry.hashBucket] += 1u;
                }
            }

            uint64_t totalHashOccupancy = 0u;
            for (uint32_t occupancy : bucketOccupancy)
            {
                if (occupancy == 0u)
                {
                    continue;
                }
                outStats.usedHashBuckets += 1u;
                outStats.maxHashBucketOccupancy = std::max(outStats.maxHashBucketOccupancy, occupancy);
                totalHashOccupancy += occupancy;
            }
            if (outStats.usedHashBuckets > 0u)
            {
                outStats.averageHashBucketOccupancy = static_cast<float>(totalHashOccupancy) / static_cast<float>(outStats.usedHashBuckets);
            }
        }

        if (UsesRadixSortedRanges(mode))
        {
            std::vector<ParticleEntry> sortedEntries = entries;
            outStats.sortedCellRangeCount = static_cast<uint32_t>(BuildCellRanges(std::move(sortedEntries)).size());
        }

        EvaluateCandidatePairs(asset, entries, bins, mode, outStats, nullptr);
        outError.clear();
        return true;
    }

    bool ExportBroadphaseDebugCsv(const HairAsset& asset,
                                  float cellSize,
                                  SelfCollisionBroadphaseMode mode,
                                  uint32_t hashBucketCount,
                                  const std::filesystem::path& outputCsv,
                                  uint32_t maxPairsToWrite,
                                  std::string& outError)
    {
        BroadphaseStats stats = {};
        if (!EvaluateBroadphaseStats(asset, cellSize, mode, hashBucketCount, stats, outError))
        {
            return false;
        }

        std::vector<ParticleEntry> entries = BuildEntries(asset, cellSize, mode, hashBucketCount, stats);
        auto bins = BuildBinMap(entries);
        std::vector<ParticleEntry> sortedEntries = entries;
        RadixSortParticleEntries(sortedEntries);
        const std::vector<CellRange> ranges = BuildCellRanges(sortedEntries);
        std::vector<PairRecord> pairs;
        if (maxPairsToWrite > 0u)
        {
            BroadphaseStats pairStats = stats;
            pairStats.candidatePairs = 0u;
            pairStats.skippedSameStrandAdjacentPairs = 0u;
            pairStats.skippedSameRootTrianglePairs = 0u;
            pairStats.duplicateCandidatePairsRemoved = 0u;
            EvaluateCandidatePairs(asset, entries, bins, mode, pairStats, &pairs);
            if (pairs.size() > maxPairsToWrite)
            {
                std::partial_sort(pairs.begin(), pairs.begin() + maxPairsToWrite, pairs.end(), [](const PairRecord& a, const PairRecord& b) {
                    if (a.distance != b.distance)
                    {
                        return a.distance < b.distance;
                    }
                    if (a.a != b.a)
                    {
                        return a.a < b.a;
                    }
                    return a.b < b.b;
                });
                pairs.resize(maxPairsToWrite);
            }
            else
            {
                std::sort(pairs.begin(), pairs.end(), [](const PairRecord& a, const PairRecord& b) {
                    if (a.distance != b.distance)
                    {
                        return a.distance < b.distance;
                    }
                    if (a.a != b.a)
                    {
                        return a.a < b.a;
                    }
                    return a.b < b.b;
                });
            }
        }

        std::ofstream out(outputCsv);
        if (!out)
        {
            outError = "Failed to open output CSV: " + outputCsv.string();
            return false;
        }

        out << "recordType,mode,cellSize,occupiedBins,candidatePairs,skippedSameStrand,skippedSameRootTriangle,duplicatePairsRemoved,sortedCellRangeCount,activeAdaptiveLevels,hashBucketCount,usedHashBuckets,maxBinOccupancy,maxHashBucketOccupancy\n";
        out << "summary," << static_cast<uint32_t>(mode) << ',' << stats.cellSize << ',' << stats.occupiedBins << ',' << stats.candidatePairs << ','
            << stats.skippedSameStrandAdjacentPairs << ',' << stats.skippedSameRootTrianglePairs << ',' << stats.duplicateCandidatePairsRemoved << ','
            << stats.sortedCellRangeCount << ',' << stats.activeAdaptiveLevels << ',' << stats.hashBucketCount << ',' << stats.usedHashBuckets << ','
            << stats.maxBinOccupancy << ',' << stats.maxHashBucketOccupancy << "\n\n";

        out << "recordType,level,cellX,cellY,cellZ,hashBucket,sortKey,start,count\n";
        for (const CellRange& range : ranges)
        {
            out << "cell," << range.level << ',' << range.coord.x << ',' << range.coord.y << ',' << range.coord.z << ','
                << range.hashBucket << ',' << range.sortKey << ',' << range.start << ',' << range.count << "\n";
        }

        if (!pairs.empty())
        {
            out << "\nrecordType,particleA,particleB,levelA,levelB,cellAx,cellAy,cellAz,cellBx,cellBy,cellBz,distance\n";
            for (const PairRecord& pair : pairs)
            {
                out << "pair," << pair.a << ',' << pair.b << ',' << pair.levelA << ',' << pair.levelB << ','
                    << pair.ax << ',' << pair.ay << ',' << pair.az << ','
                    << pair.bx << ',' << pair.by << ',' << pair.bz << ',' << pair.distance << "\n";
            }
        }

        outError.clear();
        return true;
    }
}
