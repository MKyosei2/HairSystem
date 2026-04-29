# Certificate-Driven Rod Repair Calculus 設計書

## 0. まずここだけ読む

### この機能を一言で言うと
certificateに失敗したstrand全体ではなく、**壊れたsegment範囲だけ** をlength/bend/twist/penetration制約で局所修復する。

### なぜ必要か
full rod solverを全髪へ常時使うと重い。単純projectionだけでは長さ、曲げ、ねじれ、貫通のどれが壊れたか分からない。そこで、certificateBitsとworstSegmentIndexからRepairTaskを作り、修復後に再certificateする。

### 実装で作るもの
| 種別 | 作るもの | 実装先 |
|---|---|---|
| 共通型 | `RepairTask`, `RepairCertificate`, `RodSegmentState` | `include/hairgpu/RodRepair.h` |
| CPU参照実装 | task生成、projection、certificate再計算 | `src/hairgpu_core/RodRepair.cpp` |
| GPU処理 | repair candidate compact、task生成、SolveRodRepair、commit/fallback | `HairRodRepair.hlsl` |
| viewer接続 | repair buffers、dispatch args、telemetry | `GpuHairViewer.h/.cpp` |
| 評価出力 | task count、success rate、before/after residual | final_stage tools |

### 入力 → 処理 → 出力
```txt
CertificateBuffer + CandidateState + ModeDecision
  -> BuildRepairTasks(strandId, segmentStart, segmentEnd)
  -> Decode local segment band
  -> ProjectLength / ProjectBend / ProjectTwist / ProjectPenetration
  -> EvaluateRepairCertificate
  -> Commit / Deferred / FloatFallback / Island retry
  -> Log residual before/after
```

### 最初に実装する順番
```txt
1. RepairTask と RepairCertificate を定義する
2. worstSegmentIndex からsegment範囲を作るCPU実装を作る
3. length projectionだけで最初のrepair testを通す
4. bend / twist / penetration projectionを追加する
5. GPU task bufferとdispatchを接続する
6. repair_success_rate と residual before/after をログに出す
```

### 完了条件
```txt
RepairTaskはstrand全体ではなくsegmentStart/Endを持つ
repair後にcertificateを再計算する
改善しない結果はcommitされない
Ablation_NoRodRepairと比較できるログが出る
```

### この設計書の読み方
まずこの章で「全体solverではなく局所repair」であることを掴む。詳細では「RepairTask生成」「制約投影の順序」「Commit/Reject」を優先する。

---
---

# 実装先ファイル早見表

## 新規追加するファイル

| 種別 | ファイル | 目的 |
|---|---|---|
| C++ header | `include/hairgpu/RodRepair.h` | RepairTask、RepairCertificate、RodSegmentState、repair settingsを定義する |
| C++ source | `src/hairgpu_core/RodRepair.cpp` | CPU検証用のrepair task生成、projection、certificate再計算を実装する |
| HLSL | `apps/dx11_viewer/shaders/HairRodRepair.hlsl` | GPU repair task生成、局所projection、commit/fallback判定を実装する |
| Test | `tests/Test_RodRepair.cpp` | length/bend/penetration/fallbackを検証する |

## 変更する既存ファイル

| ファイル | 変更内容 |
|---|---|
| `include/hairgpu/Collision.h` | signed distance queryをrepair projectionから使えるようにする。既存関数を維持する |
| `src/hairgpu_core/Collision.cpp` | sphere/capsule/plane penetration correction helperを追加する |
| `src/hairgpu_core/CMakeLists.txt` | `RodRepair.cpp` を追加する |
| `apps/dx11_viewer/src/GpuHairViewer.h` | RepairTaskBuffer、RepairCertificateBuffer、repair countersを追加する |
| `apps/dx11_viewer/src/GpuHairViewer.cpp` | shader load、buffer作成、dispatch、repair telemetryを追加する |
| `tools/final_stage/hairgpu_final_db.py` | repair before/after residual、success/fallbackをDBへ取り込む |
| `tools/final_stage/hairgpu_final_excel.py` | repair評価表を追加する |

