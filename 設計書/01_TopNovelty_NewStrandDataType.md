# 01 企画書兼設計書

# 証明付き静的/動的セグメントグループ型ストランド表現

## Segment-Group Certified Strand Data Type

## 0. 対象

* 対象リポジトリ：`MKyosei2/HairSystem`
* 対象ブランチ：`Novelty`
* 対象機能番号：`01`
* 対応Noveltyフラグ：`enableMicrotype`
* 目的：01単体で10万本規模の髪を60FPSに近づけるための、髪専用データ型の追加実装

本設計書では、01を単なるキャッシュ最適化ではなく、**証明付き静的/動的セグメントグループ型ストランド表現**として定義する。
髪一本を単なる点列として扱うのではなく、共有端点を持つ `点-線-点` のセグメントグループ列として扱い、各グループを `StaticCached`、`StaticTransformed`、`Dynamic`、`Deferred`、`Repair` の5状態で管理する。

これにより、静的な髪部分は物理再計算せず、固定端点・方向・長さから再構成する。
一方で、衝突・大きな運動・境界破綻・編集対象などの危険があるグループだけを動的計算または修復に回す。

---

# 1. 01の最終的な新規性定義

本研究の01は、以下のように定義する。

```text
証明付き静的/動的セグメントグループ型ストランド表現
=
髪ストランドを共有端点を持つ点-線-点グループ列として扱い、
各グループを状態付きデータ型として分類し、
静的グループを方向キャッシュで再構成し、
動的グループのみ物理計算し、
Static/Dynamic境界とfalse staticを検出・補正することで、
10万本規模の髪を固定60FPS制約下で扱うための髪専用データ表現。
```

本研究の新規性は、単に「動かない髪をキャッシュする」ことではない。
新規性の核は次の3点である。

1. 髪一本を `点-線-点` のセグメントグループ列として再定義すること。
2. 各グループに `StaticCached / StaticTransformed / Dynamic / Deferred / Repair` の5状態を持たせること。
3. `falseStaticRate`、`Static/Dynamic境界誤差`、`direction cache memory saving` によって、静的再利用の安全性を評価すること。

---

# 2. 最終的に追加するソースファイル

最終的に追加するファイルは以下である。

```text
include/hairgpu/SegmentGroupMicrotype.h
src/hairgpu_core/SegmentGroupMicrotype.cpp

apps/dx11_viewer/shaders/HairMicrotypeClassify.hlsl
apps/dx11_viewer/shaders/HairMicrotypeReconstruct.hlsl
apps/dx11_viewer/shaders/HairMicrotypeBoundaryRepair.hlsl
```

既存ファイルでは、以下に追記する。

```text
HairSystem.vcxproj
include/hairgpu/HairNoveltyTelemetry.h
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl
apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl
apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl
apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl
```

---

# 3. 追加コード：`include/hairgpu/SegmentGroupMicrotype.h`

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include "hairgpu/Math.h"
#include "hairgpu/Types.h"
#include "hairgpu/HairNoveltyTelemetry.h"

namespace hairgpu
{
    enum class SegmentGroupState : uint8_t
    {
        StaticCached = 0,
        StaticTransformed = 1,
        Dynamic = 2,
        Deferred = 3,
        Repair = 4
    };

    struct SegmentGroupMicrotypeSettings
    {
        // State thresholds.
        float staticCachedRiskThreshold = 0.20f;
        float staticTransformedRiskThreshold = 0.35f;
        float deferredRiskThreshold = 0.65f;

        // Motion thresholds.
        float maxStaticRootMotionMm = 1.0f;
        float maxStaticEndpointMotionMm = 1.5f;
        float maxStaticDirectionDeltaDeg = 2.0f;
        float maxStaticCurvatureDeltaDeg = 3.0f;

        // Boundary safety.
        float boundarySoftLimitMm = 2.0f;
        float boundaryHardLimitMm = 6.0f;
        float boundaryCorrectionBlend = 0.35f;

        // Collision / false static.
        float colliderNearDistanceMm = 8.0f;
        float maxPenetrationMm = 1.0f;
        float maxLengthErrorRatio = 0.05f;
        float falseStaticDirectionDeltaDeg = 8.0f;

        // Stability.
        uint32_t repairCooldownFrames = 8u;
        uint32_t groupsPerStrandBlock = 8u;

        // Target ratio for 60 FPS-oriented evaluation.
        float targetMaxDynamicRate = 0.15f;
        float targetMinStaticReuseRate = 0.75f;
        float targetMaxFalseStaticRate = 0.01f;
    };

    struct SegmentGroupMicrotype
    {
        uint32_t strandId = 0u;
        uint32_t groupId = 0u;

        uint32_t startPointIndex = 0u;
        uint32_t endPointIndex = 0u;

        SegmentGroupState state = SegmentGroupState::Dynamic;
        SegmentGroupState previousState = SegmentGroupState::Dynamic;

        float length = 0.0f;
        Vec3 direction = { 0.0f, 1.0f, 0.0f };
        Vec3 previousDirection = { 0.0f, 1.0f, 0.0f };

        float motionScore = 0.0f;
        float collisionRisk = 0.0f;
        float boundaryErrorMm = 0.0f;
        float errorBoundMm = 0.0f;

        uint32_t lastSolvedFrame = 0u;
        uint32_t lastRepairFrame = 0u;

        uint8_t edited = 0u;
        uint8_t colliderNear = 0u;
        uint8_t falseStatic = 0u;
        uint8_t reserved = 0u;
    };

    struct SegmentGroupBlock
    {
        uint32_t firstGroupIndex = 0u;
        uint32_t groupCount = 0u;

        float maxMotionScore = 0.0f;
        float maxCollisionRisk = 0.0f;
        float maxBoundaryErrorMm = 0.0f;

