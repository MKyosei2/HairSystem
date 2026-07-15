# 01 安全版企画書兼設計書

## Segment Group Certified Strand Runtime Data Representation

---

## 0. 文書情報

| 項目 | 内容 |
|---|---|
| 対象リポジトリ | `MKyosei2/HairSystem` |
| 対象ブランチ | `Novelty` |
| 基準日 | 2026年7月15日 |
| 文書バージョン | `01-Safe v2.0` |
| 対象表示本数 | 100,000本 |
| 主評価環境 | GTX 1660級GPU、旧世代6コアCPU、RAM 32GB以下 |
| 研究対象 | 線状構造をSegment Group単位で状態適応的に保存・更新・復旧・GPU実行するランタイムデータ表現 |
| 新規性評価方針 | 個別技術の新規性ではなく、表現・所有権・安全判定・復旧・GPU実行の統合を評価する |

本書は、既存の01コアを変更せず、新規性の主張範囲、形式仕様、比較実験、再現可能性、失敗時の安全策を強化した企画書兼設計書である。

---

# 1. 企画概要

## 1.1 背景

リアルタイム髪シミュレーションでは、表示される全ストランドを毎フレーム完全計算すると、物理計算、衝突判定、Follower再構成、描画準備、透明描画の総コストが大きくなる。

一方、髪全体が常に同じ強度で動くわけではない。静止に近い部分、親変換だけで追従できる部分、局所的に物理計算が必要な部分、誤静止や貫通から復旧すべき部分が同時に存在する。

既存のSleeping、LOD、Guide/Follower、Active-Set、Dirty Flag、Cache Reuse、GPU Compactionは、この問題を部分的に扱える。しかし、それらを髪の局所線状構造の保存単位、更新単位、安全判定単位、復旧単位、GPU実行単位として一貫して統合する設計が必要である。

## 1.2 目的

本研究の目的は、髪ストランドを重複するSegment Groupへ分割し、各Groupが状態、局所形状、共有点所有権、安全証明、Wake情報、Repair情報を保持する状態適応型ランタイムデータ表現を設計・実装・評価することである。

性能目標は、100,000本表示時に、通常シーンで計算対象を局所化しつつ、誤静止、境界断裂、Wake時の跳ね、衝突見逃しを安全機構で抑えることである。

60 FPSは保証値ではなく、指定条件下で検証する研究目標とする。

## 1.3 研究仮説

### H1：表現統合の効果

同じ判定ロジックを外付け配列で管理する方式より、Segment Groupへ状態と安全情報を統合した方式の方が、GPUメモリアクセス、状態不整合、境界修復、p95・p99フレーム時間を改善する。

### H2：安全性

Static Certificate、Predictive Wake、速度復元、Repairを統合することで、単純Segment SleepよりFalse Static、Wake遅延、境界誤差、衝突見逃しを抑制できる。

### H3：実時間性

DynamicおよびRepair対象をGPU上で圧縮し、Indirect Dispatchへ接続することで、静止Groupの削減が実際のGPU時間削減へ反映される。

---

# 2. 安全な新規性主張

## 2.1 主張する内容

本研究で提案するのは、次の要素を一体化した状態適応型ランタイムデータ表現である。

1. 共有点を持つ重複Segment Group
2. 共有点の一意な最終書き込み所有権
3. 局所方向に基づく隣接Group間の責任分割
4. Group単位の状態と状態依存保存形式
5. Static Certificateによる静止安全判定
6. Predictive Wakeによる事前復帰
7. 親運動を考慮したWake速度復元
8. False Static、貫通、境界不一致、キャッシュ破損を扱うRepair
9. 状態別GPU CompactionおよびIndirect Dispatch
10. Active-Set予算制御との接続

## 2.2 主張しない内容

以下は単独では新規性として主張しない。

- 点、線分、Particle、Rodによる髪表現
- Guide/Follower
- Sleeping
- Dirty Flag
- Cache Reuse
- PBD、XPBD、Rod Solver
- Active-Set
- Prefix Sum、Stream Compaction、Indirect Dispatch
- LOD
- 局所衝突判定
- 親子階層変換

