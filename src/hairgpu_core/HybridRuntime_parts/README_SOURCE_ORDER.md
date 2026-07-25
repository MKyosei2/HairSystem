# HybridRuntime.cpp source order

This directory splits the previous monolithic translation unit into ordered include parts. The parent `.cpp` remains the only compiled source file, so build files do not need to compile these `.inl` files directly.

| Order | File | Purpose | Lines | Bytes |
|---:|---|---|---:|---:|
| 00 | `00_helpers_error_metrics_and_frame_asset.inl` | helpers_error_metrics_and_frame_asset | 640 | 31885 |
| 01 | `01_guide_lod_and_cache_runtime.inl` | guide_lod_and_cache_runtime | 648 | 37376 |
| 02 | `02_hybrid_reduction_and_error_reports.inl` | hybrid_reduction_and_error_reports | 650 | 35241 |
| 03 | `03_sparse_corrective_and_active_runtime.inl` | sparse_corrective_and_active_runtime | 639 | 42405 |
| 04 | `04_temporal_runtime_and_summary.inl` | temporal_runtime_and_summary | 623 | 35254 |
| 05 | `05_part_05.inl` | part_05 | 639 | 40518 |
| 06 | `06_part_06.inl` | part_06 | 180 | 11338 |
