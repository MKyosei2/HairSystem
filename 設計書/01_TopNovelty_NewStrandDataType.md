# Novelty 01 完全版企画書兼設計書

# GPU-Resident Segment-Group Adaptive Payload Type for Hair Strands

## 状態依存Payload、局所再利用、依存範囲伝播を備えたGPU常駐型髪データ表現

---

# 0. 文書情報

| 項目                   | 内容                                         |
| -------------------- | ------------------------------------------ |
| 対象プロジェクト             | HairSystem                                 |
| 対象リポジトリ              | `MKyosei2/HairSystem`                      |
| 対象ブランチ               | `main`                                     |
| 基準バージョン              | Version 67                                 |
| 基準コミット               | `00fbf320b24315a343362be13712cb9ff04bb8ac` |
| 文書種別                 | 卒業研究用・企画書兼設計書                              |
| Novelty番号            | Novelty 01                                 |
| 提案名称                 | Segment-Group Adaptive Payload Type        |
| 略称                   | SG-APT                                     |
| 対象API                | DirectX 11 / HLSL                          |
| 対象OS                 | Windows                                    |
| 開発環境                 | Visual Studio 2022                         |
| 計測構成                 | Release x64                                |
| 目標解像度                | 1920 × 1080                                |
| 固定時間刻み               | 1/60秒                                      |
| Solver反復数            | 6回                                         |
| 表示髪本数                | 100,000本                                   |
| Guide本数              | 4,000本                                     |
| Follower本数           | 96,000本                                    |
| 点数                   | 128 points / strand                        |
| Guide Segment Group数 | 508,000 groups                             |
| 表示点数                 | 12,800,000 points                          |
| 主対象GPU               | GTX 1660級以上                                |
| 研究目標                 | 高密度髪表現における状態依存Payloadと局所更新の有効性検証           |
| 性能目標                 | Full HD、100,000本、安定60FPS                   |
| 新規性の主張範囲             | 状態依存Payloadを持つGPU常駐型髪データ型                  |
| 現在の実装状況              | Segment Group試作あり。正式なSG-APTは未完成            |

---

# 1. 研究題目

## 日本語題目

状態依存Payloadと局所再利用を用いたGPU常駐型髪Segment Groupデータ表現

## 英語題目

GPU-Resident Segment-Group Adaptive Payload Representation for Real-Time Hair Simulation

---

# 2. 研究概要

本研究では、高密度なリアルタイム髪シミュレーションにおいて、すべての髪Segmentが毎フレーム同じ量のデータと処理を必要とするわけではない点に着目する。

一本の髪の内部でも、次の状態が混在する。

* Root付近は頭部へ追従する
* 中間部はほとんど変化しない
* 毛先だけがColliderへ接触する
* 一部のSegmentだけが風の影響を受ける
* 編集対象周辺だけが変形する
* Static領域とDynamic領域の境界だけに誤差が生じる

従来の単純な実装では、髪全体またはGuide全体に対して、同一の物理Payloadと同一の更新経路を使用することが多い。

その場合、変化していないSegmentにも以下のDynamic専用情報が常時存在する。

* Previous Position
* Velocity
* Constraint Lambda
* Collision Residual
* Solver一時値
* Dynamic List情報

本研究では、各Segment Groupを固定CoreとOptional Dynamic Payloadから構成されるGPU常駐型データとして定義する。

Static状態のGroupは固定Coreだけを使用し、最後に検証された形状を再利用する。

Dynamic状態のGroupだけがDynamic Payloadを割り当てられ、通常の物理Solverを実行する。

状態変更時にSegment Group本体を別Bufferへ移動させず、固定Indexを維持したまま、有効なPayload、許可操作、Cache再利用条件、状態遷移、Fallback先を変更する。

本研究では、このデータ型をSegment-Group Adaptive Payload Type、略称SG-APTと呼ぶ。

---

# 3. 研究背景

## 3.1 高密度髪表現の処理負荷

100,000本の髪を一本あたり128点で表現する場合、総点数は次のようになる。

[
100,000 \times 128 = 12,800,000
]

総Segment数は次のようになる。

[
100,000 \times 127 = 12,700,000
]

ただし、本研究では4,000本のGuideを物理計算し、96,000本のFollowerをGuideから再構築する。

Guideに対する物理計算対象Group数は次のとおりである。

[
4,000 \times 127 = 508,000
]

高密度髪システムでは、以下の処理が毎フレーム発生する。

* 外力積分
* Root追従
* 距離制約
* 曲げ制約
* Collider衝突
* 自己衝突
* Shared Point処理
* GuideからFollowerへの形状再構築
* 描画用頂点生成
* Visibility判定
* LOD判定
* Draw Call生成

物理計算だけを局所化しても、Follower全再構築や描画頂点全更新が残る場合、End-to-Endでは十分な高速化にならない。

---

## 3.2 StaticとDynamicの混在

髪の動きはstrand単位で一様ではない。

例えば、頭部が移動しただけで髪の局所形状が変化していない場合、全Segmentへ物理Solverを実行する必要はない。

一方で、毛先だけがColliderへ接触している場合、strand全体をStaticにすることもできない。

したがって、次のような混在状態を扱う必要がある。

```text
Root側        中間部          毛先
RigidTransform → StaticReuse → Dynamic
```

または、

```text
Root側        編集範囲        毛先
StaticReuse → Dynamic → Repair
```

---

## 3.3 単純なSleeping方式の限界

Sleepingは、変化していない対象への計算を停止する一般的な最適化である。

しかし、Solver実行を停止するだけでは、次の問題が残る。

* Dynamic専用Payloadが保持されたままになる
* StaticとDynamicで同じBuffer Layoutを使用する
* 状態変更時に全体Copyが発生する
* Static CacheのVersion管理が弱い
* Payload Lifetimeが曖昧になる
* Dynamic割合が増えた際に管理コストだけが残る

本研究では、処理を停止するかどうかではなく、データ型が保持するPayloadそのものを状態依存で変化させる。

---

# 4. 研究目的

本研究の目的は、髪のSegment Groupについて、Dynamic状態でのみ必要なデータをOptional Payloadとして分離し、Static状態ではDynamic専用Payloadを保持・参照しないGPU常駐型データ表現を構築することである。

具体的には、次の事項を検証する。

1. Dynamic専用Payloadの実使用量を削減できるか
2. Group関連Bufferの読み書き量を削減できるか
3. Static GroupのSolver処理を省略できるか
4. 状態遷移時のGroup全体Copyを排除できるか
5. Static Cacheの誤差を許容範囲内に維持できるか
6. Dynamic割合が高い場合にFull Dynamicへ安全に戻れるか
7. 100,000本表示のEnd-to-End処理時間を改善できるか
8. GTX 1660級環境で安定60FPSへ近づけるか

---

# 5. 研究課題

## RQ1：Payload使用量

固定CoreとOptional Dynamic Payloadを分離することで、全Groupが常にDynamic Payloadを持つ方式よりGPUメモリ使用量を削減できるか。

## RQ2：GPU帯域

Static GroupがDynamic専用Payloadを読み書きしないことで、Group関連GPU帯域を削減できるか。

## RQ3：状態遷移

固定Indexを維持し、Payload Indexだけを変更する方式は、Static BufferとDynamic Buffer間でGroup本体を移動する方式より状態遷移コストを削減できるか。

## RQ4：Static再利用精度

Static Groupが最後に確定した形状を再利用しても、Full Dynamic結果との差を許容範囲内に維持できるか。

## RQ5：End-to-End性能

Guide物理計算、Follower再構築、Render Vertex更新まで局所更新範囲を伝播した場合、100,000本表示全体のFrame Timeを改善できるか。

## RQ6：負の高速化

Dynamic Groupが多い条件で、Full Dynamic Fast Pathへ切り替えることにより、提案方式の管理コストによる性能悪化を抑制できるか。

---

# 6. 研究仮説

## H1

Static割合が高いシナリオでは、SG-APTは全Groupが常にDynamic Payloadを持つ方式より、Payload使用量を削減する。

## H2

Static GroupがDynamic専用Payloadを参照しないことで、Group関連Bufferの読み書き量を削減する。