        SegmentGroupState state = SegmentGroupState::StaticCached;
    };

    struct SegmentGroupMicrotypeResult
    {
        std::vector<SegmentGroupMicrotype> groups;
        std::vector<SegmentGroupBlock> blocks;
        HairNoveltyFrameTelemetry telemetry = {};
    };

    struct SegmentGroupMicrotypeFrameInput
    {
        const HairAsset* asset = nullptr;
        const std::vector<Vec3>* currentPoints = nullptr;
        const std::vector<Vec3>* previousPoints = nullptr;

        // Optional per-point or per-group authoring masks.
        const std::vector<uint8_t>* editedPointMask = nullptr;

        uint32_t frameIndex = 0u;
        uint32_t measuredFrameIndex = 0u;

        Vec3 headTranslation = { 0.0f, 0.0f, 0.0f };
        Mat4 headTransform = {};
        Mat4 previousHeadTransform = {};
    };

    SegmentGroupMicrotypeResult BuildSegmentGroupMicrotypes(
        const HairAsset& asset,
        const std::vector<Vec3>& currentPoints,
        const std::vector<Vec3>& previousPoints,
        const SegmentGroupMicrotypeSettings& settings,
        uint32_t frameIndex);

    void EvaluateSegmentGroupMicrotypes(
        SegmentGroupMicrotypeResult& result,
        const SegmentGroupMicrotypeFrameInput& input,
        const SegmentGroupMicrotypeSettings& settings);

    void ReconstructStaticSegmentGroups(
        SegmentGroupMicrotypeResult& result,
        std::vector<Vec3>& outputPoints,
        const SegmentGroupMicrotypeFrameInput& input,
        const SegmentGroupMicrotypeSettings& settings);

    void RepairSegmentGroupBoundaries(
        SegmentGroupMicrotypeResult& result,
        std::vector<Vec3>& outputPoints,
        const SegmentGroupMicrotypeSettings& settings);

