# 新規性01 完全版企画書兼設計書

# Certified Morphing Segment-Group Data Type

## 状態依存表現・誤差上限付き遷移・決定的共有点解決を統合したGPU線状物理データ型

---

## 0. 文書情報

| 項目             | 内容                                         |
| -------------- | ------------------------------------------ |
| 文書種別           | 卒業研究・新規性企画書兼実装設計書                          |
| 対象研究           | GPU常駐型HairSystem                           |
| 新規性番号          | 新規性01                                      |
| 基準リポジトリ        | `MKyosei2/HairSystem`                      |
| 基準ブランチ         | `main`                                     |
| 基準コミット         | `4a462a4ca200b85cf7f5792f669ce3df81c59cf2` |
| コミット表示名        | `62`                                       |
| 提案名称           | Certified Morphing Segment-Group Data Type |
| 略称             | CMSG Data Type                             |
| 日本語名称          | 許容性証明書付き可変セグメントグループ型                       |
| 対象API          | DirectX 11                                 |
| 主言語            | C++17、HLSL、Python                          |
| 表示ストランド数       | 100,000本                                   |
| Guideストランド数    | 4,000本                                     |
| Followerストランド数 | 96,000本                                    |
| 1ストランドの点数      | 128点                                       |
| 1ストランドのGroup数  | 127 Group                                  |
| Guide Group総数  | 508,000 Group                              |
| 表示粒子総数         | 12,800,000点                                |
| 物理反復回数         | 6回                                         |
| 目標解像度          | 1920×1080                                  |
| 最終性能目標         | 安定60FPS                                    |
| 低性能側想定         | GTX 1660級GPUを搭載した少し古いPC                    |
| 新規性評価目標        | 「危ない」から「安全」へ移行可能な技術構成                      |

---

# 1. 概要

本研究では、GPU上で動作する大量の髪シミュレーションに対して、髪の1本全体ではなく、ストランド内部の局所的な点―線―点のまとまりを独立した実行単位として扱う。

この局所単位を`Segment Group`と呼ぶ。

本研究の中心は、Segment GroupへStatic、Dynamicなどの状態フラグを追加することではない。

各Segment Groupについて、現在の状態に応じて以下を同時に変化させる新しい線状物理データ型を提案する。

1. 保持する物理表現
2. 使用するGPUメモリ領域
3. 実行可能な演算
4. 更新頻度
5. 安全条件
6. 隣接Groupとの共有点解決方法
7. 異常発生時の復旧方法

提案型は、次の3要素を不可分な中心技術として持つ。

1. **状態依存Payload Morphing**
   状態遷移時にフラグだけでなく、データ表現とPayloadを変更する。

2. **誤差上限付きAdmissibility Certificate**
   軽量な表現を使用してよいかを、速度閾値だけではなく、誤差上限、衝突余裕、親Group誤差、Root変換、編集履歴などから判定する。

3. **Deterministic Hemisphere Shared-Point Resolve**
   隣接Groupが共有する点に対して、半球責任、優先順位、誤差、衝突、安全状態を用いて最終値を決定する。

本研究は、単に物理計算を停止する方式ではない。

> 状態に応じて物理要素の内部表現そのものを変更し、その表現を使用できる条件をデータ型自身が管理するGPU線状物理データ型を設計する。

---

# 2. 研究背景

## 2.1 大量髪シミュレーションの問題

リアルタイムHairSystemでは、次の処理が大きな負荷となる。

* 全点の位置積分
* 距離拘束
* 曲げ拘束
* Root拘束
* 頭部および身体との衝突
* 自己衝突
* 速度更新
* GuideからFollowerへの再構築
* レンダリング用頂点生成
* GPUバッファ間のCopy
* CPUへのReadback
* 毎フレームの全要素分類

100,000本、128点では、表示上の粒子数は次のようになる。

```text
100,000 × 128 = 12,800,000点
```

すべての点を毎フレーム完全に物理更新すると、少し古いGPU上で安定60FPSを維持することは難しい。

---

## 2.2 Strand単位のStatic化の限界

既存の一般的なStatic化やSleepingでは、オブジェクト全体またはストランド全体を単位として、更新を停止する。

しかし、髪の1本では次の状態が同時に存在する。

```text
根元       中間部         毛先
Static ─ Static ─ Dynamic ─ Dynamic
```

例えば、根元付近は頭皮へ固定されて安定している一方、毛先だけが風や衝突で動くことがある。

Strand全体をDynamicにすると、安定部分まで計算する。

Strand全体をStaticにすると、毛先の運動や衝突を失う。

この問題を解決するため、本研究ではStrand内部をSegment Groupへ分割する。

---

## 2.3 単純なSegment単位Sleepingの限界

Segment単位でStatic／Dynamicフラグを持たせるだけでは、次の問題が残る。

* 何をもってStaticと判定するかが不明確
* Static化後の保持データがDynamic時と同じ
* Root回転時の追従方法が定義されていない
* 隣接Groupが同じ点へ書き込む
* Static／Dynamic境界で折れや位置跳躍が起きる
* Cacheが古いかどうか判定できない
* 衝突体が近づいた場合の予測復帰がない
* Repair直後に再びStatic化される
* 状態分類処理自体が高負荷になる
* 既存のsleep、cache、active setとの差が弱い

したがって、単純な状態配列ではなく、型の表現と演算規則を含む設計が必要である。

---

# 3. 研究目的

本研究の目的は、GPU上の線状物理要素に対して、状態に応じて内部表現と演算を変更できるデータ型を構築し、大量髪シミュレーションの計算量、メモリ帯域、局所更新コストを削減することである。

具体的には、以下を目標とする。

1. Strand内部でStaticとDynamicを混在可能にする
2. Static Groupでは完全物理計算を停止する
3. Static Groupでは最後に検証された結果を再利用する
4. Rootの平行移動と回転へ正しく追従する
5. 衝突危険が発生する前にDynamicへ復帰する
6. Static／Dynamic境界の不連続を抑える
7. 共有点への複数書き込みを防止する
8. 異常Groupのみ局所修復する
9. 毎フレームの全Group詳細分類を回避する
10. Follower側の不要な全再構築も削減する
11. Full Dynamicと同等の見た目を可能な限り維持する
12. 100,000本、128点、1080p環境で安定60FPSを目指す

---

# 4. 研究上の中心命題

本研究の中心命題は次のとおりである。

> 線状物理要素を、状態に応じて内部表現、Payload、許可演算、更新頻度を変更する可変データ型として定義し、誤差上限付き許容性証明書と決定的共有点解決を組み合わせることで、同一ストランド内部に異なる計算状態を安全に共存させられる。

本研究で新規性として主張しない要素は次のとおりである。

* Segmentによる線状形状の表現
* Guide／Follower方式
* PBDまたはXPBD
* GPU Compute Shader
* Sleeping
* Cache reuse
* Active set
* GPU Compaction
* Indirect Dispatch
* LOD
* Repair処理
* Error threshold
* Dirty flag

これらは既存技術または実装基盤として扱う。

本研究が新しく構成する対象は、これらの単純な集合ではなく、次の3つを持つデータ型とその実行意味論である。

```text
State-dependent Physical Representation
+
Error-bounded Admissibility Transition
+
Deterministic Shared-Node Resolve
```

---

# 5. 新規性を構成する必須技術

## 5.1 状態依存物理表現

同一のSegment Groupであっても、現在の状態によって保持するデータを変える。

```text
Dynamic
    完全物理更新用Payload

Static Cached
    最終有効局所形状の圧縮Payload

Static Transformed
    Root差分変換と局所形状Payload

Deferred
    予測値と累積誤差上限Payload

Repair
    最終有効値と修復情報Payload
```

状態変更時には、単純なState Flagの変更ではなく、PayloadのEncode、Decode、Migrationを実行する。

---

## 5.2 誤差上限付き遷移

Staticへの遷移は、速度が小さいという条件だけでは許可しない。

次を評価する。

* 現在位置誤差
* 方向誤差
* 長さ誤差
* 親Groupから伝播する境界誤差
* Root移動誤差
* Root回転誤差
* Cache量子化誤差
* 予測残差
* 衝突体までの距離
* 衝突体の相対速度
* 編集Version
* Topology Version
* Collider Version
* Certificate期限

軽量状態を使用した場合の最大誤差が許容範囲内である場合にのみ、StaticまたはDeferredを許可する。

---

## 5.3 決定的共有点解決

隣接するSegment Groupは中間点を共有する。

```text
Parent Group : P0 ─ P1
Child Group  :      P1 ─ P2
```

P1へ両Groupが直接書き込むと、競合、非決定性、境界不連続が発生する。

