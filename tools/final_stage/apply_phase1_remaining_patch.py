
from __future__ import annotations

import csv
import shutil
import sys
from pathlib import Path


PHASE = "phase1_pre_novelty_final_adjustment"
MAPPING_VERSION = "final5_v1"

RUN_FLAG_COLUMNS = {
    "enable_budgeted_physics": "INTEGER DEFAULT 0",
    "enable_certificate_render": "INTEGER DEFAULT 0",
    "enable_per_strand_edit": "INTEGER DEFAULT 0",
    "top_level_novelty_mask": "INTEGER DEFAULT 0",
    "phase": "TEXT",
    "novelty_mapping_version": "TEXT",
}

FRAME_PLACEHOLDER_COLUMNS = {
    "top_level_novelty_mask": "INTEGER DEFAULT 0",
    "overflow_count": "INTEGER DEFAULT 0",
    "budget_drop_count": "INTEGER DEFAULT 0",
    "defer_count": "INTEGER DEFAULT 0",
    "microtype_decoded_count": "INTEGER DEFAULT 0",
    "safe_decode_count": "INTEGER DEFAULT 0",
    "false_safe_count": "INTEGER DEFAULT 0",
    "typed_repair_request_count": "INTEGER DEFAULT 0",
    "float_fallback_request_count": "INTEGER DEFAULT 0",
    "reject_payload_count": "INTEGER DEFAULT 0",
    "risky_strand_count": "INTEGER DEFAULT 0",
    "repair_commit_count": "INTEGER DEFAULT 0",
    "repair_reject_count": "INTEGER DEFAULT 0",
    "repair_defer_count": "INTEGER DEFAULT 0",
    "island_candidate_count": "INTEGER DEFAULT 0",
    "physics_fallback_count": "INTEGER DEFAULT 0",
    "render_certificate_count": "INTEGER DEFAULT 0",
    "render_skip_count": "INTEGER DEFAULT 0",
    "render_coarse_opacity_count": "INTEGER DEFAULT 0",
    "render_strand_raster_count": "INTEGER DEFAULT 0",
    "render_high_quality_shadow_count": "INTEGER DEFAULT 0",
    "render_high_quality_specular_count": "INTEGER DEFAULT 0",
    "render_full_quality_count": "INTEGER DEFAULT 0",
    "render_dropped_full_quality_count": "INTEGER DEFAULT 0",
    "p95_shadow_error_bound": "REAL DEFAULT 0",
    "p95_specular_error_bound": "REAL DEFAULT 0",
    "temporal_flicker_score": "REAL DEFAULT 0",
    "edit_operation_count": "INTEGER DEFAULT 0",
    "edit_repair_task_count": "INTEGER DEFAULT 0",
    "edit_island_escalation_count": "INTEGER DEFAULT 0",
    "edit_commit_count": "INTEGER DEFAULT 0",
    "edit_reject_count": "INTEGER DEFAULT 0",
    "edit_preview_full_quality_count": "INTEGER DEFAULT 0",
    "edit_latency_ms": "REAL DEFAULT 0",
    "gpu_resident_task_count": "INTEGER DEFAULT 0",
    "risky_compaction_count": "INTEGER DEFAULT 0",
    "dispatch_indirect_count": "INTEGER DEFAULT 0",
    "draw_indirect_count": "INTEGER DEFAULT 0",
    "delayed_telemetry_read_count": "INTEGER DEFAULT 0",
}

CPP_SAMPLE_NEW_FIELDS = [
    "topLevelNoveltyMask",
    "overflowCount",
    "budgetDropCount",
    "deferCount",
    "microtypeDecodedCount",
    "safeDecodeCount",
    "falseSafeCount",
    "typedRepairRequestCount",
    "floatFallbackRequestCount",
    "rejectPayloadCount",
    "riskyStrandCount",
    "repairCommitCount",
    "repairRejectCount",
    "repairDeferCount",
    "islandCandidateCount",
    "physicsFallbackCount",
    "renderCertificateCount",
    "renderSkipCount",
    "renderCoarseOpacityCount",
    "renderStrandRasterCount",
    "renderHighQualityShadowCount",
    "renderHighQualitySpecularCount",
    "renderFullQualityCount",
    "renderDroppedFullQualityCount",
    "p95ShadowErrorBound",
    "p95SpecularErrorBound",
    "temporalFlickerScore",
    "editOperationCount",
    "editRepairTaskCount",
    "editIslandEscalationCount",
    "editCommitCount",
    "editRejectCount",
    "editPreviewFullQualityCount",
    "editLatencyMs",
    "gpuResidentTaskCount",
    "riskyCompactionCount",
    "dispatchIndirectCount",
    "drawIndirectCount",
    "delayedTelemetryReadCount",
]


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_text_if_changed(path: Path, text: str) -> bool:
    old = read_text(path)
    if old == text:
        return False
    backup = path.with_suffix(path.suffix + ".phase1_remaining.bak")
    if not backup.exists():
        shutil.copy2(path, backup)
    path.write_text(text, encoding="utf-8", newline="")
    return True


def insert_after_once(text: str, marker: str, addition: str, needle: str | None = None) -> str:
    if (needle or addition.strip()) in text:
        return text
    if marker not in text:
        raise RuntimeError(f"marker not found: {marker[:120]!r}")
    return text.replace(marker, marker + addition, 1)


def insert_before_once(text: str, marker: str, addition: str, needle: str | None = None) -> str:
    if (needle or addition.strip()) in text:
        return text
    if marker not in text:
        raise RuntimeError(f"marker not found: {marker[:120]!r}")
    return text.replace(marker, addition + marker, 1)


def replace_once(text: str, old: str, new: str, *, required: bool = True) -> str:
    if old not in text:
        if required:
            raise RuntimeError(f"replace marker not found: {old[:160]!r}")
        return text
    return text.replace(old, new, 1)


