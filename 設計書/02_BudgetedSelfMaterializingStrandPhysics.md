# 02. 髪物理計算系：Budgeted Self-Materializing Strand Physics 完全実装設計書

---
version: Complete Implementation Design v3.0
status: implementation-ready
language: C++17 + HLSL compute shader
primary_runtime: GPU hair runtime / DX11-compatible compute path
novelty_category: budgeted_strand_physics_system
target_gpu: RTX 4090 class
target_frame_rate: 60 FPS
target_frame_budget_ms: 16.6
module_budget_target_ms: 3.0
---

# 1. 実装新規性の一文

全髪を毎frame物理計算するのではなく、**certificate / contact / edit / motion risk から必要なstrandだけをGPU上で抽出し、segment-level typed repairを優先し、repair不能な場合のみbounded physics islandへ昇格し、frame budget内で改善したcandidateだけをpersistent stateへcommitする budgeted self-materializing strand physics system** を実装する。

# 2. 完全実装の目的

本実装は、髪物理solverそのものを単体で重くするものではない。

目的は、次の高コスト処理を60FPS予算内に収めることである。

```txt
all-strand physics solve
dense contact graph generation
large island construction
multi-iteration local solver
repair / island / fallback decision
candidate-to-persistent commit
```

これらを以下の制御構造へ変換する。

```txt
risk estimation
  -> top-risk compaction
  -> budgeted mode decision
  -> typed repair
  -> repair certificate evaluation
  -> unresolved repair promotion
  -> bounded island graph
  -> bounded island solve
  -> certificate-after commit/reject
  -> defer/fallback
  -> telemetry
```

# 3. 完全実装範囲

本設計書で実装するものは以下である。

```txt
1. StrandRisk 推定
2. risk上位strandのbudgeted compaction
3. PhysicsMode decision
4. typed repair task生成
5. typed repair solver
6. repair certificate評価
7. island候補昇格
8. bounded spatial hash
9. bounded island edge生成
10. budgeted connected-component labeling
11. island member compaction
12. boundary ghost生成
13. per-island solver budget allocation
14. bounded island local solver
15. physics certificate-after評価
16. improvement-only commit/reject
17. defer/fallback policy
18. delayed telemetry
19. CPU debug baseline
20. GPU path
21. 05 GPU-resident runtime接続hook
```

# 4. 実装契約

```txt
1. frame budgetとbuffer capacityを分離する。
2. GPU pathではCPU readbackでdispatch数を決めない。
3. すべてのcounterはoverflow時の挙動を持つ。
4. candidate stateとpersistent stateを分離する。
5. 改善しないcandidateはpersistent stateへcommitしない。
6. island graph / island member / repair taskにはhard capを持たせる。
7. budget外のtaskはdeferまたはfallbackとしてtelemetryに出す。
8. telemetryに出ない性能主張をしない。
9. CPU debug baselineとGPU pathでdecisionの整合性をテストする。
10. 60FPS評価ではp95_frame_ms <= 16.6を基準にする。
```

# 5. ディレクトリ構成

