# 01 企画書兼設計書

# 証明付き静的／動的セグメントグループ型ストランド表現

## Segment-Group Certified Strand Data Type

---

# 0. 対象

* 対象リポジトリ：`MKyosei2/HairSystem`
* 対象ブランチ：`Novelty`
* 対象バージョン：`61.48`
* 対象コミット：`4a2b77f6a86374acde8d649ce150b4394b385242`
* 対象機能番号：`01`
* 対応Noveltyフラグ：`enableMicrotype`
* 実装状況：企画書兼設計書段階。01の主要機能は未実装
* 対象髪本数：100,000本
* 基本点数：1本あたり16点
* 基本セグメント数：1本あたり15セグメント
* 目標フレームレート：固定60 FPS
* 目標フレーム時間：16.67 ms以下
* 想定環境：最新世代に限定しない、少し古い一般的なデスクトップPC

本設計書では、髪ストランドを単純な点列として扱うのではなく、共有端点を持つ複数の「点―線―点」グループによって構成されるデータ型として扱う。

各グループは、静的、変換可能な静的、動的、計算延期、修復の状態を持ち、静的なグループについては物理計算および詳細な状態分類を毎フレーム行わない。

本研究の目的は、単に動かない髪をキャッシュすることではない。

共有端点、親子関係、キャッシュの有効条件、静的・動的境界、誤った静的判定からの復旧処理までを、一つのストランド専用データ型として統一的に管理することを目的とする。

---

# 1. 研究テーマ

本研究では、10万本規模の髪を固定60 FPSに近い状態で処理するため、髪一本を共有端点付きのセグメントグループ列として表現する。

通常の髪データは、次のような点列として表現される。

```text
P0 ― P1 ― P2 ― P3 ― P4 ― P5
```

本研究では、この点列を次のようなグループ列として扱う。

```text
Group 0 = P0 ― P1
Group 1 = P1 ― P2
Group 2 = P2 ― P3
Group 3 = P3 ― P4
Group 4 = P4 ― P5
```

各グループは、一つ前のグループの終点を、自身の開始点として共有する。

```text
Group i の終了点
        =
Group i+1 の開始点
```

ただし、共有点を両方のグループが個別に更新すると、書き込み競合、位置の不一致、長さ制約の破綻が起きる。

そのため、本研究では共有点の最終位置を一つ前のグループが決定し、次のグループはその点を読み取り専用の開始点として利用する。

---

# 2. 本研究の目的

本研究の目的は、次の条件を同時に満たす髪専用データ型を構築することである。

1. 静止している髪部分を毎フレーム再計算しない
2. 髪一本全体ではなく、細かなセグメントグループ単位で状態を変更できる
3. 静的なグループと動的なグループを同じ髪の中に共存させる
4. 静的なグループを開始点、方向、長さから復元する
5. 髪全体が頭部とともに動く場合は、局所形状を再計算せず変換だけを適用する
6. 静的・動的境界のずれを検出する
7. 誤って静的と判定されたグループを検出する
8. 誤判定時に動的状態または修復状態へ復帰させる
9. 静的グループを状態分類処理からも除外する
10. 10万本規模で固定60 FPSに近づける

---

# 3. 本研究の新規性

本研究の新規性は、次の三つを中心とする。

## 3.1 セグメントグループを状態付きデータ型として定義する

本研究では、髪を単なる位置配列として扱わず、各「点―線―点」に以下の情報を持たせる。

* ストランド番号
* グループ番号
* 親グループ番号
* 子グループ番号
* 開始点番号
* 終了点番号
* 開始点の所有者
* 方向
* 長さ
* 現在状態
* 前フレーム状態
* キャッシュ生成時のフレーム番号
* 親グループのバージョン
* コライダー環境のバージョン
* 境界誤差
* 衝突危険度
* 誤静的化フラグ
* 最終修復フレーム
* 更新要求の原因

これにより、単なる計算結果のキャッシュではなく、実行時の判断規則を持つストランド専用データ型として扱う。

## 3.2 共有端点に所有規則を持たせる

隣接するグループ間の共有点は、一つ前のグループが所有する。

```text
Group 0 = P0 ― P1
Group 1 = P1 ― P2
```

