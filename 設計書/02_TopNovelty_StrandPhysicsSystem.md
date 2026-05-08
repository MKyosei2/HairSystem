# 02. 髪物理計算系：Self-Materializing Strand Physics System 完全版

---
version: Top Novelty Implementation Design v1.0
status: implementation-ready design
language: C++17 + HLSL compute shader
primary_runtime: GPU hair runtime / DX11-compatible compute path
novelty_category: strand_physics_system
not_included: paper strategy, publication framing
---

# 1. 実装新規性の一文

全髪を毎frame解くのではなく、**certificate / contact / edit / motion risk から、そのframeだけ必要な局所物理islandをGPU上で自己生成し、typed local repair と boundary ghost 付き island solver を適用し、改善した場合だけ persistent state にcommitする髪物理システム**を実装する。

# 2. これは何が新しいのか

弱い実装は「髪を1本ずつ物理計算する」ことである。

トップ会議級の実装は、髪1本単位の処理をただ増やすのではなく、**必要なstrandだけを選び、その場で物理ドメインを作り、局所solveし、安全な結果だけcommitする**ことである。

```txt
従来型:
  all strands or fixed guide/cluster
    -> solver
    -> update state

提案:
  certificate/contact/edit/motion risk
    -> risky strand compaction
    -> risk-induced graph
    -> self-materialized physics islands
    -> typed repair / island local solve
    -> certificate-after evaluation
    -> commit / reject / fallback
```

# 3. 研究範囲

このファイルは、元々分かれていた以下を「髪物理計算系」1本に統合する。

```txt
- Self-materializing physics islands
- Certificate-driven rod repair
- Per-strand mode decision used only inside physics system
```

mode allocationは単独研究にはしない。髪物理システムの内部機能として使う。

# 4. 弱い実装と強い実装

## 弱い実装

```txt
髪の毛を全部1本ずつ物理計算する。
PBD/XPBD/DERっぽいsolverをGPUに載せる。
危険そうな髪だけthresholdで処理する。
```

これは重いだけ、または普通のactive set solverに見える。

## トップ会議級の実装新規性

```txt
1. certificate/contact/edit/motionから危険strandを作る
2. 危険strand同士の関係をGPU上でgraph化する
3. connected componentからphysics islandを毎frame生成する
4. islandの境界にはboundary ghostを作る
5. まずsegment-level typed repairを試す
6. repairでは不十分ならisland-local solverへ昇格する
7. solve後certificateが改善した場合だけcommitする
8. boundary discontinuityやnew failureが出たらreject/fallbackする
```

# 5. ディレクトリ構成

```txt
include/strand/physics/StrandPhysicsSystem.h
include/strand/physics/StrandRisk.h
include/strand/physics/PhysicsIsland.h
include/strand/physics/BoundaryGhost.h
include/strand/physics/RodRepairTask.h
include/strand/physics/RepairCertificate.h
include/strand/physics/PhysicsCommitPolicy.h

src/strand_physics/StrandRiskEstimator.cpp
src/strand_physics/RiskyStrandCompaction.cpp
src/strand_physics/PhysicsIslandBuilder.cpp
src/strand_physics/IslandGraph.cpp
src/strand_physics/BoundaryGhostBuilder.cpp
src/strand_physics/RodRepairTaskBuilder.cpp
src/strand_physics/RodRepairOperators.cpp
src/strand_physics/IslandLocalSolver.cpp
src/strand_physics/PhysicsCommit.cpp
src/strand_physics/PhysicsTelemetry.cpp

shaders/PhysicsCommon.hlsli
shaders/EstimateStrandRiskCS.hlsl
shaders/CompactRiskyStrandsCS.hlsl
shaders/BuildIslandSpatialHashCS.hlsl
shaders/GenerateIslandEdgesCS.hlsl
shaders/BuildIslandLabelsCS.hlsl
shaders/CompactIslandMembersCS.hlsl
shaders/BuildBoundaryGhostsCS.hlsl
shaders/BuildRepairTasksCS.hlsl
shaders/SolveTypedRepairCS.hlsl
shaders/SolvePhysicsIslandsCS.hlsl
shaders/EvaluatePhysicsCertificatesCS.hlsl
shaders/CommitOrRejectPhysicsCS.hlsl

tests/TestStrandRiskEstimator.cpp
tests/TestPhysicsIslandBuilder.cpp
tests/TestBoundaryGhost.cpp
tests/TestRodRepairOperators.cpp
tests/TestPhysicsCommitPolicy.cpp

tools/analyze_strand_physics_system.py
```

