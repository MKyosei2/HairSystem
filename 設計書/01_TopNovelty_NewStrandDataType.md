# 固定60FPS・32GB制約下における

# Segment Group Runtime Data Typeを用いた編集可能ハイブリッド髪システムの研究

— 髪ストランド内部の `点-線-点` グループを実行時処理経路選択単位として扱うデータ構造の設計と評価 —

---

# 0. 対象リポジトリ・対象ブランチ・対象バージョン

本企画書兼設計書は、以下の実装環境を対象とする。

```text
Repository : MKyosei2/HairSystem
Branch     : Novelty
Version    : 61.44
Commit     : 724c7a16fc96cafc74f50a1c3e77bd8875d23a56
対象ファイル : 設計書/01_TopNovelty_NewStrandDataType.md
```

主な実装対象は以下である。

```text
include/hairgpu/
src/hairgpu_core/
apps/dx11_viewer/src/
apps/dx11_viewer/src/GpuHairViewer_parts/
apps/hairgpu_cli/src/main_parts/
tools/final_stage/
docs/sample_assets/
```

本設計書は、既存のHairSystemに存在する以下の機能を前提とする。

```text
Guide / Follower hair representation
ActiveSet Runtime
HybridRuntime
DX11 Viewer
CLI evaluation tools
Runtime telemetry
CSV / JSONL / SQLite / Markdown report pipeline
```

---

# 1. 研究テーマ

本研究のテーマは、以下のように定義する。

```text
固定60FPS・32GB制約下における
Segment Group Runtime Data Typeを用いた
編集可能ハイブリッド髪システムの研究
```

本研究では、髪ストランドを単なる点列として扱うのではなく、髪一本の内部を `点-線-点` の小さなグループに分割し、それぞれのグループが実行時に次のいずれの処理経路へ進むべきかを判断できるデータ構造を設計・実装する。

```text
Static Reuse
Static Transform
Dynamic Solve
Deferred Correction
Boundary Repair
Fallback
Frozen LOD
Rejected
```

この小さな単位を、本研究では **Segment Group** と呼ぶ。

---

# 2. 安全な新規性の主張

## 2.1 本研究で新規性として主張すること

本研究の新規性は、以下の点に限定して主張する。

```text
髪ストランド内部の Segment Group を、
単なる幾何表現や物理制約単位ではなく、
実行時の処理経路を選択する Runtime Decision Unit として扱う点。
```

つまり、本研究は「髪を点列やsegmentで表すこと」自体を新規性とはしない。

また、「静的なものを再計算しないこと」や「GPUで計算すること」自体も新規性とはしない。

本研究の中心は、以下である。

```text
各 Segment Group が、
自身の状態、cache validity、certificate、境界誤差、接触リスク、
編集影響、前回更新時刻、false-static判定結果を持ち、
Static / Dynamic / Repair / Fallback を実行時に選べる髪専用データ型を作ること。
```

## 2.2 本研究で新規性として主張しないこと

以下は既存研究・既存技術に含まれるため、本研究の新規性としては主張しない。

```text
髪を点列として扱うこと
髪をsegmentとして扱うこと
髪をrodとして扱うこと
Guide / Follower型
LOD
Active Set
GPU multi-pass simulation
PBD / XPBD
制約残差
cache reuse
sleeping
partial update
fallback
```

これらは本研究の前提技術である。

## 2.3 本研究の安全な主張

本研究の安全な主張は、次の一文にまとめられる。

```text
本研究は、髪ストランド内部を Segment Group 単位に分解し、
各グループに certificate と cache validity を持たせることで、
Static reuse、Dynamic solve、Deferred correction、Repair、Fallback を
同一データ構造内で切り替える runtime decision data type を提案する。
さらに、false-static率、Static/Dynamic境界誤差、cache reuse率を
telemetryとして記録することで、再利用の安全性を評価可能にする。
```

---

# 3. 研究背景

髪シミュレーションでは、髪一本を複数の点からなる点列として扱うことが多い。

```text
P0 - P1 - P2 - P3 - P4 - ... - Pn
```

髪全体を毎フレーム高精度に計算すると、接触や形状保持の品質は高くなるが、計算負荷が大きくなる。

固定60FPSでは、1フレームあたりの処理時間は約16.67ms以内に収める必要がある。

```text
60 FPS = 16.67 ms / frame
```

そのため、すべての髪を毎フレーム同じ精度で処理する方式は、髪本数や点数が増えるほど不利になる。

一方で、単純に動かない部分を再計算しないだけでは、以下の問題が発生する。

```text
静的と判断した部分が実際には動くべきだった
静的部分と動的部分の境界が折れる
接触や貫通を見逃す
編集した部分だけが不自然に変形する
cacheが古くなり、形状が破綻する
```

本研究では、この問題に対して、髪一本の内部をより細かいSegment Group単位に分け、各グループごとに処理経路を判断する。

---

# 4. 本研究で作るもの

本研究で作る中心成果物は、以下のデータ型である。

```text
Segment Group Runtime Data Type
```

