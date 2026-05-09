# 03. レンダリング系：Budgeted Certificate-Aware Strand Rendering 完全実装設計書

---
version: Complete Implementation Design v3.0
status: implementation-ready
language: C++17 + HLSL compute shader
primary_runtime: GPU hair runtime / DX11-compatible render path
novelty_category: budgeted_strand_rendering
target_gpu: RTX 4090 class
target_frame_rate: 60 FPS
target_frame_budget_ms: 16.6
module_budget_target_ms: 4.0
---

# 1. 実装新規性の一文

髪のライト・シャドウ・ハイライトを全strandに一律適用するのではなく、**strandごとのphysics/edit/render certificateとopacity/shadow/specular/silhouette誤差境界を使い、render budget内で視覚効果の大きいstrandだけを高品質modeへ昇格する budgeted certificate-aware strand rendering system** を実装する。

# 2. 完全実装の目的

本実装は、髪を単に高品質に描画するものではない。

目的は、次の高コスト処理を60FPS予算内に収めることである。

```txt
all-strand strand raster
high-quality shadow for all visible strands
high-quality specular for all visible strands
full-quality preview for all edited/repaired strands
debug certificate overlay
transparency / opacity overdraw
render mode instability
```

これらを以下の制御構造へ変換する。

```txt
render certificate
  -> error / importance estimation
  -> preferred render mode
  -> benefit-cost ranking
  -> budgeted selected render mode
  -> temporal lock / hysteresis
  -> render task compaction
  -> mode-specific rendering
  -> dropped-quality telemetry
```

# 3. 完全実装範囲

本設計書で実装するものは以下である。

```txt
1. StrandRenderCertificate生成
2. opacity error bound推定
3. shadow error bound推定
4. specular lobe error bound推定
5. silhouette importance推定
6. physics/edit/repair visibility統合
7. preferred render mode選択
8. render benefit-cost推定
9. budgeted render mode allocation
10. temporal lock / hysteresis
11. render task compaction
12. coarse opacity pass
13. strand raster pass
14. high-quality shadow pass
15. high-quality specular pass
16. full-quality pass
17. debug certificate visualization
18. render budget telemetry
19. CPU debug baseline
20. GPU path
21. 02/04/05との接続hook
```

# 4. 実装契約

```txt
1. preferred modeとselected modeを分離する。
2. high-quality shadow/specular/full-quality対象はhard capを持つ。
3. physics/edit/repair由来のrender boostは無制限にしない。
4. edit preview / repair previewはTTLと上限を持つ。
5. debug visualizationは評価時だけ有効化できる。
6. budget外で落としたstrand数をtelemetryに出す。
7. temporal lockによりmode switchのちらつきを抑える。
8. emergency degradationではlockを解除できる。
9. GPU pathではCPU readbackでdispatch数を決めない。
10. 60FPS評価ではp95_frame_ms <= 16.6を基準にする。
```

# 5. ディレクトリ構成

