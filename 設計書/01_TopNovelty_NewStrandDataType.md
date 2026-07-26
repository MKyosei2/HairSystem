# Novelty 01 企画書兼設計書

# Segment-Group Adaptive Payload Strand Type

## 経験的許容判定に基づくGPU常駐型・状態依存髪データ表現

---

# 0. 文書情報

| 項目                   | 内容                                               |
| -------------------- | ------------------------------------------------ |
| 対象リポジトリ              | `MKyosei2/HairSystem`                            |
| 対象ブランチ               | `main`                                           |
| 基準バージョン              | Version 66                                       |
| 基準コミット               | `8929d4685e20ed7637fbe86819ea9fe20b3d0837`       |
| 文書種別                 | Novelty 01 企画書兼設計書                               |
| 研究テーマ                | 新しい状態依存型Strand Data Type                         |
| 正式名称                 | Segment-Group Adaptive Payload Strand Type       |
| 略称                   | SG-APST                                          |
| 対象環境                 | Windows / Visual Studio 2022 / DirectX 11 / HLSL |
| 表示本数                 | 100,000 strands                                  |
| Guide本数              | 4,000 strands                                    |
| Follower本数           | 96,000 strands                                   |
| 点数                   | 128 points / strand                              |
| Guide Segment Group数 | 4,000 × 127 = 508,000 groups                     |
| 表示点数                 | 12,800,000 points                                |
| 解像度                  | Full HD                                          |
| 時間刻み                 | 1/60秒固定                                          |
| Solver反復数            | 6回固定                                             |
| 旧世代GPU評価             | GTX 1660級を想定した理論換算                               |
| 現在の実装状態              | 基盤・試作段階。正式なNovelty 01は未完成                        |
| 現時点の新規性判定            | 危ない。ただし安全に近い条件付き候補                               |

---

# 1. 企画概要

本研究では、一本の髪を構成する点列を、隣接する二点とその間の線分からなるSegment Groupへ分割する。

各Segment Groupは、単なる幾何形状や制約計算の単位ではなく、実行時に次の処理経路を選択する最小単位として扱う。

* 最後に確定した形状をそのまま再利用する
* Rootまたは親Transformの剛体変換だけを適用する
* 通常の物理Solverを実行する
* 局所的な境界修復を実行する
* 異常時に通常のFull Dynamic Solverへ戻る

本研究の中心は、髪を点・線分・点で表現することではない。

また、髪の静止部分を再計算しないこと、Sleeping、Cache Reuse、Guide/Follower、LOD、Dirty Rangeを使用すること自体も新規性とはしない。

本研究で提案する中心的な新規性は、次の一つである。

> 各Segment Groupを、固定配置されたReusable Coreと、Dynamic状態でのみ実体化するOptional Dynamic Sidecarから構成される状態依存型データとして表現し、そのデータ型自身がStatic Reuse、Rigid Transform、Dynamic Solve、Repair、Fallbackの実行経路を選択できるGPU常駐型Strand Representation。

Admissibility Certificate、Block Invalidation、Follower Dependency Propagation、Render Dirty Rangeは、それぞれ独立した新規性として過度に主張するのではなく、この新しいデータ型を安全かつEnd-to-Endで成立させるための必須機構として位置づける。

---

# 2. 研究背景

## 2.1 高密度髪シミュレーションの問題

髪は多数の細長いstrandから構成される。

一本のstrandを128点で表現し、100,000本を表示する場合、表示点数は12,800,000点となる。

各点または線分に対して、毎フレーム次の処理を行うと大きな負荷が発生する。

* 外力積分
* Root追従
* 距離制約
* 曲げ制約
* ねじれ制約
* Colliderとの衝突判定
* 自己衝突
* 局所座標Frame更新
* GuideからFollowerへの形状再構築
* 描画用頂点生成
* VisibilityおよびLOD判定

しかし、実際の髪では、一本の髪全体が常に同じ強さで動いているとは限らない。

例えば、次のような局所状態が発生する。

* Root付近だけが頭部運動の影響を受ける
* 毛先だけが肩や衣服に接触する
* 中間部だけが風の影響を受ける
* 編集対象周辺だけが変形する
* 一部のSegmentだけが破綻する
* 一本の髪の中にStatic領域とDynamic領域が混在する

このような場合でもstrand全体をDynamicとして処理すると、変化していない領域に対して不要な計算が発生する。

一方、strand全体をStaticとして扱うと、衝突漏れ、境界不連続、長さ誤差、編集反映漏れが発生する。

---

## 2.2 既存方式との関係

本研究は、以下の既存技術を基盤として使用する。

* 点列およびSegmentによる髪表現
* Discrete Rod
* Position Based Dynamics
* XPBD
* Guide/Follower
* WispおよびCluster
* Sleeping
* Cache Reuse
* Active Set
* Dirty Range
* LOD
* GPU Compute
* Prefix Sum
* Compaction
* Indirect Dispatch
* Indirect Draw
* Persistent Buffer
* Watchdog
* Fallback

これらの要素を使用すること自体は新規性として主張しない。

本研究との差分は、これらをSegment Groupごとの状態依存データ型として統合し、保持Payload、Cache Validity、物理処理経路、Follower更新範囲、描画更新範囲を一つのランタイム表現から制御する点にある。

---

# 3. 研究目的

本研究の目的は、100,000本、128点、Full HDの髪表示において、変化していない局所領域の物理計算、Follower再構築、描画準備を省略できる状態依存型の髪データ表現を構築することである。

ただし、単純に処理を省略するのではなく、次の条件を満たすことを必須とする。

1. Static化したGroupの形状誤差を評価できる
2. 衝突や編集が発生した場合は即座にDynamicへ戻せる
3. StaticとDynamicの境界を決定論的に処理できる
4. Groupの状態変更時に大規模Buffer Copyを発生させない
5. Dynamic GroupだけがDynamic専用Payloadを持つ
6. Static Groupでは最後に確定した結果を再利用する
7. Guideの局所更新をFollowerと描画まで伝播できる
8. Dynamic割合が高い場合は通常Solverへ退避できる
9. 提案方式の管理コストがBaselineを恒常的に上回らない
10. 異常時に安全な通常経路へ戻れる

---

# 4. 研究仮説

## H1：Segment Group単位の処理選択

一本のstrand内部にStatic領域とDynamic領域が混在する場合、strand全体を一つの処理状態にする方式よりも、Segment Group単位で処理経路を選択した方が更新対象を削減できる。

## H2：状態依存Payloadによるメモリと帯域の削減

Static Groupでは、速度、Previous Position、Solver Lambda、衝突計算用一時値などのDynamic専用情報を保持する必要がない。

固定Reusable CoreとOptional Dynamic Sidecarを分離することで、Dynamic Groupだけに必要なPayloadを実体化できる。

## H3：増分許容判定によるStatic再利用

位置変化、速度、加速度、拘束残差、境界誤差、Root変換量、Collider接近量などを用いてStatic再利用の可否を判定することで、単純な移動量判定よりFalse Staticを抑制できる。

## H4：Block単位の早期省略

毎フレーム全508,000 Groupを評価するのではなく、変更原因から影響Blockだけを無効化することで、変化していないBlockではGroup単位判定自体を省略できる。

## H5：Followerおよび描画への局所更新伝播

Guideだけを局所更新しても、96,000本のFollowerを毎フレーム全再構築した場合は十分な高速化にならない。

Guide GroupのDirty RangeをFollower Segment RangeとRender Vertex Rangeへ伝播することで、SimulationだけでなくEnd-to-End Frame Timeを削減できる。

## H6：全動的条件での負の高速化防止

強風や大規模衝突によってDynamic Groupが増えた場合、Certificate、分類、Compaction、Sidecar管理の追加コストが不利益になる。