そのため各Groupは、最終点ではなく候補結果を出力する。

最終的な共有点は、次を用いた決定的Reduceで確定する。

* Fatal
* Collision
* Edit
* Repair
* Dynamic
* Static Transform
* Static Cache
* Certificate有効性
* 境界誤差
* 衝突残差
* Group ID

---

# 6. 適用範囲

## 6.1 本研究で扱う対象

* 髪
* ロープ
* チェーン
* 尻尾
* 触手
* 草
* 線状パーティクル
* 分岐を持たない細長い弾性体
* Guide／Follower型の線状表現

## 6.2 卒業研究での評価対象

実際の評価はHairSystemで行う。

一般化可能性は設計上示すが、Hair以外の完全実装は必須としない。

## 6.3 対象外

以下は新規性01の対象外とする。

* 新しい髪物理方程式の提案
* 新しい衝突アルゴリズムそのもの
* 新しいレンダリングモデル
* 新しいニューラルネットワーク
* 新しいGuide生成法
* ヘアスタイリングUI全体
* Alembic入出力
* 1024点対応
* マルチGPU
* DirectX 12への移行

---

# 7. 用語定義

## 7.1 Point

髪ストランドを構成する位置要素。

```text
P0, P1, P2, ..., P127
```

## 7.2 Segment

隣接する2点を接続する線分。

```text
Segment i = Pi ─ Pi+1
```

## 7.3 Segment Group

本研究における局所実行単位。

基本形は点―線―点である。

```text
Group i = Pi ─ Pi+1
```

実装上は、開始点、終了点、Rest Length、親子関係、状態、Payload参照を持つ。

## 7.4 Shared Point

Parent Groupの終点であり、Child Groupの開始点でもある点。

## 7.5 Payload

状態固有の物理データ。

## 7.6 Certificate

現在の軽量表現を継続して使用してよいかを表す実行時有効性記述子。

数学的な完全証明ではなく、計測可能な誤差上限とVersion整合性に基づく許容性記録である。

## 7.7 Morphing Transition

状態遷移に伴って、内部Payloadを別形式へ変換する処理。

## 7.8 Final Owner

共有点へ最終値を書き込む唯一のGroupまたはResolve処理。

---

# 8. トポロジー設計

## 8.1 Guideストランド

Guide数は4,000本とする。

各Guideを128点へ等間隔Resampleする。

```text
Guide Points = 128
Guide Groups = 127
```

Group総数は次のとおりである。

```text
4,000 × 127 = 508,000 Group
```

## 8.2 Followerストランド

Follower数は96,000本とする。

Followerには完全なCMSG型を持たせない。

FollowerはGuideから再構築するための軽量データだけを保持する。

## 8.3 等間隔Resample

各Guideについて累積弧長を計算する。

元の総弧長を`Ltotal`とすると、目標Group長は次である。

```text
Lgroup = Ltotal / 127
```

各点は、元ストランド上の累積弧長位置から補間して生成する。

同一ストランド内部では、原則としてGroupのRest Lengthを等しくする。

## 8.4 Topology Version

次が発生した場合にTopology Versionを更新する。

* 点数変更
* Group再生成
* Strand切断
* Root付け替え
* Resample
* Guide対応変更
* Asset再読み込み

Topology Versionが異なるCertificateおよびCacheは無効とする。

---

# 9. 正式データ型

## 9.1 論理定義

```cpp
enum class SegmentGroupState : uint32_t
{
    StaticCached = 0,
    StaticTransformed = 1,
    Dynamic = 2,
    Deferred = 3,
    Repair = 4
};
```

```cpp
enum class SegmentGroupPayloadType : uint32_t
{
    StaticCachedPayload = 0,
    StaticTransformedPayload = 1,
    DynamicPayload = 2,
    DeferredPayload = 3,
    RepairPayload = 4
};
```

## 9.2 SegmentGroupHeader

全Groupが共通して保持する。

```cpp
struct SegmentGroupHeader
{
    uint32_t strandId;
    uint32_t localGroupId;

    uint32_t startPointIndex;
    uint32_t endPointIndex;

    uint32_t parentGroupId;
    uint32_t childGroupId;

    uint32_t blockId;
    uint32_t topologyVersion;

    uint32_t currentState;
    uint32_t previousState;

    uint32_t payloadType;
    uint32_t payloadIndex;

    uint32_t certificateIndex;
    uint32_t dirtyReasonMask;

    uint32_t requestPriority;
    uint32_t runtimeFlags;
};
```

## 9.3 Headerの制約

* `currentState`と`payloadType`は一致しなければならない
* `payloadIndex`は対応Poolの有効範囲内でなければならない
* ParentとChildは同一Strand内でなければならない
* Root Groupの`parentGroupId`は無効値とする
* Tip Groupの`childGroupId`は無効値とする
* Topology Version不一致の場合はRepairへ移行する
* 不明なState値はDynamicまたはRepairへフォールバックする

---

# 10. 状態別Payload

## 10.1 Dynamic Payload

完全物理計算を行うGroupが使用する。

```cpp
struct DynamicPayload
{
    float3 currentDirection;
    float restLength;

    float3 previousDirection;
    float currentLength;

    float3 currentVelocity;
    float distanceLambda;

    float3 previousVelocity;
    float bendLambda;

    float collisionRisk;
    float penetrationRisk;
    float motionRisk;
    uint32_t lastSolvedFrame;

    uint32_t solverFlags;
    uint32_t iterationMask;
    uint32_t reserved0;
    uint32_t reserved1;
};
```

Dynamic状態では、以下の演算を許可する。

* Integration
* Distance Constraint
* Bend Constraint
* Root Constraint
* Collision
* Self Collision
* Wind
* Damping
* Velocity Update
* Dynamicから他状態へのEncode

---

## 10.2 Static Cached Payload

Root変換がなく、局所形状をそのまま再利用可能なGroupが使用する。

```cpp
struct StaticCachedPayload
{
    uint32_t packedLocalDirection;
    float restLength;

    uint32_t packedCachedVelocity;
    float cachedEndOffsetLength;

    uint32_t sourceFrame;
    uint32_t rootFrameVersion;
    uint32_t colliderVersion;
    uint32_t editVersion;

    float maximumPositionError;
    float maximumDirectionError;
    float collisionClearance;
    uint32_t cacheHash;
};
```

Static Cached状態では、通常の物理Solverを実行しない。

終点位置は次で復元する。

```text
P_end = P_start + Decode(LocalDirection) × RestLength
```

次の場合はStatic Cachedを継続しない。

* Rootが回転した
* Rootが許容量を超えて移動した
* Collider Versionが変化した
* Edit Versionが変化した
* Certificate期限切れ
* 隣接Groupが強制Dynamicになった
* 衝突体が安全距離内へ接近した
* 境界誤差が増加した

---

## 10.3 Static Transformed Payload

局所形状を保ちながら、Rootまたは頭部の剛体変換へ追従するGroupが使用する。

```cpp
struct StaticTransformedPayload
{
    uint32_t packedLocalDirection;
    float restLength;

    float4 sourceRootRotation;
    float3 sourceRootPosition;
    uint32_t sourceRootVersion;

    float4 targetRootRotation;
    float3 targetRootPosition;
    uint32_t targetRootVersion;

    float sweptBoundRadius;
    float collisionClearance;
    float maximumTransformError;
    uint32_t transformFlags;
};
```

位置変換は次で求める。

```text
P' = R_delta × (P - Root_old) + Root_new
```

ここで、

```text
R_delta = R_new × inverse(R_old)
```

である。

Groupを根元から逐次復元する場合は次を使用する。

```text
P'_start = Parent Groupの確定終点

P'_end =
    P'_start
    + R_delta × CachedDirection × RestLength
```

XYZへの単純な平行移動だけでは、頭部回転時に髪形状が追従しないため禁止する。

---

## 10.4 Deferred Payload

完全Dynamicへ戻すほどではないが、完全Staticを継続するには不確実性があるGroupが使用する。

```cpp
struct DeferredPayload
{
    uint32_t packedPredictedDirection;
    float predictedSpeed;

    uint32_t packedPredictedVelocity;
    float accumulatedErrorBound;

    uint32_t deferStartFrame;
    uint32_t maximumDeferFrames;

    float parentMotionBound;
    float rootTransformBound;

    float collisionClearance;
    float predictionResidual;

    uint32_t fallbackState;
    uint32_t deferredFlags;
};
```

Deferredでは限定的な予測更新を実行する。

累積誤差は次で更新する。

```text
E_next =
    E_current
    + E_parent
    + E_root
    + E_prediction
    + E_collision
```

次の場合はDynamicへ戻す。

```text
E_next > DeferredTolerance
```

