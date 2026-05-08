# 03. レンダリング系：Certificate-Aware Strand Rendering 完全版

---
version: Top Novelty Implementation Design v1.0
status: implementation-ready design
language: C++17 + HLSL compute shader
primary_runtime: GPU hair runtime / DX11-compatible render path
novelty_category: strand_rendering
not_included: paper strategy, publication framing
---

# 1. 実装新規性の一文

髪のライト・シャドウを普通に実装するのではなく、**strandごとの物理certificate、編集certificate、視覚重要度、shadow/specular/opacity誤差境界を使い、必要なstrandだけ高品質レンダリングへ昇格する certificate-aware rendering system** を実装する。

# 2. これは何が新しいのか

弱い実装は、髪にライト、シャドウ、透明度、ハイライトを実装することである。

トップ会議級の実装では、レンダリングの品質を一律に決めない。

```txt
従来型:
  camera / light
    -> all strands same rendering path or distance LOD

提案:
  physics certificate
  edit certificate
  render error certificate
  camera / light visibility
    -> per-strand render mode allocation
    -> high quality only where needed
    -> temporal stability checked commit
```

# 3. 研究範囲

この研究は、レンダリングそのものの美しさだけを目的にしない。

目的は、strandごとに以下を決めることである。

```txt
1. そのstrandを描くべきか
2. coarse opacityで十分か
3. strand rasterが必要か
4. high-quality shadowが必要か
5. high-quality specularが必要か
6. debug/certificate visualizationを出すべきか
7. 編集中・修復中だけpreview品質を上げるべきか
```

# 4. 弱い実装と強い実装

## 弱い実装

```txt
髪にshadow mapを当てる。
髪にspecular highlightを付ける。
髪を半透明で描く。
距離でLODを変える。
```

これは通常のレンダリング実装であり、新規性は弱い。

## トップ会議級の実装新規性

```txt
1. strandごとにrender certificateを生成する
2. physics riskとedit riskをrendering品質決定に使う
3. shadow / opacity / specular の誤差境界を持つ
4. silhouette、highlight、shadow contributorだけ高品質化する
5. frame間でrender modeが暴れないようtemporal lockを持つ
6. repair中・編集中strandだけdebug/preview pathへ昇格する
7. GPU上でrender task listをcompactする
```

# 5. ディレクトリ構成

```txt
include/strand/render/StrandRenderCertificate.h
include/strand/render/StrandRenderMode.h
include/strand/render/RenderModeAllocator.h
include/strand/render/StrandShadowImportance.h
include/strand/render/StrandSpecularImportance.h
include/strand/render/RenderTemporalStability.h

src/strand_render/RenderCertificateBuilder.cpp
src/strand_render/RenderModeAllocator.cpp
src/strand_render/ShadowImportance.cpp
src/strand_render/SpecularImportance.cpp
src/strand_render/OpacityErrorBound.cpp
src/strand_render/RenderTemporalStability.cpp
src/strand_render/RenderTelemetry.cpp

shaders/RenderCommon.hlsli
shaders/BuildRenderCertificatesCS.hlsl
shaders/AllocateRenderModesCS.hlsl
shaders/CompactRenderTasksCS.hlsl
shaders/RenderCoarseOpacityCS.hlsl
shaders/RenderStrandRasterCS.hlsl
shaders/RenderHighQualityShadowCS.hlsl
shaders/RenderHighQualitySpecularCS.hlsl
shaders/VisualizeRenderCertificatesCS.hlsl

tests/TestRenderCertificate.cpp
tests/TestRenderModeAllocator.cpp
tests/TestShadowImportance.cpp
tests/TestSpecularImportance.cpp
tests/TestRenderTemporalStability.cpp

tools/analyze_certificate_aware_rendering.py
```

# 6. Render certificate

```cpp
namespace strand::render {

struct StrandRenderCertificate
{
    uint32_t strandId;

    float screenSpaceCoverage;
    float opacityErrorBound;
    float shadowErrorBound;
    float specularLobeErrorBound;
    float silhouetteImportance;
    float motionStability;

    float physicsRiskVisibility;
    float editVisibility;
    float repairVisibility;

    float totalRenderRisk;

    uint32_t physicsCertificateBits;
    uint32_t editCertificateBits;
    uint32_t renderCertificateBits;

    uint32_t previousRenderMode;
    uint32_t selectedRenderMode;
    uint32_t reasonBits;

    uint16_t framesSinceModeChange;
    uint16_t lockFramesRemaining;
};

}
```

# 7. Render certificate bits

