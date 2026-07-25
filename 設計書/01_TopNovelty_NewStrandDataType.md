# 新規性01 完全版企画書兼設計書

## Segment-Group Certified Strand Data Type

### 状態・安全証明・共有点責任・局所修復を内包するGPU髪ストランドデータ型

---

# 0. 文書情報

| 項目       | 内容                                             |
| -------- | ---------------------------------------------- |
| 研究対象     | GPU常駐型HairSystem                               |
| 新規性番号    | 新規性01                                          |
| 提案名称     | Segment-Group Certified Strand Data Type       |
| 日本語名称    | 安全証明付きセグメントグループ型髪ストランドデータ                      |
| 主対象      | 4,000 Guide、100,000表示ストランド、128点                |
| 実行環境     | Windows、C++、DirectX 11、HLSL、GPU Compute Shader |
| 最終性能目標   | 1920×1080、100,000本×128点、平均60FPS                |
| 最低研究成立条件 | Full Dynamic方式と比較可能な正式計測環境が存在すること              |
| 後続新規性    | 新規性02〜05の共通データ基盤                               |

---

# 1. エグゼクティブサマリー

本企画では、GPU髪シミュレーションにおけるストランド表現そのものを再設計する。

従来の髪シミュレーションでは、髪を粒子列、ストランド、Guide、Follower、ブロックなどの単位で管理し、計算対象か否かを外部のフラグやスリープ配列によって判断することが多い。

しかし、この構成では次の問題が発生する。

* 髪の一部分だけが動く場合でも、ストランド全体を計算しやすい
* 静止判定の根拠が速度や変位の閾値に依存しやすい
* 静的区間と動的区間の境界で不連続が発生する
* 隣接区間が共有する点への書き込み責任が曖昧になる
* 異常が一部に発生しただけで全体再計算やResetが必要になる
* 物理、編集、描画、GPUスケジューリングが別々の状態を持つ
* CPUが毎フレーム計算対象を選ぶと、転送と同期がボトルネックになる

これに対して本研究では、隣接する2点とその間を結ぶ線分を**Segment Group**として定義する。

各Segment Groupは、位置や長さだけでなく、次の情報をデータ型の内部に持つ。

* 現在状態
* 前状態
* 親・子Group
* 共有点の所有権
* 静的再利用の安全証明
* Dirty理由
* 状態変更要求の優先度
* 衝突・移動・編集の危険度
* キャッシュ情報
* Repair要求
* Repair後の検証状態
* 最終計算フレーム
* 状態安定期間

GPUはこの情報を用いて、各Groupを次の5状態へ分類する。

```text
Static Cached
Static Transformed
Dynamic
Deferred
Repair
```

その後、GPU上で対象Groupを分類、圧縮し、Indirect Dispatchによって必要な処理だけを実行する。

本企画の最終目的は、単なるスリープ最適化ではない。

> 髪の各区間を、状態、安全証明、責任範囲、修復能力を持つ正式なデータ型として定義し、物理・編集・描画・GPU実行制御の共通基盤にする。

これが新規性01の中心的な研究提案である。

---

# 2. 企画の背景

## 2.1 高密度HairSystemの問題

高密度な髪をリアルタイムで表示・シミュレーションする場合、次の処理量が大きくなる。

* 粒子位置の積分
* 距離制約
* 曲げ制約
* Root拘束
* Scalp Collision
* Self Collision
* Follower再構築
* 描画用頂点生成
* GPUバッファ更新
* テレメトリ取得

本研究の固定対象は次のとおりである。

```text
Guide Strand                 4,000本
Follower per Guide           24本
表示ストランド総数           100,000本
1ストランド当たりの点数      128点
総表示粒子数                 12,800,000点
Solver Iteration             6回
Fixed Time Step              1/60秒
解像度                       1920×1080
最終目標                     60FPS
```

現在のViewerにも、4,000 Guide、1 Guide当たり24 Follower、100,000表示ストランド、128点、6反復が固定値として定義されている。

この規模では、すべての髪を毎フレーム同一品質で更新すると、GPU計算量とメモリ帯域が大きくなる。

## 2.2 実際の髪の状態は一様ではない

1フレーム内の髪は、次のような異なる状態を同時に持つ。

* 完全に停止している
* 頭部の移動に追従するだけ
* 慣性で動いている
* 風の影響を受けている
* 衝突周辺だけ動いている
* 編集直後で再計算が必要
* 異常が発生し修復が必要

ストランド全体、Guide全体、固定ブロック全体を同じ状態として扱うと、不要な計算または不正な静的化が発生する。

## 2.3 既存のスリープ判定の限界

速度または変位が小さいという理由だけで静的化すると、次のようなFalse Staticが発生する可能性がある。

* Colliderが接近している
* 隣接Groupが動いている
* Rootが急回転しようとしている
* Editの影響が伝播している
* Static/Dynamic境界が破綻している
* Cacheが古い
* Topologyまたは設定が変化している

このため、「動いていないように見える」という条件だけでは、計算を停止する根拠として不十分である。

---

# 3. 企画上の課題定義

本研究で解決する課題を次の7項目とする。

## 3.1 計算単位が粗い

ストランド全体またはブロック全体を計算単位にすると、一部だけ動いている領域を細かく制御できない。

## 3.2 静的再利用の安全性を証明できない

単一の速度閾値では、Collider、Edit、Root移動、境界誤差を同時に判断できない。

## 3.3 共有点の責任が曖昧である

隣接Groupが同じ点へ書き込むと、競合と非決定性が発生する。

## 3.4 StaticとDynamicの境界が破綻する

位置、速度、接線、長さの不連続が発生し、折れ、隙間、跳ね、振動の原因になる。

## 3.5 異常時の処理範囲が大きい

一部に異常が起きた場合でも、全体をResetまたはFull Dynamicへ戻す方式では負荷が大きい。

