# GPU-Resident Strand Runtime 設計書

## 0. まずここだけ読む

### この機能を一言で言うと
Microtype、CBMA、Physics Island、Rod Repairを、**CPU readbackなしのGPU buffer上の制御ループ** として接続する。

### なぜ必要か
CPUが毎フレーム危険strand、island、repair taskを作ると、同期readbackでp99 frame timeが悪化する。GPU上でrisk、mode、compaction、task生成、commit判定まで行うことで、可変数処理を止めずに進める。

### 実装で作るもの
| 種別 | 作るもの | 実装先 |
|---|---|---|
| 共通runtime型 | `RuntimeSettings`, `RuntimeBuffers`, `RuntimeFrameInput`, `RuntimeCounters` | `include/hairgpu/StrandRuntime.h` |
| CPU orchestration | 初期化、buffer作成、pass順序、ping-pong state | `src/hairgpu_core/StrandRuntime.cpp` / `GpuHairViewer.cpp` |
| HLSL共通 | buffer layout、certificate bits、mode enum | `HairStrandRuntimeCommon.hlsl` |
| HLSL pass | frame loop用compute shader群 | `HairStrandRuntime.hlsl` |
| 評価出力 | pass time、readback count、overflow、p99 | final_stage tools |

### 入力 → 処理 → 出力
```txt
CPU: settings + camera + edit commands + collision scene
  -> GPU RuntimeFrame
      EvaluateCertificate
      EstimateRisk
      AllocateModes
      CompactRiskyStrands
      Build/Solve Islands
      Build/Solve Repairs
      CommitAcceptedStates
      ComposeRenderVertices
  -> DrawIndirect + delayed telemetry
```

### 最初に実装する順番
```txt
1. RuntimeSettings / RuntimeBuffers / counters を定義する
2. GpuHairViewerにfeature flagsとbuffer allocationを追加する
3. pass順序だけを空実装で接続する
4. EvaluationRingBufferとcpu_readback_countを追加する
5. Microtype -> CBMA -> Island -> Repairの順でpassを差し込む
6. DispatchIndirect / DrawIndirect argsをGPUで作る
```

### 完了条件
```txt
release評価pathで同期readbackが発生しない
可変task数はGPU counterからDispatchIndirectできる
buffer overflowはsilent corruptionではなくcounterに出る
Proposed_FullSystem / Ablation_NoGpuResident が同じログschemaで比較できる
```

### この設計書の読み方
まずこの章で「GPUは演算場所ではなく制御ループの常駐場所」という役割を掴む。詳細では「GPU Pass一覧」「Indirect Dispatch」「Buffer Overflow」「Debug Readback」を優先する。

---
---

# 実装先ファイル早見表

## 新規追加するファイル

| 種別 | ファイル | 目的 |
|---|---|---|
| C++ header | `include/hairgpu/StrandRuntime.h` | RuntimeSettings、RuntimeBuffers、RuntimeFrameInput、RuntimeStatsを定義する |
| C++ source | `src/hairgpu_core/StrandRuntime.cpp` | CPU側の設定、buffer contract、stats aggregation、ablation presetを実装する |
| HLSL shared | `apps/dx11_viewer/shaders/HairStrandRuntimeCommon.hlsl` | 共通struct、bitmask、counter、utilityを定義する |
| HLSL pass | `apps/dx11_viewer/shaders/HairStrandRuntime.hlsl` | frame orchestrationに必要なdispatch args、commit、render compose、evaluation counterを実装する |
| Test | `tests/Test_StrandRuntime.cpp` | no-readback、indirect args、buffer overflow、ping-pong stateを検証する |

## 変更する既存ファイル

