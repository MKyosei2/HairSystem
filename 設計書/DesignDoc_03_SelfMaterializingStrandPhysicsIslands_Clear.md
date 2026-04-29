# Self-Materializing Strand Physics Islands 設計書

## 0. まずここだけ読む

### この機能を一言で言うと
接触・編集・certificate failureで危険になったstrandだけを集め、**そのフレームだけ存在する局所物理island** をGPU上で作る。

### なぜ必要か
全strandに高精度physicsをかけると重い。固定clusterやguide単位では、実際に壊れているstrandの局所性と合わない。そこで、riskの高いpersistent strandIdを近接グラフでつなぎ、connected componentをislandとして解く。

### 実装で作るもの
| 種別 | 作るもの | 実装先 |
|---|---|---|
| 共通型 | `RiskyStrandEntry`, `PhysicsIsland`, `IslandMember`, `IslandSolveResult` | `include/hairgpu/PhysicsIslands.h` |
| CPU参照実装 | connected component、分割、commit判定 | `src/hairgpu_core/PhysicsIslands.cpp` |
| GPU処理 | candidate compact、spatial hash、component生成、solve、commit/reject | `HairPhysicsIslands.hlsl` |
| viewer接続 | island buffers、dispatch args、telemetry | `GpuHairViewer.h/.cpp` |
| 評価出力 | island count、member count、commit/reject、overflow | final_stage tools |

### 入力 → 処理 → 出力
```txt
ModeDecisionBuffer + RiskBuffer + CollisionScene
  -> Compact island candidates
  -> Build spatial hash / proximity graph
  -> Connected components
  -> PhysicsIsland tasks + IslandMemberBuffer
  -> Solve local physics
  -> Commit or Reject by certificate
  -> Repair / Fallback / Render / Log
```

### 最初に実装する順番
```txt
1. PhysicsIsland / IslandMember のstructを定義する
2. CPU版connected componentで動作確認する
3. candidate抽出条件をModeDecision / certificate / contactから作る
4. GPU candidate compact とisland bufferを作る
5. 簡易solverとcommit/rejectを追加する
6. island_count / reject_count / overflow_count をログに出す
```

### 完了条件
```txt
ContactHeavyで危険strandからislandが生成される
maxMembersを超えたislandは分割またはoverflow記録される
solve結果はcertificateなしでcommitされない
Ablation_NoIslandsと同じログschemaで比較できる
```

### この設計書の読み方
まずこの章で「active setではなくframe-local island」であることを掴む。実装時は「Candidate抽出」「近接グラフ」「Commit/Reject条件」を優先して読む。

---
---

# 実装先ファイル早見表

## 新規追加するファイル

| 種別 | ファイル | 目的 |
|---|---|---|
| C++ header | `include/hairgpu/PhysicsIslands.h` | RiskyStrandEntry、PhysicsIsland、IslandMember、IslandSolveResultを定義する |
| C++ source | `src/hairgpu_core/PhysicsIslands.cpp` | CPU検証用のcandidate抽出、connected component、commit/reject判定を実装する |
| HLSL | `apps/dx11_viewer/shaders/HairPhysicsIslands.hlsl` | GPU candidate compaction、spatial hash、label propagation、island task生成を実装する |
| Test | `tests/Test_PhysicsIslands.cpp` | connected component、max members、commit/reject、hysteresisを検証する |

## 変更する既存ファイル

| ファイル | 変更内容 |
|---|---|
| `include/hairgpu/ActiveSet.h` | 既存active-setとの比較用に、active-set clusterからisland入力へ変換するhelperを任意で追加する |
| `src/hairgpu_core/Collision.cpp` / `include/hairgpu/Collision.h` | contact distance / penetration queryをisland候補抽出で使える形にする。既存APIを壊さない |
| `src/hairgpu_core/CMakeLists.txt` | `PhysicsIslands.cpp` を追加する |
| `apps/dx11_viewer/src/GpuHairViewer.h` | RiskyStrands、SpatialHash、IslandTask、IslandMember、StrandToIsland bufferを追加する |
| `apps/dx11_viewer/src/GpuHairViewer.cpp` | shader load、buffer作成、dispatch、commit/reject telemetryを追加する |
| `tools/final_stage/hairgpu_final_db.py` | island count、commit/reject、boundary discontinuityをDBへ取り込む |
| `tools/final_stage/hairgpu_final_excel.py` | island評価表を追加する |