## 3.6 CPUとGPUの同期が増える

CPUが全Groupの状態を調べて実行対象を作ると、Readback、Upload、待機が発生する。

## 3.7 後続機能が別々の状態を持つ

物理、編集、描画、GPU常駐処理が独立した状態配列を持つと、判断の不一致が起こる。

---

# 4. 企画目的

本企画の目的は、髪の各区間を自己記述型のSegment Groupへ再定義し、次の機能を一つのデータ型から実現することである。

1. Segment Group単位の状態分類
2. 安全証明付きの静的再利用
3. 頭部移動時の剛体追従
4. Dynamic Groupだけの物理計算
5. 安全な処理延期
6. 異常Groupの局所Repair
7. 共有点の一意な管理
8. Static/Dynamic境界の連続性維持
9. GPU上のDirty伝播
10. Predictive Wake
11. 決定的なGPU Compaction
12. Indirect Dispatch
13. GuideからのFollower再構築
14. 正式テレメトリによる安全性証明
15. 新規性02〜05への共通状態提供

---

# 5. 研究質問

本研究では、次の研究質問に答える。

## RQ1

髪の状態をストランド全体ではなくSegment Group単位で管理することで、見た目を維持しながら物理計算量を削減できるか。

## RQ2

速度閾値だけではなく、Version、誤差、Collider、Edit、境界情報を含むCertificateによって、False Staticを防止できるか。

## RQ3

共有点に明示的な所有権と優先度を導入することで、書き込み競合と非決定性を排除できるか。

## RQ4

異常区間をRepair状態として局所処理することで、全体Resetを避けながら安定性を維持できるか。

## RQ5

分類、Compaction、DispatchをGPU上で完結させることで、CPU全量Uploadと同一フレームReadbackを排除できるか。

## RQ6

Segment Group型を共通基盤とすることで、後続の物理、描画、編集、GPU常駐処理を一貫した判断体系へ統合できるか。

---

# 6. 研究仮説

## H1：計算量削減仮説

髪全体ではなくDynamicまたはRepair Groupだけを処理すれば、Idle、Walk、Head Rotation、Wind StopなどのシナリオでFull DynamicよりGPU時間を削減できる。

## H2：安全性仮説

CertificateとPredictive Wakeを組み合わせることで、False Static Countを0に維持できる。

## H3：境界連続性仮説

共有点所有とBoundary Resolveを導入することで、Static/Dynamic境界の最大位置誤差を2mm以下に抑えられる。

## H4：決定性仮説

Atomic Appendのみに依存せずStable Compactionを採用することで、同一条件のState Sequence HashとCompaction Sequence Hashを一致させられる。

## H5：局所修復仮説

RepairとRepair Verificationを導入することで、異常発生時の全体Resetを避け、修復成功率99%以上を達成できる。

## H6：統合基盤仮説

Segment GroupにState、Risk、Certificate、Dirty、Repairを統合することで、新規性02〜05が独立した状態配列を持つ必要をなくせる。

---

# 7. 提案方式の価値

## 7.1 研究的価値

* ストランドを構成する区間自体を状態付きデータ型として再定義する
* 静的再利用にCertificateという安全証明を導入する
* 共有点の所有責任をデータ型として明示する
* Repairを通常状態モデルへ統合する
* 物理・編集・描画・実行制御の共通基盤を構築する

## 7.2 技術的価値

* 不要な物理計算を削減できる
* CPU-GPU転送量を削減できる
* 局所的な異常へ対応できる
* 状態の可視化とデバッグが容易になる
* 後続機能を同じ状態体系へ接続できる

## 7.3 制作現場における価値

* 高密度な髪をリアルタイムで扱いやすくなる
* 編集直後の局所更新が可能になる
* 異常箇所を特定しやすくなる
* 遠距離・静止領域の計算を抑えられる
* 物理品質と描画品質を同じ安全情報から制御できる

---

# 8. 企画の対象範囲

## 8.1 対象

* 4,000本のGuide Strand
* 1 Guide当たり127 Segment Groups
* 合計508,000 Segment Groups
* 96,000本のFollower再構築
* GPU上の状態分類
* Certificate
* Predictive Wake
* Shared Point Ownership
* Stable Compaction
* Static Cache
* Static Transform
* Dynamic Solve
* Repair
* Boundary Resolve
* Watchdog
* Formal Telemetry

## 8.2 対象外

新規性01単独では、次を主目的としない。

* 新しいCosserat Rodソルバそのものの提案
* 新しい髪シェーディング方式
* 新しい編集UI方式
* CPU中心のスケジューラ
* Followerごとの完全な物理状態
* Hair Asset自動生成手法
* Alembic規格そのものの拡張

これらは後続新規性または周辺機能として扱う。

---

# 9. 成果物

本企画の完成時には、次の成果物が存在する。

## 9.1 研究成果物

* Segment-Group Certified Strand Data Typeの定義
* 状態遷移モデル
* Certificateモデル
* Shared Point Ownershipモデル
* Dirty伝播モデル
* Predictive Wakeモデル
* Repair Verificationモデル
* GPU実行パイプライン
* Ablation Study結果
* Baseline比較結果
* 論文用図表
* 発表用可視化

## 9.2 ソフトウェア成果物

* C++とHLSLで共通化されたSegment Group構造
* Canonical Topology
* GPU分類シェーダー
* Stable Compaction
* DispatchIndirect経路
* Static/Dynamic/Repair経路
* Follower Reconstruction
* デバッグ表示
* Formal Telemetry
* 再現性Hash

## 9.3 評価成果物

* Frame CSV
* Frame JSONL
* Settings JSON
* Summary JSON
* Formal Evidence
* SQLiteまたはDB登録結果
* 比較グラフ
* シナリオ別集計
* Seed別集計
* 失敗理由記録

