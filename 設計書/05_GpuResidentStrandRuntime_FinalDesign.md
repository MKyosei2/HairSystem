# 05. GPU-Resident Strand Runtime 最終版 企画書兼実装設計書


---
version: Final Design v1.0
status: implementation-ready
language: C++17 + HLSL compute shader
primary_runtime: DX11 viewer integration
thread_group_size: 128
max_points_per_strand_mvp: 32
---

# 共通実装契約

この文書は、研究アイデアの説明ではなく、実装者がそのままファイル作成・型定義・shader実装・テストに入るための最終版である。最初の実装ではMVP固定仕様を守り、Phase 2以降の拡張を混ぜない。

## 共通ルール

```txt
1. まずMVPだけを実装する。
2. GPU版の前にCPU debug版を作る。ただし05はGPU resident pathが主対象なのでCPU baselineも同時に作る。
3. すべてのcounterはoverflow時の挙動を持つ。
4. candidate stateとpersistent stateを分離する。
5. CPU readback禁止pathと評価用delayed readbackを分ける。
6. telemetryに出ない性能主張はしない。
7. undefined helperを残さない。MVPでは固定式・stub・no-opのどれかを明記する。
```

## 共通C++定数

```cpp
#pragma once
#include <cstdint>

namespace hairgpu {
static constexpr uint32_t kThreadsPerGroup = 128;
static constexpr uint32_t kMaxPointsPerStrand = 32;
static constexpr uint32_t kMaxSegmentsPerStrand = kMaxPointsPerStrand - 1;
static constexpr uint32_t kInvalidU32 = 0xffffffffu;
static constexpr float kEpsilon = 1.0e-6f;
}
```

## 共通HLSL定数

```hlsl
#define HAIRGPU_THREADS_PER_GROUP 128
#define HAIRGPU_MAX_POINTS_PER_STRAND 32
#define HAIRGPU_MAX_SEGMENTS_PER_STRAND 31
#define HAIRGPU_INVALID_U32 0xffffffffu
#define HAIRGPU_EPSILON 1.0e-6f
```

## 共通telemetry列

```txt
case_id, variant, scenario, seed,
p50_frame_ms, p95_frame_ms, p99_frame_ms,
gpu_hair_ms, vram_mb, bandwidth_mb_frame,
certificate_failure_count, certificate_failure_rate,
fallback_count, fallback_rate,
mode_skip_count, mode_qs_count, mode_dynamic_count,
mode_island_count, mode_repair_count, mode_float_fallback_count,
island_count, island_member_count, island_commit_count, island_reject_count,
repair_task_count, repair_success_rate,
cpu_readback_count, overflow_count,
mean_decode_error_mm, max_penetration_mm
```


# 1. 研究概要

## 1.1 一文要約

危険strand、repair task、island task、fallback対象のような毎frame変動するtaskを、CPU readbackなしにGPU上で生成・dispatch・commit/fallbackするruntimeを実装する。

## 1.2 研究レベル

```txt
synthetic risk + dummy task MVP: 学士卒研として十分
04/01と接続した実task runtime: 修士レベル
GPU scheduling一般化と複数workload比較: 博士レベル
```

# 2. 卒研としての主張

```txt
従来path:
  GPU computes risk
  CPU readback task count
  CPU builds task list
  CPU dispatches repair/island task

提案path:
  GPU computes risk
  GPU compacts task list
  GPU builds DispatchIndirect args
  GPU solves tasks
  GPU commits or falls back
  CPU reads delayed telemetry only
```

主張は「髪物理solverそのもの」ではなく、可変数strand taskの制御ループをGPU上に閉じることでp95/p99 frame timeを改善することである。

# 3. MVP固定仕様

## 3.1 最初に実装するもの

```txt
RuntimeBuffers
RuntimeCounters GPU buffer
synthetic risk generation
risky strand compaction
repair task compaction
GPU-built DispatchIndirect args
dummy repair kernel
candidate/persistent commit
delayed telemetry
CPU task build baseline
```

## 3.2 MVPでは作らないもの

```txt
本物の01 Microtype
本物の02 CBMA
本物の03 island solver
本物の04 rod repair
```

# 4. ファイル構成

