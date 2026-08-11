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

namespace
{
    // HAIRGPU_FORMAL_POST_MEASUREMENT_BARRIER_V1
    // Performance evidence deliberately avoids the quality-mode per-frame counter
    // readback.  The missing readback must not be replaced inside the measured
    // frame interval because doing so would contaminate performance timing.
    // Instead, wait for all GPU work only after the measurement/visual-capture
    // interval and before telemetry export/resource teardown.
    bool HairGpuWaitForGpuIdleAfterMeasurement(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        DWORD timeoutMilliseconds = 15000u)
    {
        if (!device || !context)
        {
            return true;
        }

        D3D11_QUERY_DESC queryDesc = {};
        queryDesc.Query = D3D11_QUERY_EVENT;
        queryDesc.MiscFlags = 0u;

        ComPtr<ID3D11Query> completionQuery;
        if (FAILED(device->CreateQuery(&queryDesc, &completionQuery)) ||
            !completionQuery)
        {
            return false;
        }

        context->End(completionQuery.Get());
        context->Flush();

        const ULONGLONG started = GetTickCount64();
        for (;;)
        {
            BOOL completed = FALSE;
            const HRESULT result = context->GetData(
                completionQuery.Get(),
                &completed,
                static_cast<UINT>(sizeof(completed)),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (result == S_OK && completed)
            {
                return true;
            }
            if (FAILED(result))
            {
                return false;
            }
            if (GetTickCount64() - started >= timeoutMilliseconds)
            {
                return false;
            }
            Sleep(1u);
        }
    }

    // HAIRGPU_N01_PERFORMANCE_QUERY_GATED_READBACK_V2
    // SG-APT's quality path intentionally reads back the full dynamic/repair
    // index lists so it can build CPU-side validation/diagnostic views. Those
    // multi-megabyte staging copies are not part of the production simulation
    // algorithm and become unsafe/high-pressure when Performance submits frames
    // much faster than Quality. Performance therefore keeps only the 64-counter
    // asynchronous feedback required by the FullDynamicFastPath controller.
    // V2 removes the remaining age-only Map heuristic: every staging copy is
    // paired with an EVENT query and is mapped only after the GPU confirms that
    // the CopyResource has completed. This avoids a P/Performance-only race with
    // the D3D11 driver observed as 0xC0000005 on S01.
    struct HairGpuN01PerformanceCounterReadbackSlot
    {
        ComPtr<ID3D11Buffer> counters;
        ComPtr<ID3D11Query> completionQuery;
        uint32_t sourceFrame = 0u;
        uint32_t ageFrames = 0u;
        bool issued = false;
    };

    struct HairGpuN01PerformanceCounterReadbackState
    {
        std::array<HairGpuN01PerformanceCounterReadbackSlot, 16u> slots = {};
        uint32_t writeIndex = 0u;
    };

    std::unordered_map<
        hairgpu::viewer::GpuHairViewer*,
        HairGpuN01PerformanceCounterReadbackState>&
    HairGpuN01PerformanceCounterReadbacks()
    {
        static std::unordered_map<
            hairgpu::viewer::GpuHairViewer*,
            HairGpuN01PerformanceCounterReadbackState> states;
        return states;
    }

    void HairGpuReleaseN01PerformanceCounterReadbacks(
        hairgpu::viewer::GpuHairViewer* viewer)
    {
        HairGpuN01PerformanceCounterReadbacks().erase(viewer);
    }

    // HAIRGPU_N01_PERFORMANCE_POST_MEASUREMENT_DRAIN_V3
    // A later EVENT query completing proves every earlier CopyResource/End pair
    // has completed too.  Release the P-only staging/query ring immediately after
    // that global barrier, before the legacy drain and shutdown paths touch other
    // D3D11 query/resource state.  This is intentionally outside the measured
    // interval and therefore cannot improve or contaminate Performance timings.
    bool HairGpuDrainAndReleaseN01PerformanceCounterReadbacksAfterGpuIdle(
        hairgpu::viewer::GpuHairViewer* viewer,
        ID3D11DeviceContext* context)
    {
        auto& states = HairGpuN01PerformanceCounterReadbacks();
        const auto found = states.find(viewer);
        if (found == states.end())
        {
            return true;
        }

        if (!context)
        {
            states.erase(found);
            return false;
        }

        bool valid = true;
        for (HairGpuN01PerformanceCounterReadbackSlot& slot : found->second.slots)
        {
            if (!slot.issued)
            {
                continue;
            }
            if (!slot.completionQuery || !slot.counters)
            {
                valid = false;
                continue;
            }

            BOOL completed = FALSE;
            const HRESULT result = context->GetData(
                slot.completionQuery.Get(),
                &completed,
                static_cast<UINT>(sizeof(completed)),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (result != S_OK || !completed)
            {
                valid = false;
            }

            // No Map is required here. The measurement has ended and the latest
            // feedback already used by the controller is sufficient for export.
            slot.issued = false;
            slot.ageFrames = 0u;
            slot.completionQuery.Reset();
            slot.counters.Reset();
        }
        states.erase(found);
        return valid;
    }

    // HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_V1
    // Enabled only by qualification/debug runs. Formal 100-run evidence leaves
    // HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_FILE unset, so measured frames perform
    // no file I/O. The durable last checkpoint helps distinguish a simulation,
    // final-capture, GPU-drain, or shutdown crash when Windows terminates the
    // Viewer with 0xC0000005 before C++ can report an ordinary fatal code.
    void HairGpuAppendFormalDiagnosticCheckpoint(
        const wchar_t* stage,
        uint32_t frameIndex)
    {
        if (!stage)
        {
            return;
        }
        wchar_t pathBuffer[32768] = {};
        const DWORD length = GetEnvironmentVariableW(
            L"HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_FILE",
            pathBuffer,
            static_cast<DWORD>(std::size(pathBuffer)));
        if (length == 0u || length >= std::size(pathBuffer))
        {
            return;
        }

        std::ofstream stream(
            std::filesystem::path(pathBuffer),
            std::ios::binary | std::ios::app);
        if (!stream)
        {
            return;
        }
        stream
            << "tick=" << static_cast<unsigned long long>(GetTickCount64())
            << ",pid=" << static_cast<unsigned long>(GetCurrentProcessId())
            << ",frame=" << frameIndex
            << ",stage=";
        for (const wchar_t* cursor = stage; *cursor; ++cursor)
        {
            const wchar_t value = *cursor;
            stream << static_cast<char>(
                value >= 0 && value <= 0x7f ? value : L'?');
        }
        stream << "\n";
        stream.flush();
    }

    // HAIRGPU_FORMAL_SOAK_SAMPLE_RETENTION_V1
    // The legacy Run() include trims m_performanceSamples at 12,000 rows for
    // interactive UI history. Formal Soak requires all 108,000 measured rows.
    // This proxy changes only the trim predicate's size() observation while all
    // actual container operations still target the original vector. Export and
    // validation therefore receive the complete formal sample set.
    template <typename TContainer>
    class HairGpuPerformanceSampleHistoryView
    {
    public:
        using value_type = typename TContainer::value_type;
        using iterator = typename TContainer::iterator;
        using const_iterator = typename TContainer::const_iterator;

        HairGpuPerformanceSampleHistoryView(
            TContainer& container,
            bool formalExperiment)
            : m_container(container),
              m_formalExperiment(formalExperiment)
        {
        }

        void clear() { m_container.clear(); }
        void reserve(size_t count) { m_container.reserve(count); }
        void push_back(const value_type& value) { m_container.push_back(value); }
        void push_back(value_type&& value) { m_container.push_back(std::move(value)); }
        bool empty() const { return m_container.empty(); }
        value_type& back() { return m_container.back(); }
        const value_type& back() const { return m_container.back(); }
        iterator begin() { return m_container.begin(); }
        iterator end() { return m_container.end(); }
        const_iterator begin() const { return m_container.begin(); }
        const_iterator end() const { return m_container.end(); }
        iterator erase(iterator first, iterator last)
        {
            return m_container.erase(first, last);
        }

        size_t size() const
        {
            // Run() compares size() with 12000 solely for the interactive trim.
            // Keep that predicate false for formal runs without dropping data.
            return m_formalExperiment
                ? std::min<size_t>(m_container.size(), 12000u)
                : m_container.size();
        }

    private:
        TContainer& m_container;
        bool m_formalExperiment = false;
    };

    template <typename TContainer>
    HairGpuPerformanceSampleHistoryView<TContainer>
    HairGpuFormalPerformanceSampleHistory(
        TContainer& container,
        bool formalExperiment)
    {
        return HairGpuPerformanceSampleHistoryView<TContainer>(
            container,
            formalExperiment);
    }
}

#include "GpuHairViewer_parts/00_preamble_helpers_and_phase1_hooks.inl"
#include "GpuHairViewer_parts/01_window_device_and_ui_setup.inl"

// Route only the main run loop to the method dispatcher that selects SG-APT
// for P and the untouched Main implementation for B0-B8.
#define ExecuteExperimentSimulationTick ExecuteNovelty01ExperimentTick
// HAIRGPU_FORMAL_POST_MEASUREMENT_BARRIER_V1:
// The original Run() calls DrainGpuTimingAndCounterReadbacks() after its measured
// loop and after the final visual capture.  Interpose only at that exact call.
// Legacy Performance methods get one terminal counter copy outside the measured
// frames. P already owns a dedicated counter-only SG-APT readback controller and
// must not touch the unrelated legacy segment-group counter resource at teardown.
// All methods then use the event-query GPU completion barrier before teardown.
// The macro self-reference intentionally resolves to the real member function
// after this replacement is disabled by the preprocessor.
#define DrainGpuTimingAndCounterReadbacks()                                      \
    do                                                                            \
    {                                                                             \
        const bool hairGpuFormalPPerformance =                                    \
            IsExperimentModeEnabled() &&                                          \
            m_experimentCase.measurementMode == MeasurementMode::Performance &&    \
            m_experimentCase.methodId ==                                           \
                ExperimentMethodId::P_IntegratedSegmentGroup;                      \
        HairGpuAppendFormalDiagnosticCheckpoint(                                  \
            L"post_measurement_before_terminal_drain",                            \
            m_globalFrameIndex);                                                   \
        if (IsExperimentModeEnabled() &&                                          \
            m_experimentCase.measurementMode == MeasurementMode::Performance &&    \
            !hairGpuFormalPPerformance)                                            \
        {                                                                         \
            QueueCounterReadback(m_globalFrameIndex);                              \
        }                                                                         \
        const bool hairGpuBarrierPassed =                                         \
            !IsExperimentModeEnabled() ||                                          \
            HairGpuWaitForGpuIdleAfterMeasurement(                                \
                m_device.Get(),                                                    \
                m_context.Get());                                                  \
        if (!hairGpuBarrierPassed)                                                 \
        {                                                                         \
            SetFatalExperimentFailure(                                             \
                1501u,                                                             \
                L"Post-measurement GPU completion barrier failed or timed out."); \
            runExitCode = 94;                                                      \
        }                                                                         \
        HairGpuAppendFormalDiagnosticCheckpoint(                                  \
            L"post_measurement_after_gpu_idle",                                  \
            m_globalFrameIndex);                                                   \
        if (hairGpuFormalPPerformance && hairGpuBarrierPassed && m_context)         \
        {                                                                         \
            m_context->ClearState();                                               \
            m_context->Flush();                                                    \
            if (!HairGpuDrainAndReleaseN01PerformanceCounterReadbacksAfterGpuIdle( \
                    this,                                                          \
                    m_context.Get()))                                              \
            {                                                                     \
                SetFatalExperimentFailure(                                         \
                    1510u,                                                         \
                    L"P/Performance counter readback ring was not complete "       \
                    L"after the post-measurement GPU idle barrier.");              \
                runExitCode = 95;                                                  \
            }                                                                     \
        }                                                                         \
        HairGpuAppendFormalDiagnosticCheckpoint(                                  \
            L"post_measurement_before_legacy_drain",                             \
            m_globalFrameIndex);                                                   \
        DrainGpuTimingAndCounterReadbacks();                                       \
        HairGpuAppendFormalDiagnosticCheckpoint(                                  \
            L"post_measurement_after_legacy_drain",                              \
            m_globalFrameIndex);                                                   \
        if (IsExperimentModeEnabled() &&                                          \
            m_experimentCase.measurementMode == MeasurementMode::Performance &&    \
            m_context)                                                             \
        {                                                                         \
            m_context->ClearState();                                               \
            m_context->Flush();                                                    \
            HairGpuReleaseN01PerformanceCounterReadbacks(this);                    \
        }                                                                         \
    } while (false)
#define Shutdown ShutdownNovelty01Routed
// Formal experiments must retain every measured PerformanceSample. The included
// Run() still keeps its 12k interactive UI history cap; route only this include's
// member accesses through a formal-aware view so the trim remains interactive-only.
#define m_performanceSamples \
    HairGpuFormalPerformanceSampleHistory( \
        m_performanceSamples, IsExperimentModeEnabled())
#include "GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
#undef m_performanceSamples
#undef Shutdown
#undef DrainGpuTimingAndCounterReadbacks
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
    // HAIRGPU_FORMAL_SAMPLE_PREALLOC_V1
    // Allocate the complete evidence vector during the first warm-up tick. This
    // prevents std::vector growth/copies from entering Quality/Performance/Soak
    // measured intervals and is applied identically to B0 and P.
    if (IsExperimentModeEnabled() && m_simulationTickIndex == 0u)
    {
        m_performanceSamples.clear();
        m_performanceSamples.reserve(
            static_cast<size_t>(m_experimentCase.measurementFrames));
    }

    // HAIRGPU_FORMAL_METHOD_LABEL_V1
    // Every matrix case is a separate Viewer process. Label the first simulation
    // tick so an unattended/interactive run makes the active thesis side explicit
    // without adding per-frame UI work to the measured interval.
    if (m_hwnd && m_simulationTickIndex == 0u)
    {
        if (m_experimentCase.methodId == ExperimentMethodId::B0_FullDynamic)
        {
            SetWindowTextW(
                m_hwnd,
                L"HairSystem Thesis | PRE-NOVELTY BASELINE [B0]");
        }
        else if (m_experimentCase.methodId ==
                 ExperimentMethodId::P_IntegratedSegmentGroup)
        {
            SetWindowTextW(
                m_hwnd,
                L"HairSystem Thesis | POST-NOVELTY [P / SG-APT]");
        }
    }

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

    const bool performanceMode =
        m_experimentCase.measurementMode == MeasurementMode::Performance;

    if (performanceMode)
    {
        const uint32_t warmup = m_experimentCase.warmupFrames;
        const uint32_t measurementEnd =
            warmup + m_experimentCase.measurementFrames;
        if (m_globalFrameIndex == 0u ||
            m_globalFrameIndex + 1u == warmup ||
            m_globalFrameIndex == warmup ||
            m_globalFrameIndex == warmup + 1u ||
            m_globalFrameIndex + 1u == measurementEnd)
        {
            HairGpuAppendFormalDiagnosticCheckpoint(
                L"p_performance_before_tick",
                m_globalFrameIndex);
        }
        // Performance is defined by the formal matrix with ground_truth=false.
        // kAllFeatures historically retained Feature_GroundTruth even when the
        // corresponding resources were not created. Make the execution mask
        // match the actual resource contract before the first P dispatch.
        if (runtime->formalGroundTruthEnabled)
        {
            SetFatalExperimentFailure(
                1502u,
                L"Performance P run requested Ground Truth resources. "
                L"The thesis Performance profile requires ground_truth=false.");
            return;
        }
        runtime->featureMask &=
            ~hairgpu::novelty01::Feature_GroundTruth;
        runtime->formalGroundTruthValidThisFrame = false;

        // Prevent the legacy SG-APT diagnostics ring from issuing/mapping the
        // full dynamicList + repairList staging copies. These CPU vectors are
        // used only for diagnostics/telemetry sizing; GPU follower propagation
        // is driven by followerFlags/activeFollowerList and does not consume them.
        for (auto& legacySlot : runtime->readbacks)
        {
            legacySlot.issued = true;
            legacySlot.ageFrames = 0u;
            legacySlot.formalMetricsIssued = false;
        }

        HairGpuN01PerformanceCounterReadbackState& state =
            HairGpuN01PerformanceCounterReadbacks()[this];

        // Resolve only tiny counter snapshots. Age is used only to avoid polling
        // a query immediately after submission; the EVENT query, not frame age,
        // is the synchronization authority for CPU mapping.
        for (HairGpuN01PerformanceCounterReadbackSlot& slot : state.slots)
        {
            if (!slot.issued)
            {
                continue;
            }
            ++slot.ageFrames;
            if (slot.ageFrames < 2u)
            {
                continue;
            }
            if (!slot.counters || !slot.completionQuery || !m_context)
            {
                SetFatalExperimentFailure(
                    1508u,
                    L"Performance SG-APT readback slot lost its staging/query resource.");
                return;
            }

            BOOL copyCompleted = FALSE;
            const HRESULT completionResult = m_context->GetData(
                slot.completionQuery.Get(),
                &copyCompleted,
                static_cast<UINT>(sizeof(copyCompleted)),
                D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (completionResult == S_FALSE ||
                (completionResult == S_OK && !copyCompleted))
            {
                continue;
            }
            if (FAILED(completionResult))
            {
                SetFatalExperimentFailure(
                    1507u,
                    L"Performance SG-APT counter completion query failed.");
                return;
            }

            D3D11_MAPPED_SUBRESOURCE mapped = {};
            const HRESULT mapResult = m_context->Map(
                slot.counters.Get(),
                0u,
                D3D11_MAP_READ,
                0u,
                &mapped);
            if (FAILED(mapResult))
            {
                SetFatalExperimentFailure(
                    1506u,
                    L"Performance SG-APT counter staging Map failed after GPU completion.");
                return;
            }
            if (!mapped.pData)
            {
                m_context->Unmap(slot.counters.Get(), 0u);
                SetFatalExperimentFailure(
                    1503u,
                    L"Performance SG-APT counter readback returned null data.");
                return;
            }

            std::memcpy(
                runtime->latestCounters.values.data(),
                mapped.pData,
                sizeof(uint32_t) * hairgpu::novelty01::kCounterCount);
            m_context->Unmap(slot.counters.Get(), 0u);

            runtime->latestCountersValid = true;
            runtime->latestCounterSourceFrame = slot.sourceFrame;

            // The CPU-side follower vector is telemetry-only in the integrated
            // runtime. Keep its size meaningful without copying 508k group IDs.
            const uint32_t dirtyFollowers = std::min<uint32_t>(
                runtime->latestCounters.Get(
                    hairgpu::novelty01::Counter_DirtyFollowers),
                m_asset.followerStrandCount);
            runtime->activeGuides.clear();
            runtime->activeFollowers.assign(dirtyFollowers, 0u);

            slot.issued = false;
            slot.ageFrames = 0u;
            ++m_counterReadbackResolvedCount;
        }
    }

    const uint32_t savedDesignScenarioIndex = runtime->designScenarioIndex;
    runtime->designScenarioIndex = std::numeric_limits<uint32_t>::max();
    ExecuteNovelty01ExperimentTickLegacy(dt);
    runtime->designScenarioIndex = savedDesignScenarioIndex;

    if (performanceMode)
    {
        const uint32_t warmup = m_experimentCase.warmupFrames;
        const uint32_t measurementEnd =
            warmup + m_experimentCase.measurementFrames;
        if (m_globalFrameIndex == 0u ||
            m_globalFrameIndex + 1u == warmup ||
            m_globalFrameIndex == warmup ||
            m_globalFrameIndex == warmup + 1u ||
            m_globalFrameIndex + 1u == measurementEnd)
        {
            HairGpuAppendFormalDiagnosticCheckpoint(
                L"p_performance_after_legacy_tick",
                m_globalFrameIndex);
        }
    }

    if (performanceMode &&
        m_experimentRunStatus != ExperimentRunStatus::Failed)
    {
        // The legacy tick increments the internal readback ages once. Re-pin
        // them so the next tick cannot Map or re-issue the large list copies.
        for (auto& legacySlot : runtime->readbacks)
        {
            legacySlot.issued = true;
            legacySlot.ageFrames = 0u;
            legacySlot.formalMetricsIssued = false;
        }

        HairGpuN01PerformanceCounterReadbackState& state =
            HairGpuN01PerformanceCounterReadbacks()[this];
        HairGpuN01PerformanceCounterReadbackSlot& slot =
            state.slots[state.writeIndex];
        if (!slot.issued)
        {
            if (!slot.counters)
            {
                if (!runtime->counters.buffer)
                {
                    SetFatalExperimentFailure(
                        1504u,
                        L"Performance SG-APT counter source is missing.");
                    return;
                }

                D3D11_BUFFER_DESC desc = {};
                runtime->counters.buffer->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING;
                desc.BindFlags = 0u;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.MiscFlags = 0u;
                desc.StructureByteStride = 0u;
                if (FAILED(m_device->CreateBuffer(
                        &desc,
                        nullptr,
                        &slot.counters)) ||
                    !slot.counters)
                {
                    SetFatalExperimentFailure(
                        1505u,
                        L"Performance SG-APT counter staging allocation failed.");
                    return;
                }
            }

            if (!slot.completionQuery)
            {
                D3D11_QUERY_DESC queryDesc = {};
                queryDesc.Query = D3D11_QUERY_EVENT;
                queryDesc.MiscFlags = 0u;
                if (!m_device ||
                    FAILED(m_device->CreateQuery(
                        &queryDesc,
                        &slot.completionQuery)) ||
                    !slot.completionQuery)
                {
                    SetFatalExperimentFailure(
                        1509u,
                        L"Performance SG-APT counter completion query allocation failed.");
                    return;
                }
            }
            if (!m_context || !runtime->counters.buffer)
            {
                SetFatalExperimentFailure(
                    1504u,
                    L"Performance SG-APT counter source/context is missing.");
                return;
            }

            // Explicitly remove any compute bindings before the copy. The EVENT
            // query is ended immediately after CopyResource, so GetData later
            // proves that this exact staging snapshot is complete before Map.
            N01UnbindCompute(m_context.Get());
            m_context->CopyResource(
                slot.counters.Get(),
                runtime->counters.buffer.Get());
            m_context->End(slot.completionQuery.Get());
            slot.sourceFrame = m_globalFrameIndex;
            slot.ageFrames = 0u;
            slot.issued = true;
            state.writeIndex =
                (state.writeIndex + 1u) %
                static_cast<uint32_t>(state.slots.size());
        }
    }
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