```txt
include/strand/physics/StrandPhysicsSystem.h
include/strand/physics/StrandPhysicsTypes.h
include/strand/physics/StrandRisk.h
include/strand/physics/PhysicsMode.h
include/strand/physics/PhysicsFrameBudget.h
include/strand/physics/PhysicsBudgetController.h
include/strand/physics/PhysicsIsland.h
include/strand/physics/BoundaryGhost.h
include/strand/physics/RodRepairTask.h
include/strand/physics/RepairCertificate.h
include/strand/physics/PhysicsCommitPolicy.h
include/strand/physics/PhysicsDeferFallbackPolicy.h
include/strand/physics/PhysicsTelemetry.h

src/strand_physics/StrandPhysicsSystem.cpp
src/strand_physics/StrandRiskEstimator.cpp
src/strand_physics/RiskyStrandCompaction.cpp
src/strand_physics/PhysicsModeDecision.cpp
src/strand_physics/PhysicsBudgetController.cpp
src/strand_physics/PhysicsIslandBuilder.cpp
src/strand_physics/IslandGraph.cpp
src/strand_physics/BoundaryGhostBuilder.cpp
src/strand_physics/RodRepairTaskBuilder.cpp
src/strand_physics/RodRepairOperators.cpp
src/strand_physics/IslandLocalSolver.cpp
src/strand_physics/PhysicsCommit.cpp
src/strand_physics/PhysicsDeferFallback.cpp
src/strand_physics/PhysicsTelemetry.cpp
src/strand_physics/CpuPhysicsBaseline.cpp

shaders/PhysicsCommon.hlsli
shaders/ClearPhysicsCountersCS.hlsl
shaders/EstimateStrandRiskCS.hlsl
shaders/CompactTopRiskyStrandsCS.hlsl
shaders/AllocatePhysicsModesCS.hlsl
shaders/BuildRepairTasksCS.hlsl
shaders/SolveTypedRepairBudgetedCS.hlsl
shaders/EvaluateRepairCertificatesCS.hlsl
shaders/PromoteRepairsToIslandCandidatesCS.hlsl
shaders/BuildBoundedIslandSpatialHashCS.hlsl
shaders/GenerateBoundedIslandEdgesCS.hlsl
shaders/BuildIslandLabelsBudgetedCS.hlsl
shaders/CompactIslandMembersCS.hlsl
shaders/BuildBoundaryGhostsCS.hlsl
shaders/AllocateIslandSolverBudgetsCS.hlsl
shaders/SolvePhysicsIslandsBudgetedCS.hlsl
shaders/EvaluatePhysicsCertificatesCS.hlsl
shaders/CommitOrRejectPhysicsCS.hlsl
shaders/DeferOrFallbackPhysicsCS.hlsl
shaders/WritePhysicsTelemetryCS.hlsl

tests/TestStrandRiskEstimator.cpp
tests/TestPhysicsModeDecision.cpp
tests/TestPhysicsBudgetController.cpp
tests/TestRiskyStrandTopKCompaction.cpp
tests/TestRepairTaskBudgetCap.cpp
tests/TestRepairImprovementCommit.cpp
tests/TestRepairRejectOnNewFailure.cpp
tests/TestIslandEdgeBudgetCap.cpp
tests/TestIslandLabelBudgetIterations.cpp
tests/TestBoundaryGhostBudgetCap.cpp
tests/TestIslandSolverBudgetCap.cpp
tests/TestCommitRejectFallbackPolicy.cpp
tests/TestDeferQueue.cpp
tests/TestGpuCpuPhysicsDecisionConsistency.cpp
tests/TestNoCpuReadbackInGpuResidentPath.cpp
tests/TestPhysicsTelemetryCompleteness.cpp

tools/analyze_budgeted_strand_physics.py
```

# 6. C++ 定数

```cpp
#pragma once
#include <cstdint>

namespace strand::physics {

static constexpr uint32_t kThreadsPerGroup = 128;
static constexpr uint32_t kMaxPointsPerStrand = 32;
static constexpr uint32_t kMaxSegmentsPerStrand = kMaxPointsPerStrand - 1;
static constexpr uint32_t kInvalidU32 = 0xffffffffu;
static constexpr float kEpsilon = 1.0e-6f;

}
```

# 7. HLSL 定数

```hlsl
#define PHYSICS_THREADS_PER_GROUP 128
#define PHYSICS_MAX_POINTS_PER_STRAND 32
#define PHYSICS_MAX_SEGMENTS_PER_STRAND 31
#define PHYSICS_INVALID_U32 0xffffffffu
#define PHYSICS_EPSILON 1.0e-6f
```

# 8. Risk data

```cpp
namespace strand::physics {

struct StrandRisk
{
    uint32_t strandId;
    float representativePosition[3];

    float certificateRisk;
    float contactRisk;
    float editRisk;
    float motionRisk;
    float visualImportance;
    float recentFailureRisk;

    float totalRisk;
    uint32_t reasonBits;
};

enum RiskReasonBits : uint32_t
{
    RISK_NONE              = 0,
    RISK_CERTIFICATE_FAIL  = 1u << 0,
    RISK_CONTACT           = 1u << 1,
    RISK_EDIT_STALE        = 1u << 2,
    RISK_MOTION_DIVERGENCE = 1u << 3,
    RISK_RECENT_FAILURE    = 1u << 4,
    RISK_VISUAL_IMPORTANCE = 1u << 5,
};

}
```

# 9. Physics mode

```cpp
namespace strand::physics {

enum PhysicsMode : uint32_t
{
    PHYSICS_MODE_SKIP = 0,
    PHYSICS_MODE_QS_REUSE = 1,
    PHYSICS_MODE_TYPED_REPAIR = 2,
    PHYSICS_MODE_MICRO_ISLAND = 3,
    PHYSICS_MODE_LOCAL_ISLAND = 4,
    PHYSICS_MODE_FALLBACK = 5,
};

struct PhysicsModeDecision
{
    uint32_t strandId;
    uint32_t mode;
    uint32_t reasonBits;

    float risk;
    float estimatedCostMs;
    float expectedImprovement;

    uint32_t budgetAccepted;
};

}
```

