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
// Declare both the untouched implementations and the formal wrappers without
// editing the public header solely for aggregation routing.
#define ExecuteNovelty01ExperimentTick(...) \
    ExecuteNovelty01ExperimentTickLegacy(__VA_ARGS__); \
    void ExecuteNovelty01ExperimentTick(__VA_ARGS__)
#define ApplyScenarioTimelineFrame(...) \
    ApplyScenarioTimelineFrameLegacy(__VA_ARGS__); \
    void ApplyScenarioTimelineFrame(__VA_ARGS__)
#include "GpuHairViewer.h"
#undef ApplyScenarioTimelineFrame
#undef ExecuteNovelty01ExperimentTick
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
#define ExecuteNovelty01ExperimentTick ExecuteNovelty01ExperimentTickLegacy
#include "GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl"
#undef ExecuteNovelty01ExperimentTick
#undef StepNovelty01SingleFrame

// Publication-grade P runs first consume the exact same seeded timeline as B0.
// The original SG-APT tick is retained intact, but its design-scenario selector
// is temporarily disabled so it cannot add Proposal-only wind, root, collider,
// edit, repair, camera, or force-state stimuli on top of the common input.
void GpuHairViewer::ExecuteNovelty01ExperimentTick(float dt)
{
    const bool timelineOwnsExternalInputs =
        N01EnvironmentEnabled(
            L"HAIRGPU_N01_TIMELINE_OWNS_EXTERNAL_INPUTS",
            false);
    if (m_experimentCase.methodId !=
            ExperimentMethodId::P_IntegratedSegmentGroup ||
        !timelineOwnsExternalInputs)
    {
        ExecuteNovelty01ExperimentTickLegacy(dt);
        return;
    }

    ApplyScenarioForTick(m_simulationTickIndex, dt);

    // Runtime creation itself does not inject a design scenario. Creating it
    // here lets the wrapper disable the selector before the legacy P tick runs.
    if (!EnsureNovelty01Runtime(dt))
    {
        ExecuteNovelty01ExperimentTickLegacy(dt);
        return;
    }

    const auto runtime = N01FindRuntime(this);
    if (!runtime)
    {
        ExecuteNovelty01ExperimentTickLegacy(dt);
        return;
    }

    const uint32_t savedDesignScenarioIndex = runtime->designScenarioIndex;
    runtime->designScenarioIndex = std::numeric_limits<uint32_t>::max();
    ExecuteNovelty01ExperimentTickLegacy(dt);
    runtime->designScenarioIndex = savedDesignScenarioIndex;
}

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
#define ApplyScenarioTimelineFrame ApplyScenarioTimelineFrameLegacy
#include "GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl"
#undef ApplyScenarioTimelineFrame


#include "GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl"
#include "GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl"
#include "GpuHairViewer_parts/13_edit_stack_inline_layers_and_masks.inl"
#include "GpuHairViewer_parts/14_scalp_overlay_and_window_messages.inl"
#include "GpuHairViewer_parts/15_shutdown_and_tail.inl"
#undef StepSingleFrame