## 既存実装との関係

既存のsparse corrective physicsはBaseline側の比較対象として残す。  
RodRepairは、certificate failureまたはisland rejectから生成されたsegment範囲だけを対象にする。  
既存corrective physicsを初期solverとして使う場合でも、`RepairTask`、`RepairCertificate`、commit/reject/fallbackの形式は本仕様に合わせる。

---

# 実装仕様

## C++ API

```cpp
namespace hairgpu
{
    struct RepairTask;
    struct RepairCertificate;
    struct RodRepairSettings;
    struct RodRepairStats;

    RodRepairStats BuildRepairTasksCpu(
        std::span<const UnifiedStrandCertificate> certificates,
        std::span<const ModeDecision> decisions,
        const RodRepairSettings& settings,
        std::vector<RepairTask>& tasks);

    RepairCertificate SolveRodRepairCpu(
        const RepairTask& task,
        std::span<Vec3> points,
        const CollisionScene& collisionScene,
        const RodRepairSettings& settings);
}
```

## HLSL entry point

```txt
CompactRepairCandidatesCS
BuildRepairTaskRangesCS
SolveRodRepairCS
EvaluateRepairCertificateCS
CommitOrFallbackRepairCS
BuildRepairDispatchArgsCS
```

---

# 実装タスク

## Task 1: RepairTaskとCPU projectionを追加する

実装先:

```txt
include/hairgpu/RodRepair.h
src/hairgpu_core/RodRepair.cpp
src/hairgpu_core/CMakeLists.txt
```

完了条件:

```txt
1. certificate failureからsegmentStart/segmentEnd付きRepairTaskを作れる。
2. length projectionでlengthResidualが改善する。
3. penetration projectionでpenetrationResidualが改善する。
4. repair後certificateを再計算できる。
```

## Task 2: GPU repair task生成を追加する

実装先:

```txt
apps/dx11_viewer/shaders/HairRodRepair.hlsl
apps/dx11_viewer/src/GpuHairViewer.cpp
```

完了条件:

```txt
1. RepairTaskBufferがGPU上でappendされる。
2. maxRepairTasks超過時にoverflow counterが増える。
3. task countからDispatchIndirect argsが生成される。
```

## Task 3: GPU solver / commit / fallbackを接続する

完了条件:

```txt
1. RepairTaskごとに局所segment範囲だけdecode/solveする。
2. RepairCertificateが出る。
3. Committed/Rejected/Deferred/FallbackRequiredをstatusとして持つ。
4. Reject結果がsilent commitされない。
```

## Task 4: telemetryを拡張する

追加する列:

```txt
rod_repair_enabled
repair_task_count
repair_committed_count
repair_rejected_count
repair_deferred_count
repair_fallback_count
repair_iteration_avg
repair_iteration_max
repair_time_ms
length_residual_before_avg
length_residual_after_avg
bend_residual_before_avg
bend_residual_after_avg
twist_residual_before_avg
twist_residual_after_avg
penetration_before_avg
penetration_after_avg
```

## Task 5: testを追加する

```txt
Test_Repair_LengthResidual
Test_Repair_BendResidual
Test_Repair_PenetrationResidual
Test_Repair_FallbackOnNaN
Test_Repair_MaxTaskOverflow
```

---

# 受け入れ条件

```txt
1. enableRodRepair=falseではrepair task countが0になる。
2. enableRodRepair=trueではcertificate failureからRepairTaskが生成される。
3. RepairTaskはstrand全体ではなくsegment範囲を持つ。
4. 修復後certificateが改善しない場合はcommitされない。
5. Ablation_NoRodRepairとProposed_FullSystemを同じログschemaで比較できる。
```

この設計書は、既存プロジェクトへ実装するために必要な構造、入出力、接続先、受け入れ条件を記述する。  
評価指標と比較対象は `Common_Evaluation_and_Baseline.md` を参照する。

---


# 0. 実装定義

この機能は「破綻後の見た目補正」ではなく、**certificateに失敗したsegment帯だけを、局所ロッド制約でJIT修復するruntime処理**として実装する。

