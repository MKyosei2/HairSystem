# 第5章 README — データ出典と実装記述

本章はソースコードを引用せず、**正式ZIPに残された設定・Telemetry・Visual出力から確認できる実装構造を数式で記述**する。

## 主な元データ
- `thesis_7032_20260919_195059_703\environment\hardware_profile.json` / `thesis_7032_20260919_195059_703\environment\observed_hardware.json`: 実装・測定環境
- `thesis_7032_20260919_195059_703\formal_session.json`: Viewer SHA、Commit、sameBinary/sameCommit
- `thesis_7032_20260919_195059_703\asset\frozen_asset_manifest.json`: 100k asset、groupsPerBlock
- `thesis_7032_20260919_195059_703\integrity\novelty01_preflight.json`: shaderEntryPointCount=50、正式Preflight
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\N01_performance_P_S01_seed001_r01\N01_performance_P_S01_seed001_r01_settings.json`: method flags、IntegratedRecord
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\N01_performance_P_S01_seed001_r01\N01_performance_P_S01_seed001_r01_frames.csv`: Classification / Compaction / Solve / Repair / Follower Reconstruction / Render等のfield
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\N01_performance_P_S01_seed001_r01\N01_performance_P_S01_seed001_r01_visual.bmp`: 図5-3の元BMP

## 図
- **図5-1**: `frames.csv` の処理時間・Counter名からRuntime段階を整理した自作図。
- **図5-2**: Asset ManifestとRuntime counterによるデータ階層。
- **図5-3**: 元ZIPの正式Viewer BMPをPNG変換したもの。内容加工なし。

数式はコードそのものではなく、観測可能な実装を説明する抽象表現である。