または、

```text
CollisionClearance - E_next <= CollisionMargin
```

または、

```text
CurrentFrame - DeferStartFrame >= MaximumDeferFrames
```

---

## 10.5 Repair Payload

通常処理を継続できないGroupが使用する。

```cpp
struct RepairPayload
{
    uint32_t packedLastValidDirection;
    float lastValidLength;

    uint32_t packedLastValidVelocity;
    float lastValidBoundaryError;

    uint32_t invalidReasonMask;
    uint32_t repairFirstGroup;
    uint32_t repairLastGroup;

    uint32_t repairAttemptFrame;
    uint32_t verifyAfterFrame;
    uint32_t recurrenceCount;
    uint32_t repairStatus;

    uint32_t lastValidHash;
    uint32_t reserved0;
};
```

Repair対象は以下とする。

* NaN
* Inf
* 長さ異常
* 速度異常
* 位置異常
* 共有点競合
* Cache Hash不一致
* Certificate矛盾
* Payload Type不一致
* Topology Version不一致
* 衝突貫通
* 境界誤差超過
* Root追従失敗
* Owner重複
* 不正なGroup ID
* 不正なPayload Index

Repairは即時成功扱いにしない。

```text
Repair Requested
    ↓
Repair Executed
    ↓
Pending Verification
    ↓
Verification Hold
    ↓
Verified / Failed
```

Repair後は、一定フレームDynamic状態を維持する。

---

# 11. Payload Pool設計

## 11.1 物理配置

状態ごとに独立したGPU Bufferを使用する。

```text
SegmentGroupHeaderBuffer

DynamicPayloadBuffer
StaticCachedPayloadBuffer
StaticTransformedPayloadBuffer
DeferredPayloadBuffer
RepairPayloadBuffer

AdmissibilityCertificateBuffer
```

## 11.2 状態別Poolを使用する理由

全状態のデータを一つの巨大Structへ持たせると、Static GroupでもDynamic用データを読み書きする。

これにより以下が発生する。

* 不要なメモリ消費
* 不要なGPU帯域
* Cache効率低下
* 状態別処理で不要フィールドまでFetch
* データ型としての差が弱くなる

状態別Poolを用いることで、現在必要なPayloadだけを参照する。

## 11.3 Hot／Cold分離

頻繁に参照する情報をHot Bufferへ置く。

```cpp
struct SegmentGroupHotData
{
    uint32_t state;
    uint32_t payloadIndex;
    uint32_t certificateIndex;
    uint32_t runtimeFlags;
};
```

低頻度情報はCold Bufferへ置く。

```cpp
struct SegmentGroupColdData
{
    uint32_t strandId;
    uint32_t localGroupId;
    uint32_t parentGroupId;
    uint32_t childGroupId;
    uint32_t topologyVersion;
    uint32_t blockId;
};
```

分類処理で巨大Header全体を毎回読み込まない。

---

# 12. 状態遷移

## 12.1 状態遷移図

```text
                     ┌─────────────────────┐
                     │       Repair        │
                     └──────────┬──────────┘
                                │ Verified
                                ▼
┌────────────────┐       ┌──────────────┐
│ Static Cached  │◀──────│   Dynamic    │
└───────┬────────┘ Encode└──────┬───────┘
        │ Root Transform         │ Limited Prediction
        ▼                        ▼
┌────────────────────┐   ┌──────────────┐
│ Static Transformed │   │   Deferred   │
└─────────┬──────────┘   └──────┬───────┘
          │ Wake / Invalid       │ Error Limit
          └───────────────┬──────┘
                          ▼
                       Dynamic
```

全状態から異常検出時にRepairへ遷移可能とする。

---

## 12.2 DynamicからStatic Cached

次をすべて満たす場合に候補とする。

```text
MotionRisk <= StaticMotionThreshold
AND
LengthError <= StaticLengthThreshold
AND
DirectionError <= StaticDirectionThreshold
AND
CollisionClearance > RequiredClearance
AND
No Edit
AND
No Collision
AND
No Repair
AND
No NeighborBoundaryFailure
AND
StableFrameCount >= RequiredStableFrames
AND
CertificateGenerationSucceeded
```

遷移時にはDynamic PayloadをStatic Cached PayloadへEncodeする。

---

## 12.3 Static CachedからStatic Transformed

次の場合に遷移する。

```text
Root Transform Version changed
AND
Local Shape remains valid
AND
Swept Collision test passed
AND
Transform Error <= TransformTolerance
```

---

## 12.4 StaticまたはDeferredからDynamic

次の場合にDynamicへ戻す。

* Collider接近
* Wind増加
* Root加速度増加
* Root角速度増加
* 編集イベント
* ParentまたはChildのDynamic化
* Certificate期限切れ
* Version不一致
* 予測誤差超過
* 境界誤差超過
* 衝突余裕不足
* Forced Dynamic
* Unknown状態
* Repair再発

---

## 12.5 DynamicからDeferred

次を満たす場合に候補とする。

```text
完全Static条件は満たさない
AND
MotionRisk is low
AND
PredictionResidual is bounded
AND
CollisionClearance is sufficient
AND
MaximumDeferFrames > 0
```

---

## 12.6 全状態からRepair

次の場合にRepairへ遷移する。

```text
Fatal Error
OR
NaN / Inf
OR
Invalid Payload
OR
Owner Conflict
OR
Boundary Error Limit Exceeded
OR
Penetration Limit Exceeded
OR
Certificate Contradiction
OR
Topology Mismatch
```

---

# 13. 状態決定優先順位

複数の状態要求が同時に発生した場合、次の優先順位を使用する。

```text
1. Fatal / Reject
2. Collision
3. Edit
4. Repair
5. Force Dynamic
6. Predictive Wake
7. Force Static
8. Deferred Request
9. Normal Classification
```

優先度の低い要求が高い要求を上書きしてはならない。

特に、`Force Static`がCollisionまたはEditを上書きすることを禁止する。

---

# 14. Admissibility Certificate

## 14.1 定義

Certificateは、現在の軽量状態を継続して使用できる条件を保持する。

```cpp
struct AdmissibilityCertificate
{
    uint32_t sourceFrame;
    uint32_t validUntilFrame;

    uint32_t topologyVersion;
    uint32_t rootTransformVersion;

    uint32_t scalpVersion;
    uint32_t colliderVersion;

    uint32_t editVersion;
    uint32_t cacheVersion;

    float maximumPositionError;
    float maximumDirectionError;

    float maximumLengthError;
    float maximumBoundaryError;

    float sweptCollisionClearance;
    float maximumRootMotion;

    uint32_t validStateMask;
    uint32_t certificateHash;
};
```

## 14.2 有効条件

Certificateは次をすべて満たす場合に有効である。

```text
CurrentFrame <= ValidUntilFrame
AND
Topology Version一致
AND
Root Transform Version整合
AND
Scalp Version整合
AND
Collider Version整合
AND
Edit Version整合
AND
Cache Version整合
AND
Position Error <= PositionTolerance
AND
Direction Error <= DirectionTolerance
AND
Length Error <= LengthTolerance
AND
Boundary Error <= BoundaryTolerance
AND
Collision Clearance > Error Bound + CollisionMargin
AND
Certificate Hash一致
```

## 14.3 不明状態の扱い

安全性を優先する。

```text
Valid      → 軽量状態を継続可能
Invalid    → DynamicまたはRepair
Unknown    → DynamicまたはRepair
Expired    → Dynamic
```

UnknownをStaticとして扱ってはならない。

---

# 15. 誤差上限

## 15.1 位置誤差

Static再利用時の位置誤差を次で定義する。

```text
E_position =
    E_cache
    + E_root
    + E_parent
    + E_rotation
    + E_prediction
    + E_quantization
```

| 項目               | 内容                      |
| ---------------- | ----------------------- |
| `E_cache`        | Cache生成時の残差             |
| `E_root`         | Root移動の近似誤差             |
| `E_parent`       | 親Groupから伝播する境界誤差        |
| `E_rotation`     | Root回転に伴う方向誤差           |
| `E_prediction`   | Deferred予測誤差            |
| `E_quantization` | Packed Direction等の量子化誤差 |

## 15.2 回転誤差

Group長を`L`、方向角誤差を`θ`とすると、回転による位置誤差を次で近似する。

```text
E_rotation = L × sin(θ)
```

小角度の場合は次を使用できる。

```text
E_rotation ≈ L × θ
```

## 15.3 親Group誤差の伝播

Group `i`の開始点は、Group `i-1`の終点である。

したがって、

```text
E_start(i) = E_end(i - 1)
```

終点誤差は次で計算する。

