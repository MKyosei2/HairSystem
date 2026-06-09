# GpuHairViewer.cpp source order

This directory splits the previous monolithic translation unit into ordered include parts. The parent `.cpp` remains the only compiled source file, so build files do not need to compile these `.inl` files directly.

| Order | File | Purpose | Lines | Bytes |
|---:|---|---|---:|---:|
| 00 | `00_preamble_helpers_and_phase1_hooks.inl` | preamble_helpers_and_phase1_hooks | 691 | 29695 |
| 01 | `01_window_device_and_ui_setup.inl` | window_device_and_ui_setup | 697 | 29409 |
| 02 | `02_resource_creation_and_shader_loading.inl` | resource_creation_and_shader_loading | 699 | 33262 |
| 03 | `03_simulation_reset_and_frame_step.inl` | simulation_reset_and_frame_step | 660 | 38760 |
| 04 | `04_constant_buffers_and_active_set_sync.inl` | constant_buffers_and_active_set_sync | 697 | 39208 |
| 05 | `05_active_set_scoring_and_runtime_update.inl` | active_set_scoring_and_runtime_update | 696 | 32679 |
| 06 | `06_render_mesh_and_visibility_recovery.inl` | render_mesh_and_visibility_recovery | 687 | 46587 |
| 07 | `07_draw_path_and_gpu_timing.inl` | draw_path_and_gpu_timing | 698 | 53356 |
| 08 | `08_ui_state_sync_and_controls.inl` | ui_state_sync_and_controls | 689 | 37407 |
| 09 | `09_settings_load_save_and_export.inl` | settings_load_save_and_export | 697 | 37425 |
| 10 | `10_phase1_telemetry_csv_jsonl_summary.inl` | phase1_telemetry_csv_jsonl_summary | 675 | 34911 |
| 11 | `11_experiment_env_and_performance_sample.inl` | experiment_env_and_performance_sample | 660 | 41269 |
| 12 | `12_strand_pick_gizmo_and_brush_edit.inl` | strand_pick_gizmo_and_brush_edit | 681 | 28164 |
| 13 | `13_edit_stack_inline_layers_and_masks.inl` | edit_stack_inline_layers_and_masks | 700 | 30331 |
| 14 | `14_scalp_overlay_and_window_messages.inl` | scalp_overlay_and_window_messages | 700 | 31219 |
| 15 | `15_shutdown_and_tail.inl` | shutdown_and_tail | 307 | 12626 |
