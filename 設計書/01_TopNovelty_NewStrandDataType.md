# 01 安全版企画書兼設計書 完全版

## Segment Group Certified Strand Runtime Data Representation

---

## 0. 文書情報

| 項目 | 内容 |
|---|---|
| 対象リポジトリ | `MKyosei2/HairSystem` |
| 対象ブランチ | `Novelty` |
| 文書バージョン | `01-Safe v3.1 Existing-Source-Only` |
| 作成基準日 | 2026年7月16日 |
| 対象表示本数 | 100,000本 |
| 対象Guide数 | 2,000～5,000本 |
| 1 Guide当たりの点数 | 16～24点 |
| Solver反復数 | 3～8回 |
| 主評価環境 | GTX 1660 6GB級GPU、旧世代6コアCPU、RAM 32GB以下 |
| 研究対象 | 線状構造をSegment Group単位で状態適応的に保存・更新・復旧・GPU実行するランタイムデータ表現 |
| 新規性評価方針 | 個別技術ではなく、表現・所有権・安全判定・復旧・GPU実行の統合効果を評価する |
| 60 FPSの扱い | 保証値ではなく、指定Asset・指定PC・指定シナリオで検証する研究目標 |

本書は、既存の01コアを維持しながら、形式仕様、実装契約、比較実験、Ground Truth、再現可能性、統計処理、失敗時の安全策、成果物完全性を統合した完全版設計書である。実装方針として、新規の`.h`、`.cpp`、`.inl`、`.py`を追加せず、Noveltyブランチに存在するソースファイルへ機能を統合する。

---

# 1. 企画概要

## 1.1 背景

リアルタイム髪シミュレーションでは、表示される全ストランドを毎フレーム完全計算すると、物理積分、拘束解法、衝突判定、Follower再構成、描画準備、透明描画の総コストが大きくなる。

一方、髪全体が常に同じ強度で動くわけではない。静止に近い領域、親変換だけで追従できる領域、局所的な物理計算が必要な領域、誤静止や貫通から復旧すべき領域が同時に存在する。

Sleeping、LOD、Guide/Follower、Dirty Flag、Cache Reuse、Active-Set、GPU Compaction、Indirect Dispatchは、この問題を部分的に扱える。しかし、それらを髪の局所線状構造に対して、保存単位、更新単位、安全判定単位、復旧単位、GPU実行単位として一貫して接続する必要がある。

## 1.2 目的

髪ストランドを共有点を持つSegment Groupへ分割し、各Groupが次の情報を持つ状態適応型ランタイムデータ表現を設計・実装・評価する。

- 局所トポロジー
- 局所形状
- Runtime State
- Shared Pointの所有権
- 方向責任領域
- Static Certificate
- Predictive Wake
- Wake時速度復元
- Repair情報
- GPU Dispatch情報
- Active-Set連携情報

## 1.3 研究仮説

### H1：表現統合

同一の状態判定を外付け配列で管理する方式より、Segment GroupのRuntime Recordへ状態と安全情報を統合する方式の方が、状態不整合、境界修復数、GPUメモリアクセス量、p95・p99フレーム時間を改善する。

### H2：安全性

Static Certificate、Predictive Wake、速度復元、Repair、Shared Point Ownershipを統合することで、単純なSegment SleepよりFalse Static、Wake遅延、境界誤差、衝突見逃しを抑制する。

### H3：実時間性

DynamicおよびRepair対象をGPU上で圧縮し、Indirect Dispatchへ接続することで、静止Groupの削減を実際のGPU処理時間削減へ反映する。

### H4：再現可能性

同一バイナリ、同一Asset、同一入力、同一Seed、同一シナリオTimeline、同一Solver設定で比較し、Hash付き生データを保存することで、第三者が結果を追跡可能にする。

---

# 2. 安全な新規性主張

## 2.1 主張する内容

本研究が提案するのは、次の要素を一体化した状態適応型ランタイムデータ表現である。

