# 新規性01 完全版企画書兼設計書

# Certified Morphing Segment-Group Data Type

## 状態依存Payload変形・誤差上限付き遷移・決定的共有点解決を統合したGPU線状物理データ型

---

# 0. 文書情報

| 項目             | 内容                                         |
| -------------- | ------------------------------------------ |
| 文書種別           | 卒業研究・新規性企画書兼実装設計書                          |
| 対象研究           | GPU常駐型HairSystem                           |
| 新規性番号          | 新規性01                                      |
| 基準リポジトリ        | `MKyosei2/HairSystem`                      |
| 基準ブランチ         | `main`                                     |
| 基準コミット         | `4582b8c2b49fb832da4e88a0e5bf15d96cc5b2a0` |
| コミット表示名        | `64`                                       |
| 提案名称           | Certified Morphing Segment-Group Data Type |
| 略称             | CMSG Data Type                             |
| 日本語名称          | 許容性証明付き可変セグメントグループ型                        |
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
| 低性能側評価対象       | GTX 1660級GPUを搭載した少し古いPC                    |
| 新規性最終評価目標      | 安全                                         |
| 実装状態           | 現在は基盤実装段階。本文書に基づき完成実装する                    |

---

# 1. 企画概要

本研究では、大量の髪、ロープ、繊維、植物、パーティクル列などの線状物理要素を、常に同じデータ表現と同じ物理演算で処理する方式から脱却する。

髪の1本全体を一つの処理単位とするのではなく、ストランド内部を局所的な「点―線―点」の単位へ分割し、この単位を`Segment Group`と呼ぶ。

各Segment Groupは、単なるStatic／Dynamicフラグではなく、状態に応じて次の要素を変化させる。

1. 保持する物理データ
2. 使用するGPUメモリ領域
3. 実行可能な演算
4. 更新頻度
5. 衝突確認方法
6. 隣接Groupとの共有点解決方法
7. 異常時の復旧方法
8. 描画用データの再利用方法

本研究の目的は、動いていないGroupの計算を止めるだけではない。

> 線状物理要素を、状態に応じて内部表現、Payload、許可演算、更新頻度を変更する可変データ型として定義し、その表現を安全に使用できる条件と、隣接Group間の共有点整合性をデータ型自身が管理する。

この提案を、Certified Morphing Segment-Group Data Type、略してCMSG Data Typeと呼ぶ。

---

# 2. 研究背景

## 2.1 大量髪シミュレーションの問題

髪を100,000本、1本128点で表現する場合、表示粒子数は次のようになる。

```text
100,000 × 128 = 12,800,000点
```

すべての点に対して毎フレーム以下を実行すると、少し古いGPUで安定60FPSを維持することは難しい。

* 位置積分
* 距離拘束
* 曲げ拘束
* ねじれ拘束
* Root拘束
* 頭部・身体との衝突
* 自己衝突
* 速度更新
* GuideからFollowerへの再構築
* 描画用頂点生成
* GPUバッファ更新
* 状態分類
* 破綻検出
* 修復処理

特に100,000本すべてを高精度な動的物理として扱う場合、計算量だけでなく、GPUメモリ帯域、UAV書き込み、衝突探索、描画時のオーバードローも大きくなる。

---

## 2.2 Strand単位のStatic化の限界

髪1本の内部では、異なる状態が同時に存在する。

```text
根元                  中間部                 毛先
Static ─ Static ─ Deferred ─ Dynamic ─ Dynamic
```

根元付近は頭皮へ固定されて安定している一方、毛先だけが風や衝突によって動くことがある。

Strand全体をDynamicにすると、動いていない根元や中間部まで再計算する。

Strand全体をStaticにすると、毛先の運動、衝突、慣性を失う。

そのため、本研究ではStrand内部をSegment Groupへ分割し、同一Strand内で複数の状態を共存させる。

---

## 2.3 単純なSegment単位Sleepingの限界

SegmentごとにStatic／Dynamicフラグを付けるだけでは、次の問題が残る。

* Static時もDynamic時と同じPayloadを保持する
* Static化してよい条件が曖昧
* Root回転時の追従方法が未定義
* 隣接Groupが共有点へ同時に書き込む
* Static／Dynamic境界で折れや跳躍が発生する
* Cacheが古いか判定できない
* 衝突体が近づいても復帰が遅れる
* Repair後すぐStaticへ戻り、状態が振動する
* 全Groupを毎フレーム詳細分類すると分類処理自体が重い
* Sleeping、Cache reuse、Active Setとの差が弱い

したがって、単純な状態配列ではなく、データ型、Payload、遷移条件、演算規則をまとめて定義する必要がある。

---

# 3. 研究目的

本研究の目的は、GPU上の線状物理要素に対して、状態に応じて内部表現と演算を変更できるデータ型を構築し、大量髪シミュレーションの計算量、GPUメモリ帯域、局所更新コストを削減することである。

具体的には、次を実現する。

1. Strand内部でStaticとDynamicを混在させる
2. Static Groupの完全物理計算を停止する
3. Static Groupでは最後に検証された結果を再利用する
4. Static状態ではDynamic時より軽量なPayloadへ変換する
5. Rootの平行移動と回転へ正しく追従する
6. 衝突危険が発生する前にDynamicへ復帰する
7. Static／Dynamic境界の不連続を抑える
8. 共有点への複数書き込みを防止する
9. 異常Groupだけを局所修復する
10. 毎フレームの全Group詳細分類を回避する
11. Follower側の不要な全再構築を削減する
12. Full Dynamicと同等の見た目を可能な限り維持する
13. 100,000本、128点、1080pで安定60FPSを目指す
14. B8 External State Array方式より明確な優位性を示す
15. Ablation Studyにより三つの中心技術の独立した効果を示す

---

# 4. 研究課題

本研究では次の研究課題を設定する。

## RQ1

Segment Groupの状態に応じてPayloadを変形させる方式は、外部State Array方式よりGPUメモリ帯域、VRAM、Cache効率を改善できるか。

## RQ2

誤差上限付きAdmissibility Certificateを用いることで、単純な速度閾値方式よりFalse Staticを削減できるか。

## RQ3

Deterministic Hemisphere Shared-Point Resolveを用いることで、親Group固定Owner方式や直接書き込み方式より境界誤差と非決定性を削減できるか。

## RQ4

Segment Group単位のStatic再利用によって、Full Dynamicに近い品質を維持しながらフレーム時間を削減できるか。

## RQ5

Guide状態とFollower Cacheを連動させることで、96,000本のFollowerを毎フレーム全再構築する方式より安定60FPSへ近づけるか。

---

# 5. 研究仮説

## H1：性能仮説

提案方式Pは、Static Group率が一定以上存在するシーンにおいて、B0 Full DynamicおよびB8 External State ArrayよりGPU Hair処理時間を短縮する。

## H2：安全性仮説

誤差上限付きCertificateを使用することで、単純閾値方式よりFalse Static率、境界誤差、貫通量を削減する。

## H3：データ型仮説