```txt
include/hairgpu/runtime/GpuResidentRuntime.h
include/hairgpu/runtime/GpuResidentRuntimeTypes.h
src/hairgpu_runtime/GpuResidentRuntime.cpp
src/hairgpu_runtime/RuntimeBuffers.cpp
src/hairgpu_runtime/RuntimeTelemetry.cpp
src/hairgpu_runtime/CpuTaskBuildBaseline.cpp
apps/dx11_viewer/shaders/RuntimeCommon.hlsli
apps/dx11_viewer/shaders/ClearRuntimeCountersCS.hlsl
apps/dx11_viewer/shaders/GenerateSyntheticRiskCS.hlsl
apps/dx11_viewer/shaders/CompactRiskyStrandsCS.hlsl
apps/dx11_viewer/shaders/FixedSafeModeRuleCS.hlsl
apps/dx11_viewer/shaders/BuildRepairTasksCS.hlsl
apps/dx11_viewer/shaders/BuildRepairDispatchArgsCS.hlsl
apps/dx11_viewer/shaders/DummyRepairTaskCS.hlsl
apps/dx11_viewer/shaders/CommitAcceptedStatesCS.hlsl
apps/dx11_viewer/shaders/WriteEvaluationCountersCS.hlsl
tools/final_stage/analyze_gpu_resident_runtime.py
tests/TestGpuResidentRuntime.cpp
```

# 5. C++データ構造

```cpp
namespace hairgpu::runtime {

struct RuntimeFeatureFlags
{
    uint32_t enableMicrotype = 0;
    uint32_t enableCBMA = 0;
    uint32_t enablePhysicsIslands = 0;
    uint32_t enableRodRepair = 0;
    uint32_t enableGpuResident = 1;
};

struct RuntimeSettings
{
    uint32_t strandCount = 0;
    uint32_t pointCount = 32;
    uint32_t maxRiskyStrands = 65536;
    uint32_t maxIslandTasks = 4096;
    uint32_t maxIslandMembersTotal = 65536;
    uint32_t maxRepairTasks = 65536;
    uint32_t maxRenderVertices = 0;
    float frameBudgetMs = 16.6f;
    float gpuHairBudgetMs = 2.0f;
    float contactRadius = 0.01f;
    float riskThreshold = 0.5f;
    RuntimeFeatureFlags flags;
};

struct DispatchArgs
{
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
};

struct RuntimeCounters
{
    uint32_t riskyStrandCount;
    uint32_t islandTaskCount;
    uint32_t islandMemberCount;
    uint32_t repairTaskCount;
    uint32_t fallbackCount;
    uint32_t overflowCount;
    uint32_t cpuReadbackCount;
};

struct RiskyStrandEntry
{
    uint32_t strandId;
    float risk;
    uint32_t reasonBits;
    uint32_t pad;
};

struct RepairTaskMvp
{
    uint32_t taskId;
    uint32_t strandId;
    float risk;
    uint32_t mode;
};

struct StrandStateMvp
{
    float p[32][3];
};

}
```

# 6. RuntimeBuffers

```cpp
struct RuntimeBuffers
{
    GPUBuffer strandStatesA;
    GPUBuffer strandStatesB;
    GPUBuffer candidateStates;
    GPUBuffer lastStableStates;

    GPUBuffer syntheticRisks;
    GPUBuffer modeDecisions;
    GPUBuffer riskyStrands;
    GPUBuffer repairTasks;

    GPUBuffer runtimeCounters;
    GPUBuffer repairDispatchArgs;
    GPUBuffer evaluationRing;
};
```

# 7. C++ runtime API

```cpp
class GpuResidentRuntime
{
public:
    bool Initialize(const RuntimeSettings& settings);
    void Shutdown();
    void Resize(uint32_t strandCount);
    void ExecuteFrame(const RuntimeFrameInput& input);
    void ExecuteCpuTaskBuildBaseline(const RuntimeFrameInput& input);
    RuntimeTelemetry ReadTelemetryDelayed(uint32_t frameIndex);

private:
    void CreateBuffers();
    void BindRuntimeBuffers();
    void UploadFrameConstants(const RuntimeFrameInput& input);
    void DispatchClearCounters();
    void DispatchGenerateSyntheticRisk();
    void DispatchCompactRiskyStrands();
    void DispatchFixedSafeModeRule();
    void DispatchBuildRepairTasks();
    void DispatchBuildRepairArgs();
    void DispatchDummyRepairIndirect();
    void DispatchCommitAcceptedStates();
    void DispatchWriteEvaluationCounters();
    void SwapStateBuffers();
};
```

# 8. Frame algorithm

```cpp
void GpuResidentRuntime::ExecuteFrame(const RuntimeFrameInput& input)
{
    BindRuntimeBuffers();
    UploadFrameConstants(input);

    DispatchClearCounters();
    DispatchGenerateSyntheticRisk();
    DispatchFixedSafeModeRule();
    DispatchCompactRiskyStrands();
    DispatchBuildRepairTasks();
    DispatchBuildRepairArgs();
    DispatchDummyRepairIndirect();
    DispatchCommitAcceptedStates();
    DispatchWriteEvaluationCounters();

    DrawHair();
    SwapStateBuffers();
}
```

MVPのCPU側はtask countをreadbackしてdispatch数を決めない。`DispatchDummyRepairIndirect`ではGPUが作った`repairDispatchArgs`を使う。