---

# 10. システム全体像

```text
Hair Asset
    ↓
Canonical Resample
    ↓
Segment Group Topology
    ↓
Segment Group Runtime Records
    ↓
Dirty Event / Version Update
    ↓
Certificate Validation
    ↓
Predictive Wake
    ↓
Priority Resolution
    ↓
Stable Compaction
    ↓
┌───────────────────────────────┐
│ Static Cached                 │
│ Static Transformed            │
│ Dynamic                       │
│ Deferred                      │
│ Repair                        │
└───────────────────────────────┘
    ↓
Shared Boundary Resolve
    ↓
Follower Reconstruction
    ↓
Rendering
    ↓
Delayed Formal Telemetry
```

---

# 11. Canonical Hair Topology

## 11.1 Guide構造

各Guideは128点へ統一する。

```text
P0 ─ P1 ─ P2 ─ ... ─ P127
 G0   G1           G126
```

1本のGuideにつき127 Groupを生成する。

```text
4,000 × 127 = 508,000 Segment Groups
```

## 11.2 Arc Length Resample

入力曲線の累積長を求め、全長を127等分して128点を配置する。

Resample対象は次のとおりである。

* Position
* Width
* Root属性
* Material属性
* Guide属性
* Follower対応属性

## 11.3 Canonical条件

```text
全Guideが128点
全Guideが127 Group
Zero Length Segment = 0
Non-Finite Point = 0
点間距離偏差が規定内
Topology Hashが固定
保存・再読込後も同一Topology
```

既存の型には、Segment Group Topology、Block情報、Topology Hash、平均Segment Length、最大偏差を持つ領域が存在する。

---

# 12. GuideとFollowerの役割

## 12.1 Guide

重いRuntime Recordと物理状態を持つ。

対象：

* 4,000 Guide
* 508,000 Segment Groups

## 12.2 Follower

Guideの最終状態から再構築する。

Followerは次の軽量情報のみを持つ。

```text
Parent Guide ID
対応Segment Group
Root Offset
Follow Weight
Clump Weight
Noise Weight
Width
Material
```

Followerは独立した重いCertificate、Repair Record、State Transition履歴を持たない。

## 12.3 Follower再構築条件

Follower再構築は次の処理後に1度だけ行う。

```text
Dynamic Solve
Static Transform
Repair
Boundary Resolve
Cache Update
```

Solver Iteration内でFollowerを繰り返し再構築してはならない。

---

# 13. Segment Group正式データ型

最終的なSegment Group Recordは次の情報を持つ。

```text
SegmentGroupRecord
├─ strandId
├─ localGroupId
├─ globalGroupId
├─ parentGroupId
├─ childGroupId
├─ startPointIndex
├─ endPointIndex
├─ blockId
├─ sharedPointOwner
├─ currentState
├─ previousState
├─ requestedState
├─ dirtyReasonMask
├─ requestPriority
├─ restLength
├─ currentLength
├─ cachedDirection
├─ previousDirection
├─ cachedVelocity
├─ motionRisk
├─ collisionRisk
├─ editRisk
├─ boundaryErrorMm
├─ errorBoundMm
├─ lastSolvedFrame
├─ lastRepairFrame
├─ stateStableFrameCount
├─ certificateIndex
└─ runtimeFlags
```

現在のコードにも、State、Dirty Reason、長さ、方向、境界誤差、最終計算フレームを持つ基本構造が存在する。

完成版では、Viewer内の補助構造ではなく、HairSystem全体の正式データ型として扱う。

---

# 14. 状態モデル

## 14.1 Static Cached

完全に静止し、前回結果を安全に再利用できる状態。

実行内容：

* 通常物理計算を行わない
* Cacheを使用する
* Certificateを検証する
* Watchdog対象に残る
* Follower再構築の入力になる

遷移条件：

```text
Certificate Valid
AND Motion Riskが閾値以下
AND Collision Riskが閾値以下
AND Edit Riskが0
AND Boundary Errorが閾値以下
AND Stable Frame数が規定以上
```

## 14.2 Static Transformed

局所形状は変化せず、Rootまたは頭部の剛体移動へ追従する状態。

必要な処理：

* Translation
* Rotation
* Bone Transform
* Root Local Frame変換
* Scalp Animation追従

現在のStatic Transformは、終点へ移動量を加える処理を中心とした仮構造である。

完成形では、頭部回転を含む差分変換を適用する。

## 14.3 Dynamic

通常物理計算が必要な状態。

処理対象：

* Integration
* Gravity
* Inertia
* Distance Constraint
* Bend Constraint
* Root Constraint
* Scalp Collision
* Self Collision
* Wind
* Damping
* Velocity Update

Dynamic Groupは、新規性01専用の簡易ソルバではなく、既存Hair Solverへ接続する。

現在の仮Dynamic Solveは、終点をRest Lengthに近づける単純補正が中心である。

## 14.4 Deferred

安全性を失わず、完全物理計算を一時的に延期する状態。

Deferred可能条件：

* Certificateが有効
* Collision Riskが低い
* Edit影響がない
* Boundary Errorが小さい
* Root運動が小さい
* Repair対象ではない

Deferred禁止条件：

```text
Collision中
Edit直後
Certificate失効
NaN/Inf
Boundary Error超過
Repair要求
Force Dynamic
急激なRoot運動
```

## 14.5 Repair

通常計算を継続すると危険な状態。

Repair対象：

* NaN
* Inf
* 長さ破綻
* Boundary Error超過
* Shared Point Conflict
* Collision Miss
* 大規模な貫通
* Cache不正
* Certificate矛盾
* Watchdog違反

Repair後は直ちに成功としない。

```text
Repair Requested
↓
Repair Executed
↓
Pending Verification
↓
一定期間再検証
↓
Verified / Failed
```