状態ごとにPayloadを変形する方式は、同一Structを全状態で保持する方式より、VRAM使用量またはGPUメモリ帯域を削減する。

## H4：共有点仮説

候補生成と決定的Reduceによる共有点解決は、複数Groupの直接書き込み方式より、実行順依存と境界不連続を削減する。

## H5：Follower仮説

Guide状態に連動したFollower Cacheは、全Follower再構築方式よりFollower Reconstruction時間を削減し、見た目の差を許容範囲内に維持する。

---

# 6. 新規性の中心

本研究の新規性は、次の三つを不可分な一つのデータ型として統合する点にある。

```text
State-dependent Payload Morphing
+
Error-bounded Admissibility Certificate
+
Deterministic Hemisphere Shared-Point Resolve
```

## 6.1 State-dependent Payload Morphing

同じSegment Groupでも、状態に応じて保持するPayload形式、参照先、許可演算を変える。

状態遷移時にはState値だけを変更するのではなく、PayloadのEncode、Decode、Migrationを実行する。

## 6.2 Error-bounded Admissibility Certificate

軽量表現を使用してよいかを、速度が小さいという条件だけで判断しない。

位置誤差、方向誤差、長さ誤差、境界誤差、衝突余裕、Root変換、編集履歴、親Group誤差、Cache経過時間を統合して判断する。

## 6.3 Deterministic Hemisphere Shared-Point Resolve

隣接Groupが共有する点へ直接書き込まず、各Groupが候補値を出力し、後段のResolve処理で最終値を決める。

半球責任、状態優先度、衝突状態、誤差上限、Group IDによるTie Breakを用い、実行順に依存しない結果を生成する。

---

# 7. 新規性として主張しない要素

次の要素は既存技術または実装基盤として扱い、新規性として単独では主張しない。

* 点列による髪表現
* Segment、edge、rod表現
* Guide／Follower方式
* PBD
* XPBD
* Cosserat Rod
* GPU Compute Shader
* Sleeping
* Cache reuse
* Active Set
* Dirty flag
* Error threshold
* GPU Compaction
* Indirect Dispatch
* LOD
* Repair
* Fallback
* Root剛体変換
* GPU multi-pass
* GPU常駐処理

本研究の新規性は、これらを単純に組み合わせたことではない。

> Segment Groupの状態に応じて内部物理表現と実行意味論を変化させ、その表現を使用できる条件と共有点整合性をデータ型自身が管理すること。

---

# 8. Segment Groupの幾何定義

## 8.1 基本単位

一本の髪を次の点列で表す。

```text
P0 ─ P1 ─ P2 ─ P3 ─ ... ─ P127
```

各Segment Groupは次のように定義する。

```text
G0 = (P0, E0, P1)
G1 = (P1, E1, P2)
G2 = (P2, E2, P3)
...
G126 = (P126, E126, P127)
```

各Groupは点―線―点を一つの局所単位とする。

## 8.2 等間隔条件

一本の髪の中では、原則として各GroupのRest Lengthを同一とする。

```text
RestLength(strand, group)
=
TotalRestLength(strand) / GroupCount(strand)
```

髪ごとに全長が異なることは許可する。

入力データの点間隔が不均一な場合は、実行前に等間隔Resamplingを行う。

## 8.3 Root固定

各Strandの最初の点`P0`は頭皮またはRoot Bindingに固定する。

Group `Gi`の開始点は、原則として一つ前のGroup `Gi-1`の解決済み終了点と一致する。

```text
Start(Gi) = ResolvedEnd(Gi - 1)
```

そのため、開始点を各Groupが独立に再計算することは禁止する。

---

# 9. Root移動と剛体追従

## 9.1 平行移動

頭部が平行移動した場合、Staticな髪形状にはすべての点へ同じ差分を適用する。

```text
p'i = pi + Δroot
```

Rootから毛先へ増加量の異なるXYZ値を加える方式は、髪の長さを変化させるため採用しない。

## 9.2 回転を含む変換

Root回転を含む場合は、Rootローカル座標を用いる。

```text
p'i = RootPositionNew
    + RootRotationNew
    × CachedLocalPositioni
```

または、旧Root変換と新Root変換の差分を用いる。

```text
p'i = Troot,new × inverse(Troot,old) × pi
```

## 9.3 Rootから毛先への伝播

本研究における「Rootから毛先へ順番に位置を確定する」とは、XYZ増加量を等差数列として加算することではない。

次を順番に伝播する。

* 解決済み開始点
* Root差分変換
* 親Groupの局所フレーム
* 親Groupの誤差上限
* 境界状態
* 衝突危険度

各Groupの開始点は親Groupの確定結果から取得し、終了点だけを状態別の演算によって生成する。

---

# 10. 状態定義

Segment Groupは次の状態を持つ。

```cpp
enum class SegmentGroupState : uint32_t
{
    StaticCached,
    StaticTransformed,
    Dynamic,
    DeferredCorrection,
    Repair,
    Fallback,
    Rejected
};
```

## 10.1 StaticCached

最後に検証された位置と局所形状をそのまま再利用する。

使用条件：

* Root変換なし、または無視できる
* 衝突危険なし
* 編集なし
* Cache有効
* 誤差上限内
* 親Group境界が有効

## 10.2 StaticTransformed

Cached Local ShapeへRoot差分変換または親Group変換を適用する。

完全物理計算は行わない。

## 10.3 Dynamic

積分、距離拘束、曲げ拘束、衝突、速度更新を行う。

簡易距離補正だけで完了させず、既存の実Solverへ接続する。

## 10.4 DeferredCorrection

直ちにFull Dynamicへ送る必要はないが、誤差が蓄積しているGroupを軽量補正する。

次の情報を保持する。

* 予測終点
* 累積誤差上限
* 補正期限
* 親境界誤差
* 予測速度

## 10.5 Repair

NaN、Inf、長さ破綻、境界破綻、深い貫通、Payload不整合が発生したGroupを局所修復する。

Repairを実行しただけでは成功扱いしない。

一定フレーム後に再検証し、成功、継続、Fallbackのいずれかを決定する。

## 10.6 Fallback

軽量表現や局所修復では安全性を維持できない場合、Float32 Full Dynamic表現へ戻す。

## 10.7 Rejected

Payload破損、Topology不整合、不正Indexなど、実行不能なGroupを隔離する。

描画時は最後の有効値または安全なRest Poseを使用する。

---

# 11. データ型構造

## 11.1 共通Header

```cpp
struct SegmentGroupHeader
{
    uint32_t strandId;
    uint32_t groupId;

    uint32_t parentGroupId;
    uint32_t childGroupId;

    uint32_t startPointIndex;
    uint32_t endPointIndex;

    uint32_t state;
    uint32_t previousState;

    uint32_t payloadType;
    uint32_t payloadIndex;

    uint32_t certificateIndex;
    uint32_t flags;

    uint32_t stateVersion;
    uint32_t payloadVersion;

    uint32_t lastSolvedFrame;
    uint32_t lastTransitionFrame;

    float restLength;
    float currentLength;

    float errorBoundMm;
    float boundaryErrorMm;
};
```