## H3

固定Indexを維持することで、状態遷移時にGroup本体を別BufferへCopyする方式より、状態変更コストを削減する。

## H4

Version管理、Hysteresis、Minimum Residence、定期検証を組み合わせることで、False Staticを許容範囲内へ抑制できる。

## H5

Guideの局所変更をFollowerおよび描画範囲へ伝播することで、物理計算だけの局所化よりもEnd-to-End Frame Timeを改善する。

## H6

全動的条件では、GPU実測コストに基づくFast Path切替により、提案方式の性能悪化を小さく抑えられる。

---

# 7. 新規性の主張

## 7.1 主新規性

本研究の主新規性は、次の一点に限定する。

> 髪のSegment Groupを、固定Indexを持つReusable Coreと、Dynamic状態でのみ有効になるOptional Dynamic Payloadから構成されるGPU常駐型データとして定義し、状態に応じて有効Payload、許可操作、Cache再利用条件、Payload Lifetime、状態遷移およびFallback先を変更する。

---

## 7.2 主貢献

本研究の貢献は以下の三点とする。

### 貢献1：状態依存Payloadを持つ髪データ型

Fixed CoreとOptional Dynamic Payloadから構成される髪Segment Groupデータ型を定義する。

### 貢献2：データ型の操作と不変条件

以下をデータ型の正式な仕様として定義する。

* Dynamic昇格
* Static降格
* Payload割当
* Payload遅延解放
* Cache再利用
* Version無効化
* Rigid Transform
* Repair
* Full Dynamic Fallback

### 貢献3：状態依存Payload固有の評価方法

以下を測定し、単純なSleeping方式との差を示す。

* Dynamic Payload使用量
* Payload Allocation数
* Payload Retire数
* Group関連GPU帯域
* Group状態遷移量
* Static Cache誤差
* False Static率
* Full Dynamic Fallback率

---

## 7.3 主新規性に含めない要素

以下は既存技術または一般的な実装技術として扱う。

* 点列による髪表現
* Segment表現
* Point-Line-Point構造
* 等間隔Resample
* PBD
* XPBD
* Rod Solver
* Guide/Follower
* Sleeping
* Active Set
* Cache Reuse
* Dirty Range
* Partial Update
* GPU Compute
* Prefix Sum
* Compaction
* Persistent Buffer
* Indirect Dispatch
* Indirect Draw
* Frustum Culling
* LOD
* Watchdog
* Fallback
* Telemetry
* GPU Timer

---

## 7.4 補助機構の扱い

以下は主新規性ではなく、SG-APTを実システムとして成立させるための補助機構とする。

* Shared Pointの決定論的解決
* Incremental Block Invalidation
* Follower Dependency Propagation
* Render Dirty Range
* Full Dynamic Fast Path
* Shadow Dynamic検証

これらが単独で新しいとは主張しない。

---

# 8. システム要件

## 8.1 固定実験条件

| 項目              |         固定値 |
| --------------- | ----------: |
| 表示本数            |     100,000 |
| Guide本数         |       4,000 |
| Follower本数      |      96,000 |
| Follower数／Guide |          24 |
| 点数／strand       |         128 |
| Segment数／strand |         127 |
| Guide Group数    |     508,000 |
| Solver反復数       |           6 |
| 固定時間刻み          |       1/60秒 |
| 解像度             | 1920 × 1080 |
| ビルド             | Release x64 |
| VSync           |          無効 |
| CPU Readback    |  正式計測時は原則無効 |

---

## 8.2 機能要件

* Segment Group単位でStatic／Dynamicを管理できる
* Group Coreは固定Indexを維持する
* Dynamic GroupだけがDynamic Payloadを持つ
* Static Groupは最後に検証された形状を再利用する
* Root移動を剛体変換として処理する
* Collider接近時に該当GroupをDynamicへ戻す
* User Edit時に影響範囲をDynamicへ戻す
* Payload Pool不足時にFallbackできる
* Dynamic割合が高い場合にFull Dynamicへ切り替えられる
* 状態遷移、Payload量、誤差を記録できる
* Ground Truthと比較できる

---

## 8.3 非機能要件

* Release x64で再現可能
* GPU処理順序による結果差を最小化する
* NaNおよびInfを検出する
* 30分以上の長時間試験を通過する
* Payload Leakを発生させない
* 異常時にBaselineへ復帰できる
* 計測値をCSVおよびJSONLへ保存できる
* 実験条件をJSONとして固定できる

---

# 9. Segment Groupの定義

strandを次の点列として表す。

[
S={P_0,P_1,\ldots,P_{N-1}}
]

Segment Group (G_i) を次のように定義する。

[
G_i=(P_i,E_i,P_{i+1})
]

[
E_i=P_{i+1}-P_i
]

128点のstrandでは、127個のSegment Groupを持つ。

[
N_G=128-1=127
]

Segment Groupは、新しい物理Primitiveではない。

既存のSegmentを、SG-APTのデータ管理単位として使用する。

---

# 10. 初期形状と等間隔Resample

一本のstrandの全長を (L)、Segment数を (N_G) とする。

基準Segment長を次のように定義する。

[
l_0=\frac{L}{N_G}
]

初期化時またはAsset生成時に、各strandを弧長に基づいてResampleする。

同一strand内では原則としてSegment長を統一する。

ただし、髪ごとの全長は異なってよい。

元Asset形状を優先する必要がある場合は、GroupごとのRest Lengthを保持する互換モードを用意する。

正式評価では以下を分ける。

* Uniform Rest Length
* Per-Group Rest Length

等間隔Resample自体は新規性として主張しない。

---

# 11. SG-APTデータ構造

## 11.1 概念構造

```text
SegmentGroupAdaptivePayload
├─ Fixed Core
├─ Runtime State
├─ Version Information
├─ Static Cache
├─ Optional Dynamic Payload
├─ Shared-Point Ownership
├─ Dependency Metadata
└─ Telemetry Metadata
```

---

## 11.2 Fixed Core

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
    uint blockIndex;

    uint state;
    uint previousState;

    uint dynamicPayloadIndex;
    uint dirtyFlags;

    float restLength;
    float3 cachedLocalDirection;

    float maximumObservedPositionError;
    float maximumObservedBoundaryError;
    float minimumObservedCollisionMargin;

    uint rootTransformVersion;
    uint forceVersion;
    uint colliderVersion;
    uint topologyVersion;
    uint editVersion;

    uint payloadVersion;
    uint dependencyVersion;

    uint lastSolvedFrame;
    uint lastValidatedFrame;
    uint lastTransitionFrame;
    uint minimumResidenceEndFrame;
};
```

Fixed Coreは、StaticとDynamicの切替時にも別Bufferへ移動しない。

---

## 11.3 Optional Dynamic Payload

Dynamic状態でのみ必要となる情報を保持する。

```cpp
struct SegmentDynamicPayload
{
    float3 previousEndPosition;
    float previousPadding;

    float3 velocity;
    float velocityPadding;

    float distanceLambda;
    float bendLambda;
    float constraintResidual;
    float collisionResidual;

    float predictedPositionError;
    float boundaryError;

    uint solverFlags;
    uint collisionFlags;
    uint allocationEpoch;
    uint retireEpoch;
};
```

Static Groupの`dynamicPayloadIndex`はInvalid値とする。

---

## 11.4 Static Cache

Static Groupは、最後にDynamic Solverで確定した方向とRest Lengthを保持する。

[
P_{i+1}=P_i+l_id_i
]

ここで、

* (P_i)：Rootまたは親Groupから確定した始点
* (l_i)：Rest Length
* (d_i)：最後に検証された方向

とする。

Static Groupでは以下を保持しない。

* Previous Position
* Velocity
* Distance Lambda
* Bend Lambda
* Collision Solver一時値

---

## 11.5 Block Descriptor

連続するGroupをBlockとして管理する。

Blockサイズ候補は以下とする。

* 16 Groups
* 32 Groups
* 64 Groups

```cpp
struct SegmentGroupBlockDescriptor
{
    uint firstGroupIndex;
    uint groupCount;

    uint dirtyMask;
    uint validMask;