```text
E_end(i) =
    E_start(i)
    + E_local(i)
```

誤差が毛先方向へ無制限に累積しないよう、一定Group数ごとにAnchorまたはFull Dynamic補正を入れる。

## 15.4 Static許可条件

```text
E_position <= PositionTolerance
AND
E_boundary <= BoundaryTolerance
AND
CollisionClearance - E_position > CollisionMargin
```

---

# 16. Predictive Wake

## 16.1 目的

衝突が発生した後にDynamicへ戻すのでは遅い。

衝突体またはRoot運動が危険域へ入る前にWakeする。

## 16.2 Wake入力

* 隣接GroupのDynamic状態
* Colliderまでの距離
* Colliderの相対速度
* Root線形速度
* Root角速度
* Root加速度
* Wind強度
* Editイベント
* 境界誤差
* Certificate残り寿命
* Repair履歴
* Camera距離
* 見た目上の重要度

## 16.3 Swept Collision

Static Transformedへ移行する前に、変換前から変換後までのGroup軌跡をカプセルまたはAABBで近似する。

```text
SweptBound =
    Union(
        PreviousGroupBound,
        TransformedGroupBound
    )
```

SweptBoundとColliderが交差する可能性がある場合、Static Transformを適用せずDynamicへ戻す。

---

# 17. Shared Point Hemisphere Resolve

## 17.1 問題

隣接Groupは共有点を持つ。

両方が同じ点へ書き込むと、GPU上で競合が発生する。

固定Ownerだけを使用すると、非Owner側の拘束や衝突情報が失われる。

## 17.2 Group Result Buffer

各Groupは最終位置ではなく候補結果を出力する。

```cpp
struct GroupResult
{
    float3 candidateEndPosition;
    float positionResidual;

    float3 candidateVelocity;
    float velocityResidual;

    float3 constraintContribution;
    float collisionResidual;

    uint32_t sourceState;
    uint32_t requestPriority;

    uint32_t certificateValid;
    uint32_t hemisphere;

    uint32_t groupId;
    uint32_t resultFlags;
};
```

## 17.3 半球定義

共有点`Pi`における局所接線を次で求める。

```text
T = normalize(Pi+1 - Pi-1)
```

責任領域を次で分ける。

```text
Parent Hemisphere = -T側
Child Hemisphere  = +T側
```

この半球は点を物理的に複製するものではない。

どの方向から生じた拘束・衝突・運動寄与であるかを分類するために使用する。

## 17.4 優先順位

共有点候補の優先順位は次とする。

```text
1. Fatal / Invalid
2. Collision
3. Edit
4. Repair
5. Force Dynamic
6. Dynamic Constraint
7. Static Transform
8. Deferred Prediction
9. Static Cache
10. Normal
```

## 17.5 同一優先度の決定

```text
Certificateが有効
Boundary Errorが小さい
Collision Residualが小さい
Position Residualが小さい
Group IDが小さい
```

## 17.6 非Owner側の寄与

採用されなかったGroupの結果を完全に破棄しない。

Constraint Contributionを次フレームのOwner側入力または境界補正へ渡す。

これにより、書き込みOwnerは一つに保ちながら、両Groupの物理的影響を維持する。

---

# 18. データ型の不変条件

## 18.1 Owner一意性

```text
各Shared PointのFinal Ownerは必ず1つ
```

## 18.2 Payload整合性

```text
StateとPayload Typeが一致する
```

## 18.3 Static安全性

```text
Invalid / Unknown / Expired CertificateではStaticを禁止
```

## 18.4 長さ保存

```text
abs(CurrentLength - RestLength) / RestLength
<= LengthTolerance
```

## 18.5 境界連続性

```text
Boundary Position Error <= 2mm
```

加えて次も計測する。

* Boundary Direction Error
* Boundary Velocity Error
* Boundary Acceleration Spike

## 18.6 安全優先

```text
Fatal > Collision > Edit > Repair > Force Dynamic > Force Static
```

## 18.7 Repair再検証

```text
Repair実行直後のStatic遷移は禁止
```

## 18.8 決定性

同じ入力、同じSeed、同じバイナリ、同じGPU条件では、次のHashが一致する。

```text
State Sequence Hash
Payload Migration Hash
Owner Resolve Hash
Compaction Sequence Hash
Final Position Hash
```

---

# 19. Morphing Transition

## 19.1 DynamicToStaticEncodeCS

処理内容：

1. Dynamic結果を検証
2. 方向を局所座標へ変換
3. Directionを圧縮
4. Rest Lengthを保存
5. 速度要約を保存
6. 誤差上限を算出
7. Cache Hashを生成
8. Certificateを生成
9. Static Payloadを確保
10. HeaderのPayload参照を更新
11. Dynamic Payloadを遅延解放Queueへ登録

## 19.2 StaticToDynamicDecodeCS

1. Static Payloadを取得
2. Cache Hashを検証
3. 方向を復号
4. 現在の開始点から終点を再構築
5. 速度を復元
6. Dynamic Payloadを確保
7. Lambdaを初期化または履歴から復元
8. HeaderをDynamicへ変更
9. 一定フレームDynamic維持を設定

## 19.3 StaticToTransformedCS

1. Root差分回転を計算
2. Root差分平行移動を計算
3. Swept Boundを生成
4. 衝突Broad Phaseを実行
5. 誤差上限を更新
6. 安全ならStatic Transformedへ移行
7. 危険ならDynamicへ移行

## 19.4 AnyToRepairCS

1. 異常理由を保存
2. 最終有効値を保存
3. 隣接GroupをDirty化
4. Repair範囲を決定
5. Repair Payloadを確保
6. Repair Listへ追加

## 19.5 RepairToDynamicCS

1. 修復値を検証
2. Dynamic Payloadを再生成
3. Verification Holdを設定
4. Repair履歴を保存
5. 再発時は修復範囲を拡張

---

# 20. Payload解放と再利用

DirectX 11ではGPU上の動的メモリ管理に制約がある。

Payload解放後、同一フレームで即時再利用すると、未完了Dispatchから参照される可能性がある。

そのため、世代別Recycle Queueを使用する。

```text
Frame N
    Payload release requested

Frame N + SafetyDelay
    GPU completion confirmed

Frame N + SafetyDelay + 1
    Payload index returned to Free List
```

安全遅延は初期値として3フレームを使用し、実測で調整する。

---

# 21. Block Certificate

## 21.1 目的

508,000 Groupを毎フレーム詳細分類すると、分類処理がボトルネックになる。

そのため複数GroupをBlockへまとめる。

## 21.2 構造

```cpp
struct SegmentGroupBlockCertificate
{
    uint32_t firstGroup;
    uint32_t groupCount;

    uint32_t stateMask;
    uint32_t dirtyMask;

    float maximumMotionRisk;
    float maximumCollisionRisk;

    float maximumBoundaryError;
    float minimumCollisionClearance;

    uint32_t rootTransformVersion;
    uint32_t colliderVersion;

    uint32_t childVersionHash;
    uint32_t validUntilFrame;
};
```

## 21.3 詳細分類を省略する条件

```text
DirtyMask == 0
AND
CurrentFrame <= ValidUntilFrame
AND
Root Transform Version一致
AND
Collider Version一致
AND
MaximumBoundaryError <= BlockBoundaryTolerance
AND
MinimumCollisionClearance > BlockErrorBound + Margin
```

条件を満たすBlockは、内部Groupの詳細Certificate判定を省略する。

---

# 22. Compaction

## 22.1 必要List

```text
DynamicGroupList
StaticCachedGroupList
StaticTransformedGroupList
DeferredGroupList
RepairGroupList
TransitionGroupList
DirtyBlockList
FollowerRebuildList
```

## 22.2 実行モード

### Runtime Fast Mode

高速なAtomic Appendを許可する。

### Formal Deterministic Mode

Prefix SumとStable Scatterを使用する。

研究評価、決定性試験、再現性試験ではFormal Deterministic Modeを使用する。

## 22.3 DispatchIndirect

Listの要素数からIndirect Argsを生成し、必要なGroupだけをDispatchする。

---

# 23. GPU実行パイプライン

```text
01. 入力イベント適用
02. Root Transform更新
03. Collider Version更新
04. Edit Version更新
05. Dirty Block Mark
06. Block Certificate判定
07. Dirty Group展開
08. Group Certificate検証
09. Predictive Wake
10. 状態要求生成
11. 状態優先度Resolve
12. Transition List生成
13. Payload Migration
14. State別Compaction
15. DispatchIndirect Args生成
16. Static Cached Decode
17. Static Transformed適用
18. Deferred Prediction
19. Dynamic Integration
20. Distance Constraint
21. Bend Constraint
22. Root Constraint
23. Collision
24. Self Collision
25. Repair Solve
26. Group Result生成
27. Hemisphere Resolve
28. Shared Point Apply
29. Velocity Recovery
30. Boundary Verification
31. Repair Verification
32. Certificate再生成
33. Follower更新判定
34. Follower ReconstructionまたはCache Reuse
35. Render Preparation
36. Hair Rendering
37. Delayed Telemetry
```