def ensure_vcxproj(root: Path) -> list[str]:
    changed = []
    path = root / "HairSystem.vcxproj"
    text = read_text(path)

    compile_marker = '    <ClCompile Include="src\\hairgpu_core\\HairMath.cpp" />\n'
    compile_add = (
        '    <ClCompile Include="src\\hairgpu_core\\GlobalHairBudget.cpp" />\n'
        '    <ClCompile Include="src\\hairgpu_core\\HairNoveltyFlags.cpp" />\n'
        '    <ClCompile Include="src\\hairgpu_core\\HairNoveltyTelemetry.cpp" />\n'
    )
    text = insert_after_once(text, compile_marker, compile_add, 'src\\hairgpu_core\\HairNoveltyFlags.cpp')

    include_marker = '    <ClInclude Include="include\\hairgpu\\HairCache.h" />\n'
    include_add = (
        '    <ClInclude Include="include\\hairgpu\\GlobalHairBudget.h" />\n'
        '    <ClInclude Include="include\\hairgpu\\HairNoveltyFlags.h" />\n'
        '    <ClInclude Include="include\\hairgpu\\HairNoveltyTelemetry.h" />\n'
    )
    text = insert_after_once(text, include_marker, include_add, 'include\\hairgpu\\HairNoveltyFlags.h')

    if write_text_if_changed(path, text):
        changed.append(str(path.relative_to(root)))

    filters = root / "HairSystem.vcxproj.filters"
    if filters.exists():
        f = read_text(filters)
        f_compile_marker = '    <ClCompile Include="src\\hairgpu_core\\HairMath.cpp"><Filter>Source Files\\Core</Filter></ClCompile>\n'
        f_compile_add = (
            '    <ClCompile Include="src\\hairgpu_core\\GlobalHairBudget.cpp"><Filter>Source Files\\Core</Filter></ClCompile>\n'
            '    <ClCompile Include="src\\hairgpu_core\\HairNoveltyFlags.cpp"><Filter>Source Files\\Core</Filter></ClCompile>\n'
            '    <ClCompile Include="src\\hairgpu_core\\HairNoveltyTelemetry.cpp"><Filter>Source Files\\Core</Filter></ClCompile>\n'
        )
        f = insert_after_once(f, f_compile_marker, f_compile_add, 'src\\hairgpu_core\\HairNoveltyFlags.cpp')

        f_include_marker = '    <ClInclude Include="include\\hairgpu\\HairCache.h"><Filter>Header Files\\hairgpu</Filter></ClInclude>\n'
        f_include_add = (
            '    <ClInclude Include="include\\hairgpu\\GlobalHairBudget.h"><Filter>Header Files\\hairgpu</Filter></ClInclude>\n'
            '    <ClInclude Include="include\\hairgpu\\HairNoveltyFlags.h"><Filter>Header Files\\hairgpu</Filter></ClInclude>\n'
            '    <ClInclude Include="include\\hairgpu\\HairNoveltyTelemetry.h"><Filter>Header Files\\hairgpu</Filter></ClInclude>\n'
        )
        f = insert_after_once(f, f_include_marker, f_include_add, 'include\\hairgpu\\HairNoveltyFlags.h')
        if write_text_if_changed(filters, f):
            changed.append(str(filters.relative_to(root)))

    return changed


def ensure_viewer_header(root: Path) -> list[str]:
    changed = []
    path = root / "apps/dx11_viewer/src/GpuHairViewer.h"
    text = read_text(path)

    text = insert_after_once(
        text,
        '#include "hairgpu/ActiveSet.h"\n',
        '#include "hairgpu/GlobalHairBudget.h"\n#include "hairgpu/HairNoveltyFlags.h"\n#include "hairgpu/HairNoveltyTelemetry.h"\n',
        '#include "hairgpu/HairNoveltyTelemetry.h"',
    )

    performance_add = """
            uint32_t topLevelNoveltyMask = 0u;
            uint32_t overflowCount = 0u;
            uint32_t budgetDropCount = 0u;
            uint32_t deferCount = 0u;
            uint32_t microtypeDecodedCount = 0u;
            uint32_t safeDecodeCount = 0u;
            uint32_t falseSafeCount = 0u;
            uint32_t typedRepairRequestCount = 0u;
            uint32_t floatFallbackRequestCount = 0u;
            uint32_t rejectPayloadCount = 0u;
            uint32_t riskyStrandCount = 0u;
            uint32_t repairCommitCount = 0u;
            uint32_t repairRejectCount = 0u;
            uint32_t repairDeferCount = 0u;
            uint32_t islandCandidateCount = 0u;
            uint32_t physicsFallbackCount = 0u;
            uint32_t renderCertificateCount = 0u;
            uint32_t renderSkipCount = 0u;
            uint32_t renderCoarseOpacityCount = 0u;
            uint32_t renderStrandRasterCount = 0u;
            uint32_t renderHighQualityShadowCount = 0u;
            uint32_t renderHighQualitySpecularCount = 0u;
            uint32_t renderFullQualityCount = 0u;
            uint32_t renderDroppedFullQualityCount = 0u;
            float p95ShadowErrorBound = 0.0f;
            float p95SpecularErrorBound = 0.0f;
            float temporalFlickerScore = 0.0f;
            uint32_t editOperationCount = 0u;
            uint32_t editRepairTaskCount = 0u;
            uint32_t editIslandEscalationCount = 0u;
            uint32_t editCommitCount = 0u;
            uint32_t editRejectCount = 0u;
            uint32_t editPreviewFullQualityCount = 0u;
            float editLatencyMs = 0.0f;
            uint32_t gpuResidentTaskCount = 0u;
            uint32_t riskyCompactionCount = 0u;
            uint32_t dispatchIndirectCount = 0u;
            uint32_t drawIndirectCount = 0u;
            uint32_t delayedTelemetryReadCount = 0u;
"""
    text = insert_after_once(text, "            uint32_t measuredFrameIndex = 0u;\n", performance_add, "topLevelNoveltyMask = 0u")

    experiment_add = (
        "            bool enableBudgetedPhysics = false;\n"
        "            bool enableCertificateRender = false;\n"
        "            bool enablePerStrandEdit = false;\n"
    )
    text = insert_after_once(text, "            bool enableMicrotype = false;\n", experiment_add, "enableCertificateRender")

    hooks_add = (
        "        void ResetNoveltyTelemetryForFrame();\n"
        "        void EvaluateMicrotypeCertificates();\n"
        "        void ExecuteBudgetedPhysics();\n"
        "        void ExecuteCertificateAwareRendering();\n"
        "        void ExecutePerStrandEditing();\n"
        "        void ExecuteGpuResidentRuntime();\n"
    )
    text = insert_after_once(text, "        void StepSimulation(float dt);\n", hooks_add, "EvaluateMicrotypeCertificates")

    member_add = (
        "        hairgpu::HairNoveltyFeatureFlags m_noveltyFlags = {};\n"
        "        hairgpu::GlobalHairFrameBudget m_globalHairBudget = {};\n"
        "        hairgpu::HairNoveltyFrameTelemetry m_noveltyTelemetry = {};\n"
    )
    text = insert_before_once(text, "        std::vector<PerformanceSample> m_performanceSamples;\n", member_add, "m_noveltyTelemetry")

    if write_text_if_changed(path, text):
        changed.append(str(path.relative_to(root)))
    return changed


def cpp_csv_columns() -> list[str]:
    return [
        "phase", "noveltyMappingVersion", "topLevelNoveltyMask",
        "enableBudgetedPhysics", "enableCertificateRender", "enablePerStrandEdit",
        "overflowCount", "budgetDropCount", "deferCount",
        "microtypeDecodedCount", "safeDecodeCount", "falseSafeCount",
        "typedRepairRequestCount", "floatFallbackRequestCount", "rejectPayloadCount",
        "riskyStrandCount", "repairCommitCount", "repairRejectCount", "repairDeferCount",
        "islandCandidateCount", "physicsFallbackCount",
        "renderCertificateCount", "renderSkipCount", "renderCoarseOpacityCount",
        "renderStrandRasterCount", "renderHighQualityShadowCount",
        "renderHighQualitySpecularCount", "renderFullQualityCount",
        "renderDroppedFullQualityCount", "p95ShadowErrorBound",
        "p95SpecularErrorBound", "temporalFlickerScore",
        "editOperationCount", "editRepairTaskCount", "editIslandEscalationCount",
        "editCommitCount", "editRejectCount", "editPreviewFullQualityCount",
        "editLatencyMs", "gpuResidentTaskCount", "riskyCompactionCount",
        "dispatchIndirectCount", "drawIndirectCount", "delayedTelemetryReadCount",
    ]


