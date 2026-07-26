# Novelty 01

# Segment-Group Adaptive Payload Hair Representation

## 経験的検証付きAdmissibility CertificateによるGPU常駐型局所更新髪シミュレーション

---

# 0. 文書情報

| 項目           | 内容                            |
| ------------ | ----------------------------- |
| 対象リポジトリ      | `MKyosei2/HairSystem`         |
| 対象ブランチ       | `main`                        |
| 基準バージョン      | Version 65                    |
| 文書種別         | 新規性の企画書兼設計書                   |
| 新規性番号        | Novelty 01                    |
| 対象方式         | GPU常駐型Guide/Follower髪シミュレーション |
| 表示目標         | 100,000 strands               |
| Guide本数      | 4,000 strands                 |
| Follower本数   | 96,000 strands                |
| 点数           | 128 points / strand           |
| Guide Group数 | 4,000 × 127 = 508,000 groups  |
| 表示点数         | 12,800,000 points             |
| 画面解像度        | Full HD                       |
| 時間刻み         | 1/60秒固定                       |
| Solver反復数    | 6回                            |
| GTX 1660級評価  | 理論換算のみ。実機検証は今後の展望             |
| 現在の実装状態      | 基盤・試作段階。正式方式は未完成              |

---

# 1. 企画概要

本研究では、一本の髪を複数のSegment Groupに分割し、各Groupを局所的な実行単位として扱う。

各Segment Groupは、状態に応じて次のいずれかとして処理される。

* 通常の物理計算を行うDynamic Group
* 直前の確定形状を再利用するStatic Reuse Group
* Rootや親Transformの剛体変換だけを適用するStatic Transform Group
* 境界や衝突を局所補正するRepair Group
* 異常時に強制的に通常Solverへ戻すForced Dynamic Group

本方式の目的は、単に髪を静的・動的に切り替えることではない。

提案方式の中心は、次の三点である。

1. **Segment Groupごとに、固定Reusable Coreと必要時だけ確保するDynamic Sidecarを切り替える状態依存Payload表現**
2. **GroupおよびBlock単位で、静的再利用の許容性を経験的に検証したAdmissibility Certificateにより判定する方式**
3. **Guideの局所更新範囲をFollower再構築範囲および描画更新範囲まで伝播させるEnd-to-End局所更新方式**

加えて、提案方式が実用上破綻しないよう、次の安全機構を組み込む。

* Hysteresis
* Minimum Residence Time
* Transition Budget
* Incremental Block Invalidation
* Deterministic Shared-Point Resolve
* Full Dynamic Fast Path
* Watchdog Fallback
* Ground Truth比較
* Shadow Dynamic検証
* Stable Compaction
* Follower Cache整合性検査

---

# 2. 研究背景

## 2.1 髪シミュレーションの計算量

髪シミュレーションでは、各strandを複数の点と線分で表現し、各点または線分に対して次の処理を行う。

* 外力積分
* Root追従
* 距離制約
* 曲げ制約
* 衝突判定
* 自己衝突
* 局所座標Frame更新
* GuideからFollowerへの形状再構築
* 描画用頂点生成

100,000本のstrandを128点で表現すると、表示上の点数は12,800,000点となる。

すべての点を毎フレーム同一の方法で再計算すると、ほとんど変化していない領域についても、物理計算、Follower再構築、描画準備が繰り返される。

---

## 2.2 Strand全体を単位とした静的化の問題

既存のSleeping、Cache Reuse、Guide/Follower、LOD、Cluster更新などでは、次の単位で更新可否を判断することが多い。

* オブジェクト全体
* strand全体
* Guide全体
* 毛束
* Cluster
* 空間Block

しかし、一本の髪の内部でも、動的状態は均一とは限らない。

例として、次の状態が存在する。

* Root付近だけが頭部運動の影響を受ける
* 毛先付近だけがColliderへ接触する
* 中央部だけが風の影響を受ける
* 編集対象周辺だけが変形する
* 静的領域と動的領域が一本の髪の中で混在する

髪一本全体をDynamicにすると不要な計算が増える。

一方で、髪一本全体をStaticにすると、衝突漏れ、形状誤差、境界不連続が発生する。

---

## 2.3 単純なStatic/Dynamicフラグの問題

Segment GroupごとにBooleanフラグを持たせるだけでは、次の問題が残る。

* DynamicとStaticが同じPayloadを保持し、メモリ削減にならない
* 毎フレーム状態判定を行い、判定コストが残る
* Static化してよい根拠が弱い
* 共有点へ複数Groupが書き込む
* 状態変更のたびにBuffer間Copyが発生する
* Followerを全再構築すると高速化が失われる
* Dynamic割合が高い場合に提案方式がBaselineより遅くなる
* 描画側がボトルネックとなり、Simulation削減がEnd-to-End性能へ反映されない

---

# 3. 研究目的

本研究の目的は、髪一本の内部をSegment Group単位で局所的に管理し、変化が小さい領域について物理計算、Follower再構築、描画準備を省略することである。

ただし、静的化による見た目や物理的整合性の低下を防ぐため、次の条件を必須とする。

1. 静的再利用による誤差を計測できる
2. 許容範囲を超える前にDynamicへ戻せる
3. 衝突や編集時は安全側へ退避できる
4. Static/Dynamic境界を決定論的に解決できる
5. Guideの局所更新をFollowerと描画へ伝播できる
6. Dynamic割合が高い場合は通常Solverへ退避できる
7. 提案方式の追加処理がBaselineを恒常的に上回らない

---

# 4. 研究仮説

## H1：局所状態による不要計算削減

髪一本の内部に静的領域と動的領域が混在する場合、Segment Group単位で更新可否を判断することで、strand全体を単位とする方式よりも更新対象を削減できる。

---

## H2：固定CoreとDynamic SidecarによるPayload削減

Static Groupでは、Velocity、Previous Position、Solver内部状態などを毎フレーム保持する必要がない。

すべてのGroupが固定Reusable Coreを持ち、Dynamic状態に必要な情報だけをSidecar Poolから取得することで、Buffer間の大規模Copyを避けながら、状態依存Payloadを実現できる。

---

## H3：Admissibility Certificateによる安全な再利用

位置変化、速度、拘束残差、境界誤差、衝突余裕、Root変換量などを用いて静的再利用の許容性を判定することで、単純な移動量判定よりFalse Staticを抑制できる。

---

## H4：Block単位の増分無効化による判定削減

毎フレーム全GroupからBlock Certificateを再構築するのではなく、Root、Collider、外力、編集などのDirty Sourceから影響Blockだけを失効させることで、静的BlockではGroup単位判定自体を省略できる。

---

## H5：Follower局所再構築によるEnd-to-End高速化

Guideだけを局所更新しても、Followerを毎フレーム全再構築した場合、100,000本表示ではFollower処理が支配的になる。

Guide GroupのDirty RangeをFollower依存関係に基づいて伝播することで、提案方式の効果を表示strand全体へ接続できる。

---

## H6：実測コスト型Fast Pathによる負の高速化防止

