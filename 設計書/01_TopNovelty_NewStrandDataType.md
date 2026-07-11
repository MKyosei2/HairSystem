# 固定60FPS・32GB制約下における

# 静的・動的セグメントグループ型髪データ構造を用いた編集可能ハイブリッド髪システムの研究

— `点-線-点` セグメントグループの静的キャッシュ・動的更新・半球境界補正による自由な髪型生成と実時間再現 —

---

# 0. 対象リポジトリ・対象ブランチ

本設計書兼企画書は、以下の実装環境を対象とする。

* Repository: `MKyosei2/HairSystem`
* Branch: `Novelty`
* 対象コードベース: HairSystem
* 主な実装対象:

  * `include/hairgpu/`
  * `src/hairgpu_core/`
  * `apps/dx11_viewer/src/`
  * `apps/hairgpu_cli/src/`
  * `tools/final_stage/`
  * `docs/sample_assets/`

本研究では、既存の guide / follower 型髪表現、Active-Set Runtime、DX11 Viewer、評価用CLI、テレメトリ出力系を土台とし、その上に **静的・動的セグメントグループ型髪データ構造** を追加する。

---

# 1. 研究背景

髪シミュレーションでは、髪一本を多数の点列として扱うことが多い。たとえば、一本の髪は次のように表現できる。

```text
P0 - P1 - P2 - P3 - P4 - ... - Pn
```

髪全体を毎フレーム高精度に解く方式は、接触や形状保持に強い一方で、計算負荷が大きい。特に固定60FPS、すなわち1フレーム16.67ms以内という条件では、髪全体を毎フレーム一律に再計算する構成は不利である。

一方、軽量な予測やキャッシュを使う方式は高速だが、髪の一部だけが動く場合、静的部分と動的部分の境界で折れや段差が発生しやすい。また、手、肩、椅子、アクセサリ、外部オブジェクトとの接触時には、局所的な貫通や形状破綻が起こりやすい。

そこで本研究では、髪一本全体を一つの更新単位として扱うのではなく、髪一本の内部を `点-線-点` のセグメントグループに分割し、各グループを静的キャッシュ状態、動的更新状態、境界補正状態として管理する新しい髪データ構造を提案する。

---

# 2. 研究目的

本研究の目的は、固定60FPS・RAM32GB以下という制約下で、髪の自由な編集と実時間再現を両立するために、髪ストランド専用の **静的・動的セグメントグループ型データ構造** を設計・実装・評価することである。

本研究では、髪一本を以下のように分割する。

```text
G0 = P0 - line0 - P1
G1 = P1 - line1 - P2
G2 = P2 - line2 - P3
G3 = P3 - line3 - P4
...
```

各 `G` を **Segment Group** と呼ぶ。

各Segment Groupは、以下の状態を持つ。

* 静的キャッシュ状態
* 動的更新状態
* 境界補正状態
* 遠距離・不可視用の凍結状態
* キャッシュ無効状態

静的グループは、前回の有効結果をそのまま使うだけでなく、始点、方向、長さ、局所座標系を用いて軽量に再構成する。動的グループは、編集、接触、曲率変化、境界誤差、時間変化に応じて再計算する。

さらに、隣接するSegment Groupが共有する点を単純な一点として扱うのではなく、前後の線分方向に応じた **半球境界** として扱う。これにより、静的グループと動的グループが隣接しても、境界の折れや段差を抑える。

---

# 3. 本研究の中核新規性

本研究の新規性は、単に「動かない部分を再計算しない」ことではない。

本研究の中核新規性は、以下の5点を統合した **髪専用の実行時データ型** を提案する点にある。

## 3.1 `点-線-点` セグメントグループ

髪ストランドを点列のまま扱うのではなく、`点-線-点` を最小管理単位とする。

## 3.2 静的・動的状態遷移

各Segment Groupが、毎フレーム以下の状態を切り替える。

```text
StaticCached
DynamicSolved
BoundaryRepair
FrozenLOD
Invalidated
```

## 3.3 方向付き静的キャッシュ

静的グループは、点位置を全て保存・再計算するのではなく、始点、方向、長さ、局所座標系、最後に有効だった終点を保持する。

## 3.4 半球境界

隣接Segment Groupの共有点を、前後の方向に応じた半球境界として扱う。

## 3.5 境界誤差監視

静的グループと動的グループの境界で、位置差、方向差、曲率差を監視し、誤差が大きい場合は境界補正または動的更新へ切り替える。

---

# 4. 現行コードとの対応

現行のHairSystemには、以下の既存要素がある。

## 4.1 `HairAsset`

既存の `HairAsset` は、髪のストランド情報、点列、幅、マテリアル、root binding、follow data、guide数、follower数、particles per strandを持つ。

本研究では、`HairAsset::strands` と `HairAsset::restPoints` をもとにSegment Groupを生成する。

## 4.2 `ActiveSetPlan`

既存の `ActiveSetPlan` は、選択されたcluster、guide、strand、corrective mask、simulation tierを持つ。

本研究では、`ActiveSetPlan` を上位レイヤーとして使い、どのstrandが詳細更新対象かを決める。その後、strand内部でSegment Group単位の静的・動的判定を行う。

## 4.3 `HybridRuntime`

既存の `HybridRuntime` には、Sparse Corrective Physics用の設定とレポートがある。

ただし現状では、補正関数が `activeSetPlan` と `colliders` を十分に使っていない。したがって、本研究ではここを修正し、Segment Group Runtimeと接続する。

---

# 5. 追加・修正するファイル一覧

## 5.1 新規追加ファイル

### 1. `include/hairgpu/SegmentGroup.h`

Segment Group用の構造体、設定、レポート、関数宣言を定義する。

### 2. `src/hairgpu_core/SegmentGroup.cpp`

Segment Groupの生成、状態遷移、静的キャッシュ再構成、動的更新、境界補正、レポート生成を実装する。

### 3. `docs/SEGMENT_GROUP_RUNTIME.md`

研究説明用の技術仕様書を追加する。

### 4. `docs/sample_assets/segment_group_runtime_preset.json`

評価実験用のSegment Group設定プリセットを追加する。

### 5. `docs/sample_assets/segment_group_metric_definitions.json`

Segment Group評価指標の説明ファイルを追加する。

---

## 5.2 修正する既存ファイル

### 1. `HairSystem.vcxproj`

新規追加する `SegmentGroup.h` と `SegmentGroup.cpp` をVisual Studioビルド対象に追加する。

### 2. `HairSystem.vcxproj.filters`