def cpp_csv_row_append() -> str:
    fields = [
        f'"{PHASE}"',
        f'"{MAPPING_VERSION}"',
        "s.topLevelNoveltyMask",
        "(m_experimentCase.enableBudgetedPhysics ? 1 : 0)",
        "(m_experimentCase.enableCertificateRender ? 1 : 0)",
        "(m_experimentCase.enablePerStrandEdit ? 1 : 0)",
    ] + [f"s.{name}" for name in CPP_SAMPLE_NEW_FIELDS if name != "topLevelNoveltyMask"]
    return ''.join([f'\n                << "," << {expr}' for expr in fields])


def ensure_viewer_cpp(root: Path) -> list[str]:
    changed = []
    path = root / "apps/dx11_viewer/src/GpuHairViewer.cpp"
    text = read_text(path)

    text = insert_after_once(
        text,
        '        m_experimentCase.enableMicrotype = EnvironmentValueIsTrue(GetEnvironmentVariableString(L"HAIRGPU_ENABLE_MICROTYPE"));\n',
        '        m_experimentCase.enableBudgetedPhysics = EnvironmentValueIsTrue(GetEnvironmentVariableString(L"HAIRGPU_ENABLE_BUDGETED_PHYSICS"));\n'
        '        m_experimentCase.enableCertificateRender = EnvironmentValueIsTrue(GetEnvironmentVariableString(L"HAIRGPU_ENABLE_CERTIFICATE_RENDER"));\n'
        '        m_experimentCase.enablePerStrandEdit = EnvironmentValueIsTrue(GetEnvironmentVariableString(L"HAIRGPU_ENABLE_PER_STRAND_EDIT"));\n',
        "HAIRGPU_ENABLE_CERTIFICATE_RENDER",
    )
    text = insert_after_once(
        text,
        '        m_experimentCase.settingsHash = GetEnvironmentVariableString(L"HAIRGPU_SETTINGS_HASH");\n',
        """
        m_noveltyFlags = hairgpu::NormalizeNoveltyFlags(
            m_experimentCase.enableMicrotype ? 1u : 0u,
            m_experimentCase.enableBudgetedPhysics ? 1u : 0u,
            m_experimentCase.enableCertificateRender ? 1u : 0u,
            m_experimentCase.enablePerStrandEdit ? 1u : 0u,
            m_experimentCase.enableGpuResident ? 1u : 0u,
            m_experimentCase.enableCBMA ? 1u : 0u,
            m_experimentCase.enablePhysicsIslands ? 1u : 0u,
            m_experimentCase.enableRodRepair ? 1u : 0u,
            m_experimentCase.enableNoNoveltyBaseline ? 1u : 0u);
        m_experimentCase.enableNoNoveltyBaseline = m_noveltyFlags.enableNoNoveltyBaseline != 0u;
        m_experimentCase.enableMicrotype = m_noveltyFlags.enableMicrotype != 0u;
        m_experimentCase.enableBudgetedPhysics = m_noveltyFlags.enableBudgetedPhysics != 0u;
        m_experimentCase.enableCertificateRender = m_noveltyFlags.enableCertificateRender != 0u;
        m_experimentCase.enablePerStrandEdit = m_noveltyFlags.enablePerStrandEdit != 0u;
        m_experimentCase.enableGpuResident = m_noveltyFlags.enableGpuResident != 0u;
        m_experimentCase.enableCBMA = m_noveltyFlags.enableCBMA != 0u;
        m_experimentCase.enablePhysicsIslands = m_noveltyFlags.enablePhysicsIslands != 0u;
        m_experimentCase.enableRodRepair = m_noveltyFlags.enableRodRepair != 0u;
        m_globalHairBudget = hairgpu::BuildGlobalHairFrameBudget(m_noveltyFlags);
""",
        "BuildGlobalHairFrameBudget(m_noveltyFlags)",
    )

    text = insert_after_once(
        text,
        "        m_performanceSamples.clear();\n",
        "        m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();\n",
        "m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();",
    )

    old_step = """    void GpuHairViewer::StepSingleFrame()
    {
        LARGE_INTEGER simBegin = {};
        LARGE_INTEGER simEnd = {};
        QueryPerformanceCounter(&simBegin);
        StepSimulation(m_simConfig.fixedTimeStep);
        QueryPerformanceCounter(&simEnd);
        m_elapsedSeconds += m_simConfig.fixedTimeStep;
        m_lastSimulationStepMilliseconds = static_cast<float>(simEnd.QuadPart - simBegin.QuadPart) * 1000.0f / static_cast<float>(m_qpcFrequency.QuadPart);

        PerformanceSample sample = BuildPerformanceSample(m_simConfig.fixedTimeStep, (m_simConfig.fixedTimeStep > 1.0e-6f) ? (1.0f / m_simConfig.fixedTimeStep) : 0.0f);
        sample.globalFrameIndex = m_globalFrameIndex++;
        sample.measuredFrameIndex = m_measuredFrameIndex++;
        m_performanceSamples.push_back(sample);
        if (m_performanceSamples.size() > 12000)
        {
            m_performanceSamples.erase(m_performanceSamples.begin(), m_performanceSamples.begin() + static_cast<std::ptrdiff_t>(m_performanceSamples.size() - 12000));
        }
        UpdateStatsLabel();
    }

"""
    new_step = """    void GpuHairViewer::StepSingleFrame()
    {
        ResetNoveltyTelemetryForFrame();

        LARGE_INTEGER simBegin = {};
        LARGE_INTEGER simEnd = {};
        QueryPerformanceCounter(&simBegin);

        // Phase1 no-op hook order. These functions intentionally do not change
        // simulation/render output in Phase1; they only establish future extension
        // points and zero-valued telemetry placeholders.
        ExecutePerStrandEditing();
        EvaluateMicrotypeCertificates();
        ExecuteBudgetedPhysics();
        ExecuteGpuResidentRuntime();

        StepSimulation(m_simConfig.fixedTimeStep);

        ExecuteCertificateAwareRendering();

        QueryPerformanceCounter(&simEnd);
        m_elapsedSeconds += m_simConfig.fixedTimeStep;
        m_lastSimulationStepMilliseconds = static_cast<float>(simEnd.QuadPart - simBegin.QuadPart) * 1000.0f / static_cast<float>(m_qpcFrequency.QuadPart);

        PerformanceSample sample = BuildPerformanceSample(m_simConfig.fixedTimeStep, (m_simConfig.fixedTimeStep > 1.0e-6f) ? (1.0f / m_simConfig.fixedTimeStep) : 0.0f);
        sample.globalFrameIndex = m_globalFrameIndex++;
        sample.measuredFrameIndex = m_measuredFrameIndex++;
        m_performanceSamples.push_back(sample);
        if (m_performanceSamples.size() > 12000)
        {
            m_performanceSamples.erase(m_performanceSamples.begin(), m_performanceSamples.begin() + static_cast<std::ptrdiff_t>(m_performanceSamples.size() - 12000));
        }
        UpdateStatsLabel();
    }

"""
    if "void GpuHairViewer::ResetNoveltyTelemetryForFrame()" not in text:
        text = replace_once(text, old_step, new_step)

        hook_defs = """    void GpuHairViewer::ResetNoveltyTelemetryForFrame()
    {
        m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();
        m_noveltyTelemetry.frameIndex = m_globalFrameIndex;
        m_noveltyTelemetry.measuredFrameIndex = m_measuredFrameIndex;
        m_noveltyTelemetry.topLevelNoveltyMask = hairgpu::BuildTopLevelNoveltyMask(m_noveltyFlags);
    }

    void GpuHairViewer::EvaluateMicrotypeCertificates()
    {
        if (!m_noveltyFlags.enableMicrotype)
        {
            return;
        }
        m_noveltyTelemetry.microtypeDecodedCount = 0u;
        m_noveltyTelemetry.safeDecodeCount = 0u;
        m_noveltyTelemetry.certificateFailureCount = 0u;
        m_noveltyTelemetry.falseSafeCount = 0u;
        m_noveltyTelemetry.typedRepairRequestCount = 0u;
        m_noveltyTelemetry.floatFallbackRequestCount = 0u;
        m_noveltyTelemetry.rejectPayloadCount = 0u;
    }

    void GpuHairViewer::ExecuteBudgetedPhysics()
    {
        if (!m_noveltyFlags.enableBudgetedPhysics)
        {
            return;
        }
        m_noveltyTelemetry.riskyStrandCount = 0u;
        m_noveltyTelemetry.repairTaskCount = 0u;
        m_noveltyTelemetry.repairCommitCount = 0u;
        m_noveltyTelemetry.repairRejectCount = 0u;
        m_noveltyTelemetry.repairDeferCount = 0u;
        m_noveltyTelemetry.islandCandidateCount = 0u;
        m_noveltyTelemetry.islandCount = 0u;
        m_noveltyTelemetry.islandMemberCount = 0u;
        m_noveltyTelemetry.islandCommitCount = 0u;
        m_noveltyTelemetry.islandRejectCount = 0u;
        m_noveltyTelemetry.physicsFallbackCount = 0u;
        m_noveltyTelemetry.maxPenetrationMm = 0.0f;
    }

    void GpuHairViewer::ExecuteCertificateAwareRendering()
    {
        if (!m_noveltyFlags.enableCertificateRender)
        {
            return;
        }
        m_noveltyTelemetry.renderCertificateCount = 0u;
        m_noveltyTelemetry.renderSkipCount = 0u;
        m_noveltyTelemetry.renderCoarseOpacityCount = 0u;
        m_noveltyTelemetry.renderStrandRasterCount = 0u;
        m_noveltyTelemetry.renderHighQualityShadowCount = 0u;
        m_noveltyTelemetry.renderHighQualitySpecularCount = 0u;
        m_noveltyTelemetry.renderFullQualityCount = 0u;
        m_noveltyTelemetry.renderDroppedFullQualityCount = 0u;
        m_noveltyTelemetry.p95ShadowErrorBound = 0.0f;
        m_noveltyTelemetry.p95SpecularErrorBound = 0.0f;
        m_noveltyTelemetry.temporalFlickerScore = 0.0f;
    }

    void GpuHairViewer::ExecutePerStrandEditing()
    {
        if (!m_noveltyFlags.enablePerStrandEdit)
        {
            return;
        }
        m_noveltyTelemetry.editOperationCount = 0u;
        m_noveltyTelemetry.editRepairTaskCount = 0u;
        m_noveltyTelemetry.editIslandEscalationCount = 0u;
        m_noveltyTelemetry.editCommitCount = 0u;
        m_noveltyTelemetry.editRejectCount = 0u;
        m_noveltyTelemetry.editPreviewFullQualityCount = 0u;
        m_noveltyTelemetry.editLatencyMs = 0.0f;
    }

    void GpuHairViewer::ExecuteGpuResidentRuntime()
    {
        if (!m_noveltyFlags.enableGpuResident)
        {
            return;
        }
        m_noveltyTelemetry.gpuResidentTaskCount = 0u;
        m_noveltyTelemetry.riskyCompactionCount = 0u;
        m_noveltyTelemetry.dispatchIndirectCount = 0u;
        m_noveltyTelemetry.drawIndirectCount = 0u;
        m_noveltyTelemetry.delayedTelemetryReadCount = 0u;
    }

"""
        text = insert_before_once(text, "    Vec3 GpuHairViewer::ApplySampleScalpSkinning", hook_defs, "ExecuteCertificateAwareRendering()")

    assignment_lines = """
        sample.topLevelNoveltyMask = m_noveltyTelemetry.topLevelNoveltyMask;
        sample.overflowCount = m_noveltyTelemetry.overflowCount;
        sample.budgetDropCount = m_noveltyTelemetry.budgetDropCount;
        sample.deferCount = m_noveltyTelemetry.deferCount;
        sample.microtypeDecodedCount = m_noveltyTelemetry.microtypeDecodedCount;
        sample.safeDecodeCount = m_noveltyTelemetry.safeDecodeCount;
        sample.falseSafeCount = m_noveltyTelemetry.falseSafeCount;
        sample.typedRepairRequestCount = m_noveltyTelemetry.typedRepairRequestCount;
        sample.floatFallbackRequestCount = m_noveltyTelemetry.floatFallbackRequestCount;
        sample.rejectPayloadCount = m_noveltyTelemetry.rejectPayloadCount;
        sample.riskyStrandCount = m_noveltyTelemetry.riskyStrandCount;
        sample.repairCommitCount = m_noveltyTelemetry.repairCommitCount;
        sample.repairRejectCount = m_noveltyTelemetry.repairRejectCount;
        sample.repairDeferCount = m_noveltyTelemetry.repairDeferCount;
        sample.islandCandidateCount = m_noveltyTelemetry.islandCandidateCount;
        sample.physicsFallbackCount = m_noveltyTelemetry.physicsFallbackCount;
        sample.renderCertificateCount = m_noveltyTelemetry.renderCertificateCount;
        sample.renderSkipCount = m_noveltyTelemetry.renderSkipCount;
        sample.renderCoarseOpacityCount = m_noveltyTelemetry.renderCoarseOpacityCount;
        sample.renderStrandRasterCount = m_noveltyTelemetry.renderStrandRasterCount;
        sample.renderHighQualityShadowCount = m_noveltyTelemetry.renderHighQualityShadowCount;
        sample.renderHighQualitySpecularCount = m_noveltyTelemetry.renderHighQualitySpecularCount;
        sample.renderFullQualityCount = m_noveltyTelemetry.renderFullQualityCount;
        sample.renderDroppedFullQualityCount = m_noveltyTelemetry.renderDroppedFullQualityCount;
        sample.p95ShadowErrorBound = m_noveltyTelemetry.p95ShadowErrorBound;
        sample.p95SpecularErrorBound = m_noveltyTelemetry.p95SpecularErrorBound;
        sample.temporalFlickerScore = m_noveltyTelemetry.temporalFlickerScore;
        sample.editOperationCount = m_noveltyTelemetry.editOperationCount;
        sample.editRepairTaskCount = m_noveltyTelemetry.editRepairTaskCount;
        sample.editIslandEscalationCount = m_noveltyTelemetry.editIslandEscalationCount;
        sample.editCommitCount = m_noveltyTelemetry.editCommitCount;
        sample.editRejectCount = m_noveltyTelemetry.editRejectCount;
        sample.editPreviewFullQualityCount = m_noveltyTelemetry.editPreviewFullQualityCount;
        sample.editLatencyMs = m_noveltyTelemetry.editLatencyMs;
        sample.gpuResidentTaskCount = m_noveltyTelemetry.gpuResidentTaskCount;
        sample.riskyCompactionCount = m_noveltyTelemetry.riskyCompactionCount;
        sample.dispatchIndirectCount = m_noveltyTelemetry.dispatchIndirectCount;
        sample.drawIndirectCount = m_noveltyTelemetry.drawIndirectCount;
        sample.delayedTelemetryReadCount = m_noveltyTelemetry.delayedTelemetryReadCount;
"""
    text = insert_before_once(text, "        return sample;\n", assignment_lines, "sample.renderCertificateCount = m_noveltyTelemetry.renderCertificateCount;")

    text = replace_once(
        text,
        "enableNoNoveltyBaseline,enableMicrotype,enableCBMA,enablePhysicsIslands,enableRodRepair,enableGpuResident,",
        "enableNoNoveltyBaseline,enableMicrotype,enableBudgetedPhysics,enableCertificateRender,enablePerStrandEdit,enableCBMA,enablePhysicsIslands,enableRodRepair,enableGpuResident,",
        required=False,
    )
    text = insert_after_once(
        text,
        '                << (m_experimentCase.enableMicrotype ? 1 : 0) << ","\n',
        '                << (m_experimentCase.enableBudgetedPhysics ? 1 : 0) << ","\n'
        '                << (m_experimentCase.enableCertificateRender ? 1 : 0) << ","\n'
        '                << (m_experimentCase.enablePerStrandEdit ? 1 : 0) << ","\n',
        "m_experimentCase.enableCertificateRender ? 1 : 0",
    )

    if "phase,noveltyMappingVersion,topLevelNoveltyMask" not in text:
        text = text.replace(
            "meanDecodeErrorMm,maxPenetrationMm\\n\";",
            "meanDecodeErrorMm,maxPenetrationMm," + ",".join(cpp_csv_columns()) + "\\n\";",
            1,
        )
        text = text.replace(
            '                << s.maxPenetrationMm << "\\n";',
            '                << s.maxPenetrationMm' + cpp_csv_row_append() + ' << "\\n";',
            1,
        )

    text = insert_after_once(
        text,
        '                << "\\"enable_no_novelty_baseline\\":" << (m_experimentCase.enableNoNoveltyBaseline ? "true" : "false") << ","\n',
        f'                << "\\"phase\\":\\"{PHASE}\\","\n'
        f'                << "\\"novelty_mapping_version\\":\\"{MAPPING_VERSION}\\","\n'
        '                << "\\"top_level_novelty_mask\\":" << s.topLevelNoveltyMask << ","\n'
        '                << "\\"enable_microtype\\":" << (m_experimentCase.enableMicrotype ? "true" : "false") << ","\n'
        '                << "\\"enable_budgeted_physics\\":" << (m_experimentCase.enableBudgetedPhysics ? "true" : "false") << ","\n'
        '                << "\\"enable_certificate_render\\":" << (m_experimentCase.enableCertificateRender ? "true" : "false") << ","\n'
        '                << "\\"enable_per_strand_edit\\":" << (m_experimentCase.enablePerStrandEdit ? "true" : "false") << ","\n'
        '                << "\\"enable_gpu_resident\\":" << (m_experimentCase.enableGpuResident ? "true" : "false") << ","\n',
        '\\"enable_certificate_render\\"',
    )
    if '\\"render_certificate_count\\"' not in text:
        text = text.replace(
            '                << "\\"mean_decode_error_mm\\":" << s.meanDecodeErrorMm\n',
            '                << "\\"mean_decode_error_mm\\":" << s.meanDecodeErrorMm << ","\n'
            '                << "\\"overflow_count\\":" << s.overflowCount << ","\n'
            '                << "\\"budget_drop_count\\":" << s.budgetDropCount << ","\n'
            '                << "\\"defer_count\\":" << s.deferCount << ","\n'
            '                << "\\"microtype_decoded_count\\":" << s.microtypeDecodedCount << ","\n'
            '                << "\\"safe_decode_count\\":" << s.safeDecodeCount << ","\n'
            '                << "\\"false_safe_count\\":" << s.falseSafeCount << ","\n'
            '                << "\\"typed_repair_request_count\\":" << s.typedRepairRequestCount << ","\n'
            '                << "\\"float_fallback_request_count\\":" << s.floatFallbackRequestCount << ","\n'
            '                << "\\"reject_payload_count\\":" << s.rejectPayloadCount << ","\n'
            '                << "\\"risky_strand_count\\":" << s.riskyStrandCount << ","\n'
            '                << "\\"island_candidate_count\\":" << s.islandCandidateCount << ","\n'
            '                << "\\"physics_fallback_count\\":" << s.physicsFallbackCount << ","\n'
            '                << "\\"render_certificate_count\\":" << s.renderCertificateCount << ","\n'
            '                << "\\"render_skip_count\\":" << s.renderSkipCount << ","\n'
            '                << "\\"render_coarse_opacity_count\\":" << s.renderCoarseOpacityCount << ","\n'
            '                << "\\"render_strand_raster_count\\":" << s.renderStrandRasterCount << ","\n'
            '                << "\\"render_high_quality_shadow_count\\":" << s.renderHighQualityShadowCount << ","\n'
            '                << "\\"render_high_quality_specular_count\\":" << s.renderHighQualitySpecularCount << ","\n'
            '                << "\\"render_full_quality_count\\":" << s.renderFullQualityCount << ","\n'
            '                << "\\"render_dropped_full_quality_count\\":" << s.renderDroppedFullQualityCount << ","\n'
            '                << "\\"p95_shadow_error_bound\\":" << s.p95ShadowErrorBound << ","\n'
            '                << "\\"p95_specular_error_bound\\":" << s.p95SpecularErrorBound << ","\n'
            '                << "\\"temporal_flicker_score\\":" << s.temporalFlickerScore << ","\n'
            '                << "\\"edit_operation_count\\":" << s.editOperationCount << ","\n'
            '                << "\\"edit_repair_task_count\\":" << s.editRepairTaskCount << ","\n'
            '                << "\\"edit_island_escalation_count\\":" << s.editIslandEscalationCount << ","\n'
            '                << "\\"edit_commit_count\\":" << s.editCommitCount << ","\n'
            '                << "\\"edit_reject_count\\":" << s.editRejectCount << ","\n'
            '                << "\\"edit_preview_full_quality_count\\":" << s.editPreviewFullQualityCount << ","\n'
            '                << "\\"edit_latency_ms\\":" << s.editLatencyMs << ","\n'
            '                << "\\"gpu_resident_task_count\\":" << s.gpuResidentTaskCount << ","\n'
            '                << "\\"risky_compaction_count\\":" << s.riskyCompactionCount << ","\n'
            '                << "\\"dispatch_indirect_count\\":" << s.dispatchIndirectCount << ","\n'
            '                << "\\"draw_indirect_count\\":" << s.drawIndirectCount << ","\n'
            '                << "\\"delayed_telemetry_read_count\\":" << s.delayedTelemetryReadCount\n',
            1,
        )

    out_old = """        out << "  \\"settings_hash\\": \\"" << JsonEscape(WideToUtf8(m_experimentCase.settingsHash)) << "\\",\\n";
        out << "  \\"feature_flags\\": {\\n";
        out << "    \\"enableNoNoveltyBaseline\\": " << (m_experimentCase.enableNoNoveltyBaseline ? "true" : "false") << ",\\n";
        out << "    \\"enableMicrotype\\": " << (m_experimentCase.enableMicrotype ? "true" : "false") << ",\\n";
        out << "    \\"enableCBMA\\": " << (m_experimentCase.enableCBMA ? "true" : "false") << ",\\n";
        out << "    \\"enablePhysicsIslands\\": " << (m_experimentCase.enablePhysicsIslands ? "true" : "false") << ",\\n";
        out << "    \\"enableRodRepair\\": " << (m_experimentCase.enableRodRepair ? "true" : "false") << ",\\n";
        out << "    \\"enableGpuResident\\": " << (m_experimentCase.enableGpuResident ? "true" : "false") << "\\n";
        out << "  }\\n";
"""
    out_new = f"""        out << "  \\"settings_hash\\": \\"" << JsonEscape(WideToUtf8(m_experimentCase.settingsHash)) << "\\",\\n";
        out << "  \\"phase\\": \\"{PHASE}\\",\\n";
        out << "  \\"novelty_mapping_version\\": \\"{MAPPING_VERSION}\\",\\n";
        out << "  \\"top_level_novelty_mask\\": " << hairgpu::BuildTopLevelNoveltyMask(m_noveltyFlags) << ",\\n";
        out << "  \\"top_level_novelty_bits\\": \\"" << hairgpu::BuildTopLevelNoveltyBits(m_noveltyFlags) << "\\",\\n";
        out << "  \\"feature_flags\\": {{\\n";
        out << "    \\"enableNoNoveltyBaseline\\": " << (m_experimentCase.enableNoNoveltyBaseline ? "true" : "false") << ",\\n";
        out << "    \\"enableMicrotype\\": " << (m_experimentCase.enableMicrotype ? "true" : "false") << ",\\n";
        out << "    \\"enableBudgetedPhysics\\": " << (m_experimentCase.enableBudgetedPhysics ? "true" : "false") << ",\\n";
        out << "    \\"enableCertificateRender\\": " << (m_experimentCase.enableCertificateRender ? "true" : "false") << ",\\n";
        out << "    \\"enablePerStrandEdit\\": " << (m_experimentCase.enablePerStrandEdit ? "true" : "false") << ",\\n";
        out << "    \\"enableCBMA\\": " << (m_experimentCase.enableCBMA ? "true" : "false") << ",\\n";
        out << "    \\"enablePhysicsIslands\\": " << (m_experimentCase.enablePhysicsIslands ? "true" : "false") << ",\\n";
        out << "    \\"enableRodRepair\\": " << (m_experimentCase.enableRodRepair ? "true" : "false") << ",\\n";
        out << "    \\"enableGpuResident\\": " << (m_experimentCase.enableGpuResident ? "true" : "false") << "\\n";
        out << "  }}\\n";
"""
    text = replace_once(text, out_old, out_new, required=False)

    json_old = """        json << "  \\"settings_hash\\": \\"" << JsonEscape(WideToUtf8(m_experimentCase.settingsHash)) << "\\",\\n";
        json << "  \\"feature_flags\\": {\\n";
        json << "    \\"enableNoNoveltyBaseline\\": " << (m_experimentCase.enableNoNoveltyBaseline ? "true" : "false") << ",\\n";
        json << "    \\"enableMicrotype\\": " << (m_experimentCase.enableMicrotype ? "true" : "false") << ",\\n";
        json << "    \\"enableCBMA\\": " << (m_experimentCase.enableCBMA ? "true" : "false") << ",\\n";
        json << "    \\"enablePhysicsIslands\\": " << (m_experimentCase.enablePhysicsIslands ? "true" : "false") << ",\\n";
        json << "    \\"enableRodRepair\\": " << (m_experimentCase.enableRodRepair ? "true" : "false") << ",\\n";
        json << "    \\"enableGpuResident\\": " << (m_experimentCase.enableGpuResident ? "true" : "false") << "\\n";
        json << "  },\\n";
"""
    json_new = f"""        json << "  \\"settings_hash\\": \\"" << JsonEscape(WideToUtf8(m_experimentCase.settingsHash)) << "\\",\\n";
        json << "  \\"phase\\": \\"{PHASE}\\",\\n";
        json << "  \\"novelty_mapping_version\\": \\"{MAPPING_VERSION}\\",\\n";
        json << "  \\"top_level_novelty_mask\\": " << hairgpu::BuildTopLevelNoveltyMask(m_noveltyFlags) << ",\\n";
        json << "  \\"top_level_novelty_bits\\": \\"" << hairgpu::BuildTopLevelNoveltyBits(m_noveltyFlags) << "\\",\\n";
        json << "  \\"feature_flags\\": {{\\n";
        json << "    \\"enableNoNoveltyBaseline\\": " << (m_experimentCase.enableNoNoveltyBaseline ? "true" : "false") << ",\\n";
        json << "    \\"enableMicrotype\\": " << (m_experimentCase.enableMicrotype ? "true" : "false") << ",\\n";
        json << "    \\"enableBudgetedPhysics\\": " << (m_experimentCase.enableBudgetedPhysics ? "true" : "false") << ",\\n";
        json << "    \\"enableCertificateRender\\": " << (m_experimentCase.enableCertificateRender ? "true" : "false") << ",\\n";
        json << "    \\"enablePerStrandEdit\\": " << (m_experimentCase.enablePerStrandEdit ? "true" : "false") << ",\\n";
        json << "    \\"enableCBMA\\": " << (m_experimentCase.enableCBMA ? "true" : "false") << ",\\n";
        json << "    \\"enablePhysicsIslands\\": " << (m_experimentCase.enablePhysicsIslands ? "true" : "false") << ",\\n";
        json << "    \\"enableRodRepair\\": " << (m_experimentCase.enableRodRepair ? "true" : "false") << ",\\n";
        json << "    \\"enableGpuResident\\": " << (m_experimentCase.enableGpuResident ? "true" : "false") << "\\n";
        json << "  }},\\n";
"""
    text = replace_once(text, json_old, json_new, required=False)

    if write_text_if_changed(path, text):
        changed.append(str(path.relative_to(root)))
    return changed