Headerは状態に関係なく固定する。

状態固有の大きな情報はHeaderへ直接保持しない。

---

## 11.2 Dynamic Payload

```cpp
struct DynamicPayload
{
    float3 currentStart;
    float3 currentEnd;

    float3 previousStart;
    float3 previousEnd;

    float3 velocityStart;
    float3 velocityEnd;

    float3 restDirection;
    float3 currentDirection;

    float distanceLambda;
    float bendLambda;

    float collisionRisk;
    float penetrationDepthMm;
};
```

用途：

* 積分
* 距離拘束
* 曲げ拘束
* 衝突
* 速度復元
* Repair前の保存

---

## 11.3 Static Cached Payload

```cpp
struct StaticCachedPayload
{
    uint32_t packedDirection;
    uint32_t packedLocalEnd;

    uint16_t quantizedLength;
    uint16_t quantizedWidth;

    uint16_t localFrameId;
    uint16_t cacheAge;

    uint32_t cacheVersion;
};
```

Static時はDynamic Payloadを保持し続けない。

最後に検証された局所形状を圧縮して保持する。

圧縮形式はFloat32版との比較を行い、品質Gateを満たす形式を採用する。

---

## 11.4 Static Transformed Payload

```cpp
struct StaticTransformedPayload
{
    uint32_t cachedPayloadIndex;
    uint32_t rootTransformVersion;

    float3 transformedEnd;
    float transformedErrorBoundMm;
};
```

---

## 11.5 Deferred Payload

```cpp
struct DeferredPayload
{
    float3 predictedEnd;
    float3 predictedVelocity;

    float accumulatedPositionErrorMm;
    float accumulatedDirectionErrorDeg;

    uint32_t correctionDeadlineFrame;
    uint32_t sourceStateVersion;
};
```

---

## 11.6 Repair Payload

```cpp
struct RepairPayload
{
    float3 lastValidStart;
    float3 lastValidEnd;

    float3 fallbackDirection;

    uint32_t reasonMask;
    uint32_t attemptFrame;
    uint32_t verifyAfterFrame;
    uint32_t attemptCount;
};
```

---

## 11.7 Payload Pool

状態ごとに独立したGPU Payload Poolを用意する。

```text
DynamicPayloadPool
StaticCachedPayloadPool
StaticTransformedPayloadPool
DeferredPayloadPool
RepairPayloadPool
FallbackPayloadPool
```

各Poolは次を持つ。

* Free List
* Recycle Queue
* Delayed Release Queue
* Generation Counter
* Capacity Counter
* Overflow Counter

状態遷移中に古いPayloadを即時再利用してはならない。

GPU実行中の参照競合を避けるため、一定フレーム遅延後に解放する。

---

# 12. Payload Migration

## 12.1 DynamicからStatic Cached

次を満たした場合に遷移する。

* 速度が閾値以下
* 長さ誤差が閾値以下
* 曲げ誤差が閾値以下
* 境界誤差が閾値以下
* 衝突余裕が十分
* 編集履歴が最新
* 親Certificateが有効
* 一定フレーム状態が安定
* Watchdog違反なし

処理：

1. Dynamic結果を検証
2. 局所座標へ変換
3. Static Payloadへ圧縮
4. Certificate生成
5. Static Poolへ確保
6. HeaderのPayload参照を変更
7. 古いDynamic PayloadをDelayed Releaseへ送る

## 12.2 StaticからDynamic

次の場合にWakeする。

* Rootまたは親Groupが大きく移動
* 衝突体が誤差上限内へ接近
* 風または外力が閾値を超える
* 編集された
* 親GroupがDynamicになった
* 境界誤差が増加
* Cache期限切れ
* Certificate無効
* Watchdog違反

処理：

1. Static PayloadをDecode
2. 現在位置と前フレーム位置を復元
3. 速度を復元
4. Dynamic Payloadを確保
5. Header参照を更新
6. Static PayloadをDelayed Releaseへ送る

---

# 13. Admissibility Certificate

## 13.1 定義

```cpp
struct AdmissibilityCertificate
{
    uint32_t sourceFrame;
    uint32_t validUntilFrame;

    uint32_t parentVersion;
    uint32_t rootTransformVersion;
    uint32_t colliderVersion;
    uint32_t editVersion;
    uint32_t payloadVersion;

    float maximumPositionErrorMm;
    float maximumDirectionErrorDeg;
    float maximumLengthErrorRatio;
    float maximumBoundaryErrorMm;

    float collisionClearanceMm;
    float predictedMotionMm;
    float accumulatedDeferredErrorMm;

    uint32_t validStateMask;
    uint32_t certificateHash;
};
```

## 13.2 有効条件

軽量表現の使用は次をすべて満たす場合のみ許可する。

```text
CurrentFrame <= ValidUntilFrame
AND
ParentVersion一致
AND
RootTransformVersion一致
AND
ColliderVersion一致
AND
EditVersion一致
AND
PayloadVersion一致
AND
PositionError <= PositionThreshold
AND
DirectionError <= DirectionThreshold
AND
LengthError <= LengthThreshold
AND
BoundaryError <= BoundaryThreshold
AND
CollisionClearance >
    ErrorBound + CollisionMargin
AND
CertificateHash一致
```

## 13.3 Unknownの扱い

安全性を優先する。

```text
Valid      → 軽量状態を継続可能
Invalid    → DynamicまたはRepair
Unknown    → DynamicまたはRepair
Corrupted  → FallbackまたはRejected
```

UnknownをStaticとして扱うことは禁止する。

## 13.4 Certificateの位置づけ

本研究のCertificateは、数学的に完全な正しさを証明するものではない。

位置、方向、長さ、衝突、境界、Version整合性をまとめたruntime validity descriptorである。

論文では「完全な証明」と表現せず、「誤差上限付き実行時許容性証明書」と定義する。

---

# 14. 衝突予測とPredictive Wake

Static Groupであっても、衝突体が近づいた場合は接触前にDynamicへ戻す。

## 14.1 Conservative Bounds

各Groupについて、Cached Segmentを覆うCapsuleまたはSwept AABBを保持する。

Boundsには次を加算する。

```text
Expansion
=
PositionErrorBound
+ BoundaryErrorBound
+ PredictedMotion
+ CollisionMargin
```

## 14.2 Wake条件

```text
Distance(Collider, ExpandedBounds) <= 0
```

または、

```text
CollisionClearance
<=
ErrorBound + PredictedMotion + CollisionMargin
```

の場合、Dynamicへ遷移する。

## 14.3 Wake伝播

一つのGroupがDynamicへ遷移した場合、隣接GroupへDirty理由を伝播する。

ただし、全Strandを即座にDynamic化しない。

最大伝播距離、誤差減衰、衝突方向を用いて局所範囲に限定する。

---

# 15. Hemisphere Shared-Point Resolve

## 15.1 問題

隣接Group `Gi-1`と`Gi`は共有点`Pi`を持つ。

両Groupが`Pi`へ直接書き込むと、GPU実行順、Thread Group順、Atomic順によって結果が変化する。

