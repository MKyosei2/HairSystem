# 第1章 README — データ出典

本章の図・数値・研究対象の具体的規模は **`thesis_7032_20260919_195059_703.zip` のみ**から作成した。外部Web・外部画像・外部ベンチマークは使用していない。

## 主な元データ
- `thesis_7032_20260919_195059_703\asset\frozen_asset_manifest.json`: 100,000 display strands、4,000 guides、24 followers/guide、128 particles/guide、6 solver iterations、groupsPerBlock=32
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\N01_performance_P_S01_seed001_r01\N01_performance_P_S01_seed001_r01_case.json`: 1920×1080、VSync OFF、Fixed Timestep、Performance基準
- `thesis_7032_20260919_195059_703\formal_session.json`: sameBinary / sameCommit / counterbalanced schedule
- `thesis_7032_20260919_195059_703\integrity\final_gate.json`: 90 Run、Final Gate status

## 図
- **図1-1**: Frozen Asset ManifestとPerformance P `frames.csv` の `totalSegmentGroupCount`, `totalSegmentGroupBlockCount` から作成。
- **図1-2**: 正式Timelineの入力種別、Asset構成、P method settings、Telemetry出力項目を模式化。外部図版は使用していない。

正確な元ZIP内部パスは `data/source_files_used_from_original_zip.txt` に列挙する。