def ensure_runner(root: Path) -> list[str]:
    changed = []
    path = root / "tools/final_stage/hairgpu_final_run.py"
    text = read_text(path)
    text = insert_after_once(
        text,
        '        env["HAIRGPU_ENABLE_MICROTYPE"] = "1" if case.get("enableMicrotype") else "0"\n',
        '        env["HAIRGPU_ENABLE_BUDGETED_PHYSICS"] = "1" if case.get("enableBudgetedPhysics") else "0"\n'
        '        env["HAIRGPU_ENABLE_CERTIFICATE_RENDER"] = "1" if case.get("enableCertificateRender") else "0"\n'
        '        env["HAIRGPU_ENABLE_PER_STRAND_EDIT"] = "1" if case.get("enablePerStrandEdit") else "0"\n',
        "HAIRGPU_ENABLE_CERTIFICATE_RENDER",
    )
    if write_text_if_changed(path, text):
        changed.append(str(path.relative_to(root)))
    return changed


def ensure_schema(root: Path) -> list[str]:
    changed = []
    path = root / "tools/final_stage/schema.sql"
    text = read_text(path)

    text = insert_after_once(
        text,
        "    enable_microtype INTEGER DEFAULT 0,\n",
        "    enable_budgeted_physics INTEGER DEFAULT 0,\n"
        "    enable_certificate_render INTEGER DEFAULT 0,\n"
        "    enable_per_strand_edit INTEGER DEFAULT 0,\n",
        "enable_certificate_render INTEGER DEFAULT 0",
    )
    text = insert_after_once(
        text,
        "    enable_gpu_resident INTEGER DEFAULT 0,\n",
        "    top_level_novelty_mask INTEGER DEFAULT 0,\n"
        "    phase TEXT,\n"
        "    novelty_mapping_version TEXT,\n",
        "novelty_mapping_version TEXT",
    )

    text = insert_after_once(
        text,
        "    measured_frame_index INTEGER,\n",
        "    top_level_novelty_mask INTEGER DEFAULT 0,\n"
        "    overflow_count INTEGER DEFAULT 0,\n"
        "    budget_drop_count INTEGER DEFAULT 0,\n"
        "    defer_count INTEGER DEFAULT 0,\n"
        "    microtype_decoded_count INTEGER DEFAULT 0,\n"
        "    safe_decode_count INTEGER DEFAULT 0,\n"
        "    false_safe_count INTEGER DEFAULT 0,\n"
        "    typed_repair_request_count INTEGER DEFAULT 0,\n"
        "    float_fallback_request_count INTEGER DEFAULT 0,\n"
        "    reject_payload_count INTEGER DEFAULT 0,\n"
        "    risky_strand_count INTEGER DEFAULT 0,\n"
        "    island_candidate_count INTEGER DEFAULT 0,\n"
        "    physics_fallback_count INTEGER DEFAULT 0,\n"
        "    render_certificate_count INTEGER DEFAULT 0,\n"
        "    render_skip_count INTEGER DEFAULT 0,\n"
        "    render_coarse_opacity_count INTEGER DEFAULT 0,\n"
        "    render_strand_raster_count INTEGER DEFAULT 0,\n"
        "    render_high_quality_shadow_count INTEGER DEFAULT 0,\n"
        "    render_high_quality_specular_count INTEGER DEFAULT 0,\n"
        "    render_full_quality_count INTEGER DEFAULT 0,\n"
        "    render_dropped_full_quality_count INTEGER DEFAULT 0,\n"
        "    p95_shadow_error_bound REAL DEFAULT 0,\n"
        "    p95_specular_error_bound REAL DEFAULT 0,\n"
        "    temporal_flicker_score REAL DEFAULT 0,\n"
        "    edit_operation_count INTEGER DEFAULT 0,\n"
        "    edit_repair_task_count INTEGER DEFAULT 0,\n"
        "    edit_island_escalation_count INTEGER DEFAULT 0,\n"
        "    edit_commit_count INTEGER DEFAULT 0,\n"
        "    edit_reject_count INTEGER DEFAULT 0,\n"
        "    edit_preview_full_quality_count INTEGER DEFAULT 0,\n"
        "    edit_latency_ms REAL DEFAULT 0,\n"
        "    gpu_resident_task_count INTEGER DEFAULT 0,\n"
        "    risky_compaction_count INTEGER DEFAULT 0,\n"
        "    dispatch_indirect_count INTEGER DEFAULT 0,\n"
        "    draw_indirect_count INTEGER DEFAULT 0,\n"
        "    delayed_telemetry_read_count INTEGER DEFAULT 0,\n",
        "render_certificate_count INTEGER DEFAULT 0",
    )

    if write_text_if_changed(path, text):
        changed.append(str(path.relative_to(root)))
    return changed


