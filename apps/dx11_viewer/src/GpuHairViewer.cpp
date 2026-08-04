// Clean ordered aggregation unit generated from the original monolithic source.
// Keep this file as the only compiled translation unit; edit ordered .inl parts below.
// See README_SOURCE_ORDER.md in the part directory for the reading order.
//
// Novelty 01 integration:
// - B0-B8 continue to use the latest Main implementation.
// - Only P_IntegratedSegmentGroup is routed to SG-APT.
// - The Main runtime-foundation include remains enabled.
// - RenderDirtyRange is consumed by a persistent render-position cache.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "hairgpu/ActiveSet.h"
#include "hairgpu/GlobalHairBudget.h"
#include "hairgpu/HairNoveltyFlags.h"
#include "hairgpu/HairNoveltyTelemetry.h"
#include "hairgpu/Authoring.h"
#include "hairgpu/BoneAnimation.h"
#include "hairgpu/HybridRuntime.h"
#include "hairgpu/RenderData.h"
#include "hairgpu/Types.h"

// Preserve the Main implementations as explicit baselines while the public
// class declaration is read. Novelty 01 declarations are now part of the real
// header; no access-control macro rewriting is used.
#define ExecuteExperimentSimulationTick ExecuteExperimentSimulationTickLegacy
#define StepSingleFrame StepSingleFrameLegacy
#include "GpuHairViewer.h"
#undef StepSingleFrame
#undef ExecuteExperimentSimulationTick

#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "hairgpu/AssetIO.h"
#include "hairgpu/Binding.h"
#include "hairgpu/Collision.h"
#include "hairgpu/Math.h"
#include "hairgpu/Metrics.h"
#include "hairgpu/SegmentGroupAdaptivePayload.h"
#include "hairgpu/Wig.h"
#include "hairgpu/Validation.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "comctl32.lib")

using Microsoft::WRL::ComPtr;

#include "GpuHairViewer_parts/00_preamble_helpers_and_phase1_hooks.inl"
#include "GpuHairViewer_parts/01_window_device_and_ui_setup.inl"

// Route only the main run loop to the method dispatcher that selects SG-APT
// for P and the untouched Main implementation for B0-B8.
#define ExecuteExperimentSimulationTick ExecuteNovelty01ExperimentTick
#define Shutdown ShutdownNovelty01Routed
#include "GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
#undef Shutdown
#undef ExecuteExperimentSimulationTick

// Compile the Main simulation functions under explicit Legacy names.
#define ExecuteExperimentSimulationTick ExecuteExperimentSimulationTickLegacy
#define StepSingleFrame StepSingleFrameLegacy
#include "GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl"
#undef StepSingleFrame
#undef ExecuteExperimentSimulationTick

// Keep the original Novelty source available as Core methods. The wrappers in
// part 18 add persistent render-cache update and safe output configuration.
#define StepNovelty01SingleFrame StepNovelty01SingleFrameCore
#include "GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl"
#undef StepNovelty01SingleFrame

#define ShutdownNovelty01Routed ShutdownNovelty01RoutedCore
#include "GpuHairViewer_parts/17_novelty01_formal_evidence.inl"
#undef ShutdownNovelty01Routed

#include "GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl"

// All later call sites use the routed single-frame method.
#define StepSingleFrame StepNovelty01SingleFrame
#include "GpuHairViewer_parts/04_constant_buffers_and_active_set_sync.inl"

// Main's renderer is retained as RenderLegacy. The Novelty wrapper swaps in the
// persistent render cache only while P is rendered.
#define Render RenderLegacy
#include "GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl"
#include "GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl"
#undef Render

// Required by current Main. Never drop this include while integrating Novelty01.
#include "GpuHairViewer_parts/02a_required_runtime_foundation.inl"

#include "GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl"
#include "GpuHairViewer_parts/08_ui_state_sync_and_controls.inl"
#include "GpuHairViewer_parts/09_settings_load_save_and_export.inl"
#include "GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl"
#include "GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl"
#include "GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl"
#include "GpuHairViewer_parts/13_edit_stack_inline_layers_and_masks.inl"
#include "GpuHairViewer_parts/14_scalp_overlay_and_window_messages.inl"
#include "GpuHairViewer_parts/15_shutdown_and_tail.inl"
#undef StepSingleFrame