    HairNoveltyFrameTelemetry BuildMicrotypeTelemetry(
        const SegmentGroupMicrotypeResult& result,
        const SegmentGroupMicrotypeSettings& settings,
        uint32_t frameIndex,
        uint32_t measuredFrameIndex);
}
```

---

# 4. 追加コード：`src/hairgpu_core/SegmentGroupMicrotype.cpp`

```cpp
#include "hairgpu/SegmentGroupMicrotype.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hairgpu
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        float SafeLength(const Vec3& v)
        {
            return std::max(Length(v), 1.0e-6f);
        }

        float DistanceMm(const Vec3& a, const Vec3& b)
        {
            return Length(a - b) * 1000.0f;
        }

        float DirectionDeltaDeg(const Vec3& a, const Vec3& b)
        {
            const Vec3 na = Normalize(a);
            const Vec3 nb = Normalize(b);
            const float d = Clamp(Dot(na, nb), -1.0f, 1.0f);
            return std::acos(d) * 180.0f / kPi;
        }

        float SafeRatio(float a, float b)
        {
            return a / std::max(b, 1.0e-6f);
        }

        SegmentGroupState ClassifyByRisk(float risk, const SegmentGroupMicrotypeSettings& settings)
        {
            if (risk < settings.staticCachedRiskThreshold)
            {
                return SegmentGroupState::StaticCached;
            }
            if (risk < settings.staticTransformedRiskThreshold)
            {
                return SegmentGroupState::StaticTransformed;
            }
            if (risk < settings.deferredRiskThreshold)
            {
                return SegmentGroupState::Deferred;
            }
            return SegmentGroupState::Dynamic;
        }

        bool IsStaticState(SegmentGroupState state)
        {
            return state == SegmentGroupState::StaticCached ||
                   state == SegmentGroupState::StaticTransformed;
        }

        float StateWeight(SegmentGroupState state)
        {
            switch (state)
            {
            case SegmentGroupState::StaticCached: return 0.0f;
            case SegmentGroupState::StaticTransformed: return 0.15f;
            case SegmentGroupState::Deferred: return 0.45f;
            case SegmentGroupState::Dynamic: return 1.0f;
            case SegmentGroupState::Repair: return 1.0f;
            default: return 1.0f;
            }
        }
    }

    SegmentGroupMicrotypeResult BuildSegmentGroupMicrotypes(
        const HairAsset& asset,
        const std::vector<Vec3>& currentPoints,
        const std::vector<Vec3>& previousPoints,
        const SegmentGroupMicrotypeSettings& settings,
        uint32_t frameIndex)
    {
        SegmentGroupMicrotypeResult result;

        if (currentPoints.empty() || previousPoints.empty())
        {
            return result;
        }

        result.groups.reserve(currentPoints.size());

        uint32_t globalGroupId = 0u;

        for (uint32_t strandId = 0u; strandId < static_cast<uint32_t>(asset.strands.size()); ++strandId)
        {
            const StrandRange& strand = asset.strands[strandId];

            if (strand.pointCount < 2u)
            {
                continue;
            }

            const uint32_t firstPoint = strand.pointOffset;
            const uint32_t lastPointExclusive = strand.pointOffset + strand.pointCount;

            if (lastPointExclusive > currentPoints.size() ||
                lastPointExclusive > previousPoints.size())
            {
                continue;
            }

            const uint32_t firstGroupForStrand = static_cast<uint32_t>(result.groups.size());

            for (uint32_t pointIndex = firstPoint; pointIndex + 1u < lastPointExclusive; ++pointIndex)
            {
                const Vec3 start = currentPoints[pointIndex];
                const Vec3 end = currentPoints[pointIndex + 1u];

                const Vec3 prevStart = previousPoints[pointIndex];
                const Vec3 prevEnd = previousPoints[pointIndex + 1u];

                const Vec3 segment = end - start;
                const Vec3 prevSegment = prevEnd - prevStart;

                SegmentGroupMicrotype group;
                group.strandId = strandId;
                group.groupId = globalGroupId++;
                group.startPointIndex = pointIndex;
                group.endPointIndex = pointIndex + 1u;
                group.length = SafeLength(segment);
                group.direction = Normalize(segment);
                group.previousDirection = Normalize(prevSegment);
                group.previousState = SegmentGroupState::Dynamic;
                group.state = SegmentGroupState::Dynamic;
                group.lastSolvedFrame = frameIndex;

                result.groups.push_back(group);
            }

            const uint32_t groupCountForStrand =
                static_cast<uint32_t>(result.groups.size()) - firstGroupForStrand;

            if (groupCountForStrand > 0u)
            {
                for (uint32_t blockStart = 0u;
                     blockStart < groupCountForStrand;
                     blockStart += std::max(settings.groupsPerStrandBlock, 1u))
                {
                    SegmentGroupBlock block;
                    block.firstGroupIndex = firstGroupForStrand + blockStart;
                    block.groupCount = std::min(
                        settings.groupsPerStrandBlock,
                        groupCountForStrand - blockStart);
                    result.blocks.push_back(block);
                }
            }
        }

        result.telemetry = BuildMicrotypeTelemetry(
            result,
            settings,
            frameIndex,
            frameIndex);

        return result;
    }

    void EvaluateSegmentGroupMicrotypes(
        SegmentGroupMicrotypeResult& result,
        const SegmentGroupMicrotypeFrameInput& input,
        const SegmentGroupMicrotypeSettings& settings)
    {
        if (!input.currentPoints || !input.previousPoints)
        {
            return;
        }

        const std::vector<Vec3>& currentPoints = *input.currentPoints;
        const std::vector<Vec3>& previousPoints = *input.previousPoints;

        for (SegmentGroupBlock& block : result.blocks)
        {
            block.maxMotionScore = 0.0f;
            block.maxCollisionRisk = 0.0f;
            block.maxBoundaryErrorMm = 0.0f;

            for (uint32_t i = 0u; i < block.groupCount; ++i)
            {
                const uint32_t groupIndex = block.firstGroupIndex + i;
                if (groupIndex >= result.groups.size())
                {
                    continue;
                }

                const SegmentGroupMicrotype& g = result.groups[groupIndex];
                block.maxMotionScore = std::max(block.maxMotionScore, g.motionScore);
                block.maxCollisionRisk = std::max(block.maxCollisionRisk, g.collisionRisk);
                block.maxBoundaryErrorMm = std::max(block.maxBoundaryErrorMm, g.boundaryErrorMm);
            }

            const float blockRisk = std::max(
                block.maxMotionScore,
                std::max(block.maxCollisionRisk, block.maxBoundaryErrorMm / settings.boundaryHardLimitMm));

            block.state = ClassifyByRisk(blockRisk, settings);
        }

        for (SegmentGroupMicrotype& group : result.groups)
        {
            if (group.startPointIndex >= currentPoints.size() ||
                group.endPointIndex >= currentPoints.size() ||
                group.startPointIndex >= previousPoints.size() ||
                group.endPointIndex >= previousPoints.size())
            {
                continue;
            }

            const Vec3 currentStart = currentPoints[group.startPointIndex];
            const Vec3 currentEnd = currentPoints[group.endPointIndex];
            const Vec3 previousStart = previousPoints[group.startPointIndex];
            const Vec3 previousEnd = previousPoints[group.endPointIndex];

            const float rootMotionMm = DistanceMm(currentStart, previousStart);
            const float endpointMotionMm = DistanceMm(currentEnd, previousEnd);

            const Vec3 currentSegment = currentEnd - currentStart;
            const Vec3 previousSegment = previousEnd - previousStart;

            const float currentLength = SafeLength(currentSegment);
            const float previousLength = SafeLength(previousSegment);

            const Vec3 currentDirection = Normalize(currentSegment);
            const Vec3 previousDirection = Normalize(previousSegment);

            const float directionDeltaDeg = DirectionDeltaDeg(currentDirection, previousDirection);
            const float lengthErrorRatio = std::fabs(currentLength - previousLength) / std::max(previousLength, 1.0e-6f);

            const float rootMotionScore = Clamp(rootMotionMm / settings.maxStaticRootMotionMm, 0.0f, 1.0f);
            const float endpointMotionScore = Clamp(endpointMotionMm / settings.maxStaticEndpointMotionMm, 0.0f, 1.0f);
            const float directionScore = Clamp(directionDeltaDeg / settings.maxStaticDirectionDeltaDeg, 0.0f, 1.0f);
            const float lengthScore = Clamp(lengthErrorRatio / settings.maxLengthErrorRatio, 0.0f, 1.0f);

            group.previousState = group.state;
            group.previousDirection = group.direction;
            group.direction = currentDirection;
            group.length = currentLength;

            group.motionScore =
                rootMotionScore * 0.35f +
                endpointMotionScore * 0.25f +
                directionScore * 0.20f +
                lengthScore * 0.20f;

            group.collisionRisk = group.colliderNear ? 1.0f : 0.0f;

            const float boundaryRisk = Clamp(
                group.boundaryErrorMm / settings.boundaryHardLimitMm,
                0.0f,
                1.0f);

            const float editRisk = group.edited ? 1.0f : 0.0f;

            const float risk = std::max(
                group.motionScore,
                std::max(group.collisionRisk, std::max(boundaryRisk, editRisk)));

            group.state = ClassifyByRisk(risk, settings);

            const bool coolingDown =
                input.frameIndex > group.lastRepairFrame &&
                (input.frameIndex - group.lastRepairFrame) < settings.repairCooldownFrames;

            if (coolingDown && IsStaticState(group.state))
            {
                group.state = SegmentGroupState::Deferred;
            }

            const bool falseStatic =
                IsStaticState(group.previousState) &&
                (
                    group.boundaryErrorMm > settings.boundaryHardLimitMm ||
                    directionDeltaDeg > settings.falseStaticDirectionDeltaDeg ||
                    lengthErrorRatio > settings.maxLengthErrorRatio
                );

            group.falseStatic = falseStatic ? 1u : 0u;

            if (falseStatic)
            {
                group.state = SegmentGroupState::Repair;
                group.lastRepairFrame = input.frameIndex;
            }

            if (group.state == SegmentGroupState::Dynamic ||
                group.state == SegmentGroupState::Repair)
            {
                group.lastSolvedFrame = input.frameIndex;
            }
        }

        result.telemetry = BuildMicrotypeTelemetry(
            result,
            settings,
            input.frameIndex,
            input.measuredFrameIndex);
    }

    void ReconstructStaticSegmentGroups(
        SegmentGroupMicrotypeResult& result,
        std::vector<Vec3>& outputPoints,
        const SegmentGroupMicrotypeFrameInput& input,
        const SegmentGroupMicrotypeSettings& settings)
    {
        (void)settings;

        if (!input.currentPoints)
        {
            return;
        }

        if (outputPoints.size() != input.currentPoints->size())
        {
            outputPoints = *input.currentPoints;
        }

        for (const SegmentGroupMicrotype& group : result.groups)
        {
            if (!IsStaticState(group.state))
            {
                continue;
            }

            if (group.startPointIndex >= outputPoints.size() ||
                group.endPointIndex >= outputPoints.size())
            {
                continue;
            }

            const Vec3 start = outputPoints[group.startPointIndex];

            if (group.state == SegmentGroupState::StaticCached)
            {
                outputPoints[group.endPointIndex] = start + group.direction * group.length;
            }
            else if (group.state == SegmentGroupState::StaticTransformed)
            {
                const Vec3 transformedStart = TransformPoint(input.headTransform, start);
                const Vec3 transformedEnd = transformedStart + group.direction * group.length;
                outputPoints[group.startPointIndex] = transformedStart;
                outputPoints[group.endPointIndex] = transformedEnd;
            }
        }
    }

    void RepairSegmentGroupBoundaries(
        SegmentGroupMicrotypeResult& result,
        std::vector<Vec3>& outputPoints,
        const SegmentGroupMicrotypeSettings& settings)
    {
        if (result.groups.size() < 2u)
        {
            return;
        }

        for (size_t i = 0; i + 1u < result.groups.size(); ++i)
        {
            SegmentGroupMicrotype& left = result.groups[i];
            SegmentGroupMicrotype& right = result.groups[i + 1u];

            if (left.strandId != right.strandId)
            {
                continue;
            }

            const bool leftStatic = IsStaticState(left.state);
            const bool rightStatic = IsStaticState(right.state);

            if (leftStatic == rightStatic)
            {
                continue;
            }

            if (left.endPointIndex >= outputPoints.size() ||
                right.startPointIndex >= outputPoints.size() ||
                left.startPointIndex >= outputPoints.size())
            {
                continue;
            }

            const Vec3 leftStart = outputPoints[left.startPointIndex];
            const Vec3 leftEnd = outputPoints[left.endPointIndex];
            const Vec3 rightStart = outputPoints[right.startPointIndex];

            const float errorMm = DistanceMm(leftEnd, rightStart);

            left.boundaryErrorMm = std::max(left.boundaryErrorMm, errorMm);
            right.boundaryErrorMm = std::max(right.boundaryErrorMm, errorMm);

            if (errorMm > settings.boundarySoftLimitMm)
            {
                if (leftStatic)
                {
                    const Vec3 targetDirection = Normalize(rightStart - leftStart);
                    left.direction = Normalize(Lerp(left.direction, targetDirection, settings.boundaryCorrectionBlend));
                    outputPoints[left.endPointIndex] = leftStart + left.direction * left.length;
                }
                else if (rightStatic)
                {
                    outputPoints[right.startPointIndex] = leftEnd;
                }
            }

            if (errorMm > settings.boundaryHardLimitMm)
            {
                if (leftStatic)
                {
                    left.state = SegmentGroupState::Repair;
                    left.lastRepairFrame += 1u;
                }
                if (rightStatic)
                {
                    right.state = SegmentGroupState::Repair;
                    right.lastRepairFrame += 1u;
                }
            }
        }

        result.telemetry = BuildMicrotypeTelemetry(
            result,
            settings,
            result.telemetry.frameIndex,
            result.telemetry.measuredFrameIndex);
    }

    HairNoveltyFrameTelemetry BuildMicrotypeTelemetry(
        const SegmentGroupMicrotypeResult& result,
        const SegmentGroupMicrotypeSettings& settings,
        uint32_t frameIndex,
        uint32_t measuredFrameIndex)
    {
        HairNoveltyFrameTelemetry telemetry = {};
        telemetry.frameIndex = frameIndex;
        telemetry.measuredFrameIndex = measuredFrameIndex;
        telemetry.topLevelNoveltyMask = 1u << 0;

        telemetry.totalSegmentGroupCount = static_cast<uint32_t>(result.groups.size());

        float boundaryErrorSum = 0.0f;
        float boundaryErrorMax = 0.0f;
        uint32_t boundaryErrorCount = 0u;

        for (const SegmentGroupMicrotype& group : result.groups)
        {
            switch (group.state)
            {
            case SegmentGroupState::StaticCached:
                ++telemetry.staticCachedGroupCount;
                break;
            case SegmentGroupState::StaticTransformed:
                ++telemetry.staticTransformedGroupCount;
                break;
            case SegmentGroupState::Dynamic:
                ++telemetry.dynamicGroupCount;
                break;
            case SegmentGroupState::Deferred:
                ++telemetry.deferredCorrectionGroupCount;
                break;
            case SegmentGroupState::Repair:
                ++telemetry.repairRequiredGroupCount;
                break;
            default:
                ++telemetry.fallbackGroupCount;
                break;
            }

            if (group.falseStatic)
            {
                ++telemetry.falseStaticCount;
            }

            if (group.boundaryErrorMm > 0.0f)
            {
                boundaryErrorSum += group.boundaryErrorMm;
                boundaryErrorMax = std::max(boundaryErrorMax, group.boundaryErrorMm);
                ++boundaryErrorCount;
            }
        }

        telemetry.falseStaticRate =
            SafeRatio(
                static_cast<float>(telemetry.falseStaticCount),
                static_cast<float>(std::max(telemetry.totalSegmentGroupCount, 1u)));

        telemetry.staticDynamicBoundaryMeanErrorMm =
            boundaryErrorCount > 0u
                ? boundaryErrorSum / static_cast<float>(boundaryErrorCount)
                : 0.0f;

        telemetry.staticDynamicBoundaryMaxErrorMm = boundaryErrorMax;

        const float fullPositionBytes =
            static_cast<float>(telemetry.totalSegmentGroupCount) *
            2.0f *
            3.0f *
            sizeof(float);

        const float directionCacheBytes =
            static_cast<float>(telemetry.totalSegmentGroupCount) *
            4.0f *
            sizeof(float);

        telemetry.fullPositionMemoryMb = fullPositionBytes / (1024.0f * 1024.0f);
        telemetry.directionCacheMemoryMb = directionCacheBytes / (1024.0f * 1024.0f);

        telemetry.directionCacheMemorySavingRate =
            1.0f - SafeRatio(telemetry.directionCacheMemoryMb, telemetry.fullPositionMemoryMb);

        const float staticReuseRate =
            SafeRatio(
                static_cast<float>(telemetry.staticCachedGroupCount + telemetry.staticTransformedGroupCount),
                static_cast<float>(std::max(telemetry.totalSegmentGroupCount, 1u)));

        const float dynamicRate =
            SafeRatio(
                static_cast<float>(telemetry.dynamicGroupCount),
                static_cast<float>(std::max(telemetry.totalSegmentGroupCount, 1u)));

        if (staticReuseRate < settings.targetMinStaticReuseRate)
        {
            ++telemetry.budgetDropCount;
        }

        if (dynamicRate > settings.targetMaxDynamicRate)
        {
            ++telemetry.deferCount;
        }

        if (telemetry.falseStaticRate > settings.targetMaxFalseStaticRate)
        {
            ++telemetry.fallbackCount;
        }

        telemetry.microtypeDecodedCount = telemetry.totalSegmentGroupCount;
        telemetry.safeDecodeCount =
            telemetry.staticCachedGroupCount +
            telemetry.staticTransformedGroupCount;

        return telemetry;
    }
}
```

---

# 5. 追加コード：`apps/dx11_viewer/shaders/HairMicrotypeClassify.hlsl`

```hlsl
struct MicrotypeGroup
{
    uint startPointIndex;
    uint endPointIndex;
    uint state;
    uint flags;

