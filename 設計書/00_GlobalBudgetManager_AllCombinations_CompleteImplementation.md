# 00. 60FPS統合実験用 Global Budget Manager 完全実装設計書

---
version: Complete Implementation Design v2.0
status: implementation-ready
language: C++17 + HLSL compute shader
target_gpu: RTX 4090 class
target_frame_rate: 60 FPS
target_frame_budget_ms: 16.6
scope: 01/02/03/04/05 all-combination experiments
---

# 1. 実装目的

本設計は、01/02/03/04/05の単体および全組み合わせ実験で、60FPSを目指すための共通budget層である。

目的は以下である。

```txt
1. 各moduleの処理時間とtask数を同じ形式で管理する。
2. module単体でも組み合わせでもp95_frame_ms <= 16.6を目標にする。
3. 高コスト化したmoduleのcapを下げる。
4. 余裕があるmoduleにはquality budgetを戻す。
5. 何を処理し、何を落としたかをtelemetryに残す。
6. 05接続時はCPU readbackなしで可変taskを実行する。
```

# 2. Module role

```txt
01:
  decode前にsafe / repair / island / fallback / rejectを決める早期分岐。

02:
  高コストな全髪物理を、budgeted repair-first / island-laterに変換する。

03:
  高コストな全strand高品質描画を、budgeted render mode allocationに変換する。

04:
  編集操作をcertificate化し、対象strandとpreviewをbudget内に制限する。

05:
  可変taskをGPU residentにcompact / dispatch / commitし、CPU readbackを避ける。
```

# 3. Global budget structure

```cpp
struct GlobalHairFrameBudget
{
    float targetFrameMs = 16.6f;
    float totalHairBudgetMs = 8.0f;

    float dataBudgetMs = 1.0f;
    float physicsBudgetMs = 3.0f;
    float renderBudgetMs = 4.0f;
    float editBudgetMs = 1.0f;
    float runtimeBudgetMs = 0.8f;

    uint32_t maxDecodedStrands = 65536;

    uint32_t maxRiskyStrands = 16384;
    uint32_t maxRepairTasks = 8192;
    uint32_t maxIslandEdges = 32768;
    uint32_t maxIslandMembers = 16384;

    uint32_t maxRenderTasks = 65536;
    uint32_t maxFullQualityRenderStrands = 2048;
    uint32_t maxHighQualityShadowStrands = 4096;
    uint32_t maxHighQualitySpecularStrands = 4096;

    uint32_t maxEditOperations = 512;
    uint32_t maxEditRepairTasks = 2048;
    uint32_t maxEditPreviewFullQuality = 1024;

    uint32_t cpuReadbackAllowed = 0;
};
```

# 4. Module mask

```cpp
enum HairModuleMask : uint32_t
{
    MODULE_01_DATA    = 1u << 0,
    MODULE_02_PHYSICS = 1u << 1,
    MODULE_03_RENDER  = 1u << 2,
    MODULE_04_EDIT    = 1u << 3,
    MODULE_05_RUNTIME = 1u << 4,
};
```

# 5. Budget builder

```cpp
GlobalHairFrameBudget BuildBudgetForCombination(uint32_t mask)
{
    GlobalHairFrameBudget b = {};

    if ((mask & MODULE_01_DATA) == 0)
    {
        b.dataBudgetMs = 0.0f;
        b.maxDecodedStrands = 0;
    }

    if ((mask & MODULE_02_PHYSICS) == 0)
    {
        b.physicsBudgetMs = 0.0f;
        b.maxRiskyStrands = 0;
        b.maxRepairTasks = 0;
        b.maxIslandEdges = 0;
        b.maxIslandMembers = 0;
    }

    if ((mask & MODULE_03_RENDER) == 0)
    {
        b.renderBudgetMs = 0.0f;
        b.maxRenderTasks = 0;
        b.maxFullQualityRenderStrands = 0;
        b.maxHighQualityShadowStrands = 0;
        b.maxHighQualitySpecularStrands = 0;
    }

    if ((mask & MODULE_04_EDIT) == 0)
    {
        b.editBudgetMs = 0.0f;
        b.maxEditOperations = 0;
        b.maxEditRepairTasks = 0;
        b.maxEditPreviewFullQuality = 0;
    }

    if ((mask & MODULE_05_RUNTIME) == 0)
    {
        b.runtimeBudgetMs = 0.0f;
        b.cpuReadbackAllowed = 1;
    }

    if ((mask & MODULE_02_PHYSICS) &&
        (mask & MODULE_03_RENDER) &&
        (mask & MODULE_04_EDIT))
    {
        b.physicsBudgetMs = 2.5f;
        b.renderBudgetMs = 3.5f;
        b.editBudgetMs = 0.8f;

        b.maxRiskyStrands = 8192;
        b.maxRepairTasks = 4096;
        b.maxIslandEdges = 16384;
        b.maxIslandMembers = 8192;

        b.maxFullQualityRenderStrands = 1024;
        b.maxEditPreviewFullQuality = 512;
    }

    if (mask & MODULE_05_RUNTIME)
    {
        b.cpuReadbackAllowed = 0;
    }

    return b;
}
```

