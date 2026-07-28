// Clean ordered aggregation unit generated from the original monolithic source.
// Keep this file as the only compiled translation unit; edit ordered .inl parts below.
// See README_SOURCE_ORDER.md in the part directory for the reading order.
//
// Novelty 01 integration note:
// The original experiment and single-step methods are retained under Legacy names.
// Only P_IntegratedSegmentGroup is routed to the complete SG-APT implementation.

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Pre-include every direct dependency of GpuHairViewer.h. The temporary
// `private` macro below then affects only the viewer class body, never Windows,
// STL, WRL, or hairgpu dependency headers.
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

// Preserve the Version-68 implementations as explicit baselines.
#define ExecuteExperimentSimulationTick ExecuteExperimentSimulationTickLegacy
#define StepSingleFrame StepSingleFrameLegacy

// Inject the Novelty 01 declarations into the existing class without replacing
// the large header and without changing public API or ABI outside this unit.
#define private \
public: \
    struct Novelty01RuntimeState; \
    void ExecuteNovelty01ExperimentTick(float dt); \
    void StepNovelty01SingleFrame(); \
    void ExecuteNovelty01Complete(float dt); \
    bool EnsureNovelty01Runtime(float dt); \
    void ResetNovelty01RuntimeIfNeeded(); \
    void RunNovelty01PartialSimulation(float dt); \
    bool RunNovelty01GroundTruth(float dt); \
    void CompareNovelty01GroundTruth(); \
    void WriteNovelty01FormalTelemetry(); \
    void ValidateNovelty01FormalInvariants(); \
    void ExecuteNovelty01PreClassificationValidation(); \
    void ExecuteNovelty01PostCompactionLifecycle(); \
    void ExecuteNovelty01DirtyRangePropagation(); \
    void ExecuteNovelty01BuildIndirectArguments(); \
    void ExecuteNovelty01B4MigrationBaseline(); \
    void ApplyNovelty01DesignScenarioForTick(float dt); \
    void ShutdownNovelty01Routed(); \
private

#include "GpuHairViewer.h"

#undef private
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
// for P and the untouched legacy implementation for B0-B8.
#define ExecuteExperimentSimulationTick ExecuteNovelty01ExperimentTick
#define Shutdown ShutdownNovelty01Routed
#include "GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
#undef Shutdown
#undef ExecuteExperimentSimulationTick

// Compile the original Version-68 functions under explicit Legacy names.
#define ExecuteExperimentSimulationTick ExecuteExperimentSimulationTickLegacy
#define StepSingleFrame StepSingleFrameLegacy
#include "GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl"
#undef StepSingleFrame
#undef ExecuteExperimentSimulationTick

#include "GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl"
#include "GpuHairViewer_parts/17_novelty01_formal_evidence.inl"

// All later call sites use the routed single-frame method. The legacy method
// remains available to the routed implementation and to baseline code.
#define StepSingleFrame StepNovelty01SingleFrame
#include "GpuHairViewer_parts/04_constant_buffers_and_active_set_sync.inl"
#include "GpuHairViewer_parts/05_active_set_scoring_and_runtime_update.inl"
#include "GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl"
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