これは、髪の各 `点-線-点` グループが、単なる形状情報だけではなく、実行時の判断情報を持つデータ型である。

## 4.1 基本表現

髪一本を以下のように分割する。

```text
G0 = P0 - line0 - P1
G1 = P1 - line1 - P2
G2 = P2 - line2 - P3
G3 = P3 - line3 - P4
...
```

各 `G` が Segment Group である。

## 4.2 共有点の扱い

隣接するSegment Groupは点を共有する。

```text
G0 = P0 - line0 - P1
G1 = P1 - line1 - P2
```

このとき、`P1` は `G0` の終点であり、`G1` の始点でもある。

本研究では、この共有点を単なる一点として扱うのではなく、前後の線分方向に応じた **半球境界** として扱う。

これにより、片側がStatic、もう片側がDynamicになった場合でも、境界で折れや段差が出ることを抑える。

---

# 5. Segment Groupの状態

各Segment Groupは、以下の状態を持つ。

```cpp
enum class SegmentGroupState : uint32_t
{
    StaticCached = 0,
    StaticTransformed = 1,
    DynamicSolved = 2,
    DeferredCorrection = 3,
    BoundaryRepair = 4,
    Fallback = 5,
    FrozenLod = 6,
    Rejected = 7,
    Invalidated = 8
};
```

## 5.1 StaticCached

前回の有効な結果をそのまま再利用する状態。

使用条件は以下。

```text
接触リスクが低い
編集影響が低い
境界誤差が小さい
cacheが古すぎない
前フレームとの差分が小さい
```

## 5.2 StaticTransformed

形状は再計算せず、始点の移動だけを反映して終点を再構成する状態。

```text
end = start + cachedDirection * cachedLength
```

髪全体が頭部移動やroot移動に追従する場合に使用する。

## 5.3 DynamicSolved

そのフレームで実際に局所補正を行う状態。

使用条件は以下。

```text
編集された
接触している可能性がある
貫通がある
曲率誤差が大きい
ActiveSetで補正対象になった
```

## 5.4 DeferredCorrection

今フレームでは完全な動的更新を行わず、軽量な補正だけを行う状態。

使用条件は以下。

```text
動的更新したいがフレーム予算が足りない
境界誤差はあるが致命的ではない
次フレーム以降にDynamicSolveへ送ってもよい
```

## 5.5 BoundaryRepair

Static部分とDynamic部分の境界を補正する状態。

使用条件は以下。

```text
共有点の位置差が大きい
方向差が大きい
曲率の連続性が崩れている
```

## 5.6 Fallback

軽量処理では危険なため、安全側の処理へ戻す状態。

使用条件は以下。

```text
NaN / Inf が発生した
大きな貫通が発生した
cacheが破損した
境界修復に失敗した
false-staticが連続発生した
```

## 5.7 FrozenLod

遠距離または不可視のため、更新を停止する状態。

## 5.8 Rejected

今フレームの更新予算に入らなかった状態。

ただし、Rejectedが長く続くと破綻するため、最大保留フレーム数を超えたらFallbackまたはDynamicSolvedへ送る。

---

# 6. Segment Groupが持つ情報

各Segment Groupは、以下の情報を持つ。

```cpp
struct SegmentGroupRecord
{
    uint32_t groupIndex = 0;
    uint32_t strandIndex = 0;
    uint32_t localSegmentIndex = 0;

    uint32_t startPointIndex = 0;
    uint32_t endPointIndex = 0;

    Vec3 anchorPoint = {};
    Vec3 direction = {0.0f, -1.0f, 0.0f};
    float length = 0.0f;

    Vec3 localXAxis = {1.0f, 0.0f, 0.0f};
    Vec3 localYAxis = {0.0f, 1.0f, 0.0f};
    Vec3 localZAxis = {0.0f, 0.0f, 1.0f};

    Vec3 lastValidStartPoint = {};
    Vec3 lastValidEndPoint = {};

    SegmentGroupState state = SegmentGroupState::StaticCached;

    uint32_t invalidationMask = 0;

    float editRisk = 0.0f;
    float contactRisk = 0.0f;
    float penetrationRisk = 0.0f;
    float visibilityRisk = 0.0f;

    float lengthResidual = 0.0f;
    float bendResidual = 0.0f;
    float curvatureResidual = 0.0f;
    float penetrationResidual = 0.0f;

    float boundaryPositionError = 0.0f;
    float boundaryDirectionError = 0.0f;
    float boundaryCurvatureError = 0.0f;

    float certificateValue = 1.0f;
    float cacheValidity = 1.0f;
    float cacheConfidence = 1.0f;

    uint32_t cacheAge = 0;
    uint32_t lastUpdatedFrame = 0;
    uint32_t rejectedFrameCount = 0;

    bool falseStaticDetected = false;
};
```

---

# 7. Certificateの定義

本研究では、certificateという言葉を使う。

ただし、ここでのcertificateは数学的な完全証明ではない。

本研究におけるcertificateは、以下をまとめた **runtime validity descriptor** である。