GPU実測時間に基づいてFull Dynamic Fast Pathへ切り替えることで、提案方式がBaselineより大幅に遅くなることを防止できる。

---

# 5. 新規性の主張範囲

## 5.1 主新規性

本研究の主新規性は一つに限定する。

> Segment Groupごとに、固定Reusable Coreと必要時のみ実体化するDynamic Sidecarを持ち、Cache ValidityとRuntime Stateに応じて保持Payloadと実行経路が変化するSegment-Group Adaptive Payload Strand Type。

このデータ型は、単なるStatic/Dynamicフラグではない。

状態によって次が変化する。

* 保持するデータ
* 使用するGPU Buffer
* 実行するCompute Pass
* Cacheの再利用可否
* Shared Pointへの書き込み権限
* Followerの更新範囲
* Render Bufferの更新範囲
* Fallback先

---

## 5.2 主新規性を成立させる必須機構

次の二つは、主新規性であるデータ型を成立させる必須構成要素とする。

### A. Incremental Admissibility Control

GroupおよびBlockのCache Validityを経験的な許容判定によって管理し、Dirty Sourceから影響範囲だけを増分無効化する。

### B. End-to-End Dependency Propagation

Guide Groupの状態変更を、Follower Dependency Range、Follower Segment Range、Render Vertex Rangeまで伝播する。

---

## 5.3 補助的新規性候補

Deterministic Hemisphere Shared-Point Resolveは、実験結果によって扱いを決定する。

次の改善が有意に確認できた場合のみ、補助的新規性として扱う。

* Static/Dynamic境界誤差の減少
* GPU実行順序による差の減少
* 再実行時Hash一致率の向上
* Repair発火数の減少

効果が小さい場合は、Engineering Safeguardとして扱う。

---

## 5.4 新規性として主張しない要素

以下は既存技術または一般的な最適化として扱う。

* 点・線分・点による表現
* 等間隔Resample
* PBD
* XPBD
* Rod Solver
* Guide/Follower
* Sleeping
* Cache Reuse
* Dirty Range
* Active Set
* LOD
* GPU Compute
* Prefix Sum
* Atomic Counter
* Stable Compaction
* Indirect Dispatch
* Indirect Draw
* Persistent Buffer
* Frustum Culling
* Screen-space LOD
* Hysteresis
* Watchdog
* Ground Truth比較
* Telemetry
* GPU Timer
* Full Dynamic Solver
* Full Dynamic Fast Path

---

# 6. Segment Groupの定義

## 6.1 基本構造

strandを次の点列として表す。

[
S = {P_0, P_1, \ldots, P_{N-1}}
]

Segment Group (G_i) を次のように定義する。

[
G_i = (P_i, E_i, P_{i+1})
]

ここで、

[
E_i = P_{i+1} - P_i
]

とする。

128点のstrandでは、127個のSegment Groupを持つ。

[
N_g = 128 - 1 = 127
]

---

## 6.2 子Groupの始点

隣接するGroupは共有点を持つ。

[
G_i = (P_i,E_i,P_{i+1})
]

[
G_{i+1} = (P_{i+1},E_{i+1},P_{i+2})
]

子Group (G_{i+1}) は、共有点 (P_{i+1}) を独立に積分しない。

子Groupの始点は、親GroupまたはShared-Point Resolveによって確定した位置を読み取り専用の入力として使用する。

子Groupが主に計算する対象は終点 (P_{i+2}) とする。

これにより、隣接Groupによる共有点への二重書き込みを防ぐ。

---

## 6.3 等間隔Resample

一本のstrandの全長を (L)、Group数を (N_g) とした場合、基準Segment長を次のように定義する。

[
l_0 = \frac{L}{N_g}
]

初期化時またはAsset生成時に、各strandを弧長に基づいて等間隔へResampleする。

髪ごとに全長が異なることは許可する。

ただし、同一strand内では原則としてSegment長を統一する。

Asset形状の都合で完全等長化が適さない場合のみ、GroupごとのRest Lengthを保持する互換モードを使用する。

正式評価では、次を別条件として扱う。

* Uniform Rest Length
* Per-Group Rest Length

---

# 7. 新しいデータ型

## 7.1 概念定義

Segment-Group Adaptive Payload Strand Typeは、次の要素から構成する。

```text
SegmentGroupAdaptivePayload
├─ Fixed Reusable Core
├─ Runtime State
├─ Cache Validity
├─ Version Information
├─ Admissibility Summary
├─ Optional Dynamic Sidecar
├─ Shared-Point Ownership
├─ Follower Dependency
└─ Render Dependency
```

---

## 7.2 Fixed Reusable Core

すべてのGuide Segment Groupが固定Indexで保持する。

```cpp
struct SegmentGroupCore
{
    uint strandId;
    uint localGroupIndex;

    uint startPointIndex;
    uint endPointIndex;

    uint parentGroupIndex;
    uint childGroupIndex;

    uint ownerGroupIndex;
    uint blockIndex;

    uint state;
    uint previousState;

    uint dynamicSidecarIndex;
    uint dirtyMask;

    float restLength;
    float cachedDirectionX;
    float cachedDirectionY;
    float cachedDirectionZ;

    float maximumObservedPositionError;
    float maximumObservedBoundaryError;
    float minimumObservedCollisionMargin;

    uint stateEpoch;
    uint payloadVersion;
    uint certificateVersion;
    uint dependencyVersion;

    uint lastSolvedFrame;
    uint lastValidatedFrame;
    uint lastTransitionFrame;
    uint minimumResidenceEndFrame;
};
```

最終的な実装では、必要に応じて`cachedDirection`をOctahedral Encodingなどで圧縮する。

ただし、圧縮による誤差が新規性評価へ混入しないよう、最初はfloat32を使用したReference Layoutを実装する。

---

## 7.3 Static形状の保持

Static Groupの線分は、始点、方向、Rest Lengthから復元する。

[
P_{i+1} = P_i + l_i d_i
]

ここで、

* (P_i)：親GroupまたはRootから得た始点
* (l_i)：Rest Length
* (d_i)：最後にDynamic Solverで確定した方向

とする。

Static Groupでは、速度やPrevious Positionを保持せず、最後に確定した方向と長さをReusable Coreから復元する。

---

## 7.4 Dynamic Sidecar

Dynamic状態でのみ必要な情報をSidecarとして保持する。

```cpp
struct DynamicGroupSidecar
{
    float previousEndX;
    float previousEndY;
    float previousEndZ;

    float velocityEndX;
    float velocityEndY;
    float velocityEndZ;

    float distanceLambda;
    float bendLambda;

    float constraintResidual;
    float collisionResidual;
    float predictedPositionError;

    uint solverFlags;
    uint collisionFlags;
    uint allocationEpoch;
};
```

始点は親Groupの確定位置を使用するため、子Group側でPrevious Startを重複保持しないことを基本方針とする。

ただし、Solverの品質上必要な場合は、Shared-Point Candidate用の一時Bufferとして始点履歴を保持する。

---

## 7.5 データ型の不変条件

SG-APSTは次の不変条件を持つ。

1. Group CoreのGPU Indexは実行中に変更しない
2. Group Core全体をStatic PoolとDynamic Poolの間でCopyしない
3. Dynamic状態でのみ有効なSidecar Indexを持つ
4. Static状態のSidecar IndexはInvalidとする
5. Groupの始点は子Groupが直接所有しない
6. Shared Pointの最終書き込みPassは一つだけとする
7. Cache Versionが一致しないStatic Groupは使用しない
8. Topology変更時は関連Groupを必ずInvalidまたはDynamicへ戻す
9. NaNまたはInfを含むGroupはStaticとして再利用しない
10. Follower CacheのVersionが一致しない場合は局所または全再構築へ戻す

