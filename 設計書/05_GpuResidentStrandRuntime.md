# 05. GPU-Resident Strand Runtime 最終形仕様書

---
version: Final Shape v1.0
status: bachelor-thesis final system specification
language: C++17 + HLSL compute shader
runtime: DX11 viewer integration
thread_group_size: 128
mvp_strands: 100000
mvp_segments_per_strand: 128
mvp_points_per_strand: 129
future_target: 1000000+ strands / 20000+ segments via paged segment runtime
scope: final system shape, not production flow
---

## 0. この文書の位置づけ

この文書は、`05_GpuResidentStrandRuntime` が最終的にどのような研究・システム・成果物になるかを定義する。

ここでは、制作手順、実装順、開発フロー、AIコード生成プロンプト、卒論の章立ては扱わない。

この文書が定義するのは、次の5点である。

```txt
1. 最終的な研究主張
2. 完成時のruntimeの構造
3. 完成時に存在するデータ・buffer・shader・telemetry
4. 卒研MVPとして評価する固定スケール
5. 論文の今後の展望として置く大規模拡張の形
```

---

## 1. 最終的な研究主張

### 1.1 一文主張

毎 frame 変動する髪 strand task を、CPU readback なしに GPU 上で生成・compact・indirect dispatch・commit/fallback する **GPU-resident strand runtime** を実装し、CPU task build baseline と比較して frame time の安定性を評価する。

### 1.2 研究の中心

この研究の中心は、髪物理 solver そのものではない。

中心は、以下のような可変数 task の制御を GPU 上に閉じる runtime である。

```txt
- risky strand
- repair task
- fallback target
- candidate state
- persistent state commit
- delayed telemetry
```

従来型では、GPU で計算された task 数や risk 情報を CPU に readback し、CPU が task list や dispatch 数を組み立てる。

本研究では、risk 生成、risky strand の compact、repair task の compact、DispatchIndirect args 生成、dummy repair、commit 判定、telemetry 書き込みを GPU 側に置く。

CPU は frame 内で task 数を読む必要がなく、評価用の delayed telemetry のみを読む。

### 1.3 主張すること

```txt
1. 髪 strand に対する可変数 task 制御を GPU-resident に閉じられる。
2. GPU-resident path では frame 内 CPU readback を 0 にできる。
3. CPU readback baseline と比較して、task 数が sparse / fluctuating な条件で p95 / p99 frame time の改善が期待できる。
4. zero-task、dense-task、overflow-task でも破綻しない runtime 契約を持てる。
5. 卒研MVPとしては synthetic risk と dummy repair で成立する。
```

### 1.4 主張しないこと

```txt
1. 新しい髪物理 solver を提案したとは主張しない。
2. dummy repair の物理的正しさは主張しない。
3. DispatchIndirect そのものを新規技術とは主張しない。
4. 01/02/03/04 の完成を前提にしない。
5. telemetry に出していない性能を主張しない。
6. MVPで100万本・2万segment超をdense配列で処理できるとは主張しない。
```

---

## 2. 最終的に完成するシステム

最終的に完成するものは、DX11 viewer に統合された GPU-resident hair task runtime である。

完成時のruntimeは、次の要素を持つ。

```txt
GpuResidentRuntime
  - RuntimeSettings
  - RuntimeFrameInput
  - RuntimeFeatureFlags
  - persistent strand state buffer
  - candidate strand state buffer
  - last stable strand state buffer
  - synthetic risk buffer
  - risky strand buffer
  - mode decision buffer
  - repair task buffer
  - runtime counters buffer
  - DispatchIndirect args buffer
  - delayed telemetry ring buffer
  - GPU resident execution path
  - CPU task build baseline path
  - CSV telemetry output
```

MVPでは、本物の物理 solver は入れない。

MVPの repair は dummy repair でよい。dummy repair は、可変数 task を GPU 上で生成し、indirect dispatch し、candidate state を書き込み、commit まで行うための評価用 kernel である。

---

## 3. 完成時のruntime構造

### 3.1 runtime全体像

```txt
GpuResidentRuntime

Input:
  - frame index
  - strand count
  - risk threshold
  - persistent strand states
  - runtime settings

GPU-resident runtime state:
  - synthetic risk values
  - compacted risky strands
  - mode decisions
  - compacted repair tasks
  - indirect dispatch arguments
  - candidate strand states
  - runtime counters
  - delayed telemetry

Output:
  - updated persistent strand states
  - last stable strand states
  - telemetry CSV rows
```