# 6. C++ 定数

```cpp
namespace strand::physics {
static constexpr uint32_t kThreadsPerGroup = 128;
static constexpr uint32_t kMaxPointsPerStrand = 32;
static constexpr uint32_t kMaxSegmentsPerStrand = kMaxPointsPerStrand - 1;
static constexpr uint32_t kInvalidU32 = 0xffffffffu;
static constexpr float kEpsilon = 1.0e-6f;
}
```

# 7. Risk data

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

# 8. Physics island data

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

# 9. Typed repair data

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

# 10. Settings

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
    uint32_t labelIterations = 16;

    float maxBoundaryDiscontinuity = 0.002f;
    float maxPenetrationMm = 0.05f;
    float maxRepairDeltaMm = 2.0f;
    float maxBoundaryLeakageMm = 0.2f;
    float minRepairImprovement = 0.05f;

    uint16_t defaultRepairRadiusSegments = 2;
    uint16_t maxRepairRadiusSegments = 6;
    uint16_t repairIterations = 4;
    uint16_t islandSolverIterations = 8;

    uint16_t islandHistoryCooldownFrames = 4;
};

}
```

# 11. C++ API

```cpp
namespace strand::physics {

StrandRisk EstimateStrandRiskCpu(
    uint32_t strandId,
    uint32_t certificateBits,
    float certificateValue,
    float contactRisk,
    float editRisk,
    float motionRisk,
    float recentFailureRisk,
    const StrandPhysicsSettings& settings);

bool IsRiskyStrand(
    const StrandRisk& risk,
    const StrandPhysicsSettings& settings);

void BuildRiskyStrandsCpu(
    const StrandRisk* risks,
    uint32_t strandCount,
    const StrandPhysicsSettings& settings,
    RiskyStrandEntry* outRisky,
    uint32_t* outRiskyCount);

void GenerateIslandEdgesCpu(
    const RiskyStrandEntry* risky,
    uint32_t riskyCount,
    const StrandPhysicsSettings& settings,
    IslandEdge* outEdges,
    uint32_t* outEdgeCount);

void BuildIslandLabelsCpu(
    const IslandEdge* edges,
    uint32_t edgeCount,
    uint32_t riskyCount,
    uint32_t* outLabels);

void CompactPhysicsIslandsCpu(
    const RiskyStrandEntry* risky,
    const uint32_t* labels,
    uint32_t riskyCount,
    PhysicsIsland* outIslands,
    IslandMember* outMembers,
    uint32_t* outIslandCount);

void BuildBoundaryGhostsCpu(
    const PhysicsIsland* islands,
    const IslandMember* members,
    uint32_t islandCount,
    BoundaryGhost* outGhosts,
    uint32_t* outGhostCount);

RodRepairTask BuildRepairTaskCpu(
    uint32_t taskId,
    uint32_t strandId,
    uint32_t certificateBits,
    uint16_t worstSegmentIndex,
    uint16_t segmentCount,
    const StrandPhysicsSettings& settings);

uint32_t SelectRepairOperatorMask(uint32_t certificateBits);

RepairCertificate SolveTypedRepairCpu(
    const RodRepairTask& task,
    StrandState32* candidate,
    const StrandPhysicsSettings& settings);

IslandSolveResult SolvePhysicsIslandLocalCpu(
    const PhysicsIsland& island,
    const IslandMember* members,
    const BoundaryGhost* ghosts,
    StrandState32* candidateStates,
    const StrandPhysicsSettings& settings);

bool ShouldCommitRepairCpu(
    const RodRepairTask& task,
    const RepairCertificate& cert,
    const StrandPhysicsSettings& settings);

bool ShouldCommitIslandCpu(
    const PhysicsIsland& before,
    const IslandSolveResult& result,
    const StrandPhysicsSettings& settings);

}
```

# 12. Repair calculus

repairは関数ではなく、規則として扱う。

```txt
precondition:
  certificateBitsBefore contains target failure bit