既存実装には、修復後の再検証期間として12フレーム相当の設定が存在する。

---

# 15. 状態遷移

```text
Dynamic
├─ 安定継続 → Static Cached
├─ Root剛体移動 → Static Transformed
├─ 安全な予算不足 → Deferred
└─ 異常 → Repair

Static Cached
├─ Root剛体移動 → Static Transformed
├─ Wake条件 → Dynamic
└─ 異常 → Repair

Static Transformed
├─ 変換後も安全 → Static Cached
├─ 衝突・非剛体変形 → Dynamic
└─ 境界破綻 → Repair

Deferred
├─ 予算回復 → Dynamic
├─ 安定確認 → Static Cached
└─ Risk上昇 → DynamicまたはRepair

Repair
├─ 修復検証成功 → Dynamic
├─ その後安定 → Static Cached
└─ 修復失敗 → Repair継続または安全Fallback
```

---

# 16. 状態要求の優先度

複数の状態要求が同一Groupに発生した場合、次の優先順位で解決する。

```text
1. Fatal / Reject
2. Collision
3. Edit
4. Force Dynamic
5. Force Static
6. Normal
```

重要原則：

* Force StaticはCollisionを上書きできない
* Force StaticはEditを上書きできない
* 未検証状態をStaticにしない
* 不明な状態はDynamicまたはRepairへ倒す
* 同一優先度ではGroup IDなどの決定的規則を使用する

---

# 17. Certificate設計

## 17.1 目的

Certificateは、Groupを物理計算せず再利用しても安全であることを示す。

## 17.2 Certificate構造

```text
SegmentGroupCertificate
├─ sourceFrame
├─ topologyVersion
├─ headTransformVersion
├─ scalpVersion
├─ colliderVersion
├─ editVersion
├─ cacheVersion
├─ maximumPositionErrorMm
├─ maximumDirectionErrorDeg
├─ maximumLengthErrorRatio
├─ maximumBoundaryErrorMm
├─ topologyValid
├─ parentValid
├─ collisionValid
├─ editValid
├─ boundaryValid
└─ cacheValid
```

現在のコードにも、Parent、Collider、Boundary、Edit、Cacheの有効性と各種誤差を持つCertificateが存在する。

## 17.3 Certificate Valid条件

```text
Topology Version一致
AND Head Transform整合
AND Scalp Version整合
AND Collider Version一致
AND Edit Version一致
AND Cache Version一致
AND Position Errorが閾値以下
AND Direction Errorが閾値以下
AND Length Errorが閾値以下
AND Boundary Errorが閾値以下
AND NaN/Infなし
```

## 17.4 False Static

DynamicまたはRepairが必要であるにもかかわらず、Staticとして再利用されたGroupをFalse Staticと定義する。

正式合格条件：

```text
False Static Count = 0
```

---

# 18. Dirty Event設計

CPUは全Groupを直接書き換えず、小規模なEventだけを送る。

```text
DirtyEvent
├─ eventType
├─ targetStrand
├─ firstGroup
├─ lastGroup
├─ version
├─ priority
└─ eventData
```

Dirty Reason：

* Parent Moved
* Head Moved
* Collider Entered
* Collider Changed
* Neighbor Changed
* Edited
* Cache Expired
* Boundary Failed
* Watchdog
* Force Dynamic
* Force Static

GPUは対象BlockをDirtyにし、必要なGroupだけへ伝播する。

正常実行では次を満たす。

```text
Full CPU Group Upload Count = 0
```

現在のコードでは、頭部移動時に全Groupを更新する経路が一時的Fallbackであることが明記されている。

---

# 19. Block Hierarchy

508,000 Groupすべてを毎フレーム詳細判定すると、分類処理自体が負荷になる。

複数GroupをBlockとしてまとめる。

```text
SegmentGroupBlock
├─ firstGroupIndex
├─ groupCount
├─ strandId
├─ stateMask
├─ dirtyMask
├─ dynamicGroupCount
├─ repairGroupCount
├─ maxMotionRisk
├─ maxCollisionRisk
└─ maxBoundaryError
```

現在のコードにも、Group範囲、State Mask、Dirty Mask、最大リスクを持つBlock構造が存在する。

処理順：

```text
Block Classification
↓
Dirtyまたは危険BlockのみGroup Classification
```

完全にStaticで安全なBlockは詳細分類を省略する。

---

# 20. Predictive Wake

## 20.1 目的

実際に大きな誤差が発生する前に、将来Dynamic化する可能性が高いGroupを起こす。

## 20.2 Wake要因

* Parent GroupがDynamic
* Child GroupがDynamic
* Collider接近
* Root速度上昇
* Root角速度上昇
* Wind急変
* Local Edit
* Boundary Error増加
* Certificate失効予兆
* Cache寿命超過

## 20.3 評価指標

```text
Predictive Wake Evaluated Count
True Positive
False Positive
False Negative
Precision
Recall
Wake Delay
Wake Position Error
Wake Velocity Error
```

目標値：

```text
Recall    ≥ 0.99
Precision ≥ 0.90
```

False Negativeを最優先で防止する。

---

# 21. 共有点所有設計

隣接Groupは内部点を共有する。

```text
Parent Group : P0 ─ P1
Child Group  :      P1 ─ P2
```

P1へ両方が同時に最終書き込みすると、競合と非決定性が発生する。

## 21.1 Ownerの一意性

各共有点について、最終書き込みOwnerを必ず一つ決定する。

## 21.2 半球分割

共有点`P[i]`の局所接線を次のように定義する。

```text
T = normalize(P[i+1] - P[i-1])
```

共有点周辺を次の2領域へ分ける。

* `T`方向：Child Hemisphere
* `-T`方向：Parent Hemisphere

## 21.3 担当規則