### 3.2 2つの評価path

本研究の完成形には、必ず2つのpathが存在する。

```txt
Path A: CPU task build baseline
  GPUでriskを計算する。
  CPUがtask数またはrisk情報をreadbackする。
  CPUがtask listまたはdispatch数を作る。
  CPUがrepair dispatchを発行する。

Path B: GPU-resident proposed path
  GPUがriskを計算する。
  GPUがrisky strandをcompactする。
  GPUがrepair taskをcompactする。
  GPUがDispatchIndirect argsを作る。
  GPUがdummy repairをindirect dispatchする。
  GPUがcandidate stateをcommit/fallbackする。
  CPUはdelayed telemetryだけ読む。
```

卒論で比較するのは、この2つの path の frame time、CPU readback count、overflow safety、task count 変動への安定性である。

---

## 4. MVPスケール

### 4.1 固定スケール

卒研MVPでは、以下を固定前提とする。

```txt
strand_count: 100,000
segments_per_strand: 128
points_per_strand: 129
thread_group_size: 128
state_representation: dense fixed-length StrandStateMvp
primary_goal: GPU-resident variable task control
```

MVPでは、1 strand あたり 129 点を固定長で持つ。1点は `float3` とする。

### 4.2 MVPメモリ規模

```txt
1 state buffer:
  100,000 strands * 129 points * 12 bytes
  = 154,800,000 bytes
  = 約154.8 MB

3 state buffers:
  persistent + candidate + lastStable
  = 約464.4 MB
```

この3 state buffer は、以下の意味を持つ。

```txt
persistent:
  現在描画・評価に使う安定状態

candidate:
  dummy repair や task 処理後の候補状態

lastStable:
  fallback 用の最後に安定していた状態
```

---

## 5. 完成時の主要データ型

### 5.1 定数

```cpp
namespace hairgpu {
static constexpr uint32_t kThreadsPerGroup = 128;
static constexpr uint32_t kMaxSegmentsPerStrandMvp = 128;
static constexpr uint32_t kMaxPointsPerStrandMvp = 129;
static constexpr uint32_t kInvalidU32 = 0xffffffffu;
static constexpr float kEpsilon = 1.0e-6f;
}
```

### 5.2 RuntimeFeatureFlags

MVPでは `enableGpuResident` のみを主対象とする。他の flag は将来接続用に存在する。

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

}
```

### 5.3 RuntimeSettings

```cpp
namespace hairgpu::runtime {

struct RuntimeSettings
{
    uint32_t strandCount = 100000;
    uint32_t segmentsPerStrand = 128;
    uint32_t pointsPerStrand = 129;

    uint32_t maxRiskyStrands = 65536;
    uint32_t maxRepairTasks = 65536;
    uint32_t maxIslandTasks = 0;
    uint32_t maxIslandMembersTotal = 0;

    float frameBudgetMs = 16.6f;
    float gpuHairBudgetMs = 2.0f;
    float riskThreshold = 0.5f;

    RuntimeFeatureFlags flags;
};

}
```

`maxRiskyStrands` と `maxRepairTasks` は別の capacity として扱う。

```txt
maxRiskyStrands:
  risky strand compact buffer の上限

maxRepairTasks:
  repair task buffer の上限
```

### 5.4 RuntimeFrameInput

```cpp
namespace hairgpu::runtime {

struct RuntimeFrameInput
{
    uint32_t frameIndex;
    float deltaTimeSec;
    float timeSec;
    uint32_t seed;
};

}
```

### 5.5 RuntimeCounters

```cpp
namespace hairgpu::runtime {

struct RuntimeCounters
{
    uint32_t riskyStrandCount;
    uint32_t repairTaskCount;
    uint32_t fallbackCount;
    uint32_t overflowCount;
    uint32_t cpuReadbackCount;
};

}
```

MVPでは island 関連 counter は含めない。必要な場合のみ将来拡張で追加する。

### 5.6 RiskyStrandEntry

```cpp
namespace hairgpu::runtime {

struct RiskyStrandEntry
{
    uint32_t strandId;
    float risk;
    uint32_t reasonBits;
    uint32_t pad;
};

}
```

### 5.7 RepairTaskMvp

```cpp
namespace hairgpu::runtime {

struct RepairTaskMvp
{
    uint32_t taskId;
    uint32_t strandId;
    float risk;
    uint32_t mode;
};

}
```

### 5.8 StrandStateMvp

```cpp
namespace hairgpu::runtime {

struct StrandStateMvp
{
    float p[129][3];
};

}
```

### 5.9 DispatchArgs

DX11 の `DispatchIndirect` 用に、3つの `uint32_t` を持つ。

```cpp
namespace hairgpu::runtime {

struct DispatchArgs
{
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
};

}
```

### 5.10 RuntimeTelemetry

```cpp
namespace hairgpu::runtime {

struct RuntimeTelemetry
{
    uint32_t frameIndex;
    uint32_t variant;
    uint32_t scenario;
    uint32_t seed;