    float3 boundsMin;
    float boundsPadding0;

    float3 boundsMax;
    float boundsPadding1;

    float maximumMotion;
    float maximumResidual;
    float minimumCollisionMargin;

    uint descriptorVersion;
    uint lastValidatedFrame;
    uint lastUpdatedFrame;
    uint flags;
};
```

正式なBlockサイズは実験により決定する。

---

# 12. データ型の不変条件

SG-APTは以下の不変条件を満たさなければならない。

1. Group CoreのGPU Indexは実行中に変更しない
2. Static Groupは有効なDynamic Payloadを持たない
3. Dynamic Groupは有効なDynamic Payloadを一つだけ持つ
4. 一つのPayloadを複数Groupで共有しない
5. Payload VersionがCore Versionと一致しない場合は使用しない
6. Static CacheはVersion一致時だけ再利用する
7. Topology変更後のCacheを使用しない
8. NaNまたはInfを含むGroupをStatic化しない
9. Static化前に最終Dynamic結果をFixed Coreへ保存する
10. Dynamic Payloadを解放直後に再利用しない
11. Payload Pool不足時はBaselineへFallbackする
12. Shared Pointの最終Writerは一つに限定する
13. 安全上必要なDynamic化をBudgetで遅延しない
14. Follower Dependency Versionが不一致の場合は再構築する
15. 状態遷移をTelemetryへ記録する

---

# 13. Runtime State

SG-APTは以下の状態を持つ。

```text
StaticReuse
StaticTransform
Dynamic
Repair
ForcedDynamic
Invalid
```

---

## 13.1 StaticReuse

最後に検証された局所形状をそのまま再利用する。

条件は以下とする。

* Root Transform Version一致
* Force Version一致
* Collider Version一致
* Topology Version一致
* Edit Version一致
* Dependency Version一致
* Descriptorが有効
* Descriptor Ageが許容範囲内
* 親側境界が有効
* Collision Marginが十分
* Watchdog未発火

---

## 13.2 StaticTransform

内部形状を再計算せず、Rootまたは親Transformの剛体変換だけを適用する。

許可する変換は以下とする。

* 平行移動
* 回転

一様Scaleは別評価とする。

非一様ScaleはGeometry Dirtyとして扱い、DynamicまたはRepairへ移行する。

---

## 13.3 Dynamic

Dynamic Payloadを使用してBaselineと同品質の物理Solverを実行する。

Dynamic状態では以下を実行する。

* Integration
* Root Constraint
* Distance Constraint
* Bend Constraint
* External Force
* Collider Constraint
* Boundary処理
* Cache更新

---

## 13.4 Repair

以下の軽微な不整合を局所的に修正する。

* Static／Dynamic境界誤差
* Shared Point不整合
* Segment Length誤差
* Static復帰時の速度不連続
* 軽微なCollider Penetration

Repairで復旧できない場合はForcedDynamicへ移行する。

---

## 13.5 ForcedDynamic

安全上の理由によりStatic判定を無視してDynamicへ戻す。

発火条件は以下とする。

* Collision Penetration
* User Edit
* Topology変更
* Version不一致
* Payload不整合
* Shared Point未解決
* Segment Length異常
* NaN
* Inf
* Watchdog
* Follower Cache不整合

---

## 13.6 Invalid

入力、Index、Version、Topology、Payloadが不正な状態である。

Invalid GroupはStatic再利用しない。

修復できない場合は次の単位でFallbackする。

* Group
* Block
* Strand
* Guide
* Frame

---

# 14. データ型の操作

SG-APTの操作を次のように定義する。

```text
Initialize
PromoteToDynamic
DemoteToStatic
ApplyRigidTransform
ReuseStaticCache
Invalidate
Validate
RepairBoundary
RetireDynamicPayload
FallbackToBaseline
```

---

## 14.1 Initialize

* Fixed Coreを生成する
* Group Indexを固定する
* Rest Lengthを設定する
* Parent／Child関係を設定する
* Block Indexを設定する
* 初期状態をDynamicとする
* Dynamic Payloadを割り当てる

初期化直後からStaticと決め打ちしない。

---

## 14.2 PromoteToDynamic

Static GroupをDynamicへ昇格させる。

処理内容は以下とする。

1. Dynamic Payload Indexを確保する
2. Previous Positionを初期化する
3. Velocityを推定または0で初期化する
4. Lambdaを初期化する
5. Payload Versionを更新する
6. 状態をDynamicへ変更する
7. Telemetryへ記録する

---

## 14.3 DemoteToStatic

Dynamic GroupをStaticへ降格させる。

処理内容は以下とする。

1. 最終形状をFixed Coreへ保存する
2. Local Directionを正規化して保存する
3. 最終誤差を保存する
4. lastValidatedFrameを更新する
5. Dynamic PayloadをRetiredへ移動する
6. 状態をStaticReuseへ変更する
7. Telemetryへ記録する

---

## 14.4 ApplyRigidTransform

Rootまたは親Transformの剛体差分を適用する。

局所形状は変化させない。

---

## 14.5 ReuseStaticCache

以下を満たす場合のみ実行する。

* Cache Version一致
* Descriptor有効
* Collision Margin十分
* Descriptor Age以内
* 親境界有効
* NaN／Infなし

---

## 14.6 Invalidate

次の変更を検出した場合、該当GroupまたはBlockのCacheを無効化する。

* Root変更
* Collider変更
* Force Field変更
* User Edit
* Topology変更
* Solver Settings変更
* Follower Mapping変更
* LOD変更

---

## 14.7 FallbackToBaseline

以下の場合にFull Dynamic経路へ戻す。

* Payload Pool不足
* Descriptor異常
* Dynamic割合が高い
* Proposal経路の予測時間がBaseline以上
* Watchdog発火
* Shared Point解決失敗
* Dependency伝播失敗

---

# 15. Root移動設計

## 15.1 禁止する方式

Segment番号に比例してXYZ差分を増加させてはならない。

[
P_i'=P_i+i\Delta x
]

この方式は隣接点の移動量を変化させるため、髪を伸長または収縮させる。

---

## 15.2 剛体差分変換

前回のRoot Transformを (T_{old})、現在のRoot Transformを (T_{new}) とする。

[
\Delta T=T_{new}T_{old}^{-1}
]

位置を次式で更新する。

[
P_i^{new}=\Delta TP_i^{old}
]

方向には回転成分だけを適用する。

[
d_i^{new}=R(\Delta T)d_i^{old}
]

---

## 15.3 Root Local Cache

Static形状をRoot Local Spaceで保持する方式を比較対象とする。

```text
World Position
=
Current Root Transform
×
Cached Root-Local Position
```

Rootだけが移動した場合、全Simulation Bufferを書き換えず、描画時Transformだけで処理できる可能性を評価する。

---

## 15.4 Root移動時の衝突

Root移動中は現在位置だけでなく、移動区間を評価する。

使用候補は以下とする。

* Root Swept AABB
* Group Swept AABB
* Collider Swept AABB
* Uniform Grid
* Spatial Hash
* Broad Phase Intersection

相対衝突の可能性があるBlockだけをDynamicへ戻す。

Rootが移動したことだけを理由に全Groupを無条件でDirtyにしない。

---

# 16. 状態遷移制御

## 16.1 DynamicからStatic

次の条件をすべて満たす場合だけStaticへ移行する。

* 位置変化が閾値以下
* 速度予測変位が閾値以下
* 拘束残差が閾値以下
* Collision Marginが十分
* Root変換量が閾値以下
* Force Version一致
* Collider Version一致
* Topology Version一致
* 一定フレーム連続で条件を満たす
* Minimum Residenceを満たす

---

## 16.2 StaticからDynamic

以下の場合は即座にDynamicへ戻す。

* Collider接近または侵入
* User Edit
* Root変換量超過
* Force変更
* Topology変更
* Descriptor Age超過
* Boundary Error超過
* Version不一致
* Watchdog発火
* NaN／Inf

---

## 16.3 Hysteresis

Static化閾値とDynamic化閾値を分離する。

例：

```text
Static化:
positionError < 0.10 mm