* Parent側の拘束・衝突：Parent Group
* Child側の拘束・衝突：Child Group
* 両側から要求：Priorityで決定
* 同一Priority：小さいGroup IDを採用

## 21.4 合格条件

```text
Shared Point Conflict Count = 0
```

現在の実装では、共有点Ownerが前方Groupへ固定されている。

完成版では、半球、優先度、決定性規則を用いたOwner解決へ発展させる。

---

# 22. Stable Compaction

分類結果から次の実行リストをGPU上で生成する。

```text
DynamicGroupList
StaticGroupList
RepairGroupList
```

Atomic Appendだけに依存すると、実行順によって出力順序が変化する可能性がある。

そのため、次の安定圧縮を使用する。

```text
Flag Pass
↓
Block Prefix Sum
↓
Global Prefix Sum
↓
Stable Scatter
```

出力順序はGroup ID順とする。

同一条件で次のHashが一致する必要がある。

```text
State Sequence Hash
Compaction Sequence Hash
Final Position Hash
```

既存テレメトリにもこれらのHash領域が存在する。

---

# 23. Indirect Dispatch

Compact List件数からGPU上で実行引数を生成する。

```text
DynamicCount → DynamicDispatchArgs
StaticCount  → StaticDispatchArgs
RepairCount  → RepairDispatchArgs
```

CPUは件数を同一フレームにReadbackしない。

```text
Same Frame Readback Count = 0
```

現在のHLSLにも、Dynamic、Static、Repair用のIndirect Argsを生成する基本経路が存在する。

---

# 24. Static Cache

Static Cached Groupは、毎フレーム完全な物理計算を行わない。

Cache候補：

* Local Direction
* Rest Length
* Local Root Frame
* Cached Velocity
* Error Bound
* Source Frame
* Cache Version

評価項目：

```text
Cache Hit Count
Cache Miss Count
Cache Invalidation Count
Direction Cache Memory
Full Position Cache Memory
Memory Saving Rate
```

Cacheが不明または古い場合は安全側へ倒す。

---

# 25. Static Transform

Static Transformed Groupは、局所形状を維持しながらRootまたは頭部の差分変換へ追従する。

必要な処理：

* Translation
* Rotation
* Bone Transform
* Root Local Frame変換
* Scalp Animation

維持条件：

```text
Rest Length維持
Local Direction維持
共有点連続性維持
Certificate Error Bound内
```

非剛体変形、Collider接近、境界誤差超過時はDynamicへ移行する。

---

# 26. Dynamic Solve

Dynamic Groupは、既存Hair Solverの計算対象となる。

必要な処理：

* Integrate
* Root Bind
* Distance Constraint
* Bend Constraint
* Collision
* Self Collision
* Wind
* Damping
* Velocity Update

新規性01は物理を簡略化する提案ではない。

新規性01が決定するのは次の点である。

> どのGroupを、どの状態として、どの物理経路へ送るか。

共有点Owner以外は、共有点へ直接最終書き込みせず、候補値または拘束寄与を出力する。

---

# 27. Velocity Recovery

StaticからDynamicへ移行するとき、Previous PositionやVelocityが不適切だと跳ねが発生する。

復元に使用する情報：

* Cached Velocity
* Root移動速度
* Root角速度
* Parent Group速度
* Child Group速度
* 最終Static Frame
* Wake Delay

合格条件：

* Wake時に目立つ瞬間移動がない
* 不自然な速度スパイクがない
* Full Dynamicと比較して跳ねが増加しない

---

# 28. Static/Dynamic Boundary Resolve

StaticとDynamicの境界では、次の連続性を保証する。

* Position
* Tangent
* Length
* Velocity
* Bend Direction

評価指標：

```text
Boundary Position Error
Boundary Direction Error
Boundary Velocity Error
Boundary Length Error
```

目標：

```text
Static/Dynamic Boundary Max Error ≤ 2.0 mm
```

規定値を超えた場合はRepairへ移行する。

---

# 29. Repair設計

## 29.1 Repair対象

* Non-Finite Position
* Non-Finite Velocity
* Length Error超過
* Boundary Error超過
* Shared Point Conflict
* Collision Miss
* Penetration
* Certificate Failure
* Cache Failure
* Watchdog Failure

## 29.2 Repair内容

* 直前の有効方向を利用
* Rest Lengthを再適用
* ParentまたはRootから再構築
* Previous Positionを再設定
* Shared Point Ownerを再解決
* Certificateを無効化
* Dynamicへ復帰

## 29.3 Repair Verification

評価項目：

```text
Repair Attempt Count
Repair Execution Success Count
Repair Verified Count
Repair Unverified Count
Repair Failure Count
Repair Recurrence Count
Repair Verification Latency
```

目標：

```text
Repair Verified Success Rate ≥ 0.99
Repair Recurrence Rate       ≤ 0.01
Repair Failure Count         = 0
```

---

# 30. Watchdog

Watchdogは、一定間隔でStatic Groupの安全性を再検証する。

確認項目：

* Position Finite
* Velocity Finite
* Length Error
* Boundary Error
* Certificate Version
* Collision Risk
* Shared Point整合性
* Cache整合性

異常検出時はRepairへ移行する。

---

# 31. 1フレームの正式パイプライン

```text
1. Scenario Event適用
2. Edit Event適用
3. Root・Collider・Edit Version更新
4. Dirty Event Upload
5. Dirty Block Mark
6. Dirty Group Propagation
7. Block Classification
8. Group Certificate Validation
9. Predictive Wake
10. Request Priority Resolve
11. Stable Compaction
12. Indirect Dispatch Args生成
13. Static Cached再利用
14. Static Transformed変換
15. Dynamic Solve
16. Velocity Recovery
17. Repair Solve
18. Shared Boundary Resolve
19. Repair Verification
20. Cache Update
21. Watchdog
22. Follower Reconstruction
23. Render Preparation
24. Delayed Telemetry Reduction
```