## 15.2 基本方針

各GroupはPoint Bufferへ直接書き込まない。

次の候補を`GroupResultBuffer`へ出力する。

```cpp
struct SharedPointCandidate
{
    uint32_t pointIndex;
    uint32_t sourceGroupId;

    uint32_t state;
    uint32_t priority;

    float3 candidatePosition;
    float candidateErrorMm;

    float penetrationDepthMm;
    float boundaryResidualMm;

    uint32_t hemisphereRole;
    uint32_t reasonMask;
};
```

## 15.3 半球責任

共有点を、Rest Tangentまたは局所接続フレームを基準に二つの論理半球へ分ける。

* Parent Groupは流入側半球を担当
* Child Groupは流出側半球を担当
* Collision Candidateは接触法線側の修正責任を持つ
* Edit Candidateは編集対象側の責任を持つ

半球責任はPointを物理的に二つへ分割することではない。

各Groupがどの方向の変形に責任を持つかを示す論理的なOwnershipである。

## 15.4 優先順位

最終候補は次の優先順位で決定する。

```text
Fatal / Rejected
>
Penetration Repair
>
Collision
>
Direct Edit
>
Forced Dynamic
>
Dynamic
>
Deferred Correction
>
Static Transformed
>
Static Cached
```

`Force Static`がCollision、Edit、Repairを上書きすることは禁止する。

## 15.5 Tie Break

同一優先度の場合は、次の順番で決定する。

1. Penetrationが小さい候補
2. Boundary Residualが小さい候補
3. Error Boundが小さい候補
4. Certificateが新しい候補
5. Group IDが小さい候補

これにより、同一入力に対して決定的な結果を生成する。

## 15.6 Apply Pass

Resolve後、`ApplyGroupResultCS`だけが最終Point Bufferへ書き込む。

複数Groupから同一点への直接書き込みを禁止する。

---

# 16. Dynamic Group Solver

Dynamic Groupは簡易距離補正だけで完了させない。

既存HairSystemの実Solverへ接続する。

最低限、次を実行する。

* VerletまたはSemi-Implicit積分
* Root拘束
* 距離拘束
* Angular Bend拘束
* 必要に応じたねじれ近似
* 頭部・身体との衝突
* 自己衝突
* 速度更新
* Stability Guard

Segment Group単位で実行する場合でも、隣接Groupとの曲げ拘束に必要な前後点を参照する。

直接Point Bufferへ書かず、候補結果を出力する。

---

# 17. Static／Dynamic境界補正

Static GroupとDynamic Groupの境界では、位置だけでなく方向、速度、曲率を考慮する。

境界補正は次の誤差を評価する。

* 共有点位置誤差
* 接線方向誤差
* セグメント長誤差
* 速度不連続
* 曲率不連続

境界補正の優先順位は次とする。

```text
衝突安全性
>
長さ維持
>
共有点位置
>
速度連続性
>
接線連続性
>
見た目の平滑性
```

境界誤差が閾値を超えた場合、隣接Static GroupもDeferredまたはDynamicへ昇格する。

---

# 18. 速度復元

StaticからDynamicへ戻る際、前フレーム位置を現在位置と同じにすると速度が0になり、不自然に停止する。

そのため、次の情報から速度を復元する。

* Rootの平行移動速度
* Rootの角速度
* Parent Groupの速度
* Static化直前の局所速度
* Deferred Payloadの予測速度
* 外力履歴

```text
PreviousPosition
=
CurrentPosition - RecoveredVelocity × Δt
```

復元速度には上限を設け、異常な発散を防止する。

---

# 19. Follower Cache

## 19.1 現行方式の問題

96,000本×128点、合計12,288,000 Follower点を毎フレーム全再構築すると、Guide側の物理計算を削減してもFollower処理が支配的になる。

## 19.2 Guide連動状態

Followerは親GuideのGroup状態に連動して更新する。

```text
Guide Static Cached
→ Follower Cache reuse

Guide Static Transformed
→ Follower Cached Local Shapeへ同じRoot差分変換

Guide Dynamic
→ 対応するFollower範囲だけ再構築

Guide Repair
→ 最終有効Follower Cacheまたは安全Fallback
```

## 19.3 Follower局所更新

Guide Group `Gi`がDynamicの場合、対応するFollowerの全128点ではなく、該当Group付近だけを更新する。

境界平滑化のため、前後1～2 Groupを追加更新する。

## 19.4 Follower Cache Payload

Followerごとに全点のFloat32位置を保持するのではなく、必要に応じて次を保持する。

* Guide-relative local offset
* clump係数
* noise係数
* 前回の局所速度
* Cache Version
* 有効Group範囲

## 19.5 Follower品質確認

Follower Cache導入前後で次を比較する。

* 位置RMSE
* Silhouette差
* Root coverage
* Tip spread
* Frame-to-frame flicker
* Collision penetration
* GPU reconstruction time

---

# 20. GPU実行パイプライン

完成版の1フレーム処理順を次とする。

## Phase 1：Event Capture

* Root Transform更新
* Collider Version更新
* Edit Version更新
* Wind／Force更新
* Dirty Event生成

## Phase 2：Block Classification

Groupを8～16個のBlockへまとめ、Block単位で詳細評価の必要性を判定する。

完全にStaticで有効なBlockはGroup詳細分類を省略する。

## Phase 3：Certificate Query

Dirty Block内のGroupだけCertificateを評価する。

## Phase 4：State Transition

Static、Dynamic、Deferred、Repair、Fallbackへの遷移を決定する。

## Phase 5：Payload Migration

状態遷移が発生したGroupのPayloadをEncode、Decode、Migrationする。

## Phase 6：Stable Compaction

状態別Group Listを生成する。

```text
StaticCachedList
StaticTransformedList
DynamicList
DeferredList
RepairList
FallbackList
```

## Phase 7：Indirect Args生成

各Listの要素数からDispatchIndirect引数を生成する。

## Phase 8：Static Transform

Root差分変換と親境界変換を適用する。

## Phase 9：Dynamic Solve

Dynamic Groupだけ実Solverへ送る。

## Phase 10：Collision and Predictive Wake

現在のDynamic Groupと、接触前のStatic Group候補を処理する。

## Phase 11：Candidate Generation

各Groupが共有点候補を出力する。

## Phase 12：Shared-Point Resolve

優先度、半球責任、誤差、Tie Breakにより最終点を決定する。

## Phase 13：Boundary Correction

Static／Dynamic境界を補正する。

## Phase 14：Velocity Recovery

WakeしたGroupの前フレーム位置を復元する。

## Phase 15：Follower Update

Guide状態に応じてFollower Cache reuse、Transform、局所再構築を行う。

## Phase 16：Repair Verification

Repair結果を一定フレーム後に再検証する。

## Phase 17：Render Buffer Build

最後に確定した位置から描画用データを生成する。

## Phase 18：Telemetry

GPU Timestamp、状態数、誤差、安全性、Memory情報を遅延Readbackする。

---