Dynamic割合が高い状況では、Certificate、分類、Compaction、Sidecar確保などの追加処理が不利益になる。

前フレームまでのGPU実測時間と現在の状態分布から、提案経路とFull Dynamic経路の予測コストを比較することで、固定Dynamic比率だけを使う方式より適切にFast Pathを選択できる。

---

# 5. 新規性の主張範囲

## 5.1 主新規性

本研究の主新規性は、次の三点に限定する。

### 主新規性1

## Segment Group単位のAdaptive Payload Representation

すべてのGroupが固定Reusable Coreを持ち、Dynamic状態に必要な情報のみをSidecar Payloadとして追加保持する。

StaticとDynamicを単なるフラグ分岐ではなく、実際の保持情報と実行経路が異なる状態依存表現として扱う。

---

### 主新規性2

## Incremental Admissibility Certificate

Group単位とBlock単位のCertificateを使用し、静的再利用の許容性を判定する。

Block Certificateは毎フレーム全Groupから再構築せず、Dirty Sourceから影響範囲だけを増分無効化する。

---

### 主新規性3

## End-to-End Local Update Propagation

Guide Groupの状態をGuide内部だけで完結させず、次の範囲へ伝播する。

```text
Guide Group
→ Follower Dependency Range
→ Follower Segment Range
→ Render Vertex Range
→ Indirect Draw Range
```

これにより、局所的な静的化をSimulationだけでなく、Follower再構築と描画準備の削減へ接続する。

---

## 5.2 補助的な新規性候補

Deterministic Hemisphere Shared-Point Resolveは、実験結果によって扱いを決定する。

境界誤差、競合、決定性に明確な改善が確認できた場合は補助的新規性として主張する。

効果が小さい場合は、安全機構として位置づけ、新規性の中心には含めない。

---

## 5.3 新規性として主張しない要素

次は既存技術、一般的最適化、またはEngineering Safeguardとして扱う。

* 点・線分による髪表現
* Guide/Follower
* PBD
* XPBD
* Rod系Solver
* Sleeping
* Cache Reuse
* Dirty Range
* LOD
* GPU Compute
* Prefix Sum
* Stable Compaction
* Indirect Draw
* Frustum Culling
* Persistent Buffer
* Full Dynamic Solver
* Full Dynamic Fast Path
* Hysteresis
* Watchdog
* Ground Truth比較
* Shadow Validation
* Telemetry
* Frame Time計測

---

# 6. Segment Groupの定義

## 6.1 基本構造

一本のstrandを隣接する二点とその間のSegmentからなるGroupへ分割する。

[
G_i = (P_i, E_i, P_{i+1})
]

* (P_i)：Group始点
* (E_i)：Segment
* (P_{i+1})：Group終点
* (G_i)と(G_{i+1})は(P_{i+1})を共有する

128点のstrandでは、127個のSegment Groupを持つ。

---

## 6.2 等間隔化

strand全長を(L)、Group数を(N_g)とすると、基準Segment長は次式となる。

[
l_0 = \frac{L}{N_g}
]

初期化時またはAsset生成時に、strandを等間隔へResampleする。

Asset固有の形状保持のため完全等長化が適さない場合は、GroupごとのRest Lengthを保持する。

ただし、極端に短いSegmentや長いSegmentが混在しないよう、許容範囲を定める。

---

## 6.3 Root固定

Root点は頭皮または親TransformへBindingする。

Root変換は次のように扱う。

### 平行移動

Root移動量を全Groupへ同一に加算する。

### 回転

Root基準の局所座標から剛体変換を適用する。

### 禁止事項

Rootから毛先へXYZ値を等差的に加算してはならない。

この方式はSegment Lengthを変化させ、髪の不自然な伸長を引き起こす。

---

# 7. Group状態

Groupは最低限、次の状態を持つ。

```text
Dynamic
StaticReuse
StaticTransform
Repair
ForcedDynamic
Invalid
```

---

## 7.1 Dynamic

通常の物理Solverを実行する。

Dynamic Sidecarを保持する。

---

## 7.2 StaticReuse

前回確定した形状をそのまま再利用する。

Root、Collider、外力、編集、Certificateの各条件が有効である場合に使用する。

---

## 7.3 StaticTransform

内部形状は再計算せず、Rootまたは親Transformの剛体変換だけを適用する。

平行移動と回転に対応する。

---

## 7.4 Repair

Static/Dynamic境界、共有点、局所衝突、長さ誤差などを限定的に補正する。

Repairは通常Solver全体の代替ではない。

---

## 7.5 ForcedDynamic

次の条件では、他の判定より優先してDynamicへ戻す。

* 衝突侵入
* User Edit
* Certificate失効
* Watchdog発火
* Payload不整合
* NaNまたはInf
* Segment Length異常
* Shared Point未解決
* Follower Cache不整合
* Topology変更

---

## 7.6 Invalid

入力、Version、Payload Index、Topologyなどに不整合がある状態。

Invalid GroupはStaticとして使用せず、修復またはBaseline Fallbackへ送る。

---

# 8. Fixed Reusable CoreとDynamic Sidecar

## 8.1 基本方針

Dynamic PoolとStatic Poolの間で、Group全体を毎回Copyする方式は使用しない。

すべてのGroupは固定位置のReusable Coreを持つ。

Dynamic状態に必要な情報だけをSidecar Poolから取得する。

---

## 8.2 Group Core

```cpp
struct GroupCore
{
    float3 localStart;
    float3 localEnd;

    float restLength;
    float maxObservedPositionError;
    float maxObservedBoundaryError;
    float minObservedCollisionMargin;

    uint state;
    uint dynamicSidecarIndex;

    uint strandId;
    uint localGroupIndex;

    uint stateEpoch;
    uint payloadVersion;
    uint certificateVersion;

    uint lastTransitionFrame;
    uint minimumResidenceEndFrame;
    uint dirtyFlags;
};
```

Group Coreは固定Indexで保持する。

状態が変化しても、Group Core自体の配置は変更しない。

---

## 8.3 Dynamic Sidecar

```cpp
struct DynamicSidecar
{
    float3 previousStart;
    float3 previousEnd;

    float3 velocityStart;
    float3 velocityEnd;

    float constraintResidual;
    float collisionResidual;

    uint solverFlags;
    uint collisionFlags;
};
```

DynamicになったGroupだけがSidecarを保持する。

---

## 8.4 Dynamic化

Static GroupがDynamicになる場合、次の処理を行う。

1. Group Coreの確定位置を取得
2. Sidecar IndexをFree Listから取得
3. Previous Positionを現在位置で初期化
4. Velocityを0または予測値で初期化
5. StateをDynamicへ変更
6. State Epochを更新

---

## 8.5 Static化

Dynamic GroupがStaticへ戻る場合、次の処理を行う。

1. Solver後の確定形状をGroup Coreへ保存
2. Certificate情報を更新
3. Sidecarを即時解放せずRetire Listへ移動
4. GPU処理完了後に遅延解放
5. State Epochを更新

---

## 8.6 Sidecar遅延解放

