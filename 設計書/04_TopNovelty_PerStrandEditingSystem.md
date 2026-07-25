# 04. 髪1本単位編集系：Certificate-Preserving Per-Strand Editing 完全版

---
version: Top Novelty Implementation Design v1.0
status: implementation-ready design
language: C++17 + HLSL compute shader
primary_runtime: GPU hair runtime / interactive editor path
novelty_category: per_strand_editing
not_included: paper strategy, publication framing
---

# 1. 実装新規性の一文

髪1本を選んで動かせるだけのeditorではなく、**編集操作そのものが edit certificate を発行し、破綻segment、必要repair、island escalation、render preview昇格、commit/rejectをGPU上で決定する certificate-preserving per-strand editing system** を実装する。

# 2. これは何が新しいのか

弱い実装は、髪1本をクリックして動かすUIである。

トップ会議級の実装では、編集操作が物理破綻を作る可能性を前提にし、編集後の状態をそのままcommitしない。

```txt
従来型:
  user drags strand
    -> update points
    -> maybe smooth / maybe simulate later

提案:
  user edit operation
    -> edit certificate generation
    -> local physical violation detection
    -> typed repair
    -> island escalation if needed
    -> render preview quality boost
    -> safe commit / reject / fallback
```

# 3. 研究範囲

この研究は、UI機能だけを作るものではない。

目的は、1本単位またはsegment単位の編集を、物理・レンダリング・runtimeと一貫させることである。

```txt
1. strandを1本単位で選択できる
2. segment範囲を編集できる
3. edit certificateを発行できる
4. length/bend/twist/penetration破綻を検出できる
5. repairが必要か判定できる
6. repair不能ならphysics islandへ昇格できる
7. 編集中strandだけrender preview品質を上げられる
8. 安全な編集結果だけcommitできる
```

# 4. 弱い実装と強い実装

## 弱い実装

```txt
髪1本を選べる。
髪1本を動かせる。
ブラシで髪を変形できる。
編集後に見た目をsmoothする。
```

これはeditor機能であり、トップ級の新規性には足りない。

## トップ会議級の実装新規性

```txt
1. 編集操作をEditOperationとして記録する
2. 編集操作からEditCertificateを生成する
3. どのsegmentが壊れたかを即座に出す
4. certificate bitからtyped repairを起動する
5. repairできない場合はphysics islandへ昇格する
6. 編集中だけrendering品質を上げる
7. before/after certificateを比較してcommit/rejectする
8. 破綻する編集はfallbackまたはsoft rejectする
```

# 5. ディレクトリ構成

```txt
include/strand/edit/StrandEditOperation.h
include/strand/edit/StrandEditCertificate.h
include/strand/edit/PerStrandSelection.h
include/strand/edit/EditConstraint.h
include/strand/edit/EditCommitPolicy.h
include/strand/edit/EditUndoRedo.h
include/strand/edit/EditPreviewState.h

src/strand_edit/PerStrandSelection.cpp
src/strand_edit/StrandPicking.cpp
src/strand_edit/EditOperationBuilder.cpp
src/strand_edit/EditCertificateBuilder.cpp
src/strand_edit/EditViolationDetector.cpp
src/strand_edit/EditRepairBridge.cpp
src/strand_edit/EditIslandEscalation.cpp
src/strand_edit/EditCommitPolicy.cpp
src/strand_edit/EditUndoRedo.cpp
src/strand_edit/EditTelemetry.cpp

shaders/EditCommon.hlsli
shaders/PickStrandCS.hlsl
shaders/BuildEditOperationCS.hlsl
shaders/ApplyEditCandidateCS.hlsl
shaders/GenerateEditCertificateCS.hlsl
shaders/DetectEditViolationsCS.hlsl
shaders/BuildEditRepairTasksCS.hlsl
shaders/CommitOrRejectEditCS.hlsl
shaders/WriteEditPreviewFlagsCS.hlsl

tests/TestStrandPicking.cpp
tests/TestEditOperation.cpp
tests/TestEditCertificate.cpp
tests/TestEditViolationDetector.cpp
tests/TestEditCommitPolicy.cpp
tests/TestEditUndoRedo.cpp

tools/analyze_certificate_preserving_editing.py
```