Visual Studio上で `SegmentGroup.h` と `SegmentGroup.cpp` が正しいフィルタに表示されるように追加する。

### 3. `include/hairgpu/HybridRuntime.h`

Segment Group Runtimeの設定とレポートをHybrid Runtimeから呼べるようにする。

### 4. `src/hairgpu_core/HybridRuntime.cpp`

`ApplySparseCorrectivePhysics` 内で `ActiveSetPlan` と `SegmentGroupRuntime` を使うように修正する。

### 5. `include/hairgpu/HairNoveltyTelemetry.h`

Segment Group用のテレメトリ項目を追加または整理する。

### 6. `src/hairgpu_core/HairNoveltyTelemetry.cpp`

新規テレメトリ項目の初期化・集計・JSON出力を追加する。

### 7. `apps/dx11_viewer/src/GpuHairViewer.h`

PerformanceSampleにSegment Group評価値を保持する項目を追加する。

### 8. `apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl`

毎フレームのシミュレーションステップでSegment Group Runtimeを呼び出す。

### 9. `apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl`

Segment Group評価値をsummary JSON / JSONLへ出力する。

### 10. `apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl`

Segment Group評価値をCSVへ出力する。

### 11. `apps/hairgpu_cli/src/main_parts/01_runtime_suite_and_evaluation_commands.inl`

CLIからSegment Group Runtimeを有効化し、評価CSVを出せるようにする。

### 12. `tools/final_stage/schema.sql`

SQLiteの評価DBにSegment Group用カラムを追加する。

### 13. `tools/final_stage/hairgpu_final_db.py`

CSVからSegment Group評価値を読み込み、DBへ保存する。

### 14. `tools/final_stage/hairgpu_runtime_suite.py`

最終評価レポートにSegment Group項目を追加する。

---

# 6. 新規ファイル: `include/hairgpu/SegmentGroup.h`

このファイルには、Segment Groupの核となる型を定義する。

```cpp
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/Types.h"

namespace hairgpu
{
    enum class SegmentGroupState : uint32_t
    {
        StaticCached = 0,
        DynamicSolved = 1,
        BoundaryRepair = 2,
        FrozenLod = 3,
        Invalidated = 4
    };

    enum class SegmentGroupInvalidationReason : uint32_t
    {
        None = 0,
        Edit = 1 << 0,
        Contact = 1 << 1,
        Penetration = 1 << 2,
        BoundaryError = 1 << 3,
        CurvatureError = 1 << 4,
        TemporalError = 1 << 5,
        CacheAge = 1 << 6,
        ParentMotion = 1 << 7,
        Visibility = 1 << 8
    };

    struct SegmentGroupSettings
    {
        uint32_t segmentsPerGroup = 1u;
        uint32_t maxCacheAgeFrames = 6u;

        float staticVelocityThreshold = 0.012f;
        float staticDisplacementThreshold = 0.010f;
        float contactRiskThreshold = 0.35f;
        float editRiskThreshold = 0.20f;
        float penetrationRiskThreshold = 0.20f;

        float boundaryPositionErrorThreshold = 0.006f;
        float boundaryDirectionErrorThreshold = 0.18f;
        float curvatureErrorThreshold = 0.22f;

        float staticCacheBlend = 0.92f;
        float boundaryRepairBlend = 0.55f;
        float dynamicSolveBlend = 0.85f;

        float hemisphereRadiusScale = 0.50f;
        float minSegmentLength = 1.0e-5f;

        bool enableHemisphereBoundary = true;
        bool enableWatchdogRepair = true;
        bool enableDirectionCache = true;
        bool enableFrozenLod = true;
    };

    struct SegmentGroupRecord
    {
        uint32_t segmentGroupIndex = 0u;
        uint32_t strandIndex = 0u;
        uint32_t localSegmentIndex = 0u;

        uint32_t startPointIndex = 0u;
        uint32_t endPointIndex = 0u;

        Vec3 anchorPoint = { 0.0f, 0.0f, 0.0f };
        Vec3 direction = { 0.0f, -1.0f, 0.0f };
        float length = 0.0f;

        Vec3 localXAxis = { 1.0f, 0.0f, 0.0f };
        Vec3 localYAxis = { 0.0f, 1.0f, 0.0f };
        Vec3 localZAxis = { 0.0f, 0.0f, 1.0f };

        Vec3 lastValidStartPoint = { 0.0f, 0.0f, 0.0f };
        Vec3 lastValidEndPoint = { 0.0f, 0.0f, 0.0f };

        SegmentGroupState state = SegmentGroupState::StaticCached;
        uint32_t invalidationMask = 0u;

        float contactRisk = 0.0f;
        float editRisk = 0.0f;
        float penetrationRisk = 0.0f;
        float boundaryPositionError = 0.0f;
        float boundaryDirectionError = 0.0f;
        float curvatureError = 0.0f;
        float temporalError = 0.0f;
        float cacheConfidence = 1.0f;

        uint32_t lastUpdatedFrame = 0u;
        uint32_t cacheAge = 0u;
    };

    struct SegmentGroupRuntimeInput
    {
        const HairAsset* asset = nullptr;
        const ActiveSetPlan* activeSetPlan = nullptr;
        const ColliderSet* colliders = nullptr;

        std::vector<Vec3>* points = nullptr;
        const std::vector<Vec3>* previousPoints = nullptr;

        std::vector<float> strandEditScores;
        std::vector<float> strandContactScores;
        std::vector<float> strandPenetrationScores;
        std::vector<float> strandVisibilityScores;

        Vec3 cameraPosition = { 0.0f, 1.6f, -1.8f };
        Vec3 cameraForward = { 0.0f, 0.0f, 1.0f };

        float fixedTimeStep = 1.0f / 60.0f;
        uint32_t frameIndex = 0u;
    };

    struct SegmentGroupRuntimeReport
    {
        uint32_t totalSegmentGroupCount = 0u;
        uint32_t staticCachedGroupCount = 0u;
        uint32_t staticTransformedGroupCount = 0u;
        uint32_t dynamicGroupCount = 0u;
        uint32_t boundaryRepairGroupCount = 0u;
        uint32_t frozenGroupCount = 0u;
        uint32_t invalidatedGroupCount = 0u;

        uint32_t cacheHitCount = 0u;
        uint32_t cacheMissCount = 0u;
        uint32_t cacheInvalidationCount = 0u;

        uint32_t watchdogCheckCount = 0u;
        uint32_t watchdogRepairCount = 0u;
        uint32_t falseStaticCount = 0u;

        float falseStaticRate = 0.0f;
        float staticDynamicBoundaryMeanErrorMm = 0.0f;
        float staticDynamicBoundaryMaxErrorMm = 0.0f;

        float directionCacheMemoryMb = 0.0f;
        float fullPositionMemoryMb = 0.0f;
        float directionCacheMemorySavingRate = 0.0f;

        float estimatedRuntimeMilliseconds = 0.0f;
    };

    struct SegmentGroupRuntimeCache
    {
        std::vector<SegmentGroupRecord> groups;
        uint32_t lastBuiltStrandCount = 0u;
        uint32_t lastBuiltPointCount = 0u;
    };

    bool BuildSegmentGroupsFromAsset(const HairAsset& asset,
                                     const SegmentGroupSettings& settings,
                                     SegmentGroupRuntimeCache& outCache,
                                     std::string& outError);

    bool UpdateSegmentGroupRuntime(const SegmentGroupRuntimeInput& input,
                                   const SegmentGroupSettings& settings,
                                   SegmentGroupRuntimeCache& cache,
                                   SegmentGroupRuntimeReport& outReport,
                                   std::string& outError);

    bool WriteSegmentGroupRuntimeCsv(const std::filesystem::path& outputPath,
                                     const SegmentGroupRuntimeCache& cache,
                                     const SegmentGroupRuntimeReport& report,
                                     std::string& outError);

    std::string BuildSegmentGroupRuntimeSummary(const SegmentGroupRuntimeReport& report);
}
```