| ファイル | 変更内容 |
|---|---|
| `apps/dx11_viewer/src/GpuHairViewer.h` | Runtime buffers、feature flags、case settings、debug stats ringを追加する |
| `apps/dx11_viewer/src/GpuHairViewer.cpp` | RuntimeFrame順序、shader load、buffer barrier、dispatch、telemetry出力を追加する |
| `apps/dx11_viewer/CMakeLists.txt` | 新規HLSLをコピー対象に追加する |
| `src/hairgpu_core/CMakeLists.txt` | `StrandRuntime.cpp` を追加する |
| `tools/final_stage/hairgpu_final_run.py` | Runtime feature flagsを環境変数としてviewerへ渡す |
| `tools/final_stage/hairgpu_runtime_suite.py` | Proposed/Ablation/Comboのログを同一schemaで集計する |
| `tools/final_stage/schema.sql` | runtime pass time、readback count、overflow flagsを追加する |

## 既存実装との関係

この機能は5要素を接続する統合runtimeであり、既存viewerの描画・telemetryを置き換えるのではなく、feature flagで切り替える。  
`enableGpuResident=false` の場合は、既存のCPU/viewer制御pathまたはdebug pathを使う。  
`enableGpuResident=true` の場合は、risk/mode/island/repair/certificate関連の可変task生成をGPU上で行い、通常frame pathで同期readbackしない。

---

# 実装仕様

## C++ API

```cpp
namespace hairgpu
{
    struct RuntimeSettings;
    struct RuntimeFrameInput;
    struct RuntimeFrameStats;
    struct RuntimeFeatureFlags;

    RuntimeSettings MakeRuntimeSettingsForVariant(std::string_view variantName);
    RuntimeFeatureFlags DecodeComboVariant(std::string_view comboName);
    void AccumulateRuntimeFrameStats(RuntimeFrameStats& dst, const RuntimeFrameStats& src);
}
```

## HLSL entry point

```txt
ResetRuntimeCountersCS
BuildIndirectArgsCS
CommitAcceptedStatesCS
ApplyFloatFallbackCS
ComposeRenderVerticesCS
WriteEvaluationCountersCS
```

Microtype、CBMA、Island、RodRepairの個別passは、それぞれのHLSLから呼ぶ。  
`HairStrandRuntime.hlsl` は、それらを接続する共通counter、dispatch args、commit、evaluationを担当する。

---

# RuntimeFrame実装先

主な実装先は `apps/dx11_viewer/src/GpuHairViewer.cpp` である。  
既存のrender/update関数の中で、以下の順番を守る。

```txt
1. UploadSettingsAndScene
2. ApplyEditCommands
3. UpdateRootBindings
4. EvaluateMicrotypeCertificates
5. EstimateStrandRisk
6. PredictCounterfactualOutcomes
7. AllocateModesUnderBudget
8. CompactRiskyStrands
9. BuildPhysicsIslands
10. SolvePhysicsIslands
11. BuildRepairTasks
12. SolveRodRepairTasks
13. ApplyFloatFallbackIfNeeded
14. CommitAcceptedStates
15. ComposeRenderVertices
16. WriteEvaluationCounters
17. DrawHair
```

既存pathと混在させる場合でも、評価時はvariantごとにどちらのpathを使ったかをログへ出す。

---

# 実装タスク

## Task 1: RuntimeSettingsとfeature flagを追加する

実装先:

```txt
include/hairgpu/StrandRuntime.h
src/hairgpu_core/StrandRuntime.cpp
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer.cpp
```

完了条件:

```txt
1. Baseline_CurrentProject、Proposed_FullSystem、Ablation、Combo_00000〜Combo_11111を設定へ変換できる。
2. enableMicrotype/enableCBMA/enablePhysicsIslands/enableRodRepair/enableGpuResidentがログへ出る。
3. feature flag OFF時に既存pathへ戻る。
```

## Task 2: RuntimeBuffersを追加する

完了条件:

```txt
1. Microtype、certificate、risk、mode、island、repair、candidate state、render vertex、counter、dispatch argsのbufferが確保される。
2. capacityがsettingsから決まる。
3. capacity超過時にoverflow counterが出る。
```