この場合、`P1`の位置を決定するのは`Group 0`である。

`Group 1`は`P1`を書き換えず、開始点として参照する。

これにより、静的グループと動的グループが隣接する場合でも、共有点の二重更新を防ぐ。

## 3.3 静的再利用の成立条件と失敗時の復旧をデータ型に含める

静的なグループは、単に前回結果が存在するという理由だけでは再利用しない。

次の条件を満たす場合にのみ、静的キャッシュを有効とする。

* 親グループの接続状態が有効
* 長さ誤差が許容範囲内
* 方向誤差が許容範囲内
* 境界誤差が許容範囲内
* 衝突危険度が許容範囲内
* コライダー環境の世代が一致
* 編集対象ではない
* 修復待ちではない

条件を満たさなくなった場合は、静的キャッシュを無効化し、`Dynamic`または`Repair`へ遷移させる。

---

# 4. 既存方式との差

本研究は、次の既存概念と一部の考え方を共有する。

* sleeping
* inactive particle
* active set
* simulation island
* temporal cache
* animation cache
* LOD
* adaptive simulation
* region of interest
* guide/follower hair
* position-based dynamics
* rod simulation

ただし、本研究の主張は「動かない要素を止めること」ではない。

本研究では、共有端点を持つ細かなセグメントグループを最小単位とし、以下を統合する。

* グループ単位の実行状態
* 共有端点の所有関係
* 静的キャッシュの有効証明
* 隣接グループへの依存関係
* 静的・動的境界の連続性
* 誤静的化の検出
* 修復状態への遷移
* 更新理由の伝播

この統合されたデータ表現を、本研究では「証明付き静的／動的セグメントグループ型ストランド表現」と呼ぶ。

---

# 5. 用語定義

## 5.1 Strand

頭部などに接続された一本の髪を表す。

## 5.2 Point

髪の形状を構成する位置情報である。

## 5.3 Segment

二つの点を結ぶ線分である。

## 5.4 Segment Group

開始点、線分、終了点を一組として扱う最小単位である。

```text
開始点 ― 線分 ― 終了点
```

## 5.5 Shared Endpoint

隣り合う二つのグループが共有する点である。

## 5.6 Parent Group

共有点の位置を決定する一つ前のグループである。

## 5.7 Child Group

親グループの終了点を、自身の開始点として参照するグループである。

## 5.8 Cache Certificate

静的キャッシュが現在の環境でも利用可能であることを示す情報である。

## 5.9 False Static

静的状態として再利用されたグループが、後から判明した安全条件または参照結果に対して、許容誤差を超えていた状態である。

## 5.10 Boundary Error

静的グループと動的グループの共有点または方向が、不連続になったときの誤差である。

---

# 6. データ型の不変条件

本データ型は、常に以下の条件を満たす。

1. 各グループは必ず一つのストランドに属する
2. 各グループは開始点と終了点を一つずつ持つ
3. 子グループの開始点は親グループの終了点と一致する
4. 共有点の最終位置を書き込めるのは親グループだけである
5. 子グループは共有開始点を読み取り専用として扱う
6. 各グループの基準長は、明示的な編集がない限り変化しない
7. 同一ストランド内では、基本的に各グループの基準長を同じとする
8. 異なるストランド間では、グループ長が異なってもよい
9. `StaticCached`は有効なCache Certificateを必要とする
10. `StaticTransformed`は局所形状が変化していないことを必要とする
11. `Dynamic`は物理計算の対象となる
12. `Deferred`は誤差上限を超える前に再評価される
13. `Repair`完了前に`StaticCached`へ直接戻らない
14. 誤差上限を超えた静的グループは必ず`Repair`または`Dynamic`へ移る
15. 編集対象になったグループは静的状態を維持しない
16. 衝突危険領域へ入った静的グループは再評価対象となる
17. 親グループが更新された場合、子グループへ更新要求を伝播する

---

# 7. 状態定義

## 7.1 StaticCached

完全に停止している状態である。

物理計算、詳細な状態分類、位置再生成を原則として行わず、前回の有効な結果を利用する。

頭部、親グループ、周辺コライダー、編集状態に変化がない場合に利用する。

## 7.2 StaticTransformed

局所形状は変化していないが、頭部または親グループの移動に合わせて、剛体的な変換だけを適用する状態である。

