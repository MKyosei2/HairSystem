# Novelty01 Main統合・完了チェックリスト

## 1. 前回不足していた項目と対応

| 項目 | 対応 | 実装場所 |
|---|---|---|
| 最新Mainとの統合 | `02a_required_runtime_foundation.inl`を維持 | `GpuHairViewer.cpp` |
| Proposal限定ルーティング | PだけSG-APT、B0～B8はMain Legacy経路 | `GpuHairViewer.cpp` |
| Formal Asset Binding | 4必須環境変数を検証済みManifestから設定 | `hairgpu_novelty01_matrix.py` |
| Formal Telemetry出力先未設定 | DirectoryとRun NameをShutdown前に設定 | `18_novelty01_render_dirty_range_consumer.inl` |
| Render Dirty RangeがTelemetryのみ | Persistent GPU render-position cacheへ接続 | `18_*.inl`, `HairNovelty01RenderDirtyCache.hlsl` |
| Static follower render再生成 | Dirty Point Rangeだけ更新 | `N01CopyFollowerRenderCacheCS` |
| Guide更新 | Guide点は毎Proposal frame更新 | `N01CopyGuideRenderCacheCS` |
| Full Dynamic安全経路 | Feature無効／Fallback時は全量CopyResource | `UpdateNovelty01RenderCache` |
| 実行時Owner検証 | Counter 35/36/37/38をFail-Closed | Readiness Smoke |
| Dispatch/Shared Point検証 | Counter 48/49をFail-Closed | Readiness Smoke |
| NaN/Inf/Safe Stop | Counter 59/60/63をFail-Closed | Readiness Smoke |
| Ground Truth未証明 | Readinessで実サンプル数を必須化 | Readiness Smoke |
| Optional Payload未証明 | Active/Allocated Counterを必須化 | Readiness Smoke |
| 重複Tree | `overlay/`、`replacement/`を適用対象外 | Apply Script |
| 文字列検査だけのテスト | Asset Env、Render Consumer、GPU resident条件を追加 | Runtime Contract Tests |

## 2. Readiness PASS条件

- Release x64 build: PASS
- Native Self-Test: PASS
- Baseline Shader Self-Test: PASS
- P / S01_Idle: 完走
- 100,000 strands
- 4,000 guides
- 96,000 followers
- 128 points
- 6 solver iterations
- Warm-up 120
- Measurement 600
- CSV 600 rows
- JSONL 600 rows
- SG-APT telemetry: 600 rows以上
- Payload active/allocated: 1以上
- Render Dirty Range: 1以上
- Render regenerated vertices: 1以上
- Ground Truth samples: 1以上
- full CPU group upload: 0
- same-frame readback: 0
- payload overflow: 記録する（Safe fallbackが働き、Invalid Index / Safe Stopが0であること）
- owner mismatch: 0
- version mismatch: 0
- duplicate owner: 0
- invalid payload index: 0
- dispatch mismatch: 0
- unresolved shared point: 0
- memory leak suspect: 0
- NaN: 0
- Inf: 0
- descriptor violation: 0
- safe stop: 0

## 3. Readiness後の正式実験

Readiness PASSはMainへ取り込める最低条件であり、研究結果ではない。

正式評価:

```text
Variants : B0, B6, B8, P
Scenarios: S01, S03, S09, S17
Seed     : 1103
Warm-up  : 600
Measure  : 6000
Repeat   : 3
```

その後、A1～A15、S01～S18、30分Soak、Determinism、GTX 1660級実機評価へ拡張する。