---

# 8. Group状態

Groupは最低限、次の状態を持つ。

```text
StaticReuse
StaticTransform
Dynamic
Repair
ForcedDynamic
Invalid
```

## 8.1 StaticReuse

最後に確定した形状をそのまま再利用する。

次の条件をすべて満たす場合に使用する。

* Root Transformが変化していない
* 外力Versionが一致している
* Collider Versionが一致している
* Edit Versionが一致している
* Topology Versionが一致している
* Certificateが有効
* Certificate Ageが許容範囲内
* 親境界が有効
* Follower Cacheが有効

## 8.2 StaticTransform

内部形状を再計算せず、Rootまたは親Transformの剛体変換だけを適用する。

使用可能な変換は次に限定する。

* 平行移動
* 回転
* 一様Scaleを許可する場合は別評価

非一様ScaleはSegment Lengthと方向を変化させるため、原則としてTopologyまたはGeometry Dirtyとして扱う。

## 8.3 Dynamic

Baselineと同品質のSolverを実行する。

Dynamic Sidecarを保持する。

## 8.4 Repair

次を局所的に補正する。

* Static/Dynamic境界
* Shared Point不整合
* Segment Length誤差
* 軽微なCollider Penetration
* Cache復帰時の速度不連続

Repairで安全に復旧できない場合はForcedDynamicへ移行する。

## 8.5 ForcedDynamic

安全性上の理由から、通常のStatic判定を無視してDynamicへ戻す。

発火条件は次とする。

* Collision Penetration
* User Edit
* Topology変更
* Payload不整合
* Sidecar Index不整合
* Certificate Version不一致
* Segment Length異常
* Shared Point未解決
* Follower Cache不整合
* NaN
* Inf
* Watchdog発火

## 8.6 Invalid

入力、Topology、Version、Index、Dependencyに不整合がある状態。

Invalid GroupはStaticとして使用しない。

修復できない場合は、Strand、Guide、Frameのいずれかの単位でBaselineへFallbackする。

---

# 9. Root移動と髪全体の移動

## 9.1 禁止する方式

Rootから毛先へ向かって、Group番号に比例したXYZ差分を加算してはならない。

例えば、

```text
P_i' = P_i + i × ΔXYZ
```

のような方式は禁止する。

この方式では隣接点間の移動量が異なるため、Segment Lengthが変化し、髪が不自然に伸長または収縮する。

---

## 9.2 正しい剛体変換

最後に検証したRoot Transformを (T_r^{old})、現在のRoot Transformを (T_r^{new}) とする。

差分変換を次のように定義する。

[
\Delta T_r = T_r^{new}(T_r^{old})^{-1}
]

Static Transform Groupの位置は次式で更新する。

[
P_i^{new} = \Delta T_r P_i^{old}
]

方向は回転成分だけを適用する。

[
d_i^{new} = R(\Delta T_r)d_i^{old}
]

これにより、髪全体が移動してもSegment Lengthを維持できる。

---

## 9.3 Root相対表現

可能な範囲でStatic CacheをRoot Local Spaceに保持する。

```text
World Position
= Current Root Transform
× Cached Root-Local Position
```

Rootだけが移動し、Colliderや外力も同一Transformへ追従する場合は、Simulation Buffer全体を書き換えず、描画時にRoot Transformを適用する経路も評価する。

---

## 9.4 移動時の衝突確認

Rootが移動する場合、現在位置だけでなく移動区間を考慮する。

* Root Swept AABB
* Group Swept AABB
* Collider Swept AABB
* Broad Phase Intersection
* Spatial Hash
* Uniform Grid

相対的な衝突可能性があるBlockだけを無効化する。

すべてのGroupを無条件にDynamicへ戻してはならない。

---

# 10. 状態遷移

## 10.1 DynamicからStaticへの移行

一度閾値を満たしただけではStatic化しない。

次をすべて満たした場合にStatic候補とする。

```text
Certificate合格がKフレーム連続
かつ
Minimum Residenceを満たす
かつ
Transition Budget内
かつ
親Group境界が安定
かつ
Follower Dependencyが有効
```

---

## 10.2 StaticからDynamicへの移行

Hard Safety違反時は即時にDynamicへ戻す。

安全上必要なStaticからDynamicへの移行は、Transition Budgetで遅延させない。

---

## 10.3 Hysteresis

DynamicからStaticへ移行する閾値と、StaticからDynamicへ戻る閾値を分ける。

```text
Dynamic → Static候補:
predictedError < 0.5 × allowedError
constraintResidual < 0.5 × allowedResidual
velocity < staticVelocityThreshold

Static → Dynamic:
predictedError >= allowedError
constraintResidual >= allowedResidual
collisionMargin <= requiredMargin
```

---

## 10.4 Minimum Residence Time

状態変更後、一定フレームは同じ状態を維持する。

ただし、次の場合はResidenceを無視する。

* Collision Penetration
* User Edit
* Fatal
* Invalid
* Watchdog
* Topology変更
* NaNまたはInf

---

## 10.5 Transition Budget

一フレーム内の状態変更量を制限する。

```text
maxDynamicToStaticPerFrame
maxSidecarAllocationsPerFrame
maxSidecarRetiresPerFrame
maxTransitionBytesPerFrame
```

Budgetを超えた場合、DynamicからStaticへの移行を延期する。

StaticからDynamicへの安全上必要な移行は延期しない。

---

# 11. Sidecar Pool

## 11.1 基本方針

毎フレームの個別AllocationとFreeを禁止する。

固定容量のPoolを使用する。

```text
Free
→ Active
→ Retired
→ SafeToReuse
→ Free
```

## 11.2 一括割当

Dynamic化するGroupをCompactionし、Prefix Sumまたは一括CounterによってSidecar Indexを割り当てる。

正式方式では、Atomic到着順に依存する割当とStable Prefix Sum方式を比較する。

## 11.3 遅延解放

Static化したGroupのSidecarは即時再利用しない。

Frame FenceまたはEpochを使用し、GPUが参照を終了したことを確認した後でFree Listへ戻す。

## 11.4 Overflow

Sidecar Poolが不足した場合は、次の優先順位で処理する。

1. 新規Static化を停止
2. Full Dynamic Baseline Bufferを使用
3. Frame Full Dynamic Fast Pathへ移行
4. TelemetryへOverflowを記録
5. 継続不能な場合のみSafe Stop

---

# 12. Empirically Validated Admissibility Descriptor

## 12.1 名称

数学的な厳密証明ではないため、主名称に`Proof`や`Guaranteed Error Bound`を使用しない。

正式名称は次とする。

> Empirically Validated Admissibility Descriptor

日本語では次のように表記する。

> 経験的検証付き静的再利用許容記述子

論文中で短縮する場合は`Admissibility Descriptor`または`Certificate`と記載するが、数学的証明ではないことを明示する。

---

## 12.2 Hard SafetyとSoft Prediction

### Hard Safety

一つでも違反した場合、必ずStaticを解除する。

* NaN
* Inf
* Collision Penetration
* Segment Length異常
* Topology変更
* Version不一致
* Payload不整合
* Sidecar Index不整合
* Shared Point未解決
* User Edit
* Follower Cache不整合
* Dependency範囲外

### Soft Prediction

Staticを継続できる可能性を評価する。

* 位置予測誤差
* 速度
* 加速度
* 拘束残差
* 境界誤差
* Root変換量
* 外力変化
* Collider接近量
* Certificate Age
* Shadow Dynamic履歴

---

## 12.3 予測誤差

Group (G_i) の予測誤差を次のように定義する。