```text
このSegment GroupをStaticとして再利用しても安全かどうかを表す実行時評価値
```

## 7.1 certificateに含める要素

```text
lengthResidual
bendResidual
curvatureResidual
penetrationResidual
boundaryPositionError
boundaryDirectionError
editRisk
contactRisk
cacheAge
visibilityRisk
previousFrameMotion
```

## 7.2 certificateValueの例

```cpp
certificateValue =
    1.0f
    - wLength      * lengthResidual
    - wBend        * bendResidual
    - wCurvature   * curvatureResidual
    - wPenetration * penetrationResidual
    - wBoundary    * boundaryPositionError
    - wEdit        * editRisk
    - wContact     * contactRisk
    - wCacheAge    * normalizedCacheAge;
```

値は `0.0f` から `1.0f` にclampする。

```text
1.0 に近いほどStatic reuseが安全
0.0 に近いほどDynamic / Repair / Fallbackが必要
```

---

# 8. Static / Dynamic 判定

Segment Groupの状態は、以下の優先順位で決める。

```text
1. 致命的エラーがある → Fallback
2. ActiveSetで補正対象 → DynamicSolved
3. 編集影響が大きい → DynamicSolved
4. 接触・貫通リスクが大きい → DynamicSolved
5. 境界誤差が大きい → BoundaryRepair
6. フレーム予算不足だが軽量補正可能 → DeferredCorrection
7. 遠距離・不可視 → FrozenLod
8. certificateが高い → StaticCached / StaticTransformed
9. それ以外 → DynamicSolved
```

疑似コードは以下。

```cpp
SegmentGroupState DecideSegmentGroupState(
    const SegmentGroupRecord& group,
    const SegmentGroupSettings& settings,
    const FrameBudgetState& budget)
{
    if (HasFatalError(group))
    {
        return SegmentGroupState::Fallback;
    }

    if (group.editRisk >= settings.editRiskThreshold)
    {
        return SegmentGroupState::DynamicSolved;
    }

    if (group.contactRisk >= settings.contactRiskThreshold ||
        group.penetrationRisk >= settings.penetrationRiskThreshold)
    {
        return SegmentGroupState::DynamicSolved;
    }

    if (group.boundaryPositionError >= settings.boundaryPositionErrorThreshold ||
        group.boundaryDirectionError >= settings.boundaryDirectionErrorThreshold)
    {
        return SegmentGroupState::BoundaryRepair;
    }

    if (!budget.CanAcceptDynamicGroup())
    {
        if (group.certificateValue >= settings.deferredCertificateThreshold)
        {
            return SegmentGroupState::DeferredCorrection;
        }

        return SegmentGroupState::Rejected;
    }

    if (group.certificateValue >= settings.staticCertificateThreshold)
    {
        return SegmentGroupState::StaticTransformed;
    }

    return SegmentGroupState::DynamicSolved;
}
```

---

# 9. 60FPS制約との関係

本研究のデータ構造は、60FPSを自動的に保証するものではない。

本研究の目的は、60FPSを守るために、毎フレーム動的更新するSegment Group数を制御できるようにすることである。

フレーム時間は、以下で近似する。

```text
T = C + dG + rR
```

ここで、

```text
T : 総フレーム時間
C : 固定処理時間
d : DynamicSolved 1 groupあたりの平均時間
G : DynamicSolved group数
r : Repair 1 groupあたりの平均時間
R : Repair group数
```

60FPSを満たすには、

```text
T <= 16.67 ms
```

である必要がある。

したがって、

```text
C + dG + rR <= 16.67
```

となるように、動的更新数を制限する。

## 9.1 古いPC向けの安全設定

古いPCでは、以下の初期値を推奨する。

```cpp
settings.maxDynamicGroupRatio = 0.12f;
settings.maxRepairGroupRatio = 0.08f;
settings.maxRejectedFrameCount = 3;
settings.maxCacheAgeFrames = 6;
settings.staticCertificateThreshold = 0.82f;
settings.dynamicCertificateThreshold = 0.55f;
settings.fallbackCertificateThreshold = 0.25f;
```

つまり、初期実装では全Segment Groupのうち、毎フレームDynamicに送るのは最大12%程度に制限する。

---

# 10. 実装ファイル一覧

## 10.1 新規追加ファイル

```text
include/hairgpu/SegmentGroup.h
src/hairgpu_core/SegmentGroup.cpp
docs/SEGMENT_GROUP_RUNTIME.md
docs/sample_assets/segment_group_runtime_preset.json
docs/sample_assets/segment_group_metric_definitions.json
```

## 10.2 修正する既存ファイル

```text
HairSystem.vcxproj
HairSystem.vcxproj.filters

include/hairgpu/HybridRuntime.h
src/hairgpu_core/HybridRuntime.cpp

include/hairgpu/HairNoveltyTelemetry.h
src/hairgpu_core/HairNoveltyTelemetry.cpp

apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl
apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl
apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl

apps/hairgpu_cli/src/main_parts/01_runtime_suite_and_evaluation_commands.inl

tools/final_stage/schema.sql
tools/final_stage/hairgpu_final_db.py
tools/final_stage/hairgpu_runtime_suite.py

docs/sample_assets/metric_definitions.json
```