# 21. 毎フレーム全Group分類の回避

508,000 Groupを毎フレーム詳細分類すると、Static率が高くても分類処理が大きな負荷になる。

次の階層化を使用する。

```text
Strand
└─ Block
   └─ Segment Group
```

Block Certificateが有効で、Block内にDirty Eventがない場合は、Group詳細分類を省略する。

詳細分類を実行する条件：

* Block Dirty
* Root Version不一致
* Collider Version不一致
* Edit Version不一致
* Certificate期限切れ
* Watchdog対象Frame
* 親BlockがDynamic
* 境界誤差増加

Watchdogは全Groupを毎フレーム確認せず、分割して周期的に確認する。

---

# 22. GPUメモリ設計

## 22.1 Structure of Arrays

頻繁に読む情報はSoAへ分離する。

```text
GroupStateBuffer
GroupPayloadTypeBuffer
GroupPayloadIndexBuffer
GroupErrorBoundBuffer
GroupVersionBuffer
GroupTopologyBuffer
```

大きなPayloadをHeaderへ埋め込まない。

## 22.2 Alignment

StructuredBuffer要素は16Byte境界を基本とする。

## 22.3 Static Payload圧縮

Static Payloadの候補形式を比較する。

* Float32 local vector
* Float16 local vector
* Octahedral direction＋quantized length
* Quaternion frame＋length
* Endpoint delta compression

品質Gateを満たす最小形式を採用する。

## 22.4 目標

* プロセスRAM：32GB以下
* 目標RAM：1GB前後
* GTX 1660級VRAM上限：6GB程度
* 不要な位置Buffer複製を削減
* 同一内容のCPU／GPU二重保持を削減
* 同一フレームReadbackを禁止

---

# 23. 比較手法

正式比較では次の手法を使用する。

| ID | 手法                           |
| -- | ---------------------------- |
| B0 | Full Dynamic                 |
| B1 | Strand-level Sleep           |
| B2 | Block-level Sleep            |
| B3 | Segment Threshold Sleep      |
| B4 | Segment Hysteresis Sleep     |
| B5 | Segment Cache Reuse          |
| B6 | Segment Cache＋GPU Compaction |
| B7 | Active Set Only              |
| B8 | External State Array         |
| P  | Integrated CMSG Data Type    |

---

# 24. B8 External State Array比較

## 24.1 B8の定義

B8では、Segment Group本体とは別にState配列を持つ。

```text
SegmentGroupGeometryBuffer
SegmentGroupPhysicsBuffer
ExternalStateBuffer
CertificateBuffer
```

状態値は外部配列に保存するが、Group本体のPayload形式は状態によって変化させない。

## 24.2 Pとの違い

Pでは、State、Payload Type、Payload Index、Certificate参照をGroup Headerへ統合し、状態遷移時にPayload Poolを切り替える。

```text
B8
Stateだけ外部管理
Payloadは原則固定

P
StateとPayload参照を型へ統合
状態に応じてPayload形式と許可演算を変更
```

## 24.3 公平条件

B8とPでは次を完全に一致させる。

* Asset
* Seed
* Scenario
* Root Motion
* Collider Motion
* Solver
* Iteration数
* Threshold
* Wake条件
* Repair条件
* Render条件
* Measurement Frame
* Hardware
* Shader最適化設定

異なるのは状態管理とPayload表現だけとする。

## 24.4 B8に対して示すべき優位性

次のうち少なくとも一つで統計的に明確な改善を示す。

* GPU Hair処理時間
* GPUメモリ帯域
* VRAM使用量
* Cache効率
* 状態／Payload不整合数
* Boundary Error
* False Static率
* Repair率
* p95／p99 Frame Time

性能が同等でも、状態不整合、境界安全性、VRAMで明確な改善があれば研究上の差分として扱う。

---

# 25. Ground Truth

Ground Truthを一種類だけに限定しない。

## 25.1 GT-RT：Runtime Ground Truth

同一の100,000本、128点、6反復条件で、すべてのGuide GroupをDynamicとして処理するB0 Full Dynamicを使用する。

Pの性能とリアルタイム品質比較に用いる。

## 25.2 GT-HQ：High Quality Ground Truth

品質評価用に、対象範囲を限定した高反復Solverを使用する。

例：

* 24反復
* 32反復
* 小規模Strand subset
* 高精度衝突
* 高精度Rod Solver

GT-HQは60FPS性能比較には使用せず、物理品質と誤差閾値の校正に使用する。

## 25.3 Shadow Dynamic Validation

Staticと判定したGroupの一部を、描画結果には反映せず裏側でDynamic計算する。

Static再利用結果とShadow Dynamic結果の差を測定する。

```text
False Static
=
Static継続と判定されたが、
Ground TruthではDynamicまたはRepairが必要だったGroup
```

## 25.4 Ground Truth比較値

* Endpoint Position Error
* Direction Error
* Length Error
* Boundary Error
* Penetration Depth
* Velocity Error
* Curvature Error
* Follower Position Error
* Silhouette Error

---

# 26. Ablation Study

三つの中心技術が単なる付加機能ではなく、性能、安全性、品質へ独立した効果を持つことを示す。

| ID  | 除去・変更内容                | 確認する効果            |
| --- | ---------------------- | ----------------- |
| A1  | Payload Morphingなし     | VRAM、帯域、Cache効率   |
| A2  | 外部State Array化         | PとB8の差            |
| A3  | Certificateなし          | False Static、境界誤差 |
| A4  | Certificateをbool集合へ簡略化 | 誤差上限の必要性          |
| A5  | Collision Clearanceなし  | 接触前Wake           |
| A6  | Hemisphere Resolveなし   | 境界誤差、非決定性         |
| A7  | Parent Group常時優先       | 衝突、編集時の破綻         |
| A8  | Predictive Wakeなし      | Wake遅延、貫通         |
| A9  | Velocity Recoveryなし    | Wake時の跳躍          |
| A10 | Follower Cacheなし       | Follower負荷        |
| A11 | Watchdogなし             | 長時間False Static   |
| A12 | Repair再検証なし            | Repair成功率の誤判定     |

## 26.1 成立条件

次を示す必要がある。

* Payload Morphingを外すとVRAM、帯域、性能のいずれかが悪化
* Certificateを外すとFalse Staticまたは品質が悪化
* Hemisphere Resolveを外すと境界誤差または決定性が悪化
* Follower Cacheを外すとFollower再構築時間が悪化
* 全要素を有効にしたPが総合的に最も良いPareto点となる

---

# 27. 評価Scenario

正式評価では最低18Scenarioを使用する。

| ID  | Scenario                  |
| --- | ------------------------- |
| S01 | 完全静止                      |
| S02 | Root平行移動                  |
| S03 | Root低速回転                  |
| S04 | Root高速回転                  |
| S05 | 周期的Head Motion            |
| S06 | 弱風                        |
| S07 | 強風                        |
| S08 | 瞬間Impulse                 |
| S09 | 単一Collider接近              |
| S10 | 高速Collider侵入              |
| S11 | 複数Collider接触              |
| S12 | 局所編集                      |
| S13 | 連続編集                      |
| S14 | Static／Dynamic混在          |
| S15 | Dynamic率80%以上             |
| S16 | 全動的                       |
| S17 | State Thrashing           |
| S18 | Payload破損・Repair・Fallback |