Dynamic化:
positionError > 0.20 mm
```

正式な値は校正データによって決定し、評価開始前にFreezeする。

---

## 16.4 Minimum Residence

状態変更後、一定フレームは同じ状態を維持する。

ただし、安全上必要なDynamic化ではResidenceを無視する。

---

## 16.5 Transition Budget

一フレーム内の以下の量を制限する。

* DynamicからStaticへの遷移数
* Sidecar Allocation数
* Sidecar Retire数
* 状態遷移Byte数

StaticからDynamicへの安全遷移はBudgetで遅延させない。

---

# 17. Dynamic Payload Pool

Dynamic Payloadは固定容量Poolで管理する。

```text
Free
→ Active
→ Retired
→ SafeToReuse
→ Free
```

毎フレームの個別GPU Allocationは禁止する。

---

## 17.1 Allocation

Dynamic化対象をCompactionする。

次の方式を比較する。

* Atomic Counter
* Stable Prefix Sum

正式方式は決定性と性能の両方で選定する。

---

## 17.2 Retire

Static化したPayloadを即時再利用しない。

Frame EpochまたはFenceを使用する。

---

## 17.3 Overflow

Payload Pool不足時は次の順で対処する。

1. DynamicからStaticへの新規遷移を停止する
2. Baseline用Dynamic Bufferを使用する
3. Frame全体をFull Dynamicへ切り替える
4. OverflowをTelemetryへ保存する
5. 継続不能時のみSafe Stopする

Payload不足によって不正なIndexを使用してはならない。

---

# 18. Static再利用許容判定

本研究の判定は数学的安全証明ではない。

そのため、Proof、Guaranteed、Certified Safeなどの表現を使用しない。

正式名称は次とする。

> Empirically Validated Static-Reuse Descriptor

日本語では次とする。

> 経験的検証付き静的再利用許容記述子

---

## 18.1 Hard Safety

一つでも違反した場合、Staticを解除する。

* NaN
* Inf
* Collision Penetration
* Segment Length異常
* Topology変更
* Version不一致
* Payload不整合
* Shared Point未解決
* User Edit
* Dependency不整合

---

## 18.2 Soft Prediction

Static継続可能性を評価する。

* 予測位置誤差
* 速度
* 加速度
* 拘束残差
* Root変換量
* Collider接近量
* 外力変化
* Boundary Error
* Descriptor Age
* Shadow Dynamic履歴

---

## 18.3 単位正規化

速度による予測変位は次とする。

[
E_v=|v|\Delta t
]

加速度による予測変位は次とする。

[
E_a=\frac{1}{2}|a|\Delta t^2
]

Root回転による予測変位は次とする。

[
E_r=2d\sin\left(\frac{\theta}{2}\right)
]

すべての誤差項を位置誤差単位へ変換する。

---

## 18.4 Collision Margin

Colliderまでの最小距離を (m_c) とする。

次を満たさないGroupはStatic化しない。

[
m_c >
E_v
+E_a
+E_{collider}
+\tau_c
]

高速移動ColliderにはSwept Volumeを使用する。

---

## 18.5 Descriptor Age

[
Age_i=currentFrame-lastValidatedFrame
]

以下の場合は再検証する。

* Age超過
* Root Transform変更
* Force Version変更
* Collider接近
* 隣接GroupがDynamic
* Boundary Error増加
* Shadow Dynamic誤差増加
* User Edit発生

---

# 19. Incremental Block Invalidation

毎フレーム全508,000 Groupを評価すると、判定処理自体が負荷になる。

連続GroupをBlockとして管理し、変更のないBlockではGroup単位判定も省略する。

---

## 19.1 Dirty Source

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

## 19.2 空間的無効化

```text
Dirty Source
→ Swept AABB
→ Spatial Query
→ Affected Block List
→ Block Descriptor Invalidation
```

---

## 19.3 Block Skip

有効なStatic Blockでは以下を省略する。

* Group Descriptor更新
* State Candidate生成
* Dynamic List登録
* Sidecar Allocation
* Constraint Solver
* Collision Solver
* Shared Point候補生成
* Follower再構築
* Render Vertex再生成

---

# 20. Shared Point設計

隣接Groupは共有点を持つ。

[
G_i=(P_i,P_{i+1})
]

[
G_{i+1}=(P_{i+1},P_{i+2})
]

子Groupは共有始点を独立に積分しない。

共有点の最終書き込み元は一つに限定する。

---

## 20.1 責任範囲

Root側GroupとTip側Groupで処理候補を分ける。

これは物理的に点を二つへ分割することではない。

---

## 20.2 Candidate生成

各Groupは共有点に対する候補を生成する。

---

## 20.3 優先順位

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

---

## 20.4 Stable Reduce

同じPriorityの場合は次の順で決定する。

1. 有限値である
2. Position Errorが小さい
3. Constraint Residualが小さい
4. Collision Residualが小さい
5. Source Group IDが小さい

Atomic到着順を最終決定に使用しない。

Shared Point処理は補助機構であり、主新規性としては扱わない。

---

# 21. Dynamic Solver

Dynamic Groupでは、Baselineと同品質のSolverを使用する。

提案方式を高速に見せるため、Dynamic Groupの物理品質を低下させてはならない。

最低限以下を含める。

* Verletまたは同等のIntegration
* Root Constraint
* Distance Constraint
* Bend Constraint
* External Force
* Collider Constraint
* Boundary Repair

Baselineと提案方式では以下を一致させる。

* 時間刻み
* 反復回数
* Compliance
* Collision条件
* Root条件
* 数値精度
* Solver順序

単純な「方向をRest Lengthへ戻すだけ」の処理は正式Solverとしない。

---

# 22. Follower依存伝播

4,000本のGuideを部分更新しても、96,000本のFollowerを毎フレーム全再構築した場合、End-to-End高速化は限定的になる。

したがって、Follower局所再構築はシステム成立条件として実装する。

ただし、主新規性とはしない。

---

## 22.1 Dependency表現

CSR形式を基本候補とする。

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

## 22.2 Dirty Range伝播

```text
Guide Group Dirty
→ Dependency Lookup
→ Affected Follower
→ Target Segment Range Union
→ Guard Band Expansion
→ Follower Dirty Range
```

---

## 22.3 Follower更新モード

* Cache Reuse
* Rigid Transform
* Local Reconstruct
* Full Reconstruct Fallback

以下の場合はFull Reconstructへ戻す。

* Dependency Version不一致
* Topology変更
* LOD変更
* 大規模編集
* Dirty Range計算失敗
* Watchdog発火

---

# 23. 描画更新設計

Follower Dirty RangeをRender Vertex Rangeへ伝播する。

```text
Follower Segment Dirty
→ Render Vertex Range
→ Persistent Vertex Cache Invalidation
→ Dirty Range Vertex Regeneration
→ Indirect Draw Data Update
```

Static Rangeは前フレームのRender Bufferを再利用する。

Rootだけが剛体移動した場合は、可能であればVertex ShaderまたはInstance Transformで移動する。

以下はBaselineと提案方式の両方へ適用する。

* GPU Resident Buffer
* Persistent Vertex Buffer
* Indirect Draw
* Frustum Culling
* Distance LOD
* Screen-space LOD
* Width LOD
* CPU Readback排除

一般的な描画最適化の効果をSG-APT固有の効果として扱わない。

---

# 24. Full Dynamic Fast Path

Dynamic Groupが多い場合、以下の管理処理が追加負荷になる。

* Descriptor評価
* Block判定
* Group分類
* Compaction
* Payload管理
* Static Transform
* Dirty Range生成

提案経路の時間を次のように定義する。

[
T_P=
T_{dirty}
+T_{descriptor}
+T_{classify}
+T_{compact}
+T_{payload}
+T_{partialSolve}
+T_{followerPartial}
+T_{renderPartial}
]

Full Dynamic経路を次のように定義する。

[
T_F=
T_{fullSolve}
+T_{followerFull}
+T_{renderFull}
]

固定Dynamic Ratioだけではなく、GPU実測時間を用いて切り替える。

入力は以下とする。

* Dynamic Group Ratio
* Dynamic Block Ratio
* Block Skip率
* Payload Allocation数
* State Transition数
* Dirty Follower Ratio
* Dirty Render Vertex Ratio
* GPU Pass別時間
* p95 Frame Time履歴

提案経路の予測時間がFull Dynamic経路以上となる状態がKフレーム継続した場合、Full Dynamicへ移行する。

切替にはHysteresisを設ける。

---

# 25. フレーム処理Pipeline

```text
Phase 01  Frame Input確定
Phase 02  Root / Collider / Force Version更新
Phase 03  Topology / Settings / LOD変更検出
Phase 04  Dirty Source生成
Phase 05  Swept AABB生成
Phase 06  Spatial Query
Phase 07  Affected Block抽出
Phase 08  Block Descriptor失効
Phase 09  Fast Path Cost Prediction
Phase 10  Full Dynamic Fast Path判定