## Task 3: Indirect dispatchとcounter resetを実装する

完了条件:

```txt
1. frame開始時にruntime counterがresetされる。
2. RepairTaskCounterやIslandTaskCounterからDispatchIndirect argsが生成される。
3. task count 0でもクラッシュしない。
```

## Task 4: ping-pong stateとcommitを実装する

完了条件:

```txt
1. StrandStateBufferA/Bを交互にread/writeする。
2. candidate stateはcommitまでpersistent stateを上書きしない。
3. certificate失敗時はLastStableStateまたはFloatFallbackへ逃がす。
```

## Task 5: delayed debug readbackを実装する

完了条件:

```txt
1. release評価pathにblocking readbackが残らない。
2. debug statsはring bufferへ書かれる。
3. cpu_readback_countをログへ出せる。
```

## Task 6: telemetryを拡張する

追加する列:

```txt
gpu_resident_enabled
cpu_readback_count
runtime_overflow_flags
runtime_dispatch_count
runtime_indirect_dispatch_count
pass_time_certificate_ms
pass_time_risk_ms
pass_time_mode_ms
pass_time_island_build_ms
pass_time_island_solve_ms
pass_time_repair_build_ms
pass_time_repair_solve_ms
pass_time_commit_ms
pass_time_render_compose_ms
```

---

# 受け入れ条件

```txt
1. Proposed_FullSystemでRuntimeFrameが1frame完走する。
2. release評価pathで同期readbackが発生しない。
3. Indirect argsがtask countに応じて変わる。
4. Buffer overflowがsilent corruptionにならない。
5. Ablation_NoGpuResidentとProposed_FullSystemを同じログschemaで比較できる。
6. p50/p95/p99 frame timeとpass別GPU時間が出る。
```

この設計書は、既存プロジェクトへ実装するために必要な構造、入出力、接続先、受け入れ条件を記述する。  
評価指標と比較対象は `Common_Evaluation_and_Baseline.md` を参照する。

---


# 0. 実装定義

この機能は「GPUで処理する」こと自体ではなく、**risk、mode、island、repair、certificate更新をCPU readbackなしでGPU上に閉ループ化するruntime**として実装する。

```txt
Invariant:
1. strand state、risk、mode decision、certificateはGPU bufferに常駐する。
2. CPUはsettings、edit command、collision scene更新だけを送る。
3. risky strand compaction、mode allocation、island task generation、repair task generationはGPU上で行う。
4. 可変数タスクはDispatchIndirect / DrawIndirectで実行する。
5. debug statsのreadbackはN frame遅延またはdebug build限定にする。
```

## 0.1 既存技術との差分

TressFXなど既存GPU実装は、GPU上で髪をシミュレーション・描画する点で重要である。  
本機能の差分は、GPU上のhair runtimeをcertificate中心の制御ループとして構成し、可変数の危険strand・island・repairをCPUへ戻さず処理する点である。

```txt
Novelty Boundary:
既存: GPU shaderで髪を更新・描画する。
本研究: GPU上で危険度推定、反実仮想mode選択、局所物理生成、repair生成、certificate更新を閉ループ化する。
```

## 0.2 実装範囲

本設計書で求める実装は、単にGPU shaderで髪を更新するものではなく、5つの新規性をGPU常駐の閉ループとして統合したruntimeである。
実装は以下をすべて満たすこと。

```txt
Implementation Scope:
1. strandMicrotypes、payload、strandStates、certificates、risks、modeDecisions、islandTasks、repairTasks、decodedPoints、renderVerticesをGPU bufferとして常駐させる。
2. CPUはRuntimeSettings、EditCommandBuffer、CollisionScene、CameraDataだけを送る。
3. risk評価、certificate更新、mode allocation、risky strand compaction、island task生成、repair task生成、dispatch args生成をGPU上で実行する。
4. 可変数処理はDispatchIndirect / DrawIndirectで実行し、CPU readbackを通常frame pathに入れない。
5. Microtype decode、CBMA、PhysicsIsland、RodRepair、FloatFallback、render composeをRuntimeFrame内で一貫したbuffer contractに接続する。
6. debugStatsはring bufferでN frame遅延readbackにし、release buildでは同期readbackを禁止する。
7. ablation切替をRuntimeSettingsで行い、共通評価ログのschemaを必ず満たす。
8. p50/p95/p99 frame time、GPU pass time、mode counts、island counts、repair counts、certificate failure countsを毎frame記録する。
```