GPUがSidecarを参照中に再利用しないよう、Frame FenceまたはEpochを使用する。

```text
Active
→ Retired
→ Safe To Reuse
→ Free
```

毎フレームの即時AllocationとFreeを禁止する。

---

## 8.7 Pool管理

Sidecar Poolは次の方式で管理する。

* 固定容量
* Free List
* Retire List
* Prefix Sumによる一括割当
* Epochによる遅延再利用
* Overflow時のForced Dynamic Baseline Fallback

---

# 9. 状態遷移制御

## 9.1 Hysteresis

DynamicからStaticへ移行する閾値と、StaticからDynamicへ戻る閾値を分ける。

例：

```text
Dynamic → Static候補:
predictedError < 0.5 × allowedError
residual       < 0.5 × allowedResidual
velocity       < staticVelocityThreshold

Static → Dynamic:
predictedError >= 1.0 × allowedError
residual       >= 1.0 × allowedResidual
collisionMargin <= requiredMargin
```

---

## 9.2 Minimum Residence Time

状態変更後、一定フレームは同じ状態を維持する。

ただし、次の場合はResidenceを無視して即時Dynamic化する。

* 衝突侵入
* User Edit
* Fatal
* Invalid
* Watchdog
* Topology変更

---

## 9.3 Consecutive Pass条件

DynamicからStaticへの移行は、一度Certificateに合格しただけでは許可しない。

```text
Kフレーム連続合格
かつ
Minimum Residenceを満たす
かつ
Transition Budget内
```

の場合のみStatic化する。

---

## 9.4 Transition Budget

一フレームで処理可能な状態変更量を制限する。

```text
maxDynamicToStaticPerFrame
maxStaticToDynamicPerFrame
maxSidecarAllocationsPerFrame
maxSidecarRetiresPerFrame
maxTransitionBytesPerFrame
```

安全上必要なStaticからDynamicへの移行は、原則として遅延しない。

Budgetを超えた場合は、Static化候補をDynamicのまま維持する。

---

# 10. Empirically Validated Admissibility Certificate

## 10.1 名称

本研究では、数学的に厳密な誤差上限が証明できるまでは、`Error-Bounded Certificate`という表現を主名称として使用しない。

正式名称を次とする。

> **Empirically Validated Admissibility Certificate**

日本語では次のように表記する。

> **経験的検証付き静的再利用許容判定**

---

## 10.2 Certificateの役割

Certificateは、GroupをStaticとして再利用した場合に、Ground Truthとの差が許容範囲内に収まる可能性を判定する。

Certificateは数学的証明ではなく、次の情報を用いた保守的な予測判定である。

* 現在の位置変化
* 速度
* 加速度
* 拘束残差
* 境界誤差
* Root変換量
* 外力変化
* Collider接近量
* 過去のShadow Dynamic誤差
* Certificate Age

---

## 10.3 予測誤差

Group (G_i)の次フレーム予測誤差を次のように定義する。

[
\hat{E}_i^{t+1}
===============

w_p E_{position}
+
w_v E_{velocity}
+
w_a E_{acceleration}
+
w_c E_{constraint}
+
w_b E_{boundary}
+
w_r E_{root}
+
w_f E_{force}
+
w_h E_{history}
]

各項は同一の長さ単位へ正規化する。

単位の異なる値をそのまま加算してはならない。

---

## 10.4 正規化

例として、拘束残差は次のように長さ誤差へ変換する。

[
E_{constraint}
==============

|,|P_{i+1}-P_i|-l_0,|
]

Root回転誤差は、GroupのRootからの距離(d_i)と回転角(\theta)から次のように近似する。

[
E_{root}
========

2 d_i \sin\left(\frac{\theta}{2}\right)
]

速度による予測変位は次式とする。

[
E_{velocity}
============

|v_i| \Delta t
]

加速度による変位は次式とする。

[
E_{acceleration}
================

\frac{1}{2}|a_i|\Delta t^2
]

---

## 10.5 衝突余裕

Colliderとの最小距離を(m_{col})とする。

次の条件を満たさない場合、Static化しない。

[
m_{col}

>

E_{velocity}
+
E_{acceleration}
+
E_{colliderMotion}
+
\tau_{collisionSafety}
]

Colliderが高速移動する場合は、現在距離だけではなくSwept AABBまたは予測移動量を使用する。

---

## 10.6 Static許可条件

次をすべて満たす場合にStatic候補とする。

[
\hat{E}*i^{t+1} < \tau*{position}
]

[
E_{constraint} < \tau_{constraint}
]

[
E_{boundary} < \tau_{boundary}
]

[
m_{col} > \tau_{requiredMargin}
]

さらに、必要なVersionがすべて一致していなければならない。

---

## 10.7 複数フレームStatic

Static継続中は、誤差が無制限に蓄積しないよう、Certificate Ageを管理する。

[
Age_i = currentFrame - lastValidatedFrame
]

次のいずれかを満たした場合、再検証する。

* Ageが最大値を超えた
* Root Transformが変化した
* 外力が変化した
* Colliderが接近した
* 隣接GroupがDynamicになった
* Shadow Dynamic誤差が増加した
* Boundary Errorが増加した

---

## 10.8 Hard SafetyとSoft Predictionの分離

Certificateを二層に分ける。

### Hard Safety

一つでも違反した場合、必ずDynamicへ戻す。

* Collision Penetration
* NaN / Inf
* Topology変更
* Payload不整合
* Shared Point未解決
* Segment Length異常
* Version不一致
* User Edit

### Soft Prediction

Static継続可否を判定する。

* 位置予測誤差
* 速度
* 加速度
* 拘束残差
* 境界誤差
* Certificate Age

---

# 11. Incremental Block Certificate

## 11.1 目的

Block Certificateを毎フレーム全Groupから再集約すると、静的BlockでもGroup読み込みが発生する。

そこで、Dirty Sourceから影響Blockだけを無効化する。

---

## 11.2 Block定義

連続する32または64 Groupを一つのBlockとする。

Blockサイズは固定せず、性能評価によって決定する。

```cpp
struct BlockCertificate
{
    float maxPredictedError;
    float maxConstraintResidual;
    float maxBoundaryError;
    float minCollisionMargin;

    uint dynamicCount;
    uint repairCount;
    uint invalidCount;

    uint rootVersion;
    uint forceVersion;
    uint topologyVersion;
    uint settingsVersion;

    uint certificateVersion;
    uint lastValidatedFrame;

    uint dirtyMask;
    uint validityFlags;
};
```

---

## 11.3 Dirty Source

Blockを無効化する原因を次のように分類する。

```text
Root Dirty
Collider Dirty
Force Field Dirty
User Edit Dirty
Topology Dirty
Solver Settings Dirty
Follower Mapping Dirty
LOD Dirty
Watchdog Dirty
```

---

## 11.4 空間的影響範囲

Colliderが移動しただけで全Blockを失効させてはならない。

次の構造を使用し、影響Blockだけを無効化する。