# 10. Physics frame budget

```cpp
namespace strand::physics {

struct PhysicsFrameBudget
{
    float physicsBudgetMs = 3.0f;

    uint32_t maxRiskyStrandsPerFrame = 16384;
    uint32_t maxRepairTasksPerFrame = 8192;
    uint32_t maxIslandCandidatesPerFrame = 2048;
    uint32_t maxIslandEdgesPerFrame = 32768;
    uint32_t maxIslandMembersPerFrame = 16384;

    uint32_t maxNewIslandsPerFrame = 256;
    uint32_t maxPersistentIslands = 1024;
    uint32_t maxBoundaryGhostsPerFrame = 16384;

    uint16_t maxRepairIterations = 2;
    uint16_t maxIslandIterations = 4;
    uint16_t maxLabelIterations = 8;

    float minRiskForRepair = 0.50f;
    float minRiskForIsland = 0.75f;
    float minRepairImprovement = 0.05f;

    float maxBoundaryDiscontinuity = 0.002f;
    float maxPenetrationMm = 0.05f;
    float emergencyFallbackCertificateValue = 10.0f;

    uint32_t allowDeferredWork = 1;
    uint32_t allowLastStableFallback = 1;
};

struct PhysicsBudgetCounters
{
    uint32_t acceptedRiskyStrandCount;
    uint32_t deferredRiskyStrandCount;

    uint32_t repairTaskCount;
    uint32_t repairCommitCount;
    uint32_t repairRejectCount;
    uint32_t repairDeferCount;

    uint32_t islandCandidateCount;
    uint32_t islandEdgeCount;
    uint32_t islandCount;
    uint32_t islandMemberCount;
    uint32_t islandCommitCount;
    uint32_t islandRejectCount;
    uint32_t islandDeferCount;

    uint32_t fallbackCount;
    uint32_t overflowCount;
    uint32_t budgetDropCount;
};

}
```

# 11. Physics island data

```cpp
namespace strand::physics {

struct RiskyStrandEntry
{
    uint32_t strandId;
    float position[3];
    float risk;
    uint32_t reasonBits;
};

struct IslandEdge
{
    uint32_t aCandidateIndex;
    uint32_t bCandidateIndex;
    float contactDistance;
    float edgeRisk;
    uint32_t reasonBits;
};

struct PhysicsIsland
{
    uint32_t islandId;
    uint32_t previousIslandId;

    uint32_t memberOffset;
    uint32_t memberCount;
    uint32_t ghostOffset;
    uint32_t ghostCount;

    float boundsMin[3];
    float boundsMax[3];

    float riskScore;
    float solverBudgetMs;

    float certificateValueBefore;
    float certificateValueAfter;
    float maxPenetrationMm;
    float boundaryDiscontinuity;
    float memberChurnRate;

    uint32_t frameCreated;
    uint16_t lifetimeFrames;
    uint16_t cooldownFrames;
    uint32_t flags;
};

struct IslandMember
{
    uint32_t strandId;
    uint16_t localIndex;
    uint16_t flags;
    float localRisk;
    float boundaryWeight;
};

struct BoundaryGhost
{
    uint32_t strandId;
    uint32_t sourceIslandId;
    float weight;
    float restPosition[3];
    float predictedPosition[3];
};

}
```

# 12. Typed repair data

```cpp
namespace strand::physics {

enum RepairOperatorType : uint32_t
{
    REPAIR_OP_LENGTH = 0,
    REPAIR_OP_BEND = 1,
    REPAIR_OP_TWIST = 2,
    REPAIR_OP_PENETRATION = 3,
    REPAIR_OP_EDIT_CONSISTENCY = 4,
};

enum RepairStatus : uint32_t
{
    REPAIR_NONE = 0,
    REPAIR_COMMIT = 1,
    REPAIR_REJECT = 2,
    REPAIR_DEFER_TO_ISLAND = 3,
    REPAIR_FLOAT_FALLBACK = 4,
    REPAIR_DEFER_NEXT_FRAME = 5,
};

struct RodRepairTask
{
    uint32_t taskId;
    uint32_t strandId;

    uint16_t segmentStart;
    uint16_t segmentEnd;

    uint32_t certificateBitsBefore;
    uint32_t reasonBits;
    uint32_t operatorMask;

    float priority;
    float budgetMs;

    float lengthResidualBefore;
    float bendResidualBefore;
    float twistResidualBefore;
    float penetrationBefore;
    float boundaryLeakageBefore;

    uint16_t maxIterations;
    uint16_t flags;
};

struct RepairCertificate
{
    uint32_t taskId;
    uint32_t strandId;

    uint32_t certificateBitsAfter;
    uint32_t newFailureBits;

    float lengthResidualAfter;
    float bendResidualAfter;
    float twistResidualAfter;
    float penetrationAfter;
    float boundaryLeakageAfter;
    float improvementRatio;

    uint16_t iterationCount;
    uint16_t status;
};

}
```