---

# 24. Dynamic Solverとの接続

新規性01のDynamic状態は、簡易的な終点長さ補正だけで終わらせてはならない。

現在のHairSystemが持つ実際の物理経路へ接続する。

最低限、以下を通す。

```text
Integration
Distance Constraint
Bend Constraint
Root Constraint
Collision
Self Collision
Wind
Damping
Velocity Update
```

提案方式とFull Dynamicで異なるSolverを使うと、公平な比較にならない。

B0とPでは、同一のDynamic Solverを使用する。

---

# 25. Root移動

## 25.1 Root平行移動

Root位置差分を次で求める。

```text
DeltaTranslation = RootNew - RootOld
```

平行移動のみの場合は次で適用できる。

```text
P' = P + DeltaTranslation
```

## 25.2 Root回転

回転を含む場合は単純加算を使用しない。

```text
P' =
    RootNewPosition
    + DeltaRotation × (P - RootOldPosition)
```

## 25.3 順次再構築

Group間の接続を保つため、原則として根元から毛先方向へ再構築する。

```text
Group 0
    Rootから再構築

Group 1
    Group 0の確定終点から再構築

...

Group 126
    Group 125の確定終点から再構築
```

並列化する場合は、Block ScanまたはAnchor分割を使用する。

---

# 26. 編集処理

## 26.1 編集対象

* 1点移動
* 1Group変形
* 複数Group変形
* Strand全体移動
* Root移動
* Brush編集
* 長さ変更
* 曲率変更

## 26.2 編集優先度

EditはStatic要求より優先する。

```text
Edit > Force Static
```

## 26.3 Dirty伝播

編集Groupを中心に、必要範囲だけDirty化する。

```text
Edited Group
Parent Boundary
Child Boundary
Certificate
Follower Cache
```

全ストランドResetは原則として行わない。

## 26.4 編集後の状態

編集対象は即座にDynamicへ戻す。

一定フレーム安定するまでStatic化を禁止する。

---

# 27. 衝突処理

## 27.1 Dynamic Group

通常のCollision Solverを使用する。

## 27.2 Static Group

完全なCollision Solverを毎フレーム実行しない。

代わりに次を使用する。

* Collider Version
* Collision Clearance
* Swept Bound
* Broad Phase
* Predictive Wake
* Error Bound

## 27.3 Static継続条件

```text
CollisionClearance
    >
MaximumPositionError
    + CollisionMargin
```

## 27.4 衝突不明

衝突状態が取得できない場合は安全側へ倒す。

```text
Unknown Collision State
    → Dynamic
```

---

# 28. Follower設計

## 28.1 軽量情報

Followerは次のみを保持する。

```cpp
struct FollowerRuntimeData
{
    uint32_t parentGuideId;
    uint32_t correspondingGroupId;

    float3 rootOffset;
    float followWeight;

    float clumpWeight;
    float noiseWeight;
    float width;

    uint32_t materialId;
    uint32_t cacheVersion;
    uint32_t runtimeFlags;
};
```

## 28.2 Follower更新規則

```text
Guide Dynamic
    → 対応Followerを再構築

Guide Static Transformed
    → Followerへ同じ剛体変換を適用

Guide Static Cached
    → Follower Cacheを再利用

Guide Deferred
    → 必要範囲だけ予測更新

Guide Repair
    → 対応Followerを無効化または局所再構築
```

## 28.3 Follower Cache

Guide GroupのCertificate、Root Version、Follower Cache Versionが一致する場合、Follower全点の再構築を省略する。

## 28.4 目的

Guide計算だけを削減しても、96,000 Followerの全点再構築が残ると60FPS達成が難しい。

したがってFollower Cache Reuseは性能上の必須実装とする。

ただし、研究上の中心新規性ではなく、提案型の効果を最終描画まで反映するための実装要件として扱う。

---

# 29. Repair設計

## 29.1 修復範囲

最初は異常Group単体を対象とする。

再発した場合は範囲を拡大する。

```text
1回目
    Group単体

2回目
    Parent + Current + Child

3回目
    Strand Block

4回目
    Strand全体
```

## 29.2 修復方法

優先順位は次とする。

1. 最終有効Cacheから復元
2. ParentとChildから補間
3. Rest Poseから復元
4. Strand Rootから再生成
5. Full Strand Reset

## 29.3 検証

Repair後は次を検証する。

* NaN／Inf
* 長さ誤差
* 境界誤差
* Collision
* Owner一意性
* Velocity Spike
* Certificate整合
* 再発

---

# 30. 決定性

## 30.1 決定性が必要な理由

Atomic Appendや実行順依存の書き込みにより、同一入力でも結果が変わる可能性がある。

研究評価では、性能だけでなく再現性が必要である。

## 30.2 Hash

毎フレーム次を記録する。

```text
StateSequenceHash
PayloadMigrationHash
OwnerResolveHash
CompactionSequenceHash
FinalPositionHash
```

## 30.3 Formal Mode

正式評価時には次を固定する。

* Random Seed
* Time Step
* Iteration Count
* Dispatch順序
* Stable Compaction
* Asset
* Camera
* Wind
* Collider Animation
* Edit Event Sequence

---

# 31. テレメトリ

## 31.1 フレーム性能

```text
frame_index
cpu_frame_ms
gpu_frame_ms
fps
p95_frame_ms
p99_frame_ms
overrun_16_67ms
```

## 31.2 Group状態

```text
static_cached_count
static_transformed_count
dynamic_count
deferred_count
repair_count
transition_count
dirty_group_count
dirty_block_count
```

## 31.3 Payload

```text
dynamic_payload_bytes
static_payload_bytes
deferred_payload_bytes
repair_payload_bytes
payload_migration_count
payload_migration_ms
payload_pool_fragmentation
```

## 31.4 Certificate

```text
certificate_valid_count
certificate_invalid_count
certificate_expired_count
certificate_unknown_count
false_accept_count
false_reject_count
```

## 31.5 品質

```text
maximum_length_error
maximum_boundary_position_error
maximum_boundary_direction_error
maximum_boundary_velocity_error
collision_miss_count
penetration_count
shared_point_conflict_count
nan_count
inf_count
```

## 31.6 Repair

```text
repair_requested_count
repair_executed_count
repair_verified_count
repair_failed_count
repair_recurrence_count
unverified_repair_count
```

## 31.7 Follower

```text
follower_rebuild_count
follower_transform_count
follower_cache_reuse_count
follower_rebuild_ms
```

---

# 32. 出力ファイル

正式実験では次を保存する。

```text
run_settings.json
hardware_profile.json
frame_metrics.csv
group_state_metrics.csv
certificate_metrics.csv
payload_metrics.csv
quality_metrics.csv
repair_metrics.csv
follower_metrics.csv
event_log.jsonl
summary.json
formal_result.json
graphs/
```

---

# 33. 比較対象

## B0：Full Dynamic

全Guide Groupを毎フレーム完全物理計算する。

## B1：Strand単位Sleep

Strand全体をStaticまたはDynamicにする。

## B2：Block単位Sleep

複数GroupをBlockとしてSleepする。

## B3：Segment Threshold Sleep

速度または変位の閾値だけでSegmentをStatic化する。

## B4：Segment Cache Reuse

Segment単位で最終結果を再利用する。

## B5：Segment Active Set

Dynamic対象のSegmentだけをSolverへ参加させる。

## B6：GPU Compaction方式

状態分類後にGPU Listへ集約する。

## B7：固定サイズ統合Struct

全状態のフィールドを一つの固定Structへ保持する。

## B8：外部State Array方式

State、Certificate、Risk、Repairを別配列へ保持する。

アルゴリズム自体は提案方式と揃える。

## B9：状態別Payload、Certificateなし

Payload Morphingだけを行う。

## B10：Certificateあり、Hemisphere Resolveなし

共有点を固定Ownerまたは単純平均で決定する。

## P：完全提案方式

以下をすべて使用する。

```text
State-dependent Payload
Morphing Transition
Admissibility Certificate
Error Bound
Predictive Wake
Hemisphere Resolve
Repair Verification
Block Certificate
Follower Cache Reuse
```

---

# 34. B8比較の重要性

B8は提案方式と同じ状態、同じCertificate、同じSolver、同じ判定式を使用する。