```cpp
namespace strand::render {

enum RenderCertificateBits : uint32_t
{
    RENDER_CERT_NONE                 = 0,
    RENDER_CERT_OPACITY_UNSAFE       = 1u << 0,
    RENDER_CERT_SHADOW_UNSAFE        = 1u << 1,
    RENDER_CERT_SPECULAR_UNSAFE      = 1u << 2,
    RENDER_CERT_SILHOUETTE_IMPORTANT = 1u << 3,
    RENDER_CERT_MOTION_UNSTABLE      = 1u << 4,
    RENDER_CERT_EDIT_VISIBLE         = 1u << 5,
    RENDER_CERT_REPAIR_VISIBLE       = 1u << 6,
    RENDER_CERT_DEBUG_REQUESTED      = 1u << 7,
};

}
```

# 8. Render modes

```cpp
namespace strand::render {

enum StrandRenderMode : uint32_t
{
    RENDER_SKIP = 0,
    RENDER_COARSE_OPACITY = 1,
    RENDER_STRAND_RASTER = 2,
    RENDER_HIGH_QUALITY_SHADOW = 3,
    RENDER_HIGH_QUALITY_SPECULAR = 4,
    RENDER_FULL_QUALITY = 5,
    RENDER_DEBUG_CERTIFICATE = 6,
};

}
```

# 9. Render task

```cpp
namespace strand::render {

struct StrandRenderTask
{
    uint32_t taskId;
    uint32_t strandId;
    uint32_t renderMode;
    uint32_t reasonBits;

    float estimatedCostMs;
    float expectedErrorReduction;
    float screenSpaceCoverage;
    float depthMin;
    float depthMax;

    uint32_t materialId;
    uint32_t lightMask;
};

struct RenderCounters
{
    uint32_t skipCount;
    uint32_t coarseOpacityCount;
    uint32_t strandRasterCount;
    uint32_t highQualityShadowCount;
    uint32_t highQualitySpecularCount;
    uint32_t fullQualityCount;
    uint32_t debugCertificateCount;
    uint32_t overflowCount;
};

}
```

# 10. Settings

```cpp
namespace strand::render {

struct CertificateAwareRenderSettings
{
    uint32_t strandCount = 0;
    uint32_t maxRenderTasks = 131072;

    float opacityErrorTolerance = 0.02f;
    float shadowErrorTolerance = 0.03f;
    float specularErrorTolerance = 0.04f;
    float silhouetteCoverageThreshold = 0.001f;

    float physicsRiskRenderBoost = 1.5f;
    float editRiskRenderBoost = 2.0f;
    float repairRenderBoost = 2.0f;

    float renderBudgetMs = 4.0f;
    float highQualityShadowBudgetMs = 1.0f;
    float highQualitySpecularBudgetMs = 1.0f;

    uint16_t renderModeLockFrames = 3;
    float modeSwitchPenalty = 0.25f;
};

}
```

# 11. C++ API

```cpp
namespace strand::render {

StrandRenderCertificate BuildRenderCertificateCpu(
    uint32_t strandId,
    const CameraView& camera,
    const LightView& light,
    uint32_t physicsCertificateBits,
    uint32_t editCertificateBits,
    float physicsRisk,
    float editRisk,
    float repairRisk,
    const CertificateAwareRenderSettings& settings);

float EstimateOpacityErrorBoundCpu(
    uint32_t strandId,
    const CameraView& camera);

float EstimateShadowErrorBoundCpu(
    uint32_t strandId,
    const LightView& light);

float EstimateSpecularLobeErrorBoundCpu(
    uint32_t strandId,
    const CameraView& camera,
    const LightView& light);

uint32_t SelectRenderModeCpu(
    const StrandRenderCertificate& cert,
    const CertificateAwareRenderSettings& settings);

void AllocateRenderModesUnderBudgetCpu(
    StrandRenderCertificate* certificates,
    uint32_t strandCount,
    const CertificateAwareRenderSettings& settings);

void BuildRenderTasksCpu(
    const StrandRenderCertificate* certificates,
    uint32_t strandCount,
    StrandRenderTask* outTasks,
    uint32_t* outTaskCount);

}
```

# 12. Render mode selection rule

```cpp
uint32_t SelectRenderModeCpu(
    const StrandRenderCertificate& c,
    const CertificateAwareRenderSettings& s)
{
    if ((c.renderCertificateBits & RENDER_CERT_DEBUG_REQUESTED) != 0)
        return RENDER_DEBUG_CERTIFICATE;

    if (c.editVisibility > 0.5f || c.repairVisibility > 0.5f)
        return RENDER_FULL_QUALITY;

    if (c.shadowErrorBound > s.shadowErrorTolerance &&
        c.specularLobeErrorBound > s.specularErrorTolerance)
        return RENDER_FULL_QUALITY;

    if (c.shadowErrorBound > s.shadowErrorTolerance)
        return RENDER_HIGH_QUALITY_SHADOW;

    if (c.specularLobeErrorBound > s.specularErrorTolerance)
        return RENDER_HIGH_QUALITY_SPECULAR;

    if (c.screenSpaceCoverage > s.silhouetteCoverageThreshold)
        return RENDER_STRAND_RASTER;

    if (c.opacityErrorBound > s.opacityErrorTolerance)
        return RENDER_COARSE_OPACITY;

    return RENDER_SKIP;
}
```