# 13. Runtime buffers

```cpp
namespace strand::physics {

struct PhysicsRuntimeBuffers
{
    GPUBuffer persistentStates;
    GPUBuffer candidateStates;
    GPUBuffer lastStableStates;

    GPUBuffer strandRisks;
    GPUBuffer riskyStrands;
    GPUBuffer physicsModeDecisions;

    GPUBuffer repairTasks;
    GPUBuffer repairCertificates;

    GPUBuffer islandCandidates;
    GPUBuffer islandEdges;
    GPUBuffer islandLabels;
    GPUBuffer islands;
    GPUBuffer islandMembers;
    GPUBuffer boundaryGhosts;
    GPUBuffer islandSolveResults;

    GPUBuffer physicsCounters;
    GPUBuffer physicsDispatchArgs;
    GPUBuffer physicsTelemetryRing;
};

}
```

# 14. Settings

```cpp
namespace strand::physics {

struct StrandPhysicsSettings
{
    uint32_t strandCount = 0;

    uint32_t maxRiskyStrands = 65536;
    uint32_t maxIslandEdges = 131072;
    uint32_t maxIslands = 4096;
    uint32_t maxIslandMembersTotal = 65536;
    uint32_t maxBoundaryGhosts = 65536;
    uint32_t maxRepairTasks = 65536;

    float riskThreshold = 0.5f;
    float contactRadius = 0.01f;
    float cellSize = 0.02f;

    float maxBoundaryDiscontinuity = 0.002f;
    float maxPenetrationMm = 0.05f;
    float maxRepairDeltaMm = 2.0f;
    float maxBoundaryLeakageMm = 0.2f;
    float minRepairImprovement = 0.05f;

    uint16_t defaultRepairRadiusSegments = 2;
    uint16_t maxRepairRadiusSegments = 6;

    PhysicsFrameBudget frameBudget;
};

}
```

# 15. C++ API

```cpp
namespace strand::physics {

class BudgetedStrandPhysicsSystem
{
public:
    bool Initialize(const StrandPhysicsSettings& settings);
    void Shutdown();
    void Resize(uint32_t strandCount);

    void ExecuteFrame(const PhysicsFrameInput& input);
    void ExecuteCpuDebugBaseline(const PhysicsFrameInput& input);

    PhysicsTelemetry ReadTelemetryDelayed(uint32_t frameIndex);

private:
    void CreateBuffers();
    void BindBuffers();
    void UploadFrameConstants(const PhysicsFrameInput& input);

    void DispatchClearPhysicsCounters();
    void DispatchEstimateStrandRisk();
    void DispatchCompactTopRiskyStrands();
    void DispatchAllocatePhysicsModes();
    void DispatchBuildRepairTasks();
    void DispatchSolveTypedRepairBudgeted();
    void DispatchEvaluateRepairCertificates();
    void DispatchPromoteUnresolvedRepairsToIslandCandidates();
    void DispatchBuildBoundedIslandSpatialHash();
    void DispatchGenerateBoundedIslandEdges();
    void DispatchBuildIslandLabelsBudgeted();
    void DispatchCompactIslandMembers();
    void DispatchBuildBoundaryGhosts();
    void DispatchAllocateIslandSolverBudgets();
    void DispatchSolvePhysicsIslandsBudgeted();
    void DispatchEvaluatePhysicsCertificates();
    void DispatchCommitOrRejectPhysics();
    void DispatchDeferOrFallbackPhysics();
    void DispatchWritePhysicsTelemetry();
};

StrandRisk EstimateStrandRiskCpu(
    uint32_t strandId,
    uint32_t certificateBits,
    float certificateValue,
    float contactRisk,
    float editRisk,
    float motionRisk,
    float recentFailureRisk,
    const StrandPhysicsSettings& settings);

PhysicsModeDecision DecidePhysicsModeCpu(
    const StrandRisk& risk,
    uint32_t certificateBits,
    float certificateValue,
    const PhysicsFrameBudget& budget);

RodRepairTask BuildRepairTaskCpu(
    uint32_t taskId,
    uint32_t strandId,
    uint32_t certificateBits,
    uint16_t worstSegmentIndex,
    uint16_t segmentCount,
    const PhysicsFrameBudget& budget);

uint32_t SelectRepairOperatorMask(uint32_t certificateBits);

RepairCertificate SolveTypedRepairCpu(
    const RodRepairTask& task,
    StrandState32* candidate,
    const PhysicsFrameBudget& budget);

bool ShouldPromoteRepairToIslandCpu(
    const RodRepairTask& task,
    const RepairCertificate& cert,
    const PhysicsFrameBudget& budget);

bool ShouldCommitRepairCpu(
    const RodRepairTask& task,
    const RepairCertificate& cert,
    const PhysicsFrameBudget& budget);

bool ShouldCommitIslandCpu(
    const PhysicsIsland& before,
    const IslandSolveResult& result,
    const PhysicsFrameBudget& budget);

}
```