1. 共有点を持つ重複Segment Group
2. Shared Pointの一意な最終書き込み所有権
3. 局所方向に基づく隣接Group間の責任分割
4. Group単位のRuntime Stateと状態依存保存形式
5. Static Certificateによる低コスト状態の安全判定
6. Predictive Wakeによる事前復帰
7. 親並進・親角速度を考慮したWake速度復元
8. False Static、貫通、境界不一致、Cache破損を扱うRepair
9. 状態別GPU CompactionおよびIndirect Dispatch
10. Active-Set予算制御との接続
11. External State Arrayとの同条件比較による表現統合効果の分離評価

## 2.2 単独では主張しない内容

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
- GPU timestamp query

## 2.3 推奨する論文表現

> 本研究は、点列・線分・Sleeping等の個別要素を新規と主張するものではない。共有点を持つSegment Groupを保存・更新・安全判定・復旧・GPU実行の共通単位として形式化し、状態依存データ、共有点所有権、Static Certificate、Predictive Wake、速度復元、Repair、GPU Compaction、Active-Set連携を一体化したランタイムデータ表現を提案する。

## 2.4 避ける表現

- 世界初の髪データ型
- 既存研究に同様の技術は存在しない
- 必ず60 FPSを達成する
- どのPCでも高速である
- 品質劣化がない
- 衝突を完全に防止できる
- 100,000本すべてを独立物理計算できる

---

# 3. 適用範囲

## 3.1 対象

- リアルタイム髪シミュレーション
- Guide/Follower型表示
- 2,000～5,000 Guide
- 100,000表示ストランド
- 1 Guide当たり16～24点
- 3～8 Solver反復
- 剛体、風、局所編集、接触を含むシーン
- Direct3D 11 Compute Shader環境

## 3.2 将来適用候補

Rope、Cable、Grass blade、Fur guide、Streamer、Ribbon particle、Fiber bundle、Tentacle。

本論文の実証対象は髪に限定し、他領域への適用可能性は考察として扱う。

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
| Shared Point | 隣接Group間で共有されるPoint |
| Owner Group | Shared Pointの最終位置を書き込む唯一のGroup |
| Directional Responsibility | 共有点周辺の変形・衝突要求を担当する局所方向領域 |
| Static Certificate | Groupを低コスト状態に置いてよいことを示す安全判定 |
| Predictive Wake | 将来の接触、親運動、隣接伝播を予測した事前復帰 |
| Repair | 誤静止、貫通、境界不一致、Cache破損を復旧する状態 |
| External State Baseline | 同一判定を外付け配列で管理する比較方式 |
| Ground Truth | Full Dynamicまたは小規模CPU参照から得る比較基準 |
| Formal Run | 全測定契約を満たし、論文結果として受理可能な実行 |

---

# 5. コアデータモデル

## 5.1 ストランドとSegment Group

```text
S = {P0, P1, ..., Pn}
Ei = (Pi, Pi+1)
Gi = (Pi, Ei, Pi+1)
Gi.endPoint == Gi+1.startPoint
```

共有点は独立コピーではなく、同一トポロジー参照として扱う。

## 5.2 Group長

同一ストランド内では基本Group長を統一する。ストランド間では異なる基本長を許容する。

```text
validLengthRatio = actualLength / baseGroupLength
```

## 5.3 Runtime Record

```text
SegmentGroupRuntimeRecord
├─ Topology
│  ├─ startPointIndex / endPointIndex
│  ├─ previousGroupIndex / nextGroupIndex
│  ├─ strandIndex / blockIndex
│  └─ validLengthRatio
├─ Ownership
│  ├─ ownerGroupIndex
│  ├─ responsibilityAxis
│  └─ responsibilityMask
├─ LocalShape
│  ├─ restLength / localDirection / localRotation
│  ├─ bendReference
│  └─ cachedLocalShape
├─ RuntimeState
│  ├─ state / previousState / stateAge
│  ├─ lastDynamicFrame / lastValidFrame
│  └─ hysteresisCounter
├─ Safety
│  ├─ certificateScore
│  ├─ collisionRisk / boundaryRisk / uncertaintyRisk
│  ├─ falseStaticFlag
│  └─ parent/collider/edit/topology version
├─ Recovery
│  ├─ residualVelocity
│  ├─ parentLinearVelocity / parentAngularVelocity
│  ├─ repairReason / repairUrgency
│  └─ recurrenceCount
└─ Dispatch
   ├─ compactedIndex
   ├─ dispatchClass
   └─ flags
```