通常経路:
Phase 11  Invalid BlockのみGroup Descriptor更新
Phase 12  Hard Safety判定
Phase 13  State Candidate生成
Phase 14  Hysteresis適用
Phase 15  Minimum Residence適用
Phase 16  Transition Budget適用
Phase 17  Payload Allocation / Retire Plan
Phase 18  Dynamic / Static / Repair List生成
Phase 19  Dynamic Integration
Phase 20  Dynamic Constraint Solve
Phase 21  Collision Solve
Phase 22  Static Reuse / Static Transform
Phase 23  Shared-Point Candidate生成
Phase 24  Stable Reduce
Phase 25  Shared-Point Apply
Phase 26  Boundary Repair
Phase 27  Guide Dirty Range確定
Phase 28  Follower Dependency伝播
Phase 29  Follower Cache Reuse / Local Reconstruct
Phase 30  Render Dirty Range生成
Phase 31  Render Vertex部分更新
Phase 32  Indirect Draw Data更新
Phase 33  Telemetry更新
Phase 34  Watchdog
Phase 35  Shadow Dynamic検証
```

---

# 26. WatchdogとFallback

## 26.1 監視対象

* NaN
* Inf
* Segment Length異常
* Constraint Residual急増
* Boundary Error急増
* Payload Index不整合
* Duplicate Payload Allocation
* Descriptor Version不一致
* Shared Point未解決
* Follower Cache不整合
* Dirty Range範囲外
* Dispatch Count不整合
* Transition過多
* False Static急増
* Shadow Dynamic誤差急増
* GPU Query未解決
* Frame Time異常
* Memory増加

---

## 26.2 Fallback単位

```text
Group Fallback
Block Fallback
Strand Fallback
Guide Fallback
Frame Full Dynamic Fallback
Application Safe Stop
```

提案経路が失敗しても、可能な限り通常Solverで描画を継続する。

---

# 27. Ground TruthとShadow Dynamic

## 27.1 Ground Truth

Ground Truthは、同一入力、同一時間刻み、同一Solver設定で、すべてのGuide GroupをDynamicとして処理した結果とする。

---

## 27.2 Shadow Dynamic

Static Groupの一部をサンプリングし、表示には使用しないDynamic Solverを並行実行する。

Static結果とShadow Dynamic結果の差を測定する。

---

## 27.3 優先サンプリング対象

* Descriptor閾値付近
* Static継続時間が長いGroup
* Collision Marginが小さいGroup
* Static／Dynamic境界
* Root変換量が大きいGroup
* 過去にFalse Staticを発生したGroup
* Descriptor Ageが大きいGroup

---

## 27.4 校正と正式評価の分離

Descriptorの閾値を調整するCalibrationデータと、正式評価データを分離する。

正式評価開始前に閾値をFreezeする。

評価結果を見てから閾値を都合よく変更しない。

---

# 28. メモリ設計

Guide Group数を (N_G)、Dynamic Group数を (N_D)、Coreサイズを (S_C)、Payloadサイズを (S_P) とする。

[
M_{group}=N_GS_C+N_DS_P
]

本研究条件では、

[
N_G=508,000
]

Dynamic Ratioを (r) とすると、

[
N_D=rN_G
]

となる。

初期目標は次のとおりとする。

| データ                           |                       目標サイズ |
| ----------------------------- | --------------------------: |
| Fixed Core                    |         64～96 bytes / group |
| Dynamic Payload               | 32～64 bytes / dynamic group |
| Block Descriptor              |         64～96 bytes / block |
| Static GroupのDynamic専用Payload |                     0 bytes |
| Payload Temporary Data        |                       必要最小限 |

メモリ容量だけでなく、毎フレームの読み書きByte数を重視する。

---

# 29. 現行Mainとの対応

| 機能                     | 現行状態     | 正式完了条件               |
| ---------------------- | -------- | -------------------- |
| 100,000本固定             | 実装あり     | 維持                   |
| Segment Group Metadata | 試作あり     | Fixed Coreへ統一        |
| Group State            | 簡易実装     | 全状態を実装               |
| Static Reuse           | 試作あり     | VersionとDescriptor連携 |
| Static Transform       | 平行移動中心   | 剛体差分Transform        |
| Dynamic Solver         | 簡易補正     | Baseline同品質          |
| Dynamic Payload        | 未完成      | 固定PoolとEpoch         |
| Shared Point           | 部分試作     | Stable Reduce        |
| Compaction             | Atomic中心 | Stable方式と比較          |
| Block Dirty            | 全体失効あり   | 増分失効                 |
| Follower更新             | 全再構築中心   | 局所再構築                |
| Render Dirty Range     | 未完成      | Persistent部分更新       |
| Full Dynamic Fast Path | 基本経路あり   | GPU実測切替              |
| Shadow Dynamic         | 未完成      | Sampling比較           |
| Watchdog               | 部分対応     | Proposal全体へ拡張        |

---

# 30. 実装対象ファイル

## 30.1 C++／Header

* `apps/dx11_viewer/src/GpuHairViewer.h`
* `include/hairgpu/Types.h`
* `include/hairgpu/HairNoveltyTelemetry.h`

## 30.2 Viewer Parts

* `GpuHairViewer_parts/01_window_device_and_ui_setup.inl`
* `GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl`
* `GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl`
* `GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl`
* `GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl`
* `GpuHairViewer_parts/09_runtime_quality_gate.inl`
* `GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl`
* `GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl`

## 30.3 HLSL

* `apps/dx11_viewer/shaders/HairSegmentGroupRuntime.hlsl`
* `apps/dx11_viewer/shaders/HairSim.hlsl`
* `apps/dx11_viewer/shaders/HairIndirectArgs.hlsl`
* `apps/dx11_viewer/shaders/HairStrandInstanced.hlsl`

## 30.4 実験基盤

* `tools/final_stage/hairgpu_final_run.py`
* `tools/final_stage/hairgpu_runtime_suite.py`
* `tools/final_stage/hairgpu_final_db.py`
* `tools/final_stage/hairgpu_novelty01_preflight.py`
* `tools/final_stage/Run_Final_Runtime_Suite.bat`

## 30.5 設定・文書

* `docs/sample_assets/final_runtime_suite_template.json`
* `docs/sample_assets/hardware_profiles.json`
* `docs/experiments/NOVELTY01_SAFE_EVALUATION_PROTOCOL.md`
* `設計書/01_TopNovelty_NewStrandDataType.md`
* `論文パーツ/ch02_*.md`
* `論文パーツ/+ch02_*.md`

---

# 31. 実装工程

## Phase 0：Baseline固定

* Frozen Asset固定
* 100,000本固定
* 4,000 Guides固定
* 128 points固定
* 6 iterations固定
* 1/60秒固定
* Release x64固定
* Telemetry Schema固定

### 完了条件

* 同一条件で再実行可能
* Baseline計測値を保存可能
* GPU Timerが解決される
* 実験IDを再現可能

---

## Phase 1：Fixed Core

* 固定Group Index
* Parent／Child
* Block Index
* State
* Version
* Dirty Flag
* Static Cache

### 完了条件

* 状態変更時にGroup本体Copyが発生しない
* Indexが実行中に変化しない

---

## Phase 2：Dynamic Payload Pool

* Free List
* Active List
* Retired List
* Epoch
* Overflow Fallback

### 完了条件

* Duplicate Allocation 0件
* Leak 0件
* Overflow時継続可能

---

## Phase 3：State Machine

* PromoteToDynamic
* DemoteToStatic
* Hysteresis
* Minimum Residence
* Transition Budget

### 完了条件

* State Thrashingを測定可能
* 状態遷移をTelemetryへ保存可能

---

## Phase 4：Static-Reuse Descriptor

* Hard Safety
* Soft Prediction
* 単位正規化
* Descriptor Age
* Collision Margin

### 完了条件

* Ground Truthとの差を測定可能
* False Static率を測定可能

---

## Phase 5：Incremental Block Invalidation

* Dirty Source
* Swept AABB
* Spatial Query
* Affected Block List
* Block Skip

### 完了条件

* Root移動で全Groupを無条件Dirtyにしない
* Block Skip率を測定可能

---

## Phase 6：Shared Point Resolve

* Candidate Generation
* Priority
* Stable Reduce
* Single Writer Apply

### 完了条件

* Atomic到着順に依存しない
* 実行間Hash一致率を測定可能

---

## Phase 7：Dynamic Solver統一

* Baselineと同一制約
* 同一反復回数
* 同一Collision条件

### 完了条件

* Dynamic Groupの品質がBaselineと一致する
* Proposalのために品質を下げていない

---

## Phase 8：Follower Dependency

* CSR Mapping
* Support Range
* Guard Band
* Dirty Range Union

### 完了条件

* Guide局所変更がFollower範囲へ伝播する
* 通常経路でFollower全再構築を避けられる

---

## Phase 9：Render Dirty Range

* Persistent Render Cache
* Dirty Vertex Update
* Indirect Draw Data

### 完了条件

* Static RangeのRender Vertex再生成を省略できる

---

## Phase 10：Cost-Based Fast Path

* Pass別GPU Timer
* Proposal Cost Prediction
* Full Dynamic Cost Prediction
* Hysteresis切替

### 完了条件

* 全動的条件で管理コストの悪化を抑制できる

---

## Phase 11：正式評価

* Baseline比較
* アブレーション
* S01～S18
* Ground Truth
* Shadow Dynamic
* 決定性試験
* 長時間試験
* GTX 1660級実機試験

---

# 32. 比較対象

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

## B2：Sleeping Flag

* 全Groupが同じPayloadを保持
* Static GroupのSolverだけ停止

## B3：Active List

* Dynamic GroupだけをActive Listへ登録
* 全GroupがDynamic Payloadを保持

## B4：Separate Static／Dynamic Buffer

* Static GroupとDynamic Groupを別Buffer間で移動

## B5：Fixed Core＋Optional Payload

* 固定CoreとOptional Payload
* 単純な速度閾値で状態判定

## B6：SG-APT without Block Invalidation

* SG-APT
* 全Group Descriptorを毎フレーム評価

## B7：Guide Local Update Only

* Guideのみ局所更新
* Follower全再構築
* Render全更新

## B8：Strongest Practical Baseline

以下の一般的最適化を組み合わせる。

* Guide/Follower
* Sleeping
* Block Update
* Cache Reuse
* Dirty Range
* GPU Resident
* Indirect Draw
* LOD
* Full Dynamic Fast Path

ただし、以下は使用しない。

* 固定Core＋状態依存Optional Payloadの統合データ型
* Payload Lifetimeを含む状態遷移仕様
* SG-APT固有の不変条件

## P：Proposed SG-APT

本設計をすべて適用する。

---

# 33. アブレーション

| ID  | 無効化する要素                        | 評価目的            |
| --- | ------------------------------ | --------------- |
| A1  | Optional Dynamic Payload       | Payload分離の効果    |
| A2  | 固定Group Index                  | Buffer移動方式との差   |
| A3  | Version Check                  | 古いCache再利用の影響   |
| A4  | Delayed Free                   | Payload再利用異常    |
| A5  | Hysteresis                     | State Thrashing |
| A6  | Minimum Residence              | 遷移回数            |
| A7  | Static Cache                   | 再構築方式との差        |
| A8  | Incremental Block Invalidation | 全Group評価との差     |
| A9  | Stable Shared Point            | 決定性             |
| A10 | Follower Dependency            | End-to-End性能    |
| A11 | Render Dirty Range             | 描画更新量           |
| A12 | Full Dynamic Fast Path         | 全動的条件           |
| A13 | Shadow Dynamic                 | False Static検出  |
| A14 | Watchdog                       | 異常復帰            |
| A15 | Stable Compaction              | 再現性と性能          |

---

# 34. 評価シナリオ

| ID  | シナリオ                   |
| --- | ---------------------- |
| S01 | 完全静止                   |
| S02 | Root平行移動               |
| S03 | Root回転                 |
| S04 | 弱い風                    |
| S05 | 通常頭部運動                 |
| S06 | 局所衝突                   |
| S07 | 複数Collider             |
| S08 | 一本単位編集                 |
| S09 | 連続Brush編集              |
| S10 | 強風                     |
| S11 | 全Group Dynamic         |
| S12 | Static／Dynamic境界Stress |
| S13 | State Thrashing Stress |
| S14 | 複数Guide依存Follower      |
| S15 | 30分長時間試験               |
| S16 | 決定性試験                  |
| S17 | Payload Pool Stress    |
| S18 | Version／Descriptor異常注入 |

---

# 35. 評価指標

## 35.1 性能

* Average Frame Time
* Median Frame Time
* p95 Frame Time
* p99 Frame Time
* Maximum Frame Time
* 50ms以上Frame数
* 100ms以上Frame数
* Simulation Time
* Descriptor Time
* Block Invalidation Time
* Classification Time
* Compaction Time
* Payload Allocation Time
* Follower Reconstruction Time
* Render Preparation Time
* Draw Time

---

## 35.2 Payload

* Dynamic Payload使用数
* Dynamic Payload使用率
* Payload Allocation数
* Payload Retire数
* Payload再利用数
* Pool Overflow数
* Payload Memory
* Group関連読み書きByte数

---

## 35.3 状態

* StaticReuse Group数
* StaticTransform Group数
* Dynamic Group数
* Repair Group数
* Invalid Group数
* State Transition数
* State Thrashing率
* Block Skip率
* Full Dynamic Fallback率

---

## 35.4 正確性

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

---

## 35.5 安定性

* NaN数
* Inf数
* Invalid Payload Index数
* Duplicate Payload数
* Shared Point未解決数
* Watchdog発火数
* Fallback成功率
* 実行間Hash一致率
* 長時間Memory増加量

---

# 36. Formal 60FPS判定

60FPSは画面表示値ではなくFrame Timeで判定する。

```text
Average Frame Time <= 16.6667 ms
p95 Frame Time <= 16.6667 ms
p99 Frame Time <= 20.0 ms
Maximum Frame Time <= 50.0 ms
100 ms以上のFrame = 0
```

固定条件は以下とする。

* Release x64
* Full HD
* 100,000 strands
* 4,000 Guides
* 96,000 Followers
* 128 points
* 6 Solver iterations
* 1/60秒
* 同一Asset
* 同一Camera
* 同一Seed
* 同一Scenario

VSync、Frame Cap、画面表示FPSだけで合否を判定しない。

---

# 37. 性能目標

## 37.1 研究上の必須目標

* B2よりPayload使用量を削減
* B3より状態遷移Copy量を削減
* B5よりFalse Staticまたは状態安定性を改善
* B8より通常シナリオのEnd-to-End Frame Timeを改善
* 全動的条件でB1より大幅に悪化しない
* Payload Leak 0件
* NaN／Inf 0件

---

## 37.2 暫定効果量目標

* 通常シナリオでB8よりp95 Frame Timeを15%以上改善
* Dynamic Ratio 30%以下でGroup関連帯域を20%以上削減
* 全動的条件でB1に対するp95悪化を5%以内
* False Static率0.1%以下
* 重大Collision Penetration 0件
* 30分試験で継続的Memory増加なし
* 実行間Hash一致率99.9%以上

これらは研究開始時の目標値であり、保証値ではない。

正式評価開始前に固定する。

---

# 38. GTX 1660級での60FPS見込み

実機測定前の時点では保証しない。

実装完成後の理論的な目安は次のとおりである。

| シナリオ       | 安定60FPS見込み |
| ---------- | ---------: |
| 完全静止       |     70～85% |
| Root剛体移動   |     65～80% |
| 弱い風        |     50～65% |
| 通常頭部運動     |     40～55% |
| 局所衝突       |     35～50% |
| 複数Collider |     20～40% |
| 強風         |     10～25% |
| ほぼ全動的      |      5～20% |
| 全シナリオ一括合格  |     20～30% |

60FPSへ到達しなくても、B8より有意に高速で、SG-APT固有のPayload削減効果が確認できれば研究的価値は成立する。

---

# 39. リスクと対策

## 39.1 先行研究との重複

### リスク

状態依存PayloadやSparse Payloadに近い方式が、髪以外の粒子、布、流体、剛体分野に存在する可能性がある。

### 対策

* 新規性を髪Segmentへの適用だけで主張しない
* データ型の操作、不変条件、Payload Lifetimeまで定義する
* SleepingやActive Setとの差をB2、B3で示す
* Static／Dynamic Buffer移動との差をB4で示す
* 最新先行研究を最終提出前に再確認する
* 同一方式が確認された場合は実装・評価貢献へ位置付け直す

---

## 39.2 Static割合が低い

### リスク

強風や多数ColliderではDynamic Groupが増える。

### 対策

* Cost-Based Full Dynamic Fast Path
* Block単位早期判定
* Proposal経路の自動停止

---

## 39.3 Descriptor評価が重い

### リスク

全Groupを毎フレーム評価すると高速化にならない。

### 対策

* Incremental Block Invalidation
* Block Skip
* Descriptor Age
* Dirty Source限定

---

## 39.4 False Static

### リスク

Static判定誤りにより衝突漏れや形状誤差が発生する。

### 対策

* Hard Safety
* Collision Margin
* Shadow Dynamic
* Periodic Validation
* Watchdog
* ForcedDynamic

---

## 39.5 State Thrashing

### リスク

StaticとDynamicを毎フレーム往復する。

### 対策

* Hysteresis
* Minimum Residence
* Consecutive Pass
* Transition Budget

---

## 39.6 Payload Pool不足

### 対策

* Worst Case容量評価
* Overflow Telemetry
* Baseline Buffer使用
* Full Dynamic Fallback
* 遅延再利用

---

## 39.7 Shared Point競合

### 対策

* Candidate生成
* Stable Reduce
* Single Writer
* Atomic到着順の排除

---

## 39.8 Follower全再構築

### リスク

Guideの高速化がFollower処理に隠れる。

### 対策

* CSR Dependency
* Guard Band
* Local Reconstruct
* Full Reconstruct Fallback

---

## 39.9 描画ボトルネック

### 対策

* SimulationとEnd-to-Endを分離計測
* Persistent Render Cache
* Render Dirty Range
* Indirect Draw
* Overdraw計測

---

## 39.10 Root移動による髪の伸長

### 対策

* 剛体差分Transform
* Root Local Cache
* Segment Length Watchdog
* XYZ等差加算の禁止

---

# 40. 新規性を安全に主張するための条件

以下を満たすまで、新規性を確定的に「安全」と断定しない。

1. SG-APTが正式に実装されている
2. B2との比較で単純Sleepingとの差を示している
3. B3との比較でActive Listとの差を示している
4. B4との比較でBuffer移動方式との差を示している
5. B5との比較で単純Core／Payload分離との差を示している
6. A1でOptional Payload固有の効果を示している
7. A2で固定Index固有の効果を示している
8. A8でIncremental Block Invalidationの効果を示している
9. A10でFollower局所更新の効果を示している
10. False StaticをGround Truthで評価している
11. 全動的条件でFallbackが正常に動作している
12. Payload Pool不足時も安全に継続できる
13. 長時間試験でLeakがない
14. 決定性試験を通過している
15. 最新の髪、粒子、布、流体、剛体、Sparse Simulation研究と比較している
16. 同一のデータ型仕様を持つ先行方式が確認されていない
17. 実験結果を過大に一般化していない

---

# 41. 先行研究と重複した場合のFallback Positioning

同一または極めて近いデータ型が既に存在した場合、研究全体を破棄しない。

次の位置付けへ変更する。

> 高密度GPU Hair System向け状態依存Payload設計の実装、性能評価、および誤差評価。

その場合の貢献は次のとおりとする。

* DirectX 11上での実装
* 100,000本、128点での評価
* Guide／Follower／Renderを含むEnd-to-End評価
* False Static評価
* Payload Lifetime評価
* 全動的Fallback評価
* GTX 1660級環境での実測

これにより、新規性判定が変化しても卒業研究としての価値を維持する。

---

# 42. 論文で使用する推奨表現

## 42.1 研究目的

> 本研究では、髪strand内部を構成するSegment Groupを、固定Indexを持つReusable Coreと、Dynamic状態でのみ有効になるOptional Payloadから構成されるGPU常駐型データとして表現する。各Groupは実行時状態に応じてStatic Reuse、Rigid Transform、Dynamic Solve、Repair、Fallbackの操作を選択する。これにより、Static GroupがDynamic専用情報を保持・参照する必要をなくし、高密度髪システムにおけるPayload使用量とGPU処理量の削減を目指す。

---

## 42.2 本研究の貢献

> 本研究の主な貢献は、髪Segment Groupに対して、固定Core、Optional Dynamic Payload、状態遷移、Version管理、Payload Lifetime、不変条件およびFallback操作を備えた状態依存型データ表現を定義した点にある。また、単純なSleeping方式、Active List方式、Static／Dynamic Buffer移動方式と比較し、Payload使用量、GPU帯域、状態遷移コスト、False Static率を用いて提案方式を評価する。

---

## 42.3 安全な新規性表現

> 本研究は、Segment表現、Sleeping、Cache Reuse、Active Set、Guide/Follower、GPU Computeなどの個別技術そのものを新規性として主張するものではない。本研究の検証対象は、これらの処理を支える髪Segment Groupを、固定Coreと状態依存Optional Payloadからなる一つのGPU常駐型データとして定義し、状態に応じて有効Payload、許可操作、Cache条件、Payload LifetimeおよびFallback先を変更する点にある。

---

# 43. 使用禁止表現

正式な結果が得られるまで、以下の表現を使用しない。

* 世界初
* 髪SegmentのStatic／Dynamic切替を初めて実現
* 完全に安全なStatic判定
* 数学的に誤差が発生しない
* 必ず60FPSを達成
* すべてのGPUで高速
* すべてのシナリオで高速
* 既存研究に存在しないことが証明された
* 100,000本すべてを独立に物理計算
* FollowerもGuideと同品質の物理を持つ
* GPUメモリを必ず削減
* 完全な物理保証

---

# 44. 論文章構成案

## 第1章　はじめに

1.1 研究背景
1.2 高密度髪シミュレーションの問題
1.3 状態ごとに必要データが異なる問題
1.4 研究目的
1.5 研究課題
1.6 本研究の貢献
1.7 論文構成

## 第2章　関連研究

2.1 髪の点列・Segment表現
2.2 RodおよびPBD／XPBD
2.3 Guide／Follower
2.4 GPU Hair Simulation
2.5 SleepingとActive Set
2.6 Cache ReuseとPartial Update
2.7 Sparse PayloadおよびDynamic Compaction
2.8 粒子・布・流体・剛体の局所更新
2.9 既存研究の限界
2.10 本研究の位置付け

## 第3章　提案データ型

3.1 SG-APT概要
3.2 Segment Group定義
3.3 Fixed Core
3.4 Optional Dynamic Payload
3.5 Runtime State
3.6 データ型の操作
3.7 不変条件
3.8 VersionとPayload Lifetime
3.9 Root剛体変換
3.10 Fallback

## 第4章　GPU実装

4.1 全体構成
4.2 Buffer Layout
4.3 Payload Pool
4.4 State Classification
4.5 Block Invalidation
4.6 Dynamic Solver
4.7 Shared Point Resolve
4.8 Follower Dependency
4.9 Render Dirty Range
4.10 Full Dynamic Fast Path
4.11 Telemetry

## 第5章　実験

5.1 実験環境
5.2 固定条件
5.3 比較対象
5.4 アブレーション
5.5 シナリオ
5.6 評価指標
5.7 Ground Truth
5.8 長時間試験
5.9 決定性試験

## 第6章　結果

6.1 Payload使用量
6.2 GPU帯域
6.3 State Transition
6.4 Simulation性能
6.5 End-to-End性能
6.6 False Static
6.7 Full Dynamic条件
6.8 GTX 1660級結果

## 第7章　考察

7.1 SG-APT固有の効果
7.2 Sleeping方式との差
7.3 Active Listとの差
7.4 負の高速化
7.5 誤差と安全性
7.6 実装上の制約
7.7 新規性の限界
7.8 将来課題

## 第8章　結論

---

# 45. 発表スライド構成案

1. 研究背景
2. 100,000本×128点の負荷
3. 一本の髪内部にStatic／Dynamicが混在する問題
4. 既存Sleeping方式の限界
5. 提案SG-APT
6. Fixed CoreとOptional Payload
7. 状態遷移
8. Root移動とCollision
9. GPU Pipeline
10. 比較対象
11. 評価結果
12. 新規性の位置付け
13. 結論

発表では詳細な実装一覧より、以下を視覚的に示す。

```text
従来:
全Group
=
常に同じDynamic Payload