```txt
include/strand/render/StrandRenderCertificate.h
include/strand/render/StrandRenderMode.h
include/strand/render/RenderFrameBudget.h
include/strand/render/RenderBudgetController.h
include/strand/render/RenderModeAllocator.h
include/strand/render/RenderBenefitCost.h
include/strand/render/StrandShadowImportance.h
include/strand/render/StrandSpecularImportance.h
include/strand/render/OpacityErrorBound.h
include/strand/render/RenderTemporalStability.h
include/strand/render/RenderDegradationPolicy.h
include/strand/render/RenderTelemetry.h

src/strand_render/BudgetedCertificateAwareRenderer.cpp
src/strand_render/RenderCertificateBuilder.cpp
src/strand_render/RenderBudgetController.cpp
src/strand_render/RenderBenefitCost.cpp
src/strand_render/RenderModeAllocator.cpp
src/strand_render/ShadowImportance.cpp
src/strand_render/SpecularImportance.cpp
src/strand_render/OpacityErrorBound.cpp
src/strand_render/RenderTemporalStability.cpp
src/strand_render/RenderDegradationPolicy.cpp
src/strand_render/RenderTaskCompaction.cpp
src/strand_render/RenderTelemetry.cpp
src/strand_render/CpuRenderBaseline.cpp

shaders/RenderCommon.hlsli
shaders/ClearRenderCountersCS.hlsl
shaders/BuildRenderCertificatesCS.hlsl
shaders/EstimateRenderBenefitCostCS.hlsl
shaders/SelectPreferredRenderModesCS.hlsl
shaders/AllocateRenderModesUnderBudgetCS.hlsl
shaders/ApplyRenderTemporalStabilityCS.hlsl
shaders/CompactRenderTasksCS.hlsl
shaders/RenderCoarseOpacityCS.hlsl
shaders/RenderStrandRasterCS.hlsl
shaders/RenderHighQualityShadowBudgetedCS.hlsl
shaders/RenderHighQualitySpecularBudgetedCS.hlsl
shaders/RenderFullQualityBudgetedCS.hlsl
shaders/VisualizeRenderCertificatesCS.hlsl
shaders/WriteRenderTelemetryCS.hlsl

tests/TestOpacityErrorBound.cpp
tests/TestShadowErrorBound.cpp
tests/TestSpecularErrorBound.cpp
tests/TestSilhouetteImportance.cpp
tests/TestRenderCertificateBits.cpp
tests/TestPreferredRenderModeSelection.cpp
tests/TestBudgetedRenderModeSelection.cpp
tests/TestRenderBenefitCostRanking.cpp
tests/TestRenderTemporalLock.cpp
tests/TestEmergencyModeDrop.cpp
tests/TestRenderTaskCompaction.cpp
tests/TestRenderBudgetOverflow.cpp
tests/TestDebugCertificateVisualization.cpp
tests/TestGpuCpuRenderModeConsistency.cpp
tests/TestNoCpuReadbackInGpuResidentPath.cpp
tests/TestRenderTelemetryCompleteness.cpp

tools/analyze_budgeted_certificate_rendering.py
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
    uint32_t preferredRenderMode;
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
    float benefitCostRatio;

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

    uint32_t highQualityShadowDroppedCount;
    uint32_t highQualitySpecularDroppedCount;
    uint32_t fullQualityDroppedCount;
    uint32_t debugDroppedCount;

    uint32_t overflowCount;
    uint32_t budgetDropCount;
};

}
```

# 10. Render frame budget

```cpp
namespace strand::render {

struct RenderFrameBudget
{
    float renderBudgetMs = 4.0f;

    uint32_t maxRenderTasks = 65536;
    uint32_t maxStrandRasterTasks = 32768;
    uint32_t maxHighQualityShadowTasks = 4096;
    uint32_t maxHighQualitySpecularTasks = 4096;
    uint32_t maxFullQualityTasks = 2048;
    uint32_t maxDebugCertificateTasks = 0;

    float coarseOpacityBudgetMs = 0.75f;
    float strandRasterBudgetMs = 1.25f;
    float highQualityShadowBudgetMs = 1.0f;
    float highQualitySpecularBudgetMs = 1.0f;
    float fullQualityBudgetMs = 1.0f;

    uint16_t renderModeLockFrames = 3;
    uint16_t emergencyModeDropFrames = 2;

    float modeSwitchPenalty = 0.25f;

    float maxAllowedOpacityError = 0.04f;
    float maxAllowedShadowError = 0.06f;
    float maxAllowedSpecularError = 0.08f;

    uint32_t allowAdaptiveQualityDrop = 1;
};

struct RenderBudgetCounters
{
    uint32_t acceptedRenderTaskCount;
    uint32_t droppedRenderTaskCount;

    uint32_t acceptedHighQualityShadowCount;
    uint32_t droppedHighQualityShadowCount;

    uint32_t acceptedHighQualitySpecularCount;
    uint32_t droppedHighQualitySpecularCount;

    uint32_t acceptedFullQualityCount;
    uint32_t droppedFullQualityCount;

    uint32_t overflowCount;
    uint32_t budgetDropCount;
};

}
```

# 11. Settings

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

    RenderFrameBudget frameBudget;
};

}
```

# 12. Runtime buffers

```cpp
namespace strand::render {

struct RenderRuntimeBuffers
{
    GPUBuffer strandBounds;
    GPUBuffer physicsCertificates;
    GPUBuffer editCertificates;

    GPUBuffer renderCertificates;
    GPUBuffer renderTasks;
    GPUBuffer renderCounters;
    GPUBuffer renderDispatchArgs;
    GPUBuffer renderTelemetryRing;

    GPUBuffer coarseOpacityTargets;
    GPUBuffer strandRasterTargets;
    GPUBuffer highQualityShadowTargets;
    GPUBuffer highQualitySpecularTargets;
    GPUBuffer fullQualityTargets;
};

}
```

# 13. C++ API

```cpp
namespace strand::render {

class BudgetedCertificateAwareRenderer
{
public:
    bool Initialize(const CertificateAwareRenderSettings& settings);
    void Shutdown();
    void Resize(uint32_t strandCount);