物理拘束計算は行わない。

## 7.3 Dynamic

衝突、慣性、外力、編集、親グループの変形などにより、物理計算が必要な状態である。

## 7.4 Deferred

動的処理が必要になる可能性があるが、現フレームでは計算を延期している状態である。

延期中も誤差上限、衝突危険度、経過フレーム数を監視する。

## 7.5 Repair

誤静的化、境界破綻、長さ誤差、方向不連続、めり込みなどを修復する状態である。

---

# 8. 状態遷移

## 8.1 StaticCachedからの遷移

```text
頭部のみが移動
    → StaticTransformed

親グループが移動
    → StaticTransformedまたはDynamic

衝突候補が接近
    → Dynamic

編集対象になる
    → Dynamic

境界誤差が上限を超える
    → Repair

方向または長さの誤差が上限を超える
    → Repair

キャッシュ証明が無効
    → Dynamic
```

## 8.2 StaticTransformedからの遷移

```text
変換のみで安全
    → StaticTransformed

一定時間停止
    → StaticCached

局所変形が必要
    → Dynamic

境界誤差が上限を超える
    → Repair
```

## 8.3 Dynamicからの遷移

```text
一定時間安定
    → StaticCached候補

頭部追従のみで形状維持可能
    → StaticTransformed

計算予算不足
    → Deferred

境界破綻
    → Repair
```

## 8.4 Deferredからの遷移

```text
計算予算を確保
    → Dynamic

誤差上限を超える
    → Repair

安全性を再確認
    → StaticCachedまたはStaticTransformed
```

## 8.5 Repairからの遷移

```text
修復成功
    → Dynamic

修復後、一定時間安定
    → StaticCachedまたはStaticTransformed

修復失敗
    → DynamicまたはFallback
```

---

# 9. 共有端点の所有規則

次のようにグループが接続されている場合を考える。

```text
Group i     = Pi ― Pi+1
Group i + 1 = Pi+1 ― Pi+2
```

共有点`Pi+1`は、`Group i`が所有する。

```text
Owner(Pi+1) = Group i
```

`Group i+1`は`Pi+1`を読み取り専用の開始点として利用する。

GPU上でも、共有点への書き込みは所有者のみが行う。

これにより、同じ点へ複数スレッドが同時に異なる位置を書き込むことを防止する。

---

# 10. 共有点周辺の半球分割

共有点周辺では、前のグループと次のグループのどちらが衝突または修復処理を担当するかを決める必要がある。

共有点を`P`、前方向を`din`、次方向を`dout`、接触方向を`c`とする。

```text
din  = normalize(P - Pprev)
dout = normalize(Pnext - P)
```

接触方向と各方向の内積を利用して、処理責任を分ける。

```text
scoreParent = dot(c, -din)
scoreChild  = dot(c,  dout)
```

```text
scoreParent >= scoreChild
    → 親グループ側が処理

scoreParent < scoreChild
    → 子グループ側が処理
```

この半球分割は、共有点の衝突形状そのものを二つの物理オブジェクトに分けることを必須としない。

主な目的は、共有点周辺の衝突処理、境界補正、責任範囲を一意に決定することである。

---

# 11. 静的グループの復元

静的グループは、開始点、方向、長さから終了点を復元する。

```text
Pend = Pstart + direction × length
```

開始点は親グループの終了点または髪の根元位置を参照する。

```text
P0 = root position
P1 = P0 + d0 × L0
P2 = P1 + d1 × L1
P3 = P2 + d2 × L2
```

これは単純なXYZ軸の正負だけで表すのではなく、正規化方向ベクトルと長さを利用する。

保存形式は、以下のいずれかとする。

* 32ビット浮動小数点方向
* 16ビット量子化方向
* 球面座標による量子化方向
* 親方向との差分角度

初期実装では32ビット浮動小数点方向を使用し、性能評価後に量子化形式を検討する。

---

# 12. 髪一本全体が移動する場合

髪一本の局所形状が変わらず、頭部または親オブジェクトとともに移動する場合は、各グループを物理計算しない。

キャッシュ生成時の頭部変換を`Hcached`、現在の頭部変換を`Hcurrent`とする。