# 9. HLSL実装

## 9.1 RuntimeCommon.hlsli

```hlsl
#define MODE_SKIP 0u
#define MODE_QS_ONLY 1u
#define MODE_DYNAMIC_RESIDUAL 2u
#define MODE_PHYSICS_ISLAND 3u
#define MODE_ROD_REPAIR 4u
#define MODE_FLOAT_FALLBACK 5u

struct RuntimeCountersGpu
{
    uint riskyStrandCount;
    uint islandTaskCount;
    uint islandMemberCount;
    uint repairTaskCount;
    uint fallbackCount;
    uint overflowCount;
    uint cpuReadbackCount;
};

struct RiskyStrandEntryGpu
{
    uint strandId;
    float risk;
    uint reasonBits;
    uint pad;
};

struct RepairTaskMvpGpu
{
    uint taskId;
    uint strandId;
    float risk;
    uint mode;
};

struct DispatchArgsGpu
{
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};
```

## 9.2 ClearRuntimeCountersCS.hlsl

```hlsl
RWStructuredBuffer<RuntimeCountersGpu> g_Counters : register(u0);

[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    RuntimeCountersGpu c;
    c.riskyStrandCount = 0;
    c.islandTaskCount = 0;
    c.islandMemberCount = 0;
    c.repairTaskCount = 0;
    c.fallbackCount = 0;
    c.overflowCount = 0;
    c.cpuReadbackCount = 0;
    g_Counters[0] = c;
}
```

## 9.3 GenerateSyntheticRiskCS.hlsl

```hlsl
cbuffer RuntimeSettingsCB : register(b0)
{
    uint g_StrandCount;
    uint g_FrameIndex;
    float g_RiskThreshold;
    uint g_MaxRepairTasks;
};

RWStructuredBuffer<float> g_SyntheticRisks : register(u0);

float Hash01(uint x)
{
    x ^= x >> 17;
    x *= 0xed5ad4bbU;
    x ^= x >> 11;
    x *= 0xac4c1b51U;
    x ^= x >> 15;
    return (float)(x & 0x00ffffffu) / 16777215.0f;
}

[numthreads(128,1,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint strandId = tid.x;
    if (strandId >= g_StrandCount) return;
    g_SyntheticRisks[strandId] = Hash01(strandId * 1664525u + g_FrameIndex * 1013904223u);
}
```

## 9.4 CompactRiskyStrandsCS.hlsl

```hlsl
StructuredBuffer<float> g_SyntheticRisks : register(t0);
RWStructuredBuffer<RiskyStrandEntryGpu> g_RiskyStrands : register(u0);
RWStructuredBuffer<RuntimeCountersGpu> g_Counters : register(u1);

[numthreads(128,1,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint strandId = tid.x;
    if (strandId >= g_StrandCount) return;

    float risk = g_SyntheticRisks[strandId];
    if (risk < g_RiskThreshold) return;

    uint index;
    InterlockedAdd(g_Counters[0].riskyStrandCount, 1, index);
    if (index >= g_MaxRepairTasks)
    {
        InterlockedAdd(g_Counters[0].overflowCount, 1);
        return;
    }

    RiskyStrandEntryGpu e;
    e.strandId = strandId;
    e.risk = risk;
    e.reasonBits = 0;
    e.pad = 0;
    g_RiskyStrands[index] = e;
}
```

## 9.5 BuildRepairTasksCS.hlsl

```hlsl
StructuredBuffer<RiskyStrandEntryGpu> g_RiskyStrands : register(t0);
RWStructuredBuffer<RepairTaskMvpGpu> g_RepairTasks : register(u0);
RWStructuredBuffer<RuntimeCountersGpu> g_Counters : register(u1);

[numthreads(128,1,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint idx = tid.x;
    uint riskyCount = min(g_Counters[0].riskyStrandCount, g_MaxRepairTasks);
    if (idx >= riskyCount) return;

    RiskyStrandEntryGpu e = g_RiskyStrands[idx];
    uint taskIndex;
    InterlockedAdd(g_Counters[0].repairTaskCount, 1, taskIndex);
    if (taskIndex >= g_MaxRepairTasks)
    {
        InterlockedAdd(g_Counters[0].overflowCount, 1);
        return;
    }

    RepairTaskMvpGpu t;
    t.taskId = taskIndex;
    t.strandId = e.strandId;
    t.risk = e.risk;
    t.mode = MODE_ROD_REPAIR;
    g_RepairTasks[taskIndex] = t;
}
```

## 9.6 BuildRepairDispatchArgsCS.hlsl