## 5.4 必須不変条件

1. 隣接Groupの共有点参照が一致する
2. Shared Pointの最終書き込み者は1つ
3. Group範囲がストランド範囲を超えない
4. Rest Lengthは正かつ有限
5. `0 < validLengthRatio <= 1`
6. Static状態ではCacheが有効
7. Repair状態ではRepair理由が設定済み
8. Dynamic、Static、RepairのCompaction結果に重複がない
9. Mandatory Groupが予算不足で破棄されない
10. NaN、Infinityを検出したGroupは即時Fallback
11. CPU側構造体とHLSL側構造体のSize、Alignment、Field Orderが一致
12. PとB8で判定式、閾値、Solver、入力が一致

---

# 6. Runtime State

| 状態 | 意味 | 主処理 |
|---|---|---|
| StaticCached | 局所形状を再利用可能 | Cache再利用、安全監視 |
| StaticTransformed | 相対形状固定で親変換のみ必要 | Quaternion/Matrix変換 |
| Dynamic | 完全な局所物理計算が必要 | 積分、拘束、衝突 |
| Deferred | 低危険度処理を一時延期 | 最小安全監視 |
| Repair | 誤静止・貫通・境界破綻から復旧 | 再構築、投影、再検証 |

## 6.1 主な遷移

```text
Dynamic → StaticCached / StaticTransformed / Deferred / Repair
StaticCached → StaticTransformed / Dynamic / Repair
StaticTransformed → StaticCached / Dynamic / Repair
Deferred → Dynamic / Repair / prior required state
Repair → Dynamic / Full Dynamic Fallback / Safe Reset
```

## 6.2 ヒステリシス

- Static昇格とDynamic復帰で異なる閾値を使用
- Minimum residence framesを設定
- 再発Groupは閾値を厳格化
- 連続振動Groupは一定時間Dynamic固定

---

# 7. Shared Point Ownershipと方向責任

共有点`Pi+1`の最終位置は前側Group`Gi`が書き込む。次Group`Gi+1`は共有点を固定された開始点として読む。

Ownershipは位置の最終書き込み権限、Directional Responsibilityは共有点周辺の変形・衝突要求の担当を表す。局所接線`T`、法線`N`、従法線`B`による局所フレームへ衝突法線や変位方向を射影し、担当Groupを決定する。

Repairで双方向候補を生成しても、最終CommitはOwner Groupだけが行う。

評価項目：Shared Point Conflict Count、最大共有点誤差、境界接線誤差、Determinism mismatch。

---

# 8. Static Certificate

## 8.1 入力

速度、加速度、Stretch residual、Bend residual、親並進、親角速度、隣接Group運動、衝突距離、編集Version、Collider Version、Topology Version、不確実性、Cache age、Watchdog結果。

## 8.2 判定

```text
R = wvRv + waRa + wsRs + wbRb + wpRp + wnRn + wcRc + weRe + wuRu
```

```text
R < staticThreshold
AND version stamps valid
AND cache valid
AND minimum residence satisfied
AND no mandatory wake
AND no repair reason
```

Version更新、Watchdog失敗、Boundary Error超過、Ground Truthとの差分超過でCertificateを失効する。

---

# 9. Static状態の処理

## 9.1 StaticCached

物理積分、拘束反復、詳細衝突解法を実行しない。Version、Coarse collision、Neighbor wake、Watchdog、Cache有効性のみ監視する。

## 9.2 StaticTransformed

```text
worldPosition = parentTranslation + parentRotation * localPosition
```

