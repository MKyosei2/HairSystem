from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def read(path: str) -> str:
    return (ROOT / path).read_text(encoding='utf-8-sig')

def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding='utf-8', newline='\n')

def insert_once(text: str, marker: str, insertion: str, *, after: bool = True) -> str:
    if insertion.strip() in text:
        return text
    if marker not in text:
        raise RuntimeError(f'marker not found: {marker!r}')
    idx = text.index(marker)
    if after:
        idx += len(marker)
        return text[:idx] + insertion + text[idx:]
    return text[:idx] + insertion + text[idx:]

def patch_project() -> None:
    path = 'HairSystem.vcxproj'
    text = read(path)
    text = insert_once(text, '    <ClCompile Include="src\\hairgpu_core\\DccInterop.cpp" />\n',
        '    <ClCompile Include="src\\hairgpu_core\\GlobalHairBudget.cpp" />\n')
    text = insert_once(text, '    <ClCompile Include="src\\hairgpu_core\\HairMath.cpp" />\n',
        '    <ClCompile Include="src\\hairgpu_core\\HairNoveltyFlags.cpp" />\n    <ClCompile Include="src\\hairgpu_core\\HairNoveltyTelemetry.cpp" />\n')
    text = insert_once(text, '    <ClInclude Include="include\\hairgpu\\DccInterop.h" />\n',
        '    <ClInclude Include="include\\hairgpu\\GlobalHairBudget.h" />\n')
    text = insert_once(text, '    <ClInclude Include="include\\hairgpu\\HairCache.h" />\n',
        '    <ClInclude Include="include\\hairgpu\\HairNoveltyFlags.h" />\n    <ClInclude Include="include\\hairgpu\\HairNoveltyTelemetry.h" />\n')
    write(path, text)

def patch_header() -> None:
    path = 'apps/dx11_viewer/src/GpuHairViewer.h'
    text = read(path)
    text = insert_once(text, '#include "hairgpu/Types.h"\n',
        '#include "hairgpu/HairNoveltyFlags.h"\n#include "hairgpu/HairNoveltyTelemetry.h"\n#include "hairgpu/GlobalHairBudget.h"\n')
    text = insert_once(text, '            float maxPenetrationMm = 0.0f;\n',
'''            uint32_t topLevelNoveltyMask = 0u;
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
''')
    text = insert_once(text, '            bool enableMicrotype = false;\n',
        '            bool enableBudgetedPhysics = false;\n            bool enableCertificateRender = false;\n            bool enablePerStrandEdit = false;\n')
    text = insert_once(text, '        void StepSimulation(float dt);\n',
'''        void ResetNoveltyTelemetryForFrame();
        void EvaluateMicrotypeCertificates();
        void ExecuteBudgetedPhysics();
        void ExecuteCertificateAwareRendering();
        void ExecutePerStrandEditing();
        void ExecuteGpuResidentRuntime();
''')
    text = insert_once(text, '        ExperimentCaseConfig m_experimentCase;\n',
'''        hairgpu::HairNoveltyFeatureFlags m_noveltyFlags = {};
        hairgpu::GlobalHairFrameBudget m_globalHairBudget = {};
        hairgpu::HairNoveltyFrameTelemetry m_noveltyTelemetry = {};
''')
    write(path, text)

def patch_cpp_minimal() -> None:
    path = 'apps/dx11_viewer/src/GpuHairViewer.cpp'
    text = read(path)
    text = insert_once(text, '#include "hairgpu/Wig.h"\n',
        '#include "hairgpu/HairNoveltyFlags.h"\n#include "hairgpu/HairNoveltyTelemetry.h"\n#include "hairgpu/GlobalHairBudget.h"\n')
    marker = '        QueryPerformanceCounter(&simBegin);\n'
    text = insert_once(text, marker,
'''        m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();
        m_noveltyTelemetry.frameIndex = m_globalFrameIndex;
        m_noveltyTelemetry.measuredFrameIndex = m_measuredFrameIndex;
        m_noveltyTelemetry.topLevelNoveltyMask = hairgpu::BuildTopLevelNoveltyMask(m_noveltyFlags);
        ExecutePerStrandEditing();
        EvaluateMicrotypeCertificates();
        ExecuteBudgetedPhysics();
        ExecuteGpuResidentRuntime();
        ExecuteCertificateAwareRendering();
''')
    definitions = '''
    void GpuHairViewer::ResetNoveltyTelemetryForFrame()
    {
        m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();
        m_noveltyTelemetry.frameIndex = m_globalFrameIndex;
        m_noveltyTelemetry.measuredFrameIndex = m_measuredFrameIndex;
        m_noveltyTelemetry.topLevelNoveltyMask = hairgpu::BuildTopLevelNoveltyMask(m_noveltyFlags);
    }

    void GpuHairViewer::EvaluateMicrotypeCertificates()
    {
        if (!m_noveltyFlags.enableMicrotype) return;
    }

    void GpuHairViewer::ExecuteBudgetedPhysics()
    {
        if (!m_noveltyFlags.enableBudgetedPhysics) return;
    }

    void GpuHairViewer::ExecuteCertificateAwareRendering()
    {
        if (!m_noveltyFlags.enableCertificateRender) return;
    }

    void GpuHairViewer::ExecutePerStrandEditing()
    {
        if (!m_noveltyFlags.enablePerStrandEdit) return;
    }

    void GpuHairViewer::ExecuteGpuResidentRuntime()
    {
        if (!m_noveltyFlags.enableGpuResident) return;
    }

'''
    if 'void GpuHairViewer::EvaluateMicrotypeCertificates()' not in text:
        text = insert_once(text, '    void GpuHairViewer::UpdateRenderStatistics()\n', definitions, after=False)
    write(path, text)

def main() -> int:
    patch_project()
    patch_header()
    patch_cpp_minimal()
    print('Applied Phase1 scaffold patches.')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