[
\hat{E}*i =
w_pE*{position}
+w_vE_{velocity}
+w_aE_{acceleration}
+w_cE_{constraint}
+w_bE_{boundary}
+w_rE_{root}
+w_fE_{force}
+w_hE_{history}
]

すべての項を同一の長さ単位へ正規化する。

単位の異なる値をそのまま加算してはならない。

速度による予測変位は次式とする。

[
E_{velocity}=|v|\Delta t
]

加速度による予測変位は次式とする。

[
E_{acceleration}=\frac{1}{2}|a|\Delta t^2
]

Root回転による変位は、Rootからの距離 (d) と回転角 (\theta) を用いて次のように近似する。

[
E_{root}=2d\sin\left(\frac{\theta}{2}\right)
]

---

## 12.4 Collision Margin

Colliderとの最小距離を (m_{col}) とする。

次を満たさないGroupはStatic化しない。

[
m_{col}>
E_{velocity}
+E_{acceleration}
+E_{colliderMotion}
+\tau_{collisionSafety}
]

高速移動Colliderに対しては、現在距離だけでなくSwept Volumeを使用する。

---

## 12.5 Certificate Age

Static Cacheを永久に再利用しない。

[
Age_i=currentFrame-lastValidatedFrame
]

次の場合は再検証する。

* Ageが最大値を超えた
* Root Transformが変化した
* 外力Versionが変化した
* Colliderが接近した
* 隣接GroupがDynamicになった
* Boundary Errorが増加した
* Shadow Dynamic誤差が増加した
* 編集が発生した

---

# 13. Incremental Block Invalidation

## 13.1 目的

毎フレーム508,000 GroupすべてのCertificateを評価すると、Static化によってSolverを省略しても判定コストが残る。

そのため、連続するGroupをBlockとして管理し、変更がないBlockではGroup単位判定も省略する。

---

## 13.2 Blockサイズ

初期候補を次とする。

* 32 Groups
* 64 Groups

正式なBlockサイズは性能評価で決定する。

---

## 13.3 Block Certificate

```cpp
struct BlockCertificate
{
    float maximumPredictedError;
    float maximumConstraintResidual;
    float maximumBoundaryError;
    float minimumCollisionMargin;

    uint dynamicCount;
    uint repairCount;
    uint invalidCount;

    uint rootVersion;
    uint forceVersion;
    uint colliderVersion;
    uint topologyVersion;
    uint settingsVersion;
    uint dependencyVersion;

    uint lastValidatedFrame;
    uint dirtyMask;
    uint validityFlags;
};
```

---

## 13.4 Dirty Source

Blockを無効化する原因を次に分類する。

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

## 13.5 空間的無効化

Colliderが移動しただけで全Blockを無効化してはならない。

```text
Dirty Source
→ Swept AABB
→ Spatial Query
→ Affected Block List
→ Block Certificate Invalidation
```

使用候補は次とする。

* Block AABB
* Collider Swept AABB
* Spatial Hash
* Uniform Grid
* Root Binding Range
* Edit Brush Range

---

## 13.6 Block Skip

有効なStatic Blockでは、次を省略する。

* Group Certificate更新
* State Candidate生成
* Dynamic List登録
* Sidecar Allocation
* Constraint Solver
* Collision Solver
* Shared-Point Candidate生成
* Guide形状再生成
* Follower再構築
* Render Vertex再生成

Root Transformだけが変化した場合はStatic Transform専用経路を使用する。

---

# 14. Shared-Point Resolve

## 14.1 問題

隣接Groupは点を共有する。

複数Groupが同じPoint Bufferへ直接書き込むと、GPU実行順序によって結果が変化する。

---

## 14.2 半球責任範囲

共有点を物理的に二つへ複製しない。

共有点周辺の処理候補を論理的にRoot側とTip側へ分ける。

* Root側GroupはRoot方向の制約候補を生成する
* Tip側GroupはTip方向の制約候補を生成する

これは幾何的な点の分割ではなく、候補生成責任の分割である。

---

## 14.3 Candidate

```cpp
struct SharedPointCandidate
{
    float positionX;
    float positionY;
    float positionZ;

    float positionError;
    float constraintResidual;
    float collisionResidual;

    uint priority;
    uint sourceGroupId;
    uint reasonFlags;
};
```

---

## 14.4 優先順位

```text
Fatal / Invalid Reject
>
Collision Escape
>
User Edit
>
Forced Dynamic
>
Watchdog Recovery
>
Dynamic Solver
>
Boundary Repair
>
Static Transform
>
Static Reuse
```

同一Priorityでは次の順で決定する。

1. 有限値である
2. Position Errorが小さい
3. Constraint Residualが小さい
4. Collision Residualが小さい
5. Source Group IDが小さい

---

## 14.5 Single Writer

最終Positionを書き込むPassは一つだけとする。

```text
Candidate Generation
→ Stable Reduce
→ Apply
```

Atomicの到着順によって最終候補を決定しない。

---

# 15. Dynamic Solver

Dynamic Groupには、Baselineと同品質のSolverを使用する。

提案方式を高速に見せるために、Dynamic Groupの品質を下げてはならない。

最低限、次を含める。

* Integration
* Root Constraint
* Distance Constraint
* Bend Constraint
* External Force
* Collider Constraint
* Boundary Repair

BaselineとProposed Methodで次を一致させる。

* 時間刻み
* 反復回数
* 制約式
* Compliance
* Collision条件
* Root条件
* 数値精度
* Solver順序

現在の簡易的な「方向をRest Lengthへ戻すだけ」のGroup Solverは正式方式として扱わない。

正式実装では、現在のFull Dynamic Baselineと同じDistance、Bend、Collision品質をGroup経路へ接続する。

---

# 16. Follower全再構築の廃止

## 16.1 成立条件

4,000本のGuideを部分更新しても、96,000本のFollowerを毎フレーム全再構築した場合、Novelty 01の効果は失われる。

したがって、Follower局所再構築は補助最適化ではなく、100,000本表示におけるSG-APSTの成立条件とする。

---

## 16.2 Dependency表現

固定長配列ではなく、GPUで扱いやすいCSR形式を基本候補とする。

```text
GuideGroupToFollowerOffset[]
GuideGroupToFollowerEntry[]
FollowerDependencyMetadata[]
```

```cpp
struct FollowerDependencyEntry
{
    uint followerId;
    uint sourceGuideId;

    uint sourceSegmentBegin;
    uint sourceSegmentEnd;

    uint targetSegmentBegin;
    uint targetSegmentEnd;

    uint guardBand;
    uint dependencyVersion;
};
```

---

## 16.3 Guard Band

Follower生成で参照する範囲からGuard Bandを導出する。

* 対象Segmentだけを参照：0
* 前後1点からTangentを計算：1
* 前後2点でSmoothing：2
* 複数Guide補間：影響GuideすべてのDirty RangeをUnion

固定の経験値だけで決定しない。

---

## 16.4 Dirty Range伝播

```text
Guide Group Dirty
→ Dependency Lookup
→ Affected Follower
→ Target Segment Range Union
→ Guard Band Expansion
→ Follower Dirty Range
→ Render Dirty Range
```

---

## 16.5 Follower更新モード

### Cache Reuse

依存GuideがすべてStaticReuseで、Versionが一致する場合、Follower Cacheをそのまま使用する。

### Rigid Transform

依存GuideがStaticTransformで内部形状が変わっていない場合、同一Root Transformを適用する。

### Local Reconstruct

Dirty Segment RangeとGuard Bandだけを再構築する。

### Full Reconstruct

次の場合は対象Follower全体を再構築する。

* Dependency Version不一致
* Guide/Follower Mapping変更
* Topology変更
* LOD変更
* Cache不整合
* 大規模Edit
* Watchdog発火
* Dirty Range計算失敗

---

# 17. 描画への局所更新伝播

