# 第8章 README — データ出典と考察の証拠境界

## 使用した情報源

本章は **`thesis_7032_20260919_195059_703.zip` の正式測定結果と、第7章でそこから集計した値のみ**を根拠とする。外部Web・インターネット・外部研究の性能数値は使用していない。

主要出典は次の通りである。

- `thesis_7032_20260919_195059_703\report\thesis_statistical_report.json`
- `thesis_7032_20260919_195059_703\report\thesis_paired_b0_p.csv`
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\novelty01_matrix_summary.json`
- `thesis_7032_20260919_195059_703\quality\B0_vs_P\novelty01_matrix_summary.json`
- `thesis_7032_20260919_195059_703\soak\B0_vs_P\novelty01_matrix_summary.json`
- Performance / Quality Pの `*_frames.csv`, `*_novelty01.csv`, `*_settings.json`, `*_telemetry.jsonl`
- `thesis_7032_20260919_195059_703\integrity\final_gate.json`

正確な全内部パスは `data/source_files_used_from_original_zip.txt` に列挙している。

## 図別の出典

| 図 | 元ZIP内データ | 使用内容 |
|---|---|---|
| 図8-1 | 図7で使ったPerformance実測値 | Microtype局所保存率、Memory、Frame Median/Mean/p95、GPU Hair MedianのB0→P変化率 |
| 図8-2 | Performance P frames + novelty01 | Dirty Block/Group、Static Reuse、Full Dynamic、追加管理処理の観測状態 |
| 図8-3 | Performance P settings + frames | 5要素のON/OFFと主要Runtime Counter |
| 図8-4 | Statistical Report + telemetry + Final Gate | 「今回確認できたこと」と「今回のデータだけでは確認できないこと」を分離 |

## 8.9 既存研究との差分について

元ZIPには、既存研究を同一GPU・同一100,000 strands・同一18シナリオで再測定したデータがない。このため、既存研究に対する速度優位・メモリ優位の数値は本章に追加していない。方式上の差分と、実測による性能優位性を分けて記述している。
