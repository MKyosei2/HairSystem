# Novelty01 Main統合・完了チェックリスト

## 実装対象

| 項目 | 実装 |
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
| Guide Dirty Range | 完了 |
| Follower Dirty Range | 完了 |
| Render Dirty Range生成 | 完了 |
| Persistent Render Cache Consumer | 完了 |
| Ground Truth Full Dynamic | 完了 |
| Formal Asset 4環境変数 | 完了 |
| SG-APT Formal Telemetry | 完了 |
| 最新Main Runtime Foundation保持 | 完了 |
| overlay/replacement非依存 | 完了 |
| 自己完結ZIP | 完了 |

## Readiness Smoke必須条件

- `main`
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
- Optional Payloadが実際にActiveまたはAllocated
- RenderDirtyRangeが生成
- Render Cache ConsumerがVertex更新
- Ground Truth Sampleが存在
- `fullCpuGroupUploadCount == 0`
- `sameFrameReadbackCount == 0`
- Owner mismatch == 0
- Version mismatch == 0
- Duplicate owner == 0
- Invalid payload index == 0
- Ground-truth violation == 0
- Dispatch mismatch == 0
- Shared point unresolved == 0
- Memory leak suspect == 0
- NaN == 0
- Inf == 0
- Descriptor violation == 0
- Safe stop == 0

Payload Overflowは発生自体を隠さず記録する。発生時はBaseline Fallbackへ移行し、Invalid Index、Duplicate Owner、Safe Stopを0に保つ。

## 正式実験

Readiness PASS後:

```text
B0 / B6 / B8 / P
S01 / S03 / S09 / S17
Seed 1103
Warm-up 600
Measurement 6000
3 repetitions
```

その後:

- A1～A15
- S01～S18
- 30分Soak
- Determinism
- GTX 1660級実機
- CSV/JSONL完全一致
- Formal Hardware / Visual / Availability / Statistical Gate