## 2.3 推奨する論文上の表現

> 本研究は、点列・線分・Sleepingなどの個別要素を新規と主張するものではない。共有点を持つSegment Groupを保存・更新・安全判定・復旧・GPU実行の共通単位とし、状態依存データ、共有点所有権、Static Certificate、Predictive Wake、速度復元、Repair、GPU Compactionを一体化したランタイムデータ表現を提案する。

## 2.4 避ける表現

- 世界初の髪データ型
- 既存研究に同様の技術は存在しない
- 必ず60 FPSを達成する
- どのPCでも高速である
- 品質劣化がない
- 衝突を完全に防げる

---

# 3. 適用範囲

## 3.1 対象

- リアルタイム髪シミュレーション
- Guide/Follower型表示
- 2,000～5,000 Guide
- 100,000表示ストランド
- 1 Guideあたり16～24点
- 3～8 Solver反復
- 剛体、風、局所編集、接触を含むシーン

## 3.2 将来適用候補

同じ表現は、次の線状構造へ拡張可能性がある。

- Rope
- Cable
- Grass blade
- Fur guide
- Streamer
- Ribbon particle
- Fiber bundle
- Tentacle

ただし、本論文の実証対象は髪に限定する。

## 3.3 対象外

- 100,000本すべてを独立物理ストランドとして完全計算する構成
- 流体と髪の完全連成
- 高精度な全ストランド自己衝突
- 破断・接着・結び目の完全処理
- オフライン映画品質の保証

---

# 4. 用語

| 用語 | 定義 |
|---|---|
| Point | ストランド上の位置、速度、質量等を持つ節点 |
| Segment | 隣接Point間の線分 |
| Segment Group | `Point_i - Segment_i - Point_i+1`を基本とする局所単位 |
| Shared Point | 隣接Group間で共有される点 |
| Owner Group | Shared Pointの最終位置を書き込む唯一のGroup |
| Responsibility | 共有点周辺の変形・衝突要求を処理する担当領域 |
| Static Certificate | Groupを低コスト状態に置いてよいことを示す安全判定 |
| Predictive Wake | 将来の接触・親運動・隣接伝播を予測した事前復帰 |
| Repair | 誤静止、貫通、境界不一致等を再構築する状態 |
| External State Baseline | 同一判定を外付け配列で管理する比較方式 |

---

# 5. コアデータモデル

## 5.1 ストランド

ストランドを点列として表す。

```text
S = {P0, P1, ..., Pn}
```

各Segmentは、

```text
Ei = (Pi, Pi+1)
```

とする。

## 5.2 Segment Group

基本Groupは、

```text
Gi = (Pi, Ei, Pi+1)
```

である。

隣接Groupは点を共有する。

```text
Gi.endPoint == Gi+1.startPoint
```

共有点の重複は、独立コピーではなく同一トポロジー参照として扱う。

## 5.3 Group長

同一ストランド内では基本Group長を統一する。ストランド間では異なる基本長を許容する。

最後のGroupで残余長が発生する場合は、

```text
validLengthRatio = actualLength / baseGroupLength
```

を保持する。

## 5.4 データ型の構成

```text
SegmentGroupRuntimeRecord
├─ Topology
│  ├─ startPointIndex
│  ├─ endPointIndex
│  ├─ previousGroupIndex
│  ├─ nextGroupIndex
│  ├─ strandIndex
│  └─ validLengthRatio
├─ LocalShape
│  ├─ restLength
│  ├─ localDirection
│  ├─ localRotation
│  ├─ bendReference
│  └─ cachedLocalShape
├─ RuntimeState
│  ├─ state
│  ├─ stateAge
│  ├─ lastDynamicFrame
│  ├─ lastValidFrame
│  └─ hysteresisCounter
├─ Safety
│  ├─ certificateScore
│  ├─ collisionRisk
│  ├─ boundaryRisk
│  ├─ uncertaintyRisk
│  ├─ falseStaticFlag
│  └─ versionStamps
├─ Recovery
│  ├─ residualVelocity
│  ├─ parentLinearVelocity
│  ├─ parentAngularVelocity
│  ├─ repairReason
│  └─ repairUrgency
└─ Dispatch
   ├─ compactedIndex
   ├─ dispatchClass
   └─ flags
```