```txt
Invariant:
1. repair対象はstrand全体ではなくsegmentStart〜segmentEndで指定する。
2. length、bend、twist、penetration residualからRepairCertificateを作る。
3. iteration数はcertificateValueから決める。
4. 修復後にcertificateを再計算し、成功/失敗を判定する。
5. 失敗時はPhysicsIslandまたはFloatFallbackへ戻す。
```

## 0.1 既存技術との差分

rod solverは高品質だが、全髪へ常時適用すると重い。  
本機能は、rod solverを全体の主solverにするのではなく、certificateが失敗した局所範囲にだけ呼び出すrepair calculusとして使う。

```txt
Novelty Boundary:
既存: rod / DER / Cosserat系のsolverで髪の物理を高精度に解く。
本研究: certificate failureを入力に、必要なsegment帯・iteration数・fallbackを決める局所修復制御としてrod repairを使う。
```

## 0.2 実装範囲

本設計書で求める実装は、見た目の後処理ではなく、統合runtimeに組み込まれるcertificate駆動の局所ロッド修復である。
実装は以下をすべて満たすこと。

```txt
Implementation Scope:
1. certificate failure、island reject、contact penetration、high bend/twist residualからRepairTaskを生成する。
2. RepairTaskはstrandIdだけでなくsegmentStart、segmentEnd、initialCertificate、priority、budgetMsを必ず持つ。
3. 修復対象segment帯をresidual分布から決め、strand全体へ不要なrod solverを適用しない。
4. length projection、bend projection、twist projection、penetration projectionを順序付きで実行する。
5. iteration countはcertificateValue、priority、budgetMsから決め、上限を超えないようにする。
6. 修復後はRepairCertificateを再計算し、成功時のみdecoded stateへcommitする。
7. 修復失敗時はsilent failureにせず、PhysicsIsland再投入、FloatFallback、または次フレーム継続taskとして明示的に扱う。
8. repairTaskCount、successRate、fallbackRate、residualBefore/After、iterationCount、repairTimeMsをログへ出す。
```

実装では、rod solverを主solverとして全体に適用しない。
certificateが示した破綻箇所だけに、必要な制約投影を必要な回数だけ適用する**局所修復calculus**として実装する。

---

# 目的

破綻したstrandまたはsegment帯に対して、長さ・曲げ・ねじれ・貫通残差を用いたcertificateに基づき、局所ロッド修復を行う。

本機能の目的は以下である。

1. full rod solverを全髪へ常時適用しない
2. 破綻したstrandまたは局所segment帯だけ修復する
3. 修復範囲をcertificateで決定する
4. iteration数をcertificateで決定する
5. 修復成功/失敗をcertificateで判定する
6. 失敗時にPhysicsIslandまたはfallbackへ戻す

---

# 解決する問題

現行プロジェクトでは、局所的な破綻に対して以下の問題がある。

1. 単純projectionでは伸び・曲げ・ねじれの整合性が崩れる
2. full rod solverは重すぎる
3. どのsegmentを修復すべきかが明確でない
4. 修復が成功したかどうかの定量的判定がない
5. 修復失敗時のfallbackが制御ループに組み込まれていない

本設計では、certificateを使って修復範囲・iteration数・成功判定を統一する。

---

# 入力と出力

## 入力

```cpp
struct RodRepairInput
{
    uint32_t strandId;
    uint32_t segmentStart;
    uint32_t segmentEnd;

    const Vec3* positions;
    const Vec3* previousPositions;
    const float* restLengths;
    const float* restBendAngles;
    const float* restTwistAngles;

    const ContactInfo* contacts;
    const StrandDecodeCertificate* previousCertificate;

    float repairBudgetMs;
};
```

---

## 出力

```cpp
struct RodRepairOutput
{
    uint32_t strandId;
    uint32_t segmentStart;
    uint32_t segmentEnd;

    float lengthResidual;
    float bendResidual;
    float twistResidual;
    float penetrationResidual;
    float certificateValue;

    uint32_t iterationCount;
    bool success;
    bool fallbackRequired;
};
```

