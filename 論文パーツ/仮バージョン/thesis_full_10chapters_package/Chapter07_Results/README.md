# 第7章 README — データ出典と図表の再現根拠

## 使用した情報源

本章は **`thesis_7032_20260919_195059_703.zip` の正式測定結果のみ**を使用する。外部Web・インターネット・外部ベンチマーク値は使用していない。

主要出典は以下である。

- `thesis_7032_20260919_195059_703\report\thesis_statistical_report.json`
- `thesis_7032_20260919_195059_703\report\thesis_paired_b0_p.csv`
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\novelty01_matrix_summary.json`
- `thesis_7032_20260919_195059_703\quality\B0_vs_P\novelty01_matrix_summary.json`
- `thesis_7032_20260919_195059_703\soak\B0_vs_P\novelty01_matrix_summary.json`
- `thesis_7032_20260919_195059_703\determinism\B0_vs_P\novelty01_matrix_summary.json`
- Performance P S01〜S18 `*_frames.csv`
- Performance P S01〜S18 `*_novelty01.csv`
- Quality P S01〜S18 `*_novelty01.csv`
- Performance P S01〜S18 `*_settings.json`
- Performance P S01〜S18 `*_telemetry.jsonl`

正確な全内部パスは `data/source_files_used_from_original_zip.txt` に列挙している。

## 図表別の出典

| 図表 | 元ZIP内データ | 使用列 / 集計 |
|---|---|---|
| 表7-1 | Final Gate / 各Matrix Summary | Profile別Run数、Warm-up、Measurement Frames |
| 表7-2・図7-1 | Performance `novelty01_matrix_summary.json` | `processMemory.peakProcessPrivateMiB`, `peakProcessWorkingSetMiB`, `peakDxgiLocalUsageMiB` を18シナリオ平均 |
| 表7-3・図7-2 | `thesis_statistical_report.json` / `thesis_paired_b0_p.csv` | Performance `frameMilliseconds` Median |
| 図7-3 | `thesis_statistical_report.json` | Performance `frameMilliseconds` p95 |
| 図7-4 | `thesis_statistical_report.json` | `overBudgetRate` から16.6667 ms以内率を算出 |
| 図7-5 | Performance P `*_frames.csv` | `directionCacheMemoryMb`, `fullPositionMemoryMb` |
| 7.4〜7.8本文 | Performance P `*_frames.csv`, `*_settings.json`, `*_telemetry.jsonl` | Microtype/Mode/Island/Repair/GPU-Resident各カウンタ |
| 図7-6 | `thesis_statistical_report.json` | `gpuHairMilliseconds` Median / p95 の18シナリオ平均 |
| 表7-4・図7-7 | Soak `novelty01_matrix_summary.json` | Average/p95 Frame Time、Peak Private、Slope、DXGI Local |
| 図7-8 | `thesis_paired_b0_p.csv` | 全45ペアの `frameMillisecondsDeltaMs` / 勝敗集計 |
| 表7-5 | Performance P settings + frames | 5要素の設定状態と実行カウンタの最大値 / Full Dynamic率 |

## 解釈上の制約

Final Gate=`PASS` はデータ完全性・実行契約が成立したことを示し、Pの性能優位性を意味しない。Physics Islands / Rod Repairは正式PでOFFであり、単体性能の効果を主張していない。正式アブレーション条件も存在しないため、5要素の因果的寄与を推定値で補っていない。