実装では、GPU化そのものではなく、**certificateを中心にrisk、mode、island、repair、fallback、renderを閉ループ化すること**を成功条件にする。

---

# 目的

5つの新規性をGPU上で閉ループ化し、CPU依存を極限まで減らす。

本機能の目的は以下である。

1. strand状態をGPU上に常駐させる
2. CPU readbackを避ける
3. mode allocationをGPUで行う
4. island生成をGPUで行う
5. rod repair task生成をGPUで行う
6. DispatchIndirect / DrawIndirectで可変処理を行う
7. 60FPS制約下でruntime全体を動かす

---

# 解決する問題

現行プロジェクトでは、CPU側の制御やreadbackが増えると以下の問題が起きる。

1. CPU-GPU同期でframe timeが不安定になる
2. 可変数のislandやrepair taskをCPUで作ると遅い
3. 10万strandの状態をCPUへ戻すと帯域が大きすぎる
4. p99 frame timeが悪化する
5. 制作中のinteractive editingで遅延が出る

本設計では、GPU上でrisk、mode、island、repair、certificateを更新し、CPUはパラメータ設定と表示だけを担当する。

---

# 入力と出力

## 入力

```cpp
struct RuntimeInput
{
    uint32_t strandCount;
    uint32_t frameIndex;

    float deltaTime;
    float frameBudgetMs;

    CameraData camera;
    EditCommandBuffer editCommands;
    CollisionScene collisionScene;

    RuntimeSettings settings;
};
```

---

## 出力

```cpp
struct RuntimeOutput
{
    GPUBuffer decodedRenderBuffer;
    GPUBuffer certificateBuffer;
    GPUBuffer debugStatsBuffer;

    uint32_t drawIndirectOffset;
    uint32_t dispatchIndirectOffset;
};
```

---

# データ構造

## StrandState

```cpp
struct StrandState
{
    uint32_t strandId;

    Vec3 rootPosition;
    Vec3 velocity;

    float risk;
    float certificateValue;

    uint8_t currentMode;
    uint8_t previousMode;
    uint16_t flags;
};
```

---

## RuntimeBuffers

```cpp
struct RuntimeBuffers
{
    GPUBuffer strandMicrotypes;
    GPUBuffer microtypePayload;

    GPUBuffer strandStates;
    GPUBuffer certificates;
    GPUBuffer risks;
    GPUBuffer modeDecisions;

    GPUBuffer riskyStrands;
    GPUBuffer islandTasks;
    GPUBuffer islandMembers;
    GPUBuffer repairTasks;

    GPUBuffer decodedPoints;
    GPUBuffer renderVertices;

    GPUBuffer dispatchArgs;
    GPUBuffer drawArgs;
    GPUBuffer debugStats;
};
```

---

## DispatchArgs

```cpp
struct DispatchArgs
{
    uint32_t threadGroupCountX;
    uint32_t threadGroupCountY;
    uint32_t threadGroupCountZ;
};
```

---

# 数式

GPU常駐runtime自体は数式よりも制御構造が中心である。  
ただし、各フレームは以下の閉ループとして定義する。

\[
S_t
\rightarrow
R_t
\rightarrow
M_t
\rightarrow
I_t
\rightarrow
P_t
\rightarrow
C_t
\rightarrow
S_{t+1}
\]

ここで、

- \(S_t\): strand state
- \(R_t\): risk
- \(M_t\): mode decision
- \(I_t\): island tasks
- \(P_t\): repaired / simulated positions
- \(C_t\): certificate
- \(S_{t+1}\): next strand state