---

# データ構造

## RepairTask

```cpp
struct RepairTask
{
    uint32_t strandId;
    uint32_t segmentStart;
    uint32_t segmentEnd;

    float initialCertificate;
    float priority;
    float budgetMs;

    uint32_t flags;
};
```

---

## RepairCertificate

```cpp
struct RepairCertificate
{
    float lengthResidual;
    float bendResidual;
    float twistResidual;
    float penetrationResidual;

    float certificateValue;

    uint32_t pass;
    uint32_t fallbackMode;
};
```

---

## RodSegmentState

```cpp
struct RodSegmentState
{
    Vec3 p0;
    Vec3 p1;

    float restLength;
    float bendAngle;
    float twistAngle;
};
```

---

# 数式

長さ残差：

\[
R_{\text{length}}
=
\max_i
\frac{
|\|x_{i+1}-x_i\| - l_i|
}{
\max(l_i,\epsilon)
}
\]

曲げ残差：

\[
R_{\text{bend}}
=
\max_i
\frac{
|\theta_i - \theta_i^0|
}{
\tau_{\theta}
}
\]

ねじれ残差：

\[
R_{\text{twist}}
=
\max_i
\frac{
|\phi_i - \phi_i^0|
}{
\tau_{\phi}
}
\]

貫通残差：

\[
R_{\text{penetration}}
=
\max_i
\frac{
d_i^{-}
}{
\tau_p
}
\]

総合certificate：

\[
C_B =
\max
(
R_{\text{length}},
R_{\text{bend}},
R_{\text{twist}},
R_{\text{penetration}}
)
\]

成功条件：

\[
C_B \leq 1
\]

iteration数：

\[
n_{\text{iter}}
=
\text{clamp}
(
\alpha C_B
+
\beta \dot{C}_B
+
\gamma contact_j,
n_{\min},
n_{\max}
)
\]

修復対象範囲：

\[
B^*
=
\arg\max_B
\frac{
C_B - C'_B
}{
T_B
}
\]

意味：

```txt
単位時間あたりcertificateを最も改善できるsegment帯を優先して修復する。
```

---

# アルゴリズム

## BuildRepairTasks

```txt
function BuildRepairTasks:
    1. certificateValue > 1 のstrandを探す
    2. residualが大きいsegmentを探す
    3. 連続segmentをrepair bandにまとめる
    4. 各repair bandのpriorityを計算する
    5. budget内でrepair taskを生成する
```

---

## SolveRodRepair

```txt
function SolveRodRepair(task):
    1. repair対象segment帯を読む
    2. 初期certificateを計算する
    3. certificateからiteration数を決める
    4. length constraint projectionを行う
    5. bend constraint projectionを行う
    6. twist constraint projectionを行う
    7. penetration projectionを行う
    8. 更新後certificateを計算する
    9. C <= 1ならsuccess
    10. C > 1ならfallbackRequired
```

---

## RepairPriority

```txt
priority =
initialCertificate
* contactWeight
* visibilityWeight
/ max(predictedRepairCost, epsilon)
```

---

# GPU/CPU処理フロー

## CPU側

```txt
1. repair閾値とiteration上限を設定する
2. debug時のみrepair結果をreadbackする
3. 通常はrepair task生成と実行をGPUで完結させる
```

---

## GPU側

```txt
1. certificate bufferを読む
2. C > 1 のstrandをrepair candidateにする
3. segment residualを計算する
4. repair bandを作る
5. repair taskをcompactする
6. RepairTaskごとにsolverをdispatchする
7. RepairCertificateを更新する
8. successなら結果をcommitする
9. failureならfallback modeをModeDecisionへ返す
```

GPUバッファ：

```cpp
StructuredBuffer<StrandState> g_StrandStates;
StructuredBuffer<StrandMicrotype> g_Microtypes;
RWStructuredBuffer<RepairTask> g_RepairTasks;
RWStructuredBuffer<RepairCertificate> g_RepairCertificates;
RWStructuredBuffer<ModeDecision> g_ModeDecisions;
RWStructuredBuffer<DispatchArgs> g_RepairDispatchArgs;
```