追加長時間Scenario：

* Walk Motion
* Strong Wind
* Repeated Collision
* State Thrashing
* Edit and Undo
* Root Rotation Loop

---

# 28. 性能評価指標

## 28.1 Primary Performance Metrics

* Average FPS
* Effective FPS
* 1% Low FPS
* p95 Frame Time
* p99 Frame Time
* Maximum Frame Time
* 60FPS以上のFrame比率
* Frame Budget Overrun Rate
* 100ms以上の停止回数

## 28.2 GPU Pass Metrics

* Block Classification時間
* Group Classification時間
* Certificate時間
* Payload Migration時間
* Compaction時間
* Static Transform時間
* Dynamic Solve時間
* Collision時間
* Shared-Point Resolve時間
* Boundary Repair時間
* Follower Reconstruction時間
* Render Preparation時間
* Hair Draw時間

## 28.3 Memory Metrics

* Process Private Memory
* VRAM Local Usage
* Dynamic Payload Pool使用量
* Static Payload Pool使用量
* Peak Payload Migration量
* Pool Overflow数
* GPU Buffer Copy量

---

# 29. 安全性・品質指標

## 29.1 False Static

```text
FalseStaticRate
=
FalseStaticGroupCount
/
StaticDecisionGroupCount
```

目標：

```text
FalseStaticRate <= 0.1%
```

重大衝突を見逃したFalse Staticは0件を目標とする。

## 29.2 Boundary Error

目標：

```text
p95 Boundary Error <= 1.0 mm
p99 Boundary Error <= 2.0 mm
Maximum Boundary Error <= 5.0 mm
```

## 29.3 Length Error

目標：

```text
p99 Length Error Ratio <= 1.0%
Maximum Length Error Ratio <= 2.0%
```

## 29.4 Penetration

目標：

```text
p99 Penetration Depth <= 2.0 mm
Maximum Penetration Depth <= 5.0 mm
Catastrophic Penetration = 0
```

## 29.5 Wake Latency

```text
Collision Wake Latency <= 1 Frame
Neighbor Wake Latency <= 2 Frames
Edit Wake Latency <= 1 Frame
```

## 29.6 異常値

次は0件必須とする。

* NaN
* Inf
* Invalid Index
* Payload Type不整合
* Payload Generation不一致
* Shared Point多重Commit
* Unrecoverable Buffer Overflow
* Formal Run中のCPU Full Group Upload

---

# 30. 60FPS正式Gate

100,000本、128点、1920×1080、6反復、Release x64、VSync OFFで次を満たす。

```text
Average FPS >= 60.0
60FPS以上のFrame比率 >= 95%
Frame Budget Overrun Rate <= 5%
p95 Frame Time <= 16.6667 ms
p99 Frame Time <= 20.0 ms
Maximum Frame Time <= 50.0 ms
100 ms超過Frame = 0
```

平均FPSだけで達成と判定しない。

GTX 1660級での達成は、実機測定に成功した場合のみ主張する。

RTX 4070 Ti SUPERからの換算値は予測として分離し、正式証拠として扱わない。

---

# 31. 決定性評価

## 31.1 Decision Determinism

同一Asset、Seed、Scenario、Commit、Hardwareで、次が完全一致することを要求する。

* Group State列
* Payload Type列
* Compacted Group順
* Shared Point Owner
* Repair理由
* Fallback理由
* Transition Frame

## 31.2 Geometry Determinism

Float演算の完全Bit一致が難しい環境では、位置差の許容値を定義する。

```text
Same GPU / Same Driver
→ 原則Bitwise一致を目標

Different GPU
→ Position Difference <= Defined Tolerance
```

## 31.3 CPU／GPU判定一致

同じ入力Snapshotに対して、CPU Reference QueryとGPU Queryの状態判定が一致することを確認する。

目標：

```text
CPU/GPU Decision Match Rate = 100%
```

---

# 32. 長時間試験

| Test | 内容              |  時間 |
| ---- | --------------- | --: |
| L30  | Walk＋通常衝突       | 30分 |
| L2H  | Strong Wind     | 2時間 |
| L8H  | State Thrashing | 8時間 |

評価項目：

* RAM slope
* VRAM slope
* p99 Frame Time劣化
* Payload Pool Fragmentation
* Repair増加率
* False Static増加率
* Unresolved Query
* State Oscillation
* Cache Version Overflow
* Payload Generation Overflow

目標：

```text
VRAM slope <= 1 MB/min
RAM slope <= 2 MB/min
p99劣化率 <= 1.10
Unresolved Query slope = 0
NaN / Inf = 0
```

---

# 33. 統計処理

各手法、Scenarioについて最低5 Seedを使用する。

通常構成：

```text
10手法 × 18 Scenario × 5 Seed
= 900 Run
```

各Run：

* Warm-up 600 Frame以上
* Measurement 6,000 Frame以上
* GPU Timing Coverage 99%以上
* CSV／JSONL同時出力
* 実行Fingerprint保存

比較では次を使用する。

* 平均
* 中央値
* 標準偏差
* 95%信頼区間
* 効果量
* Scenario別分布
* Worst-case
* Pareto比較

平均値だけで結論を出さない。

---

# 34. 新規性成立Gate

## Gate 1：真正なPayload変形

状態遷移時にPayload形式と参照先が実際に変わる。

State Flagだけを変更する方式は不合格。

## Gate 2：型固有演算

状態ごとに許可演算が異なる。

すべての状態で同一Solverを実行する方式は不合格。

## Gate 3：誤差上限

Certificateがbool集合ではなく、測定可能な誤差上限を持つ。

## Gate 4：共有点解決

候補結果、半球責任、優先順位、決定的Tie Breakを実装する。

## Gate 5：B8との差

B8 External State Arrayに対して、性能、VRAM、帯域、安全性、状態整合性のいずれかで明確な改善を示す。

## Gate 6：Ground Truth

Full DynamicおよびShadow Dynamicを用いてFalse Staticと誤差を測定する。

## Gate 7：Ablation

三つの中心技術を外すと、性能、安全性、品質のいずれかが悪化することを示す。

## Gate 8：Follower

Follower Cacheまたは局所再構築が実装され、全再構築方式との比較が存在する。

## Gate 9：決定性

Shared Point、Compaction、State Decisionの再現性を示す。

## Gate 10：実測

実測なしで60FPSや安全性を主張しない。

---

# 35. 新規性を弱くする実装

次の場合、最終評価は「危ない」のままとする。