def ensure_db_importer(root: Path) -> list[str]:
    changed = []
    path = root / "tools/final_stage/hairgpu_final_db.py"
    text = read_text(path)

    text = insert_after_once(
        text,
        "    enable_microtype: bool = False\n",
        "    enable_budgeted_physics: bool = False\n"
        "    enable_certificate_render: bool = False\n"
        "    enable_per_strand_edit: bool = False\n"
        "    top_level_novelty_mask: int = 0\n"
        "    phase: str = \"phase1_pre_novelty_final_adjustment\"\n"
        "    novelty_mapping_version: str = \"final5_v1\"\n",
        "enable_certificate_render: bool = False",
    )

    text = insert_after_once(
        text,
        '            "enable_microtype": "INTEGER DEFAULT 0",\n',
        '            "enable_budgeted_physics": "INTEGER DEFAULT 0",\n'
        '            "enable_certificate_render": "INTEGER DEFAULT 0",\n'
        '            "enable_per_strand_edit": "INTEGER DEFAULT 0",\n'
        '            "top_level_novelty_mask": "INTEGER DEFAULT 0",\n'
        '            "phase": "TEXT",\n'
        '            "novelty_mapping_version": "TEXT",\n',
        '"enable_certificate_render": "INTEGER DEFAULT 0"',
    )

    frame_ensure = ''.join(f'            "{name}": "{ddl}",\n' for name, ddl in FRAME_PLACEHOLDER_COLUMNS.items())
    text = insert_after_once(
        text,
        '            "measured_frame_index": "INTEGER",\n',
        frame_ensure,
        '"render_certificate_count": "INTEGER DEFAULT 0"',
    )

    text = insert_after_once(
        text,
        '        settings_hash = str(summary.get("settings_hash", ""))\n',
        f'        phase = str(summary.get("phase", metadata.phase or "{PHASE}"))\n'
        f'        novelty_mapping_version = str(summary.get("novelty_mapping_version", metadata.novelty_mapping_version or "{MAPPING_VERSION}"))\n'
        '        top_level_novelty_mask = int(summary.get("top_level_novelty_mask", summary.get("topLevelNoveltyMask", metadata.top_level_novelty_mask or 0)) or 0)\n',
        "top_level_novelty_mask = int(summary.get",
    )

    update_run_code = """
        self.connection.execute(
            \"\"\"
            UPDATE runtime_runs
            SET enable_budgeted_physics = ?,
                enable_certificate_render = ?,
                enable_per_strand_edit = ?,
                top_level_novelty_mask = ?,
                phase = ?,
                novelty_mapping_version = ?
            WHERE run_id = ?
            \"\"\",
            (
                1 if flags.get("enableBudgetedPhysics", metadata.enable_budgeted_physics) else 0,
                1 if flags.get("enableCertificateRender", metadata.enable_certificate_render) else 0,
                1 if flags.get("enablePerStrandEdit", metadata.enable_per_strand_edit) else 0,
                top_level_novelty_mask,
                phase,
                novelty_mapping_version,
                run_id,
            ),
        )
"""
    text = insert_after_once(
        text,
        "        run_id = int(cursor.lastrowid)\n",
        update_run_code,
        "UPDATE runtime_runs\n            SET enable_budgeted_physics",
    )

    update_cols = list(FRAME_PLACEHOLDER_COLUMNS.keys())
    camel = {
        "top_level_novelty_mask": "topLevelNoveltyMask",
        "overflow_count": "overflowCount",
        "budget_drop_count": "budgetDropCount",
        "defer_count": "deferCount",
        "microtype_decoded_count": "microtypeDecodedCount",
        "safe_decode_count": "safeDecodeCount",
        "false_safe_count": "falseSafeCount",
        "typed_repair_request_count": "typedRepairRequestCount",
        "float_fallback_request_count": "floatFallbackRequestCount",
        "reject_payload_count": "rejectPayloadCount",
        "risky_strand_count": "riskyStrandCount",
        "repair_commit_count": "repairCommitCount",
        "repair_reject_count": "repairRejectCount",
        "repair_defer_count": "repairDeferCount",
        "island_candidate_count": "islandCandidateCount",
        "physics_fallback_count": "physicsFallbackCount",
        "render_certificate_count": "renderCertificateCount",
        "render_skip_count": "renderSkipCount",
        "render_coarse_opacity_count": "renderCoarseOpacityCount",
        "render_strand_raster_count": "renderStrandRasterCount",
        "render_high_quality_shadow_count": "renderHighQualityShadowCount",
        "render_high_quality_specular_count": "renderHighQualitySpecularCount",
        "render_full_quality_count": "renderFullQualityCount",
        "render_dropped_full_quality_count": "renderDroppedFullQualityCount",
        "p95_shadow_error_bound": "p95ShadowErrorBound",
        "p95_specular_error_bound": "p95SpecularErrorBound",
        "temporal_flicker_score": "temporalFlickerScore",
        "edit_operation_count": "editOperationCount",
        "edit_repair_task_count": "editRepairTaskCount",
        "edit_island_escalation_count": "editIslandEscalationCount",
        "edit_commit_count": "editCommitCount",
        "edit_reject_count": "editRejectCount",
        "edit_preview_full_quality_count": "editPreviewFullQualityCount",
        "edit_latency_ms": "editLatencyMs",
        "gpu_resident_task_count": "gpuResidentTaskCount",
        "risky_compaction_count": "riskyCompactionCount",
        "dispatch_indirect_count": "dispatchIndirectCount",
        "draw_indirect_count": "drawIndirectCount",
        "delayed_telemetry_read_count": "delayedTelemetryReadCount",
    }

    row_values = []
    for c in update_cols:
        alt = camel[c]
        if any(s in c for s in ["bound", "score", "latency"]):
            row_values.append(f'                        float(self._pick_csv_value(row, "{alt}", "{c}", default="0")),')
        else:
            row_values.append(f'                        int(float(self._pick_csv_value(row, "{alt}", "{c}", default="0"))),')
    row_values.append('                        run_id,')
    row_values.append('                        int(float(self._pick_csv_value(row, "sampleIndex", "sample_index", default="0"))),')
    set_clause = ",\n                ".join(f"{c} = ?" for c in update_cols)
    placeholders = "\n".join(row_values)
    supplemental_code = f"""
        with Path(frame_csv_path).open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            phase1_rows = [
                (
{placeholders}
                )
                for row in reader
            ]
        if phase1_rows:
            self.connection.executemany(
                \"\"\"
                UPDATE runtime_frame_metrics
                SET {set_clause}
                WHERE run_id = ? AND sample_index = ?
                \"\"\",
                phase1_rows,
            )
"""
    if "phase1_rows" not in text:
        text = text.replace("        self.connection.commit()\n\n        avg_frame_ms =", supplemental_code + "        self.connection.commit()\n\n        avg_frame_ms =", 1)

    if write_text_if_changed(path, text):
        changed.append(str(path.relative_to(root)))
    return changed


