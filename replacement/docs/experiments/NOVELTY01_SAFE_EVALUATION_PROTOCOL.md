# Novelty 01 安全評価プロトコル

## 1. 対象

- Repository: `MKyosei2/HairSystem`
- Branch source: `Novelty01`
- Verified source commit: `a0521e258acdbd9d71262172d6d2ba208c338eeb`
- Integration revision: `69.3-package`
- Proposal: Segment-Group Adaptive Payload Type（SG-APT）

本プロトコルは、ソースが存在することと、実機で合格したことを区別する。`Source Present`または`Connected`を、`Build Passed`、`Formal Passed`、`Hardware Passed`として扱ってはならない。

## 2. Source of Truth

実行対象は次のルートだけとする。

```text
apps/dx11_viewer/src
apps/dx11_viewer/shaders
include/hairgpu
tools/final_stage
benchmarks/novelty01/sgapt_scenarios
docs/sample_assets/novelty01_frozen_parameters.json
```

`overlay/`はリポジトリ内に置かない。配布用適用スクリプトも実行リポジトリへコピーしない。同名ファイルを二系統で管理しない。

## 3. 実装状態

状態は以下のいずれかだけを使用する。

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

現在のソース状態は`docs/experiments/NOVELTY01_IMPLEMENTATION_TRACEABILITY.json`を正とする。Windows実機ログがない項目は、コードが接続されていても`Connected`より上へ進めない。

## 4. Frozen Parameter

正式値は`docs/sample_assets/novelty01_frozen_parameters.json`に固定する。Matrix Runnerは同JSONから`HAIRGPU_N01_*`環境変数を生成する。Runtimeは環境変数がない場合も同じ既定値を使用する。

以下を満たさないCaseは起動時にFatal Failureとする。

```text
dynamicPositionThresholdMm > staticPositionThresholdMm
dynamicVelocityThresholdMm > staticVelocityThresholdMm
fullDynamicEnterRatio > fullDynamicExitRatio
payloadRetireDelayFrames >= payloadGenerationCount
```

正式評価結果を確認した後に閾値を変更しない。変更時はFreeze Revision、Commit、Calibration Datasetを更新し、過去の正式結果と混在させない。

## 5. Baseline実行契約

`docs/experiments/NOVELTY01_FORMAL_CONTRACT.json`を正とする。

- B0は既存Legacy Full Dynamicをそのまま実行する。
- B1～B8、P、A1～A15は同じControlled Dispatcherを使う。
- Controlled Dispatcherを共有する目的は、Shader Load、GPU Resident Buffer、計測経路などの共通オーバーヘッドを揃えることにある。
- 各BaselineはFeature Maskだけでなく、Payload Capacity、Migration Buffer、Compaction方式、Follower／Render更新方式を変える。
- B8はOptional Payload、Fixed Group Index、Payload Owner、Delayed Reuse、Transition Budget、Hard／Soft Descriptorを使用しない。
- B8とPで共通の一般的最適化を、SG-APT固有効果として報告しない。

## 6. Canonical Scenario

正式シナリオは`benchmarks/novelty01/sgapt_scenarios/S01.json`～`S18.json`だけを使用する。

JSONの`scenario`は既存Viewer Parserへ渡す互換名であり、研究上の意味は`novelty01DesignScenario`と`description`が正である。Runtimeでは`HAIRGPU_N01_DESIGN_SCENARIO`により専用注入を行う。

既存`benchmarks/novelty01/scenarios`をNovelty 01正式評価へ使用しない。

## 7. Ground Truth、Shadow、Performanceの分離

### Quality Run

- Ground Truth有効
- 同一Frame Input、同一dt、同一6反復、同一Collision、同一Root条件
- Ground Truth用Position／Lambda BufferをProposalと共有しない
- Ground Truth処理時間をProposal性能値へ含めない
- False Static、False Dynamic、RMSE、Maximum Error、Collision、Boundaryを評価

### Performance Run

- Ground Truth無効
- ProposalまたはBaseline単体のEnd-to-End Frame Timeを測定
- VSync無効
- CPU Readback原則無効
- GPU Timer未解決Frameは0msとして扱わず、Unavailableとして失敗判定

### Shadow Dynamic

- 表示へ使用しない
- Static Groupのサンプルだけを動的に解く
- 閾値付近、長時間Static、Collision Margin小、過去False Staticを優先

### Determinism Run

- 同一Asset、Camera、Seed、Scenario、Frozen Parameter
- 3反復以上
- State Sequence Hash、Compaction Sequence Hash、Final Position Hashを比較