# 6. 全組み合わせ実験リスト

```txt
01
02
03
04
05

01/02
01/03
01/04
01/05
02/03
02/04
02/05
03/04
03/05
04/05

01/02/03
01/02/04
01/02/05
01/03/04
01/03/05
01/04/05
02/03/04
02/03/05
02/04/05
03/04/05

01/02/03/04
01/02/03/05
01/02/04/05
01/03/04/05
02/03/04/05

01/02/03/04/05
```

# 7. Execution variants

```txt
V0_quality_fixed:
  品質固定。高コスト化原因を測る。

V1_naive_threshold:
  単純threshold / distance LOD / basic active set。

V2_budgeted:
  hard capとmodule budgetで16.6ms以内を目指す。

V3_adaptive_budgeted:
  前frameのp95/p99、fallback率、error boundに応じてcapを動かす。

V4_gpu_resident_adaptive:
  05あり。CPU readbackなしでadaptive budgeted taskを実行する。
```

# 8. Combination-specific policy

## 8.1 02/03

```txt
physics risk / repair visibility がrender full qualityを増やす。
maxPhysicsRiskRenderBoost、maxRepairVisibleFullQuality、repairPreviewTTLで制限する。
```

## 8.2 02/04

```txt
edit certificate がrepair / island escalationを増やす。
maxEditIslandEscalations、maxEditRepairTasks、soft reject、preview-only、deferで制限する。
```

## 8.3 03/04

```txt
edit preview がfull quality renderを増やす。
maxEditPreviewFullQuality、preview TTL、importance rankingで制限する。
```

## 8.4 02/03/04

```txt
04 edit -> 02 physics escalation -> 03 full quality boost の連鎖を制限する。
new island cap、full quality cap、edit preview capを下げる。
p95 > 16.6msならislandよりrepair/fallbackを優先する。
```

## 8.5 01/02/03/04

```txt
05がないためCPU readback_countを明示的に測る。
V0/V1/V2 baselineとして扱い、05あり構成との差を見る。
```

## 8.6 01/02/03/04/05

```txt
05を共通runtimeにする。
01で早期分岐する。
02/03/04はglobal budgetからcapを受け取る。
CPU readback_count == 0を必須にする。
16.6ms超過時は品質を落とす。
```

# 9. Adaptive global budget

```cpp
void AdaptGlobalBudget(
    const GlobalHairTelemetry& prev,
    GlobalHairFrameBudget* b)
{
    if (prev.p95FrameMs > 16.6f)
    {
        b->maxRiskyStrands = std::max(4096u, b->maxRiskyStrands * 3 / 4);
        b->maxRepairTasks = std::max(2048u, b->maxRepairTasks * 3 / 4);
        b->maxIslandEdges = std::max(8192u, b->maxIslandEdges * 3 / 4);
        b->maxIslandMembers = std::max(4096u, b->maxIslandMembers * 3 / 4);

        b->maxFullQualityRenderStrands =
            std::max(512u, b->maxFullQualityRenderStrands * 3 / 4);

        b->maxHighQualityShadowStrands =
            std::max(1024u, b->maxHighQualityShadowStrands * 3 / 4);

        b->maxHighQualitySpecularStrands =
            std::max(1024u, b->maxHighQualitySpecularStrands * 3 / 4);

        b->maxEditOperations =
            std::max(64u, b->maxEditOperations * 3 / 4);

        b->maxEditPreviewFullQuality =
            std::max(256u, b->maxEditPreviewFullQuality * 3 / 4);
    }

    if (prev.p95FrameMs < 14.0f && prev.qualityDebtHigh)
    {
        b->maxRiskyStrands += 512;
        b->maxRepairTasks += 512;
        b->maxFullQualityRenderStrands += 128;
    }
}
```