def write_additive_sql(root: Path) -> str:
    path = root / "tools/final_stage/schema_phase1_remaining_additive.sql"
    lines = [
        "-- Phase1 remaining additive schema update for existing databases.",
        "-- The Python importer also adds these columns automatically.",
        "",
    ]
    for name, ddl in RUN_FLAG_COLUMNS.items():
        lines.append(f"ALTER TABLE runtime_runs ADD COLUMN {name} {ddl};")
    lines.append("")
    for name, ddl in FRAME_PLACEHOLDER_COLUMNS.items():
        lines.append(f"ALTER TABLE runtime_frame_metrics ADD COLUMN {name} {ddl};")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return str(path.relative_to(root))


def write_notes(root: Path) -> str:
    path = root / "docs/PHASE1_61_16_REMAINING_PATCH_NOTES.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"""# Phase1 remaining patch for Novelty 61.16

Apply from the repository root:

```bat
python tools\\final_stage\\apply_phase1_remaining_patch.py
```

This patch fills the remaining Phase1 scaffold gaps:

- registers Phase1 core source/header files in `HairSystem.vcxproj` and `.filters`
- connects `HairNoveltyFlags`, `HairNoveltyTelemetry`, and `GlobalHairBudget` to `GpuHairViewer`
- adds formal 01-05 env flags:
  - `HAIRGPU_ENABLE_BUDGETED_PHYSICS`
  - `HAIRGPU_ENABLE_CERTIFICATE_RENDER`
  - `HAIRGPU_ENABLE_PER_STRAND_EDIT`