## 8. Payload Lifetime

Payload Ownerの成立条件は次のとおりとする。

```text
payloadOwner[p].groupIndex == g
core[g].payloadIndex == p
core[g].payloadVersion == payloadOwner[p].payloadVersion
payloadOwner[p].generation == currentPayloadGeneration
```

Lifecycleは以下とする。

```text
Free -> Active -> Retired -> SafeToReuse -> Free
```

- 4世代を使用する。
- Retireした世代を同一Frameで再利用しない。
- `retireEpoch + retireDelay <= currentEpoch`を満たすまで再利用しない。
- Ground TruthとProposalはPayload Poolを共有しない。
- Duplicate Owner、Invalid Index、Version Mismatchが一件でも発生したFrameはStatic再利用しない。
- Overflow時は不正Indexを使わず、Frame Full DynamicへFallbackする。

## 9. GPU Resource Binding

`docs/experiments/NOVELTY01_RESOURCE_BINDING_CONTRACT.md`を正とする。

- D3D11のUAV上限8を超えない。
- 同一ResourceをSRVとUAVへ同時Bindしない。
- 各Pass後にCS、SRV、UAV、CBをUnbindする。
- C++／HLSL Constant Buffer順序不一致はPreflightで失敗する。
- Entry PointとBinding Mode不一致はPreflightで失敗する。

## 10. Telemetry Traceability

主要RQと証拠は次のとおり。

| RQ | 指標 | Runtime Counter／Source | 出力 | 合格判定 |
|---|---|---|---|---|
| RQ1 | Payload使用量 | PayloadActive／Allocated／Retired／Reused | Novelty01 CSV/JSONL | B2未満 |
| RQ2 | Group/Payload Byte | GroupReadBytes等 | Novelty01 CSV/JSONL | B3またはB8より削減 |
| RQ3 | Transition Copy | TransitionBytes／StateCopyBytes | Novelty01 CSV/JSONL | B4未満 |
| RQ4 | False Static | GroundTruthFalseStatic | Quality Run | 0.1%以下目標 |
| RQ5 | End-to-End | Frame、Simulation、Follower、Render、Draw GPU時間 | Final Runner | B8より改善 |
| RQ6 | 負の高速化 | Full Dynamic Entry／Exit、P95履歴 | Novelty01 CSV/JSONL | B1比悪化5%以内目標 |

`0`とUnavailableを区別する。Availability BitまたはSample Countが0の場合、値0を合格証拠として扱わない。

## 11. 統計計画

- Formal／Performanceは各Case 3反復以上。
- Warm-up Frameを集計から除外する。
- 失敗Runを除外せず、失敗率として報告する。
- PとB8はScenario／RepetitionでPairingする。
- Primary Metricはp95 Frame Time。
- 95% Bootstrap Confidence Intervalを10,000 resamplesで算出する。
- Average、Median、p99、Maximum、50ms以上、100ms以上も併記する。
- 品質値はMedian、p95、Maximumを併記する。
- 効果量は相対差と絶対差を併記する。

集計は`tools/final_stage/hairgpu_novelty01_report.py`で行う。

## 12. VRAM

`tools/final_stage/hairgpu_novelty01_resource_audit.py`で以下を分離して算出する。

- Proposal Steady-State
- Proposal Peak
- Ground Truth追加分
- Shadow追加分
- Render Cache
- Payload 4世代
- List／Prefix／Owner／Counter／Readback

CoreとPayloadだけの式をEnd-to-End VRAMとして報告しない。

## 13. 実行順序

```text
1. Clean checkout / Release x64
2. Source Preflight
3. FXC 53 Entry Point compile
4. MSBuild Release x64
5. Native Self-Test
6. B0 Smoke
7. P S01/S06/S11/S17/S18 Smoke
8. B1-B8/P Formal Quality
9. B1-B8/P Performance
10. A1-A15
11. Determinism
12. 30-minute Soak
13. GTX1660 Hardware Run
14. Formal Report
```

## 14. Merge Gate

次を満たすまでMainへMergeしない。

- `#define private`なし
- `overlay/`なし
- Release x64 Build Passed
- FXC Entry Point全件Passed
- B0 Regressionなし
- P Smoke Passed
- Invalid Payload Index 0
- Duplicate Payload Owner 0
- NaN／Inf 0
- S17 Fallback成功
- S18異常注入から復帰
- 30分Memory継続増加なし
- Determinism結果保存

GTX1660未実施の場合、60FPS達成を結果として書かない。