## 既存実装との関係

既存の `ActiveSet` はBaselineとして残す。  
Physics Islandは、active-set clusterをそのまま使うのではなく、`ModeDecision == PhysicsIsland`、certificate failure、contact risk、edit riskから生成する。  
ただし、比較用・debug用としてactive-set cluster IDをログに残してよい。

---

# 実装仕様

## C++ API

```cpp
namespace hairgpu
{
    struct RiskyStrandEntry;
    struct PhysicsIsland;
    struct IslandMember;
    struct PhysicsIslandSettings;
    struct PhysicsIslandStats;

    PhysicsIslandStats BuildPhysicsIslandsCpu(
        std::span<const RiskyStrandEntry> candidates,
        const PhysicsIslandSettings& settings,
        std::vector<PhysicsIsland>& islands,
        std::vector<IslandMember>& members);

    bool ShouldCommitIsland(const IslandSolveResult& result, const PhysicsIslandSettings& settings);
}
```

## HLSL entry point

```txt
CompactIslandCandidatesCS
BuildIslandSpatialHashCS
BuildIslandLabelsCS
CompactIslandMembersCS
BuildIslandTasksCS
SolvePhysicsIslandsCS
CommitOrRejectIslandsCS
BuildIslandDispatchArgsCS
```

卒業研究実装では、label propagation方式を標準とする。Union-Findは任意実装でよい。

---

# 実装タスク

## Task 1: data structureとCPU検証を追加する

実装先:

```txt
include/hairgpu/PhysicsIslands.h
src/hairgpu_core/PhysicsIslands.cpp
src/hairgpu_core/CMakeLists.txt
```

完了条件:

```txt
1. risky strand entriesからconnected componentを作れる。
2. maxMembers超過時に分割またはtruncate reasonを出せる。
3. commit/reject判定がcertificate値で決まる。
```

## Task 2: GPU candidate compactionを追加する

実装先:

```txt
apps/dx11_viewer/shaders/HairPhysicsIslands.hlsl
apps/dx11_viewer/src/GpuHairViewer.cpp
```

完了条件:

```txt
1. ModeDecisionとRiskBufferからRiskyStrandListを作れる。
2. candidate countがGPU counterに入る。
3. capacity超過時にoverflow counterが増える。
```

## Task 3: spatial hash / label propagationを実装する

完了条件:

```txt
1. contactRadius内のcandidateが同じlabelへ収束する。
2. max iteration到達時はnot-converged counterが増える。
3. island taskとmember listが生成される。
```

## Task 4: solver stubとcommit/rejectを接続する

最初の実装では、solverは簡易projectionまたは既存corrective physicsへの接続でよい。  
ただし、commit/reject certificateは必須とする。

完了条件:

```txt
1. island結果がcandidate stateへ書かれる。
2. certificateに通った結果だけcommitされる。
3. reject時にRodRepairまたはfallback reasonが出る。
```

## Task 5: telemetryを拡張する

追加する列:

```txt
island_enabled
island_candidate_count
island_count
island_member_total
island_member_max
island_build_time_ms
island_solve_time_ms
island_commit_count
island_reject_count
island_overflow_count
island_label_not_converged_count
boundary_discontinuity_avg
boundary_discontinuity_p95
```

---

# 受け入れ条件

```txt
1. enablePhysicsIslands=falseではisland countが0になる。
2. enablePhysicsIslands=trueではContactHeavyでcandidateとislandが生成される。
3. island巨大化時にbuffer overflowを起こさず、overflow counterに出る。
4. island solver結果はcertificateなしでcommitされない。
5. Ablation_NoIslandsとProposed_FullSystemを同じログschemaで比較できる。
```