* Block AABB
* Collider Swept AABB
* Spatial Hash
* Uniform Grid
* Broad Phase Intersection
* Root Binding Range
* Edit Brush Range

```text
Dirty Source
→ 影響AABB
→ Spatial Query
→ Affected Block List
→ Block Certificate Invalid
```

---

## 11.5 Block Certificate再利用条件

次をすべて満たすBlockは、前フレームのCertificateを再利用できる。

```text
dirtyMask == 0

rootVersion一致
forceVersion一致
topologyVersion一致
settingsVersion一致

certificateAge <= maxCertificateAge

dynamicCount == 0
repairCount == 0
invalidCount == 0

maxPredictedError < threshold
maxConstraintResidual < threshold
maxBoundaryError < threshold
minCollisionMargin > requiredMargin
```

---

## 11.6 Block Skip時に省略する処理

Block Certificateが有効な場合、次を省略する。

* Group単位Certificate更新
* State Candidate生成
* Dynamic List登録
* Constraint Solver
* Collision Solver
* Sidecar Allocation
* Payload Transition判定
* Guide形状再生成
* Follower再構築
* Render Vertex再生成

Root Transformだけが変化した場合は、Static Transform専用経路を使用する。

---

## 11.7 Certificate Age

Dirtyが発生しなくても、永久にBlock Certificateを再利用してはならない。

一定期間ごとに再検証する。

```text
Normal Block:
一定フレームごとに再検証

Collision近傍Block:
短い間隔で再検証

長時間Static Block:
Shadow Dynamic優先対象
```

---

# 12. Shared-Point Resolve

## 12.1 問題

隣接するGroupは共有点を持つ。

複数Groupが同じ点へ直接書き込むと、GPU実行順序によって結果が変化する。

---

## 12.2 半球責任範囲

共有点を物理的に二つへ分割するのではない。

共有点周辺の候補方向を論理的に前方半球と後方半球へ分ける。

* Root側GroupはRoot方向の制約候補を生成
* Tip側GroupはTip方向の制約候補を生成

---

## 12.3 Candidate構造

```cpp
struct SharedPointCandidate
{
    float3 position;

    float positionError;
    float constraintResidual;
    float collisionResidual;

    uint priority;
    uint sourceGroupId;
    uint reasonFlags;
};
```

---

## 12.4 優先順位

```text
1. Fatal / Invalid Reject
2. Collision Escape
3. User Edit
4. Forced Dynamic
5. Watchdog Recovery
6. Dynamic Solver
7. Boundary Repair
8. Static Transform
9. Static Reuse
```

同一Priorityでは、次の順で決定する。

1. NaNではない
2. Position Errorが小さい
3. Constraint Residualが小さい
4. Collision Residualが小さい
5. Source Group IDが小さい

---

## 12.5 書き込み規則

共有点の最終Positionを書き込むPassは一つだけに限定する。

```text
Candidate Generation
→ Stable Reduce
→ Apply
```

Atomic到着順で最終候補を決定しない。

---

## 12.6 決定論性の主張範囲

本研究が保証対象とする決定論性は、次に限定する。

> 同一GPU、同一Driver、同一Shader、同一入力、同一実行設定における再実行時の結果一致。

異なるGPUベンダー間や異なるDriver間でのBit完全一致は主張しない。

正式評価では次を固定する。

* 候補格納位置
* Group ID順
* Reduce順序
* NaNの扱い
* 同値判定epsilon
* Stable Compaction
* 固定時間刻み
* 固定乱数Seed

---

# 13. Dynamic Solver

Dynamic Groupは、Baselineと同品質のSolverを使用する。

提案方式の性能を良く見せるため、Dynamic側の品質を下げてはならない。

最低限、次を含める。

* Integration
* Root Constraint
* Distance Constraint
* Bend Constraint
* External Force
* Collider Constraint
* Boundary Repair

BaselineとProposed Methodでは、以下を一致させる。

* 時間刻み
* 反復回数
* 制約式
* Collision条件
* Root条件
* 数値精度
* Solver順序

---

# 14. Follower全再構築の廃止

## 14.1 必須要件

4,000本のGuideを部分更新しても、96,000本のFollowerを毎フレーム全再構築した場合、提案方式の効果が失われる。

Follower局所再構築は、補助的最適化ではなく、100,000本表示における提案方式の成立条件とする。

---

## 14.2 Follower依存関係

Followerが一つのGuideだけに依存するとは限らない。

Follower生成アルゴリズムは、次を参照する可能性がある。

* Guide Position
* Guide Tangent
* Guide Normal
* Parallel Transport Frame
* 隣接Segment
* 複数GuideのWeighted Interpolation
* Curl Parameter
* Strand Width
* Smoothing Filter
* Root Offset

そのため、Guide GroupからFollowerへの依存関係を事前計算する。

---

## 14.3 Dependency Metadata

```cpp
struct FollowerDependency
{
    uint followerId;

    uint guideCount;
    uint guideIds[MAX_GUIDE_INFLUENCE];

    uint sourceSegmentBegin;
    uint sourceSegmentEnd;

    uint targetSegmentBegin;
    uint targetSegmentEnd;

    uint supportRadius;
    uint guardBand;

    uint dependencyVersion;
};
```

---

## 14.4 Guard Bandの導出

Guard Bandを固定の経験値だけで決めてはならない。

Follower生成アルゴリズムの参照範囲から導出する。

例：

### 線形補間

対象Segmentのみを参照する場合：

```text
guardBand = 0
```

### Tangent計算

前後1点を参照する場合：

```text
guardBand = 1
```

### 中心差分＋Smoothing

前後2点を参照する場合：

```text
guardBand = 2
```

### 複数Guide補間

影響GuideすべてのDirty RangeをUnionする。

---

## 14.5 Dirty Range伝播

```text
Guide Group Dirty
→ Guide Dependency Lookup
→ Affected Follower IDs
→ Segment Range Union
→ Guard Band拡張
→ Follower Dirty Range
→ Render Dirty Range
```

---

## 14.6 Follower更新モード

### Cache Reuse

依存GuideがすべてStatic Reuseで、Versionが一致する場合、Follower Cacheをそのまま使用する。

### Rigid Transform

依存GuideがStatic Transformで、内部形状が変化しない場合、同一Root Transformを適用する。

### Local Reconstruct

Dirty RangeとGuard Bandのみを再構築する。

### Full Follower Reconstruct

次の条件では対象Follower全体を再構築する。

* Dependency Version不一致
* Guide-Follower Mapping変更
* Topology変更
* LOD変更
* Cache不整合
* 大規模Edit
* Watchdog
* Dirty Range計算失敗

---

## 14.7 Follower Cache Metadata

```cpp
struct FollowerCacheMetadata
{
    uint followerId;

    uint guideShapeVersion;
    uint dependencyVersion;
    uint rootTransformVersion;
    uint followerRuleVersion;
    uint lodVersion;
    uint assetVersion;

    uint dirtyBegin;
    uint dirtyEnd;

    uint cacheState;
    uint validityFlags;
};
```

---

# 15. Full Dynamic Fast Path