CPU readbackを伴う場合の同期コストを、

\[
T_{sync}
\]

とすると、本runtimeの目的は、

\[
T_{sync} \approx 0
\]

にすることである。

総frame time：

\[
T_{frame}
=
T_{decode}
+
T_{risk}
+
T_{mode}
+
T_{island}
+
T_{repair}
+
T_{render}
+
T_{sync}
\]

制約：

\[
T_{frame} \leq 16.67ms
\]

---

# アルゴリズム

## RuntimeFrame

```txt
function RuntimeFrame:
    1. ApplyEditCommands
    2. UpdateStrandStates
    3. DecodeMicrotypesForNeededStrands
    4. EvaluateCertificates
    5. ComputeRisks
    6. AllocateModes
    7. CompactByMode
    8. BuildPhysicsIslands
    9. BuildRepairTasks
    10. DispatchIslandSolvers
    11. DispatchRodRepair
    12. UpdateCertificates
    13. CommitResults
    14. BuildRenderVertices
    15. DrawIndirect
```

---

## GPU pass構成

```txt
Pass 1: ApplyEditsCS
Pass 2: DecodeNeededStrandsCS
Pass 3: EvaluateCertificateCS
Pass 4: ComputeRiskCS
Pass 5: AllocateModeCS
Pass 6: CompactModeCS
Pass 7: BuildIslandCandidatesCS
Pass 8: BuildIslandsCS
Pass 9: BuildRepairTasksCS
Pass 10: SolveIslandsCS
Pass 11: SolveRodRepairCS
Pass 12: CommitResultsCS
Pass 13: BuildRenderVerticesCS
Pass 14: BuildIndirectArgsCS
```

---

# GPU/CPU処理フロー

## CPU側

CPUが行うこと：

```txt
1. RuntimeSettingsを更新する
2. EditCommandBufferをGPUへ送る
3. CameraDataをGPUへ送る
4. CollisionSceneの更新差分をGPUへ送る
5. GPU passを順番にdispatchする
6. debug時のみ統計をreadbackする
```

CPUが行わないこと：

```txt
1. 全strand状態のreadback
2. 全certificateのreadback
3. island member listの構築
4. repair task listの構築
5. mode decisionのCPU計算
```

---

## GPU側

GPUが行うこと：

```txt
1. microtype decode
2. risk計算
3. certificate計算
4. mode allocation
5. compaction
6. island生成
7. repair task生成
8. solver dispatch args生成
9. render vertex生成
10. draw args生成
```

---

# fallback設計

## fallback条件

```txt
1. GPU passが予算を超える
2. island task数が上限を超える
3. repair task数が上限を超える
4. DispatchIndirectがサポートされない
5. GPU memory不足
6. debug readbackでstallが発生する
```

## fallback先

```txt
F0: repair task数を制限する
F1: island数を制限する
F2: microtype decode範囲を減らす
F3: render LODを下げる
F4: CPU fallback modeを使う
F5: debug readbackを間引く
```

---

# 予想される失敗例

## 1. GPU passが多すぎる

原因：

- 各処理を細かく分けすぎる

対策：

- pass fusionを行う
- riskとcertificateを同じpassで計算する
- mode allocationとcompactionを統合する

---

## 2. indirect dispatch overheadが大きい

原因：

- 小さいtaskを大量にdispatchしている

対策：

- task batchingを行う
- island solverをpersistent kernel化する
- 小さいrepair taskをまとめる

---

## 3. CPU-GPU同期が残る

原因：

- debug statsやreadbackを毎フレーム行っている

対策：

- ring buffer化
- N frame遅延readback
- debug buildのみreadback

---

## 4. GPU memory不足

原因：

- intermediate bufferが多い
- island/repairの最大数を大きく取りすぎる

対策：