この設計書は、既存プロジェクトへ実装するために必要な構造、入出力、接続先、受け入れ条件を記述する。  
評価指標と比較対象は `Common_Evaluation_and_Baseline.md` を参照する。

---


# 0. 実装定義

この機能は「active set」ではなく、**危険strand ID集合がフレームごとに一時的な局所物理空間として自己実体化する仕組み**として実装する。

```txt
Invariant:
1. islandはpersistent strand IDの集合である。
2. islandはrisk thresholdを超えたstrandの近接グラフconnected componentとして作る。
3. islandにはsolverBudgetMs、bounds、member list、certificateValueを持たせる。
4. island結果はcommit/rejectされ、失敗時はRodRepairまたはFloatFallbackへ送る。
5. merge/splitの振動を防ぐため、hysteresisとminimum lifetimeを持つ。
```

## 0.1 既存技術との差分

guide clusterや固定gridは、事前に定義された単位で処理範囲を決める。  
本機能では、接触・編集・certificate failureから危険になった実strand IDだけを、その場で近接グラフにより束ねる。

```txt
Novelty Boundary:
既存: guide cluster、固定LOD、固定空間分割を処理単位にする。
本研究: runtime riskから実strand ID集合を作り、そのconnected componentを一時的なphysics islandとして生成する。
```

## 0.2 実装範囲

本設計書で求める実装は、固定clusterや単純active setではなく、統合runtime上で危険strand集合から自己生成される局所物理islandである。
実装は以下をすべて満たすこと。

```txt
Implementation Scope:
1. CBMAでPhysicsIsland modeに選ばれたstrand、contact riskが高いstrand、certificate failure strandをcandidateとして抽出する。
2. candidate strandをGPU上でcompactし、spatial hashまたは同等の近接探索構造へ登録する。
3. contactRadius内のcandidate同士をconnected componentとしてまとめ、PhysicsIslandを生成する。
4. islandごとにmember list、bounds、riskScore、solverBudgetMs、certificateValue、lifetime、flagsを保持する。
5. islandのmerge/splitにはhysteresisとminimum lifetimeを入れ、境界のpoppingを抑える。
6. island solverの結果はcertificateでcommit/rejectし、reject時はRodRepairまたはFloatFallbackへ送る。
7. islandが巨大化した場合はbudget、risk、bounds、member countに基づき分割または降格する。
8. islandCount、memberCount、commitRate、rejectRate、boundaryDiscontinuity、solveTimeMsをログへ出す。
```

実装では、islandは補助的なdebug構造ではない。
接触・編集・certificate failureに応じて、実strand ID集合が一時的な物理空間として生成され、成功判定まで含めてruntime制御に参加する。

---

# 目的

接触・編集・破綻リスクの高いpersistent strand ID集合だけを、一時的な局所物理空間として実体化する。

本機能の目的は以下である。

1. 全strandにdense physicsを適用せず、危険領域だけ高精度化する
2. guide clusterではなく実strand ID集合としてislandを作る
3. islandをフレームごとに生成・結合・分割・消滅させる
4. islandごとにsolver budgetを割り当てる
5. island結果をcertificateに基づいてcommitまたはrejectする

---

# 解決する問題

現行プロジェクトでは、局所的な接触や編集破綻に対して以下の問題がある。

1. 全体physicsは重すぎる
2. 単純active setでは接触領域の連続性が弱い
3. guide cluster単位では実strandの局所差を扱いにくい
4. 固定空間分割では接触領域の形に追従できない
5. 局所solverを使う範囲・予算・commit判定が明確でない

本設計では、GPU上で危険strand集合を抽出し、近接グラフ上のconnected componentとしてphysics islandを生成する。

---

# 入力と出力

## 入力