# 16. Physics mode decision rule

```cpp
PhysicsModeDecision DecidePhysicsModeCpu(
    const StrandRisk& r,
    uint32_t certificateBits,
    float certificateValue,
    const PhysicsFrameBudget& b)
{
    PhysicsModeDecision d = {};
    d.strandId = r.strandId;
    d.risk = r.totalRisk;
    d.reasonBits = r.reasonBits;

    if (certificateValue >= b.emergencyFallbackCertificateValue)
    {
        d.mode = PHYSICS_MODE_FALLBACK;
        d.estimatedCostMs = 0.0f;
        d.expectedImprovement = 0.0f;
        d.budgetAccepted = 1;
        return d;
    }

    if (r.totalRisk < b.minRiskForRepair)
    {
        d.mode = PHYSICS_MODE_SKIP;
        d.estimatedCostMs = 0.0f;
        d.expectedImprovement = 0.0f;
        d.budgetAccepted = 1;
        return d;
    }

    if ((certificateBits & CERT_REPAIRABLE_MASK) != 0)
    {
        d.mode = PHYSICS_MODE_TYPED_REPAIR;
        d.estimatedCostMs = 0.0002f;
        d.expectedImprovement = r.totalRisk;
        d.budgetAccepted = 1;
        return d;
    }

    if (r.totalRisk >= b.minRiskForIsland)
    {
        d.mode = PHYSICS_MODE_MICRO_ISLAND;
        d.estimatedCostMs = 0.0010f;
        d.expectedImprovement = r.totalRisk;
        d.budgetAccepted = 1;
        return d;
    }

    d.mode = PHYSICS_MODE_QS_REUSE;
    d.estimatedCostMs = 0.0f;
    d.expectedImprovement = 0.0f;
    d.budgetAccepted = 1;
    return d;
}
```

# 17. Repair calculus

```txt
precondition:
  certificateBitsBefore contains target failure bit

local domain:
  [segmentStart, segmentEnd]

operator:
  typed repair operator selected from certificate bits

budget:
  task.budgetMs and maxIterations are bounded by PhysicsFrameBudget

postcondition:
  target residual decreases
  fatal/new failure bits do not appear

commit:
  improvementRatio >= minRepairImprovement
  boundaryLeakageAfter <= maxBoundaryLeakageMm

promote:
  repair did not improve enough
  risk >= minRiskForIsland
  island budget is available

fallback:
  fatal bit
  worse residual
  new severe failure
  budget exceeded
  overflow
```

# 18. Frame algorithm

```cpp
void BudgetedStrandPhysicsSystem::ExecuteFrame(const PhysicsFrameInput& input)
{
    BindBuffers();
    UploadFrameConstants(input);

    DispatchClearPhysicsCounters();

    // 1. 全strandに対して安いrisk推定だけを行う。
    DispatchEstimateStrandRisk();

    // 2. risk上位だけをbudget内でcompactする。
    DispatchCompactTopRiskyStrands();

    // 3. 各strandをskip / reuse / repair / island / fallbackへ分類する。
    DispatchAllocatePhysicsModes();

    // 4. 低コストなtyped repairを先に実行する。
    DispatchBuildRepairTasks();
    DispatchSolveTypedRepairBudgeted();
    DispatchEvaluateRepairCertificates();

    // 5. repairで改善しなかったものだけisland候補へ昇格する。
    DispatchPromoteUnresolvedRepairsToIslandCandidates();

    // 6. island graphはboundedに作る。
    DispatchBuildBoundedIslandSpatialHash();
    DispatchGenerateBoundedIslandEdges();

    // 7. connected componentはiteration上限付きで行う。
    DispatchBuildIslandLabelsBudgeted();

    // 8. island memberとboundary ghostをbudget内で生成する。
    DispatchCompactIslandMembers();
    DispatchBuildBoundaryGhosts();

    // 9. islandごとのsolver budgetを割り当てる。
    DispatchAllocateIslandSolverBudgets();

    // 10. budget内だけisland solveする。
    DispatchSolvePhysicsIslandsBudgeted();
    DispatchEvaluatePhysicsCertificates();

    // 11. 改善したcandidateだけcommitする。
    DispatchCommitOrRejectPhysics();

    // 12. 未処理・悪化・budget超過はdefer/fallbackへ送る。
    DispatchDeferOrFallbackPhysics();

    // 13. 評価用telemetryを書く。
    DispatchWritePhysicsTelemetry();
}
```