---

# 7. 新規ファイル: `src/hairgpu_core/SegmentGroup.cpp`

このファイルに、Segment Groupの実処理を書く。

## 7.1 実装する関数一覧

```cpp
BuildSegmentGroupsFromAsset
UpdateSegmentGroupRuntime
EvaluateSegmentGroupRisk
UpdateSegmentGroupState
ApplyStaticCache
ApplyDynamicSegmentSolve
RepairStaticDynamicBoundary
ComputeHemisphereBoundaryError
ComputeDirectionCacheMemory
WriteSegmentGroupRuntimeCsv
BuildSegmentGroupRuntimeSummary
```

---

## 7.2 `BuildSegmentGroupsFromAsset`

目的は、`HairAsset::strands` と `HairAsset::restPoints` から `点-線-点` グループを作ることである。

```cpp
bool BuildSegmentGroupsFromAsset(const HairAsset& asset,
                                 const SegmentGroupSettings& settings,
                                 SegmentGroupRuntimeCache& outCache,
                                 std::string& outError)
{
    outError.clear();
    outCache.groups.clear();

    if (asset.strands.empty() || asset.restPoints.empty())
    {
        outError = "SegmentGroup requires non-empty hair asset.";
        return false;
    }

    uint32_t globalGroupIndex = 0u;

    for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
    {
        const StrandRange& strand = asset.strands[strandIndex];

        if (strand.pointCount < 2u)
        {
            continue;
        }

        for (uint32_t local = 0u; local + 1u < strand.pointCount; local += settings.segmentsPerGroup)
        {
            const uint32_t startIndex = strand.pointOffset + local;
            const uint32_t endIndex = strand.pointOffset + std::min(local + settings.segmentsPerGroup, strand.pointCount - 1u);

            if (startIndex >= asset.restPoints.size() || endIndex >= asset.restPoints.size())
            {
                continue;
            }

            const Vec3 start = asset.restPoints[startIndex];
            const Vec3 end = asset.restPoints[endIndex];
            const Vec3 delta = end - start;
            const float length = std::max(Length(delta), settings.minSegmentLength);

            SegmentGroupRecord record = {};
            record.segmentGroupIndex = globalGroupIndex++;
            record.strandIndex = strandIndex;
            record.localSegmentIndex = local;
            record.startPointIndex = startIndex;
            record.endPointIndex = endIndex;

            record.anchorPoint = start;
            record.direction = delta / length;
            record.length = length;

            record.lastValidStartPoint = start;
            record.lastValidEndPoint = end;

            record.state = SegmentGroupState::StaticCached;
            record.cacheConfidence = 1.0f;

            outCache.groups.push_back(record);
        }
    }

    outCache.lastBuiltStrandCount = static_cast<uint32_t>(asset.strands.size());
    outCache.lastBuiltPointCount = static_cast<uint32_t>(asset.restPoints.size());

    return true;
}
```

---

## 7.3 `UpdateSegmentGroupRuntime`

この関数が毎フレーム呼ばれる中心処理である。

処理順は以下。

```text
1. 入力チェック
2. SegmentGroupが未構築ならBuildする
3. 各SegmentGroupのリスクを計算
4. Static / Dynamic / BoundaryRepair / FrozenLOD / Invalidated を判定
5. StaticCachedなら方向キャッシュで再構成
6. DynamicSolvedなら局所更新
7. BoundaryRepairなら半球境界補正
8. Watchdogで誤静的判定を検出
9. Reportを集計
```

疑似コードは以下。

```cpp
bool UpdateSegmentGroupRuntime(const SegmentGroupRuntimeInput& input,
                               const SegmentGroupSettings& settings,
                               SegmentGroupRuntimeCache& cache,
                               SegmentGroupRuntimeReport& outReport,
                               std::string& outError)
{
    outError.clear();
    outReport = {};

    if (!input.asset || !input.points)
    {
        outError = "SegmentGroupRuntimeInput is missing asset or points.";
        return false;
    }

    const HairAsset& asset = *input.asset;
    std::vector<Vec3>& points = *input.points;

    if (points.size() != asset.restPoints.size())
    {
        outError = "SegmentGroupRuntime points size does not match asset rest points.";
        return false;
    }

    if (cache.groups.empty() ||
        cache.lastBuiltStrandCount != asset.strands.size() ||
        cache.lastBuiltPointCount != asset.restPoints.size())
    {
        if (!BuildSegmentGroupsFromAsset(asset, settings, cache, outError))
        {
            return false;
        }
    }

    for (SegmentGroupRecord& group : cache.groups)
    {
        EvaluateSegmentGroupRisk(input, settings, group);
        UpdateSegmentGroupState(input, settings, group);

        switch (group.state)
        {
        case SegmentGroupState::StaticCached:
            ApplyStaticCache(settings, group, points, outReport);
            break;

        case SegmentGroupState::DynamicSolved:
            ApplyDynamicSegmentSolve(input, settings, group, points, outReport);
            break;

        case SegmentGroupState::BoundaryRepair:
            RepairStaticDynamicBoundary(input, settings, group, points, outReport);
            break;

        case SegmentGroupState::FrozenLod:
            ApplyStaticCache(settings, group, points, outReport);
            ++outReport.frozenGroupCount;
            break;

        case SegmentGroupState::Invalidated:
            ApplyDynamicSegmentSolve(input, settings, group, points, outReport);
            break;
        }

        group.cacheAge = (group.state == SegmentGroupState::StaticCached)
            ? group.cacheAge + 1u
            : 0u;

        group.lastUpdatedFrame = input.frameIndex;
    }

    FinalizeSegmentGroupReport(settings, cache, outReport);
    return true;
}
```