```cpp
struct IslandBuildInput
{
    uint32_t strandCount;

    const StrandState* strandStates;
    const StrandRisk* risks;
    const ModeDecision* modeDecisions;
    const ContactInfo* contacts;

    float riskThreshold;
    float contactRadius;
    uint32_t maxIslandCount;
    uint32_t maxMembersPerIsland;

    float totalIslandBudgetMs;
};
```

---

## 出力

```cpp
struct IslandBuildOutput
{
    PhysicsIsland* islands;
    uint32_t* islandMembers;
    uint32_t* strandToIsland;

    uint32_t islandCount;
    uint32_t memberCount;
};
```

---

# データ構造

## PhysicsIsland

```cpp
struct PhysicsIsland
{
    uint32_t islandId;

    uint32_t memberOffset;
    uint32_t memberCount;

    Vec3 boundsMin;
    Vec3 boundsMax;

    float riskScore;
    float solverBudgetMs;
    float certificateValue;

    uint32_t frameCreated;
    uint32_t lifetime;
    uint32_t flags;
};
```

---

## IslandMember

```cpp
struct IslandMember
{
    uint32_t strandId;
    uint32_t localIndex;
    float risk;
    float boundaryWeight;
};
```

---

## IslandSolveResult

```cpp
struct IslandSolveResult
{
    uint32_t islandId;

    float solveTimeMs;
    float maxPenetration;
    float boundaryDiscontinuity;
    float certificateValue;

    bool commit;
};
```

---

# 数式

strandごとのriskを以下で定義する。

\[
R_j =
w_1 P_j^{fail}
+
w_2 contact_j
+
w_3 density_j
+
w_4 edit_j
+
w_5 residual_j
\]

island候補：

\[
R_j > \tau_R
\]

strandをノード、近接関係をエッジとするグラフ：

\[
G = (V, E)
\]

エッジ条件：

\[
(j,k) \in E
\quad \text{if} \quad
\|p_j - p_k\| < r_{contact}
\]

islandは危険strandのconnected componentとする。

\[
I_l = \{ j \mid j \text{ belongs to risky connected component } l \}
\]

island solverの局所目的関数：

\[
\min_{\Delta X_{I_l}}
E_{\text{stretch}}
+
E_{\text{bend}}
+
E_{\text{collision}}
+
E_{\text{boundary}}
\]

境界制約：

\[
\|\Delta X_{\partial I_l}\| \leq \tau_{\text{boundary}}
\]

budget制約：

\[
T(I_l) \leq T_l^{budget}
\]

---

# アルゴリズム

## BuildIslands

```txt
function BuildIslands:
    1. 各strandのriskを読む
    2. riskThresholdを超えたstrandをcandidateにする
    3. candidate strandをspatial hashへ登録する
    4. 近傍candidate同士をunion-findで結合する
    5. connected componentごとにPhysicsIslandを作る
    6. islandごとにboundsを計算する
    7. island riskScoreを計算する
    8. island budgetを割り当てる
    9. member listをcompactする
    10. island solve taskを生成する
```

---

## SolveIsland

```txt
function SolveIsland(island):
    1. island memberのstrand点を取得またはdecodeする
    2. local frameを構築する
    3. stretch/bend/collision/boundary energyを計算する
    4. budget内でiterationする
    5. certificateを計算する
    6. commit可能なら結果を書き戻す
    7. 失敗した場合はRodRepairまたはfallbackへ送る
```

---

## Merge/Split

### merge条件

```txt
1. island boundsが重なる
2. 接触関係がある
3. 同じcollision objectに触れている
4. certificate failureが連続している
```

### split条件

```txt
1. islandがmaxMembersPerIslandを超えた
2. riskが二峰性を持つ
3. island内部でconnected componentが分離した
4. solver budgetを超過した
```

---

# GPU/CPU処理フロー

## CPU側

```txt
1. island関連パラメータを設定する
2. debug時のみisland数やサイズをreadbackする
3. 通常はisland生成・solver dispatchをGPUで完結させる
```

---

