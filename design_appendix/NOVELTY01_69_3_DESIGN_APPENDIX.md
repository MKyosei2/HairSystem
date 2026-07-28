
---

# 49. Version 69.3 実装統合情報

## 49.1 文書と実装の基準

| 項目 | 内容 |
|---|---|
| 対象ブランチ | `Novelty01` |
| 分岐元Main | `8d19bb546f3505fe25436d6ffa5b390a9b83897a` |
| 確認済みNovelty01 HEAD | `a0521e258acdbd9d71262172d6d2ba208c338eeb` |
| 統合Revision | `69.3-package` |
| 実装状態 | Source統合済み、Windows Release x64／FXC／D3D11実機検証前 |
| Runtime Source of Truth | `apps/`、`include/`、`tools/final_stage/` |
| Formal Contract | `docs/experiments/NOVELTY01_FORMAL_CONTRACT.json` |
| Frozen Parameter | `docs/sample_assets/novelty01_frozen_parameters.json` |

本節以降は、旧「実装完了チェックリスト」より優先する。Sourceの存在を、実機合格と同一視しない。

## 49.2 実装状態の語彙

```text
Not Implemented
Source Present
Connected
Build Passed
Shader Passed
Smoke Passed
Formal Passed
Hardware Passed
```

状態証拠は`docs/experiments/NOVELTY01_IMPLEMENTATION_TRACEABILITY.json`へ記録する。

---

# 50. 正式統合方式

## 50.1 禁止する暫定方式

以下は配布・試作時の暫定方式であり、正式実装では使用しない。

```text
#define private
関数名のPreprocessor差替え
実行ソースとoverlay/の二重管理
未登録HLSLの実行時依存
同名ファイルの複製
```

## 50.2 正式方式

* `GpuHairViewer.h`へNovelty 01関数とRuntime所有権を正式宣言する
* `std::unique_ptr<Novelty01RuntimeState>`で寿命を管理する
* `GpuHairViewer.cpp`は通常の`.inl`集約だけを行う
* Experiment Tick、Single Step、Shutdown、Renderへ明示的に接続する
* `HairNoveltyTelemetry.h`へ正式指標を追加する
* `HairSystem.vcxproj`へHeaderとHLSLを登録する
* PostBuildでNovelty 01 HLSLを出力先へ複製する
* `overlay/`をリポジトリのSource of Truthにしない

---

# 51. Baseline Execution Contract

Baselineの正式定義は`NOVELTY01_FORMAL_CONTRACT.json`を正とする。

| ID | 実行Route | SG-APT固有表現 | 一般最適化 | 主な差分 |
|---|---|---:|---:|---|
| B0 | Legacy Full Dynamic | なし | 現行Main | 既存結果を変更しない |
| B1 | Controlled Full Dynamic | なし | GPU Resident等 | 全Guide Dynamic |
| B2 | Controlled Sleeping | なし | Solver Skip | 全GroupがPayload保持 |
| B3 | Controlled Active List | なし | Active List | 全GroupがPayload保持 |
| B4 | Controlled Migration | なし | Static/Dynamic分離 | Group本体Copyを発生 |
| B5 | Controlled Simple Payload | 部分 | 最小 | 単純閾値のみ |
| B6 | Controlled SG-APT | あり | 全Group評価 | Block Invalidationなし |
| B7 | Controlled Local Guide | あり | Guide局所 | Follower/Render全更新 |
| B8 | Controlled Strongest Practical | なし | 共通最適化最大 | Fixed Core／Optional Payload／Lifetimeなし |
| P | Controlled SG-APT | あり | 共通最適化最大 | 提案方式全部 |

B1～B8、P、A1～A15はControlled Dispatcherを共有する。これは共通GPUオーバーヘッドを揃えるためであり、同じデータ型を意味しない。B8では少なくとも次を無効化する。

```text
Optional Payload
Fixed Group Index
Payload Owner Check
Delayed Payload Reuse
SG-APT Transition Budget
Hard/Soft Descriptor Split
```

