# 第10章 README — データ出典

本章の結論は **`thesis_7032_20260919_195059_703.zip` の正式実験結果だけ**に基づく。

## 主な元データ
- `thesis_7032_20260919_195059_703\formal_session.json` / `thesis_7032_20260919_195059_703\integrity\final_gate.json`: 90 Run、sameBinary/sameCommit、Integrity
- `thesis_7032_20260919_195059_703\report\thesis_statistical_report.json` / `thesis_7032_20260919_195059_703\report\thesis_paired_b0_p.csv`: Frame/GPUのB0/P差
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\novelty01_matrix_summary.json`: MemoryとPerformance
- `thesis_7032_20260919_195059_703\quality\B0_vs_P\novelty01_matrix_summary.json`: False Static /品質
- P Runtime Telemetry: 5要素の実稼働状態

## 図
- **図10-1**: Median frame、p95、Peak Process Private、DXGI Local、Median GPU Hair TimeをB0=100に正規化。
- **図10-2**: 提案→正式評価→負の結果→失敗要因特定という本研究の到達点を自作模式図として整理。

「高速化成功」という主張は行わず、正式データが支持する範囲に限定している。