    float4 directionLength;
    float4 previousDirectionError;
    float4 riskScores;
};

RWStructuredBuffer<MicrotypeGroup> gGroups : register(u0);
StructuredBuffer<float4> gCurrentPositions : register(t0);
StructuredBuffer<float4> gPreviousPositions : register(t1);

cbuffer MicrotypeClassifyConstants : register(b0)
{
    uint gGroupCount;
    uint gFrameIndex;
    float gStaticCachedRiskThreshold;
    float gStaticTransformedRiskThreshold;

    float gDeferredRiskThreshold;
    float gBoundaryHardLimitMm;
    float gMaxStaticEndpointMotionMm;
    float gMaxStaticDirectionDeltaDeg;

    float gMaxLengthErrorRatio;
    float gFalseStaticDirectionDeltaDeg;
    float2 gPadding;
};

static const uint STATE_STATIC_CACHED = 0;
static const uint STATE_STATIC_TRANSFORMED = 1;
static const uint STATE_DYNAMIC = 2;
static const uint STATE_DEFERRED = 3;
static const uint STATE_REPAIR = 4;

float SafeLength3(float3 v)
{
    return max(length(v), 1.0e-6);
}

float DirectionDeltaDeg(float3 a, float3 b)
{
    float3 na = normalize(a);
    float3 nb = normalize(b);
    float d = clamp(dot(na, nb), -1.0, 1.0);
    return degrees(acos(d));
}