    void RenderFrame(const RenderFrameInput& input);
    void ExecuteCpuDebugBaseline(const RenderFrameInput& input);

    RenderTelemetry ReadTelemetryDelayed(uint32_t frameIndex);

private:
    void CreateBuffers();
    void BindBuffers();
    void UploadFrameConstants(const RenderFrameInput& input);

    void DispatchClearRenderCounters();
    void DispatchBuildRenderCertificates();
    void DispatchEstimateRenderBenefitCost();
    void DispatchSelectPreferredRenderModes();
    void DispatchAllocateRenderModesUnderBudget();
    void DispatchApplyRenderTemporalStability();
    void DispatchCompactRenderTasks();

    void DispatchRenderCoarseOpacity();
    void DispatchRenderStrandRaster();
    void DispatchRenderHighQualityShadowBudgeted();
    void DispatchRenderHighQualitySpecularBudgeted();
    void DispatchRenderFullQualityBudgeted();
    void DispatchVisualizeRenderCertificates();

    void DispatchWriteRenderTelemetry();
};

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

float EstimateRenderCostCpu(
    const StrandRenderCertificate& cert,
    uint32_t candidateRenderMode);

float EstimateRenderErrorReductionCpu(
    const StrandRenderCertificate& cert,
    uint32_t candidateRenderMode);

float ComputeRenderBenefitCostRatioCpu(
    const StrandRenderCertificate& cert,
    uint32_t candidateRenderMode,
    const RenderFrameBudget& budget);

uint32_t SelectPreferredRenderModeCpu(
    const StrandRenderCertificate& cert,
    const CertificateAwareRenderSettings& settings);

uint32_t SelectBudgetedRenderModeCpu(
    const StrandRenderCertificate& cert,
    const RenderFrameBudget& budget,
    RenderBudgetCounters* counters);

void AllocateRenderModesUnderBudgetCpu(
    StrandRenderCertificate* certificates,
    uint32_t strandCount,
    const CertificateAwareRenderSettings& settings);

void BuildRenderTasksCpu(
    const StrandRenderCertificate* certificates,
    uint32_t strandCount,
    const RenderFrameBudget& budget,
    StrandRenderTask* outTasks,
    uint32_t* outTaskCount);

}
```

# 14. Preferred render mode selection

```cpp
uint32_t SelectPreferredRenderModeCpu(
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

# 15. Budgeted render mode allocation

```cpp
uint32_t SelectBudgetedRenderModeCpu(
    const StrandRenderCertificate& c,
    const RenderFrameBudget& b,
    RenderBudgetCounters* counters)
{
    uint32_t preferred = c.preferredRenderMode;

    if (preferred == RENDER_DEBUG_CERTIFICATE)
    {
        if (counters->acceptedRenderTaskCount >= b.maxDebugCertificateTasks)
        {
            counters->budgetDropCount++;
            counters->droppedRenderTaskCount++;
            return RENDER_STRAND_RASTER;
        }
        return preferred;
    }

    if (preferred == RENDER_FULL_QUALITY)
    {
        if (counters->acceptedFullQualityCount >= b.maxFullQualityTasks)
        {
            counters->droppedFullQualityCount++;
            counters->droppedRenderTaskCount++;
            return RENDER_STRAND_RASTER;
        }

        counters->acceptedFullQualityCount++;
        return preferred;
    }

    if (preferred == RENDER_HIGH_QUALITY_SHADOW)
    {
        if (counters->acceptedHighQualityShadowCount >= b.maxHighQualityShadowTasks)
        {
            counters->droppedHighQualityShadowCount++;
            counters->droppedRenderTaskCount++;
            return RENDER_STRAND_RASTER;
        }

        counters->acceptedHighQualityShadowCount++;
        return preferred;
    }

    if (preferred == RENDER_HIGH_QUALITY_SPECULAR)
    {
        if (counters->acceptedHighQualitySpecularCount >= b.maxHighQualitySpecularTasks)
        {
            counters->droppedHighQualitySpecularCount++;
            counters->droppedRenderTaskCount++;
            return RENDER_STRAND_RASTER;
        }

        counters->acceptedHighQualitySpecularCount++;
        return preferred;
    }

    return preferred;
}
```

# 16. Frame algorithm

```cpp
void BudgetedCertificateAwareRenderer::RenderFrame(const RenderFrameInput& input)
{
    BindBuffers();
    UploadFrameConstants(input);

    DispatchClearRenderCounters();

    // 1. 全strandのrender certificateを安く作る。
    DispatchBuildRenderCertificates();

    // 2. 誤差低減量 / 推定コストでpriorityを計算する。
    DispatchEstimateRenderBenefitCost();

    // 3. budgetを考慮しないpreferred modeを作る。
    DispatchSelectPreferredRenderModes();

    // 4. mode別budget内でselected modeを決める。
    DispatchAllocateRenderModesUnderBudget();

    // 5. mode切替が暴れないようtemporal lock / hysteresisをかける。
    DispatchApplyRenderTemporalStability();

    // 6. mode別task listをcompactする。
    DispatchCompactRenderTasks();

    // 7. 低コストmodeから順に描画する。
    DispatchRenderCoarseOpacity();
    DispatchRenderStrandRaster();

    // 8. budget内だけ高品質passを実行する。
    DispatchRenderHighQualityShadowBudgeted();
    DispatchRenderHighQualitySpecularBudgeted();
    DispatchRenderFullQualityBudgeted();

    // 9. 評価時だけcertificate overlayを描く。
    DispatchVisualizeRenderCertificates();

    // 10. 落とした品質と残った誤差をtelemetryに出す。
    DispatchWriteRenderTelemetry();
}
```

# 17. HLSL common

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

struct RenderFrameBudgetGpu
{
    float renderBudgetMs;

    uint maxRenderTasks;
    uint maxStrandRasterTasks;
    uint maxHighQualityShadowTasks;
    uint maxHighQualitySpecularTasks;
    uint maxFullQualityTasks;
    uint maxDebugCertificateTasks;

    float coarseOpacityBudgetMs;
    float strandRasterBudgetMs;
    float highQualityShadowBudgetMs;
    float highQualitySpecularBudgetMs;
    float fullQualityBudgetMs;

    uint renderModeLockFrames;
    uint emergencyModeDropFrames;

    float modeSwitchPenalty;

    float maxAllowedOpacityError;
    float maxAllowedShadowError;
    float maxAllowedSpecularError;

    uint allowAdaptiveQualityDrop;
};

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
    uint preferredRenderMode;
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
    float benefitCostRatio;
    float screenSpaceCoverage;
    float depthMin;
    float depthMax;
    uint materialId;
    uint lightMask;
};
```

# 18. GPU pass details

## 18.1 BuildRenderCertificatesCS

```txt
input:
  strand bounds
  camera
  light
  physics certificates
  edit certificates