## 17.1 方針

Simulationの削減効果が描画ボトルネックに隠れないよう、Follower Dirty Rangeを描画用頂点範囲へ伝播する。

```text
Follower Segment Dirty
→ Render Vertex Range
→ Persistent Vertex Cache Invalidation
→ Dirty Range Vertex Regeneration
→ Indirect Draw Data Update
```

Static Rangeは前フレームのRender Bufferを再利用する。

---

## 17.2 Root Transformのみの場合

Rootだけが剛体移動した場合、可能であれば全頂点を書き換えず、Vertex ShaderまたはInstance Transformで移動する。

ただし、相対Collider、風、編集が変化した範囲はDynamicへ戻す。

---

## 17.3 一般的な描画最適化

次はBaselineとProposed Methodの双方に適用する。

* GPU Resident Buffer
* Persistent Vertex Buffer
* Indirect Draw
* Frustum Culling
* Distance LOD
* Screen-space LOD
* Width LOD
* Segment LOD
* CPU Readback排除
* Draw Command Compaction

Proposed Method固有の効果は、Guideから伝播したDirty Rangeによる部分更新として分離評価する。

---

# 18. Full Dynamic Fast Path

## 18.1 目的

Dynamic Groupが多い場合、次のProposal管理処理が追加負荷となる。

* Certificate評価
* Block判定
* Group分類
* Compaction
* Sidecar管理
* Static Transform
* Follower Cache判定
* Dirty Range生成

Proposal経路がFull Dynamic経路より高コストになる場合は、通常Solverへ退避する。

---

## 18.2 コストモデル

Proposal経路を次のように定義する。

[
T_P =
T_{dirty}
+T_{certificate}
+T_{classification}
+T_{compaction}
+T_{sidecar}
+T_{partialSolver}
+T_{followerPartial}
+T_{renderPartial}
]

Full Dynamic経路を次のように定義する。

[
T_F =
T_{fullSolver}
+T_{followerFull}
+T_{renderFull}
]

---

## 18.3 切替条件

固定Dynamic Ratioだけで判定しない。

次を入力とする。

* Dynamic Group Ratio
* Dynamic Block Ratio
* Block Skip率
* Dirty Follower Ratio
* Dirty Render Vertex Ratio
* Transition数
* Sidecar Allocation数
* Collision対象数
* GPU Pass別実測時間
* 移動平均Frame Time
* p95相当の履歴
* Worst Frame Time

Proposal予測時間がFull Dynamic予測時間以上となる状態がKフレーム継続した場合、Fast Pathへ移行する。

Fast Path終了条件にはSafety MarginとHysteresisを設ける。

---

# 19. フレーム処理Pipeline

```text
Phase 01  Frame Input確定
Phase 02  Root / Collider / Force Version更新
Phase 03  Topology / Settings / LOD変更検出
Phase 04  Dirty Source生成
Phase 05  Swept AABBおよびSpatial Query
Phase 06  Affected Block抽出
Phase 07  Block Certificate失効
Phase 08  Fast Path Cost Prediction
Phase 09  Full Dynamic Fast Path判定

通常経路:
Phase 10  Invalid BlockのみGroup Descriptor更新
Phase 11  Hard Safety判定
Phase 12  State Candidate生成
Phase 13  Hysteresis
Phase 14  Minimum Residence
Phase 15  Transition Budget
Phase 16  Sidecar Allocation / Retire Plan
Phase 17  Dynamic / Static / Repair List生成
Phase 18  Dynamic Integration
Phase 19  Dynamic Constraint Solve
Phase 20  Collision Solve
Phase 21  Static Reuse / Static Transform
Phase 22  Shared-Point Candidate生成
Phase 23  Stable Reduce
Phase 24  Shared-Point Apply
Phase 25  Boundary Repair
Phase 26  Guide Dirty Range確定
Phase 27  Follower Dependency伝播
Phase 28  Follower Cache Reuse / Local Reconstruct
Phase 29  Render Dirty Range生成
Phase 30  Render Vertex部分更新
Phase 31  Indirect Draw Data更新
Phase 32  Telemetry更新
Phase 33  Watchdog
Phase 34  Shadow Dynamic検証
```

---

# 20. WatchdogとFallback

## 20.1 監視対象

* NaN
* Inf
* Segment Length異常
* Constraint Residual急増
* Boundary Error急増
* Sidecar Index不整合
* Duplicate Sidecar Allocation
* Certificate Version不一致
* Shared Point未解決
* Follower Cache不整合
* Dirty Range範囲外
* Dispatch Count不整合
* Transition過多
* False Static急増
* Shadow Dynamic誤差急増
* Frame Time異常
* GPU Query未解決
* Memory増加

---

## 20.2 Fallback単位

```text
Group Fallback
Block Fallback
Strand Fallback
Guide Fallback
Frame Full Dynamic Fallback
Application Safe Stop
```

---

## 20.3 原則

Proposal経路が失敗しても、可能な限り通常Solverで描画を継続する。

Proposal固有Bufferに異常が発生した場合でも、Baseline BufferとFull Dynamic Solverへ戻れる経路を維持する。

---

# 21. Ground TruthとShadow Dynamic

## 21.1 Ground Truth

同一入力、同一時間刻み、同一Solver設定で、すべてのGuide GroupをDynamicとして処理した結果をGround Truthとする。

## 21.2 Shadow Dynamic

Static Groupの一部をサンプリングし、表示には使用しないDynamic Solverを並行実行する。

Static結果との差を測定する。

## 21.3 優先サンプリング

* Certificate閾値付近
* Static継続時間が長い
* Collision Marginが小さい
* Static/Dynamic境界
* Root変換量が大きい
* 過去にFalse Staticを発生
* Certificate Ageが大きい

## 21.4 校正と評価の分離

Descriptorの重みや閾値を調整するデータと、正式評価用データを分離する。

同じシナリオで調整と評価を行わない。

正式評価開始前に閾値をFreezeする。

---

# 22. メモリモデル

Guide Group数を (N_G)、Dynamic Group数を (N_D)、Coreサイズを (S_C)、Sidecarサイズを (S_D) とする。

[
M_{group}=N_GS_C+N_DS_D
]

本条件では、

[
N_G=508,000
]

となる。

Dynamic Ratioを (r) とした場合、

[
N_D=rN_G
]

である。

状態依存Payloadの有効性は、次を比較して評価する。

* 全Groupが常にDynamic Payloadを持つ方式
* Static/Dynamicで同一Payloadを持つ方式
* Fixed Core＋Optional Sidecar方式

評価対象は容量だけでなく、毎フレームの読み書き量とGPU帯域とする。

---

## 22.1 初期サイズ目標

初期実装の目標値を次とする。

| データ                           |                          目標 |
| ----------------------------- | --------------------------: |
| Group Core                    |         64～96 bytes / group |
| Dynamic Sidecar               | 32～64 bytes / dynamic group |
| Block Certificate             |         64～96 bytes / block |
| Sidecar Allocation一時情報        |                       必要最小限 |
| Static GroupのDynamic専用Payload |                     0 bytes |

これは設計目標であり、実装後に`sizeof`とGPU Resource Sizeを記録する。

---

# 23. 現在のMain実装との対応