- buffer aliasingを行う
- max task countを制限する
- dynamic allocationではなくfixed poolを使う

---


# 実装受け入れ条件

## 必須ユニットテスト / 統合テスト

```txt
Test_Runtime_NoReadbackRelease:
    release評価モードで毎frame CPU readbackが発生しないことを確認する。

Test_Runtime_IndirectArgs:
    riskyStrands / islandTasks / repairTasksの数に応じてDispatchIndirect argsが変わることを確認する。

Test_Runtime_BufferCapacity:
    max task count超過時に安全にtruncateまたはfallbackすることを確認する。

Test_Runtime_EndToEnd:
    Microtype -> Risk -> Mode -> Island -> Repair -> Certificate -> Render のpass列が1frameで完了することを確認する。
```

## Definition of Done

```txt
1. RuntimeFrameがpass列として実行できる。
2. CPU readback countをログに出せる。
3. GPU buffer residencyが保たれる。
4. Proposed_FullSystemとAblation_NoGpuResidentを比較できる。
5. p50/p95/p99 frame timeを出せる。
```

## 実装指示

```txt
この設計書に従って、GPU-Resident Strand Runtimeを実装してください。
RuntimeBuffers、RuntimeSettings、RuntimeFrameのpass orchestrationを実装として作成し、5要素のbuffer contractを接続してください。
次にrisk計算、mode allocation、risky strand compaction、island task generation、repair task generation、certificate update、render decodeをGPU passとして接続してください。
CPU readbackはdebug statsのみN frame遅延で行う設計にしてください。
```

---

# 論文での主張文

```txt
本研究では、Microtype、Mode Allocation、Physics Islands、Rod RepairをCPU制御で接続するのではなく、
GPU上で閉ループ化するGPU-Resident Strand Runtimeを実装する。

このruntimeは、risk計算、mode decision、compaction、island生成、repair task生成、indirect dispatchをGPU上で行い、
CPU readbackをdebug時以外避ける。

これにより、10万本規模の髪に対して、固定60FPS制約下で高密度表示、局所編集、接触補正、certificate更新を同時に実行できる。
```

---

# 詳細実装定義

この章では、`GPU-Resident Strand Runtime` を、単にcompute shaderで髪を更新する実装ではなく、certificateを中心にしたGPU常駐制御ループとして実装するための詳細を定義する。

## 1. GPU常駐の定義

本runtimeでのGPU常駐とは、以下を満たす状態を指す。

```txt
1. strand状態、certificate、risk、mode decisionがGPU buffer上に存在する。
2. 通常frame pathでCPUが全strand状態をreadbackしない。
3. risky strand list、island task、repair taskをGPU上で生成する。
4. 可変数処理のdispatch数をGPU上で生成する。
5. CPUはsettings、camera、collision、edit commandを送るだけである。
```

debugや評価のためのreadbackは許可するが、N frame遅延のring bufferとし、frame進行を止めない。

## 2. GPU Pass一覧

標準pass構成は以下とする。

| Pass | 入力 | 出力 | Dispatch方式 |
|---|---|---|---|
| `ApplyEditCommandsCS` | edit commands, strand state | editBits, candidate state | fixed |
| `UpdateRootBindingsCS` | root transforms | strand state | fixed |
| `EvaluateCertificateCS` | microtype, state | certificate buffer | fixed |
| `EstimateRiskCS` | certificate, collision, camera | risk buffer | fixed |
| `PredictOutcomesCS` | risk, certificate, cost history | outcome buffer | fixed |
| `AllocateModesCS` | outcome, budget | mode decisions | fixed + reductions |
| `CompactRiskyStrandsCS` | mode, risk | risky list, counters | fixed |
| `BuildSpatialHashCS` | risky list | hash table | indirect/fixed |
| `BuildIslandsCS` | hash table | island tasks, members | indirect |
| `SolveIslandsCS` | island tasks | candidate state | indirect |
| `BuildRepairTasksCS` | certificate, candidate state | repair tasks | fixed/indirect |
| `SolveRepairsCS` | repair tasks | candidate state | indirect |
| `CommitStatesCS` | candidate state, certificate | state buffer B | fixed |
| `ComposeRenderVerticesCS` | state/microtype | render vertices | fixed/indirect |
| `WriteEvaluationCountersCS` | counters | evaluation ring | fixed |
| `DrawHair` | render vertices | framebuffer | draw indirect |