output:
  StrandRenderCertificateGpu[strandCount]

rule:
  coverage, opacity error, shadow error, specular error, silhouette importance,
  physics visibility, edit visibility, repair visibilityを計算する。
```

## 18.2 EstimateRenderBenefitCostCS

```txt
input:
  StrandRenderCertificateGpu

output:
  expectedErrorReduction
  estimatedCostMs
  benefitCostRatio

rule:
  high-cost modeほどcostを高くし、誤差低減量が大きいstrandを優先する。
```

## 18.3 SelectPreferredRenderModesCS

```txt
input:
  StrandRenderCertificateGpu

output:
  preferredRenderMode

rule:
  error boundとvisibilityから理想modeを決める。
```

## 18.4 AllocateRenderModesUnderBudgetCS

```txt
input:
  preferredRenderMode
  benefitCostRatio
  RenderFrameBudgetGpu

output:
  selectedRenderMode
  RenderBudgetCountersGpu

rule:
  high-quality shadow/specular/full quality/debugはhard capを守る。
  capを超えたstrandは一段低いmodeへdegradeする。
```

## 18.5 ApplyRenderTemporalStabilityCS

```txt
input:
  previousRenderMode
  selectedRenderMode
  lockFramesRemaining

output:
  stabilized selectedRenderMode

rule:
  通常時はmode切替を抑制する。
  emergency drop時はlockを解除できる。
```

## 18.6 CompactRenderTasksCS

```txt
input:
  selectedRenderMode

output:
  mode別RenderTaskGpu
  RenderCountersGpu
```

## 18.7 Render passes

```txt
RenderCoarseOpacityCS:
  coarse opacityのみを処理する。

RenderStrandRasterCS:
  silhouette / visible strand形状を処理する。

