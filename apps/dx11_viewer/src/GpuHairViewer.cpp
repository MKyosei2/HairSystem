// Clean ordered aggregation unit generated from the original monolithic source.
// Keep this file as the only compiled translation unit; edit ordered .inl parts below.
// See README_SOURCE_ORDER.md in the part directory for the reading order.

#include "GpuHairViewer.h"

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/AssetIO.h"
#include "hairgpu/BoneAnimation.h"
#include "hairgpu/Binding.h"
#include "hairgpu/Collision.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/Math.h"
#include "hairgpu/Metrics.h"
#include "hairgpu/Wig.h"
#include "hairgpu/Validation.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "comctl32.lib")

using Microsoft::WRL::ComPtr;

#include "GpuHairViewer_parts/00_preamble_helpers_and_phase1_hooks.inl"  // preamble_helpers_and_phase1_hooks
#include "GpuHairViewer_parts/01_window_device_and_ui_setup.inl"  // window_device_and_ui_setup
#include "GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"  // resource_creation_and_shader_loading
#include "GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl"  // simulation_reset_and_frame_step
#include "GpuHairViewer_parts/04_constant_buffers_and_active_set_sync.inl"  // constant_buffers_and_active_set_sync
#include "GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl"  // active_set_scoring_and_runtime_update
#include "GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl"  // render_mesh_and_visibility_recovery
#include "GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl"  // draw_path_and_gpu_timing
#include "GpuHairViewer_parts/08_ui_state_sync_and_controls.inl"  // ui_state_sync_and_controls
#include "GpuHairViewer_parts/09_settings_load_save_and_export.inl"  // settings_load_save_and_export
#include "GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl"  // phase1_telemetry_csv_jsonl_summary
#include "GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl"  // experiment_env_and_performance_sample
#include "GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl"  // strand_pick_gizmo_and_brush_edit
#include "GpuHairViewer_parts/13_edit_stack_inline_layers_and_masks.inl"  // edit_stack_inline_layers_and_masks
#include "GpuHairViewer_parts/14_scalp_overlay_and_window_messages.inl"  // scalp_overlay_and_window_messages
#include "GpuHairViewer_parts/15_shutdown_and_tail.inl"  // shutdown_and_tail