## 3. CPU側入力

CPUから送るデータは以下に限定する。

```cpp
struct RuntimeFrameInput
{
    RuntimeSettings settings;
    CameraData camera;
    CollisionSceneGpuData collisionScene;
    EditCommandBuffer editCommands;
    RootTransformBuffer rootTransforms;
    uint32_t frameIndex;
    float deltaTime;
};
```

CPU側でやらないこと：

```txt
1. risky strand listの作成
2. mode decisionの作成
3. island member listの作成
4. repair task listの作成
5. per-strand certificate判定
6. per-strand render visibilityの同期readback
```

## 4. Indirect Dispatch設計

可変数taskはGPU上でcounterを作り、indirect argsへ変換する。

```cpp
struct DispatchArgs
{
    uint32_t groupCountX;
    uint32_t groupCountY;
    uint32_t groupCountZ;
};
```

例：repair task dispatch

```txt
BuildRepairTasksCS:
  RepairTaskCounterを増やす。

BuildRepairDispatchArgsCS:
  groupCountX = ceil(RepairTaskCounter / THREADS_PER_GROUP)

SolveRepairsCS:
  DispatchIndirect(RepairDispatchArgs)
```

counterが0の場合もdispatchが安全に無視されるようにする。

## 5. Buffer Overflow対策

可変長bufferには必ず上限がある。

```txt
RiskyStrandList capacity = strandCount
IslandTask capacity = maxIslandCount
IslandMember capacity = maxIslandMembersTotal
RepairTask capacity = maxRepairTasks
RenderVertex capacity = maxRenderVertices
```

append時にcapacityを超える場合：

```txt
1. 書き込まない。
2. overflow counterを増やす。
3. overflow bitをEvaluationRingBufferへ記録する。
4. 該当strandは安全側のfallbackへ送る。
```

silent overflowは禁止する。

## 6. Resource Barrierと同期

pass間で同じbufferをread/writeするため、barrierを明示する。

```txt
After EvaluateCertificateCS:
  CertificateBuffer UAV -> SRV barrier

After AllocateModesCS:
  ModeDecisionBuffer UAV -> SRV barrier
  RuntimeCounterBuffer UAV -> indirect args barrier if needed

After BuildIslandsCS:
  IslandTaskBuffer UAV -> SRV barrier
  IslandMemberBuffer UAV -> SRV barrier
  IslandDispatchArgs UAV -> indirect args barrier

After SolveRepairsCS:
  CandidateStateBuffer UAV -> SRV barrier
  CertificateBuffer UAV -> SRV barrier

After ComposeRenderVerticesCS:
  RenderVertexBuffer UAV -> vertex buffer barrier
  DrawArgsBuffer UAV -> indirect args barrier
```

APIがDirectX、Vulkan、Metalのいずれでも、抽象layerで同等のbarrierを表現する。

## 7. Ping-Pong State

persistent stateはdouble bufferにする。

```txt
Frame N:
  read:  StrandStateBufferA
  write: StrandStateBufferB

Frame N+1:
  read:  StrandStateBufferB
  write: StrandStateBufferA
```

同じframe内のcandidate結果は別bufferへ書く。

```txt
StrandStateCandidateBuffer:
  island、repair、fallbackの候補状態。

CommitStatesCS:
  certificateに通ったcandidateだけを次のpersistent stateへ反映する。
```

## 8. Debug Readback

debug情報はring bufferへ書き、遅延readbackする。