違いは、外部配列として管理するか、状態依存型としてPayloadをMorphingするかである。

B8とPを比較することで、次を検証する。

* メモリ量
* メモリ帯域
* Cache効率
* 分類時間
* Solver時間
* Payload移行コスト
* 不正状態発生率
* State／Payload不整合
* Repair率
* 実装上の安全性

PがB8とほぼ同じ結果の場合、「新しいデータ型」としての研究的価値は弱い。

---

# 35. Ablation Study

| ID  | 無効化する機能                   |
| --- | ------------------------- |
| A1  | 状態別Payload                |
| A2  | Morphing Transition       |
| A3  | Admissibility Certificate |
| A4  | Error Bound               |
| A5  | Predictive Wake           |
| A6  | Hemisphere Resolve        |
| A7  | Deterministic Tie Break   |
| A8  | Block Certificate         |
| A9  | Repair Verification       |
| A10 | Follower Cache Reuse      |
| A11 | Hot／Cold分離                |
| A12 | Stable Compaction         |

各機能を外した場合の性能、安全性、品質を測定する。

---

# 36. 研究質問

## RQ1

状態に応じてPayloadを切り替えることで、固定サイズStructまたは外部State Array方式よりGPUメモリ帯域を削減できるか。

## RQ2

誤差上限付きCertificateにより、単純な速度・変位閾値よりFalse Staticを削減できるか。

## RQ3

Hemisphere Resolveにより、共有点競合とStatic／Dynamic境界誤差を削減できるか。

## RQ4

Morphing Transitionによって、StaticからDynamicへ復帰するときの位置跳躍と速度スパイクを削減できるか。

## RQ5

Block Certificateによって、分類処理自体のGPU負荷を抑えられるか。

## RQ6

Follower Cache Reuseを含めた場合、Guideのみを最適化する方式よりフレーム時間を削減できるか。

## RQ7

全動的シーンでは追加管理コストをどこまで抑えられるか。

## RQ8

提案型をHair以外の線状物理要素へ一般化できるか。

---

# 37. 研究仮説

## H1：帯域削減仮説

Static GroupがDynamic Payloadを読み書きしないため、B7およびB8よりGPUメモリ帯域を削減できる。

## H2：安全再利用仮説

CertificateとError Boundにより、B3よりFalse Static率を低減できる。

## H3：境界連続性仮説

Hemisphere Resolveにより、共有点競合を0件にし、最大境界位置誤差を2mm以下へ抑えられる。

## H4：遷移品質仮説

Morphing TransitionとVelocity Recoveryにより、Static復帰時の速度スパイクを削減できる。

## H5：性能仮説

低運動および混在シーンでは、B0よりp95 GPU Frame Timeを削減できる。

## H6：最悪時制御仮説

全動的シーンでも、提案方式の追加管理コストをFull Dynamic比20%以内に抑えられる。

---

# 38. 評価シナリオ

## S01：完全静止

* Root固定
* Windなし
* Collider移動なし
* Editなし

目的：

* 最大Static率
* Cache再利用性能
* 最小負荷

## S02：Root平行移動

* 頭部を一定速度で移動
* 回転なし

目的：

* Static Transformed
* Root追従
* 境界誤差

## S03：Root回転

* 頭部を左右へ回転
* 平行移動なし

目的：

* 剛体回転追従
* 単純平行移動との差

## S04：Root複合運動

* 平行移動
* 回転
* 加速度変化

目的：

* Predictive Wake
* Transform Error

## S05：弱風

* 低振幅
* 低周波

目的：

* Static／Deferred／Dynamic混在

## S06：強風

* 高振幅
* 高周波

目的：

* 全Dynamic近似
* 最悪時オーバーヘッド

## S07：局所衝突

* 球Colliderが毛先だけへ接触

目的：

* 局所Wake
* 近傍伝播

## S08：高速Collider接近

* 高速ColliderがStatic Groupへ接近

目的：

* Swept Collision
* 予測復帰

## S09：Root近傍編集

目的：

* 下流Groupへの影響
* Dirty伝播

## S10：毛先編集

目的：

* 局所更新
* 上流Static維持

## S11：連続Brush編集

目的：

* State Thrashing
* Migration Cost

## S12：Static／Dynamic境界移動

目的：

* 境界誤差
* Owner Resolve

## S13：Repair誘発

* NaN注入
* Cache Hash破壊
* Payload Index破壊

目的：

* Repair
* 再検証

## S14：Collider Version変更

目的：

* Certificate無効化

## S15：Topology Version変更

目的：

* Cache破棄
* Repairまたは再生成

## S16：全動的

* 強風
* Root高速運動
* 衝突多数

目的：

* 最悪時性能

## S17：長時間安定性

* 30分
* 2時間
* 8時間

目的：

* メモリリーク
* Pool Fragmentation
* State Thrashing
* Repair再発

## S18：決定性

* 同一条件を複数回実行

目的：

* Hash一致
* 再現性

---

# 39. Ground Truth

False Staticと品質差を評価するため、Full DynamicをGround Truthとして使用する。

同一入力をB0とPへ与え、各Groupの位置、方向、速度、衝突状態を比較する。

## 39.1 False Static

提案方式でStaticと判定されたが、Full Dynamicとの差が許容値を超えた場合をFalse Staticとする。

```text
Proposal State = Static
AND
Position Difference > PositionTolerance
```

または、

```text
Collision State differs from Ground Truth
```

## 39.2 False Reject

Static化可能であったが、Dynamicを継続したGroupをFalse Rejectとする。

False Rejectは安全性を損なわないが、性能を損なう。

---

# 40. 評価指標

## 40.1 性能

* Average FPS
* Minimum FPS
* p95 Frame Time
* p99 Frame Time
* Overrun率
* Hair GPU Time
* Classification Time
* Transition Time
* Payload Migration Time
* Dynamic Solver Time
* Shared Point Resolve Time
* Follower Reconstruction Time
* Hair Draw Time

## 40.2 メモリ

* Header Buffer Size
* Payload Pool Size
* Certificate Size
* Position Buffer Size
* Total VRAM
* Average Payload Size per Group
* Pool Fragmentation
* Allocation Failure Count

## 40.3 安全性

* False Static Count
* False Accept Rate
* False Reject Rate
* Shared Point Conflict Count
* Collision Miss Count
* Penetration Count
* NaN Count
* Inf Count
* Unverified Repair Count
* Payload Mismatch Count
* Topology Mismatch Count

## 40.4 品質

* Position RMSE
* Direction RMSE
* Velocity RMSE
* Maximum Length Error
* Maximum Boundary Position Error
* Maximum Boundary Direction Error
* Maximum Boundary Velocity Error
* Wake Position Jump
* Wake Velocity Spike
* Temporal Flicker

## 40.5 データ型固有

* Payload Migration Count
* Payload Migration Success Rate
* State／Payload Consistency
* Static Payload Residency
* Dynamic Payload Residency
* Certificate Reuse Count
* Certificate Invalid Count
* Block Skip Rate
* Hot Buffer Bandwidth
* Cold Buffer Bandwidth

---

# 41. 合格基準

## 41.1 必須安全基準

```text
NaN Count = 0
Inf Count = 0
Fatal Failure Count = 0
Shared Point Conflict Count = 0
Payload Mismatch Count = 0
Unverified Repair at End = 0
```

## 41.2 境界基準

```text
Maximum Boundary Position Error <= 2mm
```

方向と速度については、Ground Truthおよび見た目評価から正式閾値を決定する。

## 41.3 決定性

Formal Deterministic Modeにおいて、同一条件のHashが一致する。

## 41.4 性能

最終目標：

```text
Average FPS >= 60
p95 Frame Time <= 16.67ms
p99 Frame Time <= 20ms
Overrun Rate <= 5%
```

すべての最悪シナリオで常時60FPSを必須とすると研究成立が性能依存になりすぎる。

そのため性能評価を次のように分ける。

### 必須

* 静止
* Root移動
* 弱風
* 局所衝突
* 局所編集
* 一般混在

上記で平均60FPS以上を目指す。

### 参考最悪値

* 全動的
* 強風
* 多数衝突

これらでは、Full Dynamicに対する追加オーバーヘッドを測る。

## 41.5 新規性成立基準

次の3つをすべて実装する。

```text
1. 状態依存Payload Morphing
2. 誤差上限付きAdmissibility Certificate
3. 決定的Hemisphere Shared-Point Resolve
```

さらにB8、B9、B10との比較とAblationを行う。

---

# 42. 安全判定Gate

## Gate 1：真正な型変形

状態遷移時に、実際にPayload形式と参照先が変化する。

State Flagだけを変える実装は不合格とする。