* StateをStructへ追加しただけ
* Static時もDynamic Payloadを保持する
* Certificateがbool集合だけ
* 誤差上限を計算しない
* Static Transformが平行移動だけ
* Root回転に対応しない
* Shared Pointへ複数Groupが直接書き込む
* Ownerが常にParent固定
* Dynamic Groupが簡易距離補正だけ
* Followerを毎フレーム全再構築する
* B8と比較しない
* Full DynamicをGround Truthにしない
* False Staticを測らない
* Repairを実行しただけで成功扱いする
* 全動的Scenarioを評価しない
* Ablationを行わない
* 実測なしで60FPSを主張する
* 最新先行研究との最終照合を行わない

---

# 36. 実装対象ファイル

## 36.1 `include/hairgpu/Types.h`

追加・統合：

* `SegmentGroupState`
* `SegmentGroupPayloadType`
* `SegmentGroupHeader`
* 各状態Payload
* `AdmissibilityCertificate`
* `SharedPointCandidate`
* `SharedPointResolvedResult`
* `PayloadPoolHandle`
* `PayloadGeneration`
* Telemetry構造

## 36.2 `apps/dx11_viewer/src/GpuHairViewer.h`

追加・修正：

* 状態別Payload Buffer
* Free List
* Recycle Queue
* Delayed Release Queue
* Candidate Buffer
* Resolve Buffer
* Stable Compaction Buffer
* Follower Cache Buffer
* DispatchIndirect Args
* Formal Mode設定
* CPU Reference Query
* GPU Timestamp Query

## 36.3 `GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl`

追加・修正：

* 状態別Buffer生成
* Pool初期化
* Candidate Buffer生成
* Shared Resolve Shader読込
* Follower Cache生成
* Initial Payload Encode
* Resource Capacity検証
* HLSL Self-test

## 36.4 `GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl`

追加・修正：

* Payload Migration
* Dirty Block分類
* Certificate Query
* State Transition
* Stable Compaction
* Static Transform
* Dynamic Solver接続
* Candidate Generation
* Shared-Point Resolve
* Boundary Correction
* Velocity Recovery
* Follower局所更新
* Watchdog
* Repair再検証
* 重複Pass除去

`EvaluateMicrotypeCertificates()`と提案法Pの同系Passが同一Frameで二重実行されないように、Frame Pipelineを一本化する。

## 36.5 `GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl`

追加・修正：

* Follower Cache描画
* Guide状態に応じたFollower更新
* Repair時の描画Fallback
* Last Valid Pose
* Rejected Group非表示ではなく安全Pose使用
* Follower局所再構築

## 36.6 `GpuHairViewer_parts/07_draw_path_and_gpu_query.inl`

追加・修正：

* 状態別GPU Timing
* Shared Resolve Timing
* Follower Reconstruction Timing
* Delayed Query
* 同一Frame待機禁止

## 36.7 `GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl`

追加・修正：

* Payload Migration数
* False Static数
* Boundary Error
* Certificate Failure
* Shared Owner Conflict
* B8比較情報
* Ground Truth誤差
* Ablation ID
* Follower Cache Hit率
* Repair再検証結果

## 36.8 `apps/dx11_viewer/shaders/HairSegmentGroupRuntime.hlsl`

実装：

* Block Certificate
* Group Certificate
* State Transition
* Payload Encode／Decode
* Payload Migration
* Stable Compaction
* Static Rigid Transform
* Dynamic Solver Adapter
* Predictive Wake
* Shared Candidate Generation
* Hemisphere Resolve
* Boundary Correction
* Repair Verification
* Watchdog
* Telemetry Reduction

## 36.9 `apps/dx11_viewer/shaders/HairSim.hlsl`

修正：

* Dynamic Group List対応
* Group単位Solver Dispatch
* Follower局所更新
* Follower Cache reuse
* Candidate Buffer出力
* Point Buffer直接競合の排除

## 36.10 `tools/final_stage/hairgpu_final_run.py`

追加・修正：

* B0～B8、P
* A1～A12
* Ground Truth Run
* Shadow Validation
* Gate判定
* 実行Fingerprint
* Evidence Validity
* Resume整合性

## 36.11 `tools/final_stage/hairgpu_runtime_suite.py`

追加・修正：

* 900 Run Matrix
* 長時間試験
* GTX 1660 Hardware Gate
* Determinism Replay
* Ablation集計
* B8／P統計比較

---

# 37. 実装段階

## Phase 0：Pre-Novelty固定

* Mainの基準Commit固定
* Frozen Asset生成
* B0、B6、B8のBaseline取得
* 100,000本、128点を固定
* Release x64測定
* 現行Follower全再構築時間を保存

## Phase 1：型定義

* Header／Payload分離
* Payload Type追加
* Pool Handle追加
* Generation検証
* CPU Unit Test

## Phase 2：Payload Pool

* 状態別Pool
* Free List
* Delayed Release
* Overflow処理
* Migration Test

## Phase 3：Certificate

* 誤差上限
* Version整合性
* Collision Clearance
* CPU／GPU判定一致

## Phase 4：Shared-Point Resolve

* Candidate Buffer
* Hemisphere Role
* Priority Reduce
* Tie Break
* Direct Write禁止

## Phase 5：Solver統合

* Dynamic Groupを既存実Solverへ接続
* Boundary Correction
* Velocity Recovery
* Repair再検証

## Phase 6：Follower Cache

* Guide状態連動
* Static reuse
* Static transformed
* 局所再構築
* Follower品質比較

## Phase 7：計測

* GPU Timing
* False Static
* Ground Truth
* Boundary Error
* Memory
* Long-run

## Phase 8：比較実験

* B0～B8、P
* A1～A12
* S01～S18
* 5 Seed
* 900 Run以上

## Phase 9：論文用分析

* 主要結果表
* Scenario別グラフ
* Ablation表
* B8比較
* Ground Truth品質
* 制限事項
* 再現手順

---

# 38. 研究上のリスク

## Risk 1：全動的シーンで遅い

分類、Certificate、Compactionの追加分だけB0より遅くなる可能性がある。

対策：

* Block-level skip
* Dirty Groupだけ詳細分類
* 全動的検出時のFast Path
* Certificate簡易Path
* 不要なStatic Pass停止

## Risk 2：Static率が低い

髪型やScenarioによってはStatic Groupが少なく、効果が出ない。

対策：

* StaticだけでなくDeferredを活用
* Root-relative transformed状態を導入
* Follower Cacheを独立して適用
* Scenario別に適用範囲を明示

## Risk 3：Payload Migrationが高コスト

状態が頻繁に変わるとMigrationが支配的になる。

対策：

* Hysteresis
* Minimum State Duration
* Delayed Transition
* Block Transition
* Thrashing検出
* Pool予約

## Risk 4：半球責任の効果が弱い

単純Owner方式との差が出ない可能性がある。

対策：

* Collision境界
* Edit境界
* Dynamic／Static境界
* 高曲率境界
* 多数接触Scenarioで比較する

## Risk 5：60FPS未達

Novelty 01で物理を削減しても描画とFollowerが支配的になる可能性がある。

対策：

* Follower局所更新
* Static Follower Cache
* Render Preparation削減
* OIT負荷測定
* 描画LODは別の既存最適化として併用
* 未達の場合も正しい実測結果として保存する

---

# 39. 論文での主張範囲