local domain:
  [segmentStart, segmentEnd]

operator:
  typed repair operator

postcondition:
  target residual decreases
  fatal/new failure bits do not appear

commit:
  improvementRatio >= minRepairImprovement
  boundaryLeakageAfter <= maxBoundaryLeakageMm

fallback:
  fatal bit
  worse residual
  new severe failure
  budget exceeded
```

# 13. 物理システムのframe algorithm

```cpp
void StrandPhysicsSystem::ExecuteFrame(const PhysicsFrameInput& input)
{
    // 1. certificate / contact / edit / motion から risk を推定
    DispatchEstimateStrandRisk();

    // 2. 危険strandをcompact
    DispatchCompactRiskyStrands();

    // 3. まず1本単位のtyped repair taskを作る
    DispatchBuildRepairTasks();
    DispatchSolveTypedRepair();
    DispatchEvaluateRepairCertificates();

    // 4. repairで不足したstrandだけisland candidateにする
    DispatchBuildIslandSpatialHash();
    DispatchGenerateIslandEdges();
    DispatchBuildIslandLabels();
    DispatchCompactIslandMembers();

    // 5. boundary ghostを作る
    DispatchBuildBoundaryGhosts();

    // 6. island-local solve
    DispatchSolvePhysicsIslands();
    DispatchEvaluateIslandCertificates();

    // 7. 改善した結果だけcommitする
    DispatchCommitOrRejectPhysics();
}
```

# 14. HLSL common

```hlsl
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

struct StrandRiskGpu
{
    uint strandId;
    float3 representativePosition;
    float certificateRisk;
    float contactRisk;
    float editRisk;
    float motionRisk;
    float visualImportance;
    float recentFailureRisk;
    float totalRisk;
    uint reasonBits;
};

struct RiskyStrandEntryGpu
{
    uint strandId;
    float3 position;
    float risk;
    uint reasonBits;
};

struct IslandEdgeGpu
{
    uint aCandidateIndex;
    uint bCandidateIndex;
    float contactDistance;
    float edgeRisk;
    uint reasonBits;
};

struct PhysicsCountersGpu
{
    uint riskyStrandCount;
    uint repairTaskCount;
    uint repairCommitCount;
    uint repairRejectCount;
    uint islandEdgeCount;
    uint islandCount;
    uint islandMemberCount;
    uint boundaryGhostCount;
    uint islandCommitCount;
    uint islandRejectCount;
    uint fallbackCount;
    uint overflowCount;
};
```

# 15. Island edge generation shader

```hlsl
[numthreads(128, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;
    uint n = min(g_Counters[0].riskyStrandCount, g_Settings.maxRiskyStrands);
    if (i >= n) return;

    RiskyStrandEntryGpu a = g_RiskyStrands[i];
    int3 cell = ComputeCell(a.position, g_Settings.cellSize);

    for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        CellRange range = LookupCellRange(cell + int3(dx, dy, dz));
        for (uint k = range.begin; k < range.end; ++k)
        {
            uint j = g_CellEntries[k].candidateIndex;
            if (j <= i) continue;

            RiskyStrandEntryGpu b = g_RiskyStrands[j];
            float d = length(a.position - b.position);
            float contactRisk = saturate((g_Settings.contactRadius - d) / g_Settings.contactRadius);
            float certificateCoupling = ComputeCertificateCoupling(a.reasonBits, b.reasonBits);
            float editCoupling = ComputeEditCoupling(a.reasonBits, b.reasonBits);
            float edgeRisk = max(contactRisk, max(certificateCoupling, editCoupling));

            if (edgeRisk <= 0.0f) continue;

            uint edgeIndex;
            InterlockedAdd(g_Counters[0].islandEdgeCount, 1, edgeIndex);
            if (edgeIndex >= g_Settings.maxIslandEdges)
            {
                InterlockedAdd(g_Counters[0].overflowCount, 1);
                return;
            }

            IslandEdgeGpu e;
            e.aCandidateIndex = i;
            e.bCandidateIndex = j;
            e.contactDistance = d;
            e.edgeRisk = edgeRisk;
            e.reasonBits = a.reasonBits | b.reasonBits;
            g_IslandEdges[edgeIndex] = e;
        }
    }
}
```

# 16. Commit policy

```cpp
bool ShouldCommitRepairCpu(
    const RodRepairTask& t,
    const RepairCertificate& rc,
    const StrandPhysicsSettings& s)
{
    if (rc.status != REPAIR_COMMIT) return false;
    if (rc.newFailureBits != 0) return false;
    if (rc.improvementRatio < s.minRepairImprovement) return false;
    if (rc.boundaryLeakageAfter > s.maxBoundaryLeakageMm) return false;
    if (!std::isfinite(rc.lengthResidualAfter)) return false;
    if (!std::isfinite(rc.bendResidualAfter)) return false;
    if (!std::isfinite(rc.twistResidualAfter)) return false;
    if (!std::isfinite(rc.penetrationAfter)) return false;
    return true;
}