## 15.1 目的

Dynamic Groupが多い場合、次の処理が追加負荷となる。

* Certificate評価
* Group分類
* Block集約
* Compaction
* Sidecar管理
* Static Transform
* Repair分類
* Follower Cache判定

この場合は通常のFull Dynamic Solverへ退避する。

---

## 15.2 固定比率だけを使わない

`dynamicRatio >= 0.80`のような固定値は初期値としてのみ使用する。

正式判定はGPU実測時間に基づく。

---

## 15.3 コストモデル

提案経路の予測時間を次のように表す。

[
T_P =
T_{dirty}
+
T_{certificate}
+
T_{classification}
+
T_{compaction}
+
T_{sidecar}
+
T_{partialSolver}
+
T_{followerPartial}
+
T_{renderPartial}
]

Full Dynamic経路は次とする。

[
T_F =
T_{fullSolver}
+
T_{followerFull}
+
T_{renderFull}
]

次の条件が一定フレーム続いた場合、Full Dynamic Fast Pathへ移行する。

[
\hat{T}_P
\ge
\hat{T}*F - \tau*{switch}
]

---

## 15.4 予測入力

コスト予測には次を使用する。

* Dynamic Ratio
* Dynamic Block Ratio
* Block Skip率
* Dirty Follower Ratio
* Dirty Render Vertex Ratio
* Sidecar Allocation数
* Transition数
* Collision対象数
* Dispatch数
* 前フレームGPU時間
* 移動平均GPU時間
* 最悪Frame Time

---

## 15.5 切替Hysteresis

Fast Path開始条件と終了条件を分ける。

```text
Fast Path開始:
Proposal予測時間 >= Full Dynamic予測時間
がKフレーム継続

Fast Path終了:
Proposal予測時間 <= Full Dynamic予測時間 - safetyMargin
がMフレーム継続
```

---

## 15.6 Fast Path時の処理

Full Dynamic Fast Pathでは次を省略する。

* Static候補生成
* Static Sidecar解放
* Static Compaction
* Static Transform Dispatch
* Detailed Certificate
* Follower Cache細分判定

Guideを連続Bufferとして通常Solverへ入力する。

---

## 15.7 研究上の位置づけ

Full Dynamic Fast Path自体を新規性として主張しない。

提案方式の負の高速化を防ぐEngineering Safeguardとして扱う。

---

# 16. 描画側の既存最適化

## 16.1 方針

描画最適化自体はNovelty 01の中心ではない。

ただし、Simulationの削減効果が描画ボトルネックに隠れることを防ぐため、既存最適化を適用する。

---

## 16.2 使用候補

* GPU Resident Buffer
* Persistent Vertex Buffer
* Indirect Draw
* Frustum Culling
* Screen-space LOD
* Distance LOD
* Segment LOD
* Width LOD
* Dirty Range Vertex Update
* Draw Command Compaction
* CPU Readback排除
* Range-based Dispatch

---

## 16.3 Render Dirty Range

Follower Dirty Rangeを描画用頂点範囲へ変換する。

```text
Follower Segment Dirty
→ Render Vertex Range
→ Vertex Cache Invalid
→ Dirty Rangeのみ再生成
```

Static Rangeは前フレームの描画Bufferを再利用する。

---

## 16.4 公平な比較

一般的な描画最適化は、BaselineとProposed Methodの両方へ可能な限り同じ条件で適用する。

ただし、提案方式が生成する局所Dirty Rangeを用いた更新は、提案方式固有のEnd-to-End効果として別途評価する。

---

# 17. フレーム処理Pipeline

```text
Phase 01  Frame Input確定
Phase 02  Root / Collider / Force Version更新
Phase 03  Topology / Settings / LOD変更検出
Phase 04  Dirty Source生成
Phase 05  Spatial QueryによるAffected Block抽出
Phase 06  Block Certificate失効処理
Phase 07  Fast Path Cost Prediction
Phase 08  Full Dynamic Fast Path判定

通常経路の場合:

Phase 09  Invalid BlockのみGroup Certificate更新
Phase 10  State Candidate生成
Phase 11  Hard Safety判定
Phase 12  Hysteresis適用
Phase 13  Minimum Residence適用
Phase 14  Transition Budget適用
Phase 15  Sidecar Allocation / Retire Plan
Phase 16  Dynamic / Static / Repair List生成
Phase 17  Dynamic Integration
Phase 18  Dynamic Constraint Solve
Phase 19  Collision Solve
Phase 20  Static Reuse / Static Transform
Phase 21  Shared-Point Candidate生成
Phase 22  Stable Reduce
Phase 23  Shared-Point Apply
Phase 24  Boundary Repair
Phase 25  Guide Dirty Range確定
Phase 26  Follower Dependency伝播
Phase 27  Follower Cache Reuse / Local Reconstruct
Phase 28  Render Dirty Range生成
Phase 29  Render Vertex部分更新
Phase 30  Indirect Draw Data更新
Phase 31  Telemetry更新
Phase 32  Watchdog判定
Phase 33  Shadow Dynamic検証
```

Full Dynamic Fast Pathでは、Phase 09からPhase 16の一部を省略し、通常Solverへ直接移行する。

---

# 18. 優先順位

状態競合時の優先順位を次のように固定する。

```text
Fatal / Invalid
    >
Collision Penetration
    >
User Edit
    >
Topology Change
    >
Watchdog Recovery
    >
Forced Dynamic
    >
Boundary Repair
    >
Normal Dynamic
    >
Static Transform
    >
Static Reuse
```

性能上有利であっても、安全条件を下位状態で上書きしてはならない。

---

# 19. WatchdogとFallback

## 19.1 監視対象

* NaN
* Inf
* Segment Length異常
* Constraint Residual急増
* Sidecar Index不整合
* Duplicate Sidecar Allocation
* Certificate Version不一致
* Shared Point未解決
* Follower Cache不整合
* Dirty Range範囲外
* Dispatch Count不整合
* Transition過多
* Certificate Violation急増
* Shadow Dynamic誤差急増
* Frame Time異常

---

## 19.2 Fallback単位

異常の範囲に応じて、次の単位で退避する。

```text
Group Fallback
Block Fallback
Strand Fallback
Guide Fallback
Frame Full Dynamic Fallback
Application Safe Stop
```

---

## 19.3 Fallback原則

提案方式が失敗しても、可能な限り通常Solverで描画を継続する。

Proposal固有Bufferに異常があっても、Baseline Solverへ戻れる経路を維持する。

---

# 20. 現在の実装との対応