親回転はXYZ加減算ではなくQuaternionまたはMatrixを使用する。大回転ではSwept範囲で衝突候補を確認する。

---

# 10. Predictive Wakeと速度復元

## 10.1 Wake要因

Collider接近、親並進・親角速度の急増、隣接Wake、外力予測、局所編集、Cache age超過。

```text
predictedDistance = currentDistance - relativeNormalVelocity * predictionHorizon
```

Mandatory Wakeは予算外優先とし、Wake Storm時は安全なFull Dynamic寄りFallbackを選択する。

## 10.2 Wake速度復元

```text
v_i = v_parent + omega_parent × r_i + v_residual
```

復元値はClampし、異常値はDynamic初期化またはRepairへ送る。

評価項目：Precision、Recall、False Positive、False Negative、Wake Delay、位置誤差、速度誤差。

---

# 11. Repair

## 11.1 Repair理由

False Static、Collider貫通、Shared Point不一致、Boundary Error超過、Cache破損、Version不一致、Wake速度異常、NaN、Infinity、Topology変更、Repeated state thrashing。

## 11.2 処理

1. 理由と緊急度を記録
2. 親・隣接Groupから初期候補を再構築
3. Collider外へ投影
4. 長さ拘束を再適用
5. 境界整合
6. 速度再構築
7. 検証保持期間を開始
8. 成功後Dynamicへ戻す

## 11.3 成功定義

NaN/Infinityなし、最大貫通量・共有点誤差・長さ残差が閾値以下、保持期間中に同じ原因が再発しない場合をVerified Successとする。

失敗回数上限超過時はFull Dynamic FallbackまたはSafe Resetへ移行する。

---

# 12. GPU実行パイプライン

```text
1. Block Coarse Classification
2. Group Classification
3. Predictive Wake
4. Wake Propagation
5. State Flag生成
6. Prefix Sum / Stream Compaction
7. Indirect Args生成
8. Static Transform
9. Dynamic Solve
10. Velocity Recovery
11. Repair Solve
12. Boundary Repair
13. Cache Update
14. Watchdog
15. Follower Reconstruction
16. Render Preparation
17. Hair Draw
18. Telemetry Reduction
```

必須要件：GPU resident state、同一フレーム全Group Readback禁止、timestamp query遅延取得、Compaction重複なし、CountとIndirect Args一致、Overflow検出、0件Dispatch安全処理、CPU/HLSL ABI検証。

---

# 13. Active-Set接続

入力：Segment Group Risk、Repair Urgency、False Static Risk、Certificate Risk、Dynamic/Repair Count、Visibility、Contact、Edit、Boundary、Temporal Risk。

優先順位：Mandatory Repair、Mandatory Wake、貫通危険、編集対象、境界リスク、不確実性、可視重要領域、低リスク領域。

Mandatory項目は予算外優先、RepairとDynamicの予算を分離、Deferredは低危険度だけに限定する。高Dynamic率ではFull Dynamic寄り経路へ切り替え可能にする。

---

# 14. 計算量

```text
Full Dynamic: O(NI + Kcollision)
Proposed: O(N) + O(ND I) + O(NR IR) + O(Kactive)
```

全Group分類の`O(N)`は残るため、Dynamic率が高いシーンでは効果が小さい。分類とCompactionの固定費を分離計測する。

---

# 15. 比較方式

| ID | 方式 | 目的 |
|---|---|---|
| B0 | Full Dynamic | 品質参照・上限コスト |
| B1 | Strand Sleep | 粗い単位のSleep |
| B2 | Fixed Block Sleep | 固定8 Group Block |
| B3 | Segment Threshold Sleep | 単純閾値Sleep |
| B4 | Segment Sleep + Hysteresis | 状態振動対策 |
| B5 | Segment Sleep + Cache | Cache効果 |
| B6 | Segment Sleep + GPU Compaction | 強い性能Baseline |
| B7 | Active-Set Only | 予算選択のみ |
| B8 | External State Array | 同一判定を外付け配列で管理 |
| P | Proposed Integrated Segment Group | 提案方式 |