    float frameMs;
    float gpuHairMs;
    float cpuTaskBuildMs;

    uint32_t strandCount;
    uint32_t riskyStrandCount;
    uint32_t repairTaskCount;
    uint32_t fallbackCount;
    uint32_t overflowCount;
    uint32_t cpuReadbackCount;

    float meanRisk;
    float maxRisk;
};

}
```

---

## 6. 完成時に存在するGPU buffer

完成時の `RuntimeBuffers` は以下を持つ。

```txt
strandStatesA:
  persistent state または ping-pong state

strandStatesB:
  persistent state または ping-pong state

candidateStates:
  repair task 後の候補状態

lastStableStates:
  fallback 用状態

syntheticRisks:
  strandごとのrisk値

riskyStrands:
  compact後のrisky strand list

modeDecisions:
  各strandまたは各risky strandの処理mode

repairTasks:
  compact後のrepair task list

runtimeCounters:
  task数、overflow数、fallback数、readback数

repairDispatchArgs:
  DispatchIndirect用args

evaluationRing:
  delayed telemetry用ring buffer
```

---

## 7. 完成時に存在するshader

MVP完成時には、以下の compute shader が存在する。

```txt
ClearRuntimeCountersCS:
  runtimeCountersを初期化する。

GenerateSyntheticRiskCS:
  strandごとのsynthetic riskを生成する。

CompactRiskyStrandsCS:
  risk thresholdを超えるstrandをriskyStrandsへcompactする。
  capacityはmaxRiskyStrandsを使う。

FixedModeDecisionCS:
  risky strandに対してMVP用の固定modeを割り当てる。

BuildRepairTasksCS:
  mode decisionからrepairTasksを作る。
  capacityはmaxRepairTasksを使う。

BuildRepairDispatchArgsCS:
  repairTaskCountからDispatchIndirect argsを作る。

DummyRepairTaskCS:
  repairTasksに対してdummy repairを実行しcandidateStatesを書く。

CommitAcceptedStatesCS:
  candidateStatesをpersistentへcommitする。
  fallback条件ではlastStableStatesを使う。

WriteEvaluationCountersCS:
  delayed telemetry ringへ評価用counterを書く。
```

---

## 8. 完成時のruntime契約

完成時のruntimeは、次の契約を満たす。

```txt
1. GPU-resident path では frame 内 CPU readback を行わない。
2. CPU readback は delayed telemetry のみに限定する。
3. zero repair task は正常ケースとして扱う。
4. repairTaskCount = 0 の場合、DispatchIndirect args は {0, 1, 1} になる。
5. risky strand compact のoverflowはruntimeCounters.overflowCountに記録する。
6. repair task compact のoverflowもruntimeCounters.overflowCountに記録する。
7. overflow時もbuffer外書き込みを行わない。
8. maxRiskyStrands と maxRepairTasks は混同しない。
9. candidate state と persistent state は分離する。
10. telemetry に存在しない性能主張はしない。
```

---

## 9. 完成時の比較対象

卒研評価では、次の2 variant を比較する。

```txt
V0: CPU task build baseline
V1: GPU-resident proposed path
```

### 9.1 V0: CPU task build baseline

V0は、CPU readback を含む比較対象である。

```txt
特徴:
  - CPUがtask数またはrisk情報を読む
  - CPUがtask list または dispatch数を決める
  - CPU readback count > 0
```

### 9.2 V1: GPU-resident proposed path

V1は、本研究の提案pathである。

```txt
特徴:
  - GPUがtask listを作る
  - GPUがDispatchIndirect argsを作る
  - CPUはframe内でtask数を読まない
  - CPU readback count == 0
```

---

## 10. 完成時の評価条件

### 10.1 評価scenario

```txt
zero_task:
  risk thresholdが高く、repair taskがほぼ発生しない条件

sparse_task:
  少数のstrandのみtaskになる条件

medium_task:
  中程度のtask数が発生する条件