bool ShouldCommitIslandCpu(
    const PhysicsIsland& before,
    const IslandSolveResult& r,
    const StrandPhysicsSettings& s)
{
    if (r.status != 1) return false;
    if (!std::isfinite(r.certificateValueAfter)) return false;
    if (r.certificateValueAfter > before.certificateValueBefore + 1.0e-6f) return false;
    if (r.maxPenetrationMm > s.maxPenetrationMm) return false;
    if (r.boundaryDiscontinuity > s.maxBoundaryDiscontinuity) return false;
    return true;
}
```

# 17. Telemetry

```txt
case_id, frame, strand_count,
risky_strand_count,
repair_task_count,
repair_commit_count,
repair_reject_count,
repair_success_rate,
island_edge_count,
island_count,
island_member_count,
boundary_ghost_count,
island_commit_count,
island_reject_count,
fallback_count,
overflow_count,
mean_certificate_before,
mean_certificate_after,
mean_improvement_ratio,
max_penetration_mm,
boundary_discontinuity_mean,
boundary_discontinuity_max,
temporal_popping_score,
gpu_physics_ms,
p95_frame_ms,
p99_frame_ms
```

# 18. テスト

```txt
TestRiskEstimatorFromCertificate
TestRiskyStrandCompaction
TestSpatialHashNeighborQuery
TestIslandEdgeGeneration
TestConnectedComponentsLabels
TestIslandMemberCompaction
TestBoundaryGhostExtraction
TestRepairTaskBuildFromCertificate
TestLengthRepair
TestBendRepair
TestTwistRepair
TestPenetrationRepair
TestRepairCommitReject
TestIslandCommitReject
TestNoCommitOnNewFailure
TestGpuCpuIslandLabelConsistency
```

# 19. 完了条件

```txt
1. certificate/contact/edit/motion riskからrisky strandを生成できる。
2. risky strand graphからislandをGPU上で生成できる。
3. island memberとboundary ghostをcompactできる。
4. typed repairがcertificate bitから起動する。
5. repair後certificateを評価できる。
6. repairで不十分な場合にisland solveへ昇格できる。
7. island solve後certificateを評価できる。
8. 改善した場合だけpersistent stateへcommitできる。
9. 悪化/new failure/boundary discontinuity時はreject/fallbackできる。
10. p95/p99 frame time、repair成功率、island commit率、boundary discontinuityをtelemetryに出せる。
```

# 20. この研究の核

この研究の核は、髪を全部解くことではない。

```txt
そのframeで壊れたstrandだけから、そのframeだけ必要な物理ドメインを自己生成する。
```

これに typed repair、boundary ghost、commit/reject を組み合わせることで、髪物理計算系としてトップ会議級の実装新規性になる。
