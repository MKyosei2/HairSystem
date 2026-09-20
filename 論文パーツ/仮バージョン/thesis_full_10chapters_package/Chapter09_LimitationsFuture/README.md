# 第9章 README — データ出典

本章は **`thesis_7032_20260919_195059_703.zip` の正式結果のみ**から限界と今後の課題を導出した。外部ベンチマークや外部研究の数値は使用していない。

## 主な元データ
- `thesis_7032_20260919_195059_703\report\thesis_statistical_report.json` / `thesis_7032_20260919_195059_703\report\thesis_paired_b0_p.csv`: B0/P性能差、全45ペア
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\novelty01_matrix_summary.json`: Frame/Memory結果
- `thesis_7032_20260919_195059_703\quality\B0_vs_P\novelty01_matrix_summary.json`: Quality結果
- `thesis_7032_20260919_195059_703\soak\B0_vs_P\novelty01_matrix_summary.json`: 長時間結果
- Performance P S01〜S18 `*_frames.csv`, `*_novelty01.csv`, `*_telemetry.jsonl`: Full Dynamic、Decode、Island、Repair、GPU-Residentの実行状態

## 図
- **図9-1**: 正式Performance全108,000フレームの非0率/使用率から作成。
- **図9-2**: 図9-1と第7章結果から必要な再評価順序を模式化。数値スコアや外部優先度は使用していない。