B8を主要表現Baselineとする。PとB8ではSolver、判定式、閾値、Asset、Timeline、Seed、Shader、描画設定、Build、GPUを同一にする。

---

# 16. アブレーション

| ID | 無効化要素 | 検証目的 |
|---|---|---|
| A0 | 全安全機構 | 単純Sleepとの差 |
| A1 | Static Certificate | False Staticへの寄与 |
| A2 | Predictive Wake | 接触遅延への寄与 |
| A3 | 速度復元 | Wake Popへの寄与 |
| A4 | Repair | 貫通・破綻回復への寄与 |
| A5 | Shared Point Ownership | 競合・非決定性への寄与 |
| A6 | Directional Responsibility | 境界自然性への寄与 |
| A7 | GPU Compaction | 実GPU時間削減への寄与 |
| A8 | Active-Set | 高負荷予算制御への寄与 |
| Full | 全要素 | 最終方式 |

同一バイナリ内のFeature Flagで切り替える。

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
| S08 | Static/Dynamic Boundary Contact | Shared Point境界 |
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

基本行列は10方式×18シナリオ×5 Seed＝900ケース。アブレーションは別行列とする。

---

# 18. テストアーキテクチャ

## 18.1 既存ソース限定方針

新規のソースファイルは作成しない。既存の責務を次のように再利用する。

```text
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer_parts/*.inl
include/hairgpu/HairNoveltyTelemetry.h
src/hairgpu_core/HairNoveltyTelemetry.cpp
tools/final_stage/hairgpu_final_run.py
tools/final_stage/hairgpu_runtime_suite.py
```

- Viewer：D3D11実行、Scenario適用、GPU Query、Telemetry出力
- `HairNoveltyTelemetry.h`：Availabilityと指標定義
- `hairgpu_final_run.py`：Formal Case検証、Viewer起動、成果物検証、Hash、終了コード
- `hairgpu_runtime_suite.py`：10方式×18シナリオ×5 Seedの展開、順序ランダム化、Resume、統計、集計

## 18.2 テスト方式

独立した新規テスト実行ファイルは作らず、既存のFinal Stageランナーをテストハーネスとして使用する。

1. `hairgpu_final_run.py`のPreflightで不正設定を実行前に拒否する
2. ViewerのScenario Assertionでイベント発火を確認する
3. JSONLとCSVの行数、列数、Frame ID、Availability、NaN/Infinityを実行後に検証する
4. B0～B8、Pを同じViewer BinaryからEnvironment Variableで切り替える
5. `hairgpu_runtime_suite.py --dry-run`で900ケースの完全性を確認する
6. 同一Seedの対応あり比較とBootstrap信頼区間を既存集計処理で算出する

## 18.3 小規模参照テスト

既存ViewerのAsset読込・設定機構を使い、32 Guide×16点および128 Guide×24点の小規模AssetをFormal Caseとして実行する。CPU参照またはFull Dynamic GPU参照との位置、速度、長さ残差、共有点、Wake、Repair差をTelemetryとして出力する。

## 18.4 決定性・長時間試験

- 同一Caseを複数回実行し、State列、Compaction数、Dispatch数、最終位置Hash、Repair結果を比較する
- 30分、2時間、8時間の長時間Caseを既存Runtime Suiteへ登録する
- VRAM、未解決Query、Overflow、NaN/Infinity、Repair再発、温度、クロックを既存Hardware Samplerで記録する

---

# 19. Formal Run測定契約

## 19.1 必須条件

- Release x64
- Hardware D3D11 Adapter
- 1920×1080
- VSync OFF、Present Sync Interval 0、FPS上限OFF
- Fixed timestep 1/60秒
- Warm-up 600フレーム以上
- 測定6,000フレーム以上
- 5 Seed以上
- Debug Draw OFF
- 同一フレーム全Point Readbackなし
- 同一Asset、入力、カメラ、Solver設定
- Guide 2,000～5,000
- 表示ストランド100,000
- 1 Guide 16～24点
- Solver 3～8反復
- Asset Frozen、Timeline Loaded、Method Flags Acknowledged