| 機能                     | 設計状態 | 現在の実装状態     | 正式完了条件                  |
| ---------------------- | ---- | ----------- | ----------------------- |
| Segment Group Metadata | 定義済み | 試作あり        | 固定Core構造へ統一             |
| Dynamic Sidecar        | 定義済み | 未完成         | Pool・Free List・Retire実装 |
| State Machine          | 定義済み | 簡易試作        | Hysteresis・Residence対応  |
| Numeric Certificate    | 定義済み | 簡易Boolean中心 | 数値項目・正規化・Age対応          |
| Block Certificate      | 定義済み | 部分試作        | Dirty Source増分失効        |
| Static Reuse           | 定義済み | 試作あり        | Certificate連携           |
| Static Transform       | 定義済み | 平行移動中心      | Root回転対応                |
| Dynamic Solver         | 定義済み | 簡易距離補正中心    | Baseline同品質化            |
| Shared-Point Resolve   | 定義済み | 部分試作        | Stable Reduce実装         |
| Stable Compaction      | 定義済み | Atomic方式あり  | Prefix Sum方式へ変更         |
| Follower Cache         | 定義済み | 全再構築中心      | Cache常駐化                |
| Follower Dependency    | 定義済み | 未実装         | 事前計算Metadata追加          |
| Follower Local Rebuild | 定義済み | 未実装         | Dirty Range Dispatch    |
| Render Dirty Range     | 定義済み | 未完成         | Persistent Range Update |
| Full Dynamic Fast Path | 定義済み | 通常経路あり      | 自動コスト切替                 |
| Shadow Dynamic         | 定義済み | 未完成         | Sample比較実装              |
| Watchdog Fallback      | 定義済み | 部分対応        | Proposal全体へ拡張           |

---

# 21. 比較対象

## B0：Current Full Dynamic Baseline

現在の通常経路に相当する。

* 全GuideをDynamic更新
* Follower全再構築
* 描画全更新

---

## B1：Optimized Full Dynamic Baseline

* 全Guide Dynamic
* GPU Resident
* 一般的描画最適化
* CPU Readback排除
* Indirect Draw

---

## B2：Strand-level Sleeping

strand全体をStaticまたはDynamicにする。

---

## B3：Block-level Sleeping

Block単位で更新を省略する。

Payloadは共通構造を使用する。

---

## B4：Segment Group Flag Only

Group単位で状態を持つが、Sidecar Payloadを使用しない。

---

## B5：Segment Group＋Adaptive Payload

固定Core＋Dynamic Sidecarを使用するが、Certificateは簡易閾値とする。

---

## B6：Adaptive Payload＋Group Certificate

Group Certificateを使用するが、Block増分失効を使用しない。

---

## B7：Adaptive Payload＋Incremental Block Certificate

Guide局所更新まで実装するが、Followerは全再構築する。

---

## B8：Strongest Practical Baseline

既存要素を可能な限り強く組み合わせる。

* Guide/Follower
* Sleeping
* Block Update
* Cache Reuse
* Dirty Range
* GPU Resident
* Indirect Draw
* LOD
* Full Dynamic Fast Path

ただし、次は使用しない。

* Segment Group単位の固定Core＋Dynamic Sidecar
* 経験的検証付きGroup Certificate
* GuideからFollower・描画への統合的依存伝播

---

## P：Proposed Method

本設計の全要素を使用する。

---

# 22. アブレーション

| ID  | 無効化する要素                         | 目的                  |
| --- | ------------------------------- | ------------------- |
| A1  | Dynamic Sidecar                 | Payload表現の効果        |
| A2  | Group Certificate               | 単純閾値との差             |
| A3  | Incremental Block Invalidation  | 全再集約との差             |
| A4  | Block Skip                      | Dispatch省略の効果       |
| A5  | Hysteresis                      | State Thrashingへの影響 |
| A6  | Minimum Residence               | Transition頻度への影響    |
| A7  | Transition Budget               | Frame Spikeへの影響     |
| A8  | Shared-Point Stable Resolve     | 境界誤差・決定性への影響        |
| A9  | Boundary Repair                 | 境界不連続への影響           |
| A10 | Follower Dependency Propagation | Follower全再構築との差     |
| A11 | Render Dirty Range              | End-to-End性能への影響    |
| A12 | Cost-based Fast Path            | 固定閾値との差             |
| A13 | Shadow Dynamic                  | False Static検出への影響  |
| A14 | Watchdog Fallback               | 異常復帰への影響            |
| A15 | Stable Compaction               | 再現性への影響             |

---

# 23. Ground TruthとShadow Dynamic

## 23.1 Ground Truth

同一入力、同一時間刻み、同一Solver設定で、すべてのGuide GroupをDynamicとして処理した結果をGround Truthとする。

---

## 23.2 Shadow Dynamic

Static Groupの一部をサンプリングし、表示には使用しないDynamic Solverを並行実行する。

Static結果との差を測定する。

---

## 23.3 優先サンプリング

* Certificate閾値付近
* Static継続時間が長い
* Collision Marginが小さい
* Static/Dynamic境界
* Root変換量が大きい
* 過去にFalse Staticを発生
* Certificate Ageが大きい

---

## 23.4 Certificate校正

Shadow Dynamic結果から、各誤差項の重みと閾値を調整する。

ただし、評価用データと閾値調整用データを分離する。

同じシナリオだけで調整と評価を行ってはならない。

---

# 24. 評価シナリオ

## S01：完全静止

外力、Root移動、Collider移動なし。

## S02：Root平行移動

頭部全体を一定速度で移動。

## S03：Root回転

頭部を回転し、Static Transformを評価。

## S04：弱い風

一部Groupだけが動く状態。

## S05：通常頭部運動

歩行や振り向き相当。

## S06：局所衝突

限定範囲だけColliderへ接触。

## S07：複数Collider

顔、肩、手などを想定。

## S08：局所編集

一本または限定範囲を編集。

## S09：連続編集

編集範囲を移動。

## S10：強風

Dynamic Ratioが高い状態。

## S11：全動的

Fast Path評価。

## S12：境界Stress

StaticとDynamicが細かく混在。

## S13：State Thrashing Stress

閾値付近を往復。

## S14：Follower Dependency Stress

複数Guide依存を含む。

## S15：長時間動作

数十分から数時間。

## S16：決定性

同一入力を複数回実行。

## S17：Sidecar Pool Stress

Dynamic化が集中する状態。

## S18：Certificate Invalidation Stress

Collider、Root、外力を連続更新。

---

# 25. 評価指標

## 25.1 性能

* Average Frame Time
* Median Frame Time
* p95 Frame Time
* p99 Frame Time
* Maximum Frame Time
* 50ms以上Frame数
* 100ms以上Frame数
* GPU Simulation Time
* Certificate Time
* Block Invalidation Time
* Classification Time
* Compaction Time
* Sidecar Allocation Time
* Sidecar Retire Time
* Follower Reconstruction Time
* Render Preparation Time
* Draw Time

---

## 25.2 更新量

* Dynamic Group数
* Static Reuse Group数
* Static Transform Group数
* Repair Group数
* Invalid Group数
* Valid Block数
* Invalidated Block数
* Block Skip率
* Sidecar使用数
* Sidecar Allocation数
* Sidecar Retire数
* State Transition数
* Follower再構築点数
* Render再生成頂点数
* Full Dynamic Fast Path使用率

---

## 25.3 正確性

