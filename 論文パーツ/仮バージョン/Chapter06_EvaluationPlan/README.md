# 第6章 README — データ出典と図表の再現根拠

## 使用した情報源

本章は **`thesis_7032_20260919_195059_703.zip` のみ**を情報源とする。外部Web・インターネット・外部ベンチマーク値は使用していない。

- 元ZIP SHA-256: `0427f5ff361740fc00c966569b7d927b0bab518760d3e236922c7f61d1b7dfe9`
- Formal session: `thesis_7032_20260919_195059_703\formal_session.json`
- Final Gate: `thesis_7032_20260919_195059_703\integrity\final_gate.json`
- Hardware: `thesis_7032_20260919_195059_703\environment\hardware_profile.json`
- Observed hardware: `thesis_7032_20260919_195059_703\environment\observed_hardware.json`
- Performance Matrix: `thesis_7032_20260919_195059_703\performance\B0_vs_P\novelty01_matrix_summary.json`
- Quality Matrix: `thesis_7032_20260919_195059_703\quality\B0_vs_P\novelty01_matrix_summary.json`
- Determinism Matrix: `thesis_7032_20260919_195059_703\determinism\B0_vs_P\novelty01_matrix_summary.json`
- Soak Matrix: `thesis_7032_20260919_195059_703\soak\B0_vs_P\novelty01_matrix_summary.json`
- Pilot Matrix: `thesis_7032_20260919_195059_703\pilot\B0_vs_P\novelty01_matrix_summary.json`

S01〜S18のシナリオ記述は、Performance B0各Caseの `*_timeline_materialized.json` に**実際に記録されたイベント**を用いた。正確な全内部パスは `data/source_files_used_from_original_zip.txt` に列挙している。

## 図表別の出典

| 図表 | 元ZIP内データ | 使用内容 / 集計 |
|---|---|---|
| 表6-1 | `environment\hardware_profile.json`, `environment\observed_hardware.json`, Performance S01 case/settings | CPU/GPU/RAM/OS/Toolchain、strand数、timestep、solver条件 |
| 図6-1 | Performance P S01 `*_visual.bmp` | 正式Viewer出力をPNG化。数値の追加加工なし |
| 表6-2 | Performance B0 S01〜S18 `*_timeline_materialized.json` | scenario名、event type、event count。誤解を避けるため実イベントを優先 |
| 表6-3 | Performance B0/P S01 `*_settings.json` / `*_case.json` | `method_settings` の主要機能フラグ |
| 図6-2 | `formal_session.json`, `final_gate.json`, 各 `novelty01_matrix_summary.json` | same binary/commit、counterbalanced、Profile構成を模式化 |
| 表6-4 | Performance P case/settings + telemetry column definitions | 評価に実際に使用する指標と判定基準を整理 |
| 表6-5 / 図6-3 | `integrity\final_gate.json`, 各Matrix Summary | Profile別Run数、Warm-up / Measurement Frames |

`figures/` と `data/` の内容はいずれもこの元ZIPから作成した。