# 19. HLSL common

```hlsl
#define PHYSICS_MODE_SKIP         0u
#define PHYSICS_MODE_QS_REUSE     1u
#define PHYSICS_MODE_TYPED_REPAIR 2u
#define PHYSICS_MODE_MICRO_ISLAND 3u
#define PHYSICS_MODE_LOCAL_ISLAND 4u
#define PHYSICS_MODE_FALLBACK     5u

#define RISK_CERTIFICATE_FAIL  (1u << 0)
#define RISK_CONTACT           (1u << 1)
#define RISK_EDIT_STALE        (1u << 2)
#define RISK_MOTION_DIVERGENCE (1u << 3)
#define RISK_RECENT_FAILURE    (1u << 4)

#define REPAIR_OP_LENGTH           0u
#define REPAIR_OP_BEND             1u
#define REPAIR_OP_TWIST            2u
#define REPAIR_OP_PENETRATION      3u
#define REPAIR_OP_EDIT_CONSISTENCY 4u

struct PhysicsFrameBudgetGpu
{
    float physicsBudgetMs;

    uint maxRiskyStrandsPerFrame;
    uint maxRepairTasksPerFrame;
    uint maxIslandCandidatesPerFrame;
    uint maxIslandEdgesPerFrame;
    uint maxIslandMembersPerFrame;

    uint maxNewIslandsPerFrame;
    uint maxPersistentIslands;
    uint maxBoundaryGhostsPerFrame;

    uint maxRepairIterations;
    uint maxIslandIterations;
    uint maxLabelIterations;

    float minRiskForRepair;
    float minRiskForIsland;
    float minRepairImprovement;

    float maxBoundaryDiscontinuity;
    float maxPenetrationMm;
    float emergencyFallbackCertificateValue;

    uint allowDeferredWork;
    uint allowLastStableFallback;
};

struct PhysicsBudgetCountersGpu
{
    uint acceptedRiskyStrandCount;
    uint deferredRiskyStrandCount;

    uint repairTaskCount;
    uint repairCommitCount;
    uint repairRejectCount;
    uint repairDeferCount;

    uint islandCandidateCount;
    uint islandEdgeCount;
    uint islandCount;
    uint islandMemberCount;
    uint islandCommitCount;
    uint islandRejectCount;
    uint islandDeferCount;

    uint fallbackCount;
    uint overflowCount;
    uint budgetDropCount;
};
```

# 20. GPU pass details

## 20.1 EstimateStrandRiskCS

```txt
input:
  physics certificate bits/value
  contact risk
  edit risk
  motion risk
  recent failure risk
  visual importance

output:
  StrandRiskGpu[strandCount]
```

## 20.2 CompactTopRiskyStrandsCS

```txt
input:
  StrandRiskGpu
  PhysicsFrameBudgetGpu

output:
  RiskyStrandEntryGpu
  acceptedRiskyStrandCount
  deferredRiskyStrandCount
  overflowCount

rule:
  risk < minRiskForRepair はcompactしない。
  index >= maxRiskyStrandsPerFrame はdefer扱いにする。
```

## 20.3 AllocatePhysicsModesCS

```txt
input:
  RiskyStrandEntryGpu
  certificate bits/value
  PhysicsFrameBudgetGpu

output:
  PhysicsModeDecisionGpu

rule:
  fallback value以上ならfallback
  repairable bitがあればtyped repair
  risk >= minRiskForIslandならmicro island candidate
  それ以外はreuse/skip
```

## 20.4 BuildRepairTasksCS

```txt
input:
  PhysicsModeDecisionGpu
  certificate bits
  worst segment

output:
  RodRepairTaskGpu
  repairTaskCount

rule:
  repairTaskCount >= maxRepairTasksPerFrame ならdefer
```

## 20.5 SolveTypedRepairBudgetedCS