RenderHighQualityShadowBudgetedCS:
  selectedRenderMode == RENDER_HIGH_QUALITY_SHADOW or RENDER_FULL_QUALITYのみ処理する。

RenderHighQualitySpecularBudgetedCS:
  selectedRenderMode == RENDER_HIGH_QUALITY_SPECULAR or RENDER_FULL_QUALITYのみ処理する。

RenderFullQualityBudgetedCS:
  selectedRenderMode == RENDER_FULL_QUALITYのみ処理する。

VisualizeRenderCertificatesCS:
  selectedRenderMode == RENDER_DEBUG_CERTIFICATEのみ処理する。
```

# 19. Budget policy

```txt
1. buffer capacity と per-frame render budget を分ける。
2. preferred mode と selected mode を分ける。
3. full quality は無制限にしない。
4. repair/edit visible strand でも、budget上位だけfull qualityにする。
5. high-quality shadow/specularはbenefit-cost上位だけ通す。
6. debug certificateは通常実行では0件を基本にする。
7. budget超過時は full quality -> strand raster -> coarse opacity の順でdegradeする。
8. temporal lockでちらつきを抑えるが、emergency dropではlockを解除できる。
9. 落としたstrand数と誤差は必ずtelemetryに出す。
```

# 20. Adaptive degradation

```cpp
void AdaptRenderBudget(
    const RenderTelemetry& prev,
    RenderFrameBudget* budget)
{
    if (prev.p95FrameMs > 16.6f)
    {
        budget->maxFullQualityTasks =
            std::max(512u, budget->maxFullQualityTasks * 3 / 4);

        budget->maxHighQualityShadowTasks =
            std::max(1024u, budget->maxHighQualityShadowTasks * 3 / 4);

        budget->maxHighQualitySpecularTasks =
            std::max(1024u, budget->maxHighQualitySpecularTasks * 3 / 4);

        budget->maxStrandRasterTasks =
            std::max(8192u, budget->maxStrandRasterTasks * 3 / 4);

        budget->emergencyModeDropFrames = 2;
    }
    else if (prev.p95FrameMs < 14.0f &&
             (prev.p95ShadowErrorBound > budget->maxAllowedShadowError ||
              prev.p95SpecularErrorBound > budget->maxAllowedSpecularError))
    {
        budget->maxHighQualityShadowTasks =
            std::min(4096u, budget->maxHighQualityShadowTasks + 512);

        budget->maxHighQualitySpecularTasks =
            std::min(4096u, budget->maxHighQualitySpecularTasks + 512);

        budget->maxFullQualityTasks =
            std::min(2048u, budget->maxFullQualityTasks + 256);
    }
}
```

# 21. Integration hooks

## 21.1 01との接続

```txt
certificateBits
certificateValue
safe / repair / island / fallback decision
decodeResidualBound
worstSegment
```

01でfallbackやrepairが多いstrandは、03ではpreview/debug対象になり得る。ただしrender budget内で上位のみ昇格する。

## 21.2 02との接続

```txt
physicsCertificateBits
physicsRisk
repairVisibility
islandVisibility
fallbackReasonBits
```

physics riskによる高品質化にはhard capを持たせる。

```txt
maxPhysicsRiskRenderBoost = 4096
maxRepairVisibleFullQuality = 2048
```

## 21.3 04との接続

```txt
editCertificateBits
editVisibility
renderPreviewRequired
selectedEditedStrands
previewHighQualityFrames
```

編集previewは無制限にfull qualityへ昇格しない。

```txt
maxEditPreviewFullQuality = 1024〜2048
preview TTL = 2〜5 frames
brush editではimportance上位のみfull quality
```

## 21.4 05との接続

```txt
render task compaction
DispatchIndirect args generation
CPU readback-free execution
delayed telemetry
overflow-safe counters
```

# 22. Physics-render budget handshake

```cpp
struct PhysicsRenderBudgetHandshake
{
    float totalHairBudgetMs = 8.0f;

    float physicsBudgetMs = 3.0f;
    float renderBudgetMs = 4.0f;
    float sharedEmergencyBudgetMs = 1.0f;

    uint32_t maxRepairVisibleFullQuality = 2048;
    uint32_t maxPhysicsRiskRenderBoost = 4096;

    float physicsOverBudgetRenderPenalty = 0.5f;
    float renderOverBudgetIslandPenalty = 0.5f;