提案:
Static Group
=
Fixed Coreのみ

Dynamic Group
=
Fixed Core + Dynamic Payload
```

---

# 46. 実装完了チェックリスト

```text
[ ] Fixed Coreが固定Indexで保持される
[ ] Dynamic Payloadが別Poolへ分離される
[ ] Static GroupのPayload IndexがInvalidになる
[ ] Dynamic GroupだけがPayloadを参照する
[ ] PromoteToDynamicが実装される
[ ] DemoteToStaticが実装される
[ ] Static化時に最終形状を保存する
[ ] Payload解放にEpochまたはFenceを使用する
[ ] Pool Overflow時にFallbackできる
[ ] Version不一致時にCacheを拒否する
[ ] Root移動を剛体変換として処理する
[ ] Segment番号によるXYZ等差加算を使用しない
[ ] Hysteresisが実装される
[ ] Minimum Residenceが実装される
[ ] Transition Budgetが実装される
[ ] Hard SafetyとSoft Predictionが分離される
[ ] Descriptor項が位置誤差単位へ正規化される
[ ] Descriptor Ageがある
[ ] Block Descriptorが増分無効化される
[ ] Root移動で全Groupを無条件Dirtyにしない
[ ] Block Skipが実際のDispatch省略へ使用される
[ ] Shared Pointの最終Writerが一つである
[ ] Stable Reduceが実装される
[ ] Dynamic SolverがBaselineと同品質である
[ ] Follower Dependencyが事前計算される
[ ] Follower局所再構築が実装される
[ ] Render Dirty Rangeが実装される
[ ] Full Dynamic Fast PathがGPU実測で切り替わる
[ ] WatchdogからBaselineへ退避できる
[ ] Ground Truth比較がある
[ ] Shadow Dynamic比較がある
[ ] False Static率を測定できる
[ ] Dynamic Payload使用量を測定できる
[ ] Group関連Byte数を測定できる
[ ] State Transition数を測定できる
[ ] Follower再構築点数を測定できる
[ ] Render再生成頂点数を測定できる
[ ] B2からB8と比較できる
[ ] A1からA15を実施できる
[ ] 30分長時間試験を通過する
[ ] 決定性試験を通過する
[ ] GTX 1660級実機試験を実施する
```

---

# 47. Go／No-Go判定

## Gate 1：データ型成立

### Go条件

* Fixed CoreとOptional Payloadが分離済み
* 固定Index維持
* Payload Leakなし
* Overflow Fallback成功

### No-Go条件

* 状態変更時にGroup全体Copyが必要
* Static GroupがDynamic Payloadを保持したまま
* Payload Index不整合が解消できない

---

## Gate 2：正確性成立

### Go条件

* False Static率を測定可能
* Segment Length異常なし
* 重大Collision Penetrationなし
* NaN／Infなし

### No-Go条件

* Static再利用誤差を測定できない
* Ground Truthが存在しない
* Dynamic Solver品質がBaselineと異なる

---

## Gate 3：性能成立

### Go条件

* B2よりPayload使用量削減
* B4より状態遷移Copy量削減
* 通常シナリオでB8より高速
* 全動的条件でFallback成功

### No-Go条件

* 管理コストが常にFull Dynamicより大きい
* Follower全再構築が支配的なまま
* Render全更新が支配的なまま

---

## Gate 4：論文新規性成立

### Go条件

* B2～B5との差が明確
* A1、A2でデータ型固有の効果が確認できる
* 最新先行研究に同一仕様が見つからない
* 主張範囲がデータ型に限定されている

### No-Go条件

* 新規性が「Segment単位で止める」だけになっている
* 効果が一般的なLODや描画最適化だけで説明される
* 同一データ型が既存研究に存在する

No-Goの場合は、実装・評価研究へ位置付け直す。

---

# 48. 最終的な研究上の位置付け

本研究は、新しい物理Solverを提案する研究ではない。

また、Sleeping、Active Set、Cache Reuse、Guide/Follower、GPU Computeを新しく提案する研究でもない。

本研究では、髪のSegment Groupについて、Static状態とDynamic状態で必要なデータが異なることに着目する。

すべてのGroupが常に同一Payloadを持つ方式ではなく、固定CoreとOptional Dynamic Payloadからなるデータ型を使用する。

状態変更時にもGroup本体のIndexを維持し、有効なPayloadと操作だけを変更する。

さらに、Version、Cache、Payload Lifetime、状態遷移、不変条件、Fallbackをデータ型の正式仕様として定義する。

新規性として検証する対象は、次の一点である。

> 固定IndexのSegment Group Coreと状態依存Optional Dynamic Payloadを持ち、実行時状態に応じて有効データ、許可操作、Cache条件、Payload LifetimeおよびFallback先を変更するGPU常駐型髪データ型。

この範囲を超える主張は行わない。

---

# 49. 結論

本研究では、髪の各Segment Groupを、固定CoreとOptional Dynamic Payloadから構成されるGPU常駐型データとして扱うSG-APTを提案する。

Static Groupは最後に検証された局所方向とRest LengthをCoreから再利用し、Dynamic専用Payloadを保持しない。

Dynamic Groupだけが、速度、過去位置、拘束変数、衝突情報などのPayloadを使用する。

状態変更時にGroup本体を別Bufferへ移動させず、固定Indexを維持したままPayloadの割当と有効操作を変更する。

Rootまたは頭部の移動はSegment番号に比例したXYZ加算で処理せず、剛体差分Transformとして処理する。

Collider、外力、編集、Topology変更などが発生した場合は、影響範囲のGroupまたはBlockだけをDynamicへ戻す。

Static／Dynamic境界ではShared Pointを複数Groupが直接書き換えず、候補生成、Stable Reduce、Single Writerによって最終位置を決定する。

Guideの局所変更はFollower Segment RangeおよびRender Vertex Rangeへ伝播する。

Dynamic割合が高い場合は、GPU実測コストに基づいてFull Dynamic Fast Pathへ切り替える。

本研究の評価では、単純なSleeping、Active List、Static／Dynamic Buffer移動、単純Core／Payload分離、Strongest Practical Baselineと比較する。

これにより、性能改善が一般的な部分更新や描画最適化ではなく、状態依存Payload型によるものかを検証する。

本研究では、世界初、完全保証、必ず60FPSといった過大な表現を使用しない。

比較実験、アブレーション、Ground Truth、長時間試験、決定性試験によって確認された範囲だけを、研究上の貢献として報告する。

最終的には、100,000本、128点、Full HDの高密度髪表示において、物理的整合性、局所編集性、異常時復帰性を維持しながら、変化していない領域のDynamic Payload、物理計算、Follower再構築、描画更新を省略し、安定したリアルタイム処理を目指す。