## 5.5 必須不変条件

1. 隣接Groupの共有点参照が一致する
2. Shared Pointの最終書き込み者は1つ
3. Group範囲がストランド範囲を超えない
4. Rest Lengthは正かつ有限
5. Static状態ではキャッシュが有効
6. NaN、深い貫通、境界破綻GroupはStaticになれない
7. Repair要求は予算不足のみを理由に消去されない
8. 状態遷移結果がGPU実行順序へ依存しない
9. 同一Seed・同一入力では決定的結果を得る
10. トポロジー変更時は関連キャッシュを無効化する

---

# 6. 共有点所有権

## 6.1 最終位置の所有

`Gi`と`Gi+1`が共有する点について、最終位置を書き込むOwnerは前側Groupとする。

```text
owner(Pi+1) = Gi
```

後側Groupは、共有点を固定された始点として読み取る。

## 6.2 目的

- 二重書き込み防止
- GPUスレッド競合防止
- 非決定性防止
- Static/Dynamic混在時の境界明確化

## 6.3 境界修復

Owner側の最終位置と、後側Groupの局所形状から導かれる期待位置との差を測定する。

```text
boundaryError = ||Powner - Pexpected||
```

閾値を超えた場合、次の順に処理する。

1. 軽微：局所補正
2. 中程度：隣接GroupをDynamicへ昇格
3. 大きい：両GroupをRepairへ昇格
4. 非有限：安全Fallback

---

# 7. 方向責任分割

## 7.1 位置所有権との違い

方向責任分割は、共有点の位置を書き込む権利ではない。共有点周辺で発生した衝突・変形要求を、どちらの隣接Groupが主に処理するかを決める規則である。

## 7.2 局所座標

共有点周辺に、

- Tangent
- Normal
- Binormal

からなる局所フレームを構築する。

衝突法線または変形要求方向を局所フレームへ射影し、前側・後側の担当を決める。

## 7.3 境界条件

方向が境界面に近い場合は、単独決定を避ける。

```text
if abs(dot(requestDirection, splitNormal)) < epsilon:
    mark both groups as candidates
```

この場合、最終位置のOwner規則は維持しつつ、両Groupが候補計算を行い、Owner側が決定結果を採用する。

---

# 8. Runtime State

## 8.1 状態一覧

| 状態 | 役割 |
|---|---|
| StaticCached | 最後の有効局所形状を再利用 |
| StaticTransformed | 局所形状を保ち、親・根元変換のみ適用 |
| Dynamic | 物理積分、拘束、衝突、速度更新を実行 |
| Deferred | 低リスク処理を予算内で一時延期 |
| Repair | 誤静止、貫通、境界不一致、キャッシュ破損等を復旧 |

## 8.2 StaticCached

実行しない処理：

- 物理積分
- 長さ拘束反復
- 曲げ拘束反復
- 詳細衝突解決

継続する処理：

- 最小安全監視
- バージョン確認
- 親・隣接運動リスク確認
- 予測接触確認
- Watchdog

## 8.3 StaticTransformed

局所形状は固定し、親・根元の階層変換を適用する。

位置の更新にXYZの単純加算を用いない。

```text
Pworld = Tparent * Rparent * Plocal
```

回転はQuaternionまたはMatrixを使用する。

## 8.4 Dynamic

以下を実行する。

- 外力適用
- 速度積分
- 位置予測
- 長さ拘束
- 曲げ拘束
- 衝突解決
- 共有点整合
- 速度更新
- Static Certificate更新

## 8.5 Deferred

Deferredは、Repairや安全上必須のWakeを延期するために使わない。

対象は、

- 可視性が低い
- 誤差が小さい
- 接触予測がない
- 隣接伝播リスクが低い

Groupに限定する。

最大Deferred時間を固定し、超過時はDynamicまたはRepairへ昇格する。

## 8.6 Repair

Repair理由を列挙型で保存する。

```text
FalseStatic
Penetration
BoundaryMismatch
CacheInvalidation
WakeVelocityFailure
TopologyChanged
NonFiniteState
BudgetStarvation
```