# 6. Edit operation

```cpp
namespace strand::edit {

enum EditOperationType : uint32_t
{
    EDIT_OP_NONE = 0,
    EDIT_OP_ROOT_MOVE = 1,
    EDIT_OP_TIP_MOVE = 2,
    EDIT_OP_SEGMENT_DRAG = 3,
    EDIT_OP_SEGMENT_PIN = 4,
    EDIT_OP_CURVE_BEND = 5,
    EDIT_OP_LOCAL_SMOOTH = 6,
    EDIT_OP_DELETE_STRAND = 7,
    EDIT_OP_DUPLICATE_STRAND = 8,
};

struct StrandEditOperation
{
    uint32_t operationId;
    uint32_t strandId;
    uint32_t operationType;

    uint16_t segmentStart;
    uint16_t segmentEnd;

    float handlePositionBefore[3];
    float handlePositionAfter[3];
    float editDeltaMm;
    float brushRadiusMm;
    float strength;

    uint32_t frameIndex;
    uint32_t flags;
};

}
```

# 7. Edit certificate bits

```cpp
namespace strand::edit {

enum EditCertificateBits : uint32_t
{
    EDIT_CERT_NONE                 = 0,
    EDIT_CERT_ROOT_MOVED           = 1u << 0,
    EDIT_CERT_TIP_MOVED            = 1u << 1,
    EDIT_CERT_SEGMENT_DRAGGED      = 1u << 2,
    EDIT_CERT_LENGTH_BROKEN        = 1u << 3,
    EDIT_CERT_BEND_BROKEN          = 1u << 4,
    EDIT_CERT_TWIST_BROKEN         = 1u << 5,
    EDIT_CERT_COLLISION_CREATED    = 1u << 6,
    EDIT_CERT_NEIGHBORHOOD_DIRTY   = 1u << 7,
    EDIT_CERT_REPAIR_REQUIRED      = 1u << 8,
    EDIT_CERT_ISLAND_REQUIRED      = 1u << 9,
    EDIT_CERT_RENDER_PREVIEW       = 1u << 10,
    EDIT_CERT_REJECT_RECOMMENDED   = 1u << 11,
};

}
```

# 8. Edit certificate

```cpp
namespace strand::edit {

struct StrandEditCertificate
{
    uint32_t operationId;
    uint32_t strandId;

    uint16_t editedSegmentStart;
    uint16_t editedSegmentEnd;
    uint16_t worstSegmentIndex;
    uint16_t recommendedRepairRadius;

    float editDeltaMm;
    float lengthViolation;
    float bendViolation;
    float twistViolation;
    float penetrationViolation;
    float neighborhoodDisturbance;
    float certificateValue;

    uint32_t editBits;
    uint32_t physicsCertificateBitsBefore;
    uint32_t physicsCertificateBitsAfterCandidate;

    uint32_t repairRequired;
    uint32_t islandEscalationRequired;
    uint32_t renderPreviewRequired;
    uint32_t commitRecommended;
};

}
```

# 9. Edit candidate state

```cpp
namespace strand::edit {

struct EditCandidateState
{
    uint32_t operationId;
    uint32_t strandId;

    float beforePoints[32][3];
    float candidatePoints[32][3];

    uint32_t status;
    uint32_t rejectReasonBits;

    float certificateValueBefore;
    float certificateValueAfterCandidate;
    float certificateValueAfterRepair;
};

struct EditPreviewFlags
{
    uint32_t strandId;
    uint32_t previewMode;
    uint32_t renderModeOverride;
    uint32_t debugBits;
};

}
```

# 10. Edit settings