dense_task:
  多数のstrandがtaskになる条件

overflow_task:
  capacityを超えるtask候補が発生する条件

fluctuating_task_count:
  frameごとにtask数が大きく変動する条件
```

### 10.2 評価metric

```txt
frame_ms
p50_frame_ms
p95_frame_ms
p99_frame_ms
gpu_hair_ms
cpu_task_build_ms
cpu_readback_count
risky_strand_count
repair_task_count
fallback_count
overflow_count
vram_mb
bandwidth_mb_frame
```

### 10.3 成功条件

```txt
1. V1 の cpu_readback_count が 0 である。
2. V0 の cpu_readback_count が 0 より大きい。
3. V1 が zero / sparse / fluctuating 条件で V0 より p95 または p99 frame time を改善する。
4. overflow_task でクラッシュせず、overflow_count が記録される。
5. zero_task で repairTaskCount = 0 でも正常終了する。
6. telemetry CSV だけで主要な性能主張を再現できる。
```

---

## 11. 卒研としての最終到達点

卒研としての完成形は、以下である。

```txt
1. 10万本 × 128 segment/strand のdense fixed-length hair stateを持つ。
2. synthetic risk により可変数strand taskを発生させる。
3. risky strand list と repair task list をGPU上でcompactする。
4. repair task count からGPU上でDispatchIndirect argsを作る。
5. dummy repair kernelをDispatchIndirectで実行する。
6. candidate state と persistent state を分離する。
7. GPU-resident pathでCPU readback count = 0を満たす。
8. CPU task build baselineと比較し、p95 / p99 frame timeを評価する。
9. zero-task / overflow-task / fluctuating-task-count を含む評価を行う。
```

この到達点で、学士卒研としての新規性は十分成立する。

---

## 12. 今後の展望としての最終形

### 12.1 大規模目標

論文の今後の展望では、以下を目標として置く。

```txt
strand_count >= 1,000,000
segments_per_strand > 20,000
```

ただし、この規模は MVP の dense fixed-length 配列では扱わない。

```txt
1,000,000 strands * 20,001 points * 12 bytes
= 約240 GB / 1 state buffer
```

persistent、candidate、lastStable を持つと数百GB級になるため、dense 表現は現実的ではない。

### 12.2 将来版の表現

将来版では、髪全体を固定長dense配列として持つのではなく、segmentをpage単位で管理する。

```txt
StrandSegmentPage:
  連続segmentの小さなpage

StrandPageTable:
  strandIdからsegment page群を参照するtable

ActiveSegmentWindow:
  現在frameで物理・編集・描画に必要な範囲だけをmaterializeするwindow

GpuResidentPageAllocator:
  GPU上で必要なpageを確保・再利用するallocator

PagedTelemetry:
  page miss、active page count、streaming costを記録するtelemetry
```

### 12.3 将来版の研究方向

将来の研究では、05のGPU-resident task runtimeを、次のように拡張する。

```txt
1. strand単位taskからsegment page単位taskへ拡張する。
2. 全segmentを常時materializeせず、必要なactive windowだけを扱う。
3. 01のcertificate付きstrand data typeと接続し、decode前に必要pageを判定する。
4. 02のphysics islandと接続し、危険pageだけをbounded solveする。
5. 04のper-strand editingと接続し、編集されたsegment周辺だけをpage化して更新する。
6. 03のrenderingと接続し、見えている・重要なpageだけを高品質に扱う。
```

### 12.4 卒論での書き方

```txt
本研究のMVPは、10万本・128 segment/strand の固定長dense表現で、GPU-resident task control の有効性を評価した。
一方で、100万本以上・2万segment超の大規模髪では、dense state buffer は数百GB級となり現実的でない。
そのため、今後は strand / segment をpage化し、active window のみをGPU上にmaterializeする paged GPU-resident strand runtime へ拡張する。
```

---

## 13. 最終要約

最終的な05は、次のような研究である。

```txt
10万本・128 segment/strand の髪状態に対して、毎frame変動するstrand taskをGPU上で生成・compact・DispatchIndirect・commitするruntimeを実装する。
CPU task build baselineと比較し、CPU readbackを排除したGPU-resident pathがframe timeの安定性に与える影響を評価する。
MVPではsynthetic riskとdummy repairを用い、髪物理solver自体の新規性は主張しない。
今後の展望として、100万本以上・2万segment超の髪に対し、segment pageとactive windowを用いたpaged GPU-resident runtimeへ拡張する。
```