| 機能                     | Version 66の状態 | 正式完了条件                      |
| ---------------------- | ------------- | --------------------------- |
| 100,000本固定条件           | 実装あり          | 維持                          |
| Segment Group Metadata | 試作あり          | Fixed Coreへ統一               |
| Group State            | 簡易試作          | 全状態と優先順位を実装                 |
| Group Certificate      | Boolean中心     | 数値DescriptorとAgeへ変更         |
| Block Metadata         | 試作あり          | 増分失効へ変更                     |
| Static Reuse           | 試作あり          | Descriptor連携                |
| Static Transform       | 平行移動中心        | Root剛体変換へ変更                 |
| Dynamic Solver         | 簡易距離補正        | Baseline同品質化                |
| Dynamic Sidecar        | 未完成           | Pool、Free、Retire、Epoch      |
| Shared-Point Resolve   | 部分試作          | Stable ReduceとSingle Writer |
| Compaction             | Atomic方式あり    | Stable方式と比較                 |
| Follower Cache         | 全再構築中心        | Persistent Cache            |
| Follower Dependency    | 未実装           | CSR Dependency追加            |
| Follower Local Rebuild | 未実装           | Dirty Range Dispatch        |
| Render Dirty Range     | 未完成           | Persistent部分更新              |
| Full Dynamic Fast Path | 通常経路あり        | GPU実測コスト切替                  |
| Shadow Dynamic         | 未完成           | Sampling比較                  |
| Watchdog               | 部分対応          | Proposal全体へ拡張               |

---

# 24. 主な実装対象ファイル

## C++側

* `apps/dx11_viewer/src/GpuHairViewer.h`
* `apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl`
* `apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl`
* `apps/dx11_viewer/src/GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl`
* `apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl`
* `apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl`

## HLSL側

* `apps/dx11_viewer/shaders/HairSegmentGroupRuntime.hlsl`
* `apps/dx11_viewer/shaders/HairSim.hlsl`
* `apps/dx11_viewer/shaders/HairIndirectArgs.hlsl`
* `apps/dx11_viewer/shaders/HairStrandInstanced.hlsl`

## 評価基盤

* `tools/final_stage/hairgpu_final_run.py`
* `tools/final_stage/hairgpu_runtime_suite.py`
* `tools/final_stage/hairgpu_final_db.py`
* `tools/final_stage/hairgpu_novelty01_preflight.py`
* `docs/sample_assets/final_runtime_suite_template.json`
* `docs/experiments/NOVELTY01_SAFE_EVALUATION_PROTOCOL.md`

---

# 25. 比較対象

## B0：Current Full Dynamic Baseline

* 全Guide Dynamic
* Follower全再構築
* Render全更新

## B1：Optimized Full Dynamic Baseline

* 全Guide Dynamic
* GPU Resident
* CPU Readback排除
* Persistent Buffer
* Indirect Draw
* 一般的描画最適化

## B2：Strand-Level Sleeping

strand全体をStaticまたはDynamicにする。

## B3：Block-Level Sleeping

Block単位で更新を省略するが、Group単位の状態依存Payloadは使用しない。

## B4：Segment Group Flag Only

Group単位でStatic/Dynamicフラグを持つが、StaticとDynamicで同一Payloadを使用する。

## B5：Fixed Core＋Dynamic Sidecar

状態依存Payloadを使用するが、Descriptorは簡易閾値とする。

## B6：Group Descriptor

Group単位のDescriptorを使用するが、Block増分失効を使用しない。

## B7：Guide Local Update Only

Guideは局所更新するが、Followerを全再構築する。

## B8：Strongest Practical Baseline

次を可能な限り強く組み合わせる。

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

* Fixed Core＋Optional Dynamic Sidecarの状態依存データ型
* Segment Group DescriptorによるPayloadと経路の統合制御
* Guide GroupからFollowerと描画までの統合Dependency Propagation

## P：Proposed Method

本設計のSG-APSTをすべて使用する。

---

# 26. アブレーション

| ID  | 無効化する要素                         | 確認目的                |
| --- | ------------------------------- | ------------------- |
| A1  | Dynamic Sidecar                 | 状態依存Payloadの効果      |
| A2  | Group Descriptor                | 単純移動量閾値との差          |
| A3  | Incremental Block Invalidation  | 全Group評価との差         |
| A4  | Block Skip                      | 実際のDispatch省略効果     |
| A5  | Hysteresis                      | State Thrashingへの影響 |
| A6  | Minimum Residence               | 遷移頻度への影響            |
| A7  | Transition Budget               | Frame Spikeへの影響     |
| A8  | Stable Shared-Point Resolve     | 境界誤差と決定性            |
| A9  | Boundary Repair                 | 境界不連続への影響           |
| A10 | Follower Dependency Propagation | Follower全再構築との差     |
| A11 | Render Dirty Range              | End-to-End性能        |
| A12 | Cost-Based Fast Path            | 固定比率Fast Pathとの差    |
| A13 | Shadow Dynamic                  | False Static検出      |
| A14 | Watchdog Fallback               | 異常復帰                |
| A15 | Stable Compaction               | 再現性と性能              |

---

# 27. 評価シナリオ

| ID  | シナリオ                            |
| --- | ------------------------------- |
| S01 | 完全静止                            |
| S02 | Root平行移動                        |
| S03 | Root回転                          |
| S04 | 弱い風                             |
| S05 | 通常頭部運動                          |
| S06 | 局所衝突                            |
| S07 | 複数Collider                      |
| S08 | 一本単位編集                          |
| S09 | 連続Brush編集                       |
| S10 | 強風                              |
| S11 | 全動的                             |
| S12 | Static/Dynamic境界Stress          |
| S13 | State Thrashing Stress          |
| S14 | 複数Guide依存Follower               |
| S15 | 長時間動作                           |
| S16 | 決定性                             |
| S17 | Sidecar Pool Stress             |
| S18 | Certificate Invalidation Stress |

---

# 28. 評価指標

## 28.1 性能

* Average Frame Time
* Median Frame Time
* p95 Frame Time
* p99 Frame Time
* Maximum Frame Time
* 50ms以上Frame数
* 100ms以上Frame数
* GPU Simulation Time
* Descriptor Time
* Block Invalidation Time
* Classification Time
* Compaction Time
* Sidecar Allocation Time
* Follower Reconstruction Time
* Render Preparation Time
* Draw Time

## 28.2 更新量

* Dynamic Group数
* StaticReuse Group数
* StaticTransform Group数
* Repair Group数
* Invalid Group数
* Valid Block数
* Invalidated Block数
* Block Skip率
* Sidecar使用数
* State Transition数
* Follower再構築点数
* Render再生成頂点数
* Full Dynamic Fast Path使用率

## 28.3 正確性

* Position RMSE
* Maximum Position Error
* Segment Length Error
* Constraint Residual
* Boundary Discontinuity
* Collision Penetration Depth
* False Static率
* False Dynamic率
* Descriptor Violation率
* Shadow Dynamic差分
* Follower Position Error
* Render Geometry Error

## 28.4 安定性

* State Thrashing率
* Watchdog発火数
* Fallback回数
* NaN数
* Inf数
* Sidecar Index異常数
* Cache不整合数
* Shared Point未解決数
* 実行間Hash一致率
* Long-Run Memory増加量

## 28.5 メモリ

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

# 29. 60FPS正式判定

60FPSの合否はFPS表示値ではなくFrame Timeで判定する。

```text
Average Frame Time <= 16.6667 ms
p95 Frame Time <= 16.6667 ms
p99 Frame Time <= 20.0 ms
Maximum Frame Time <= 50.0 ms
100 ms以上のFrame = 0
```

次を固定する。

* Release x64
* Full HD
* 100,000 strands
* 4,000 Guides
* 96,000 Followers
* 128 points / strand
* 6 Solver iterations
* 1/60秒固定
* 同一Asset
* 同一Camera
* 同一Seed
* 同一Scenario
* 同一Driver条件

VSync、Frame Cap、画面上のFPS表示だけで合否を判断しない。

---

# 30. 60FPS達成見込み

現時点では正式実装およびGTX 1660級実機測定が完了していないため、以下は理論的な企画段階の見積もりとする。

## 30.1 現在のMainをそのまま使用する場合