```cpp
namespace strand::edit {

struct CertificatePreservingEditSettings
{
    uint32_t strandCount = 0;
    uint32_t maxEditOperationsPerFrame = 4096;
    uint32_t maxEditRepairTasks = 4096;
    uint32_t maxEditIslands = 512;

    float maxEditDeltaMm = 20.0f;
    float lengthToleranceRatio = 0.02f;
    float bendToleranceRad = 0.65f;
    float twistToleranceRad = 0.75f;
    float penetrationToleranceMm = 0.05f;
    float neighborhoodDirtyRadiusMm = 4.0f;

    float repairRequiredCertificateValue = 0.5f;
    float islandRequiredCertificateValue = 3.0f;
    float rejectRequiredCertificateValue = 10.0f;

    uint16_t defaultRepairRadiusSegments = 2;
    uint16_t maxRepairRadiusSegments = 6;

    uint16_t previewHighQualityFrames = 5;
};

}
```

# 11. C++ API

```cpp
namespace strand::edit {

bool PickStrandCpu(
    const Ray& ray,
    const StrandBounds* bounds,
    uint32_t strandCount,
    uint32_t* outStrandId,
    uint16_t* outSegmentIndex);

StrandEditOperation BuildEditOperationCpu(
    uint32_t operationId,
    uint32_t strandId,
    uint32_t operationType,
    uint16_t segmentStart,
    uint16_t segmentEnd,
    const float handleBefore[3],
    const float handleAfter[3],
    const CertificatePreservingEditSettings& settings);

void ApplyEditCandidateCpu(
    const StrandEditOperation& op,
    const StrandState32& before,
    StrandState32* outCandidate);

StrandEditCertificate GenerateEditCertificateCpu(
    const StrandEditOperation& op,
    const StrandState32& before,
    const StrandState32& candidate,
    uint32_t physicsCertificateBitsBefore,
    const SignedDistanceFieldView* sdf,
    const CertificatePreservingEditSettings& settings);

bool NeedsEditRepairCpu(
    const StrandEditCertificate& cert,
    const CertificatePreservingEditSettings& settings);

bool NeedsEditIslandEscalationCpu(
    const StrandEditCertificate& cert,
    const CertificatePreservingEditSettings& settings);

bool ShouldCommitEditCpu(
    const StrandEditCertificate& beforeRepair,
    const StrandEditCertificate& afterRepair,
    const CertificatePreservingEditSettings& settings);

EditPreviewFlags BuildEditPreviewFlagsCpu(
    const StrandEditCertificate& cert,
    const CertificatePreservingEditSettings& settings);

}
```

# 12. Edit certificate generation rule