```hlsl
RWStructuredBuffer<RuntimeCountersGpu> g_Counters : register(u0);
RWStructuredBuffer<DispatchArgsGpu> g_RepairDispatchArgs : register(u1);

[numthreads(1,1,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint taskCount = min(g_Counters[0].repairTaskCount, g_MaxRepairTasks);
    DispatchArgsGpu a;
    a.groupCountX = (taskCount + 127u) / 128u;
    a.groupCountY = 1u;
    a.groupCountZ = 1u;
    g_RepairDispatchArgs[0] = a;
}
```

## 9.7 DummyRepairTaskCS.hlsl

```hlsl
StructuredBuffer<RepairTaskMvpGpu> g_RepairTasks : register(t0);
StructuredBuffer<RuntimeCountersGpu> g_Counters : register(t1);
StructuredBuffer<StrandStateMvpGpu> g_ReadStates : register(t2);
RWStructuredBuffer<StrandStateMvpGpu> g_CandidateStates : register(u0);

[numthreads(128,1,1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint taskIndex = tid.x;
    if (taskIndex >= g_Counters[0].repairTaskCount) return;

    RepairTaskMvpGpu t = g_RepairTasks[taskIndex];
    StrandStateMvpGpu s = g_ReadStates[t.strandId];

    // MVP dummy: tipをほんの少し上げる。物理的意味は主張しない。
    s.p[31].y += 0.0001f * t.risk;
    g_CandidateStates[t.strandId] = s;
}
```

# 10. CPU baseline

```cpp
void GpuResidentRuntime::ExecuteCpuTaskBuildBaseline(const RuntimeFrameInput& input)
{
    // 評価用baselineなので、このpathではreadbackを明示的に数える。
    RuntimeCounters counters = ReadCountersBlocking();
    counters.cpuReadbackCount += 1;

    std::vector<RepairTaskMvp> tasks;
    tasks.reserve(counters.riskyStrandCount);
    for (uint32_t i = 0; i < counters.riskyStrandCount; ++i)
    {
        RiskyStrandEntry e = ReadRiskyStrandBlocking(i);
        tasks.push_back({static_cast<uint32_t>(tasks.size()), e.strandId, e.risk, MODE_ROD_REPAIR});
    }

    UploadRepairTasks(tasks.data(), static_cast<uint32_t>(tasks.size()));
    DispatchDummyRepairDirect(static_cast<uint32_t>((tasks.size() + 127) / 128));
}
```

# 11. delayed telemetry

```cpp
RuntimeTelemetry GpuResidentRuntime::ReadTelemetryDelayed(uint32_t frameIndex)
{
    const uint32_t latency = 4;
    const uint32_t readIndex = (frameIndex + kEvaluationRingSize - latency) % kEvaluationRingSize;
    RuntimeTelemetry out = {};
    if (MapWithoutStall(evaluationRing[readIndex], &out))
        return out;
    out.valid = false;
    return out;
}
```

# 12. 実装手順

```txt
Step 1: RuntimeSettings/RuntimeCounters/RuntimeBuffersを定義する。
Step 2: ClearRuntimeCountersCSを作る。
Step 3: GenerateSyntheticRiskCSを作る。
Step 4: CompactRiskyStrandsCSを作る。
Step 5: BuildRepairTasksCSを作る。
Step 6: BuildRepairDispatchArgsCSを作る。
Step 7: DummyRepairTaskCSをDispatchIndirectで呼ぶ。
Step 8: CommitAcceptedStatesCSを作る。
Step 9: WriteEvaluationCountersCSを作る。
Step 10: CPU task build baselineを作る。
Step 11: p95/p99比較scriptを作る。
```

# 13. テスト

```txt
TestRuntime_NoReadbackPath:
  GPU resident pathでcpuReadbackCount == 0。

TestRuntime_CompactionCount:
  syntheticRisk >= thresholdの数とriskyStrandCountが一致。

TestRuntime_IndirectArgs:
  repairTaskCount=NならgroupCountX==ceil(N/128)。

TestRuntime_ZeroTaskSafe:
  task数0でもクラッシュしない。

TestRuntime_OverflowSafe:
  maxRepairTasks超過でoverflow_countが増える。
```

# 14. 評価

```txt
baseline:
  CPU readback -> CPU task build -> direct dispatch

proposed:
  GPU compaction -> GPU-built dispatch args -> DispatchIndirect

metrics:
  cpu_readback_count
  riskyStrandCount
  repairTaskCount
  overflowCount
  gpu_hair_ms
  p50_frame_ms
  p95_frame_ms
  p99_frame_ms
```

# 15. 完了条件

```txt
[ ] GPU resident pathでcpu_readback_count == 0。
[ ] CPU baselineでcpu_readback_count > 0。
[ ] riskyStrandCountとrepairTaskCountがtelemetryに出る。
[ ] DispatchIndirectでdummy repairが呼ばれる。
[ ] 0 task/満杯/overflowでクラッシュしない。
[ ] 650 caseでp95/p99比較CSVを出せる。
```