## Gate 2：型固有演算

状態ごとに許可演算が異なる。

すべての状態で同一Solverを実行する実装は不合格とする。

## Gate 3：誤差上限

Certificateが単なるbool集合ではなく、測定可能な誤差上限を持つ。

## Gate 4：共有点演算

候補結果、半球責任、優先度、決定的Tie Breakを実装する。

## Gate 5：Baselineとの差

B8外部State Array方式に対して、次のいずれかで明確な改善を示す。

* GPU帯域
* VRAM
* Cache効率
* 状態不整合
* 境界安全性
* Repair率
* 性能

## Gate 6：Ablation

コア要素を外すと、性能、安全性、品質のいずれかが悪化することを示す。

## Gate 7：先行研究比較

以下の組み合わせが同一構成として既に存在しないか確認する。

```text
State-dependent representation
Error-bounded transition certificate
Deterministic shared-node resolve
```

完全一致する先行研究が存在した場合は、差分を再設計する。

---

# 43. 新規性を弱くする実装

次の場合、研究判定は「危ない」とする。

* StateをStructへ追加しただけ
* Static時もDynamic Payloadを保持する
* Certificateが複数フラグだけ
* 誤差上限を計算しない
* Static Transformが平行移動だけ
* Shared Pointへ複数Groupが直接書き込む
* Ownerが常にParent固定
* Dynamic Groupが簡易距離補正しか行わない
* Followerを毎フレーム全再構築する
* B8と比較しない
* Full DynamicをGround Truthにしない
* False Staticを測らない
* Repairを実行しただけで成功扱いする
* 全動的シーンを評価しない
* 実測なしで60FPSを主張する

---

# 44. 実装ファイル構成

既存ファイルを優先して使用し、不要な新規ファイル乱立を避ける。

## 44.1 C++側

### `include/hairgpu/Types.h`

追加または統合する内容：

* `SegmentGroupState`
* `SegmentGroupPayloadType`
* `SegmentGroupHeader`
* 状態別Payload
* Certificate
* Group Result
* Block Certificate
* Telemetry構造

### `apps/dx11_viewer/src/GpuHairViewer.h`

追加内容：

* GPU Buffer
* UAV／SRV
* Compute Shader
* State List
* Payload Pool
* Indirect Args
* Formal Mode設定
* Telemetry

### `GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl`

追加内容：

* Buffer生成
* Shader読み込み
* UAV／SRV生成
* Initial Payload Pool
* Free List
* Recycle Queue

### `GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl`

追加内容：

* 初期状態生成
* Payload初期化
* Certificate初期化
* Frame Pipeline統合
* 全体CopyResource削減

### `GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl`

追加内容：

* Follower Cache
* Guide Stateに応じたFollower更新
* Repair時の描画フォールバック

### `GpuHairViewer_parts/07_draw_path_and_gpu_query.inl`

追加内容：

* Group状態別GPU Query
* Migration計測
* Resolve計測
* Follower計測
* Draw計測

### `GpuHairViewer_parts/09_hair_simulation.inl`

追加内容：

* Dynamic Group List接続
* Static Group処理
* Deferred処理
* Repair処理
* Shared Point Resolve
* Velocity Recovery

### `GpuHairViewer_parts/10_phase1_telemetry.inl`

追加内容：

* Requested／Applied／Observed
* Group状態
* Certificate
* Payload
* Boundary
* Repair
* Follower
* Hash

### `GpuHairViewer_parts/11_runtime_export.inl`

追加内容：

* CSV
* JSONL
* Settings
* Summary
* Formal Result

---

## 44.2 HLSL側

既存の`HairSegmentGroupRuntime.hlsl`を中心に統合する。

必要Pass：

```text
MarkDirtyBlocksCS
ValidateBlockCertificateCS
ExpandDirtyGroupsCS
ValidateGroupCertificateCS
PredictiveWakeCS
ClassifyStateRequestCS
ResolveStatePriorityCS

DynamicToStaticEncodeCS
StaticToTransformedCS
StaticToDynamicDecodeCS
DynamicToDeferredCS
DeferredToDynamicCS
AnyToRepairCS
RepairToDynamicCS

CompactStateListsCS
BuildIndirectArgsCS

ApplyStaticCachedCS
ApplyStaticTransformedCS
ApplyDeferredPredictionCS

DynamicIntegrationCS
DynamicDistanceConstraintCS
DynamicBendConstraintCS
DynamicCollisionCS

RepairSolveCS
BuildGroupResultCS
ResolveSharedPointCS
ApplySharedPointCS
RecoverVelocityCS

VerifyBoundaryCS
VerifyRepairCS
GenerateCertificateCS

ClassifyFollowerUpdateCS
ApplyFollowerTransformCS
RebuildFollowerCS
ReuseFollowerCacheCS
```

---

## 44.3 Python側

### `tools/final_stage/hairgpu_final_run.py`

追加内容：

* CMSG設定読み込み
* Formal Gate
* 必須テレメトリ確認
* 失敗理由
* 統計生成

### `tools/final_stage/hairgpu_runtime_suite.py`

追加内容：

* S01～S18実行
* B0～B10実行
* P実行
* Ablation実行
* 再開
* 長時間試験

### `tools/final_stage/hairgpu_final_db.py`

追加内容：

* Group状態
* Certificate
* Payload
* Repair
* Boundary
* Determinism
* Baseline比較

### `docs/sample_assets/final_runtime_suite_template.json`

追加内容：

```json
{
  "guide_count": 4000,
  "follower_count": 24,
  "display_strand_count": 100000,
  "point_count": 128,
  "solver_iterations": 6,
  "resolution_width": 1920,
  "resolution_height": 1080,
  "target_fps": 60,
  "proposal": "CMSG",
  "formal_deterministic_mode": true
}
```

---

# 45. 実装順序

## Phase 0：既存経路の固定

* B0 Full Dynamicを正常化
* 100,000本
* 128点
* 6反復
* 1080p
* Telemetry整備
* Ground Truth保存

## Phase 1：型の基礎

* Header
* State enum
* Payload Type
* 状態別Buffer
* Payload Index
* State／Payload整合検査

## Phase 2：Static Cached

* DynamicToStatic Encode
* Static Decode
* Cache Hash
* Rest Length
* Packed Direction
* 最終有効結果再利用

## Phase 3：Static Transformed

* Root平行移動
* Root回転
* 差分Transform
* Swept Bound
* 衝突Broad Phase

## Phase 4：Certificate

* Version Vector
* Position Error
* Direction Error
* Boundary Error
* Collision Clearance
* Valid Until Frame
* False Accept／Reject

## Phase 5：Shared Point Resolve

* Group Result Buffer
* Hemisphere
* Priority
* Deterministic Tie Break
* Shared Point Apply
* Boundary Telemetry

## Phase 6：Dynamic Solver接続

* Dynamic List
* 既存Solver
* Static／Dynamic混在
* Velocity Recovery
* Boundary Constraint

## Phase 7：Deferred

* 予測Payload
* 累積誤差
* 最大延期フレーム
* Dynamic復帰

## Phase 8：Repair

* 異常検出
* Repair Payload
* 局所修復
* Verification Hold
* 再発拡張

## Phase 9：Block Certificate

* Dirty Block
* Block有効性
* 詳細分類省略
* Classification Time削減

## Phase 10：Follower

* Guide State連動
* Follower Transform
* Cache Reuse
* 局所再構築

## Phase 11：比較実験

* B0～B10
* P
* A1～A12
* S01～S18
* 長時間試験
* 決定性

---

# 46. 実装完了条件

以下がすべて満たされた場合に、新規性01の実装完了とする。

```text
[ ] Headerと状態別Payload Poolが存在する
[ ] 状態変更時にPayload Migrationが発生する
[ ] StateとPayload Typeの整合を毎フレーム確認する
[ ] Static Cachedが実際にDynamic Solverを通らない
[ ] Static TransformedがRoot回転へ対応する
[ ] Dynamic Groupが本物のHair Solverへ接続される
[ ] Deferredが累積誤差を持つ
[ ] Certificateが誤差上限を持つ
[ ] Shared Point Resolveが候補Bufferを使用する
[ ] Final Ownerが一意である
[ ] Repair後に再検証する
[ ] Block Certificateが動作する
[ ] Follower Cache Reuseが動作する
[ ] Requested／Applied／Observedを保存する
[ ] B8比較を実行できる
[ ] Ground Truth比較を実行できる
[ ] Formal Deterministic Modeを実行できる
```

---

# 47. リスク

## 47.1 管理コストが大きい

状態分類、Certificate、Migration、CompactionがFull Dynamicより重くなる可能性がある。

対策：