---

# 8. `EvaluateSegmentGroupRisk`

この関数では、各Segment Groupが静的のままでよいか、動的更新すべきかを判定するためのリスクを計算する。

使う入力は以下。

* `ActiveSetPlan::strandCorrectiveMask`
* `ActiveSetPlan::recommendedSimulationTiers`
* strand単位のedit score
* strand単位のcontact score
* strand単位のpenetration score
* 前フレーム点位置
* 現在点位置
* カメラ距離
* 境界誤差
* キャッシュ年齢

```cpp
void EvaluateSegmentGroupRisk(const SegmentGroupRuntimeInput& input,
                              const SegmentGroupSettings& settings,
                              SegmentGroupRecord& group)
{
    group.invalidationMask = 0u;

    const uint32_t strandIndex = group.strandIndex;

    group.editRisk = ReadScore(input.strandEditScores, strandIndex, 0.0f);
    group.contactRisk = ReadScore(input.strandContactScores, strandIndex, 0.0f);
    group.penetrationRisk = ReadScore(input.strandPenetrationScores, strandIndex, 0.0f);

    if (input.activeSetPlan &&
        strandIndex < input.activeSetPlan->strandCorrectiveMask.size() &&
        input.activeSetPlan->strandCorrectiveMask[strandIndex] != 0u)
    {
        group.contactRisk = std::max(group.contactRisk, 0.65f);
    }

    if (group.editRisk >= settings.editRiskThreshold)
    {
        group.invalidationMask |= static_cast<uint32_t>(SegmentGroupInvalidationReason::Edit);
    }

    if (group.contactRisk >= settings.contactRiskThreshold)
    {
        group.invalidationMask |= static_cast<uint32_t>(SegmentGroupInvalidationReason::Contact);
    }

    if (group.penetrationRisk >= settings.penetrationRiskThreshold)
    {
        group.invalidationMask |= static_cast<uint32_t>(SegmentGroupInvalidationReason::Penetration);
    }

    if (group.boundaryPositionError >= settings.boundaryPositionErrorThreshold ||
        group.boundaryDirectionError >= settings.boundaryDirectionErrorThreshold)
    {
        group.invalidationMask |= static_cast<uint32_t>(SegmentGroupInvalidationReason::BoundaryError);
    }

    if (group.cacheAge >= settings.maxCacheAgeFrames)
    {
        group.invalidationMask |= static_cast<uint32_t>(SegmentGroupInvalidationReason::CacheAge);
    }
}
```

---

# 9. `UpdateSegmentGroupState`

この関数で、静的・動的・境界補正の状態遷移を行う。

```cpp
void UpdateSegmentGroupState(const SegmentGroupRuntimeInput& input,
                             const SegmentGroupSettings& settings,
                             SegmentGroupRecord& group)
{
    const bool hasEdit =
        (group.invalidationMask & static_cast<uint32_t>(SegmentGroupInvalidationReason::Edit)) != 0u;

    const bool hasContact =
        (group.invalidationMask & static_cast<uint32_t>(SegmentGroupInvalidationReason::Contact)) != 0u;

    const bool hasPenetration =
        (group.invalidationMask & static_cast<uint32_t>(SegmentGroupInvalidationReason::Penetration)) != 0u;

    const bool hasBoundaryError =
        (group.invalidationMask & static_cast<uint32_t>(SegmentGroupInvalidationReason::BoundaryError)) != 0u;

    const bool cacheExpired =
        (group.invalidationMask & static_cast<uint32_t>(SegmentGroupInvalidationReason::CacheAge)) != 0u;

    if (hasEdit || hasContact || hasPenetration)
    {
        group.state = SegmentGroupState::DynamicSolved;
        return;
    }

    if (hasBoundaryError)
    {
        group.state = SegmentGroupState::BoundaryRepair;
        return;
    }

    if (cacheExpired)
    {
        group.state = SegmentGroupState::Invalidated;
        return;
    }

    if (settings.enableFrozenLod && input.activeSetPlan)
    {
        if (group.strandIndex < input.activeSetPlan->recommendedSimulationTiers.size())
        {
            const SimTier tier = input.activeSetPlan->recommendedSimulationTiers[group.strandIndex];
            if (tier == SimTier::Frozen)
            {
                group.state = SegmentGroupState::FrozenLod;
                return;
            }
        }
    }

    group.state = SegmentGroupState::StaticCached;
}
```

---

# 10. `ApplyStaticCache`

静的グループでは、点列を完全再計算せず、始点・方向・長さから終点を復元する。

```cpp
void ApplyStaticCache(const SegmentGroupSettings& settings,
                      SegmentGroupRecord& group,
                      std::vector<Vec3>& points,
                      SegmentGroupRuntimeReport& report)
{
    if (group.startPointIndex >= points.size() || group.endPointIndex >= points.size())
    {
        return;
    }

    const Vec3 start = points[group.startPointIndex];
    const Vec3 cachedEnd = start + group.direction * group.length;

    points[group.endPointIndex] =
        Lerp(points[group.endPointIndex], cachedEnd, settings.staticCacheBlend);

    group.lastValidStartPoint = points[group.startPointIndex];
    group.lastValidEndPoint = points[group.endPointIndex];

    ++report.staticCachedGroupCount;
    ++report.cacheHitCount;
}
```

---

# 11. `ApplyDynamicSegmentSolve`

動的グループでは、軽量な局所補正を行う。

最低限、以下を実装する。

* 長さ保持
* 接触補正
* floor補正
* capsule / sphere collider補正
* 曲率補正
* root側固定