同一フレームで分類、Compaction、Static Transform、Repairを複数経路から重複実行してはならない。

---

# 32. GPU常駐条件

通常実行中、次をGPU上で完結させる。

* Dirty伝播
* Certificate検証
* State分類
* Predictive Wake
* Priority Resolve
* Stable Compaction
* Dispatch Args生成
* Static Transform
* Dynamic Solve
* Repair
* Boundary Resolve
* Follower Reconstruction
* Counter Reduction

CPUへ戻すのは、遅延した集計値と最終実験結果のみとする。

---

# 33. 安全Fallback

次の場合、安全側へ倒す。

* Certificate不明
* Shader失敗
* Buffer不足
* Compaction Overflow
* Indirect Args異常
* Counter不整合
* Topology不一致
* Asset Hash不一致
* NaN/Inf
* Repair失敗

優先順：

```text
Repair
↓
Dynamic
↓
必要に応じてFull Dynamic
↓
Formal RunではFailure
```

不明な状態をStaticにしてはならない。

---

# 34. デバッグ可視化

状態表示：

```text
Static Cached       青
Static Transformed  水色
Dynamic             緑
Deferred            黄
Repair              赤
Invalid Certificate 紫
```

選択Groupについて表示する情報：

* Current State
* Previous State
* Dirty Reason
* Request Priority
* Shared Point Owner
* Certificate Validity
* Version情報
* Motion Risk
* Collision Risk
* Edit Risk
* Boundary Error
* Last Solved Frame
* Repair Status

通常描画ではデバッグ色を使用しない。

---

# 35. 正式テレメトリ

既存テレメトリには、Segment Group状態、Certificate、Wake、Repair、Boundary、Compaction、GPU Pass時間などの多くの項目が定義されている。

## 35.1 状態

* Total Segment Group Count
* Static Cached Count
* Static Transformed Count
* Dynamic Count
* Deferred Count
* Repair Count

## 35.2 Dirty

* Dirty Block Count
* Dirty Group Count
* Dirty Propagation Count
* Full CPU Group Upload Count

## 35.3 Certificate

* Valid Count
* Invalid Count
* Topology Version Mismatch
* Head Version Mismatch
* Collider Version Mismatch
* Edit Version Mismatch
* Cache Failure

## 35.4 Wake

* Wake Request Count
* True Positive
* False Positive
* False Negative
* Precision
* Recall
* Wake Delay
* Position Error
* Velocity Error

## 35.5 Repair

* Attempt
* Execution Success
* Verified Success
* Failure
* Recurrence
* Verification Latency

## 35.6 境界

* Shared Point Conflict
* Parent Hemisphere Resolve
* Child Hemisphere Resolve
* Boundary Mean Error
* Boundary Max Error

## 35.7 Cache

* Cache Hit
* Cache Miss
* Cache Invalidation
* Direction Cache Memory
* Full Position Memory
* Memory Saving Rate

## 35.8 GPU時間

* Block Classification
* Group Classification
* Predictive Wake
* Priority Resolve
* Compaction
* Static Transform
* Dynamic Solve
* Velocity Recovery
* Repair
* Boundary Resolve
* Cache Update
* Watchdog
* Follower Reconstruction
* Render Preparation
* Total Hair GPU Time

## 35.9 再現性

* State Sequence Hash
* Compaction Sequence Hash
* Final Position Hash
* Asset Hash
* Topology Hash
* Settings Hash
* Shader Hash
* Executable Hash

未取得値を0と記録してはならない。

取得不能な指標はUnavailableとし、Formal Runでは不合格とする。

---

# 36. 比較対象

```text
B0 Full Dynamic
B1 Strand Sleep
B2 Block Sleep
B3 Segment Threshold Sleep
B4 Segment Hysteresis Sleep
B5 Segment Cache
B6 Segment GPU Compaction
B7 Active Set Only
B8 External State Array
P  Integrated Segment Group
```

提案方式Pでは、次をSegment Group型へ統合する。

* State
* Certificate
* Shared Point Ownership
* Dirty Reason
* Risk
* Repair
* Compaction情報

---

# 37. Ablation Study

新規性01の内部機能を個別に無効化して効果を確認する。

1. Certificateなし
2. Dirty Propagationなし
3. Block Hierarchyなし
4. Boundary Repairなし
5. Watchdogなし
6. Hemisphere Ownershipなし
7. Indirect Dispatchなし

既存Feature Flagにも、これら7種類の内部Ablation項目が定義されている。

評価対象：

* FPS
* GPU Time
* False Static
* Boundary Error
* Shared Point Conflict
* Wake Precision/Recall
* Repair Count
* Collision Miss
* Determinism
* VRAM
* Bandwidth

---

# 38. 評価シナリオ

```text
S01 Idle
S02 Breathing
S03 Walk
S04 Sudden Stop
S05 Head Rotation
S06 Teleport
S07 Collider Approach
S08 Static/Dynamic Boundary Contact
S09 Strong Wind
S10 Wind Stop
S11 Local Edit
S12 False Static Trap
S13 Repair Storm
S14 All Dynamic
S15 All Static
S16 State Thrashing
S17 Close Camera 100k
S18 Far Camera 100k
```

既存の実験構造にもS01〜S18が定義されている。

---

# 39. 実験条件

正式比較では次を固定する。

```text
Release x64
1920×1080
VSync無効
Fixed Time Step 1/60
Guide 4,000
Follower per Guide 24
表示ストランド100,000
128点
Solver Iteration 6
Frozen Asset
同一Scenario Timeline
同一Seed
同一実行バイナリ
同一Shader Bundle
```

複数Seedを使用し、BaselineとProposalを対応付ける。

---

# 40. 評価指標

## 40.1 性能

