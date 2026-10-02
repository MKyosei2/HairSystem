# 第3章 README — データ出典

本章の要求条件・問題規模・入力種別は **`thesis_7032_20260919_195059_703.zip` 内の正式Case/Timeline/Telemetryだけ**を使用した。

## 主な元データ
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\N01_performance_P_S01_seed001_r01\N01_performance_P_S01_seed001_r01_case.json`: Average FPS、60 FPS ratio、Overrun、p95/p99、GPU timing coverage等の正式基準
- Performance B0 S01〜S18 `*_timeline_materialized.json`: Root / Collider / Wind / Edit / Repair Storm / Static-Dynamic / Camera入力
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\N01_performance_P_S01_seed001_r01\N01_performance_P_S01_seed001_r01_frames.csv`: Static/Dynamic、Dirty、Certificate、Repair、GPU同期のRuntime counter

## 図
- **図3-1**: P Performance Caseの閾値を表形式で可視化。
- **図3-2**: 正式に記録された問題規模・入力・Telemetry出力の関係を模式化。

数式中の状態関数 `φ` はZIPに保存されていない閾値を捏造しないための**抽象表記**である。