```cpp
void ApplyDynamicSegmentSolve(const SegmentGroupRuntimeInput& input,
                              const SegmentGroupSettings& settings,
                              SegmentGroupRecord& group,
                              std::vector<Vec3>& points,
                              SegmentGroupRuntimeReport& report)
{
    if (group.startPointIndex >= points.size() || group.endPointIndex >= points.size())
    {
        return;
    }

    Vec3 start = points[group.startPointIndex];
    Vec3 end = points[group.endPointIndex];

    Vec3 delta = end - start;
    float currentLength = std::max(Length(delta), settings.minSegmentLength);
    Vec3 dir = delta / currentLength;

    Vec3 lengthCorrectedEnd = start + dir * group.length;
    end = Lerp(end, lengthCorrectedEnd, settings.dynamicSolveBlend);

    if (input.colliders)
    {
        ApplyColliderCorrection(*input.colliders, settings, end, report);
    }

    points[group.endPointIndex] = end;

    group.direction = NormalizeSafe(end - start, group.direction);
    group.lastValidStartPoint = start;
    group.lastValidEndPoint = end;
    group.cacheConfidence = 1.0f;
    group.cacheAge = 0u;

    ++report.dynamicGroupCount;
    ++report.cacheMissCount;
}
```

---

# 12. `RepairStaticDynamicBoundary`

静的グループと動的グループの境界を補正する。

```cpp
void RepairStaticDynamicBoundary(const SegmentGroupRuntimeInput& input,
                                 const SegmentGroupSettings& settings,
                                 SegmentGroupRecord& group,
                                 std::vector<Vec3>& points,
                                 SegmentGroupRuntimeReport& report)
{
    if (group.startPointIndex >= points.size() || group.endPointIndex >= points.size())
    {
        return;
    }

    Vec3 start = points[group.startPointIndex];
    Vec3 end = points[group.endPointIndex];

    Vec3 expectedEnd = start + group.direction * group.length;
    Vec3 repairedEnd = Lerp(end, expectedEnd, settings.boundaryRepairBlend);

    if (settings.enableHemisphereBoundary)
    {
        repairedEnd = ApplyHemisphereBoundaryClamp(group, start, repairedEnd, settings);
    }

    points[group.endPointIndex] = repairedEnd;

    group.boundaryPositionError = Length(repairedEnd - expectedEnd);
    group.lastValidStartPoint = start;
    group.lastValidEndPoint = repairedEnd;

    ++report.boundaryRepairGroupCount;
    ++report.watchdogRepairCount;
}
```

---

# 13. `ComputeHemisphereBoundaryError`

半球境界の誤差を計算する。

```cpp
float ComputeHemisphereBoundaryError(const SegmentGroupRecord& previousGroup,
                                     const SegmentGroupRecord& currentGroup,
                                     const std::vector<Vec3>& points)
{
    if (previousGroup.endPointIndex >= points.size() ||
        currentGroup.startPointIndex >= points.size())
    {
        return 0.0f;
    }

    const Vec3 sharedA = points[previousGroup.endPointIndex];
    const Vec3 sharedB = points[currentGroup.startPointIndex];

    const float positionError = Length(sharedA - sharedB);

    const float directionDot = Clamp(Dot(previousGroup.direction, currentGroup.direction), -1.0f, 1.0f);
    const float directionError = std::acos(directionDot);

    return positionError + directionError;
}
```

---

# 14. `src/hairgpu_core/HybridRuntime.cpp` の修正

## 14.1 修正目的

現状の `ApplySparseCorrectivePhysics` は、`activeSetPlan` と `colliders` を十分に利用していない。

そのため、この関数を以下のように修正する。

* `ActiveSetPlan::strandCorrectiveMask` を見る。
* corrective対象strandだけを動的補正する。
* Segment Group Runtimeを呼ぶ。
* `colliders` による補正を行う。
* reportにSegment Group結果を反映する。

## 14.2 修正方針

`HybridRuntime.cpp` の先頭に以下を追加する。

```cpp
#include "hairgpu/SegmentGroup.h"
```

`ApplySparseCorrectivePhysics` 内に、以下のような処理を追加する。

```cpp
SegmentGroupRuntimeCache segmentCache;
SegmentGroupSettings segmentSettings = {};

SegmentGroupRuntimeInput segmentInput = {};
segmentInput.asset = &asset;
segmentInput.activeSetPlan = &activeSetPlan;
segmentInput.colliders = &colliders;
segmentInput.points = &inOutPoints;
segmentInput.fixedTimeStep = 1.0f / 60.0f;

SegmentGroupRuntimeReport segmentReport = {};
if (!UpdateSegmentGroupRuntime(segmentInput,
                               segmentSettings,
                               segmentCache,
                               segmentReport,
                               outError))
{
    return false;
}
```

ただし、毎フレームcacheを保持するには、ローカル変数ではなく、ViewerまたはRuntime側で `SegmentGroupRuntimeCache` をメンバとして持つ必要がある。

そのため、最終的には `GpuHairViewer` 側にcacheを持たせる。

---

# 15. `apps/dx11_viewer/src/GpuHairViewer.h` の修正

## 15.1 include追加

```cpp
#include "hairgpu/SegmentGroup.h"
```

## 15.2 メンバ追加

`GpuHairViewer` クラスに以下を追加する。

```cpp
hairgpu::SegmentGroupSettings m_segmentGroupSettings = {};
hairgpu::SegmentGroupRuntimeCache m_segmentGroupCache = {};
hairgpu::SegmentGroupRuntimeReport m_lastSegmentGroupReport = {};
bool m_enableSegmentGroupRuntime = true;
```

## 15.3 PerformanceSampleへの追加

既に近い項目がある場合は重複させず、足りないものを追加する。

```cpp
uint32_t segmentGroupTotalCount = 0u;
uint32_t segmentGroupStaticCachedCount = 0u;
uint32_t segmentGroupDynamicCount = 0u;
uint32_t segmentGroupBoundaryRepairCount = 0u;
uint32_t segmentGroupFrozenCount = 0u;
uint32_t segmentGroupInvalidatedCount = 0u;

uint32_t segmentGroupCacheHitCount = 0u;
uint32_t segmentGroupCacheMissCount = 0u;
uint32_t segmentGroupWatchdogRepairCount = 0u;

float segmentGroupFalseStaticRate = 0.0f;
float segmentGroupBoundaryMeanErrorMm = 0.0f;
float segmentGroupBoundaryMaxErrorMm = 0.0f;
float segmentGroupDirectionCacheMemorySavingRate = 0.0f;
```

---

# 16. `apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl` の修正

このファイルの毎フレームシミュレーション処理後、描画前にSegment Group Runtimeを呼ぶ。