## 19.2 拒否条件

Debug、WARP、Build-only、Pending、Estimated、Synthetic、Placeholder、EXE未実行、1行CSV、Hash欠落、Hardware情報欠落、GPU timing coverage 99%未満、未解決Query、同一フレームReadback、NaN/Infinity、Scenario Assertion失敗、Method Flag不一致、Asset数不一致、Fixed timestep不一致、途中終了、成果物欠損、Canonical Hash不一致。

## 19.3 終了コード

| Code | 意味 |
|---:|---|
| 0 | Complete Valid |
| 1 | Quality Gate Failed |
| 2 | Formal Contract Invalid |
| 3 | Runtime Failure |
| 4 | Output Integrity Failure |
| 5 | Configuration Parse Failure |

---

# 20. 再現情報

Repository、Branch、Commit SHA、Dirty Working Tree、Build、Compiler、Windows SDK、Shader Compiler、Executable/Shader/Asset/Manifest/Settings/Method/Timeline/ResultのSHA-256、CPU、GPU、Vendor/Device ID、VRAM、Driver、OS Build、RAM、解像度、VSync、Fixed timestep、Seed、Repetition、電源プラン、HAGS、Game Mode、GPU温度・クロック、CPUクロックを保存する。

---

# 21. Ground Truth

Full Dynamic GPUを主要品質参照とし、小規模AssetではCPU Full Dynamic参照を用意する。

False Staticは、同一入力のFull Dynamic結果に対して、Static Groupの位置差・速度差・拘束残差が閾値を超える、衝突する、またはRepairが必要になる場合とする。False Static専用Availability Bitを持つ。

Collision Missは、参照で衝突候補が存在し、提案方式が候補生成または解決を行わなかった場合とする。

Repairは次を分離する。

```text
Repair execution success rate = repairSuccessCount / repairAttemptCount
Repair verified success rate = repairVerifiedCount / repairAttemptCount
```

---

# 22. 指標

性能：Average、p50、p95、p99、p99.9、Maximum、16.67ms超過率、Hair GPU total、各Pass、CPU frame work、Peak VRAM、同期・Readback回数、Timing Coverage、Compaction比率、Bandwidth。

状態：各State率、平均Static継続時間、遷移数、振動率、Wake Storm規模。

安全性：False Static、Wake Precision/Recall/Delay/誤差、Repair execution/verified success、再発率、Collision Miss、貫通量、共有点誤差、境界接線誤差、NaN、Infinity、Fallback。

表現統合効果：PとB8の1 Group当たりByte数、読み書きByte数、Cache Miss、Compaction数、状態不整合、Boundary Repair、Determinism、p95/p99、CPU upload量、Resource Binding数。

---

# 23. 統計計画

方式の固定順実行を禁止し、同一Seed内で方式順をランダム化して記録する。PとBaselineは対応あり比較とし、平均差、中央値差、95%信頼区間、効果量、p95/p99差、Gate通過率を報告する。

外れ値除外は事前定義規則だけを使用し、除外理由と除外前後の結果を保存する。Thermal Throttling、OS Update、Driver Reset、別プロセス負荷は測定失敗として再実行する。

---

# 24. ハードウェア環境

| 区分 | 例 | 用途 |
|---|---|---|
| Low | GTX 1660 6GB、旧世代6コアCPU | 主張対象 |
| Mid | RTX 2060、RTX 3060 | 一般環境確認 |
| Reference | より高性能なGPU | 品質参照・ボトルネック分離 |

600フレームWarm-upはShader/Cache用とし、Formal Run前に3～5分のPreconditioningを行う。Windows Update、ウイルススキャン、録画、ブラウザ等を確認し、電源プランとGPU電源モードを固定する。

---

# 25. 出力成果物と完全性

既存の`tools/final_stage/hairgpu_final_run.py`が1 Runにつき次を生成する。