```cpp
StrandEditCertificate GenerateEditCertificateCpu(
    const StrandEditOperation& op,
    const StrandState32& before,
    const StrandState32& candidate,
    uint32_t physicsBitsBefore,
    const SignedDistanceFieldView* sdf,
    const CertificatePreservingEditSettings& s)
{
    StrandEditCertificate c = {};
    c.operationId = op.operationId;
    c.strandId = op.strandId;
    c.editedSegmentStart = op.segmentStart;
    c.editedSegmentEnd = op.segmentEnd;
    c.editDeltaMm = op.editDeltaMm;
    c.physicsCertificateBitsBefore = physicsBitsBefore;

    c.lengthViolation = ComputeLengthViolation(before, candidate, &c.worstSegmentIndex);
    c.bendViolation = ComputeBendViolation(before, candidate, &c.worstSegmentIndex);
    c.twistViolation = ComputeTwistViolation(before, candidate, &c.worstSegmentIndex);
    c.penetrationViolation = sdf ? ComputePenetrationViolation(candidate, *sdf, &c.worstSegmentIndex) : 0.0f;
    c.neighborhoodDisturbance = ComputeNeighborhoodDisturbance(before, candidate);

    if (op.operationType == EDIT_OP_ROOT_MOVE) c.editBits |= EDIT_CERT_ROOT_MOVED;
    if (op.operationType == EDIT_OP_TIP_MOVE) c.editBits |= EDIT_CERT_TIP_MOVED;
    if (op.operationType == EDIT_OP_SEGMENT_DRAG) c.editBits |= EDIT_CERT_SEGMENT_DRAGGED;

    if (c.lengthViolation > s.lengthToleranceRatio) c.editBits |= EDIT_CERT_LENGTH_BROKEN;
    if (c.bendViolation > s.bendToleranceRad) c.editBits |= EDIT_CERT_BEND_BROKEN;
    if (c.twistViolation > s.twistToleranceRad) c.editBits |= EDIT_CERT_TWIST_BROKEN;
    if (c.penetrationViolation > s.penetrationToleranceMm) c.editBits |= EDIT_CERT_COLLISION_CREATED;
    if (c.neighborhoodDisturbance > s.neighborhoodDirtyRadiusMm) c.editBits |= EDIT_CERT_NEIGHBORHOOD_DIRTY;

    c.certificateValue =
        10.0f * c.lengthViolation +
        c.bendViolation +
        c.twistViolation +
        0.1f * c.penetrationViolation +
        0.05f * c.neighborhoodDisturbance;

    if (c.certificateValue >= s.repairRequiredCertificateValue)
        c.editBits |= EDIT_CERT_REPAIR_REQUIRED;

    if (c.certificateValue >= s.islandRequiredCertificateValue ||
        (c.editBits & EDIT_CERT_NEIGHBORHOOD_DIRTY) != 0)
        c.editBits |= EDIT_CERT_ISLAND_REQUIRED;

    if (c.certificateValue >= s.rejectRequiredCertificateValue)
        c.editBits |= EDIT_CERT_REJECT_RECOMMENDED;

    c.editBits |= EDIT_CERT_RENDER_PREVIEW;

    c.repairRequired = (c.editBits & EDIT_CERT_REPAIR_REQUIRED) != 0;
    c.islandEscalationRequired = (c.editBits & EDIT_CERT_ISLAND_REQUIRED) != 0;
    c.renderPreviewRequired = 1;
    c.commitRecommended = (c.editBits & EDIT_CERT_REJECT_RECOMMENDED) == 0;

    return c;
}
```

# 13. Frame algorithm

```cpp
void CertificatePreservingEditor::ExecuteEditFrame(const EditFrameInput& input)
{
    // 1. user inputからstrand/segmentをpickする
    DispatchPickStrand();

    // 2. edit operationを生成する
    DispatchBuildEditOperation();

    // 3. candidate stateを作る。まだpersistent stateへは書かない
    DispatchApplyEditCandidate();

    // 4. candidateに対してedit certificateを生成する
    DispatchGenerateEditCertificate();
    DispatchDetectEditViolations();

    // 5. certificateに応じてrepair taskを作る
    DispatchBuildEditRepairTasks();

    // 6. repairまたはisland escalationを実行する
    DispatchSolveEditRepairTasks();
    DispatchEscalateEditIslandsIfNeeded();

    // 7. rendering preview flagを出す
    DispatchWriteEditPreviewFlags();

    // 8. 安全な編集だけcommitする
    DispatchCommitOrRejectEdit();
}
```

# 14. HLSL common

```hlsl
#define EDIT_OP_NONE         0u
#define EDIT_OP_ROOT_MOVE    1u
#define EDIT_OP_TIP_MOVE     2u
#define EDIT_OP_SEGMENT_DRAG 3u
#define EDIT_OP_SEGMENT_PIN  4u
#define EDIT_OP_CURVE_BEND   5u
#define EDIT_OP_LOCAL_SMOOTH 6u

#define EDIT_CERT_ROOT_MOVED         (1u << 0)
#define EDIT_CERT_TIP_MOVED          (1u << 1)
#define EDIT_CERT_SEGMENT_DRAGGED    (1u << 2)
#define EDIT_CERT_LENGTH_BROKEN      (1u << 3)
#define EDIT_CERT_BEND_BROKEN        (1u << 4)
#define EDIT_CERT_TWIST_BROKEN       (1u << 5)
#define EDIT_CERT_COLLISION_CREATED  (1u << 6)
#define EDIT_CERT_NEIGHBORHOOD_DIRTY (1u << 7)
#define EDIT_CERT_REPAIR_REQUIRED    (1u << 8)
#define EDIT_CERT_ISLAND_REQUIRED    (1u << 9)
#define EDIT_CERT_RENDER_PREVIEW     (1u << 10)
#define EDIT_CERT_REJECT_RECOMMENDED (1u << 11)

struct StrandEditOperationGpu
{
    uint operationId;
    uint strandId;
    uint operationType;
    uint segmentPacked;
    float3 handlePositionBefore;
    float3 handlePositionAfter;
    float editDeltaMm;
    float brushRadiusMm;
    float strength;
    uint frameIndex;
    uint flags;
};

struct StrandEditCertificateGpu
{
    uint operationId;
    uint strandId;
    uint segmentPacked;
    uint worstRepairPacked;
    float editDeltaMm;
    float lengthViolation;
    float bendViolation;
    float twistViolation;
    float penetrationViolation;
    float neighborhoodDisturbance;
    float certificateValue;
    uint editBits;
    uint physicsCertificateBitsBefore;
    uint physicsCertificateBitsAfterCandidate;
    uint actionBits;
};
```