Follower全再構築とRender全更新が残るため、100,000本、128点、6反復、Full HDで安定60FPSを達成できる確率は低い。

推定：

> 10％未満

## 30.2 SG-APSTを正式実装した場合

次をすべて実装したことを前提とする。

* Dynamic Sidecar
* Incremental Block Invalidation
* Follower Local Rebuild
* Persistent Follower Cache
* Render Dirty Range
* Cost-Based Full Dynamic Fast Path
* GPU Resident処理
* CPU Readback排除
* 一般的描画最適化

代表的な通常シナリオにおける中央見積もりは次とする。

> 約60％

妥当な範囲：

> 45～70％

## 30.3 シナリオ別見込み

| 条件            | 安定60FPS達成見込み |
| ------------- | -----------: |
| 完全静止          |       80～90％ |
| Root剛体移動中心    |       70～85％ |
| 弱い風           |       60～75％ |
| 通常頭部運動        |       55～70％ |
| 局所衝突          |       50～65％ |
| 複数Collider    |       30～50％ |
| 強風            |       20～40％ |
| ほぼ全動的         |       10～30％ |
| S01～S18すべてで合格 |       25～35％ |

この確率は研究結果ではなく、設計時点の成功可能性評価である。

論文の結果章では実測値だけを使用する。

---

# 31. GTX 1660級評価

GTX 1660級GPUでの数値は現時点では理論換算とする。

実測値として主張しない。

開発機で次をPass別に計測する。

* Compute-bound Pass
* Memory-bound Pass
* Group Descriptor
* Block Invalidation
* Compaction
* Sidecar管理
* Guide Solver
* Follower Reconstruction
* Render Preparation
* Draw

その後、GPU Compute性能、Memory Bandwidth、世代差を用いて理論換算する。

論文では次の位置づけとする。

* 今後の展望
* 旧世代GPUへの適用可能性
* 理論性能換算
* 実機検証計画

---

# 32. 実装段階

## Phase 0：Baseline固定

* 100,000 strands
* 4,000 Guides
* 96,000 Followers
* 128 points
* 6 iterations
* Full HD
* 1/60固定
* Release x64
* Telemetry固定

## Phase 1：Fixed Core

* 固定Group Index
* Parent/Child
* Owner
* State
* Version
* Dirty Mask
* Root Binding

## Phase 2：Dynamic Sidecar

* Pool
* Free List
* Retire List
* Epoch
* Overflow Fallback

## Phase 3：State Machine

* Hysteresis
* Consecutive Pass
* Minimum Residence
* Transition Budget

## Phase 4：Admissibility Descriptor

* Hard Safety
* Soft Prediction
* 単位正規化
* Certificate Age
* Collision Margin

## Phase 5：Incremental Block Invalidation

* Block AABB
* Dirty Source
* Spatial Query
* Affected Block List
* Block Skip

## Phase 6：Shared-Point Resolve

* Candidate
* Stable Reduce
* Single Writer
* 決定性試験

## Phase 7：Dynamic Solver統一

* Baselineと同一品質
* Distance
* Bend
* Root
* Force
* Collision

## Phase 8：Follower Dependency

* CSR Mapping
* Support Range
* Guard Band
* Dirty Range Union

## Phase 9：Follower Cache

* Cache Reuse
* Rigid Transform
* Local Reconstruct
* Full Reconstruct Fallback

## Phase 10：Render Dirty Range

* Persistent Render Cache
* Dirty Vertex Update
* Indirect Draw
* LOD

## Phase 11：Cost-Based Fast Path

* GPU Pass Timer
* Cost Prediction
* Hysteresis切替
* Full Dynamic Fallback

## Phase 12：正式評価

* B0～B8
* Proposed Method
* A1～A15
* S01～S18
* Ground Truth
* Shadow Dynamic
* 決定性試験
* 長時間試験

---

# 33. 実装完了条件

```text
[ ] Group Coreが固定Indexで保持される
[ ] StaticとDynamicの間でGroup全体をCopyしない
[ ] Dynamic専用情報がSidecarへ分離される
[ ] Static GroupがDynamic専用Payloadを保持しない
[ ] Sidecarに遅延解放がある
[ ] Overflow時にBaselineへFallbackできる
[ ] Hysteresisがある
[ ] Minimum Residenceがある
[ ] Transition Budgetがある
[ ] Hard SafetyとSoft Predictionが分離される
[ ] Descriptor各項が同一単位へ正規化される
[ ] Certificate Ageがある
[ ] Block Certificateが増分無効化される
[ ] Collider移動で全Blockを無効化しない
[ ] Block Skipが実際のDispatch省略に使われる
[ ] Shared Pointの最終Writerが一つである
[ ] Stable Reduceが実装される
[ ] Dynamic SolverがBaselineと同品質である
[ ] Follower Dependencyが事前計算される
[ ] Guard Bandが参照範囲から導出される
[ ] Follower全再構築が通常経路から廃止される
[ ] Follower Dirty Rangeが描画へ伝播する
[ ] Render Vertexを局所更新できる
[ ] Root移動を剛体変換として処理する
[ ] XYZ等差加算を使用しない
[ ] Fast PathがGPU実測コストで切り替わる
[ ] WatchdogからBaselineへ退避できる
[ ] Ground Truth比較がある
[ ] Shadow Dynamic比較がある
[ ] False Static率を測定できる
[ ] Block Skip率を測定できる
[ ] Sidecar使用量を測定できる
[ ] Follower再構築点数を測定できる
[ ] Render再生成頂点数を測定できる
[ ] 決定性を測定できる
[ ] B8と比較できる
[ ] 長時間動作試験を通過する
```

---

# 34. 主なリスク

## 34.1 Static割合が低い

### リスク

強風や多数Colliderでは、大部分がDynamicとなる。

### 対策

* Cost-Based Full Dynamic Fast Path
* Block単位早期判定
* Proposal経路の自動停止

## 34.2 Descriptor評価が重い

### リスク

Solverを省略しても、毎フレーム全Groupを評価すると高速化にならない。

### 対策

* Incremental Block Invalidation
* Block Skip
* Descriptor Age
* Dirty Source限定

## 34.3 Sidecar Pool不足

### 対策

* Worst Case容量の評価
* Overflow Telemetry
* Baseline Bufferへの退避
* DynamicからStaticへの遷移停止

## 34.4 State Thrashing

### 対策

* Hysteresis
* Consecutive Pass
* Minimum Residence
* Transition Budget

## 34.5 False Static

### 対策

* Hard Safety
* Collision Margin
* Shadow Dynamic
* Boundary Error
* Descriptor Age
* Watchdog

## 34.6 Descriptorが保守的すぎる

### リスク

ほとんどStatic化されず、管理コストだけが増える。

### 対策

* 閾値感度分析
* 誤差項別Telemetry
* 校正用と評価用データの分離
* B8比較

## 34.7 Follower依存範囲の過小評価

### 対策

* CSR Dependency
* Support Radius
* Guard Band導出
* Full Reconstruct Fallback
* Follower Ground Truth比較

## 34.8 描画ボトルネック

### 対策

* SimulationとEnd-to-Endを分離計測
* Persistent Render Cache
* Render Dirty Range
* Indirect Draw
* LOD
* Overdraw計測

## 34.9 Root移動による伸長

### 対策

* Root差分剛体変換
* Root Local Cache
* Segment Length Watchdog
* XYZ等差加算の禁止

## 34.10 Hemisphere Resolveの効果が小さい

### 対策

* アブレーション
* 境界誤差比較
* 決定性比較
* 効果が小さい場合は安全機構へ格下げ

---

# 35. 新規性判定

## 35.1 現時点

三択では次の判定とする。

> 危ない