---

# 9. Static Certificate

## 9.1 評価項目

- 線形速度
- 角速度
- 加速度
- 長さ拘束残差
- 曲げ拘束残差
- 親変換変化
- 隣接Group運動
- 衝突距離
- 外力予測
- 編集バージョン
- Colliderバージョン
- Topologyバージョン
- 不確実性

## 9.2 スコア

```text
R = wvRv + waRa + wsRs + wbRb + wpRp + wnRn + wcRc + weRe + wuRu
```

`R`が低く、必須条件をすべて満たした場合のみStatic候補とする。

## 9.3 必須条件

- 位置・速度が有限
- 重大な貫通がない
- Version mismatchがない
- 共有点誤差が閾値以下
- 予測接触がない
- Repair保留がない
- 最低Dynamic保持期間を満たす

## 9.4 ヒステリシス

Static移行閾値とDynamic復帰閾値を分離する。

```text
R_static < R_enter
R_dynamic > R_exit
R_exit > R_enter
```

これにより状態振動を抑える。

---

# 10. Predictive Wake

## 10.1 Wake要因

- Collider接近
- 頭部・根元の急変
- 隣接Dynamic Groupからの伝播
- 外力・風の増加
- 局所編集
- Certificate失効
- Watchdog検出

## 10.2 予測接触

現在位置だけでなく、Swept Volumeまたは予測位置を使用する。

```text
Ppred = P + vΔt + 0.5aΔt²
```

Collider側の運動も予測へ含める。

## 10.3 伝播

Wakeは無制限に拡散させない。

- 1フレーム当たり最大伝播距離
- 最大Group数
- リスク減衰
- 共有点境界での優先度

を設定する。

## 10.4 評価

- Precision
- Recall
- False Negative
- False Positive
- Wake遅延フレーム数

をFull Dynamic参照と比較する。

---

# 11. Wake速度復元

## 11.1 基本式

```text
vi = vparent + ωparent × ri + vresidual
```

- `vparent`：親の線形速度
- `ωparent`：親の角速度
- `ri`：親原点からPointまでの相対位置
- `vresidual`：最後のDynamic状態から保持した局所残差

## 11.2 制限

- 最大速度Clamp
- エネルギー増加Clamp
- 非有限値検出
- 位置補正量が大きい場合はRepairへ昇格

## 11.3 評価

- Wake時位置誤差
- Wake時速度誤差
- Wake後10フレームのエネルギー差
- Pop検出

---

# 12. Repair

## 12.1 Repair手順

1. 失敗理由を分類
2. 近傍の安全なAnchorを特定
3. 親変換と局所キャッシュから初期形状を再構築
4. 長さ拘束を優先して修復
5. 衝突を解消
6. 共有点境界を修復
7. 速度を再構築
8. Hold Window中に再検証
9. 成功ならDynamicへ移行
10. 失敗ならFull Dynamic Fallback

## 12.2 成功条件

Repair完了直後だけでなく、一定フレーム後まで、

- 貫通が閾値以下
- 共有点誤差が閾値以下
- NaNなし
- 長さ誤差が閾値以下
- 再Repair要求なし

であることを確認する。

---

# 13. GPU実行パイプライン

```text
1. Upload/Update version and external-motion data
2. Block-level coarse classification
3. Group-level certificate and state classification
4. Predictive Wake propagation
5. Build state flags
6. Prefix Sum / Scan
7. Compact Dynamic groups
8. Compact Repair groups
9. Compact StaticTransformed groups
10. Build indirect dispatch arguments
11. Execute StaticTransformed pass
12. Execute Dynamic solver pass
13. Execute Repair solver pass
14. Execute boundary repair
15. Update cache and certificates
16. Reconstruct followers
17. Prepare render data
18. Draw hair
19. Read delayed timestamp telemetry
```

同一フレームのGPU Readbackは禁止する。Timestamp QueryはRing Bufferで遅延回収する。

---

# 14. 計算量

全Dynamic方式を、

```text
O(NI + Kcollision)
```

とする。

提案方式は、

```text
O(N) + O(ND I) + O(NR IR) + O(Kactive)
```