```text
T = Hcurrent × inverse(Hcached)
```

各点または根元に対して、次を適用する。

```text
Pcurrent = T × Pcached
```

この状態を`StaticTransformed`とする。

ただし、変換後に衝突候補領域へ入った場合は、対象グループを`Dynamic`へ昇格させる。

---

# 13. キャッシュ証明

静的キャッシュが現在も安全に利用できるかを確認するため、各グループはCache Certificateを持つ。

```cpp
struct SegmentGroupCacheCertificate
{
    uint32_t sourceFrame;
    uint32_t parentVersion;
    uint32_t colliderVersion;
    uint32_t editVersion;

    float maximumPositionErrorMm;
    float maximumDirectionErrorDeg;
    float maximumLengthErrorRatio;
    float maximumBoundaryErrorMm;

    uint8_t parentValid;
    uint8_t collisionValid;
    uint8_t boundaryValid;
    uint8_t editValid;
    uint8_t cacheValid;
};
```

Cache Certificateは、以下の条件を満たした場合に有効とする。

```text
parentValid
AND collisionValid
AND boundaryValid
AND editValid
AND maximumPositionErrorMm <= 許容値
AND maximumDirectionErrorDeg <= 許容値
AND maximumLengthErrorRatio <= 許容値
AND maximumBoundaryErrorMm <= 許容値
```

Cache Certificateが無効になったグループは、`StaticCached`を継続できない。

---

# 14. Dirty Reason

静的なグループを毎フレームすべて再分類すると、判定処理そのものが大きな負荷になる。

そのため、各グループは再評価が必要になった理由を持つ。

```cpp
enum class SegmentGroupDirtyReason : uint16_t
{
    None            = 0,
    ParentMoved     = 1 << 0,
    HeadMoved       = 1 << 1,
    ColliderEntered = 1 << 2,
    ColliderChanged = 1 << 3,
    NeighborChanged = 1 << 4,
    Edited          = 1 << 5,
    CacheExpired    = 1 << 6,
    BoundaryFailed  = 1 << 7,
    Watchdog        = 1 << 8
};
```

`DirtyReason::None`かつCache Certificateが有効な`StaticCached`グループは、詳細な再分類を行わない。

これにより、静的グループは物理計算だけでなく、毎フレームの分類処理からも除外される。

---

# 15. Dirty Propagation

親グループが更新された場合、子グループへ更新理由を伝播する。

```text
親が位置変更
    → 子へParentMoved

親がRepair
    → 子へNeighborChanged

親の終了点が許容値以上移動
    → 子へBoundaryFailed

髪の根元が移動
    → 先頭グループへHeadMoved
```

ただし、一つの変更によって髪全体を必ず動的化するのではない。

子グループが`StaticTransformed`で対応可能な場合は、物理計算へ移さず変換のみを行う。

---

# 16. 階層管理

10万本、約150万グループをすべて毎フレーム確認することは避ける。

次の三段階で管理する。

```text
Strand
    ↓
Segment Group Block
    ↓
Segment Group
```

## 16.1 Strand単位

髪一本全体が頭部変換だけで動いているかを判定する。

## 16.2 Block単位

複数の連続グループを一つのBlockにまとめる。

基本値は8グループとする。

Blockが完全に静的で、Dirty Reasonが存在しない場合、内部グループを個別に評価しない。

## 16.3 Group単位

危険と判定されたBlock内だけ、個別のグループ状態を評価する。

---

# 17. 衝突候補の絞り込み

全グループに対して詳細衝突判定を行わない。

次の順序で候補を絞り込む。

```text
1. 頭部またはコライダーの大域AABB判定
2. StrandまたはBlockのAABB判定
3. 危険BlockのGroup判定
4. 危険Groupだけ詳細衝突
5. 必要なGroupだけDynamicへ昇格
```

髪同士の衝突についても、全組み合わせを確認せず、空間グリッド、ハッシュ、ボクセルなどを利用する。

---

# 18. False Static

## 18.1 定義

false staticとは、静的状態として再利用されたグループが、後から得られた安全条件または参照結果に対して許容誤差を超えていた状態である。

## 18.2 判定条件

次のいずれかを満たした場合、false staticとする。