# 15. GenerateEditCertificateCS

```hlsl
[numthreads(128, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint opIndex = tid.x;
    if (opIndex >= g_EditCounters.operationCount) return;

    StrandEditOperationGpu op = g_EditOperations[opIndex];
    StrandState32 before = g_BeforeStates[op.strandId];
    StrandState32 candidate = g_CandidateStates[op.strandId];

    uint worstSegment = 0u;
    float lengthViolation = ComputeLengthViolation(before, candidate, worstSegment);
    float bendViolation = ComputeBendViolation(before, candidate, worstSegment);
    float twistViolation = ComputeTwistViolation(before, candidate, worstSegment);
    float penetrationViolation = ComputePenetrationViolation(candidate, worstSegment);
    float neighborhoodDisturbance = ComputeNeighborhoodDisturbance(before, candidate);

    uint bits = 0u;
    if (op.operationType == EDIT_OP_ROOT_MOVE) bits |= EDIT_CERT_ROOT_MOVED;
    if (op.operationType == EDIT_OP_TIP_MOVE) bits |= EDIT_CERT_TIP_MOVED;
    if (op.operationType == EDIT_OP_SEGMENT_DRAG) bits |= EDIT_CERT_SEGMENT_DRAGGED;

    if (lengthViolation > g_Settings.lengthToleranceRatio) bits |= EDIT_CERT_LENGTH_BROKEN;
    if (bendViolation > g_Settings.bendToleranceRad) bits |= EDIT_CERT_BEND_BROKEN;
    if (twistViolation > g_Settings.twistToleranceRad) bits |= EDIT_CERT_TWIST_BROKEN;
    if (penetrationViolation > g_Settings.penetrationToleranceMm) bits |= EDIT_CERT_COLLISION_CREATED;
    if (neighborhoodDisturbance > g_Settings.neighborhoodDirtyRadiusMm) bits |= EDIT_CERT_NEIGHBORHOOD_DIRTY;

    float value =
        10.0f * lengthViolation +
        bendViolation +
        twistViolation +
        0.1f * penetrationViolation +
        0.05f * neighborhoodDisturbance;

    if (value >= g_Settings.repairRequiredCertificateValue)
        bits |= EDIT_CERT_REPAIR_REQUIRED;

    if (value >= g_Settings.islandRequiredCertificateValue ||
        (bits & EDIT_CERT_NEIGHBORHOOD_DIRTY) != 0)
        bits |= EDIT_CERT_ISLAND_REQUIRED;

    if (value >= g_Settings.rejectRequiredCertificateValue)
        bits |= EDIT_CERT_REJECT_RECOMMENDED;

    bits |= EDIT_CERT_RENDER_PREVIEW;

    StrandEditCertificateGpu c;
    c.operationId = op.operationId;
    c.strandId = op.strandId;
    c.segmentPacked = op.segmentPacked;
    c.worstRepairPacked = PackWorstAndRepairRange(worstSegment, op.segmentPacked);
    c.editDeltaMm = op.editDeltaMm;
    c.lengthViolation = lengthViolation;
    c.bendViolation = bendViolation;
    c.twistViolation = twistViolation;
    c.penetrationViolation = penetrationViolation;
    c.neighborhoodDisturbance = neighborhoodDisturbance;
    c.certificateValue = value;
    c.editBits = bits;
    c.physicsCertificateBitsBefore = g_PhysicsCertificates[op.strandId].certificateBits;
    c.physicsCertificateBitsAfterCandidate = BuildPhysicsBitsFromEditBits(bits);
    c.actionBits = BuildActionBits(bits);

    g_EditCertificates[opIndex] = c;
}
```