```text
<telemetry_dir>/
├─ <runName>_case.json
├─ <runName>_preflight.json
├─ <runName>_frames.csv
├─ <runName>_frames.jsonl
├─ <runName>_settings.json
├─ <runName>_summary.json
├─ <runName>_environment.json
├─ <runName>_hardware_timeseries.csv
├─ <runName>_validation.json
├─ <runName>_manifest.json
└─ <runName>_error.json          # 失敗時のみ
```

## 25.1 完全性手順

1. JSON等は一時ファイルへ書き込む
2. Flushと`fsync`を行う
3. `os.replace`でAtomic置換する
4. CSVとJSONLの測定行数を厳密に照合する
5. 全成果物のSHA-256をManifestへ保存する
6. Summary、Settings、Frame、EnvironmentからCanonical Result SHA-256を生成する
7. Hash欠落、成果物欠損、列不一致、非有限値をFormal Resultとして拒否する

## 25.2 Validation Reasons

`validation.json`と`summary.json`に、次の形式で失敗理由を保存する。

```json
{
  "severity": "error",
  "code": "E_TIMING_COVERAGE",
  "message": "GPU timing coverage is below threshold.",
  "expected": 0.99,
  "observed": 0.974
}
```

## 25.3 終了コード

既存のFinal Runスクリプトが0～5の終了コードを返し、Runtime Suiteは`complete_valid`かつHash一致のCaseだけをDBへ取り込む。

---

# 26. CIと専用実機Runner

新規Workflowや新規テストソースを本変更では追加しない。既存のVisual Studio SolutionとFinal Stageスクリプトを使用する。

## 26.1 ローカル・既存CIで確認する内容

- Release x64 Build
- Debug x64 Build
- HLSL compile
- Python syntax check
- `hairgpu_runtime_suite.py --dry-run`
- 不正CaseがPreflightで拒否されること
- 出力改ざん時にManifest検証が失敗すること

## 26.2 Formal GPU測定

共有CI GPUの性能値は論文根拠にしない。Low、Mid、Referenceの専用実機で、同一Commit、同一Executable Hash、同一Shader Hashを使用する。実行前後および1秒間隔のGPU温度・クロック・消費電力を既存`hairgpu_final_run.py`で保存する。

---

# 27. 成功条件

## 27.1 新規性安全Gate

1. B8比較
2. B6比較
3. PがB8に対して表現統合由来の差を示す
4. アブレーションで各要素の必要性を示す
5. 個別既存技術を新規と主張しない
6. Full Dynamic品質参照
7. 再現可能な生データ
8. Result HashとValidation Reasons
9. PとB8の設定同一性自動検証

## 27.2 品質Gate

| 指標 | 目標 |
|---|---:|
| False Static率 | 通常2%未満、上限5% |
| Repair verified success | 95%以上 |
| Repair再発率 | 5%未満 |
| Wake遅延 | 通常0～1フレーム |
| Predictive Wake Recall | 95%以上 |
| Collision Miss率 | 1%未満 |
| Static/Dynamic境界最大誤差 | 1mm以下を第一目標 |
| 状態振動率 | 2%未満 |
| Determinism mismatch | 0 |
| NaN / Infinity | 0 |

## 27.3 性能Gate

- Pの平均Hair GPU時間が強いBaselineより20%以上改善するシーンがある
- Pのp99がB8より悪化しない
- 通常シーンの16.67ms超過率5%以下
- 強成功条件では1%以下
- All Dynamicで安全Fallback成立
- GPU timing coverage 99%以上
- Formal Run Valid

---

# 28. 60 FPS評価

達成：60 FPS比率95%以上、p95 16.67ms以下、品質Gate通過、Formal Run Valid。

条件付き達成：通常シーンでは達成するが、Strong Wind、Repair Storm、All Dynamicで超過。

未達：通常シーンでもp95が16.67msを超える。平均FPSが60を超えただけでは安定60 FPSと表現しない。

---

# 29. Feature Flags

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

Requested、Applied、Observedの3段階を保存し、完全一致しないRunは無効とする。

---

# 30. 実装計画