である。

- `N`：全Group数
- `ND`：Dynamic Group数
- `NR`：Repair Group数
- `I`：Dynamic反復数
- `IR`：Repair反復数

全Group分類の`O(N)`は残る。したがって、Dynamic率が高いシーンでは効果が小さくなる。

---

# 15. 比較方式

| ID | 方式 | 目的 |
|---|---|---|
| B0 | Full Dynamic | 品質参照・上限コスト |
| B1 | Strand Sleep | 粗い単位のSleep |
| B2 | Fixed Block Sleep | 固定8Group Block |
| B3 | Segment Threshold Sleep | 単純閾値Sleep |
| B4 | Segment Sleep + Hysteresis | 状態振動対策 |
| B5 | Segment Sleep + Cache | キャッシュ効果 |
| B6 | Segment Sleep + GPU Compaction | 強い性能Baseline |
| B7 | Active-Set Only | 予算選択のみ |
| B8 | External State Array | 同一判定を別データ表現で実装 |
| P | Proposed Integrated Segment Group | 提案方式 |

B8を主要新規性Baselineとする。

PとB8で、Solver、判定式、閾値、Asset、入力、Seed、描画設定を同一にする。

---

# 16. アブレーション

| ID | 無効化する要素 | 検証目的 |
|---|---|---|
| A0 | 全安全機構 | 単純Sleepとの差 |
| A1 | Static Certificate | False Staticへの寄与 |
| A2 | Predictive Wake | 接触遅延への寄与 |
| A3 | 速度復元 | Wake Popへの寄与 |
| A4 | Repair | 貫通・破綻回復への寄与 |
| A5 | 共有点所有権 | 競合・非決定性への寄与 |
| A6 | 方向責任分割 | 境界自然性への寄与 |
| A7 | GPU Compaction | 実GPU時間削減への寄与 |
| A8 | Active-Set | 高負荷予算制御への寄与 |
| Full | 全要素 | 最終方式 |

---

# 17. 評価シナリオ

| ID | シナリオ | 主な検証 |
|---|---|---|
| S01 | Idle | 最大Static率、分類固定費 |
| S02 | Breathing | 微小運動 |
| S03 | Walk | 通常運動 |
| S04 | Sudden Stop | Wakeと速度復元 |
| S05 | Head Rotation | Quaternion階層変換 |
| S06 | Teleport | Repair、Fallback |
| S07 | Collider Approach | Predictive Wake |
| S08 | Static/Dynamic Boundary Contact | 共有点境界 |
| S09 | Strong Wind | 高Dynamic率 |
| S10 | Wind Stop | DynamicからStaticへの収束 |
| S11 | Local Edit | Version invalidation |
| S12 | False Static Trap | Certificate安全性 |
| S13 | Repair Storm | Repair予算と成功率 |
| S14 | All Dynamic | 最悪性能 |
| S15 | All Static | 理論上限削減 |
| S16 | State Thrashing | ヒステリシス |
| S17 | Close Camera 100k | 描画負荷大 |
| S18 | Far Camera 100k | 距離・LOD条件 |

各シナリオを10方式、5 Seedで実行し、合計900ケースを基本とする。

---

# 18. テスト環境

## 18.1 必須条件

- Release x64
- 1920×1080
- VSync OFF
- FPS上限OFF
- Fixed timestep 1/60秒
- Warm-up 600フレーム以上
- 測定6,000フレーム以上
- 5 Seed以上
- Debug Draw OFF
- 同一フレームReadbackなし
- 同一Asset・入力・カメラ・Solver設定

## 18.2 実測判定

以下は性能結果として受理しない。

- Build-only
- Pending
- Estimated
- Synthetic
- Placeholder
- Debugビルド
- 1行のみのCSV
- EXE未実行
- ハードウェア情報欠落
- Hash欠落

## 18.3 保存する再現情報

- Commit SHA
- Executable SHA-256
- Shader Bundle SHA-256
- Asset SHA-256
- Settings SHA-256
- CPU名
- GPU名
- GPU Driver
- OS
- Compiler
- Shader Compiler
- 解像度
- VSync
- Fixed timestep
- Seed
- Scenario Timeline SHA-256