## 主張する内容

1. 状態に応じて内部Payloadと許可演算を変更するSegment Group型
2. 誤差上限付きCertificateによる軽量表現の使用可否判断
3. Hemisphere責任と決定的Reduceによる共有点解決
4. Strand内部で複数の実行状態を安全に共存させること
5. B8 External State Arrayとの差
6. Full Dynamic Ground Truthに対する品質と安全性
7. Ablationによる三要素の独立した効果

## 主張しない内容

* Segment表現そのものの発明
* Sleepingの発明
* Cache reuseの発明
* PBD／XPBDの発明
* Guide／Followerの発明
* GPU Compactionの発明
* すべてのGPU、すべての髪型で60FPSになるという保証
* 数学的に完全な衝突安全性
* 世界初であることの無条件保証

---

# 40. 最終新規性定義

本研究の新規性を次のように定義する。

> 本研究は、線状物理要素を、状態に応じて内部物理表現、GPUメモリ配置、Payload、許可演算、更新頻度を変化させるMorphing Segment-Group Data Typeとして定義する。さらに、誤差上限付きAdmissibility Certificateによる状態遷移管理と、決定的Hemisphere Shared-Point Resolveを統合することで、同一ストランド内部に静的再利用、剛体追従、動的物理、延期補正、局所修復、Fallbackを安全に共存させる。

簡潔には次のように表す。

> 計算を止めるだけではなく、状態に応じて物理データ表現そのものを変形し、その変形の許容性と共有点の整合性を型自身が管理するGPU線状物理データ型である。

---

# 41. 研究上の貢献

## 貢献1：Morphing Segment-Group Data Type

線状物理要素を、状態に応じて内部表現、Payload、メモリ配置、許可演算を変更する可変データ型として定義する。

## 貢献2：Error-bounded Admissibility Certificate

Version整合性、位置誤差、方向誤差、長さ誤差、境界誤差、衝突余裕を統合し、軽量表現を使用できるか判断する。

## 貢献3：Deterministic Hemisphere Shared-Point Resolve

同一Strand内部で異なる状態を混在させるため、半球責任、優先順位、誤差、決定的Tie Breakによって共有点を確定する。

## 貢献4：安全なStatic Reuse評価

Cache reuse率だけでなく、False Static率、境界誤差、Wake遅延、Repair再検証結果を同時に評価する。

## 貢献5：Guide連動Follower Cache

Guide Groupの状態をFollower更新範囲へ接続し、96,000本のFollower全再構築を削減する。

---

# 42. 完成判定チェックリスト

## 研究設計

* [ ] 中心新規性が三要素で固定されている
* [ ] 既存要素を新規性として主張していない
* [ ] B8比較が定義されている
* [ ] Ground Truthが定義されている
* [ ] Ablationが定義されている
* [ ] 最新先行研究との最終照合を行った

## データ型

* [ ] StateごとにPayloadが異なる
* [ ] HeaderとPayloadが分離されている
* [ ] Payload Type／State不整合を検出できる
* [ ] Payload Migrationが存在する
* [ ] Delayed Releaseが存在する
* [ ] Pool Overflowを安全に処理できる

## 安全性

* [ ] Certificateが誤差上限を持つ
* [ ] UnknownはDynamicまたはRepairになる
* [ ] CollisionがStaticより優先される
* [ ] Shared Point Ownerが一意である
* [ ] Repair後に再検証される
* [ ] False Staticを測定できる
* [ ] Ground Truth比較が実行できる

## 物理

* [ ] Dynamic Groupが実Solverへ接続される
* [ ] Static Transformが回転へ対応する
* [ ] Rootから毛先へ接続を維持できる
* [ ] Wake時の速度を復元できる
* [ ] Static／Dynamic境界を補正できる
* [ ] 衝突前Wakeが機能する

## GPU

* [ ] 状態別Listが生成される
* [ ] Indirect Dispatchを使用する
* [ ] Stable Compactionが存在する
* [ ] Block Certificateが存在する
* [ ] 同一Frame Readbackがない
* [ ] Candidate／Resolve方式で共有点を処理する
* [ ] 同系Passが同一Frameで重複しない

## Follower

* [ ] Guide状態に応じて更新方法が変わる
* [ ] Static Cacheを再利用できる
* [ ] Static Transformedに対応する
* [ ] 局所再構築できる
* [ ] 全再構築時間を比較できる
* [ ] 見た目の差をGround Truthと比較できる

## 評価

* [ ] S01～S18を実行できる
* [ ] B0～B8、Pを実行できる
* [ ] A1～A12を実行できる
* [ ] 5 Seed以上で評価する
* [ ] GTX 1660級実機で測定する
* [ ] 30分／2時間／8時間試験を行う
* [ ] 決定性を検証する
* [ ] Formal Resultを自動生成する
* [ ] Evidence ValidityとPerformance Gateを分離する

---

# 43. 最終判定条件

次の構成だけでは、新規性評価は「危ない」のままである。

```text
Segment Group
+
State Flag
+
Cache
+
Repair
```

次の構成をすべて完成実装し、比較実験で独立した効果を示した場合、新規性評価を「安全」とする。

```text
State-dependent Payload Morphing
+
Error-bounded Admissibility Certificate
+
Deterministic Hemisphere Shared-Point Resolve
+
B8 External State Array Comparison
+
Full Dynamic Ground Truth
+
Shadow Dynamic Validation
+
Ablation Study
+
Follower Cache / Local Reconstruction
+
Determinism Evaluation
+
Formal 100,000-Strand Measurement
```

世界初を無条件に保証するものではないが、既存のSegment表現、Sleeping、Cache reuse、Active Setをそのまま適用した方式とは異なる研究として成立する。

特にB8との比較によって、単に状態を別配列へ追加した方式ではなく、データ型内部のPayloadと実行意味論を状態に応じて変化させることの効果を示す。

---

# 44. 結論

CMSG Data Typeは、髪の一部分をStaticにするだけの最適化ではない。

Segment Groupの状態に応じて、保持する物理表現、GPU上のPayload、メモリ配置、実行可能な演算、安全条件、更新頻度を変更する。

Static Groupでは最後に検証された局所形状を再利用し、Root変換が発生した場合は剛体変換によって追従する。衝突、編集、境界誤差、Cache期限切れが発生した場合は、誤差上限付きCertificateによってDynamic、Deferred、Repair、Fallbackへ移行する。

隣接Groupが共有する点については、各Groupが候補値を生成し、半球責任、状態優先度、誤差、衝突状態、決定的Tie Breakによって最終値を確定する。

さらに、Guide Groupの状態を96,000本のFollower更新へ接続し、Static Follower Cache、剛体追従、局所再構築を使い分ける。

B8 External State Array、B0 Full Dynamic、各種Sleeping／Cache方式との比較、Full Dynamic Ground Truth、Shadow Dynamic Validation、Ablation Studyを実施することで、本研究を単なる既存技術の組み合わせではなく、GPU線状物理要素のデータ表現と実行意味論を再定義する研究として成立させる。