void hairgpu::viewer::GpuHairViewer::ApplyScenarioTimelineFrame(uint32_t frameIndex)
{
    struct FormalScenarioRootState
    {
        bool initialized = false;
        std::vector<hairgpu::Vec3> baseScalpPositions;
        std::vector<hairgpu::Vec3> baseScalpMeshPositions;
        hairgpu::Vec3 translation = { 0.0f, 0.0f, 0.0f };
        hairgpu::Vec3 rotationRadians = { 0.0f, 0.0f, 0.0f };
    };

    static std::unordered_map<
        hairgpu::viewer::GpuHairViewer*,
        FormalScenarioRootState> states;
    FormalScenarioRootState& state = states[this];

    if (frameIndex == 0u && state.initialized &&
        state.baseScalpPositions.size() == m_restScalpParticles.size())
    {
        for (size_t index = 0u; index < m_restScalpParticles.size(); ++index)
        {
            m_restScalpParticles[index].x = state.baseScalpPositions[index].x;
            m_restScalpParticles[index].y = state.baseScalpPositions[index].y;
            m_restScalpParticles[index].z = state.baseScalpPositions[index].z;
        }
        if (state.baseScalpMeshPositions.size() ==
            m_asset.scalpMesh.positions.size())
        {
            m_asset.scalpMesh.positions = state.baseScalpMeshPositions;
        }
        state = FormalScenarioRootState{};
    }

    if (!state.initialized ||
        state.baseScalpPositions.size() != m_restScalpParticles.size())
    {
        state = FormalScenarioRootState{};
        state.baseScalpPositions.reserve(m_restScalpParticles.size());
        for (const GpuParticle& particle : m_restScalpParticles)
        {
            state.baseScalpPositions.emplace_back(
                particle.x,
                particle.y,
                particle.z);
        }
        state.baseScalpMeshPositions = m_asset.scalpMesh.positions;
        state.initialized = true;
    }

    const Vec3 originalScalpCenter = m_scalpCenter;
    ApplyScenarioTimelineFrameLegacy(frameIndex);

    bool rootInputChanged = false;
    for (const ScenarioEvent& event : m_experimentCase.timeline.events)
    {
        if (event.frame != frameIndex)
        {
            continue;
        }

        switch (event.type)
        {
        case ScenarioEventType::SetRootTranslation:
            state.translation =
                Vec3(event.value[0], event.value[1], event.value[2]);
            ++m_scenarioWakeRequestCount;
            rootInputChanged = true;
            break;
        case ScenarioEventType::SetRootRotation:
        {
            constexpr float kDegreesToRadians =
                0.017453292519943295769f;
            state.rotationRadians = Vec3(
                event.value[0] * kDegreesToRadians,
                event.value[1] * kDegreesToRadians,
                event.value[2] * kDegreesToRadians);
            ++m_scenarioWakeRequestCount;
            rootInputChanged = true;
            break;
        }
        case ScenarioEventType::TeleportRoot:
            state.translation.x += event.value[0];
            state.translation.y += event.value[1];
            state.translation.z += event.value[2];
            rootInputChanged = true;
            break;
        default:
            break;
        }
    }

    if (!rootInputChanged)
    {
        return;
    }

    // Undo the old TeleportRoot side effect and disable procedural scalp motion;
    // formal root motion is defined exclusively by the seeded timeline values.
    m_scalpCenter = originalScalpCenter;
    m_enableScalpMotion = false;

    const float cx = std::cos(state.rotationRadians.x);
    const float sx = std::sin(state.rotationRadians.x);
    const float cy = std::cos(state.rotationRadians.y);
    const float sy = std::sin(state.rotationRadians.y);
    const float cz = std::cos(state.rotationRadians.z);
    const float sz = std::sin(state.rotationRadians.z);

    for (size_t index = 0u; index < m_restScalpParticles.size(); ++index)
    {
        const Vec3 local = state.baseScalpPositions[index] - m_scalpCenter;
        const Vec3 rotatedX(
            local.x,
            local.y * cx - local.z * sx,
            local.y * sx + local.z * cx);
        const Vec3 rotatedY(
            rotatedX.x * cy + rotatedX.z * sy,
            rotatedX.y,
            -rotatedX.x * sy + rotatedX.z * cy);
        const Vec3 rotatedZ(
            rotatedY.x * cz - rotatedY.y * sz,
            rotatedY.x * sz + rotatedY.y * cz,
            rotatedY.z);
        const Vec3 transformed =
            rotatedZ + m_scalpCenter + state.translation;
        m_restScalpParticles[index].x = transformed.x;
        m_restScalpParticles[index].y = transformed.y;
        m_restScalpParticles[index].z = transformed.z;
    }

    if (state.baseScalpMeshPositions.size() ==
        m_asset.scalpMesh.positions.size())
    {
        for (size_t index = 0u;
             index < m_asset.scalpMesh.positions.size();
             ++index)
        {
            const Vec3 local =
                state.baseScalpMeshPositions[index] - m_scalpCenter;
            const Vec3 rotatedX(
                local.x,
                local.y * cx - local.z * sx,
                local.y * sx + local.z * cx);
            const Vec3 rotatedY(
                rotatedX.x * cy + rotatedX.z * sy,
                rotatedX.y,
                -rotatedX.x * sy + rotatedX.z * cy);
            const Vec3 rotatedZ(
                rotatedY.x * cz - rotatedY.y * sz,
                rotatedY.x * sz + rotatedY.y * cz,
                rotatedY.z);
            m_asset.scalpMesh.positions[index] =
                rotatedZ + m_scalpCenter + state.translation;
        }
    }

    m_forceAllSegmentGroupsDirty = true;
}