---

# fallback設計

## fallback条件

```txt
1. repair後もcertificateValue > 1
2. iteration数がmaxIterationに到達
3. penetrationResidualが改善しない
4. twistResidualが悪化した
5. visual poppingが大きい
6. repair task数が予算を超える
```

## fallback先

```txt
F0: 追加iterationを行う
F1: repair bandを拡張する
F2: PhysicsIslandへ送る
F3: FloatFallbackへ送る
F4: 次フレームで再修復する
F5: 該当strandを一時的にDynamicResidualへ戻す
```

---

# 予想される失敗例

## 1. 複数strandの絡みを単独strand repairで直せない

原因：

- strand間接触を無視している

対策：

- PhysicsIslandへfallbackする
- neighboring strandsをghost constraintとして追加する

---

## 2. 修復によってpoppingが発生する

原因：

- 1フレームで大きく位置を変えすぎる

対策：

- repair deltaをclampする
- temporal blendingする
- multi-frame repairへ分割する

---

## 3. twist residualが悪化する

原因：

- length/bend優先のprojectionでtwistを壊す

対策：

- twist residualをcertificateに入れる
- projection順を調整する
- twist重みを上げる

---

## 4. repair taskが多すぎる

原因：

- thresholdが低すぎる
- islandへ送るべきケースまでrepairしている

対策：

- CBMAでrepair上限を設定
- PhysicsIslandとの役割分担を強める
- priority下位taskを次フレームへ送る

---


# 実装受け入れ条件

## 必須ユニットテスト

```txt
Test_Repair_LengthResidual:
    伸びたsegmentにrepairをかけ、lengthResidualが低下することを確認する。

Test_Repair_PenetrationResidual:
    collision内へ入った点をrepairし、penetrationResidualが低下することを確認する。

Test_Repair_Fallback:
    repairBudgetMs内でcertificateが通らない場合、fallbackRequired=trueになることを確認する。

Test_Repair_NoPopping:
    repair delta clampにより、1frameの最大変位が閾値以内に収まることを確認する。
```

## Definition of Done

```txt
1. RepairTaskをcertificate failureから生成できる。
2. segmentStart/Endが局所範囲になっている。
3. repair iteration数がcertificateValueに応じて変わる。
4. 修復前後のresidualをログに出せる。
5. Ablation_NoRodRepairと比較できる。
```

## 実装指示

```txt
この設計書に従って、Certificate-Driven Rod Repair Calculusを実装してください。
RepairTask生成、length/bend/twist/penetration residual計算、iteration数決定、局所projection、修復後certificate再計算、fallback判定を作ってください。
twist projectionは近似実装を許容するが、twistResidualの計測とログ出力は必須にしてください。
```

---

# 論文での主張文

```txt
本研究では、破綻したstrandまたは局所segment帯に対して、
length、bend、twist、penetration residualからなるcertificateを定義し、
このcertificateに基づいて修復範囲、iteration数、成功判定を決定する
Certificate-Driven Rod Repair Calculusを提案する。

提案法は、full rod solverを全髪に適用するのではなく、
必要な局所領域だけをJITに修復し、修復結果をcertificateとしてruntimeへ戻す。
```

---

# 詳細実装定義

この章では、`Certificate-Driven Rod Repair Calculus` を、破綻したstrand全体の後処理ではなく、certificateにより指定されたsegment帯の局所修復として実装するための詳細を定義する。

## 1. Repairの対象単位

Repair対象はstrand全体ではなく、segment範囲である。

```txt
Repair Target:
  strandId
  segmentStart
  segmentEnd
  reasonBits
  initial residual values
  budget
```

範囲は、`worstSegmentIndex` を中心に前後へ拡張して決める。

```txt
segmentStart = max(0, worstSegmentIndex - repairRadiusSegments)
segmentEnd   = min(segmentCount - 1, worstSegmentIndex + repairRadiusSegments)
```

