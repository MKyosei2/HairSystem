#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/Types.h"

namespace hairgpu
{
    enum class SelfCollisionBroadphaseMode : uint32_t
    {
        DenseGrid = 0,
        SpatialHash = 1,
        AdaptiveSpatialHash = 2,
        SortedGrid = 3,
    };

    struct BroadphaseStats
    {
        SelfCollisionBroadphaseMode mode = SelfCollisionBroadphaseMode::DenseGrid;
        float cellSize = 0.085f;
        uint32_t strandCount = 0;
        uint32_t particleCount = 0;
        uint32_t insertedParticles = 0;
        uint32_t occupiedBins = 0;
        uint32_t candidatePairs = 0;
        uint32_t skippedSameStrandAdjacentPairs = 0;
        uint32_t skippedSameRootTrianglePairs = 0;
        uint32_t duplicateCandidatePairsRemoved = 0;
        uint32_t maxBinOccupancy = 0;
        uint32_t hashBucketCount = 0;
        uint32_t usedHashBuckets = 0;
        uint32_t maxHashBucketOccupancy = 0;
        uint32_t sortedCellRangeCount = 0;
        uint32_t activeAdaptiveLevels = 0;
        uint32_t maxAdaptiveLevel = 0;
        float averageBinOccupancy = 0.0f;
        float averageHashBucketOccupancy = 0.0f;

        // Pre-01 support metrics.
        uint32_t nonFinitePositionCount = 0;
        uint32_t positionFallbackCount = 0;
        uint32_t candidatePairCapacity = 0;
        uint32_t candidatePairOverflowCount = 0;
        uint32_t strandAabbCount = 0;
        uint32_t segmentGroupBlockAabbCount = 0;
    };

    // Existing compatibility API. Uses restPoints.
    bool EvaluateBroadphaseStats(
        const HairAsset& asset,
        float cellSize,
        SelfCollisionBroadphaseMode mode,
        uint32_t hashBucketCount,
        BroadphaseStats& outStats,
        std::string& outError);

    // Current-position API for runtime evaluation.
    bool EvaluateBroadphaseStats(
        const HairAsset& asset,
        const std::vector<Vec3>& currentPositions,
        float cellSize,
        SelfCollisionBroadphaseMode mode,
        uint32_t hashBucketCount,
        uint32_t candidatePairCapacity,
        BroadphaseStats& outStats,
        std::string& outError);

    bool ExportBroadphaseDebugCsv(
        const HairAsset& asset,
        float cellSize,
        SelfCollisionBroadphaseMode mode,
        uint32_t hashBucketCount,
        const std::filesystem::path& outputCsv,
        uint32_t maxPairsToWrite,
        std::string& outError);

    bool ExportBroadphaseDebugCsv(
        const HairAsset& asset,
        const std::vector<Vec3>& currentPositions,
        float cellSize,
        SelfCollisionBroadphaseMode mode,
        uint32_t hashBucketCount,
        uint32_t candidatePairCapacity,
        const std::filesystem::path& outputCsv,
        uint32_t maxPairsToWrite,
        std::string& outError);
}