```cpp
if (m_enableSegmentGroupRuntime)
{
    hairgpu::SegmentGroupRuntimeInput input = {};
    input.asset = &m_asset;
    input.activeSetPlan = &m_activeSetPlan;
    input.colliders = &m_colliders;
    input.points = &m_simulatedPoints;
    input.previousPoints = &m_previousSimulatedPoints;
    input.cameraPosition = m_cameraPosition;
    input.cameraForward = m_cameraForward;
    input.fixedTimeStep = m_simulationConfig.fixedTimeStep;
    input.frameIndex = m_globalFrameIndex;

    std::string segmentError;
    if (hairgpu::UpdateSegmentGroupRuntime(input,
                                           m_segmentGroupSettings,
                                           m_segmentGroupCache,
                                           m_lastSegmentGroupReport,
                                           segmentError))
    {
        m_noveltyTelemetry.totalSegmentGroupCount =
            m_lastSegmentGroupReport.totalSegmentGroupCount;
        m_noveltyTelemetry.staticCachedGroupCount =
            m_lastSegmentGroupReport.staticCachedGroupCount;
        m_noveltyTelemetry.staticTransformedGroupCount =
            m_lastSegmentGroupReport.staticTransformedGroupCount;
        m_noveltyTelemetry.dynamicGroupCount =
            m_lastSegmentGroupReport.dynamicGroupCount;
        m_noveltyTelemetry.deferredCorrectionGroupCount =
            m_lastSegmentGroupReport.boundaryRepairGroupCount;
        m_noveltyTelemetry.falseStaticCount =
            m_lastSegmentGroupReport.falseStaticCount;
        m_noveltyTelemetry.falseStaticRate =
            m_lastSegmentGroupReport.falseStaticRate;
        m_noveltyTelemetry.cacheHitCount =
            m_lastSegmentGroupReport.cacheHitCount;
        m_noveltyTelemetry.cacheMissCount =
            m_lastSegmentGroupReport.cacheMissCount;
        m_noveltyTelemetry.watchdogRepairCount =
            m_lastSegmentGroupReport.watchdogRepairCount;
        m_noveltyTelemetry.staticDynamicBoundaryMeanErrorMm =
            m_lastSegmentGroupReport.staticDynamicBoundaryMeanErrorMm;
        m_noveltyTelemetry.staticDynamicBoundaryMaxErrorMm =
            m_lastSegmentGroupReport.staticDynamicBoundaryMaxErrorMm;
        m_noveltyTelemetry.directionCacheMemoryMb =
            m_lastSegmentGroupReport.directionCacheMemoryMb;
        m_noveltyTelemetry.fullPositionMemoryMb =
            m_lastSegmentGroupReport.fullPositionMemoryMb;
        m_noveltyTelemetry.directionCacheMemorySavingRate =
            m_lastSegmentGroupReport.directionCacheMemorySavingRate;
    }
}
```

---

# 17. `include/hairgpu/HairNoveltyTelemetry.h` の修正

既に以下のような項目が存在する場合は、そのまま活用する。

```cpp
uint32_t totalSegmentGroupCount;
uint32_t staticCachedGroupCount;
uint32_t staticTransformedGroupCount;
uint32_t dynamicGroupCount;
uint32_t deferredCorrectionGroupCount;
uint32_t repairRequiredGroupCount;
uint32_t fallbackGroupCount;

uint32_t falseStaticCount;
float falseStaticRate;

uint32_t cacheHitCount;
uint32_t cacheMissCount;
uint32_t cacheInvalidationCount;

uint32_t watchdogCheckCount;
uint32_t watchdogRepairCount;

float staticDynamicBoundaryMeanErrorMm;
float staticDynamicBoundaryMaxErrorMm;

float directionCacheMemoryMb;
float fullPositionMemoryMb;
float directionCacheMemorySavingRate;
```

不足している場合は、以下を追加する。

```cpp
uint32_t boundaryRepairGroupCount = 0u;
uint32_t frozenSegmentGroupCount = 0u;
uint32_t invalidatedSegmentGroupCount = 0u;
float segmentGroupRuntimeMilliseconds = 0.0f;
```

---

# 18. `src/hairgpu_core/HairNoveltyTelemetry.cpp` の修正

Telemetry初期化関数で、Segment Group関連項目を0に戻す。

```cpp
telemetry.totalSegmentGroupCount = 0u;
telemetry.staticCachedGroupCount = 0u;
telemetry.staticTransformedGroupCount = 0u;
telemetry.dynamicGroupCount = 0u;
telemetry.deferredCorrectionGroupCount = 0u;
telemetry.repairRequiredGroupCount = 0u;
telemetry.fallbackGroupCount = 0u;

telemetry.falseStaticCount = 0u;
telemetry.falseStaticRate = 0.0f;

telemetry.cacheHitCount = 0u;
telemetry.cacheMissCount = 0u;
telemetry.cacheInvalidationCount = 0u;

telemetry.watchdogCheckCount = 0u;
telemetry.watchdogRepairCount = 0u;

telemetry.staticDynamicBoundaryMeanErrorMm = 0.0f;
telemetry.staticDynamicBoundaryMaxErrorMm = 0.0f;

telemetry.directionCacheMemoryMb = 0.0f;
telemetry.fullPositionMemoryMb = 0.0f;
telemetry.directionCacheMemorySavingRate = 0.0f;
```

---

# 19. `apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl` の修正

既存のCSV出力に以下の列を必ず含める。

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
cacheHitCount
cacheMissCount
cacheInvalidationCount
watchdogCheckCount
watchdogRepairCount
staticDynamicBoundaryMeanErrorMm
staticDynamicBoundaryMaxErrorMm
directionCacheMemoryMb
fullPositionMemoryMb
directionCacheMemorySavingRate
```

既に出力されている項目は重複させず、足りない項目だけ追加する。

PerformanceSample生成時には、以下のように代入する。

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
sample.cacheHitCount = m_noveltyTelemetry.cacheHitCount;
sample.cacheMissCount = m_noveltyTelemetry.cacheMissCount;
sample.cacheInvalidationCount = m_noveltyTelemetry.cacheInvalidationCount;
sample.watchdogCheckCount = m_noveltyTelemetry.watchdogCheckCount;
sample.watchdogRepairCount = m_noveltyTelemetry.watchdogRepairCount;
sample.staticDynamicBoundaryMeanErrorMm = m_noveltyTelemetry.staticDynamicBoundaryMeanErrorMm;
sample.staticDynamicBoundaryMaxErrorMm = m_noveltyTelemetry.staticDynamicBoundaryMaxErrorMm;
sample.directionCacheMemoryMb = m_noveltyTelemetry.directionCacheMemoryMb;
sample.fullPositionMemoryMb = m_noveltyTelemetry.fullPositionMemoryMb;
sample.directionCacheMemorySavingRate = m_noveltyTelemetry.directionCacheMemorySavingRate;
```

---

# 20. `HairSystem.vcxproj` の修正

`<ItemGroup>` の `ClCompile` に以下を追加する。