# 16. Commit policy

```cpp
bool ShouldCommitEditCpu(
    const StrandEditCertificate& beforeRepair,
    const StrandEditCertificate& afterRepair,
    const CertificatePreservingEditSettings& s)
{
    if ((afterRepair.editBits & EDIT_CERT_REJECT_RECOMMENDED) != 0)
        return false;

    if (afterRepair.certificateValue > beforeRepair.certificateValue + 1.0e-6f)
        return false;

    if (afterRepair.lengthViolation > s.lengthToleranceRatio)
        return false;

    if (afterRepair.bendViolation > s.bendToleranceRad)
        return false;

    if (afterRepair.twistViolation > s.twistToleranceRad)
        return false;

    if (afterRepair.penetrationViolation > s.penetrationToleranceMm)
        return false;

    return true;
}
```

# 17. Undo / redo requirement

編集系では、candidate/persistent stateの分離が必須である。

```txt
persistent state:
  最後に安全commitされた髪状態

candidate state:
  現在の編集操作を適用した仮状態

repair candidate state:
  candidateにrepairを適用した仮状態

commit:
  certificateが改善し、物理制約を満たす場合だけpersistentへ反映
```

Undo/redoは operation log で管理する。

```cpp
struct EditUndoRecord
{
    uint32_t operationId;
    uint32_t strandId;
    float previousPoints[32][3];
    float committedPoints[32][3];
    uint32_t previousCertificateBits;
    uint32_t committedCertificateBits;
};
```

# 18. Telemetry

```txt
case_id, frame,
edit_operation_count,
selected_strand_count,
mean_edit_delta_mm,
max_edit_delta_mm,
edit_certificate_count,
repair_required_count,
island_required_count,
render_preview_required_count,
commit_count,
reject_count,
fallback_count,
mean_certificate_before,
mean_certificate_after_candidate,
mean_certificate_after_repair,
length_violation_mean,
bend_violation_mean,
twist_violation_mean,
penetration_violation_max,
neighborhood_dirty_count,
edit_gpu_ms,
preview_latency_ms
```

# 19. テスト

```txt
TestStrandPickingRay
TestSegmentPicking
TestRootMoveOperation
TestTipMoveOperation
TestSegmentDragOperation
TestApplyEditCandidateDoesNotModifyPersistent
TestEditCertificateLengthBroken
TestEditCertificateBendBroken
TestEditCertificateCollisionCreated
TestEditRepairRequired
TestEditIslandRequired
TestEditRenderPreviewRequired
TestEditCommitRejectPolicy
TestUndoRedoOperationLog
TestGpuCpuEditCertificateConsistency
```

# 20. 完了条件

```txt
1. 1本単位でstrandを選択できる。
2. segment範囲を指定して編集できる。
3. 編集結果はcandidate stateに入り、すぐpersistentへ書かれない。
4. edit certificateをGPU上で生成できる。
5. length/bend/twist/penetration/neighborhood disturbanceを検出できる。
6. edit certificateからrepair taskを生成できる。
7. repair不能時にphysics islandへ昇格できる。
8. 編集中strandだけrender preview品質を上げられる。
9. certificate改善時だけcommitできる。
10. reject/fallback/undo/redoが実装されている。
11. preview latencyとcommit/reject数をtelemetryに出せる。
```

# 21. この研究の核

この研究の核は、髪を1本ずつ動かせることではない。

```txt
編集操作がcertificateを発行し、物理・repair・island・render preview・commitを自動的に決める。
```

UIではなく、編集後の物理一貫性と安全なcommitまで含めることで、髪1本単位編集系としてトップ会議級の実装新規性になる。