---

# 19. 品質参照

## 19.1 Full Dynamic GPU参照

全GroupをDynamicとして、提案方式と同一Solver・同一反復数で実行する。

## 19.2 CPU参照

小規模Assetでは、CPU Full Dynamic参照を用意する。

目的は、

- トポロジー
- 長さ
- 共有点
- Wake速度
- Repair
- 決定性

の正解確認である。

100,000本の全フレームCPU参照を必須とはしない。

## 19.3 Ground Truth

False Staticは、同一入力のFull Dynamic結果で、

- 当該Groupに閾値以上の変位が生じる
- 衝突が発生する
- 拘束残差が閾値を超える
- Repairが必要になる

場合に誤静止と判定する。

---

# 20. 指標

## 20.1 性能

- Average frame ms
- p50
- p95
- p99
- p99.9
- Maximum
- 16.67ms超過率
- Hair GPU total ms
- 各GPU Pass時間
- Peak VRAM
- CPU/GPU同期回数

## 20.2 状態

- StaticCached率
- StaticTransformed率
- Dynamic率
- Deferred率
- Repair率
- 平均Static継続時間
- 状態遷移数
- 状態振動率

## 20.3 安全性

- False Static率
- Predictive Wake Precision
- Predictive Wake Recall
- Wake遅延
- Wake位置誤差
- Wake速度誤差
- Repair成功率
- Repair再発率
- 衝突見逃し率
- 最大貫通量
- 平均貫通量
- 共有点最大誤差
- 境界接線誤差
- NaN数
- Fallback数

## 20.4 表現効果

PとB8の差として、

- 1 Group当たりByte数
- 読み書きByte数
- GPU Cache Miss
- Compaction入力・出力数
- 状態不整合数
- 境界Repair数
- 決定性Mismatch
- p95・p99差

を評価する。

---

# 21. 成功条件

## 21.1 新規性安全Gate

以下をすべて満たした場合に、新規性評価を「安全」とする。

1. B8 External State Arrayとの比較がある
2. B6 GPU Compaction付きSegment Sleepとの比較がある
3. PがB8に対して、少なくとも表現統合由来の差を1つ以上示す
4. アブレーションで各統合要素の必要性を示す
5. 個別既存技術を新規と主張しない
6. Full Dynamic参照による品質評価がある
7. 再現可能な生データを保存する

## 21.2 品質Gate

| 指標 | 目標 |
|---|---:|
| False Static率 | 通常2%未満、上限5% |
| Repair成功率 | 95%以上 |
| Repair再発率 | 5%未満 |
| Wake遅延 | 通常0～1フレーム |
| Predictive Wake Recall | 95%以上 |
| 衝突見逃し率 | 1%未満 |
| Static/Dynamic境界最大誤差 | 1mm以下を第一目標 |
| 状態振動率 | 2%未満 |
| Determinism mismatch | 0 |
| NaN | 0 |

## 21.3 性能Gate

- Pの平均Hair GPU時間が強いBaselineより20%以上改善するシーンがある
- Pのp99がB8より悪化しない
- 通常シーンの16.67ms超過率が5%以下
- 強成功条件では超過率1%以下
- All Dynamicシーンで安全Fallbackが成立する

---

# 22. 60 FPS評価方針

60 FPSについては、次の3段階で報告する。

## 22.1 達成

指定PC、指定Asset、指定シナリオで、

- 60 FPS比率95%以上
- p95 16.67ms以下
- 品質Gate通過

の場合。

## 22.2 条件付き達成

通常シーンでは達成するが、強風、Repair Storm、All Dynamicで超過する場合。

## 22.3 未達

通常シーンでもp95が16.67msを超える場合。

「平均FPSが60を超えた」だけでは安定60 FPSと表現しない。

---

# 23. ハードウェア区分

| 区分 | 例 | 用途 |
|---|---|---|
| Low | GTX 1660 6GB、旧世代6コアCPU | 主張対象 |
| Mid | RTX 2060、RTX 3060 | 一般環境確認 |
| Reference | より高性能なGPU | 品質参照・ボトルネック分離 |

