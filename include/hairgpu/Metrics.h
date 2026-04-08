#pragma once

#include <cstdint>

#include "hairgpu/Types.h"

namespace hairgpu
{
    struct HairBounds
    {
        Vec3 min = { 0.0f, 0.0f, 0.0f };
        Vec3 max = { 0.0f, 0.0f, 0.0f };
    };

    struct HairAssetMetrics
    {
        uint32_t strandCount = 0;
        uint32_t guideCount = 0;
        uint32_t followerCount = 0;
        uint32_t pointCount = 0;
        uint32_t particlesPerStrand = 0;
        float totalLength = 0.0f;
        float averageLength = 0.0f;
        float maxLength = 0.0f;
        float averageWidth = 0.0f;
        float averageFollowerPerGuide = 0.0f;
        HairBounds bounds = {};
    };



    struct HairShapeComparisonReport
    {
        float rmsPositionError = 0.0f;
        float silhouetteError = 0.0f;
        float clumpPreservationScore = 1.0f;
        float rootLocalityScore = 1.0f;
    };

    struct HairEditComparisonReport
    {
        HairShapeComparisonReport shape = {};
        float editLocalityScore = 1.0f;
        float boundaryNaturalnessScore = 1.0f;
        float editedStrandRatio = 0.0f;
        float editToRunLatencyMilliseconds = 0.0f;
    };

    struct HairLodComparisonReport
    {
        float strandRetentionRatio = 1.0f;
        float particleRetentionRatio = 1.0f;
        float rmsPositionError = 0.0f;
        float maxPositionError = 0.0f;
        float averageLengthDelta = 0.0f;
        float maxLengthDelta = 0.0f;
    };

    HairAssetMetrics ComputeHairAssetMetrics(const HairAsset& asset);
    HairLodComparisonReport CompareHairAssetsResampled(const HairAsset& reference, const HairAsset& candidate, uint32_t samplesPerStrand = 16);
    HairShapeComparisonReport CompareHairShape(const HairAsset& reference, const HairAsset& candidate, uint32_t samplesPerStrand = 16);
    HairEditComparisonReport CompareHairEditRegion(const HairAsset& reference, const HairAsset& edited, uint32_t samplesPerStrand = 16);
}