penetrationが複数segmentにまたがる場合は、penetration segment listの最小・最大から範囲を作る。

## 2. RepairTask構造

```cpp
struct RepairTask
{
    uint32_t taskId;
    uint32_t strandId;

    uint16_t segmentStart;
    uint16_t segmentEnd;

    uint32_t reasonBits;
    uint32_t certificateBitsBefore;

    float priority;
    float budgetMs;

    float lengthResidualBefore;
    float bendResidualBefore;
    float twistResidualBefore;
    float penetrationBefore;

    uint16_t maxIterations;
    uint16_t flags;
};
```

`reasonBits` は以下を持つ。

```cpp
enum RepairReasonBits : uint32_t
{
    REPAIR_REASON_LENGTH       = 1 << 0,
    REPAIR_REASON_BEND         = 1 << 1,
    REPAIR_REASON_TWIST        = 1 << 2,
    REPAIR_REASON_PENETRATION  = 1 << 3,
    REPAIR_REASON_ISLAND_REJECT= 1 << 4,
    REPAIR_REASON_EDIT_STALE   = 1 << 5,
    REPAIR_REASON_NUMERIC_SAFE = 1 << 6
};
```

## 3. RepairTask生成

```txt
BuildRepairTasks:
  1. CertificateBufferを読む。
  2. failure bitが立ったstrandを候補にする。
  3. worstSegmentIndexとresidual分布からsegment範囲を決める。
  4. 同一strandの近接taskをmergeする。
  5. priorityを計算する。
  6. maxRepairTasksを超える場合はpriority順で残す。
  7. RepairTaskBufferへappendする。
```

同一strandでsegment範囲が近いtaskはmergeする。

```txt
merge if:
  same strandId
  distance(taskA.segmentEnd, taskB.segmentStart) <= mergeGapSegments
```

## 4. Priority計算

```txt
priority = w_len   * normalizedLengthResidual
         + w_bend  * normalizedBendResidual
         + w_twist * normalizedTwistResidual
         + w_pen   * normalizedPenetration
         + w_vis   * visualImportance
         + w_hist  * recentFailureCount
```

数値破綻やNaN/Infに関する修復は、通常repairではなくFloatFallbackを優先する。ただし、前フレーム状態へ戻せる場合はrepair taskとして扱ってよい。

## 5. 局所Rod State

repair solverは、対象segment帯の局所状態を以下に展開する。

```cpp
struct RodSegmentState
{
    float3 p0;
    float3 p1;
    float3 tangent;
    float restLength;
    float radius;
    float bendStiffness;
    float twistStiffness;
};
```

対象範囲の外側1segmentを境界条件として含める。

```txt
Decode range:
  decodeStart = max(0, segmentStart - 1)
  decodeEnd   = min(segmentCount - 1, segmentEnd + 1)
```

## 6. 制約投影の順序

repairは以下の順に投影する。

```txt
SolveRodRepair:
  for iter in 0..iterationCount:
      ProjectLength()
      ProjectBend()
      ProjectTwist()
      ProjectPenetration()
      ApplyDamping()
      CheckEarlyExit()
```

順序を固定する理由は、長さが大きく壊れた状態でbend/twistを直しても不安定になりやすいためである。

## 7. Length Projection

各segmentの長さをrest lengthへ近づける。

```txt
delta = p1 - p0
currentLength = length(delta)
correction = (currentLength - restLength) * normalize(delta)
```

root側境界が固定の場合はtip側だけを動かす。内部segmentでは両端へ半分ずつ配分する。

## 8. Bend Projection

隣接segmentの角度がrest bendから外れた場合に補正する。

```txt
bendResidual = currentBendAngle - restBendAngle
```

補正は過度に強くしない。1iterationあたりのbend補正量に上限を設ける。

```txt
bendCorrection = clamp(bendResidual, -maxBendStep, maxBendStep)
```

## 9. Twist Projection

twistは、局所frameの相対回転で評価する。

```txt
twistResidual = currentTwist - restTwist
```

twist frameが不安定な短segmentでは、twist補正を弱める。

