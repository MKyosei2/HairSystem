# main.cpp source order

This directory splits the previous monolithic translation unit into ordered include parts. The parent `.cpp` remains the only compiled source file, so build files do not need to compile these `.inl` files directly.

| Order | File | Purpose | Lines | Bytes |
|---:|---|---|---:|---:|
| 00 | `00_helpers_and_asset_io_commands.inl` | helpers_and_asset_io_commands | 550 | 21261 |
| 01 | `01_runtime_suite_and_evaluation_commands.inl` | runtime_suite_and_evaluation_commands | 542 | 25088 |
| 02 | `02_authoring_edit_metrics_and_reports.inl` | authoring_edit_metrics_and_reports | 419 | 23941 |
| 03 | `03_main_argument_dispatch.inl` | main_argument_dispatch | 550 | 25281 |
| 04 | `04_part_04.inl` | part_04 | 550 | 21964 |
| 05 | `05_part_05.inl` | part_05 | 547 | 24486 |
| 06 | `06_part_06.inl` | part_06 | 550 | 21255 |
| 07 | `07_part_07.inl` | part_07 | 48 | 2315 |