```cpp
struct EvaluationFrameStats
{
    uint32_t frameIndex;
    float gpuHairTimeMs;
    float p50EstimatedCostMs;
    float p95EstimatedCostMs;

    uint32_t modeCount[8];
    uint32_t certificateFailureCount;
    uint32_t islandCount;
    uint32_t repairTaskCount;
    uint32_t fallbackCount;
    uint32_t overflowFlags;
};
```

readback規則：

```txt
1. release buildでは同期readback禁止。
2. debug buildでもN frame遅延で読む。
3. GPU timestamp queryもdouble/triple bufferedにする。
4. per-strand詳細ログはsampled strandだけに限定する。
```

## 9. GPU Passの異常終了対策

compute shader内でassert停止はできないため、異常はflagへ書く。

```txt
Invalid payload:
  CERT_DECODE_ERRORを立てる。

NaN/Inf:
  CERT_NUMERIC_NAN_INFを立てる。

Buffer overflow:
  overflow counterを増やす。

Non-converged island:
  island reject flagを立てる。

Repair non-converged:
  repair reject flagを立てる。
```

## 10. Render Compose

描画は、selectedModeに応じて入力を変える。

```txt
Skip:
  previous accepted render stateを使う。

QSOnly / DynamicResidual:
  Microtypeからrender decodeする。

PhysicsIsland / RodRepair:
  committed candidate stateを使う。

FloatFallback:
  float state bufferを使う。
```

render composeは、certificate failure状態をそのまま描画しない。最低でも前フレームaccepted stateまたはFloatFallbackを使う。

## 11. Ablation切替

評価のため、各機能を無効化できる。

```txt
enableMicrotype = false:
  float stateを使用し、Microtype certificateは無効化する。

enableCBMA = false:
  固定mode規則を使う。

enableIslands = false:
  PhysicsIsland modeをRodRepairまたはDynamicResidualへ変換する。

enableRodRepair = false:
  repair taskをFloatFallbackまたはIsland継続へ送る。

enableGpuResident = false:
  debug用CPU pathを使えるが、評価時は別baselineとして扱う。
```

ablation時もログschemaは変えない。無効化されたmoduleのcountは0として出す。

## 12. Performance Instrumentation

GPU passごとの時間をtimestamp queryで測る。

```txt
passTimeMs[EvaluateCertificate]
passTimeMs[EstimateRisk]
passTimeMs[PredictOutcomes]
passTimeMs[AllocateModes]
passTimeMs[BuildIslands]
passTimeMs[SolveIslands]
passTimeMs[BuildRepairTasks]
passTimeMs[SolveRepairs]
passTimeMs[ComposeRender]
```

合計だけでなく、p95/p99 frame timeを見る。CPU同期が混ざるとp99が悪化するため、p99は本機能の重要指標である。

## 13. テストケース詳細

### 13.1 CPU Readback禁止テスト

```txt
Input:
  release runtime build
Expected:
  通常frame中にblocking readback APIが呼ばれない
  Evaluation readbackは遅延ring buffer経由のみ
```

### 13.2 Indirect Dispatchテスト

```txt
Input:
  repair task 0件、1件、多数件
Expected:
  counterから正しいdispatch argsが生成される
  0件でもクラッシュしない
```

### 13.3 Buffer Overflowテスト

```txt
Input:
  maxRepairTasksを超えるcertificate failure
Expected:
  overflow counterが増える
  範囲外書き込みが起きない
  残りstrandがfallbackへ送られる
```

### 13.4 Ping-Pong Stateテスト

```txt
Input:
  連続10 frameのsimulation
Expected:
  read/write bufferが交互に切り替わる
  同一frameでread bufferを破壊しない
```

## 14. 実装時に禁止する簡略化

```txt
1. risky strand listをCPUで作ること。
2. island/repair task数をCPU readbackしてdispatchすること。
3. release frame pathに同期readbackを入れること。
4. buffer overflowを無視すること。
5. pass時間を合計値だけで記録し、module別に分けないこと。
```