## GPU側

```txt
1. risk bufferを読む
2. risky strandをcompactする
3. spatial hashを構築する
4. union-findまたはlabel propagationでcomponentを作る
5. island bufferを作る
6. island member listを作る
7. island solver用dispatch argsを作る
8. island solverをDispatchIndirectする
9. certificateを更新する
10. commit/rejectする
```

GPUバッファ：

```cpp
RWStructuredBuffer<uint32_t> g_RiskyStrands;
RWStructuredBuffer<SpatialHashEntry> g_SpatialHash;
RWStructuredBuffer<uint32_t> g_UnionFindParent;
RWStructuredBuffer<PhysicsIsland> g_Islands;
RWStructuredBuffer<IslandMember> g_IslandMembers;
RWStructuredBuffer<IslandSolveResult> g_IslandResults;
RWStructuredBuffer<DispatchArgs> g_IslandDispatchArgs;
```

---

# fallback設計

## fallback条件

```txt
1. island memberCount > maxMembersPerIsland
2. island solve time > solverBudgetMs
3. certificateValue > 1
4. boundaryDiscontinuity > threshold
5. penetrationDepthが改善しない
6. islandが連続してcommit失敗する
```

## fallback先

```txt
F0: islandを分割する
F1: island budgetを増やす
F2: island内の高risk strandだけRodRepairへ送る
F3: island全体を低解像度physicsで解く
F4: FloatFallbackへ送る
F5: 次フレームへ持ち越す
```

---

# 予想される失敗例

## 1. islandが巨大化する

原因：

- 密な接触領域でconnected componentが大きくなる

対策：

- maxMembersPerIslandを設定
- graph cutで分割
- risk上位だけsolver対象にする

---

## 2. island境界が不自然に動く

原因：

- island内部だけ物理が強く、外側と不連続になる

対策：

- boundary constraintを入れる
- boundary weightを距離で減衰する
- 周辺strandをghost boundaryとして追加する

---

## 3. merge/splitが毎フレーム振動する

原因：

- risk threshold付近で判定が不安定

対策：

- hysteresisを入れる
- minimum island lifetimeを入れる
- previous island IDを保持する

---

## 4. island solverが予算超過する

原因：

- island countまたはmember countが多すぎる

対策：

- CBMA側でisland候補数を制限する
- solver iterationをbudgetに応じて減らす
- RodRepairへ一部処理を逃がす

---


# 実装受け入れ条件

## 必須ユニットテスト

```txt
Test_Island_ConnectedComponents:
    近接した危険strandが同じislandに入り、離れたstrandが別islandになることを確認する。

Test_Island_MaxMembers:
    maxMembersPerIslandを超えた場合に分割または上位riskだけが採用されることを確認する。

Test_Island_CommitReject:
    solve後certificateValueが閾値以下ならcommit、超過ならrejectになることを確認する。

Test_Island_Hysteresis:
    riskが閾値付近で揺れてもisland IDが毎frame不安定に変わらないことを確認する。
```

## Definition of Done

```txt
1. risky strandからisland listを生成できる。
2. islandMembersとstrandToIslandをGPU/CPUどちらかで確認できる。
3. islandごとのbudget、bounds、riskScoreが出る。
4. solve結果がcommit/rejectされる。
5. Contact-Heavy ScenarioでAblation_NoIslandsと比較できる。
```

## 実装指示

```txt
この設計書に従って、Self-Materializing Strand Physics Islandsを実装してください。
riskThresholdを超えたstrandを抽出し、spatial hashとunion-findでconnected componentを作ってください。
componentごとにPhysicsIslandを生成し、member list、bounds、riskScore、solverBudgetMsを計算してください。
solverは簡易projectionで実装し、commit/reject certificateを必ず実装してください。
```

---

# 論文での主張文

