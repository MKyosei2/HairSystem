# 第4章 README — データ出典と数式の位置づけ

本章の5要素名、状態名、Runtime Counter、正式設定は **`thesis_7032_20260919_195059_703.zip`** に基づく。外部図・外部性能値は使用していない。

## 主な元データ
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\N01_performance_P_S01_seed001_r01\N01_performance_P_S01_seed001_r01_settings.json`: `enableMicrotype`, `enablePhysicsIslands`, `enableRodRepair`, `enableGpuResident`, stateStorage等
- `thesis_7032_20260919_195059_703\performance\B0_vs_P\N01_performance_P_S01_seed001_r01\N01_performance_P_S01_seed001_r01_frames.csv`: `mode*Count`, `island*Count`, `repair*Count`, `gpuResidentTaskCount`, `dispatchIndirectCount`, Certificate/Dirty counters
- `thesis_7032_20260919_195059_703\asset\frozen_asset_manifest.json`: Guide/Particle/Block構成

## 図
- **図4-1**: 5構成要素と正式Telemetryの対応を自作模式図として整理。
- **図4-2**: `modeSkipCount`, `modeQSOnlyCount`, `modeDynamicCount`, `modeIslandCount`, `modeRepairCount`, `modeFloatFallbackCount` からMode集合を図示。

## 数式
本章の数式は、ソースコード本文がZIPに含まれないため、正式Telemetryと設定から確認できる**数学的抽象モデル**として記述した。具体的な閾値・量子化ビット数・係数は推定していない。