# 13. Frame algorithm

```cpp
void CertificateAwareStrandRenderer::RenderFrame(const RenderFrameInput& input)
{
    // 1. camera/light/physics/editからrender certificateを作る
    DispatchBuildRenderCertificates();

    // 2. per-strand render modeを決める
    DispatchAllocateRenderModes();

    // 3. render modeごとにtaskをcompactする
    DispatchCompactRenderTasks();

    // 4. mode別に描画する
    DispatchRenderCoarseOpacity();
    DispatchRenderStrandRaster();
    DispatchRenderHighQualityShadow();
    DispatchRenderHighQualitySpecular();

    // 5. debug要求がある場合certificate overlayを描く
    DispatchVisualizeRenderCertificates();
}
```

# 14. HLSL common

```hlsl
#define RENDER_SKIP                  0u
#define RENDER_COARSE_OPACITY        1u
#define RENDER_STRAND_RASTER         2u
#define RENDER_HIGH_QUALITY_SHADOW   3u
#define RENDER_HIGH_QUALITY_SPECULAR 4u
#define RENDER_FULL_QUALITY          5u
#define RENDER_DEBUG_CERTIFICATE     6u

#define RENDER_CERT_OPACITY_UNSAFE       (1u << 0)
#define RENDER_CERT_SHADOW_UNSAFE        (1u << 1)
#define RENDER_CERT_SPECULAR_UNSAFE      (1u << 2)
#define RENDER_CERT_SILHOUETTE_IMPORTANT (1u << 3)
#define RENDER_CERT_MOTION_UNSTABLE      (1u << 4)
#define RENDER_CERT_EDIT_VISIBLE         (1u << 5)
#define RENDER_CERT_REPAIR_VISIBLE       (1u << 6)
#define RENDER_CERT_DEBUG_REQUESTED      (1u << 7)

struct StrandRenderCertificateGpu
{
    uint strandId;
    float screenSpaceCoverage;
    float opacityErrorBound;
    float shadowErrorBound;
    float specularLobeErrorBound;
    float silhouetteImportance;
    float motionStability;
    float physicsRiskVisibility;
    float editVisibility;
    float repairVisibility;
    float totalRenderRisk;
    uint physicsCertificateBits;
    uint editCertificateBits;
    uint renderCertificateBits;
    uint previousRenderMode;
    uint selectedRenderMode;
    uint reasonBits;
    uint temporalPacked;
};

struct StrandRenderTaskGpu
{
    uint taskId;
    uint strandId;
    uint renderMode;
    uint reasonBits;
    float estimatedCostMs;
    float expectedErrorReduction;
    float screenSpaceCoverage;
    float depthMin;
    float depthMax;
    uint materialId;
    uint lightMask;
};
```

# 15. BuildRenderCertificatesCS

```hlsl
[numthreads(128, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint strandId = tid.x;
    if (strandId >= g_Settings.strandCount) return;

    StrandBounds b = g_StrandBounds[strandId];
    PhysicsCertificate p = g_PhysicsCertificates[strandId];
    EditCertificate e = g_EditCertificates[strandId];

    float coverage = EstimateScreenCoverage(b, g_Camera);
    float opacityErr = EstimateOpacityError(b, g_Camera);
    float shadowErr = EstimateShadowError(b, g_Light);
    float specErr = EstimateSpecularError(b, g_Camera, g_Light);
    float silhouette = EstimateSilhouetteImportance(b, g_Camera);

    uint bits = 0u;
    if (opacityErr > g_Settings.opacityErrorTolerance) bits |= RENDER_CERT_OPACITY_UNSAFE;
    if (shadowErr > g_Settings.shadowErrorTolerance) bits |= RENDER_CERT_SHADOW_UNSAFE;
    if (specErr > g_Settings.specularErrorTolerance) bits |= RENDER_CERT_SPECULAR_UNSAFE;
    if (silhouette > g_Settings.silhouetteCoverageThreshold) bits |= RENDER_CERT_SILHOUETTE_IMPORTANT;
    if (e.editRisk > 0.0f) bits |= RENDER_CERT_EDIT_VISIBLE;
    if (p.repairRisk > 0.0f) bits |= RENDER_CERT_REPAIR_VISIBLE;

    StrandRenderCertificateGpu c;
    c.strandId = strandId;
    c.screenSpaceCoverage = coverage;
    c.opacityErrorBound = opacityErr;
    c.shadowErrorBound = shadowErr;
    c.specularLobeErrorBound = specErr;
    c.silhouetteImportance = silhouette;
    c.motionStability = EstimateMotionStability(strandId);
    c.physicsRiskVisibility = p.totalRisk * coverage;
    c.editVisibility = e.editRisk * coverage;
    c.repairVisibility = p.repairRisk * coverage;
    c.totalRenderRisk = opacityErr + shadowErr + specErr + silhouette + c.physicsRiskVisibility + c.editVisibility + c.repairVisibility;
    c.physicsCertificateBits = p.certificateBits;
    c.editCertificateBits = e.editBits;
    c.renderCertificateBits = bits;
    c.previousRenderMode = g_PreviousRenderModes[strandId];
    c.selectedRenderMode = RENDER_SKIP;
    c.reasonBits = bits;
    c.temporalPacked = 0u;

    g_RenderCertificates[strandId] = c;
}
```