ただし、単純なStatic/Dynamic切替案よりは大幅に改善されており、安全に近い条件付き候補である。

---

## 35.2 危ない理由

* 点・線分・点の表現自体は既存
* SleepingとCache Reuseは既存
* Partial UpdateとActive Setは他分野にも存在
* Guide/FollowerとLODは既存
* データ型の正式実装が未完成
* Follower局所再構築が未実装
* Render Dirty Rangeが未完成
* B8との比較結果がない
* Dynamic Sidecarの効果が未実証
* False Static率が正式評価されていない
* 最新先行研究との最終照合が未完了

---

## 35.3 安全へ移行する条件

次をすべて満たした場合、安全判定候補とする。

1. B8より有意なEnd-to-End改善を示す
2. A1でDynamic Sidecarの効果を示す
3. A3でIncremental Block Invalidationの効果を示す
4. A10でFollower局所更新の効果を示す
5. A11でRender Dirty Rangeの効果を示す
6. False Static率をGround Truthで評価する
7. Descriptor Violation率を許容範囲へ抑える
8. 全動的条件でBaselineより大幅に悪化しない
9. 100,000本条件で効果を示す
10. 決定性試験を通過する
11. 長時間動作試験を通過する
12. 最新の髪、粒子、布、流体、剛体、Sparse Simulation研究との最終比較を行う
13. 同一構成の先行方式が存在しないことを確認する

---

# 36. 論文で使用する推奨表現

## 36.1 研究目的

> 本研究では、髪strand内部を構成するSegment Groupを、固定Reusable Coreと必要時のみ実体化するDynamic Sidecarからなる状態依存型データとして表現する。各Groupは、経験的に検証された静的再利用許容記述子に基づいてStatic Reuse、Rigid Transform、Dynamic Solve、Repair、Fallbackの処理経路を選択する。さらに、Guide Groupの局所更新範囲をFollower再構築範囲および描画更新範囲まで伝播し、100,000本規模の髪表示におけるEnd-to-End処理量の削減を目指す。

## 36.2 本研究の貢献

> 本研究の主な貢献は、Segment Groupごとに保持Payloadと実行経路が変化するSegment-Group Adaptive Payload Strand Typeを設計した点にある。Static Groupは最後に確定した方向と長さを固定Coreから再利用し、Dynamic Groupだけが速度、履歴位置、Solver内部値を含むSidecarを保持する。また、GroupおよびBlock単位の静的再利用許容判定と増分無効化を用いて、変化していない領域の判定処理とSolver処理を省略する。さらに、Guideの局所状態をFollowerおよび描画の依存範囲へ伝播することで、SimulationだけでなくEnd-to-Endの局所更新を可能にする。

## 36.3 安全な新規性表現

> 本研究は、点列・Segment表現、Sleeping、Cache Reuse、Guide/Follower、Dirty Range、GPU Computeなどの既存要素そのものを新規性として主張するものではない。本研究の新規性は、これらをSegment Group単位の固定Core、Optional Dynamic Sidecar、Cache Validity、処理状態、Follower依存範囲、描画依存範囲を持つ一つの状態依存型Strand Data Typeとして統合した点にある。

## 36.4 使用しない表現

正式な結果が得られるまで、次の表現を使用しない。

* 世界初
* 完全な誤差保証
* 数学的に安全
* 必ず60FPS
* どのGPUでも高速
* すべてのシナリオで高速
* 従来研究に存在しないことが証明された
* 静的化しても誤差が発生しない

---

# 37. 関連研究の整理方針

第2章では、次の順序で整理する。

1. 点列、Segment、Rodによる髪表現
2. PBD、XPBD、Elastic Rod
3. Guide/Follower、Wisp、Cluster、LOD
4. GPU Hair Simulation
5. Dense Physics Hair
6. Neural Hair Simulation
7. Hair Compression
8. Sleeping、Cache Reuse、Partial Update
9. Particle、Fluid、Cloth、Rigid Bodyの局所更新
10. 本研究との差分

本研究との差分は、次のように整理する。

| 既存領域                | 既存の中心       | 本研究との差分                           |
| ------------------- | ----------- | --------------------------------- |
| Rod Hair            | 形状と物理モデル    | Segment Groupを実行経路選択単位にする         |
| Guide/Follower      | 代表毛からの補間    | Guide内部の局所状態をFollower範囲へ伝播        |
| Sleeping            | 静止対象の停止     | 同一strand内のGroup混在状態               |
| Cache Reuse         | 過去結果の再利用    | Descriptor、Version、False Static評価 |
| Active Set          | 計算対象の選択     | 保持Payload自体を状態依存で変化               |
| Particle Compaction | Active粒子の圧縮 | 髪の連続性とShared Pointを維持             |
| Dirty Rendering     | 更新範囲の限定     | Guide物理状態から描画範囲まで統合伝播             |

---

# 38. 今後の展望

## 38.1 GTX 1660級実機検証

同一Asset、同一設定、同一Scenarioで実機測定する。

## 38.2 1024点への拡張

* Hierarchical Block
* Super Block
* Hierarchical Descriptor
* Segment LOD
* Multi-Resolution Sidecar

## 38.3 Descriptor自動校正

Shadow Dynamicの誤差分布から重みと閾値を調整する。

ただし、Hard Safetyは明示的な規則として維持する。

## 38.4 Collision-Aware Prediction

Collider速度、移動方向、Swept Volumeから将来のCollision Marginを予測する。

## 38.5 編集システム統合

一本単位編集、Brush、GizmoをDirty Sourceへ直接接続する。

## 38.6 Asset Pipeline

Maya、Alembic、USDなどから入力されたstrandを等間隔Segment Groupへ変換し、次を事前生成する。

* Rest Length
* Root Binding
* Parent/Child
* Block AABB
* Follower Dependency
* Guard Band
* Asset Version

---

# 39. 結論

本研究のテーマは、新しい髪データ型を構築することである。

提案するSegment-Group Adaptive Payload Strand Typeは、髪を単純な点列として保持するのではなく、strand内部の各Segment Groupを、固定Reusable Coreと必要時のみ実体化するDynamic Sidecarからなる状態依存型データとして扱う。

Static Groupは、最後にDynamic Solverで確定した方向とRest Lengthを再利用する。

Dynamic Groupだけが速度、履歴位置、制約値、衝突値などのDynamic専用Payloadを持つ。

Rootまたは頭部が移動する場合は、Rootから毛先へXYZ値を等差的に加算せず、最後に検証された形状へ同一の剛体変換を適用する。

衝突可能性がある領域はSwept AABBとSpatial Queryによって検出し、影響するGroupまたはBlockだけをDynamicへ戻す。

隣接Groupの共有点は物理的に分割せず、Root側とTip側の候補を生成した後、一つのStable ReduceとApply Passで確定する。

Guide Groupの局所状態は、Follower Dependency Range、Follower Segment Range、Render Vertex Rangeへ伝播する。

これにより、Static領域について次を省略する。

* 物理Solver
* Follower再構築
* Render Vertex再生成
* 詳細Descriptor評価
* Sidecar保持

Dynamic割合が高い場合は、GPU実測コストに基づいてFull Dynamic Fast Pathへ退避する。

現時点の新規性判定は「危ない」であるが、Fixed Core＋Dynamic Sidecar、Incremental Block Invalidation、Follower局所再構築、Render Dirty Rangeの効果をStrongest Practical Baselineとアブレーションによって実証できれば、安全判定へ移行できる可能性が高い。

本研究の最終目標は、100,000本、128点、Full HDの髪表示において、物理的整合性と局所編集性を維持しながら、変化していない領域の保持Payload、物理計算、Follower生成、描画更新を省略し、固定フレーム予算下で安定したリアルタイム処理を実現することである。