## Phase 0：既存構造の確認

- 新規ソースファイルを追加しない
- Viewer、Telemetry、Final Run、Runtime Suiteの既存責務を維持する
- Release x64とHLSLのBuild阻害を先に解消する

## Phase 1：Telemetry定義

`include/hairgpu/HairNoveltyTelemetry.h`へ次を統合する。

- False Static専用Availability
- Performance Environment Availability
- Result Integrity Availability
- Repair execution / verified successの分離
- Wake Precision / Recall
- State Oscillation Rate
- 分母0またはGround TruthなしをNaNとして扱う規則

## Phase 2：Formal Run

既存`tools/final_stage/hairgpu_final_run.py`へ次を統合する。

- B0～B8、PのMethod Preset
- PとB8の共通Flag一致検証
- Release x64、1920×1080、1/60秒、600/6000フレームのPreflight
- 2,000～5,000 Guide、100,000表示、16～24点、3～8反復の検証
- Frozen Asset、Timeline、Executable、Shader、SettingsのHash
- Requested / Applied / Observed照合
- CSV / JSONL / Summary / Environment検証
- Quality Gateと終了コード
- Atomic Write、Canonical Hash、Artifact Manifest

## Phase 3：Runtime Suite

既存`tools/final_stage/hairgpu_runtime_suite.py`をそのまま利用し、設定JSONから次を行う。

- 10方式×18シナリオ×5 Seed＝900ケース展開
- 実行順ランダム化
- Resumeと失敗Case再実行
- 対応ありBootstrap 95%信頼区間
- DB、Excel、CSV、Markdown、グラフ、ZIP生成

## Phase 4：段階的測定

1. 1 CaseでPreflightと成果物検証
2. 1 Scenario×B0/B6/B8/P×1 Seed
3. 18 Scenario×4方式×1 Seed
4. 10方式×18 Scenario×1 Seed
5. 基本900ケース
6. アブレーション行列
7. 30分、2時間、8時間の長時間試験

本測定は、1 Seedの完全行列がすべて`complete_valid`になるまで開始しない。

---

# 31. リスクと対策

- 全Group分類：Block coarse、SoA、Version、Early out、GPU resident
- 高Dynamic率：Full Dynamic寄りFallback、分類/Compaction停止、統合Dispatch
- Wake Storm：Mandatory優先、予算分離、伝播上限
- 状態振動：Hysteresis、Minimum residence、Dynamic固定期間
- 境界破綻：Owner規則、Boundary監視、双方向候補、OwnerのみCommit、Repair
- Telemetry負荷：Delayed readback、集約Buffer、全Point Readback禁止
- 結果欠損：Atomic Write、SHA-256 Manifest、Canonical Hash、Validation Reasons
- 新規性不明瞭：B8、形式的不変条件、Memory Access・不整合・決定性の定量化

---

# 32. 制約

DX11 Query粒度、GPU浮動小数点差、全ストランド自己衝突対象外、表示本数と物理Guide数の区別、低PC Hardware Profile固定、全シナリオ60 FPS非保証、他構造への一般化には追加実証が必要。

---

# 33. 最終的な研究貢献

1. 共有点を持つSegment GroupをRuntime意思決定単位として形式化
2. 状態依存保存、Ownership、安全証明、Wake、速度復元、Repairを同一表現へ統合
3. 状態別GPU CompactionとActive-Set予算制御へ接続
4. External State Arrayを含む強いBaselineで表現統合効果を分離評価
5. False Static、Wake、Repair、境界、Collision Miss、p99、Result Integrityを含む安全評価系
6. Hash付き生データ、Formal Validator、CI、専用実機Runnerによる再現手順

---

# 34. 最終判定規則

設計と実装だけで新規性評価を自動的に「安全」としない。

新規性安全Gate、品質Gate、Formal Run、B8/B6比較、アブレーション、生データとHash、統計的に説明可能な差を満たした時点で最終評価を「安全」とする。

実測結果が不足する段階では「安全寄りの危ない」と記述し、断定を避ける。