```text
boundaryErrorMm > 6.0 mm

directionDeltaDeg > 8.0 degrees

lengthErrorRatio > 0.05

penetrationDepthMm > 1.0 mm

parentVersion mismatch

colliderVersion mismatch

共有点の位置差 > 6.0 mm
```

## 18.3 対応

false staticを検出した場合は、次を行う。

```text
1. falseStatic = true
2. state = Repair
3. cacheValid = false
4. 子グループへNeighborChangedを伝播
5. 修復失敗時はDynamicへ移行
```

false staticを完全にゼロにすることだけを目標にしない。

重要なのは、誤判定を検出可能であり、破綻が描画結果へ長時間残らないことである。

---

# 19. Watchdog

静的グループを永久に検査しない場合、外部変化を見逃す可能性がある。

そのため、静的グループの一部を周期的に再確認する。

```text
(frameIndex + groupId) mod watchdogInterval == 0
```

基本値は64フレームとする。

```text
watchdogInterval = 64
```

これにより、一度に全グループを検査せず、複数フレームへ分散して安全性を確認する。

---

# 20. 境界補正

静的グループと動的グループが隣接する場合、共有点と方向の連続性を確認する。

## 20.1 位置誤差

```text
positionError =
distance(parent.endPoint, child.startPoint)
```

## 20.2 方向誤差

```text
directionError =
angle(parent.direction, child.direction)
```

## 20.3 軽微な誤差

誤差がSoft Limit以下の場合、軽量補間で修正する。

```text
corrected =
lerp(current, target, boundaryCorrectionBlend)
```

## 20.4 大きな誤差

誤差がHard Limitを超えた場合、静的側を`Repair`へ移す。

---

# 21. 初期しきい値

```cpp
struct SegmentGroupMicrotypeSettings
{
    float staticCachedRiskThreshold = 0.20f;
    float staticTransformedRiskThreshold = 0.35f;
    float deferredRiskThreshold = 0.65f;

    float maxStaticRootMotionMm = 1.0f;
    float maxStaticEndpointMotionMm = 1.5f;
    float maxStaticDirectionDeltaDeg = 2.0f;
    float maxStaticCurvatureDeltaDeg = 3.0f;

    float boundarySoftLimitMm = 2.0f;
    float boundaryHardLimitMm = 6.0f;
    float boundaryCorrectionBlend = 0.35f;

    float colliderNearDistanceMm = 8.0f;
    float maxPenetrationMm = 1.0f;
    float maxLengthErrorRatio = 0.05f;
    float falseStaticDirectionDeltaDeg = 8.0f;

    uint32_t repairCooldownFrames = 8;
    uint32_t groupsPerStrandBlock = 8;
    uint32_t watchdogIntervalFrames = 64;

    float targetMaxDynamicRate = 0.15f;
    float targetMinStaticReuseRate = 0.75f;
    float targetMaxFalseStaticRate = 0.01f;
};
```

これらは最終値ではなく、実験によって調整する。

---

# 22. 追加するデータ構造

```cpp
enum class SegmentGroupState : uint8_t
{
    StaticCached = 0,
    StaticTransformed = 1,
    Dynamic = 2,
    Deferred = 3,
    Repair = 4
};

struct SegmentGroupMicrotype
{
    uint32_t strandId;
    uint32_t groupId;

    uint32_t parentGroupId;
    uint32_t childGroupId;

    uint32_t startPointIndex;
    uint32_t endPointIndex;

    uint32_t ownerGroupId;

    SegmentGroupState state;
    SegmentGroupState previousState;

    Vec3 direction;
    Vec3 previousDirection;

    float restLength;
    float currentLength;

    float motionScore;
    float collisionRisk;
    float boundaryErrorMm;
    float errorBoundMm;

    uint32_t lastSolvedFrame;
    uint32_t lastRepairFrame;
    uint32_t stateStableFrameCount;

    SegmentGroupDirtyReason dirtyReason;
    SegmentGroupCacheCertificate certificate;

    uint8_t edited;
    uint8_t colliderNear;
    uint8_t falseStatic;
    uint8_t reserved;
};
```

---

# 23. 追加するソースファイル