```txt
input:
  RodRepairTaskGpu
  candidateStates

output:
  repaired candidateStates
  RepairCertificateGpu

rule:
  maxRepairIterationsを超えない。
  修復量はmaxRepairDeltaMmを超えない。
```

## 20.6 PromoteRepairsToIslandCandidatesCS

```txt
input:
  RepairCertificateGpu

output:
  islandCandidates

rule:
  REPAIR_DEFER_TO_ISLAND かつ island budgetが残っているものだけ昇格する。
```

## 20.7 GenerateBoundedIslandEdgesCS

```txt
input:
  islandCandidates
  spatial hash

output:
  IslandEdgeGpu
  islandEdgeCount

rule:
  maxIslandEdgesPerFrameを超えたedgeは捨て、overflowCountを増やす。
```

## 20.8 BuildIslandLabelsBudgetedCS

```txt
input:
  IslandEdgeGpu

output:
  islandLabels

rule:
  maxLabelIterationsだけlabel propagationする。
```

## 20.9 SolvePhysicsIslandsBudgetedCS

```txt
input:
  PhysicsIslandGpu
  IslandMemberGpu
  BoundaryGhostGpu
  candidateStates

output:
  island solve result
  candidateStates

rule:
  island.solverBudgetMs と maxIslandIterationsを守る。
```

## 20.10 CommitOrRejectPhysicsCS

```txt
input:
  candidateStates
  persistentStates
  lastStableStates
  RepairCertificateGpu
  IslandSolveResultGpu

output:
  updated persistentStates
  updated lastStableStates
  commit/reject counters

rule:
  certificateAfter < certificateBefore かつ newFailureBits == 0 の場合のみcommitする。
```

# 21. Budget policy

```txt
1. buffer capacity と per-frame execution budget を分ける。
2. maxRiskyStrands は安全上限、maxRiskyStrandsPerFrame は60FPS実行上限。
3. risk上位から処理し、budget外はdeferする。
4. repairはislandより優先する。
5. island edge生成は上限を超えたらoverflow_countを増やし、残りはdeferする。
6. island solver iterationはframe budgetに応じて下げられる。
7. budget超過時はnew island生成を止め、repairまたはfallbackに寄せる。
8. 結果が悪化したcandidateはpersistent stateへcommitしない。
```

# 22. Adaptive degradation

```cpp
void AdaptPhysicsBudget(
    const PhysicsTelemetry& prev,
    PhysicsFrameBudget* budget)
{
    if (prev.p95FrameMs > 16.6f)
    {
        budget->maxRiskyStrandsPerFrame =
            std::max(4096u, budget->maxRiskyStrandsPerFrame * 3 / 4);

        budget->maxRepairTasksPerFrame =
            std::max(2048u, budget->maxRepairTasksPerFrame * 3 / 4);

        budget->maxIslandEdgesPerFrame =
            std::max(8192u, budget->maxIslandEdgesPerFrame * 3 / 4);

        budget->maxIslandMembersPerFrame =
            std::max(4096u, budget->maxIslandMembersPerFrame * 3 / 4);

        budget->maxIslandIterations =
            std::max<uint16_t>(2, budget->maxIslandIterations - 1);

        budget->minRiskForIsland =
            std::min(0.95f, budget->minRiskForIsland + 0.05f);
    }
    else if (prev.p95FrameMs < 14.0f && prev.fallbackRate > 0.05f)
    {
        budget->maxRiskyStrandsPerFrame =
            std::min(16384u, budget->maxRiskyStrandsPerFrame + 1024);

        budget->maxRepairTasksPerFrame =
            std::min(8192u, budget->maxRepairTasksPerFrame + 512);

        budget->maxIslandEdgesPerFrame =
            std::min(32768u, budget->maxIslandEdgesPerFrame + 2048);

        budget->minRiskForIsland =
            std::max(0.65f, budget->minRiskForIsland - 0.02f);
    }
}
```

# 23. Integration hooks

## 23.1 01との接続

```txt
SAFE_DECODE -> PHYSICS_MODE_SKIP or PHYSICS_MODE_QS_REUSE
TYPED_REPAIR -> PHYSICS_MODE_TYPED_REPAIR
ISLAND_ESCALATION -> PHYSICS_MODE_MICRO_ISLAND or PHYSICS_MODE_LOCAL_ISLAND
FLOAT_FALLBACK -> PHYSICS_MODE_FALLBACK
REJECT_PAYLOAD -> PHYSICS_MODE_FALLBACK
```

## 23.2 03との接続

02は03へ以下を渡す。