[numthreads(128, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint id = dispatchThreadId.x;
    if (id >= gGroupCount)
    {
        return;
    }

    MicrotypeGroup group = gGroups[id];

    float3 currentStart = gCurrentPositions[group.startPointIndex].xyz;
    float3 currentEnd = gCurrentPositions[group.endPointIndex].xyz;
    float3 previousStart = gPreviousPositions[group.startPointIndex].xyz;
    float3 previousEnd = gPreviousPositions[group.endPointIndex].xyz;

    float3 currentSegment = currentEnd - currentStart;
    float3 previousSegment = previousEnd - previousStart;

    float currentLength = SafeLength3(currentSegment);
    float previousLength = SafeLength3(previousSegment);

    float3 currentDirection = currentSegment / currentLength;
    float3 previousDirection = previousSegment / previousLength;

    float endpointMotionMm = length(currentEnd - previousEnd) * 1000.0;
    float directionDeltaDeg = DirectionDeltaDeg(currentDirection, previousDirection);
    float lengthErrorRatio = abs(currentLength - previousLength) / max(previousLength, 1.0e-6);

    float endpointScore = saturate(endpointMotionMm / gMaxStaticEndpointMotionMm);
    float directionScore = saturate(directionDeltaDeg / gMaxStaticDirectionDeltaDeg);
    float lengthScore = saturate(lengthErrorRatio / gMaxLengthErrorRatio);

    float boundaryErrorMm = group.previousDirectionError.w;
    float boundaryRisk = saturate(boundaryErrorMm / gBoundaryHardLimitMm);

    float motionScore =
        endpointScore * 0.45 +
        directionScore * 0.35 +
        lengthScore * 0.20;

    float editRisk = (group.flags & 1u) ? 1.0 : 0.0;
    float colliderRisk = (group.flags & 2u) ? 1.0 : 0.0;

    float risk = max(motionScore, max(boundaryRisk, max(editRisk, colliderRisk)));

    uint previousState = group.state;
    uint nextState = STATE_DYNAMIC;

    if (risk < gStaticCachedRiskThreshold)
    {
        nextState = STATE_STATIC_CACHED;
    }
    else if (risk < gStaticTransformedRiskThreshold)
    {
        nextState = STATE_STATIC_TRANSFORMED;
    }
    else if (risk < gDeferredRiskThreshold)
    {
        nextState = STATE_DEFERRED;
    }
    else
    {
        nextState = STATE_DYNAMIC;
    }

    bool wasStatic =
        previousState == STATE_STATIC_CACHED ||
        previousState == STATE_STATIC_TRANSFORMED;

    bool falseStatic =
        wasStatic &&
        (
            boundaryErrorMm > gBoundaryHardLimitMm ||
            directionDeltaDeg > gFalseStaticDirectionDeltaDeg ||
            lengthErrorRatio > gMaxLengthErrorRatio
        );

    if (falseStatic)
    {
        nextState = STATE_REPAIR;
        group.flags |= 4u;
    }

    group.state = nextState;
    group.directionLength = float4(currentDirection, currentLength);
    group.previousDirectionError.xyz = previousDirection;
    group.riskScores = float4(motionScore, colliderRisk, boundaryRisk, risk);

    gGroups[id] = group;
}
```

---

# 6. 追加コード：`apps/dx11_viewer/shaders/HairMicrotypeReconstruct.hlsl`

```hlsl
struct MicrotypeGroup
{
    uint startPointIndex;
    uint endPointIndex;
    uint state;
    uint flags;

    float4 directionLength;
    float4 previousDirectionError;
    float4 riskScores;
};

StructuredBuffer<MicrotypeGroup> gGroups : register(t0);
RWStructuredBuffer<float4> gPositions : register(u0);

cbuffer MicrotypeReconstructConstants : register(b0)
{
    uint gGroupCount;
    uint gEnableStaticTransform;
    float2 gPadding0;

    float4x4 gHeadTransform;
};

static const uint STATE_STATIC_CACHED = 0;
static const uint STATE_STATIC_TRANSFORMED = 1;

[numthreads(128, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint id = dispatchThreadId.x;
    if (id >= gGroupCount)
    {
        return;
    }

    MicrotypeGroup group = gGroups[id];

    if (group.state != STATE_STATIC_CACHED &&
        group.state != STATE_STATIC_TRANSFORMED)
    {
        return;
    }

    float3 start = gPositions[group.startPointIndex].xyz;
    float3 direction = normalize(group.directionLength.xyz);
    float segmentLength = group.directionLength.w;

    if (group.state == STATE_STATIC_TRANSFORMED && gEnableStaticTransform != 0)
    {
        float4 transformedStart = mul(float4(start, 1.0), gHeadTransform);
        start = transformedStart.xyz;
        gPositions[group.startPointIndex] = float4(start, 1.0);
    }

    float3 endPosition = start + direction * segmentLength;
    gPositions[group.endPointIndex] = float4(endPosition, 1.0);
}
```

---

# 7. 追加コード：`apps/dx11_viewer/shaders/HairMicrotypeBoundaryRepair.hlsl`

```hlsl
struct MicrotypeGroup
{
    uint startPointIndex;
    uint endPointIndex;
    uint state;
    uint flags;

    float4 directionLength;
    float4 previousDirectionError;
    float4 riskScores;
};

RWStructuredBuffer<MicrotypeGroup> gGroups : register(u0);
RWStructuredBuffer<float4> gPositions : register(u1);

cbuffer MicrotypeBoundaryRepairConstants : register(b0)
{
    uint gGroupCount;
    float gBoundarySoftLimitMm;
    float gBoundaryHardLimitMm;
    float gBoundaryCorrectionBlend;
};

static const uint STATE_STATIC_CACHED = 0;
static const uint STATE_STATIC_TRANSFORMED = 1;
static const uint STATE_DYNAMIC = 2;
static const uint STATE_DEFERRED = 3;
static const uint STATE_REPAIR = 4;

bool IsStaticState(uint state)
{
    return state == STATE_STATIC_CACHED ||
           state == STATE_STATIC_TRANSFORMED;
}

[numthreads(128, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint id = dispatchThreadId.x;
    if (id + 1 >= gGroupCount)
    {
        return;
    }

    MicrotypeGroup left = gGroups[id];
    MicrotypeGroup right = gGroups[id + 1];

    bool leftStatic = IsStaticState(left.state);
    bool rightStatic = IsStaticState(right.state);

    if (leftStatic == rightStatic)
    {
        return;
    }

    if (left.endPointIndex != right.startPointIndex)
    {
        return;
    }

    float3 leftStart = gPositions[left.startPointIndex].xyz;
    float3 leftEnd = gPositions[left.endPointIndex].xyz;
    float3 rightStart = gPositions[right.startPointIndex].xyz;

    float errorMm = length(leftEnd - rightStart) * 1000.0;

    left.previousDirectionError.w = max(left.previousDirectionError.w, errorMm);
    right.previousDirectionError.w = max(right.previousDirectionError.w, errorMm);

    if (errorMm > gBoundarySoftLimitMm)
    {
        if (leftStatic)
        {
            float3 targetDirection = normalize(rightStart - leftStart);
            float3 correctedDirection = normalize(lerp(
                normalize(left.directionLength.xyz),
                targetDirection,
                gBoundaryCorrectionBlend));

            left.directionLength.xyz = correctedDirection;
            gPositions[left.endPointIndex] = float4(
                leftStart + correctedDirection * left.directionLength.w,
                1.0);
        }
        else if (rightStatic)
        {
            gPositions[right.startPointIndex] = float4(leftEnd, 1.0);
        }
    }

    if (errorMm > gBoundaryHardLimitMm)
    {
        if (leftStatic)
        {
            left.state = STATE_REPAIR;
        }

        if (rightStatic)
        {
            right.state = STATE_REPAIR;
        }
    }

    gGroups[id] = left;
    gGroups[id + 1] = right;
}
```

---

# 8. `HairSystem.vcxproj` に追加する項目

現在のプロジェクトでは `src\hairgpu_core\*.cpp` と `include\hairgpu\*.h` が明示的に並んでいるため、01の新規ファイルも追加する。

`<ClCompile>` の一覧に以下を追加する。

```xml
<ClCompile Include="src\hairgpu_core\SegmentGroupMicrotype.cpp" />
```

`<ClInclude>` の一覧に以下を追加する。

```xml
<ClInclude Include="include\hairgpu\SegmentGroupMicrotype.h" />
```

`<None>` のHLSL一覧に以下を追加する。

```xml
<None Include="apps\dx11_viewer\shaders\HairMicrotypeClassify.hlsl" />
<None Include="apps\dx11_viewer\shaders\HairMicrotypeReconstruct.hlsl" />
<None Include="apps\dx11_viewer\shaders\HairMicrotypeBoundaryRepair.hlsl" />
```

---

# 9. `GpuHairViewer.h` に追加するメンバ

`GpuHairViewer.h` に以下のメンバを追加する。

```cpp
// 01: Segment-Group Certified Strand Data Type
bool m_enableMicrotypeRuntime = false;

hairgpu::SegmentGroupMicrotypeSettings m_microtypeSettings;
hairgpu::SegmentGroupMicrotypeResult m_microtypeResult;
std::vector<hairgpu::Vec3> m_microtypeOutputPoints;

Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_microtypeClassifyShader;
Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_microtypeReconstructShader;
Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_microtypeBoundaryRepairShader;

Microsoft::WRL::ComPtr<ID3D11Buffer> m_microtypeGroupBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> m_microtypeGroupUavBuffer;
Microsoft::WRL::ComPtr<ID3D11Buffer> m_microtypeStatsBuffer;

uint32_t m_microtypeGroupCount = 0u;
```

既存の `PerformanceSample` に以下がまだない場合は追加する。

```cpp
uint32_t totalSegmentGroupCount = 0u;
uint32_t staticCachedGroupCount = 0u;
uint32_t staticTransformedGroupCount = 0u;
uint32_t dynamicGroupCount = 0u;
uint32_t deferredCorrectionGroupCount = 0u;
uint32_t repairRequiredGroupCount = 0u;
uint32_t fallbackGroupCount = 0u;

uint32_t falseStaticCount = 0u;
float falseStaticRate = 0.0f;

float staticDynamicBoundaryMeanErrorMm = 0.0f;
float staticDynamicBoundaryMaxErrorMm = 0.0f;

float directionCacheMemoryMb = 0.0f;
float fullPositionMemoryMb = 0.0f;
float directionCacheMemorySavingRate = 0.0f;
```

---

# 10. `03_simulation_reset_and_frame_step.inl` に追加する処理

既存の毎フレーム更新処理の中で、`enableMicrotype` がONの場合に以下を実行する。

```cpp
if (m_experimentCase.enableMicrotype)
{
    m_enableMicrotypeRuntime = true;

    hairgpu::SegmentGroupMicrotypeFrameInput microtypeInput;
    microtypeInput.asset = &m_asset;
    microtypeInput.currentPoints = &m_currentCpuPoints;
    microtypeInput.previousPoints = &m_previousCpuPoints;
    microtypeInput.frameIndex = m_globalFrameIndex;
    microtypeInput.measuredFrameIndex = m_measuredFrameIndex;
    microtypeInput.headTransform = m_currentHeadTransform;
    microtypeInput.previousHeadTransform = m_previousHeadTransform;

    if (m_microtypeResult.groups.empty())
    {
        m_microtypeResult = hairgpu::BuildSegmentGroupMicrotypes(
            m_asset,
            m_currentCpuPoints,
            m_previousCpuPoints,
            m_microtypeSettings,
            m_globalFrameIndex);

        m_microtypeOutputPoints = m_currentCpuPoints;
        m_microtypeGroupCount = static_cast<uint32_t>(m_microtypeResult.groups.size());
    }

    hairgpu::EvaluateSegmentGroupMicrotypes(
        m_microtypeResult,
        microtypeInput,
        m_microtypeSettings);

    hairgpu::ReconstructStaticSegmentGroups(
        m_microtypeResult,
        m_microtypeOutputPoints,
        microtypeInput,
        m_microtypeSettings);

    hairgpu::RepairSegmentGroupBoundaries(
        m_microtypeResult,
        m_microtypeOutputPoints,
        m_microtypeSettings);

    m_noveltyTelemetry = m_microtypeResult.telemetry;
}
```

注意点として、上の `m_currentCpuPoints`、`m_previousCpuPoints`、`m_currentHeadTransform` などは、実際のViewer側で使われている既存名に合わせる。
既存名が異なる場合は、以下の意味を持つ変数へ置き換える。

```text
m_currentCpuPoints      = 現在フレームの髪点列
m_previousCpuPoints     = 前フレームの髪点列
m_currentHeadTransform  = 現在フレームの頭部変換
m_previousHeadTransform = 前フレームの頭部変換
```

01単体評価では、以下の状態にする。

```text
enableMicrotype = ON
enableBudgetedPhysics = OFF
enableCertificateRender = OFF
enablePerStrandEdit = OFF
enableGpuResident = OFF
```

---

# 11. Telemetryへ反映するコード

`10_phase1_telemetry_csv_jsonl_summary.inl` またはPerformanceSampleを作る箇所で、以下を入れる。

```cpp
sample.totalSegmentGroupCount = m_noveltyTelemetry.totalSegmentGroupCount;
sample.staticCachedGroupCount = m_noveltyTelemetry.staticCachedGroupCount;
sample.staticTransformedGroupCount = m_noveltyTelemetry.staticTransformedGroupCount;
sample.dynamicGroupCount = m_noveltyTelemetry.dynamicGroupCount;
sample.deferredCorrectionGroupCount = m_noveltyTelemetry.deferredCorrectionGroupCount;
sample.repairRequiredGroupCount = m_noveltyTelemetry.repairRequiredGroupCount;
sample.fallbackGroupCount = m_noveltyTelemetry.fallbackGroupCount;

sample.falseStaticCount = m_noveltyTelemetry.falseStaticCount;
sample.falseStaticRate = m_noveltyTelemetry.falseStaticRate;

sample.staticDynamicBoundaryMeanErrorMm =
    m_noveltyTelemetry.staticDynamicBoundaryMeanErrorMm;

sample.staticDynamicBoundaryMaxErrorMm =
    m_noveltyTelemetry.staticDynamicBoundaryMaxErrorMm;

sample.directionCacheMemoryMb =
    m_noveltyTelemetry.directionCacheMemoryMb;

sample.fullPositionMemoryMb =
    m_noveltyTelemetry.fullPositionMemoryMb;

sample.directionCacheMemorySavingRate =
    m_noveltyTelemetry.directionCacheMemorySavingRate;
```

---

# 12. CSVに必ず出す列

最終評価CSVには、少なくとも以下の列を出す。

```text
totalSegmentGroupCount
staticCachedGroupCount
staticTransformedGroupCount
dynamicGroupCount
deferredCorrectionGroupCount
repairRequiredGroupCount
fallbackGroupCount
falseStaticCount
falseStaticRate
staticDynamicBoundaryMeanErrorMm
staticDynamicBoundaryMaxErrorMm
directionCacheMemoryMb
fullPositionMemoryMb
directionCacheMemorySavingRate
```

01単体で60FPSを評価する場合、特に重要なのは以下である。

```text
frameMilliseconds
fps
totalSegmentGroupCount
staticCachedGroupCount
staticTransformedGroupCount
dynamicGroupCount
falseStaticRate
staticDynamicBoundaryMeanErrorMm
directionCacheMemorySavingRate
```

---

# 13. 01単体60FPSの成功条件

10万本の髪を対象に、01単体で60FPSを狙う場合の成功条件は以下である。

```text
髪本数: 100,000
points per strand: 16を基本
p95 frame time <= 16.67ms
average FPS >= 60
StaticCached + StaticTransformed >= 75%
Dynamic <= 15%
Repair <= 3%
falseStaticRate <= 1%
staticDynamicBoundaryMeanErrorMm <= 2.0mm
staticDynamicBoundaryMaxErrorMm <= 6.0mm
directionCacheMemorySavingRate >= 30%
```

特に重要なのは、以下である。

```text
Dynamic <= 15%
```

Dynamic率が15%を超えると、01単体で60FPSを安定させるのは難しくなる。
その場合は、`StaticTransformed` と `Deferred` を増やし、完全なDynamic計算に回すグループを減らす。

---

# 14. 01の論文上の書き方

以下のようには書かない。

```text
動かない髪をキャッシュして高速化する。
```

この書き方では、既存のLOD、active set、sleeping、temporal cacheに近く見える。

代わりに、以下のように書く。

```text
本研究では、髪ストランドを単なる点列ではなく、共有端点を持つセグメントグループ列として再定義する。
各セグメントグループはStaticCached、StaticTransformed、Dynamic、Deferred、Repairの状態を持ち、静的状態では全点座標を保持せず、固定端点・方向・長さから復元する。
さらに、falseStaticRate、Static/Dynamic境界誤差、方向キャッシュによるメモリ削減率を用いて、静的再利用の安全性と有効性を評価する。
```

---

# 15. 最終結論

本01では、髪を `点-線-点` のセグメントグループ列として扱い、各グループを5状態に分類する。
静的グループは全点座標を再計算せず、方向キャッシュから復元する。
動的グループのみ物理計算し、Static/Dynamic境界では軽量補正を行う。
誤って静的化されたグループはfalse staticとして検出し、Repairへ遷移させる。

この設計により、01は単なるキャッシュ最適化ではなく、以下の特徴を持つ髪専用データ型になる。

```text
1. 点-線-点グループを最小単位とする
2. 5状態を持つ
3. 静的グループを方向キャッシュで復元する
4. Static/Dynamic境界を補正する
5. false staticを検出する
6. Telemetryで安全性と性能を評価する
```

最終的な目標は、10万本規模の髪に対し、01単体で以下を満たすことである。

```text
StaticCached + StaticTransformed >= 75%
Dynamic <= 15%
falseStaticRate <= 1%
p95 frame time <= 16.67ms
```

この条件を満たせれば、01は新規性としても安全寄りになり、裏目標である01単体60FPSにも近づく。