```text
include/hairgpu/SegmentGroupMicrotype.h
src/hairgpu_core/SegmentGroupMicrotype.cpp

apps/dx11_viewer/shaders/HairMicrotypeClassify.hlsl
apps/dx11_viewer/shaders/HairMicrotypeReconstruct.hlsl
apps/dx11_viewer/shaders/HairMicrotypeBoundaryRepair.hlsl
apps/dx11_viewer/shaders/HairMicrotypeDirtyPropagation.hlsl
apps/dx11_viewer/shaders/HairMicrotypeWatchdog.hlsl
```

---

# 24. 修正する既存ファイル

```text
HairSystem.vcxproj

include/hairgpu/HairNoveltyFlags.h
include/hairgpu/HairNoveltyTelemetry.h

apps/dx11_viewer/src/GpuHairViewer.h

apps/dx11_viewer/src/GpuHairViewer_parts/
    02_resource_creation_and_shader_loading.inl
    03_simulation_reset_and_frame_step.inl
    10_phase1_telemetry_csv_jsonl_summary.inl
    11_experiment_env_and_performance_sample.inl

tools/final_stage/schema.sql
tools/final_stage/hairgpu_final_db.py

docs/sample_assets/metric_definitions.json
```

---

# 25. GPU処理順序

毎フレーム、次の順番で処理する。

```text
1. 頭部変換、コライダー、編集状態の世代更新

2. StrandまたはBlock単位のDirty判定

3. Dirty BlockだけGroup判定

4. StaticCachedグループの証明確認

5. StaticTransformedグループへ変換適用

6. Dynamicグループ一覧を生成

7. Dynamicグループだけ物理計算

8. Static/Dynamic境界を検査

9. Repairグループを補正

10. Watchdog対象を分散検査

11. 描画用位置を生成

12. Telemetryを集計
```

CPUへ毎フレーム全グループ状態を読み戻さない。

CPU readbackは、数フレーム遅延した集計値だけとする。

---

# 26. メモリ表現

全点位置を各状態で重複保持しない。

静的グループについては、次を基本情報とする。

```text
開始点参照
方向
長さ
状態
証明情報
```

Dynamicグループのみ、現在位置、前位置、速度、拘束計算用情報を保持する。

方向情報は、初期実装では`float3`を使う。

その後、次の形式と比較する。

```text
float3 direction
half3 direction
octahedral encoded direction
spherical quantized direction
```

方向キャッシュによるメモリ削減率を評価する。

---

# 27. Telemetry

最低限、次の値を記録する。

```text
totalSegmentGroupCount

staticCachedGroupCount
staticTransformedGroupCount
dynamicGroupCount
deferredCorrectionGroupCount
repairRequiredGroupCount
fallbackGroupCount

dirtyBlockCount
dirtyGroupCount

falseStaticCount
falseStaticRate

watchdogCheckCount
watchdogRepairCount

cacheHitCount
cacheMissCount
cacheInvalidationCount

staticDynamicBoundaryMeanErrorMm
staticDynamicBoundaryMaxErrorMm

directionCacheMemoryMb
fullPositionMemoryMb
directionCacheMemorySavingRate

classificationGpuMilliseconds
reconstructionGpuMilliseconds
dynamicPhysicsGpuMilliseconds
boundaryRepairGpuMilliseconds
renderGpuMilliseconds

frameMilliseconds
fps
```

---

# 28. 評価指標

## 28.1 性能

```text
average FPS
p50 frame time
p95 frame time
p99 frame time
1% low FPS
GPU frame time
CPU frame time
```

## 28.2 状態割合

```text
StaticCached率
StaticTransformed率
Dynamic率
Deferred率
Repair率
```

## 28.3 安全性

```text
falseStaticRate
Static/Dynamic境界平均誤差
Static/Dynamic境界最大誤差
最大めり込み量
修復成功率
修復失敗率
```

## 28.4 メモリ

```text
全点位置保持時のメモリ
方向キャッシュ時のメモリ
メモリ削減率
```

## 28.5 判定コスト

```text
全グループ判定数
Dirty Group判定数
Dirty Block判定数
Watchdog判定数
```

---

# 29. 比較対象

次の方式を比較する。

## Baseline A

全髪、全グループを毎フレーム物理計算する。

## Baseline B

髪一本単位で静的または動的を決定する。

## Baseline C