```txt
本研究では、接触や編集により危険になったpersistent strand ID集合を、
GPU上で一時的なphysics islandとして自己実体化する方式を提案する。

提案islandはguide clusterや固定空間分割ではなく、実strand IDの近接グラフ上のconnected componentとして定義され、
merge、split、budget割当、commit certificateを持つ。

これにより、全髪にdense physicsを適用することなく、
必要な局所領域だけ高精度物理を実行できる。
```

---

# 詳細実装定義

この章では、`Self-Materializing Strand Physics Islands` を、単なるactive listではなく、riskから生成されるframe-localな物理空間として実装するための詳細を定義する。

## 1. Island生成の基本概念

Physics Islandは、以下の条件を満たすframe-local objectである。

```txt
1. persistent strandIdの集合として定義される。
2. contact、edit、certificate failure、mode decisionから生成される。
3. 近接グラフのconnected componentである。
4. solver budgetとcommit certificateを持つ。
5. 解けなかった場合も失敗理由を残して後続処理へ渡す。
```

Islandはassetに保存されるclusterではない。毎フレーム生成されるが、poppingを防ぐために短期的な履歴を持つ。

## 2. Candidate抽出

候補strandは以下から抽出する。

```txt
1. ModeDecision == PhysicsIsland
2. CertificateBitsにPENETRATIONがある
3. EditBitsが立っており、編集強度が閾値以上
4. contactDistance < contactRadius
5. recentFailureRiskが高い
6. previousIslandMemberで、まだcooldown中
```

GPU実装では、各strandがcandidateかどうかを判定し、prefix sumまたはappend bufferで`RiskyStrandList`へ詰める。

```cpp
struct RiskyStrandEntry
{
    uint32_t strandId;
    float3 representativePosition;
    float risk;
    uint32_t reasonBits;
};
```

`representativePosition` はroot、tip、worst segment位置のいずれかを使う。接触由来の場合は、接触に最も近いsegment位置を使う。

## 3. 近接グラフ構築

Islandは、候補strand同士の近接関係から生成する。

```txt
Build Proximity Graph:
  1. candidate strandをspatial hashへ登録する。
  2. 各candidateについて近傍cellを探索する。
  3. 距離がcontactRadius以内のcandidate同士にedgeを張る。
  4. risk差が大きすぎる場合でも、penetrationが同じcollision object由来ならedgeを許可する。
  5. connected componentを計算する。
```

spatial hash cell sizeは`contactRadius`と同程度にする。巨大cellに偏る場合はcell内上限を設け、overflow bitを記録する。

## 4. Connected Component実装

GPU上ではUnion-Findまたはラベル伝播を使う。

### 4.1 Union-Find方式

```txt
1. parent[i] = iで初期化する。
2. edgeごとにatomic minでrootを更新する。
3. path compression passを数回実行する。
4. root labelでsortまたはcompactする。
5. 同じroot labelを1つのislandにする。
```

### 4.2 Label Propagation方式

```txt
1. label[i] = iで初期化する。
2. neighbor labelの最小値を採用する。
3. 変化がなくなるまで、またはmax iterationまで繰り返す。
4. labelごとにcompactする。
```

卒業研究実装では、実装容易性を重視してlabel propagationでよい。ただし、max iteration到達時は`ISLAND_LABEL_NOT_CONVERGED`を記録する。

## 5. PhysicsIsland構造

```cpp
struct PhysicsIsland
{
    uint32_t islandId;
    uint32_t memberOffset;
    uint32_t memberCount;

    float3 boundsMin;
    float3 boundsMax;
    float3 averageVelocity;

    float riskScore;
    float solverBudgetMs;
    float certificateValueBefore;
    float certificateValueAfter;

    uint32_t reasonBits;
    uint16_t lifetimeFrames;
    uint16_t cooldownFrames;
};
```

member listは可変長bufferへ保存する。

```cpp
struct IslandMember
{
    uint32_t strandId;
    uint16_t localIndex;
    uint16_t flags;
    float localRisk;
};
```

## 6. Islandの巨大化対策

Islandが巨大化した場合は、以下の順に処理する。