```xml
<ClCompile Include="src\hairgpu_core\SegmentGroup.cpp" />
```

`<ItemGroup>` の `ClInclude` に以下を追加する。

```xml
<ClInclude Include="include\hairgpu\SegmentGroup.h" />
```

---

# 21. `HairSystem.vcxproj.filters` の修正

以下を追加する。

```xml
<ClCompile Include="src\hairgpu_core\SegmentGroup.cpp">
  <Filter>Source Files\hairgpu_core</Filter>
</ClCompile>

<ClInclude Include="include\hairgpu\SegmentGroup.h">
  <Filter>Header Files\hairgpu</Filter>
</ClInclude>
```

---

# 22. `apps/hairgpu_cli/src/main_parts/01_runtime_suite_and_evaluation_commands.inl` の修正

CLIからSegment Group Runtimeを使えるように、以下のオプションを追加する。

```text
--enable-segment-groups
--disable-segment-groups
--segment-cache-age N
--segment-boundary-threshold VALUE
--segment-contact-threshold VALUE
--segment-edit-threshold VALUE
--export-segment-group-csv PATH
```

CLI実行例:

```text
HairSystem.exe --runtime-suite final_runtime_suite_template.json ^
  --enable-segment-groups ^
  --segment-cache-age 6 ^
  --segment-boundary-threshold 0.006 ^
  --export-segment-group-csv results/segment_groups.csv
```

---

# 23. `tools/final_stage/schema.sql` の修正

`runtime_frame_metrics` に以下のカラムを追加する。

```sql
total_segment_group_count INTEGER DEFAULT 0,
static_cached_group_count INTEGER DEFAULT 0,
static_transformed_group_count INTEGER DEFAULT 0,
dynamic_group_count INTEGER DEFAULT 0,
boundary_repair_group_count INTEGER DEFAULT 0,
deferred_correction_group_count INTEGER DEFAULT 0,
repair_required_group_count INTEGER DEFAULT 0,
fallback_group_count INTEGER DEFAULT 0,
false_static_count INTEGER DEFAULT 0,
false_static_rate REAL DEFAULT 0.0,
cache_hit_count INTEGER DEFAULT 0,
cache_miss_count INTEGER DEFAULT 0,
cache_invalidation_count INTEGER DEFAULT 0,
watchdog_check_count INTEGER DEFAULT 0,
watchdog_repair_count INTEGER DEFAULT 0,
static_dynamic_boundary_mean_error_mm REAL DEFAULT 0.0,
static_dynamic_boundary_max_error_mm REAL DEFAULT 0.0,
direction_cache_memory_mb REAL DEFAULT 0.0,
full_position_memory_mb REAL DEFAULT 0.0,
direction_cache_memory_saving_rate REAL DEFAULT 0.0
```

---

# 24. `tools/final_stage/hairgpu_final_db.py` の修正

CSVから以下の列を読み込む。

```python
totalSegmentGroupCount
staticCachedGroupCount
staticTransformedGroupCount
dynamicGroupCount
deferredCorrectionGroupCount
repairRequiredGroupCount
fallbackGroupCount
falseStaticCount
falseStaticRate
cacheHitCount
cacheMissCount
cacheInvalidationCount
watchdogCheckCount
watchdogRepairCount
staticDynamicBoundaryMeanErrorMm
staticDynamicBoundaryMaxErrorMm
directionCacheMemoryMb
fullPositionMemoryMb
directionCacheMemorySavingRate
```

DB保存時には、snake_caseへ変換して保存する。

```python
"total_segment_group_count": read_int(row, "totalSegmentGroupCount"),
"static_cached_group_count": read_int(row, "staticCachedGroupCount"),
"static_transformed_group_count": read_int(row, "staticTransformedGroupCount"),
"dynamic_group_count": read_int(row, "dynamicGroupCount"),
"deferred_correction_group_count": read_int(row, "deferredCorrectionGroupCount"),
"repair_required_group_count": read_int(row, "repairRequiredGroupCount"),
"fallback_group_count": read_int(row, "fallbackGroupCount"),
"false_static_count": read_int(row, "falseStaticCount"),
"false_static_rate": read_float(row, "falseStaticRate"),
"cache_hit_count": read_int(row, "cacheHitCount"),
"cache_miss_count": read_int(row, "cacheMissCount"),
"cache_invalidation_count": read_int(row, "cacheInvalidationCount"),
"watchdog_check_count": read_int(row, "watchdogCheckCount"),
"watchdog_repair_count": read_int(row, "watchdogRepairCount"),
"static_dynamic_boundary_mean_error_mm": read_float(row, "staticDynamicBoundaryMeanErrorMm"),
"static_dynamic_boundary_max_error_mm": read_float(row, "staticDynamicBoundaryMaxErrorMm"),
"direction_cache_memory_mb": read_float(row, "directionCacheMemoryMb"),
"full_position_memory_mb": read_float(row, "fullPositionMemoryMb"),
"direction_cache_memory_saving_rate": read_float(row, "directionCacheMemorySavingRate"),
```

---

# 25. `tools/final_stage/hairgpu_runtime_suite.py` の修正

Markdownレポートに以下の表を追加する。

```markdown
## Segment Group Runtime Metrics

| Metric | Meaning |
|---|---|
| totalSegmentGroupCount | 全セグメントグループ数 |
| staticCachedGroupCount | 静的キャッシュとして再利用されたグループ数 |
| dynamicGroupCount | 動的更新されたグループ数 |
| falseStaticRate | 静的と判定したが誤りだった割合 |
| watchdogRepairCount | watchdogにより修復された回数 |
| staticDynamicBoundaryMeanErrorMm | 静的/動的境界の平均誤差 |
| directionCacheMemorySavingRate | 方向キャッシュによるメモリ削減率 |
```

さらに、比較表に以下を追加する。

```text
avg_static_cached_group_count
avg_dynamic_group_count
avg_false_static_rate
avg_boundary_error_mm
avg_direction_cache_memory_saving_rate
```

---

# 26. 評価実験

## 26.1 比較対象

以下の4方式を比較する。

### A. Full Dynamic

全ての髪を毎フレーム更新する。

### B. ActiveSet Only

guide cluster単位で補正対象を選ぶが、髪一本内部のセグメントグループ分割は行わない。

### C. Segment Cache Only

Segment Groupの静的キャッシュは使うが、半球境界補正とwatchdogを使わない。

### D. Proposed

Segment Group、静的/動的状態遷移、半球境界、watchdog、ActiveSet連動を全て使う。

---

## 26.2 評価指標

### 実時間性