セグメント単位で静的化するが、共有端点の所有規則とRepairを持たない。

## Proposed 01

共有端点所有、半球責任分割、5状態、Cache Certificate、Dirty Propagation、Boundary Repair、false static検出を持つ方式。

---

# 30. アブレーション評価

提案手法から各要素を一つずつ外して比較する。

```text
01-Full

01-NoCertificate
01-NoDirtyPropagation
01-NoBoundaryRepair
01-NoWatchdog
01-NoHalfSphereOwnership
01-NoBlockHierarchy
01-TwoStateOnly
```

これにより、単なるキャッシュとの差を明確にする。

---

# 31. テストシナリオ

## 31.1 完全静止

頭部、髪、コライダーが動かない。

目的：

* StaticCached率
* キャッシュヒット率
* 判定コスト
* 60 FPS維持率

## 31.2 頭部の平行移動

髪の局所形状を変えず、頭部だけを移動する。

目的：

* StaticTransformedの有効性
* 一括変換コスト
* 不要なDynamic昇格の有無

## 31.3 頭部の回転

頭部を連続回転させる。

目的：

* 方向キャッシュの追従
* 親子境界の連続性
* Dynamic率の増加

## 31.4 一部接触

髪の一部分だけをコライダーへ接触させる。

目的：

* 局所的なDynamic昇格
* 非接触部分の静的維持
* Dirty Propagation範囲

## 31.5 高速運動

頭部を急激に動かす。

目的：

* false static検出
* Repair移行
* 最悪フレーム時間

## 31.6 部分編集

一部ストランドまたは一部グループだけを編集する。

目的：

* 編集対象だけのDynamic化
* 周辺への伝播範囲
* 再静的化までの時間

## 31.7 多数衝突

複数のコライダーを髪へ接近させる。

目的：

* 衝突候補絞り込み
* Dynamic率増加時の性能
* Fallback発生率

## 31.8 Watchdog検出

静的グループへ、通常のDirty通知では検出できない変化を与える。

目的：

* Watchdog検出率
* 検出までの遅延
* 修復成功率

---

# 32. 10万本条件

基本条件は次とする。

```text
髪本数：100,000
1本あたりの点数：16
1本あたりのグループ数：15
総グループ数：約1,500,000
```

目標状態は次とする。

```text
StaticCached + StaticTransformed >= 75%

Dynamic <= 15%

Deferred <= 7%

Repair <= 3%

falseStaticRate <= 1%
```

ただし、激しい運動、広範囲な衝突、強い編集が行われる場面では、一時的にDynamic率が15%を超えることを許容する。

その場合でも、長時間にわたり16.67 msを超え続けないことを評価する。

---

# 33. 60 FPS成功条件

```text
平均FPS >= 60

p95 frame time <= 16.67 ms

p99 frame time <= 20.0 ms

1% low FPS >= 50

StaticCached + StaticTransformed >= 75%

Dynamic <= 15%

Repair <= 3%

falseStaticRate <= 1%

境界平均誤差 <= 2.0 mm

境界最大誤差 <= 6.0 mm

最大めり込み <= 1.0 mm

direction cache memory saving rate >= 30%
```

平均FPSだけで成功と判定しない。

---

# 34. 少し古いPCでの目標

少し古いPCでは、最新GPUと同じ処理量を前提としない。

次を重視する。

* 全グループ走査を避ける
* CPUとGPU間の同期を避ける
* Dynamic一覧をGPU上で作る
* 静的ブロックを詳細判定しない
* 詳細衝突候補を事前に減らす
* CPU readbackを集計値だけにする
* Repairを複数フレームへ分散する
* Watchdogを分散実行する

初期実装時の安定60 FPS達成確率は、25～40%程度と予測する。

GPU完結、階層判定、衝突候補絞り込みを実装した場合は、55～70%程度を予測する。

さらに実測予算に基づく処理数制御を導入した場合は、70～85%程度を目標とする。

これらは実測前の予測値であり、保証値ではない。

---

# 35. 失敗条件

次のいずれかに該当する場合、01単体では目標未達とする。