* Position RMSE
* Maximum Position Error
* Segment Length Error
* Constraint Residual
* Boundary Discontinuity
* Collision Penetration Depth
* False Static率
* False Dynamic率
* Certificate Violation率
* Shadow Dynamic差分
* Follower Position Error
* Render Geometry Error

---

## 25.4 安定性

* State Thrashing率
* Watchdog発火数
* Fallback回数
* NaN / Inf数
* Sidecar Index異常数
* Cache不整合数
* Shared Point未解決数
* 実行間Hash一致率
* Long-run Memory増加量

---

## 25.5 メモリ

* GPU Dedicated Memory
* GPU Shared Memory
* Process Working Set
* Group Core使用量
* Dynamic Sidecar使用量
* Retire List使用量
* Follower Cache使用量
* Render Cache使用量
* Peak Temporary Buffer使用量

---

# 26. 60FPS正式判定

正式判定はFPSではなくFrame Timeを使用する。

```text
Average Frame Time <= 16.6667 ms

p95 Frame Time <= 16.6667 ms

p99 Frame Time <= 20.0 ms

Maximum Frame Time <= 50.0 ms

100 ms以上のFrame = 0
```

FPSは説明用として併記する。

VSync、Frame Cap、FPS表示の丸め値だけで合否を判断しない。

---

# 27. GTX 1660級環境

GTX 1660級GPUでの性能は、現時点では理論換算とする。

実測値として主張しない。

開発機で次を個別に計測する。

* Compute-bound Pass
* Memory-bound Pass
* Follower Reconstruction
* Render Preparation
* Draw
* Sidecar管理
* Certificate
* Compaction

その後、Compute性能、Memory Bandwidth、GPU世代差を用いて理論換算する。

論文では次の章に記載する。

> 今後の展望
> 旧世代GPUへの適用可能性
> 理論性能換算
> 実機検証計画

---

# 28. 実装段階

## Phase 0：Baseline固定

* 100,000 strands
* 4,000 guides
* 24 followers / guide
* 128 points
* 6 iterations
* Full HD
* 1/60固定
* Telemetry形式固定

---

## Phase 1：Group Core

* 固定Group Index
* State
* Version
* Dirty Flags
* Root Binding

---

## Phase 2：Dynamic Sidecar

* Sidecar Pool
* Free List
* Retire List
* Epoch
* Overflow Fallback

---

## Phase 3：State Machine

* Hysteresis
* Consecutive Pass
* Minimum Residence
* Transition Budget

---

## Phase 4：Group Certificate

* 誤差項
* 単位正規化
* Hard Safety
* Soft Prediction
* Certificate Age

---

## Phase 5：Incremental Block Certificate

* Block AABB
* Dirty Source
* Spatial Query
* Affected Block List
* Block Skip

---

## Phase 6：Shared-Point Resolve

* Candidate
* Stable Reduce
* Apply
* 決定性試験

---

## Phase 7：Dynamic Solver統一

* Baselineと同一Solver
* Distance
* Bend
* Root
* Force
* Collision

---

## Phase 8：Follower Dependency

* Guide-Follower Mapping
* Support Radius
* Guard Band
* Dirty Range Union

---

## Phase 9：Follower Cache

* Cache Reuse
* Rigid Transform
* Local Reconstruct
* Full Reconstruct Fallback

---

## Phase 10：Render Dirty Range

* Persistent Vertex Cache
* Dirty Vertex Update
* Indirect Draw
* LOD

---

## Phase 11：Cost-based Fast Path

* GPU時間計測
* Cost Prediction
* Hysteresis切替
* Full Dynamic Fallback

---

## Phase 12：正式評価

* B0からB8
* Proposed Method
* A1からA15
* S01からS18
* Ground Truth
* Shadow Dynamic
* 長時間試験
* 決定性試験

---

# 29. 実装完了条件

```text
[ ] Group Coreが固定位置で保持される
[ ] Dynamic専用情報がSidecarとして分離される
[ ] Group全体を毎回Pool間Copyしない
[ ] Sidecarに遅延解放がある
[ ] Hysteresisがある
[ ] Minimum Residenceがある
[ ] Transition Budgetがある
[ ] Certificate各項が同一単位へ正規化される
[ ] Hard SafetyとSoft Predictionが分離される
[ ] Certificate Ageがある
[ ] Block Certificateが増分無効化される
[ ] Collider移動で全Blockを無効化しない
[ ] Block Skipが実際のDispatch省略に使われる
[ ] Shared Pointの最終書き込みPassが一つである
[ ] Stable Reduceが実装される
[ ] Dynamic SolverがBaselineと同品質である
[ ] Follower依存関係が事前計算される
[ ] Guard Bandが依存範囲から導出される
[ ] Follower全再構築が通常経路から廃止される
[ ] Follower Dirty Rangeが描画へ伝播する
[ ] Render Vertexを局所更新できる
[ ] Fast Pathが実測コストで切り替わる
[ ] WatchdogからBaselineへ退避できる
[ ] Ground Truth比較がある
[ ] Shadow Dynamicがある
[ ] False Static率を測定できる
[ ] Sidecar使用量を測定できる
[ ] Block Skip率を測定できる
[ ] Follower再構築点数を測定できる
[ ] 決定性を測定できる
[ ] B8と比較できる
```

---

# 30. 主なリスク

## 30.1 静的割合が低い

### リスク

強風や多数Colliderでは、Static Groupがほとんど存在しない。

### 対策

* Cost-based Full Dynamic Fast Path
* Block単位の早期判定
* Proposal経路の自動停止

---

## 30.2 Sidecar Pool不足

### リスク

大量のGroupが同時にDynamic化すると、Sidecarが不足する。

### 対策

* 最大Dynamic Group数を事前確保
* Overflow Telemetry
* Full Dynamic Baseline Bufferへの退避
* Pool容量評価

---

## 30.3 State Thrashing

### リスク

閾値付近でStaticとDynamicが往復する。

### 対策

* Hysteresis
* Consecutive Pass
* Minimum Residence
* Transition Budget

---

## 30.4 Certificateが緩い

### リスク

False Static、衝突漏れ、形状誤差が発生する。

### 対策

* Hard Safety
* Shadow Dynamic
* Collision Margin
* Boundary Error
* Certificate Age
* Watchdog

---

## 30.5 Certificateが保守的すぎる

### リスク

ほとんどStatic化されず、性能が改善しない。

### 対策

* 誤差項別Telemetry
* 校正用データと評価用データの分離
* B8との比較
* 閾値感度分析

---

## 30.6 Block失効範囲が広すぎる

### リスク

ColliderやRoot移動のたびに大半のBlockが再評価される。

### 対策

* Swept AABB
* Spatial Hash
* Root Binding Range
* Edit Range
* 影響範囲別Dirty Mask

---

## 30.7 Follower依存範囲の過小評価

### リスク

Guide更新がFollowerへ正しく反映されない。

### 対策

* Support Radiusの明示
* Guard Band導出
* Full Reconstruct Fallback
* Follower Ground Truth比較

---

## 30.8 描画がボトルネック

### リスク

Simulationが高速化してもFrame Timeが改善しない。

### 対策