```txt
if segmentLength < twistStableLength:
    twistWeight *= 0.25
```

## 10. Penetration Projection

collision objectにめり込んだ点を外側へ押し出す。

```txt
if signedDistance < radius:
    correction = normal * (radius - signedDistance)
```

押し出し後にlength residualが増えるため、次iterationでlength/bendを再評価する。

## 11. Iteration数の決定

iteration数は固定ではなく、certificate値とbudgetから決める。

```txt
baseIterations = repairMinIterations
residualBonus = ceil(normalizedResidual * residualIterationScale)
budgetLimit = floor(budgetMs / estimatedIterationCostMs)
iterationCount = clamp(baseIterations + residualBonus, repairMinIterations, min(repairMaxIterations, budgetLimit))
```

budget不足でiteration数が減った場合は、`REPAIR_BUDGET_LIMITED` を記録する。

## 12. Early Exit

以下の条件を満たしたら途中終了する。

```txt
1. lengthResidual <= threshold
2. bendResidual <= threshold
3. twistResidual <= threshold
4. penetration <= threshold
5. 前iterationからの改善量がepsilon以下
```

## 13. Commit / Reject

repair後は必ずcertificateを再計算する。

```txt
Commit if:
  residualAfter < residualBefore
  certificateBitsAfter does not contain fatal bits
  no NaN/Inf
  visual popping <= poppingThreshold

Reject if:
  residualAfter >= residualBefore
  penetrationAfter > penetrationBefore
  NaN/Inf occurred
  iterationCount reached max and certificate still failed
```

Reject時の処理：

```txt
1. multi-strand contact由来ならPhysicsIslandへ戻す。
2. local bend/twistが残るだけなら次フレーム継続repairにする。
3. 数値異常ならFloatFallbackへ送る。
4. budget不足ならpriorityを上げて次フレームへ回す。
```

## 14. RepairCertificate構造

```cpp
struct RepairCertificate
{
    uint32_t taskId;
    uint32_t strandId;
    uint32_t certificateBitsAfter;

    float lengthResidualAfter;
    float bendResidualAfter;
    float twistResidualAfter;
    float penetrationAfter;

    float improvementRatio;
    uint16_t iterationCount;
    uint16_t status;
};
```

`status` は `Committed`, `Rejected`, `Deferred`, `FallbackRequired` のいずれかとする。

## 15. ログ項目

```txt
repairTaskCount
repairCommittedCount
repairRejectedCount
repairDeferredCount
repairFallbackCount
repairIterationAvg
repairIterationMax
repairTimeMs
lengthResidualBeforeAvg
lengthResidualAfterAvg
bendResidualBeforeAvg
bendResidualAfterAvg
twistResidualBeforeAvg
twistResidualAfterAvg
penetrationBeforeAvg
penetrationAfterAvg
```

## 16. テストケース詳細

### 16.1 Length破綻修復

```txt
Input:
  1segmentだけrestLengthの2倍に伸ばしたstrand
Expected:
  RepairTaskが該当segment周辺に生成される
  lengthResidualAfter < lengthResidualBefore
  certificateが改善する
```

### 16.2 Bend破綻修復

```txt
Input:
  急角度に折れたstrand
Expected:
  bend projectionが実行される
  bendResidualAfterが減る
  lengthResidualが大きく悪化しない
```

### 16.3 Penetration修復

```txt
Input:
  collision sphereにめり込んだsegment
Expected:
  penetrationAfter <= threshold
  押し出し後にlength/bend certificateが再評価される
```

### 16.4 Repair失敗時のFallback

```txt
Input:
  NaNを含むdecoded state
Expected:
  repair commitされない
  FloatFallbackへ送られる
  CERT_NUMERIC_NAN_INFが残る
```

## 17. 実装時に禁止する簡略化

```txt
1. strand全体を常にrepair対象にすること。
2. 修復後のcertificate再計算を省略すること。
3. repair失敗を描画結果へsilent commitすること。
4. bend/twist/lengthを単一の見た目補正にまとめること。
5. repair taskの上限超過を無視すること。
```