* Hot／Cold分離
* Block Certificate
* Dirty Block限定
* Indirect Dispatch
* Delayed Telemetry
* 全動的時のFast Path

## 47.2 Payload Migrationが頻発する

StaticとDynamicを短時間で往復するState Thrashingが発生する可能性がある。

対策：

* Hysteresis
* Minimum State Hold
* Stable Frame Count
* Migration Cooldown
* Dynamic保持期間

## 47.3 誤差上限が保守的すぎる

False Rejectが増え、性能が向上しない可能性がある。

対策：

* シナリオ別閾値調整
* BlockとGroupの二段階判定
* Ground Truthから統計的校正
* 誤差項ごとのAblation

## 47.4 誤差上限が緩すぎる

False Staticや衝突見逃しが発生する可能性がある。

対策：

* UnknownはDynamic
* Collision Margin
* Swept Bound
* Predictive Wake
* False Acceptを最重要失敗として扱う

## 47.5 Follower処理がボトルネックになる

Guide計算が減ってもFollower再構築と描画が残る。

対策：

* Follower Cache Reuse
* Static Transform
* LOD
* Visibility Culling
* Render Density Control
* Instanced描画

## 47.6 VRAMが増加する

複数Payload Pool、Certificate、Result BufferによりVRAMが増える可能性がある。

対策：

* 圧縮方向
* 16-bitまたはPacked表現
* Pool上限
* Buffer再利用
* Hot／Cold分離
* Formal用BufferとRuntime用Bufferの分離

---

# 48. 性能見込み

現時点ではCMSG完全実装後の実測がないため、安定60FPSを保証しない。

GTX 1660級を想定した場合、次の傾向を予想する。

| シーン      | 60FPS達成見込み |
| -------- | ---------: |
| 完全静止     |         高い |
| Root低速移動 |      比較的高い |
| 弱風       |      中程度以上 |
| 局所衝突     |        中程度 |
| 局所編集     |        中程度 |
| 一般混在     |        中程度 |
| 強風       |         低い |
| 全動的      |      非常に低い |

CMSGだけでなく、次を同時に実装する必要がある。

* Follower Cache Reuse
* Dirty Block限定分類
* Hot／Cold分離
* CPU Readback削減
* 不要CopyResource削減
* Indirect Dispatch
* 描画LOD
* Visibility Culling
* Instanced描画

60FPSは新規性成立条件ではなく、最終性能目標として評価する。

---

# 49. 研究成果物

最終的に次を成果物とする。

1. CMSG型の形式定義
2. C++／HLSL実装
3. Full Dynamic Baseline
4. 外部State Array Baseline
5. 固定Struct Baseline
6. Ablation Study
7. S01～S18評価
8. GTX 1660級での性能測定
9. RTX 4070 Ti SUPER等での参考測定
10. False Static評価
11. 境界誤差評価
12. Determinism評価
13. 長時間安定性評価
14. 卒業論文
15. 発表スライド
16. 実行動画
17. CSV／JSONL／グラフ
18. 再現可能なFormal Runtime Suite

---

# 50. 論文構成への反映

## 第1章：はじめに

* 大量髪シミュレーションの問題
* Strand全体更新の限界
* 研究目的
* 研究貢献

## 第2章：関連研究

* Guide／Follower
* PBD／XPBD
* Rod／Cosserat Rod
* GPU Hair
* Sleeping
* Cache Reuse
* Active Set
* Adaptive Simulation
* Error-bounded Reuse
* Shared Vertex／Node Resolve
* 本研究との差

## 第3章：提案手法

* Segment Group
* 状態依存Payload
* Morphing Transition
* Certificate
* Hemisphere Resolve
* Repair
* Block Certificate
* Follower連携

## 第4章：実装

* DirectX 11
* Buffer
* Compute Shader
* Compaction
* Indirect Dispatch
* Telemetry
* Formal Mode

## 第5章：評価

* Baseline
* Ablation
* Scenario
* Metrics
* Ground Truth
* Performance
* Safety
* Quality

## 第6章：結果と考察

* 速度向上
* 帯域削減
* False Static
* 境界誤差
* 全動的時の限界
* Follower負荷
* 一般化可能性

## 第7章：結論

* 研究成果
* 制限
* 今後の課題

---

# 51. 研究上の貢献

本研究の貢献を次の3点とする。

## 貢献1

線状物理要素を、状態に応じて内部物理表現とPayloadを変更するMorphing Segment-Group Data Typeとして定義したこと。

## 貢献2

軽量表現を使用可能か判断するため、Version整合性、誤差上限、境界誤差、衝突余裕を統合したAdmissibility Certificateを導入したこと。

## 貢献3

同一ストランド内部で異なる状態を混在させるため、半球責任と決定的優先度ReduceによるShared-Point Resolveを導入したこと。

---

# 52. 最終新規性定義

本研究の新規性を次のように定義する。

> 本研究は、線状物理要素を、状態に応じて内部物理表現、GPUメモリ配置、許可演算を変化させるMorphing Segment-Group Data Typeとして定義する。さらに、誤差上限付きAdmissibility Certificateによる状態遷移管理と、決定的Hemisphere Shared-Point Resolveを統合することで、同一ストランド内部に静的再利用、剛体追従、動的物理、延期処理、局所修復を安全に共存させる。

簡潔には次のように表す。

> 計算を止めるだけではなく、状態に応じて物理データ表現そのものを変形し、その変形の許容性と共有点の整合性を型自身が管理するGPU線状物理データ型である。

---

# 53. 最終判定

次の実装では、新規性評価は「危ない」のままである。

```text
Segment Group
+
State Flag
+
Cache
+
Repair
```

次の構成をすべて実装し、比較実験で独立した効果を示した場合、完全な世界初を保証するものではないが、学部卒業研究としては「安全」と評価できる可能性が高くなる。

```text
State-dependent Payload Morphing
+
Error-bounded Admissibility Certificate
+
Deterministic Hemisphere Shared-Point Resolve
+
B8 External State Array Comparison
+
Ground Truth Safety Evaluation
+
Ablation Study
```

したがって、新規性01では、単なるSegment単位SleepingやCache reuseを中心に置かない。

本研究の中心は、線状物理データの表現と実行意味論を状態に応じて変化させる新しいデータ型の設計・実装・評価である。

---

# 54. 完成判定チェックリスト

## 研究設計

* [ ] 中心新規性が3要素で固定されている
* [ ] 既存要素を新規性として主張していない
* [ ] B8との比較が定義されている
* [ ] Ground Truthが定義されている
* [ ] Ablationが定義されている

## データ型

* [ ] StateごとにPayloadが異なる
* [ ] HeaderとPayloadが分離されている
* [ ] State／Payload不整合を検出できる
* [ ] Payload Migrationが存在する
* [ ] 遅延解放が存在する

## 安全性

* [ ] Certificateが誤差上限を持つ
* [ ] UnknownはDynamicまたはRepairになる
* [ ] CollisionがStaticより優先される
* [ ] Shared Point Ownerが一意である
* [ ] Repair後に再検証される
* [ ] False Staticを測定できる

## 物理

* [ ] Dynamic Groupが実Solverへ接続される
* [ ] Static Transformが回転へ対応する
* [ ] Rootから毛先へ接続を維持できる
* [ ] Wake時の速度を復元できる
* [ ] Static／Dynamic境界を補正できる

## GPU

* [ ] 状態別Listが生成される
* [ ] Indirect Dispatchを使用できる
* [ ] Formal Stable Compactionがある
* [ ] Block Certificateがある
* [ ] CPU Readbackを最小化している

## Follower

* [ ] Guide状態に応じて更新方法が変わる
* [ ] Static Cacheを再利用できる
* [ ] 局所再構築できる
* [ ] 全再構築時間を計測できる

## 評価

* [ ] S01～S18を実行できる
* [ ] B0～B10を実行できる
* [ ] A1～A12を実行できる
* [ ] GTX 1660級で実測する
* [ ] 長時間試験を行う
* [ ] 決定性を検証する
* [ ] Formal Resultを自動生成する

---

# 55. 結論

CMSG Data Typeは、髪の一部分をStaticにする単純な最適化ではない。

Segment Groupの状態に応じて、保持する物理表現、GPU上のPayload、実行可能な演算、安全条件を変更する。

さらに、誤差上限付きCertificateによって軽量表現の使用可否を判断し、隣接Group間では決定的なHemisphere Resolveによって共有点を確定する。

この構成を実装し、固定Struct、外部State Array、単純Sleep、Cache reuse、Active Setとの比較を行うことで、本研究を単なる既存技術の統合ではなく、GPU線状物理要素のデータ表現と実行意味論を再定義する研究として成立させる。
