# GpuHairViewer.cpp source order

This directory splits the previous monolithic translation unit into ordered include parts. The parent `.cpp` remains the only compiled source file, so build files do not need to compile these `.inl` files directly.

`02a_required_runtime_foundation.inl` restores seven foundational functions that were accidentally omitted during the split. It must remain between parts 02 and 03 because it supplies the general shader compiler, structured-buffer helper, formal viewport calculation, and workspace/UI foundation used by later parts.

| Order | File | Purpose |
|---:|---|---|
| 00 | `00_preamble_helpers_and_phase1_hooks.inl` | Shared helpers and Phase 1 hooks |
| 01 | `01_window_device_and_ui_setup.inl` | Window, device, initialization and experiment setup |
| 02 | `02_resource_creation_and_shader_loading.inl` | GPU resource and timing setup |
| 02a | `02a_required_runtime_foundation.inl` | Restored shader, buffer, viewport and UI/workspace foundation |
| 03 | `03_simulation_reset_and_frame_step.inl` | Shader loading continuation, reset and frame stepping |
| 04 | `04_constant_buffers_and_active_set_sync.inl` | Constant buffers and Active Set synchronization |
| 05 | `05_active_set_scoring_and_runtime_update.inl` | Active Set scoring and runtime update |
| 06 | `06_render_mesh_and_visibility_recovery.inl` | Render tail and visibility recovery |
| 07 | `07_draw_path_and_gpu_timing.inl` | Draw path, layout and GPU timing |
| 08 | `08_ui_state_sync_and_controls.inl` | UI state synchronization and controls |
| 09 | `09_settings_load_save_and_export.inl` | Settings loading, saving and export |
| 10 | `10_phase1_telemetry_csv_jsonl_summary.inl` | Telemetry and summary output |
| 11 | `11_experiment_env_and_performance_sample.inl` | Experiment environment and sample construction |
| 12 | `12_strand_pick_gizmo_and_brush_edit.inl` | Strand picking, gizmo and brush editing |
| 13 | `13_edit_stack_inline_layers_and_masks.inl` | Edit stack, layers and masks |
| 14 | `14_scalp_overlay_and_window_messages.inl` | Scalp overlay and window messages |
| 15 | `15_shutdown_and_tail.inl` | Shutdown, message tail and native self-test |