---

# 52. Canonical Scenario Contract

正式Timelineは`benchmarks/novelty01/sgapt_scenarios/S01.json`～`S18.json`とする。

JSONの`scenario`は既存Parser互換名、`novelty01DesignScenario`は研究上の正規IDである。専用入力は`ApplyNovelty01DesignScenarioForTick`と`N01InjectDesignScenarioCS`で発生させる。

| ID | 正規入力 | 主な確認 |
|---|---|---|
| S01 | 完全静止 | Static化、Cache、Payload削減 |
| S02 | Root平行移動 | 剛体移動、全Group Dirty禁止 |
| S03 | Root回転 | Root Local Cache回転 |
| S04 | 弱風 | 混在状態 |
| S05 | 通常頭部運動 | End-to-End標準条件 |
| S06 | 局所衝突 | Swept Spatial Invalidation |
| S07 | 複数Collider | 複数Dirty Source |
| S08 | 一本単位編集 | Guide局所Dirty |
| S09 | 連続Brush | 移動Dirty Range |
| S10 | 強風 | Dynamic Ratio増加 |
| S11 | 全Dynamic | B1とFast Path |
| S12 | 境界Stress | Shared Point／Boundary Repair |
| S13 | Thrashing | Hysteresis／Residence |
| S14 | 複数Guide依存 | Follower CSR伝播 |
| S15 | 30分 | Leak、Memory、安定性 |
| S16 | 決定性 | 3 Hash一致 |
| S17 | Pool Stress | 5%容量、Overflow Fallback |
| S18 | 異常注入 | Version／Owner／Descriptor拒否 |

既存`benchmarks/novelty01/scenarios`を正式Novelty 01の入力として代用しない。

---

# 53. Frozen Parameter Table

正式値は`docs/sample_assets/novelty01_frozen_parameters.json`を唯一の正とする。主要値は以下である。

| Parameter | 値 | 単位 |
|---|---:|---|
| Groups per Block | 32 | groups |
| Payload Capacity | 50 | % |
| Payload Generation | 4 | generations |
| Payload Retire Delay | 4 | frames |
| Descriptor Maximum Age | 64 | frames |
| Minimum Residence | 12 | frames |
| Consecutive Static | 8 | frames |
| Transition Budget | 32,768 | groups/frame |
| Static Position Threshold | 0.10 | mm |
| Dynamic Position Threshold | 0.20 | mm |
| Static Velocity Threshold | 0.10 | mm/frame |
| Dynamic Velocity Threshold | 0.20 | mm/frame |
| Boundary Error Threshold | 2.0 | mm |
| Collision Padding | 2.0 | mm |
| Shadow Error Threshold | 0.50 | mm |
| Full Dynamic Enter | 0.70 | ratio |
| Full Dynamic Exit | 0.55 | ratio |
| Fast Path Decision | 8 | frames |

Runtimeは同JSONからMatrixが設定する`HAIRGPU_N01_*`を読む。Hysteresis不成立時は起動を失敗させる。

---

# 54. GPU Resource／Binding Contract

詳細は`docs/experiments/NOVELTY01_RESOURCE_BINDING_CONTRACT.md`を正とする。

各Passは次を必須とする。

* D3D11 UAV 8本以内
* 同一ResourceのSRV／UAV同時Bind禁止
* Pass後のCS／SRV／UAV／CB Unbind
* C++／HLSL Constant Buffer順序一致
* Entry PointとBinding Mode一致
* Indirect Argument Bufferへ`D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS`

ResourceごとにStride、Count、SRV/UAV、世代、Read Pass、Write Passを契約書へ記録する。

---

# 55. Payload Lifetime厳密仕様

Payloadの成立条件を次とする。

```text
payloadOwner[p].groupIndex == groupId
core[groupId].payloadIndex == p
core[groupId].payloadVersion == payloadOwner[p].payloadVersion
payloadOwner[p].generation == currentGeneration
```

再利用条件を次とする。

