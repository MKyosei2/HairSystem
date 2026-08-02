# Novelty01 69.9 修復後・Main統合前チェックリスト

## この段階の判定

このチェックリストはMain統合そのものを実施しない。Novelty01 69.9の実装・検証基盤を、Main取り込み前に再現可能な状態へ修復する。

- Source／Unit／Build PASS: 実装がコンパイル・契約上成立
- Readiness PASS: P/S01の実ランタイムと保存経路が成立
- Formal Matrix PASS: 比較実験データが成立
- Statistical Gate PASS: 論文の性能・品質主張に利用可能

Source検査だけをFormal Experiment成功とは扱わない。

## 69.9までに修正済みの実行阻害要因

| 項目 | 旧問題 | 修正後 |
|---|---|---|
| Follower Render Cache Dispatch | 96,000 Followersを`Dispatch(96000,1,1)`し、D3D11の1軸65,535 Group上限を超過 | Follower×Pointを一次元化し、`12,288,000 / 256 = 48,000 Group`で実行 |
| Dispatch上限のFail-Closed | 上限確認なし | `D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION`で検査し、超過時Fatal Failure |
| Formal Asset Binding | テスト開始時に追跡中Runnerを書き換える方式 | `hairgpu_final_run.py`本体が4環境変数を直接設定 |
| C++ Header Preflight | 通常PowerShellで`cl.exe`がPATHにないと停止 | `vswhere.exe`から`VsDevCmd.bat`を解決 |
| Render Cache Shader検査 | Source Marker検査のみ | 2 Entry PointをFXC／Shader Model 5.0で実Compile |

## 今回のMain統合前修復

| 項目 | 修復内容 |
|---|---|
| Viewer Header | `#define private`によるAccess Control書換えを廃止し、Novelty01宣言を`GpuHairViewer.h`へ正式追加 |
| Viewer Aggregator | 通常のHeader includeを使用し、Legacy名へのMacro Routeだけを保持 |
| Windows CI | 壊れたYAMLと外部ブランチからの実行時Patcher依存を除去 |
| Novelty01 CI | `.github/workflows/novelty01-formal-gate.yml`を正式配置 |
| Git Identity | 固定された69.8表示を廃止し、実HEAD・Merge Base・Dirty状態を記録 |
| Duplicate Trees | `overlay/`・`replacement/`・旧Apply Scriptを削除 |
| Readiness Runner | 69.9を祖先に持つこと、重複Tree不在、想定外変更不在をFail-Closed検査 |

## 実装済みの新規性本体

| 項目 | 状態 |
|---|---|
| Fixed Index SegmentGroupCore | Source実装済み |
| Optional DynamicPayload | Source実装済み |
| PayloadOwner / Version / Generation | Source実装済み |
| Retire Epoch / Safe Reuse Epoch | Source実装済み |
| Static時Payload無効化 | Source実装済み |
| Promote / Demote | Source実装済み |
| Pool不足時Baseline Fallback | Source実装済み |
| Hysteresis / Minimum Residence | Source実装済み |
| Incremental Block Invalidation | Source実装済み |
| Stable Compaction | Source実装済み |
| Shared Point決定的解決 | Source実装済み |
| Guide / Follower / Render Dirty Range | Source実装済み |
| Persistent GPU Render Cache Consumer | Source実装済み |
| Ground Truth Full Dynamic | Source実装済み |
| SG-APT Formal Telemetry | Source実装済み |
| P限定SG-APTルーティング | Source実装済み |
| B0～B8のMain経路保持 | Source実装済み |

## Main統合前Source／Build Gate

次がすべてPASSするまでMainへMergeしない。

1. Python Syntax
2. `hairgpu_final_run.py --self-test`
3. Novelty01 Unit／Contract Tests
4. Repository Identity／Layout Preflight
5. C++ Header Compile
6. SG-APT HLSL 50 Entry Points FXC Compile
7. Render Cache HLSL 2 Entry Points FXC Compile
8. Release x64 Formal Rebuild
9. Native Self-Test
10. Baseline Shader Self-Test
11. P／S01 100k Readiness Smoke

## Readiness Smoke必須条件

- Branch: `Novelty01`またはMerge後の`main`
- Novelty01 69.9 commit `37ff9749fe530e8ab08d849892c4c6326a2578bf`を祖先に持つ
- 100,000 strands
- 4,000 guides
- 96,000 followers
- 128 points
- 6 solver iterations
- P / S01_Idle
- Warm-up 120
- Measurement 600
- CSV 600 rows
- JSONL 600 rows
- SG-APT telemetry 600 rows以上
- Optional PayloadがActiveまたはAllocated
- RenderDirtyRangeが生成
- Persistent Render CacheがVertex更新
- Ground Truth Sampleが存在
- `fullCpuGroupUploadCount == 0`
- `sameFrameReadbackCount == 0`
- Owner mismatch / Version mismatch == 0
- Duplicate owner / Invalid payload index == 0
- Ground-truth violation / Dispatch mismatch == 0
- Shared point unresolved / Memory leak suspect == 0
- NaN / Inf / Descriptor violation / Safe stop == 0

Payload Overflowは隠さず記録する。発生時はBaseline Fallbackへ移行し、Invalid Index、Duplicate Owner、Safe Stopを0に保つ。

## 正式実験

Readiness PASS後に、同一Frozen AssetとSeedで実施する。

```text
Methods    : B0 / B6 / B8 / P
Scenarios  : S01 / S03 / S09 / S17
Seed       : 1103
Warm-up    : 600
Measurement: 6000
Repetition : 3
```

続いてA1～A15、S01～S18、30分Soak、Determinism、GTX 1660級実機、CSV/JSONL完全一致、Formal Hardware／Visual／Availability／Statistical Gateを実行する。

## 69.10完全修復のSource of Truth

- 修復基準Commit: `4f4c6210d7be40a0b5d522789d2684ade86d3de9`
- `files/`、`overlay/`、`replacement/`、旧Apply/Patcher/Verifierは正式ソースから除去する。
- Novelty01のOpaque State型をHeaderへ正式宣言し、操作Methodはprivateに保つ。
- B1/B8は既存B1/B8 Methodを直接使用し、PだけをSG-APT経路へ通す。
- S01～S18、Formal Contract、Frozen Parameters、Gate、Report、Resource Auditを正式パスに固定する。
- Readinessは`Novelty01`と統合後`main`の両方を許可する。
- Asset Freezeは`Novelty01`、manifest v1.6、groupsPerBlock=32を要求する。

この修復はSource/実験経路を完成させるものであり、Release x64、FXC、実GPU Readiness、Formal MatrixのPASS結果そのものではない。