```txt
1. boundsが大きすぎる場合、空間的に分割する。
2. memberCountが上限を超える場合、riskの高いmemberを優先する。
3. solverBudgetMsを超える場合、境界strandを低精度modeへ戻す。
4. それでも超える場合、multi-frame islandとして分割継続する。
5. overflowは必ずログに記録する。
```

分割は、主軸方向に沿ってboundsを2分割する方式でよい。

## 7. Island Solver入力

Island solverは以下を入力にする。

```txt
1. member strandの現在位置
2. member strandの速度
3. rest lengthと局所bend/twist情報
4. collision scene
5. boundary strandの固定条件
6. solverBudgetMs
```

境界条件：

```txt
root側:
  scalp/root bindingに従うため固定または強制追従。

tip側:
  自由端。ただし過度な速度が出る場合はdamping。

island境界:
  island外strandとの不連続を減らすため、前フレーム状態またはQSOnly結果をsoft constraintにする。
```

## 8. Commit / Reject条件

Island solver結果は即commitしない。以下で判定する。

```txt
Commit if:
  lengthResidualAfter <= lengthThreshold
  bendResidualAfter <= bendThreshold
  penetrationAfter <= penetrationThreshold
  no NaN/Inf
  boundaryDiscontinuity <= boundaryThreshold
  solveTimeMs <= solverBudgetMs or quality improvement is large

Reject if:
  residualが悪化した
  penetrationが増えた
  boundaryDiscontinuityが大きい
  NaN/Infが出た
  solverが未収束
```

Reject時は、以下のどれかへ送る。

```txt
1. RodRepair: 局所segment破綻が支配的な場合
2. FloatFallback: 数値破綻またはdecode不能の場合
3. NextFrameIsland: 多strand接触が残っている場合
```

## 9. Merge/Split履歴

islandはframe-localだが、履歴を保持する。

```cpp
struct IslandHistoryEntry
{
    uint32_t historyId;
    float3 previousBoundsMin;
    float3 previousBoundsMax;
    uint32_t previousMemberCount;
    float previousRiskScore;
    uint16_t lifetimeFrames;
    uint16_t stableFrames;
};
```

履歴の紐付けは、bounds overlapとmember overlapで行う。

```txt
matchScore = 0.6 * memberOverlapRatio + 0.4 * boundsOverlapRatio
```

`matchScore` が閾値以上なら同じisland履歴として扱う。

## 10. ログ項目

```txt
islandCount
islandMemberTotal
islandMemberMax
islandBuildTimeMs
islandSolveTimeMs
islandCommitCount
islandRejectCount
islandOverflowCount
islandLabelNotConvergedCount
boundaryDiscontinuityAvg
boundaryDiscontinuityP95
```

## 11. テストケース詳細

### 11.1 接触領域からのIsland生成

```txt
Input:
  球collisionに近いstrand集合
Expected:
  contactRadius内のcandidateが同じislandに入る
  離れたstrandは別islandになる
```

### 11.2 Island分割テスト

```txt
Input:
  memberCountがmaxIslandMembersを超えるcandidate集合
Expected:
  islandが分割される
  overflow counterが増える
  riskが高いstrandが優先される
```

### 11.3 Commit/Rejectテスト

```txt
Input:
  solver後にresidualが改善するislandと悪化するisland
Expected:
  改善islandはcommit
  悪化islandはrejectされ、RodRepairまたはFallbackへ送られる
```

### 11.4 Merge/Split安定性テスト

```txt
Input:
  contact objectが境界付近を移動するscene
Expected:
  island ID履歴が毎フレーム激しく変化しない
  lifetime/hysteresisが効く
```

## 12. 実装時に禁止する簡略化

```txt
1. guide clusterをそのままislandとして扱うこと。
2. candidate listをCPUへreadbackしてislandを作ること。
3. island solver結果をcertificateなしでcommitすること。
4. island巨大化を無視してbuffer overflowを起こすこと。
5. island境界の不連続を評価しないこと。
```