GPU名だけでなく、Driver、温度、クロック、電源モードを記録する。

---

# 24. 実装計画

## Phase 0：ビルド阻害の解消

- `ActiveSetBudget`重複フィールド削除
- Release x64ビルド確認
- 警告を記録

## Phase 1：測定契約

- Build Configuration記録
- Real Measurementフラグ
- Hash記録
- Hardware記録
- Fixed timestep記録
- VSync記録

## Phase 2：決定的シナリオ

- 18 Timeline読込
- Seed固定
- 自動開始・終了
- Warm-up除外

## Phase 3：Baseline

- B0～B8実装
- 同一設定切替
- 同一バイナリで比較

## Phase 4：提案方式

- Group状態統合
- 共有点所有権
- Certificate
- Predictive Wake
- 速度復元
- Repair
- GPU Compaction
- Active-Set接続

## Phase 5：テレメトリ

- Wake
- Repair
- 境界
- 衝突見逃し
- 状態振動
- GPU Pass時間

## Phase 6：アブレーション

A0～A8をFeature Flagで実行する。

## Phase 7：最終評価

- 900ケース
- 3 PC区分
- 長時間試験
- 生データ保存
- 統計処理

---

# 25. Feature Flag

```text
enableSegmentGroupIntegratedState
enableSharedPointOwnership
enableDirectionalResponsibility
enableStaticCertificate
enablePredictiveWake
enableVelocityRecovery
enableRepair
enableGpuCompaction
enableActiveSet
enableExternalStateArray
enableStrandSleep
enableBlockSleep
enableSleepHysteresis
enableStaticCache
```

同一バイナリ内で切り替え、ビルド条件差を最小化する。

---

# 26. リスクと対策

## 26.1 全Group分類が重い

対策：

- Block coarse classification
- SoA配置
- Version stamp
- Early out
- GPU resident state

## 26.2 Dynamic率が高い

対策：

- Full Dynamicに近い経路へ安全Fallback
- 追加判定が損になる場合は一時的に統合Dispatch

## 26.3 Wake Storm

対策：

- Mandatory Wakeを優先
- RepairとDynamicの別予算
- 伝播上限
- 重大衝突は予算外優先

## 26.4 状態振動

対策：

- ヒステリシス
- Minimum residence frames
- 再発Groupの閾値引上げ

## 26.5 境界破綻

対策：

- Owner規則
- Boundary error監視
- 双方向候補
- Repair昇格

## 26.6 テレメトリが性能を悪化させる

対策：

- Delayed timestamp readback
- 集約Buffer
- 毎フレーム全Point Readback禁止
- 計測有無の差も記録

## 26.7 新規性が統合に見えない

対策：

- B8比較
- データ型の不変条件提示
- 外付け配列との差を定量化
- 表現レベルのメモリアクセス・不整合・決定性を測定

---

# 27. 論文構成案

1. はじめに
2. 関連研究
3. 問題設定
4. Segment Group状態適応型データ表現
5. Static CertificateとPredictive Wake
6. Repairと共有点安全性
7. GPU実行系
8. 実験環境
9. 比較結果
10. アブレーション
11. 制約と考察
12. 結論

---

# 28. 最終的な研究貢献

本研究の貢献は、次のように限定して記述する。

1. 共有点を持つSegment Groupを、局所線状構造のランタイム意思決定単位として形式化したこと
2. 状態依存保存、共有点所有権、安全証明、Wake、速度復元、Repairを同一表現へ統合したこと
3. 状態別GPU CompactionとActive-Set予算制御へ接続したこと
4. 外付け状態配列方式を含む強いBaselineに対し、表現統合の効果を分離評価する実験系を設計したこと
5. 平均速度だけでなく、False Static、Wake、Repair、境界、衝突見逃し、p99を含む安全評価系を提示したこと

---

# 29. 判定

本設計のコア自体は変更しない。

安全性は、主張を統合表現へ限定し、B8比較、アブレーション、Full Dynamic参照、再現可能な実測を追加することで確保する。

実装だけで自動的に「安全」になるわけではない。上記Gateを実測結果が満たした時点で、最終評価を「安全」とする。