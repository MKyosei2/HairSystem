# GpuHairViewer.cpp source order

`GpuHairViewer.cpp` は現在も唯一の compiled translation unit であり、
ordered `.inl` parts を順番に include する。

この構造は将来的に独立 `.cpp/.h` へ分割する対象だが、本変更では
機能変更と大規模 translation-unit 再編を同時に行わず、まず重複排除を優先する。

| Order | File | Purpose |
|---:|---|---|
| 00 | `00_preamble_helpers_and_phase1_hooks.inl` | Shared helpers / Phase 1 hooks |
| 01 | `01_window_device_and_ui_setup.inl` | Window / device / setup |
| 02 | `02_resource_creation_and_shader_loading.inl` | GPU resources / shader loading |
| 03 | `03_simulation_reset_and_frame_step.inl` | Reset / frame stepping |
| 04 | `04_constant_buffers_and_active_set_sync.inl` | Constant buffers / Active Set sync |
| 05 | `05_active_set_scoring_and_runtime_update.inl` | Active Set scoring / update |
| 06 | `06_render_mesh_and_visibility_recovery.inl` | Render tail / visibility recovery |
| 06a | `02a_required_runtime_foundation.inl` | Required Viewer runtime foundation |
| 07 | `07_draw_path_and_gpu_timing.inl` | Draw path / GPU timing |
| 08 | `08_ui_state_sync_and_controls.inl` | UI state / controls |
| 09 | `09_settings_load_save_and_export.inl` | Settings / save / export |
| 10 | `10_phase1_telemetry_csv_jsonl_summary.inl` | Telemetry / scenario parsing |
| 11 | `11_experiment_env_and_performance_sample.inl` | Experiment environment / samples |
| 12 | `12_strand_pick_gizmo_and_brush_edit.inl` | Picking / gizmo / brush edit |
| 13 | `13_edit_stack_inline_layers_and_masks.inl` | Edit Stack / masks |
| 14 | `14_scalp_overlay_and_window_messages.inl` | Overlay / window messages |
| 15 | `15_shutdown_and_tail.inl` | Shutdown / native self-tests |
| 16 | `16_segment_group_adaptive_payload_complete.inl` | SG-APT runtime |
| 17 | `17_novelty01_formal_evidence.inl` | Novelty01 formal evidence |
| 18 | `18_novelty01_render_dirty_range_consumer.inl` | RenderDirtyRange consumer |
| 19 | `19_thesis_practical_edit_evaluation.inl` | Thesis practical edit evaluation |