```text
retireEpoch + payloadRetireDelayFrames <= currentEpoch
```

* 4世代を使用する
* 同一Frame内でRetireしたSlotを再Allocateしない
* Ground TruthとProposalでPoolを共有しない
* Duplicate Owner／Invalid Index／Version Mismatch時はStatic再利用しない
* Overflow時は不正Indexを使わずFrame Full Dynamicへ移行する
* Reset時は全Owner、Core Payload Index、Generationを初期化する

---

# 56. Telemetry Traceability

| RQ | Formal Evidence | 出力 | Availability条件 |
|---|---|---|---|
| RQ1 | Active／Allocate／Retire／Reuse／Overflow | N01 CSV/JSONL | GPU Counter解決 |
| RQ2 | Group／Payload Read/Write Bytes | N01 CSV/JSONL | Byte Telemetry有効 |
| RQ3 | Transition／Migration Bytes | N01 CSV/JSONL | B4/P実行 |
| RQ4 | False Static／False Dynamic／RMSE | Quality Run | Ground Truth Sample > 0 |
| RQ5 | Frame／Follower／Render／Draw時間 | Final Runner | GPU Query解決 |
| RQ6 | P95履歴／Entry／Exit／Fallback | N01 CSV/JSONL | Full Dynamic比較履歴あり |

0とUnavailableを区別する。Sample CountまたはAvailabilityが0の値を合格根拠にしない。

---

# 57. Ground Truth／Performance／Shadow分離

## Quality

Ground Truthを有効にし、Proposal性能値にはGround Truth処理時間を含めない。誤差、False Static、False Dynamic、Collision、Boundaryを評価する。

## Performance

Ground Truthを無効にし、ProposalまたはBaseline単体のEnd-to-End Frame Timeを評価する。

## Shadow

Static Groupのサンプルだけを非表示で動的に解く。閾値付近、長時間Static、Collision Margin小、過去False Staticを優先する。

## Determinism

同一条件を3回以上実行し、State Sequence、Compaction Sequence、Final PositionのHashを比較する。

---

# 58. 統計評価計画

* 各Formal／Performance Caseは3反復以上
* Warm-upを除外
* 失敗Runを除外せず失敗率へ含める
* PとB8をScenario／RepetitionでPairing
* Primary Metricはp95 Frame Time
* 10,000回Bootstrapによる95%信頼区間
* Relative DifferenceとAbsolute Differenceを併記
* 品質値はMedian／p95／Maximumを併記
* 50ms以上、100ms以上Frame数を報告

集計は`hairgpu_novelty01_report.py`を用いる。

---

# 59. End-to-End VRAM

CoreとPayloadだけでなく、次を含めてSteady／Peakを分ける。

```text
Current/Previous/Rest Position
Static Cache/Core Extension/Descriptor/Block
Payload 4 generations/Owner tables
Prefix/List/Shared Candidate
Follower Dependency/Dirty Range
Persistent Render Cache
Self-Collision Grid
Ground Truth Position/Lambda
Shadow Buffer
Telemetry Readback Ring/GPU Query
OIT Render Targets
```

`hairgpu_novelty01_resource_audit.py`でProposal、Ground Truth追加分、Peakを出力する。

---

# 60. Version 69.3 Merge Gate

以下をすべて満たすまでMainへMergeしない。

```text
[ ] #define privateが存在しない
[ ] overlay/が存在しない
[ ] Release x64 Build Passed
[ ] HLSL Entry Point全件Shader Passed
[ ] B0 Regressionなし
[ ] P Smoke Passed
[ ] Invalid Payload Index 0
[ ] Duplicate Payload Owner 0
[ ] NaN 0
[ ] Inf 0
[ ] S17 Overflow Fallback成功
[ ] S18異常注入から復帰
[ ] Determinism結果保存
[ ] 30分Memory継続増加なし
[ ] GTX1660実機結果保存
```

GTX1660実機結果がない段階では、60FPS達成を結果として記述しない。