# 16. AllocateRenderModesCS

```hlsl
[numthreads(128, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint strandId = tid.x;
    if (strandId >= g_Settings.strandCount) return;

    StrandRenderCertificateGpu c = g_RenderCertificates[strandId];

    uint mode = RENDER_SKIP;

    if ((c.renderCertificateBits & RENDER_CERT_DEBUG_REQUESTED) != 0)
        mode = RENDER_DEBUG_CERTIFICATE;
    else if ((c.renderCertificateBits & RENDER_CERT_EDIT_VISIBLE) != 0 ||
             (c.renderCertificateBits & RENDER_CERT_REPAIR_VISIBLE) != 0)
        mode = RENDER_FULL_QUALITY;
    else if ((c.renderCertificateBits & RENDER_CERT_SHADOW_UNSAFE) != 0 &&
             (c.renderCertificateBits & RENDER_CERT_SPECULAR_UNSAFE) != 0)
        mode = RENDER_FULL_QUALITY;
    else if ((c.renderCertificateBits & RENDER_CERT_SHADOW_UNSAFE) != 0)
        mode = RENDER_HIGH_QUALITY_SHADOW;
    else if ((c.renderCertificateBits & RENDER_CERT_SPECULAR_UNSAFE) != 0)
        mode = RENDER_HIGH_QUALITY_SPECULAR;
    else if ((c.renderCertificateBits & RENDER_CERT_SILHOUETTE_IMPORTANT) != 0)
        mode = RENDER_STRAND_RASTER;
    else if ((c.renderCertificateBits & RENDER_CERT_OPACITY_UNSAFE) != 0)
        mode = RENDER_COARSE_OPACITY;

    mode = ApplyRenderTemporalLock(c.previousRenderMode, mode, c.temporalPacked);
    c.selectedRenderMode = mode;
    g_RenderCertificates[strandId] = c;
}
```

# 17. Telemetry

```txt
case_id, frame, strand_count,
render_skip_count,
render_coarse_opacity_count,
render_strand_raster_count,
render_high_quality_shadow_count,
render_high_quality_specular_count,
render_full_quality_count,
render_debug_certificate_count,
mean_opacity_error_bound,
p95_opacity_error_bound,
mean_shadow_error_bound,
p95_shadow_error_bound,
mean_specular_error_bound,
p95_specular_error_bound,
render_mode_switch_count,
temporal_flicker_score,
gpu_render_ms,
shadow_ms,
specular_ms,
opacity_ms,
overflow_count
```

# 18. テスト

```txt
TestOpacityErrorBound
TestShadowErrorBound
TestSpecularErrorBound
TestSilhouetteImportance
TestRenderCertificateBits
TestRenderModeSelection
TestRenderTemporalLock
TestRenderTaskCompaction
TestDebugCertificateVisualization
TestGpuCpuRenderModeConsistency
```

# 19. 完了条件

```txt
1. strandごとにrender certificateを生成できる。
2. physics certificateとedit certificateをrender mode決定に使える。
3. opacity / shadow / specular / silhouette の誤差または重要度を持てる。
4. render modeをGPU上で割り当てられる。
5. render modeごとのtask listをcompactできる。
6. 高品質shadow/specularを必要なstrandだけに適用できる。
7. temporal lockでmode切替のちらつきを抑えられる。
8. render certificate overlayを表示できる。
9. gpu_render_ms、mode counts、error bounds、flicker scoreをtelemetryに出せる。
```

# 20. この研究の核

この研究の核は、髪を綺麗に描くことだけではない。

```txt
どのstrandを高品質に描くべきかを、certificateで決める。
```

ライト・シャドウ・ハイライトを一律に処理するのではなく、物理状態、編集状態、視覚誤差からrendering精度をstrand単位で制御する。これがレンダリング系としてトップ会議級の実装新規性になる。