* 60FPS達成率
* 平均フレーム時間
* 95パーセンタイルフレーム時間
* 最悪フレーム時間
* 予算超過回数

### Segment Group評価

* totalSegmentGroupCount
* staticCachedGroupCount
* dynamicGroupCount
* staticCachedRate
* dynamicRate
* cacheHitRate
* falseStaticRate
* watchdogRepairCount

### 境界品質

* staticDynamicBoundaryMeanErrorMm
* staticDynamicBoundaryMaxErrorMm
* curvature discontinuity
* boundary repair success rate

### 見た目品質

* silhouette error
* clump preservation score
* edit locality preservation
* boundary naturalness score

### 接触品質

* penetration count per second
* max penetration depth
* average penetration depth

### メモリ

* peak RAM usage
* directionCacheMemoryMb
* fullPositionMemoryMb
* directionCacheMemorySavingRate

---

# 27. 実装順序

## Phase 1: CPU版Segment Group Runtime

対象ファイル:

* `include/hairgpu/SegmentGroup.h`
* `src/hairgpu_core/SegmentGroup.cpp`
* `HairSystem.vcxproj`
* `HairSystem.vcxproj.filters`

実装内容:

* SegmentGroup構造体
* SegmentGroup生成
* 静的/動的状態遷移
* 静的キャッシュ再構成
* 動的更新
* 基本レポート

## Phase 2: ActiveSetとの接続

対象ファイル:

* `include/hairgpu/SegmentGroup.h`
* `src/hairgpu_core/SegmentGroup.cpp`
* `src/hairgpu_core/ActiveSet.cpp`
* `include/hairgpu/ActiveSet.h`

実装内容:

* `ActiveSetPlan::strandCorrectiveMask` をSegment Groupへ反映
* `recommendedSimulationTiers` をSegment GroupのFrozenLOD判定へ反映
* active strand内のsegmentを優先的にdynamic化

## Phase 3: HybridRuntime修正

対象ファイル:

* `include/hairgpu/HybridRuntime.h`
* `src/hairgpu_core/HybridRuntime.cpp`

実装内容:

* `ApplySparseCorrectivePhysics` で `activeSetPlan` を使う
* `colliders` を使った補正を実装
* Segment Group Runtimeを補正前後に接続
* reportにcorrectedGroup数を追加

## Phase 4: Viewer接続

対象ファイル:

* `apps/dx11_viewer/src/GpuHairViewer.h`
* `apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl`
* `apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl`
* `apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl`

実装内容:

* ViewerがSegment Group Cacheを保持
* 毎フレームSegment Group Runtimeを実行
* PerformanceSampleへ出力
* CSV / JSONL / summaryへ出力

## Phase 5: CLI・評価DB接続

対象ファイル:

* `apps/hairgpu_cli/src/main_parts/01_runtime_suite_and_evaluation_commands.inl`
* `tools/final_stage/schema.sql`
* `tools/final_stage/hairgpu_final_db.py`
* `tools/final_stage/hairgpu_runtime_suite.py`

実装内容:

* CLIオプション追加
* CSV列追加
* DB保存
* Markdownレポート出力

## Phase 6: 論文・設計書用ドキュメント

対象ファイル:

* `docs/SEGMENT_GROUP_RUNTIME.md`
* `docs/sample_assets/segment_group_runtime_preset.json`
* `docs/sample_assets/segment_group_metric_definitions.json`

実装内容:

* 方式説明
* 評価指標説明
* 実験プリセット
* 既存研究との差分整理

---

# 28. 60FPSとの関係

本研究のデータ構造は、60FPSを魔法のように保証するものではない。

正確には、60FPSを守るために、毎フレームどのSegment Groupを再計算し、どのSegment Groupを静的キャッシュとして再利用するかを制御するための実行時データ構造である。

フレーム時間は以下の式で近似できる。

```text
T = C + dG
```

ここで、

* `T`: 総フレーム時間
* `C`: 固定処理時間
* `d`: 1グループあたりの平均更新時間
* `G`: 動的更新するSegment Group数

60FPSを満たすには、

```text
T <= 16.67
```

である必要がある。

したがって、

```text
G <= (16.67 - C) / d
```

を満たすように、動的更新グループ数を制御する。

本研究の価値は、品質を落とさず常に全てを計算することではなく、時間予算内で最も重要なSegment Groupから順に動的更新し、重要度の低いSegment Groupを安全に静的キャッシュへ逃がす点にある。

---

# 29. 最終的な研究主張

本研究は、髪ストランドを単なる点列や一本単位で扱うのではなく、`点-線-点` のSegment Group列として扱う新しい髪データ構造を提案する。

各Segment Groupは、静的キャッシュ、動的更新、境界補正、FrozenLOD、Invalidatedの状態を持ち、編集、接触、貫通、曲率変化、境界誤差、可視性、時間予算に応じて状態遷移する。

静的グループは、始点、方向、長さ、局所座標系を用いて軽量に再構成し、再計算を省略する。一方、動的グループは、必要に応じて局所補正物理を適用する。

さらに、静的グループと動的グループの共有点を半球境界として扱い、境界誤差を監視・修復することで、髪の連続性を保つ。

これにより、髪全体を毎フレーム一様に再計算する方式に比べて、固定60FPS制約下で再計算対象を局所化し、自由な髪編集、接触補正、自然な実時間再現の両立を目指す。

---

# 30. まとめ

本研究の中心は、ActiveSetだけではない。
ActiveSetは「どの髪領域を重点的に見るか」を決める上位機構である。

本研究の本当のコアは、髪一本の内部を `点-線-点` Segment Groupへ分割し、各グループを静的キャッシュ・動的更新・境界補正として状態管理する **静的・動的セグメントグループ型髪データ構造** にある。

実装では、まず `SegmentGroup.h` と `SegmentGroup.cpp` を追加し、既存の `HairAsset`、`ActiveSetPlan`、`HybridRuntime`、`GpuHairViewer`、評価CSV、SQLite DBへ接続する。

この構成により、研究としては以下を主張できる。

* 髪ストランド内部の再計算粒度を明確に定義した。
* 静的キャッシュと動的更新をSegment Group単位で切り替えた。
* 半球境界により静的/動的の接続破綻を抑えた。
* watchdogにより誤静的判定を検出・修復した。
* 60FPS予算とデータ構造を直接結びつけた。
* ActiveSetよりも細かい粒度で、髪の再計算量を制御できる。

以上により、本研究は、固定60FPS・32GB制約下で編集可能なリアルタイム髪システムを実現するための、新しい髪データ構造と実行時更新方式を提案するものである。