# 10. Global execution algorithm

```cpp
void GlobalHairRuntime::ExecuteFrame(const HairFrameInput& input)
{
    uint32_t mask = input.enabledModuleMask;

    GlobalHairFrameBudget budget = BuildBudgetForCombination(mask);
    AdaptGlobalBudget(previousTelemetry, &budget);

    UploadGlobalBudget(budget);

    if (mask & MODULE_05_RUNTIME)
        BeginGpuResidentFrame();
    else
        BeginBaselineFrame();

    if (mask & MODULE_01_DATA)
        ExecuteDataCertificatePass(input, budget);

    if (mask & MODULE_04_EDIT)
        ExecuteEditCertificatePass(input, budget);

    if (mask & MODULE_02_PHYSICS)
        ExecuteBudgetedPhysicsPass(input, budget);

    if (mask & MODULE_03_RENDER)
        ExecuteBudgetedRenderPass(input, budget);

    if (mask & MODULE_05_RUNTIME)
        EndGpuResidentFrame();

    WriteGlobalTelemetry(input.caseId, mask, budget);
}
```

# 11. Global telemetry

```txt
case_id,
combination_mask,
variant,
scenario,
seed,
frame,

p50_frame_ms,
p95_frame_ms,
p99_frame_ms,
gpu_hair_ms,

data_ms,
physics_ms,
render_ms,
edit_ms,
runtime_ms,

cpu_readback_count,
overflow_count,
budget_drop_count,
fallback_count,
defer_count,

safe_decode_count,
typed_repair_count,
island_escalation_count,
float_fallback_count,
reject_payload_count,
false_safe_count,

risky_strand_count,
repair_task_count,
island_edge_count,
island_count,
island_member_count,
island_commit_count,
island_reject_count,

render_skip_count,
render_coarse_opacity_count,
render_strand_raster_count,
render_high_quality_shadow_count,
render_high_quality_specular_count,
render_full_quality_count,
render_dropped_full_quality_count,

edit_operation_count,
edit_repair_task_count,
edit_island_escalation_count,
edit_commit_count,
edit_reject_count,

max_penetration_mm,
p95_shadow_error_bound,
p95_specular_error_bound,
temporal_flicker_score
```

# 12. Success conditions

```txt
1. 全組み合わせでV0_quality_fixedを実行し、高コスト化原因を測定できる。
2. 全組み合わせでV2_budgetedを実行できる。
3. 05ありの組み合わせではcpu_readback_count == 0を満たす。
4. budget_drop/fallback/deferをtelemetryに出す。
5. 60FPS主張はp95_frame_ms <= 16.6を基準にする。
6. 品質劣化はerror bound、penetration、certificate failure、flickerで測る。
7. p95/p99だけでなくfallback率とdropped task数も報告する。
```

# 13. Tests

```txt
TestBuildBudgetForCombination
TestModuleMaskParsing
TestBudgetDisableInactiveModules
TestBudgetFor020304
TestBudgetForAllModules
TestAdaptiveGlobalBudgetOverFrame
TestCpuReadbackPolicyWith05
TestGlobalTelemetryCompleteness
TestCombinationExperimentEnumeration
TestBudgetPropagationTo01
TestBudgetPropagationTo02
TestBudgetPropagationTo03
TestBudgetPropagationTo04
TestBudgetPropagationTo05
```

# 14. 完了条件

```txt
1. 31通りの組み合わせを列挙できる。
2. 組み合わせごとにGlobalHairFrameBudgetを生成できる。
3. 各moduleへbudgetを渡せる。
4. 05あり構成でCPU readback禁止pathを実行できる。
5. p95_frame_ms > 16.6時にcapを下げられる。
6. p95_frame_ms < 14.0かつquality debtが高い時にcapを戻せる。
7. 全moduleのtelemetryを1つのCSVへ統合できる。
8. 60FPS維持率、fallback率、budget_drop率を組み合わせ別に集計できる。
```

# 15. 研究核

```txt
全組み合わせで最高品質固定の60FPSを保証するのではなく、
certificate / risk / error bound に基づいて処理対象と品質をbudget内へ制限し、
落とした処理をtelemetryに出すことで、
60FPS維持と品質劣化のトレードオフを実験可能にする。
```