- adds Phase1 no-op hook functions:
  - `EvaluateMicrotypeCertificates`
  - `ExecuteBudgetedPhysics`
  - `ExecuteCertificateAwareRendering`
  - `ExecutePerStrandEditing`
  - `ExecuteGpuResidentRuntime`
- appends Phase1 placeholder telemetry columns to CSV/JSONL/summary output
- extends `schema.sql` and `hairgpu_final_db.py` for final5 run/frame columns
- keeps the existing `exp_650`/legacy flags intact

Phase intentionally remains `{PHASE}` with mapping version `{MAPPING_VERSION}`.

After applying, build `HairSystem.sln` / `HairSystem.vcxproj` with `Debug|x64` or `Release|x64`, then run the final5 smoke cases (`Combo_00000`, `Combo_11111`).
""", encoding="utf-8")
    return str(path.relative_to(root))


def main() -> int:
    root = repo_root()
    required = [
        "HairSystem.vcxproj",
        "apps/dx11_viewer/src/GpuHairViewer.h",
        "apps/dx11_viewer/src/GpuHairViewer.cpp",
        "tools/final_stage/hairgpu_final_run.py",
        "tools/final_stage/schema.sql",
        "tools/final_stage/hairgpu_final_db.py",
    ]
    missing = [p for p in required if not (root / p).exists()]
    if missing:
        print("Missing required files:", file=sys.stderr)
        for p in missing:
            print(f"  - {p}", file=sys.stderr)
        return 2

    changed: list[str] = []
    changed += ensure_vcxproj(root)
    changed += ensure_viewer_header(root)
    changed += ensure_viewer_cpp(root)
    changed += ensure_runner(root)
    changed += ensure_schema(root)
    changed += ensure_db_importer(root)
    changed.append(write_additive_sql(root))
    changed.append(write_notes(root))

    print("Phase1 remaining patch applied.")
    for item in changed:
        print(f"  - {item}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