* Average FPS
* Average Frame Time
* p95 Frame Time
* p99 Frame Time
* p99.9 Frame Time
* Frame Budget Overrun Rate
* Total Hair GPU Time
* Pass別GPU Time
* VRAM
* Bandwidth

## 40.2 安全性

* False Static
* NaN
* Inf
* Fatal Failure
* Collision Miss
* Penetration
* Shared Point Conflict
* Boundary Error
* Repair Failure

## 40.3 Wake

* Precision
* Recall
* Delay
* Position Error
* Velocity Error

## 40.4 再現性

* State Hash
* Compaction Hash
* Final Position Hash
* Seed間分散
* 実行間一致率

## 40.5 見た目

* Full Dynamicとの差
* 頭頂部の自然さ
* 毛先密度
* 前髪の板状化
* 境界の折れ
* Flicker
* Repair時の跳ね
* Follower分離
* 過伸長
* Collision貫通

---

# 41. 成功条件

## 41.1 必須安全条件

```text
NaN Count                          = 0
Inf Count                          = 0
Fatal Failure Count                = 0
False Static Count                 = 0
Shared Point Conflict Count        = 0
Determinism Mismatch Count         = 0
Full CPU Group Upload Count        = 0
Same Frame Readback Count          = 0
Unverified Repair at End           = 0
Boundary Max Error                 ≤ 2.0 mm
```

## 41.2 Wake・Repair条件

```text
Predictive Wake Recall             ≥ 0.99
Predictive Wake Precision          ≥ 0.90
Repair Verified Success Rate       ≥ 0.99
Repair Recurrence Rate             ≤ 0.01
Repair Failure Count               = 0
```

## 41.3 性能目標

```text
Average FPS                        ≥ 60
p95 Frame Time                     ≤ 16.67 ms
p99 Frame Time                     ≤ 20.00 ms
GPU Timing Coverage                ≥ 0.99
Frame Budget Overrun Rate          ≤ 0.05
```

24FPSは新規性実装前環境の最低成立条件とし、新規性01の最終目標は60FPSとする。

---

# 42. 見た目の合格条件

Full Dynamicと比較して、次の問題を増加させない。

* 頭頂部の不自然さ
* 毛先の密度不足
* 前髪の板状化
* 髪の過伸長
* Static/Dynamic境界の折れ
* 頭部回転時の瞬間移動
* Wind停止後の不自然な固定
* Repair時の跳ね
* Follower分離
* Temporal Flicker
* Collision貫通

静止画だけでなく動画で評価する。

---

# 43. 想定される研究成果

本企画が成功した場合、次の結果が期待される。

## 43.1 性能

* Idleや低運動シーンでDynamic Group数が大幅に減少する
* Constraint SolverとCollisionの実行量が減少する
* CPU全量Uploadを排除できる
* GPU上で処理対象を決定できる
* 100,000本×128点で60FPSへ近づく

## 43.2 品質

* Full Dynamicに近い形状品質を維持する
* Static/Dynamic境界を視認しにくくする
* Wake時の跳ねを抑える
* Repairで局所異常を復旧する

## 43.3 安全性

* False Staticを防止する
* Shared Point Conflictを排除する
* 不正Cacheを再利用しない
* 未知状態を安全側へ倒す

## 43.4 拡張性

* 新規性02〜05が同じStateとCertificateを利用できる
* Hair以外の線状・鎖状シミュレーションへ応用できる可能性がある

---

# 44. リスクと対策

## 44.1 分類処理自体が重くなる

リスク：

細かいGroup単位の判定により、分類コストが増える。

対策：

* Block Hierarchy
* Dirty Block限定分類
* Stable Cache
* 全Groupの毎フレーム詳細判定を避ける

## 44.2 Certificateが複雑になりすぎる

リスク：

Certificate更新が物理計算削減量を上回る。

対策：

* Version比較を中心にする
* 高コストな誤差検証をDirty Groupに限定
* Watchdogを毎フレーム実行しない

## 44.3 False Staticが発生する

リスク：

見た目の破綻または衝突見逃しが起こる。

対策：

* Predictive Wake
* 安全側のPriority
* Certificate失効時Dynamic化
* Watchdog
* Formal Ground Truth比較

## 44.4 Shared Point競合が残る

リスク：

振動、非決定性、境界破綻が起こる。

対策：

* Owner一意性
* Hemisphere Ownership
* Priority Resolve
* Stable Group ID規則
* Conflict Counter

## 44.5 Static復帰時に跳ねる

リスク：

Previous PositionまたはVelocityが不連続になる。

対策：

* Cached Velocity
* Velocity Recovery
* Wake前倒し
* Boundary Resolve

## 44.6 Stable Compactionが高コストになる

リスク：

Prefix Sumの負荷がAtomic Appendより大きくなる。

対策：

* Block単位Scan
* Dirty Block限定
* Determinism試験とPerformance試験を分離して比較
* 必要なら高速版と完全決定版を明確に区別する

## 44.7 Repairが常態化する

リスク：

Repair Groupが増加し、性能と品質が低下する。

対策：

* Repair原因別Counter
* Recurrence Rate計測
* Verified成功のみを成功扱い
* 原因別Fallback

## 44.8 Follower再構築がボトルネックになる

リスク：

Guide計算を削減しても96,000 Followerの更新が支配的になる。

対策：

* 最終Guide状態から1回だけ再構築
* Solver Iteration内更新禁止
* 新規性03・05との連携
* Follower Reconstruction GPU Timeを独立計測

---

# 45. 新規性02〜05との接続

## 45.1 新規性02：Budgeted Physics

入力：

* State
* Motion Risk
* Collision Risk
* Repair Requirement
* Dynamic Compact List

利用目的：

* Solver Iteration配分
* Collision Budget配分
* Physics Island形成
* Rod Repair対象選定
* Deferred判定

## 45.2 新規性03：Certificate-Aware Rendering