* Simulation単体とEnd-to-Endを分離
* Render Dirty Range
* Persistent Buffer
* LOD
* Indirect Draw

---

## 30.9 Hemisphere Resolveの効果が小さい

### リスク

独自要素として主張しても、性能や品質に差が出ない。

### 対策

* アブレーション
* 境界誤差比較
* 決定性比較
* 効果が小さい場合は安全機構へ格下げ

---

# 31. 成功条件

## 31.1 性能

静的領域を含む複数シナリオで、B8よりSimulationまたはEnd-to-End Frame Timeが改善する。

---

## 31.2 正確性

Ground Truthに対する位置誤差、長さ誤差、拘束誤差、衝突誤差が許容値内に収まる。

---

## 31.3 安全性

Certificateが誤判定した場合でも、Shadow Dynamic、Watchdog、Forced Dynamic、Baseline Fallbackにより継続可能である。

---

## 31.4 一般性

完全静止だけでなく、Root移動、局所衝突、弱い風、局所編集、複数Guide依存などで効果を確認できる。

---

## 31.5 負の高速化防止

全動的または高Dynamic Ratio条件で、Cost-based Fast PathによりBaselineより大幅に悪化しない。

---

# 32. 新規性判定

## 32.1 修正前

修正前は次の理由から「危ない」であった。

* Static/Dynamic切替だけでは既存Sleepingとの差が弱い
* Payload MorphingがBuffer Copy中心
* CertificateがBooleanに近い
* Block Certificateが形式的
* Followerが全再構築
* Fast Path閾値に根拠がない
* 描画最適化と主新規性が混在
* 誤差保証という表現が強すぎる

---

## 32.2 本設計による改善

本設計では次を修正した。

* 固定Core＋Dynamic Sidecarへ変更
* Sidecar遅延解放を導入
* Group全体のPool間Copyを廃止
* Certificateを経験的検証付き許容判定として再定義
* Hard SafetyとSoft Predictionを分離
* Certificate Ageを追加
* Block Certificateを増分無効化
* Collider影響範囲を空間的に限定
* Follower依存関係を明示
* Guard Bandの導出規則を追加
* Fast Pathを実測コスト型へ変更
* 決定論性の主張範囲を限定
* 主新規性を三点に絞った
* 既存最適化と安全機構を明確に分離
* Frame Time基準へ統一

---

## 32.3 現時点の判定

企画・設計段階としては、次の評価とする。

> **かなり安全に近づいた、条件付き安全候補**

ただし、設計書だけで新規性が確定するわけではない。

最終的に「安全」と判定するには、次を満たす必要がある。

1. B8より有意な改善を示す
2. A1でDynamic Sidecarの効果を示す
3. A3で増分Block Invalidationの効果を示す
4. A10でFollower局所再構築の効果を示す
5. Certificate Violation率を許容範囲へ抑える
6. False StaticをGround Truthで評価する
7. 全動的時にBaselineより大幅に悪化しない
8. 100,000本条件でEnd-to-End効果を示す
9. 決定性試験を通過する
10. 長時間動作で異常が発生しない
11. 最新関連研究で同一構成が先行発表されていない

---

# 33. 論文で使用する推奨表現

## 33.1 研究目的

> 本研究では、髪一本を構成するSegment Groupを局所的な実行単位とし、各Groupの状態に応じて固定Reusable CoreとDynamic Sidecarを切り替えるGPU常駐型髪表現を提案する。さらに、経験的に検証したAdmissibility Certificateを用いて静的再利用の許容性を判定し、Guideの局所更新範囲をFollower再構築および描画更新範囲まで伝播させることで、100,000本規模の髪表示におけるEnd-to-End処理削減を目指す。

---

## 33.2 本研究の貢献

> 本研究の貢献は三点である。第一に、Segment Groupごとに固定Reusable CoreとDynamic Sidecarを切り替える状態依存Payload表現を導入した。第二に、GroupおよびBlock単位のAdmissibility Certificateと増分無効化を組み合わせ、静的領域に対する判定処理とSolver処理を省略する構造を設計した。第三に、Guide Groupの局所状態をFollower依存範囲と描画更新範囲へ伝播し、SimulationのみではなくEnd-to-Endの局所更新を可能にした。

---

## 33.3 新規性の安全な主張

> 本研究は、Sleeping、Cache Reuse、Guide/Follower、Dirty Rangeなどの既存要素そのものを新規性として主張するものではない。本研究の新規性は、これらをSegment Group単位の状態依存Payload、経験的検証付きAdmissibility Certificate、Followerおよび描画への依存伝播を中心とする一つのGPU常駐型髪表現として統合した点にある。

---

# 34. 今後の展望

## 34.1 GTX 1660級実機検証

現在は理論換算に留め、将来的に同一Asset、同一設定、同一シナリオで実機測定する。

---

## 34.2 1024点への拡張

* Super Block
* Hierarchical Certificate
* Segment LOD
* Multi-resolution Sidecar

を検討する。

---

## 34.3 Certificate自動校正

Shadow Dynamicの誤差分布から、閾値と重みを自動調整する。

ただし、安全判定は明示的なHard SafetyとFallbackを維持する。

---

## 34.4 Collision-aware Prediction

Collider速度、方向、Swept Volumeから将来の衝突余裕を予測する。

---

## 34.5 編集システム統合

Brush、Gizmo、一本単位編集をDirty Sourceへ直接接続する。

---

## 34.6 Asset Pipeline

Maya、Alembic、USDなどから入力されたstrandを等間隔Segment Groupへ変換し、Dependency、Rest Length、Block AABBを事前生成する。

---

# 35. 結論

本提案は、Segment GroupごとにStaticとDynamicを切り替えるだけの方式ではない。

すべてのGroupが固定Reusable Coreを保持し、Dynamic状態に必要な情報だけをSidecarとして追加する。

静的再利用の許容性は、位置変化、速度、拘束残差、境界誤差、Root変換、衝突余裕などから構成される経験的検証付きAdmissibility Certificateで判定する。

Block Certificateは毎フレーム全Groupから再構築せず、Root、Collider、外力、編集などのDirty Sourceから影響範囲だけを増分無効化する。

さらに、Guide Groupの局所更新をFollower依存範囲、Follower Segment Range、Render Vertex Rangeへ伝播し、物理計算だけではなく、Follower再構築と描画準備を含むEnd-to-End処理を削減する。

Dynamic割合が高い場合は、GPU実測コストに基づいてFull Dynamic Fast Pathへ退避する。

本研究の中心的な提案は、次の三点である。

1. Segment Group単位の固定Core＋Dynamic Sidecar表現
2. 増分Block Invalidationを含むAdmissibility Certificate
3. GuideからFollower・描画まで続くEnd-to-End局所更新

これらをGround Truth、Shadow Dynamic、Strongest Practical Baseline、アブレーション、決定性試験、長時間試験によって検証する。

最終的な研究目標は、静的領域が存在する状況で物理的整合性を維持しながら、100,000本規模の髪表示におけるFrame Timeと更新量を削減することである。