---

# 11. 新規ファイル: include/hairgpu/SegmentGroup.h

このファイルには、Segment Group Runtimeの型、設定、入力、出力、cache、関数宣言を定義する。

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
        StaticTransformed = 1,
        DynamicSolved = 2,
        DeferredCorrection = 3,
        BoundaryRepair = 4,
        Fallback = 5,
        FrozenLod = 6,
        Rejected = 7,
        Invalidated = 8
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
        Visibility = 1 << 8,
        Fatal = 1 << 9
    };

    struct SegmentGroupSettings
    {
        uint32_t segmentsPerGroup = 1u;

        uint32_t maxCacheAgeFrames = 6u;
        uint32_t maxRejectedFrameCount = 3u;

        float staticVelocityThreshold = 0.012f;
        float staticDisplacementThreshold = 0.010f;

        float editRiskThreshold = 0.20f;
        float contactRiskThreshold = 0.35f;
        float penetrationRiskThreshold = 0.20f;

        float boundaryPositionErrorThreshold = 0.006f;
        float boundaryDirectionErrorThreshold = 0.18f;
        float curvatureErrorThreshold = 0.22f;

        float staticCertificateThreshold = 0.82f;
        float deferredCertificateThreshold = 0.68f;
        float fallbackCertificateThreshold = 0.25f;

        float staticCacheBlend = 0.92f;
        float boundaryRepairBlend = 0.55f;
        float dynamicSolveBlend = 0.85f;

        float hemisphereRadiusScale = 0.50f;
        float minSegmentLength = 1.0e-5f;

        float maxDynamicGroupRatio = 0.12f;
        float maxRepairGroupRatio = 0.08f;

        bool enableHemisphereBoundary = true;
        bool enableWatchdogRepair = true;
        bool enableDirectionCache = true;
        bool enableFrozenLod = true;
        bool enableHardBudget = true;
    };

    struct SegmentGroupRecord
    {
        uint32_t groupIndex = 0u;
        uint32_t strandIndex = 0u;
        uint32_t localSegmentIndex = 0u;

        uint32_t startPointIndex = 0u;
        uint32_t endPointIndex = 0u;

        Vec3 anchorPoint = {0.0f, 0.0f, 0.0f};
        Vec3 direction = {0.0f, -1.0f, 0.0f};
        float length = 0.0f;

        Vec3 localXAxis = {1.0f, 0.0f, 0.0f};
        Vec3 localYAxis = {0.0f, 1.0f, 0.0f};
        Vec3 localZAxis = {0.0f, 0.0f, 1.0f};

        Vec3 lastValidStartPoint = {0.0f, 0.0f, 0.0f};
        Vec3 lastValidEndPoint = {0.0f, 0.0f, 0.0f};

        SegmentGroupState state = SegmentGroupState::StaticCached;
        uint32_t invalidationMask = 0u;

        float editRisk = 0.0f;
        float contactRisk = 0.0f;
        float penetrationRisk = 0.0f;
        float visibilityRisk = 0.0f;

        float lengthResidual = 0.0f;
        float bendResidual = 0.0f;
        float curvatureResidual = 0.0f;
        float penetrationResidual = 0.0f;

        float boundaryPositionError = 0.0f;
        float boundaryDirectionError = 0.0f;
        float boundaryCurvatureError = 0.0f;

        float certificateValue = 1.0f;
        float cacheValidity = 1.0f;
        float cacheConfidence = 1.0f;

        uint32_t cacheAge = 0u;
        uint32_t lastUpdatedFrame = 0u;
        uint32_t rejectedFrameCount = 0u;

        bool falseStaticDetected = false;
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

        Vec3 cameraPosition = {0.0f, 1.6f, -1.8f};
        Vec3 cameraForward = {0.0f, 0.0f, 1.0f};

        float fixedTimeStep = 1.0f / 60.0f;
        float frameBudgetMilliseconds = 16.67f;
        float estimatedFixedCostMilliseconds = 6.0f;

        uint32_t frameIndex = 0u;
    };

    struct SegmentGroupRuntimeReport
    {
        uint32_t totalSegmentGroupCount = 0u;

        uint32_t staticCachedGroupCount = 0u;
        uint32_t staticTransformedGroupCount = 0u;
        uint32_t dynamicGroupCount = 0u;
        uint32_t deferredCorrectionGroupCount = 0u;
        uint32_t boundaryRepairGroupCount = 0u;
        uint32_t fallbackGroupCount = 0u;
        uint32_t frozenGroupCount = 0u;
        uint32_t rejectedGroupCount = 0u;
        uint32_t invalidatedGroupCount = 0u;

        uint32_t cacheHitCount = 0u;
        uint32_t cacheMissCount = 0u;
        uint32_t cacheInvalidationCount = 0u;

        uint32_t watchdogCheckCount = 0u;
        uint32_t watchdogRepairCount = 0u;
        uint32_t falseStaticCount = 0u;

        float falseStaticRate = 0.0f;
        float staticReuseRate = 0.0f;
        float dynamicGroupRate = 0.0f;

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

    bool BuildSegmentGroupsFromAsset(
        const HairAsset& asset,
        const SegmentGroupSettings& settings,
        SegmentGroupRuntimeCache& outCache,
        std::string& outError);

    bool UpdateSegmentGroupRuntime(
        const SegmentGroupRuntimeInput& input,
        const SegmentGroupSettings& settings,
        SegmentGroupRuntimeCache& cache,
        SegmentGroupRuntimeReport& outReport,
        std::string& outError);

    bool WriteSegmentGroupRuntimeCsv(
        const std::filesystem::path& outputPath,
        const SegmentGroupRuntimeCache& cache,
        const SegmentGroupRuntimeReport& report,
        std::string& outError);

    std::string BuildSegmentGroupRuntimeSummary(
        const SegmentGroupRuntimeReport& report);
}
```

---

# 12. 新規ファイル: src/hairgpu_core/SegmentGroup.cpp

このファイルには、以下の関数を実装する。

```text
BuildSegmentGroupsFromAsset
UpdateSegmentGroupRuntime
EvaluateSegmentGroupRisk
ComputeSegmentGroupCertificate
DecideSegmentGroupState
ApplyStaticCache
ApplyStaticTransform
ApplyDynamicSegmentSolve
ApplyDeferredCorrection
RepairStaticDynamicBoundary
ApplyFallback
DetectFalseStatic
FinalizeSegmentGroupReport
WriteSegmentGroupRuntimeCsv
BuildSegmentGroupRuntimeSummary
```

---

# 13. BuildSegmentGroupsFromAsset

`HairAsset::strands` と `HairAsset::restPoints` からSegment Groupを構築する。

```cpp
bool BuildSegmentGroupsFromAsset(
    const HairAsset& asset,
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

    const uint32_t groupStep = std::max(1u, settings.segmentsPerGroup);
    uint32_t globalGroupIndex = 0u;

    for (uint32_t strandIndex = 0u; strandIndex < asset.strands.size(); ++strandIndex)
    {
        const StrandRange& strand = asset.strands[strandIndex];

        if (strand.pointCount < 2u)
        {
            continue;
        }

        for (uint32_t local = 0u; local + 1u < strand.pointCount; local += groupStep)
        {
            const uint32_t startIndex = strand.pointOffset + local;
            const uint32_t endLocal = std::min(local + groupStep, strand.pointCount - 1u);
            const uint32_t endIndex = strand.pointOffset + endLocal;

            if (startIndex >= asset.restPoints.size() || endIndex >= asset.restPoints.size())
            {
                continue;
            }

            const Vec3 start = asset.restPoints[startIndex];
            const Vec3 end = asset.restPoints[endIndex];
            const Vec3 delta = end - start;
            const float length = std::max(Length(delta), settings.minSegmentLength);

            SegmentGroupRecord record = {};
            record.groupIndex = globalGroupIndex++;
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
            record.cacheValidity = 1.0f;
            record.cacheConfidence = 1.0f;
            record.certificateValue = 1.0f;

            outCache.groups.push_back(record);
        }
    }

    outCache.lastBuiltStrandCount = static_cast<uint32_t>(asset.strands.size());
    outCache.lastBuiltPointCount = static_cast<uint32_t>(asset.restPoints.size());

    return true;
}
```

---

# 14. UpdateSegmentGroupRuntime

毎フレーム呼び出す中心関数。

処理順は以下。

```text
1. 入力チェック
2. Segment Group cacheが未構築ならBuildする
3. 各Groupのriskを計算
4. certificateを計算
5. フレーム予算に基づいて状態を決める
6. 状態ごとの処理を行う
7. false-staticを検出
8. reportを集計
```

```cpp
bool UpdateSegmentGroupRuntime(
    const SegmentGroupRuntimeInput& input,
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

    outReport.totalSegmentGroupCount = static_cast<uint32_t>(cache.groups.size());

    const uint32_t maxDynamicGroups =
        static_cast<uint32_t>(cache.groups.size() * settings.maxDynamicGroupRatio);

    const uint32_t maxRepairGroups =
        static_cast<uint32_t>(cache.groups.size() * settings.maxRepairGroupRatio);

    uint32_t usedDynamicGroups = 0u;
    uint32_t usedRepairGroups = 0u;

    for (SegmentGroupRecord& group : cache.groups)
    {
        EvaluateSegmentGroupRisk(input, settings, group);
        ComputeSegmentGroupCertificate(input, settings, group);

        const bool dynamicBudgetAvailable = usedDynamicGroups < maxDynamicGroups;
        const bool repairBudgetAvailable = usedRepairGroups < maxRepairGroups;

        group.state = DecideSegmentGroupState(
            group,
            settings,
            dynamicBudgetAvailable,
            repairBudgetAvailable);

        switch (group.state)
        {
        case SegmentGroupState::StaticCached:
            ApplyStaticCache(settings, group, points, outReport);
            break;

        case SegmentGroupState::StaticTransformed:
            ApplyStaticTransform(settings, group, points, outReport);
            break;

        case SegmentGroupState::DynamicSolved:
            ApplyDynamicSegmentSolve(input, settings, group, points, outReport);
            ++usedDynamicGroups;
            break;

        case SegmentGroupState::DeferredCorrection:
            ApplyDeferredCorrection(input, settings, group, points, outReport);
            break;

        case SegmentGroupState::BoundaryRepair:
            RepairStaticDynamicBoundary(input, settings, group, points, outReport);
            ++usedRepairGroups;
            break;

        case SegmentGroupState::Fallback:
            ApplyFallback(input, settings, group, points, outReport);
            ++usedDynamicGroups;
            break;

        case SegmentGroupState::FrozenLod:
            ApplyStaticCache(settings, group, points, outReport);
            ++outReport.frozenGroupCount;
            break;

        case SegmentGroupState::Rejected:
            ++group.rejectedFrameCount;
            ++outReport.rejectedGroupCount;
            break;

        case SegmentGroupState::Invalidated:
            ApplyDynamicSegmentSolve(input, settings, group, points, outReport);
            ++usedDynamicGroups;
            break;
        }

        DetectFalseStatic(input, settings, group, points, outReport);

        group.cacheAge =
            (group.state == SegmentGroupState::StaticCached ||
             group.state == SegmentGroupState::StaticTransformed ||
             group.state == SegmentGroupState::FrozenLod)
            ? group.cacheAge + 1u
            : 0u;

        group.lastUpdatedFrame = input.frameIndex;
    }

    FinalizeSegmentGroupReport(settings, cache, outReport);
    return true;
}
```

---

# 15. ApplyStaticTransform

静的グループでは、全点を再計算せず、始点・方向・長さから終点を復元する。

```cpp
void ApplyStaticTransform(
    const SegmentGroupSettings& settings,
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

    ++report.staticTransformedGroupCount;
    ++report.cacheHitCount;
}
```

---

# 16. ApplyDynamicSegmentSolve

動的グループでは、長さ保持、接触補正、floor補正、collider補正、曲率補正を行う。

```cpp
void ApplyDynamicSegmentSolve(
    const SegmentGroupRuntimeInput& input,
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

# 17. Boundary Repair

Static部分とDynamic部分の境界を補正する。

```cpp
void RepairStaticDynamicBoundary(
    const SegmentGroupRuntimeInput& input,
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

# 18. HybridRuntimeとの接続

現状の `ApplySparseCorrectivePhysics` は、`ActiveSetPlan` と `ColliderSet` を十分に使えていないため、Segment Group Runtimeをここに接続する。

## 18.1 修正対象

```text
src/hairgpu_core/HybridRuntime.cpp
include/hairgpu/HybridRuntime.h
```

## 18.2 修正方針

`HybridRuntime.cpp` に以下を追加する。

```cpp
#include "hairgpu/SegmentGroup.h"
```

`ApplySparseCorrectivePhysics` では、以下を行う。

```text
1. ActiveSetPlan::strandCorrectiveMask を見る
2. corrective対象strandのriskを上げる
3. SegmentGroupRuntimeInputを作る
4. UpdateSegmentGroupRuntimeを呼ぶ
5. SparseCorrectiveReportにSegmentGroup結果を反映する
```

重要点として、`SegmentGroupRuntimeCache` はローカル変数にしない。

毎フレームcacheを維持する必要があるため、最終的にはViewerまたはRuntime側のメンバとして保持する。

---

# 19. GpuHairViewerとの接続

## 19.1 修正対象

```text
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl
```

## 19.2 追加メンバ

`GpuHairViewer` に以下を追加する。

```cpp
hairgpu::SegmentGroupSettings m_segmentGroupSettings = {};
hairgpu::SegmentGroupRuntimeCache m_segmentGroupCache = {};
hairgpu::SegmentGroupRuntimeReport m_lastSegmentGroupReport = {};
bool m_enableSegmentGroupRuntime = true;
```

## 19.3 毎フレーム呼び出し

シミュレーション更新後、描画前に以下を呼ぶ。

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
    input.frameBudgetMilliseconds = 16.67f;
    input.frameIndex = m_globalFrameIndex;

    std::string segmentError;
    if (hairgpu::UpdateSegmentGroupRuntime(
            input,
            m_segmentGroupSettings,
            m_segmentGroupCache,
            m_lastSegmentGroupReport,
            segmentError))
    {
        CopySegmentGroupReportToTelemetry(m_lastSegmentGroupReport, m_noveltyTelemetry);
    }
}
```

---

# 20. Telemetry項目

以下を `HairNoveltyTelemetry` に含める。

```cpp
uint32_t totalSegmentGroupCount = 0u;
uint32_t staticCachedGroupCount = 0u;
uint32_t staticTransformedGroupCount = 0u;
uint32_t dynamicGroupCount = 0u;
uint32_t deferredCorrectionGroupCount = 0u;
uint32_t boundaryRepairGroupCount = 0u;
uint32_t fallbackGroupCount = 0u;
uint32_t frozenGroupCount = 0u;
uint32_t rejectedGroupCount = 0u;

uint32_t falseStaticCount = 0u;
float falseStaticRate = 0.0f;

uint32_t cacheHitCount = 0u;
uint32_t cacheMissCount = 0u;
uint32_t cacheInvalidationCount = 0u;

uint32_t watchdogCheckCount = 0u;
uint32_t watchdogRepairCount = 0u;

float staticDynamicBoundaryMeanErrorMm = 0.0f;
float staticDynamicBoundaryMaxErrorMm = 0.0f;

float directionCacheMemoryMb = 0.0f;
float fullPositionMemoryMb = 0.0f;
float directionCacheMemorySavingRate = 0.0f;

float segmentGroupRuntimeMilliseconds = 0.0f;
```

---

# 21. CSV / JSONL / SQLiteへの出力

## 21.1 CSV列

```text
totalSegmentGroupCount
staticCachedGroupCount
staticTransformedGroupCount
dynamicGroupCount
deferredCorrectionGroupCount
boundaryRepairGroupCount
fallbackGroupCount
frozenGroupCount
rejectedGroupCount
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
segmentGroupRuntimeMilliseconds
```

## 21.2 SQLiteカラム

`tools/final_stage/schema.sql` の `runtime_frame_metrics` に以下を追加する。

```sql
total_segment_group_count INTEGER DEFAULT 0,
static_cached_group_count INTEGER DEFAULT 0,
static_transformed_group_count INTEGER DEFAULT 0,
dynamic_group_count INTEGER DEFAULT 0,
deferred_correction_group_count INTEGER DEFAULT 0,
boundary_repair_group_count INTEGER DEFAULT 0,
fallback_group_count INTEGER DEFAULT 0,
frozen_group_count INTEGER DEFAULT 0,
rejected_group_count INTEGER DEFAULT 0,
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
direction_cache_memory_saving_rate REAL DEFAULT 0.0,
segment_group_runtime_milliseconds REAL DEFAULT 0.0
```

---

# 22. 評価実験

## 22.1 比較対象

以下の4方式を比較する。

### A. Full Dynamic

全Segment Groupを毎フレーム動的更新する。

### B. ActiveSet Only

ActiveSetで補正対象strandを選ぶが、strand内部のSegment Group分割は行わない。

### C. Segment Cache Only

Segment GroupのStatic reuseは行うが、certificate、false-static検出、Boundary Repairを使わない。

### D. Proposed

Segment Group、certificate、cache validity、Static/Dynamic状態遷移、Boundary Repair、Fallback、false-static telemetryをすべて使う。

---

# 23. 評価指標

## 23.1 実時間性

```text
60FPS達成率
平均フレーム時間
95パーセンタイルフレーム時間
最大フレーム時間
予算超過回数
segmentGroupRuntimeMilliseconds
```

## 23.2 再利用性能

```text
staticCachedGroupCount
staticTransformedGroupCount
dynamicGroupCount
staticReuseRate
dynamicGroupRate
cacheHitCount
cacheMissCount
cacheHitRate
```

## 23.3 再利用の安全性

```text
falseStaticCount
falseStaticRate
watchdogRepairCount
fallbackGroupCount
rejectedGroupCount
```

## 23.4 境界品質

```text
staticDynamicBoundaryMeanErrorMm
staticDynamicBoundaryMaxErrorMm
boundaryRepairGroupCount
boundaryRepairSuccessRate
```

## 23.5 接触品質

```text
penetration count
max penetration depth
average penetration depth
contact repair count
```

## 23.6 メモリ

```text
peak RAM usage
directionCacheMemoryMb
fullPositionMemoryMb
directionCacheMemorySavingRate
```

---

# 24. 実装順序

## Phase 1: CPU版Segment Group Runtime

対象ファイル:

```text
include/hairgpu/SegmentGroup.h
src/hairgpu_core/SegmentGroup.cpp
HairSystem.vcxproj
HairSystem.vcxproj.filters
```

実装内容:

```text
SegmentGroup構造体
SegmentGroup生成
Static / Dynamic判定
StaticTransform
DynamicSolve
BoundaryRepair
Fallback
Report集計
```

## Phase 2: ActiveSet接続

対象ファイル:

```text
include/hairgpu/SegmentGroup.h
src/hairgpu_core/SegmentGroup.cpp
include/hairgpu/ActiveSet.h
src/hairgpu_core/ActiveSet.cpp
```

実装内容:

```text
ActiveSetPlan::strandCorrectiveMask をSegment Groupへ反映
recommendedSimulationTiers をFrozenLOD判定へ反映
Active strand内のSegment Groupを優先的にDynamic化
```

## Phase 3: HybridRuntime接続

対象ファイル:

```text
include/hairgpu/HybridRuntime.h
src/hairgpu_core/HybridRuntime.cpp
```

実装内容:

```text
ApplySparseCorrectivePhysics内でSegmentGroupRuntimeを呼ぶ
collider補正をSegment Groupに接続
SparseCorrectiveReportにSegmentGroup結果を反映
```

## Phase 4: Viewer接続

対象ファイル:

```text
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl
apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl
apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl
```

実装内容:

```text
ViewerがSegmentGroupCacheを保持
毎フレームSegmentGroupRuntimeを実行
PerformanceSampleへ出力
CSV / JSONL / summaryへ出力
```

## Phase 5: CLI / DB / Report接続

対象ファイル:

```text
apps/hairgpu_cli/src/main_parts/01_runtime_suite_and_evaluation_commands.inl
tools/final_stage/schema.sql
tools/final_stage/hairgpu_final_db.py
tools/final_stage/hairgpu_runtime_suite.py
```

実装内容:

```text
CLIオプション追加
CSV列追加
DB保存
Markdownレポート出力
```

## Phase 6: GPU化

対象ファイル:

```text
apps/dx11_viewer/shaders/SegmentGroupCertificateQueryCS.hlsl
apps/dx11_viewer/shaders/SegmentGroupStateDecisionCS.hlsl
apps/dx11_viewer/shaders/SegmentGroupApplyResultCS.hlsl
```

実装内容:

```text
GPU上でcertificateを計算
GPU上で状態判定
Group result bufferへ書き込み
Apply passでpoint bufferへ反映
CPU decisionとGPU decisionの一致率を評価
```

---

# 25. 実装上の重要注意点

## 25.1 最初からGPU実装にしない

最初はCPU版で正しさを確認する。

理由は以下。

```text
状態遷移のデバッグがしやすい
false-static検出を確認しやすい
CSV出力と比較しやすい
GPU版との一致検証に使える
```

## 25.2 cacheはローカル変数にしない

`SegmentGroupRuntimeCache` は毎フレーム維持する必要がある。

したがって、以下は避ける。

```cpp
SegmentGroupRuntimeCache cache; // 毎フレーム関数内で作るのはNG
```

正しくは、ViewerやRuntimeのメンバとして保持する。

```cpp
hairgpu::SegmentGroupRuntimeCache m_segmentGroupCache;
```

## 25.3 Static判定は安全側に倒す

Static reuseを増やしすぎると、60FPSは出やすくなるが品質が壊れる。

そのため、初期値ではStatic判定を厳しめにする。

```text
staticCertificateThreshold = 0.82
maxCacheAgeFrames = 6
falseStaticRate が高い場合は threshold を上げる
```

## 25.4 Rejectedを放置しない

フレーム予算に入らなかったGroupをRejectedにするだけでは危険である。

```text
Rejectedが3フレーム以上続いたらDynamicSolvedまたはFallbackへ送る
```

---

# 26. 予想される60FPS達成確率

少し古いPCを想定した場合、実装段階ごとの60FPS安定見込みは以下である。

```text
CPU版Segment Groupのみ:
35〜50%

CPU版 + ActiveSet接続 + hard budget:
55〜65%

GPU版 + hard budget + fallback + telemetry:
65〜75%

高密度髪 + self collision + 複数collider + 編集あり:
30〜45%
```

本研究では、60FPSを絶対保証するのではなく、60FPSを維持するために、動的更新対象を制御できるデータ構造を提案する。

---

# 27. 研究としての最終主張

本研究の最終主張は以下である。

```text
本研究は、髪ストランド内部を Segment Group に分割し、
各グループを runtime decision unit として扱う
Segment Group Runtime Data Type を提案した。

各Segment Groupは、certificate、cache validity、境界誤差、
接触リスク、編集影響、cache ageを持ち、
Static reuse、Static transform、Dynamic solve、Deferred correction、
Boundary repair、Fallbackを実行時に切り替える。

さらに、false-static率、Static/Dynamic境界誤差、cache reuse率、
watchdog repair数をtelemetryとして記録することで、
単なる高速化ではなく、安全な再利用を評価可能にした。

これにより、既存のguide/follower、LOD、ActiveSet、PBD/XPBD、
cache reuseの考え方を前提としつつ、
髪strand内部の再計算粒度を細かく制御し、
固定60FPS・32GB制約下で編集可能なリアルタイム髪表現を目指す。
```

---

# 28. まとめ

本研究で最も重要なのは、以下である。

```text
点-線-点そのものが新しいわけではない。
Static reuseそのものが新しいわけでもない。
GPU化そのものが新しいわけでもない。

新しいのは、
髪strand内部のSegment Groupを、
処理経路を選択するためのデータ型として定義し、
certificateとcache validityによって
Static / Dynamic / Repair / Fallbackを切り替え、
false-static率と境界誤差で安全性を評価する点である。
```

この形であれば、既存研究との衝突を避けつつ、卒業研究として安全に主張できる。