```text
p95 frame time > 16.67 ms

Dynamic率が通常場面で継続的に15%を超える

全グループを毎フレーム分類している

CPU readback待ちがフレーム処理を停止させる

falseStaticRate > 1%

境界最大誤差 > 6.0 mm

Repairが連続的に増え続ける

衝突候補の絞り込みが機能しない

StaticCachedが頻繁に無効化される
```

---

# 36. 新規性を弱くする書き方

論文では、次のようには書かない。

```text
動かない髪をキャッシュして高速化する。
```

```text
動く髪だけを計算する。
```

```text
髪を細かな部分に分割して処理する。
```

これらだけでは、sleeping、active set、LOD、simulation island、temporal cacheとの差が弱い。

---

# 37. 論文での新規性の書き方

本研究では、髪ストランドを単なる点列ではなく、共有端点を持つセグメントグループ列として再定義する。

各セグメントグループは、実行状態、方向・長さキャッシュ、共有端点の所有関係、親子依存、キャッシュ有効証明、境界誤差および修復履歴を保持する。

静的グループは、キャッシュ有効条件が成立する間、物理計算と毎フレームの詳細分類処理から除外される。

頭部変換、親グループの更新、コライダーの接近、編集、境界誤差などが発生した場合のみ、Dirty Reasonを用いて対象グループへ更新要求を伝播する。

また、静的判定の誤りをfalse staticとして検出し、異なる状態のグループ間に生じる境界不連続をRepair状態によって修正する。

これにより、従来のsleepingやtemporal cacheのように計算を停止するだけではなく、静的再利用の成立条件、共有端点の責任範囲、失敗検出、復旧処理までを、一つのストランド専用データ型として統一的に管理する。

---

# 38. 本研究の貢献

本研究の貢献は次のとおりである。

1. 髪ストランドを共有端点付きのセグメントグループ列として再定義した
2. グループ単位で5種類の実行状態を持たせた
3. 共有端点の書き込み所有規則を定義した
4. 共有点周辺の処理責任を半球的に分割した
5. 静的キャッシュの有効条件をCache Certificateとして定義した
6. 静的グループを詳細分類処理から除外するDirty方式を導入した
7. 親子関係に基づくDirty Propagationを定義した
8. 静的・動的境界の誤差を検出・補正する方式を定義した
9. 誤った静的化をfalse staticとして計測可能にした
10. 修復状態を用いて静的化の失敗から復旧できるようにした
11. 10万本規模を対象とした性能と安全性の評価条件を定義した

---

# 39. 新規性の最終判定条件

次の要素を実装し、比較実験によって効果を示せた場合、本研究の新規性は安全側と判断する。

```text
共有端点所有規則

半球的な処理責任分割

5状態の明確な遷移

Cache Certificate

Dirty Reason

Dirty Propagation

Block階層管理

Static/Dynamic境界補正

false static検出

Repairによる復旧

既存方式とのアブレーション比較
```

一方、単に各線分へ状態enumを追加し、静的部分を前フレームからコピーするだけの場合は、新規性が弱くなる。

---

# 40. 最終結論

本研究では、髪一本を共有端点を持つ「点―線―点」のセグメントグループ列として扱う。

各グループは、`StaticCached`、`StaticTransformed`、`Dynamic`、`Deferred`、`Repair`の状態を持つ。

静的グループは、開始点、方向、長さおよび親子接続情報から復元し、キャッシュ証明が有効な間は物理計算と詳細分類処理を行わない。

隣接するグループの共有点は親グループが所有し、子グループはその位置を読み取り専用として利用する。

共有点周辺の衝突および修復責任は、前後方向を基準とした半球的な規則によって分割する。

頭部変換、衝突候補、編集、親グループの更新、境界誤差などが発生した場合のみ、Dirty Reasonを通じて対象グループを再評価する。

誤って静的化されたグループはfalse staticとして検出し、`Repair`または`Dynamic`へ遷移させる。

本研究の新規性は、静的な髪を単にキャッシュすることではない。

共有端点、状態遷移、キャッシュ有効証明、依存関係、境界保証、失敗検出、復旧処理を統合した、新しいストランド専用実行時データ型を構築することにある。

最終的には、10万本、約150万セグメントグループの髪に対して、静的再利用率75%以上、Dynamic率15%以下、false static率1%以下、p95フレーム時間16.67 ms以下を目標とする。
