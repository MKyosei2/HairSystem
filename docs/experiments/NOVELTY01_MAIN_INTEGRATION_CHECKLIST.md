# Novelty01 69.8補完・Main統合チェックリスト

## 69.8で修正した実行阻害要因

| 項目 | 69.8の問題 | 修正後 |
|---|---|---|
| Follower Render Cache Dispatch | 96,000 Followersを`Dispatch(96000,1,1)`し、D3D11の1軸65,535 Group上限を超過 | Follower×Pointを1次元化し、`12,288,000 / 256 = 48,000 Group`で実行 |
| Dispatch上限のFail-Closed | 上限確認なし | `D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION`で検査し、超過時はFatal Failure |
| Formal Asset Binding | テスト開始時に追跡中Runnerを書き換えるPatcher方式 | `hairgpu_final_run.py`本体へ4環境変数を直接実装 |
| 実験Commitの再現性 | Patcher実行後にWorktreeがDirty | Runtime Source Patchを廃止し、Commit内容と実行内容を一致 |
| C++ Header Preflight | 通常PowerShellで`cl.exe`がPATHにないと停止 | `vswhere.exe`から`VsDevCmd.bat`を解決してCompile |
| Render Cache Shader検査 | Source Marker検査のみ | 2 Entry PointをFXC／Shader Model 5.0で実Compile |
| 旧Patcher | `.py`と不完全な`.patch`が残存 | 両方を削除し、Preflightで再混入を拒否 |
| Regression Test | 96,000 Group経路を検出できない | 48,000 Group計算、Direct Binding、旧Patcher不在を検証 |

## 実装済みの新規性本体

| 項目 | 状態 |
|---|---|
| Fixed Index SegmentGroupCore | 完了 |
| Optional DynamicPayload | 完了 |
| PayloadOwner / Version / Generation | 完了 |
| Retire Epoch / Safe Reuse Epoch | 完了 |
| Static時Payload無効化 | 完了 |
| Promote / Demote | 完了 |
| Pool不足時Baseline Fallback | 完了 |
| Hysteresis / Minimum Residence | 完了 |
| Incremental Block Invalidation | 完了 |
| Stable Compaction | 完了 |
| Shared Point決定的解決 | 完了 |
| Guide / Follower / Render Dirty Range | 完了 |
| Persistent GPU Render Cache Consumer | 完了 |
| Ground Truth Full Dynamic | 完了 |
| SG-APT Formal Telemetry | 完了 |
| Main Runtime Foundation保持 | 完了 |
| P限定SG-APTルーティング | 完了 |
| B0～B8のMain経路保持 | 完了 |

## Source／Build Gate

次がすべてPASSするまでMainへMergeしない。

1. Python Syntax
2. `hairgpu_final_run.py --self-test`
3. Novelty01 Unit／Contract Tests
4. C++ Header Compile
5. SG-APT HLSL 50 Entry Points FXC Compile
6. Render Cache HLSL 2 Entry Points FXC Compile
7. Release x64 Formal Rebuild
8. Native Self-Test
9. Baseline Shader Self-Test
10. P／S01 100k Readiness Smoke

## Readiness Smoke必須条件

- Branch: `Novelty01`またはMerge後の`main`
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

## 判定の区別

- Source／Unit／Build PASS: 実装がコンパイル・契約上成立
- Readiness PASS: P/S01の実ランタイムとデータ保存経路が成立
- Formal Matrix PASS: 比較実験のデータが成立
- Statistical Gate PASS: 論文の性能・品質主張に使用可能

Source検査だけをFormal Experiment成功とは扱わない。