```txt
physicsCertificateBits
physicsRisk
repairVisibility
islandVisibility
fallbackReasonBits
```

03側では、physics riskによる高品質化を無制限にしない。

## 23.3 04との接続

04から以下を受ける。

```txt
editCertificateBits
editRisk
editRepairRequired
editIslandEscalationRequired
editedSegmentRange
```

04由来のisland escalationは02のbudget内で処理する。

## 23.4 05との接続

05は以下を担当する。

```txt
task compaction
DispatchIndirect args generation
CPU readback-free execution
delayed telemetry
overflow-safe counters
```

02は05接続時にCPU readback禁止pathを使用する。

# 24. Telemetry

```txt
case_id, frame, strand_count, variant, scenario, seed,

p50_frame_ms,
p95_frame_ms,
p99_frame_ms,
gpu_physics_ms,

risk_estimation_ms,
repair_ms,
island_graph_ms,
island_solve_ms,
commit_ms,

accepted_risky_strand_count,
deferred_risky_strand_count,

mode_skip_count,
mode_qs_reuse_count,
mode_typed_repair_count,
mode_micro_island_count,
mode_local_island_count,
mode_fallback_count,

repair_task_count,
repair_commit_count,
repair_reject_count,
repair_defer_count,
repair_success_rate,

island_candidate_count,
island_edge_count,
island_count,
island_member_count,
boundary_ghost_count,
island_commit_count,
island_reject_count,
island_defer_count,

certificate_failure_count,
certificate_failure_rate,
mean_certificate_before,
mean_certificate_after,
p95_certificate_after,

max_penetration_mm,
mean_penetration_mm,
boundary_discontinuity_mean,
boundary_discontinuity_p95,

fallback_count,
fallback_rate,
overflow_count,
budget_drop_count,
cpu_readback_count
```

# 25. Evaluation protocol

## 25.1 Variants

```txt
V0_full_solve:
  品質上限。全strandまたは大きなactive setを処理する。

V1_naive_threshold:
  risk thresholdだけで処理するbaseline。

V2_repair_first:
  typed repairを先に実行する。

V3_budgeted_ladder:
  本実装。risk compaction + typed repair + bounded island + commit/reject。

V4_adaptive_budgeted_ladder:
  V3にadaptive degradationを追加する。

V5_gpu_resident_adaptive:
  V4を05 GPU-resident runtime上で実行する。
```

## 25.2 Scenarios

```txt
static_hair
fast_head_motion
wind_burst
dense_contact
edit_stress_from_04
render_stress_from_03
mixed_01_02_03_04_05
```

## 25.3 Success conditions

```txt
1. p95_frame_ms <= 16.6 in budgeted/adaptive variants.
2. gpu_physics_ms <= assigned physics budget.
3. cpu_readback_count == 0 in GPU-resident path.
4. repair_success_rate is higher than V1_naive_threshold.
5. max_penetration_mm does not exceed the accepted threshold.
6. certificate_failure_rate is lower than no-repair baseline.
7. overflow_count is reported and execution does not crash.
8. fallback_rate and budget_drop_count are reported.
```

# 26. Tests

```txt
TestStrandRiskEstimator
TestPhysicsModeDecision
TestPhysicsBudgetController
TestRiskyStrandTopKCompaction
TestRepairTaskBudgetCap
TestRepairImprovementCommit
TestRepairRejectOnNewFailure
TestIslandEdgeBudgetCap
TestIslandLabelBudgetIterations
TestBoundaryGhostBudgetCap
TestIslandSolverBudgetCap
TestCommitRejectFallbackPolicy
TestDeferQueue
TestGpuCpuPhysicsDecisionConsistency
TestNoCpuReadbackInGpuResidentPath
TestPhysicsTelemetryCompleteness
```

# 27. 完了条件

```txt
1. risk推定がCPU/GPUで一致する。
2. risk上位strandをbudget内でcompactできる。
3. PhysicsModeへ分類できる。
4. typed repairをisland solveより先に実行できる。
5. repair失敗strandだけをisland候補へ昇格できる。
6. island edge/member/ghostをhard cap付きで生成できる。
7. budget内でisland solveできる。
8. certificate-afterによりcommit/rejectできる。
9. budget超過時にdefer/fallbackが動く。
10. p95/p99、repair率、island率、fallback率、overflowをtelemetryに出せる。
11. budgeted/adaptive variantで60FPS維持実験を実行できる。
```

# 28. 研究核

```txt
高コストな全髪物理を、
risk-driven, repair-first, island-later, budgeted-commit のGPU制御問題へ変換する。
```