入力：

* Certificate
* State
* Error Bound
* Boundary Risk
* Camera Distance

利用目的：

* 描画再利用
* Coarse Opacity
* Strand Raster
* Shadow品質
* Specular品質

## 45.3 新規性04：Per-Strand Editing

```text
Edit
↓
対象GroupのEdit Version更新
↓
局所Dirty伝播
↓
Force Dynamic
↓
局所物理またはRepair
↓
Certificate再生成
↓
安定後Static復帰
```

## 45.4 新規性05：GPU-Resident Runtime

入力：

* State
* Compact List
* Dirty Event
* Certificate
* Repair Queue

利用目的：

* DispatchIndirect
* DrawIndirect
* GPU Repair
* Follower Reconstruction
* Render Preparation
* Delayed Telemetry

後続新規性は、独立した状態配列や安全判定を新設せず、新規性01を共通基盤として利用する。

---

# 46. 企画としての最終成果判定

本企画は、次の3段階すべてを満たした場合に研究として成功とする。

## 46.1 概念成立

* Segment Groupが正式なデータ型になっている
* State、Certificate、Ownership、Repairが統合されている
* 従来の外部スリープ配列との差が明確である

## 46.2 技術成立

* GPU上で分類・圧縮・Indirect Dispatchが動作する
* Dynamic Groupが既存Hair Solverへ接続される
* Static Transformが回転へ対応する
* Shared Point Conflictが発生しない
* Repairが再検証される

## 46.3 研究成立

* Baseline比較が可能
* Ablation Studyが可能
* 複数Seedで再現できる
* False Static、安全性、性能を実測できる
* Full Dynamicとの見た目比較ができる
* 効果が正式テレメトリで証明される

---

# 47. 100点満点の評価配分

## 47.1 企画・研究独自性：20点

| 項目                             | 点数 |
| ------------------------------ | -: |
| Segment Groupを正式な状態付きデータ型として定義 |  5 |
| Certificateによる安全再利用            |  5 |
| 共有点所有と半球分割                     |  4 |
| Repairを状態モデルへ統合                |  3 |
| 新規性02〜05の共通基盤                  |  3 |

## 47.2 正しさ・安全性：25点

| 項目                      | 点数 |
| ----------------------- | -: |
| False Staticが0          |  5 |
| NaN・Inf・Fatalが0         |  4 |
| Shared Point Conflictが0 |  4 |
| Boundary Errorが規定内      |  4 |
| Collision Miss・大規模貫通なし  |  4 |
| Repair再検証成立             |  4 |

## 47.3 GPU実装完成度：20点

| 項目                         | 点数 |
| -------------------------- | -: |
| GPU Dirty Propagation      |  4 |
| GPU Certificate Validation |  3 |
| Stable Compaction          |  4 |
| DispatchIndirect           |  3 |
| CPU全量Uploadなし              |  3 |
| Same Frame Readbackなし      |  3 |

## 47.4 物理・見た目：15点

| 項目                  | 点数 |
| ------------------- | -: |
| Full Dynamic同等の物理品質 |  5 |
| 頭部移動・回転への自然な追従      |  3 |
| 境界が視認できない           |  3 |
| Follower再構築が正しい     |  2 |
| Flicker・跳ねなし        |  2 |

## 47.5 性能：10点

| 項目                    | 点数 |
| --------------------- | -: |
| 100,000本×128点で平均60FPS |  4 |
| p95が16.67ms以下         |  2 |
| p99が20ms以下            |  2 |
| VRAM・Bandwidth削減を実測   |  2 |

## 47.6 再現性・評価可能性：10点

| 項目                    | 点数 |
| --------------------- | -: |
| Frozen Asset・Hash固定   |  2 |
| 5 Seedで再現可能           |  2 |
| 状態・圧縮・位置Hash一致        |  2 |
| 必須Telemetryが実測値       |  2 |
| Baseline・Ablation比較成立 |  2 |

```text
合計 100点
```

---

# 48. 100点の最終チェックリスト

```text
□ Segment GroupがHairSystemの正式データ型である
□ GuideがCanonicalな128点構造である
□ 4,000 Guideから508,000 Groupが決定的に生成される
□ Stateがデータ型内部に統合されている
□ CertificateがVersionと誤差を検証している
□ False Staticが0である
□ Shared Point Ownerが必ず一つに決まる
□ Shared Point Conflictが0である
□ Static Cachedが安全に再利用される
□ Static Transformが平行移動と回転へ対応する
□ Dynamic Groupが既存Hair Solverを使用する
□ Deferredが安全なGroupだけに適用される
□ Repairが実行後に再検証される
□ Predictive Wake Recallが0.99以上である
□ Stable Compactionが決定的である
□ DispatchIndirectがGPU上で生成される
□ 正常フレームのCPU全量Uploadが0である
□ Same Frame Readbackが0である
□ Boundary Max Errorが2mm以下である
□ FollowerがGuide確定後に一度だけ再構築される
□ State Hashが一致する
□ Compaction Hashが一致する
□ Final Position Hashが一致する
□ Full Dynamicと同等の見た目を維持する
□ 100,000本×128点で平均60FPSを目標達成する
□ 新規性02〜05が同じSegment Group情報を利用する
□ 正式Telemetryで効果と安全性を証明できる
```

---

# 49. 最終完成形

新規性01の完成形は、次の一文で表現できる。

> 髪の各区間が、自身の状態、安全証明、共有点責任、物理必要性、修復必要性を保持し、その情報を用いてGPU上で静的再利用、剛体追従、物理計算、局所修復、編集影響、描画判断、実行スケジュールが統合的かつ決定的に制御されるHairSystem。

本企画が完成したとき、新規性01は単なる高速化機能ではなく、HairSystem全体のデータ表現と実行方式を再定義する研究成果として成立する。