    uint16_t repairPreviewMaxFrames = 3;
};
```

```txt
1. 02がbudget超過したframeでは、03のfull quality昇格数を下げる。
2. 03がbudget超過したframeでは、02のnew island生成数を下げる。
3. repair中strandを必ずfull qualityにするのではなく、上位N本だけにする。
4. physics risk render boostにはhard capを持たせる。
5. 編集・修復previewのTTLを制限する。
6. どのstrandを落としたかをtelemetryに出す。
```

# 23. Telemetry

```txt
case_id, frame, strand_count, variant, scenario, seed,

p50_frame_ms,
p95_frame_ms,
p99_frame_ms,
gpu_render_ms,

certificate_build_ms,
mode_allocation_ms,
task_compaction_ms,
coarse_opacity_ms,
strand_raster_ms,
shadow_ms,
specular_ms,
full_quality_ms,

render_skip_count,
render_coarse_opacity_count,
render_strand_raster_count,
render_high_quality_shadow_count,
render_high_quality_specular_count,
render_full_quality_count,
render_debug_certificate_count,

dropped_high_quality_shadow_count,
dropped_high_quality_specular_count,
dropped_full_quality_count,
dropped_debug_count,

mean_opacity_error_bound,
p95_opacity_error_bound,
mean_shadow_error_bound,
p95_shadow_error_bound,
mean_specular_error_bound,
p95_specular_error_bound,

render_mode_switch_count,
temporal_flicker_score,

physics_risk_render_boost_count,
edit_preview_full_quality_count,
repair_preview_full_quality_count,

overflow_count,
budget_drop_count,
cpu_readback_count
```

# 24. Evaluation protocol

## 24.1 Variants

```txt
V0_full_quality:
  全strandを高品質に描く品質上限。

V1_distance_lod:
  距離LODのみのbaseline。

V2_visibility_lod:
  visibility / coverageのみのbaseline。

V3_certificate_aware:
  certificateでpreferred modeを決める。

V4_budgeted_certificate_aware:
  本実装。certificate + error bound + hard cap + budgeted allocation。

V5_adaptive_budgeted_certificate_aware:
  V4にadaptive degradationを追加。

V6_gpu_resident_adaptive:
  V5を05 GPU-resident runtime上で実行する。
```

## 24.2 Scenarios

```txt
static_hair
camera_closeup
backlit_shadow
strong_specular_highlight
fast_head_motion
edit_preview_from_04
repair_visibility_from_02
mixed_01_02_03_04_05
```

## 24.3 Success conditions

```txt
1. p95_frame_ms <= 16.6 in budgeted/adaptive variants.
2. gpu_render_ms <= assigned render budget.
3. cpu_readback_count == 0 in GPU-resident path.
4. full_qualityより高速。
5. distance_lodより同じbudgetでp95 error boundが低い。
6. temporal_flicker_scoreがno-lock baselineより低い。
7. dropped task countをtelemetryに出す。
8. overflow_countが増えてもクラッシュしない。
```

# 25. Tests

```txt
TestOpacityErrorBound
TestShadowErrorBound
TestSpecularErrorBound
TestSilhouetteImportance
TestRenderCertificateBits
TestPreferredRenderModeSelection
TestBudgetedRenderModeSelection
TestRenderBenefitCostRanking
TestRenderTemporalLock
TestEmergencyModeDrop
TestRenderTaskCompaction
TestRenderBudgetOverflow
TestDebugCertificateVisualization
TestGpuCpuRenderModeConsistency
TestNoCpuReadbackInGpuResidentPath
TestRenderTelemetryCompleteness
```

# 26. 完了条件

```txt
1. strandごとにrender certificateを生成できる。
2. physics certificateとedit certificateをrender mode決定に使える。
3. opacity / shadow / specular / silhouette の誤差または重要度を持てる。
4. preferred modeとselected modeを分離できる。
5. render modeをGPU上でbudget内に割り当てられる。
6. render modeごとのtask listをcompactできる。
7. 高品質shadow/specular/full qualityを必要なstrandだけに適用できる。
8. temporal lockでmode切替のちらつきを抑えられる。
9. debug certificate overlayを評価時だけ表示できる。
10. gpu_render_ms、mode counts、error bounds、flicker score、dropped countsをtelemetryに出せる。
11. budgeted/adaptive variantで60FPS維持実験を実行できる。
```

# 27. 研究核

```txt
高コストな全strand高品質描画を、
certificate-driven, error-bound-driven, benefit-cost-ranked, budgeted render mode allocationへ変換する。
```
