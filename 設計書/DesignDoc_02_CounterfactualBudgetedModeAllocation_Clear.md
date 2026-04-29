# Counterfactual Budgeted Mode Allocation 設計書

## 0. まずここだけ読む

### この機能を一言で言うと
各strandについて `Skip / QSOnly / DynamicResidual / PhysicsIsland / RodRepair / FloatFallback` のどれを選ぶかを、**予測コスト・予測品質・破綻リスク・frame budget** から決める。

### なぜ必要か
距離LODや固定閾値だけでは、接触や編集で危険なstrandを守れない。一方で高品質処理を増やしすぎると60FPSを割る。そこで、選ばなかったmodeも含めて「選んだらどうなるか」を予測し、予算内に収める。

### 実装で作るもの
| 種別 | 作るもの | 実装先 |
|---|---|---|
| CPU/GPU共通型 | `SimulationMode`, `CounterfactualOutcome`, `ModeDecision`, `StrandRisk` | `include/hairgpu/ModeAllocation.h` |
| CPU参照実装 | risk計算、予測、greedy割当、test用実装 | `src/hairgpu_core/ModeAllocation.cpp` |
| GPU処理 | risk推定、outcome予測、mode割当、mode別compact | `apps/dx11_viewer/shaders/HairModeAllocation.hlsl` |
| viewer接続 | settings反映、buffer作成、pass dispatch | `GpuHairViewer.h/.cpp` |
| 評価出力 | mode count、predicted/actual cost、budget overflow | final_stage tools |

### 入力 → 処理 → 出力
```txt
CertificateBuffer + ContactInfo + EditBits + previous ModeDecision
  -> EstimateStrandRisk
  -> Predict outcomes for all modes
  -> Allocate under gpuHairBudgetMs / bandwidth / memory
  -> ModeDecisionBuffer
  -> Island / Repair / Render / Log
```

### 最初に実装する順番
```txt
1. SimulationMode と ModeDecision を定義する
2. CPU版のrisk計算とgreedy allocationを作る
3. certificate failure strand が Skip にならないtestを書く
4. GPU版のrisk / predict / allocate passを追加する
5. mode count と budget overflow をtelemetryへ出す
```

### 完了条件
```txt
全strandにModeDecisionが出る
certificate failure strand は Skip にならない
predicted cost が budget を超えないように降格できる
mode_counts / forcedDowngrade / hysteresisBlocked がログに出る
```

### この設計書の読み方
まずこの章で役割を掴み、次に「実装先ファイル早見表」と「割当アルゴリズム」を読む。数式は目的関数の説明であり、実装はgreedy方式を標準とする。

---
---

# 実装先ファイル早見表

## 新規追加するファイル

| 種別 | ファイル | 目的 |
|---|---|---|
| C++ header | `include/hairgpu/ModeAllocation.h` | SimulationMode、StrandRisk、CounterfactualOutcome、ModeDecisionを定義する |
| C++ source | `src/hairgpu_core/ModeAllocation.cpp` | CPU検証用predictor、budget allocation、hysteresis、unit test用処理を実装する |
| HLSL | `apps/dx11_viewer/shaders/HairModeAllocation.hlsl` | GPU risk計算、outcome予測、mode割当、mode別compactionを実装する |
| Test | `tests/Test_ModeAllocation.cpp` | budget、Skip禁止、hysteresis、counterfactual loggingを検証する |

## 変更する既存ファイル

| ファイル | 変更内容 |
|---|---|
| `include/hairgpu/ActiveSet.h` | 既存active-set summaryとの対応を取るため、必要なら変換helperを追加する |
| `src/hairgpu_core/ActiveSet.cpp` | 既存active-setをBaselineとして残し、CBMA用変換は別関数に分離する |
| `src/hairgpu_core/CMakeLists.txt` | `ModeAllocation.cpp` を追加する |
| `apps/dx11_viewer/src/GpuHairViewer.h` | Risk/Outcome/ModeDecision GPU buffer、mode count telemetryを追加する |
| `apps/dx11_viewer/src/GpuHairViewer.cpp` | shader load、buffer作成、dispatch、CSV/JSONL出力を追加する |
| `apps/dx11_viewer/shaders/HairActiveSet.hlsl` | 原則変更しない。必要な場合はBaseline pathとして維持する |
| `tools/final_stage/hairgpu_final_db.py` | mode count、prediction error、budget overflowをDBへ取り込む |
| `tools/final_stage/hairgpu_final_excel.py` | mode allocation表を追加する |

## 既存実装との関係

`ActiveSet` は削除しない。  
`Baseline_CurrentProject` と `Ablation_NoCBMA` では既存active-set / fixed tier pathを使う。  
`enableCBMA=true` の場合だけ、`ModeAllocation` の出力を後続のIsland、Repair、Render composeが参照する。

---

# 実装仕様

## C++ API

```cpp
namespace hairgpu
{
    enum class SimulationMode : uint8_t;
    struct StrandRisk;
    struct CounterfactualOutcome;
    struct ModeDecision;
    struct ModeAllocationSettings;
    struct ModeAllocationStats;

    void PredictCounterfactualOutcomesCpu(
        std::span<const StrandRisk> risks,
        std::span<const UnifiedStrandCertificate> certificates,
        const ModeAllocationSettings& settings,
        std::span<CounterfactualOutcome> outcomes);

    ModeAllocationStats AllocateModesCpu(
        std::span<const CounterfactualOutcome> outcomes,
        const ModeAllocationSettings& settings,
        std::span<ModeDecision> decisions);
}
```

CPU APIはGPU実装の検証用であり、評価の通常frame pathではGPU pathを使う。

## HLSL entry point

```txt
EstimateStrandRiskCS
PredictCounterfactualOutcomesCS
AllocateModesCS
CompactModeBucketsCS
BuildModeDispatchArgsCS
```

---

# 実装タスク

## Task 1: data structureを追加する

実装先:

```txt
include/hairgpu/ModeAllocation.h
src/hairgpu_core/ModeAllocation.cpp
src/hairgpu_core/CMakeLists.txt
```

完了条件:

```txt
1. SimulationModeが共通定義として参照できる。
2. CounterfactualOutcomeをmode数ぶん保持できる。
3. ModeDecisionがstrandId基準で出力される。
```

## Task 2: CPU predictor/allocatorを作る

完了条件:

```txt
1. frameBudgetMs以下に収めるgreedy allocationが動く。
2. certificate failure strandがSkipにならない。
3. previousModeとminimum lifetimeが効く。
4. forcedDowngradeCount、hysteresisBlockedSwitchCountを集計できる。
```

## Task 3: GPU pathをviewerへ接続する

実装先:

```txt
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer.cpp
apps/dx11_viewer/shaders/HairModeAllocation.hlsl
```

完了条件:

```txt
1. RiskBuffer、OutcomeBuffer、ModeDecisionBufferが作成される。
2. GPU上でmode countが集計される。
3. mode別compaction bufferが作られる。
4. `enableCBMA=false` のときは既存active-set pathへ戻る。
```

## Task 4: telemetryを拡張する

追加する列:

```txt
cbma_enabled
mode_skip_count
mode_qs_count
mode_dynamic_count
mode_island_count
mode_repair_count
mode_float_fallback_count
predicted_cost_ms
actual_cost_ms_estimate
budget_overflow_count
forced_downgrade_count
forced_upgrade_count
skip_forbidden_count
hysteresis_blocked_switch_count
```

## Task 5: testを追加する

```txt
Test_CBMA_BudgetRespect
Test_CBMA_CertificatePromotion
Test_CBMA_Hysteresis
Test_CBMA_AblationCompatibility
```

---

# 受け入れ条件

```txt
1. Baseline_CurrentProjectでは既存active-setが動く。
2. enableCBMA=trueで全strandにModeDecisionが出る。
3. Proposed_FullSystemとAblation_NoCBMAで同じログschemaが出る。
4. mode countの合計がstrandCountまたは処理対象strand数と一致する。
5. p99悪化時にbudgetOverflowCountとpass timeから原因を追える。
```

この設計書は、既存プロジェクトへ実装するために必要な構造、入出力、接続先、受け入れ条件を記述する。  
評価指標と比較対象は `Common_Evaluation_and_Baseline.md` を参照する。

---


# 0. 実装定義

この機能は「LOD選択」ではなく、**各strandについて複数modeを選んだ場合の反実仮想結果を見積もり、frame budget内で割り当てる制御問題**として実装する。

```txt
Invariant:
1. 各strandについて全candidate modeのpredictedError、predictedFailureProbability、predictedCostMsを持つ。
2. 選択結果はframeBudgetMs、bandwidthBudgetMB、memoryBudgetMBを超えないようにする。
3. certificateが危険なstrandはSkip禁止にする。
4. mode switchingにはhysteresisとminimum lifetimeを持たせる。
5. 予算超過時は、品質劣化の小さいstrandから降格する。
```

## 0.1 既存技術との差分

既存のLODは、距離、画面サイズ、strand count、artist設定などで処理量を落とすことが多い。  
本機能の差分は、各modeを選んだ場合の品質・失敗確率・コストを予測し、限られた時間予算をstrand間で配分する点である。

```txt
Novelty Boundary:
既存: 距離・画面占有率・固定閾値でLODや処理量を変える。
本研究: modeごとの反実仮想結果を予測し、破綻確率とframe budgetを同時に満たす割当問題として解く。
```

## 0.2 実装範囲

本設計書で求める実装は、固定LODや単純heuristicではなく、統合runtime上で毎フレーム動作するmode allocationである。
実装は以下をすべて満たすこと。

```txt
Implementation Scope:
1. 全strandについて、Skip、QSOnly、DynamicResidual、PhysicsIsland、RodRepair、FloatFallbackのcandidate outcomeを計算する。
2. predictedError、predictedFailureProbability、predictedCostMs、predictedBandwidthMB、predictedMemoryMB、predictedCertificateValueをmodeごとに保持する。
3. frameBudgetMs、gpuBudgetMs、memoryBudgetMB、bandwidthBudgetMBを同時に満たすmode decisionを出力する。
4. certificate failure、contact risk、edit riskが高いstrandはSkip禁止または強制昇格にする。
5. hysteresis、minimum lifetime、previousMode penaltyによりmode poppingを抑える。
6. 予算超過時は、quality lossが小さいstrandから降格し、危険strandは最後まで保護する。
7. 選択結果をGPU bufferに書き、PhysicsIsland、RodRepair、render decodeが同じmode decisionを参照できるようにする。
8. predicted costとactual costの差分をログへ出し、次フレーム以降のcost estimate更新に利用する。
```

実装では「どのmodeを選んだか」だけでなく、**選ばなかったmodeを選んだ場合の予測結果**も保持する。
これにより、単なるLODではなく、反実仮想に基づくbudgeted allocationとして論文で説明できる。

---

# 目的

固定60FPS制約下で、各strandに対して最適な計算モードを割り当てる。

本機能の目的は以下である。

1. 各strandについて、複数の計算モードを選んだ場合の結果を予測する
2. 誤差・破綻確率・計算コストを使ってmodeを選ぶ
3. 16.67ms/frameの制約を守る
4. 危険なstrandだけ高精度処理へ昇格させる
5. 安全なstrandはskipまたは低コストmodeにする

これは単なるLODではなく、各modeを選択した場合の結果を予測して割り当てる反実仮想的なbudgeted allocationである。

---

# 解決する問題

現行プロジェクトでは、strandごとの処理モード選択が固定的、またはheuristicに依存しやすい。

問題点：

1. カメラ距離だけでは接触破綻を予測できない
2. 接触リスクだけでは計算予算を超える可能性がある
3. 高品質modeを選びすぎると60FPSを割る
4. 低品質modeを選びすぎると視覚品質や物理品質が落ちる
5. 各modeの実行コストを考慮しないとp99 frame timeが悪化する

本設計では、strandごとに各modeの反実仮想的な結果を予測し、予算内でmodeを割り当てる。

---

# 入力と出力

## 入力

```cpp
struct ModeAllocationInput
{
    uint32_t strandCount;

    const StrandState* strandStates;
    const StrandDecodeCertificate* certificates;
    const StrandRisk* risks;
    const EditMask* editMasks;
    const ContactInfo* contacts;

    float frameBudgetMs;
    float gpuBudgetMs;
    float memoryBudgetMB;
    float bandwidthBudgetMB;

    uint32_t frameIndex;
};
```

---

## 出力

```cpp
struct ModeAllocationOutput
{
    ModeDecision* modeDecisions;

    float predictedFrameCostMs;
    float predictedQualityError;
    float predictedFailureRate;

    uint32_t qsCount;
    uint32_t dynamicCount;
    uint32_t islandCount;
    uint32_t repairCount;
    uint32_t skipCount;
};
```

---

# データ構造

## SimulationMode

```cpp
enum class SimulationMode : uint8_t
{
    Skip = 0,
    QSOnly = 1,
    DynamicResidual = 2,
    PhysicsIsland = 3,
    RodRepair = 4,
    FloatFallback = 5
};
```

---

## CounterfactualOutcome

```cpp
struct CounterfactualOutcome
{
    float predictedError;
    float predictedFailureProbability;
    float predictedCostMs;
    float predictedBandwidthMB;
    float predictedMemoryMB;
    float predictedCertificateValue;
};
```

---

## ModeDecision

```cpp
struct ModeDecision
{
    uint32_t strandId;
    SimulationMode selectedMode;
    SimulationMode previousMode;

    float score;
    float priority;
    float predictedError;
    float predictedCostMs;
    float predictedFailureProbability;

    uint32_t flags;
};
```

---

## StrandRisk

```cpp
struct StrandRisk
{
    float contactRisk;
    float editRisk;
    float velocityRisk;
    float curvatureRisk;
    float certificateRisk;
    float totalRisk;
};
```

---

# 数式

各strand \(j\) のmodeを以下とする。

\[
m_j \in M
\]

候補mode集合：

\[
M =
\{
Skip,
QSOnly,
DynamicResidual,
PhysicsIsland,
RodRepair,
FloatFallback
\}
\]

各modeを選んだ場合の反実仮想結果を、

\[
O_j(m) =
(E_j(m), P_j^{fail}(m), T_j(m), B_j(m), M_j(m), C_j(m))
\]

とする。

目的関数：

\[
\min_{m_1,\ldots,m_N}
\sum_j
\left[
E_j(m_j)
+
\lambda_f P_j^{fail}(m_j)
+
\lambda_t T_j(m_j)
+
\lambda_b B_j(m_j)
+
\lambda_c C_j(m_j)
+
\lambda_s S_j(m_j, m_j^{prev})
\right]
\]

制約：

\[
\sum_j T_j(m_j) \leq T_{\text{budget}}
\]

\[
\sum_j B_j(m_j) \leq B_{\text{budget}}
\]

\[
\sum_j M_j(m_j) \leq M_{\text{budget}}
\]

mode切替ペナルティ：

\[
S_j(m, m^{prev})
=
\begin{cases}
0 & m = m^{prev} \\
s_{switch} & m \neq m^{prev}
\end{cases}
\]

---

# アルゴリズム

## PredictCounterfactualOutcome

予測器は手設計式で実装し、学習モデルへ差し替え可能なinterfaceを持たせる。

```txt
function PredictCounterfactualOutcome(strand, mode):
    1. risk, certificate, velocity, contact, edit状態を読む
    2. modeごとのbase costを読む
    3. modeごとのbase errorを読む
    4. riskでfailure probabilityを補正する
    5. certificate失敗履歴でrepair/island優先度を補正する
    6. predicted outcomeを返す
```

---

## AllocateModes

```txt
function AllocateModes(input):
    1. 全strandについて各modeのoutcomeを計算する
    2. 各strandのbest modeを選ぶ
    3. predicted total costを計算する
    4. 予算内なら採用する
    5. 予算超過なら、低priority strandからmodeを降格する
    6. 予算が余るなら、高benefit strandを昇格する
    7. ModeDecision bufferへ書く
```

---

## Priority計算

```txt
benefit = errorReduction + failureReduction * lambda
cost = predictedCostMs + predictedBandwidthMB * bandwidthWeight
priority = benefit / max(cost, epsilon)
```

---

## Downgrade順

予算超過時の降格順：

```txt
RodRepair -> PhysicsIsland -> DynamicResidual -> QSOnly -> Skip
```

ただし、certificateValueが高いstrandは降格禁止。

---

## Upgrade順

予算が余った場合の昇格順：

```txt
Skip -> QSOnly -> DynamicResidual -> PhysicsIsland -> RodRepair
```

ただし、contactRiskが高いstrandはPhysicsIslandを優先する。

---

# GPU/CPU処理フロー

## CPU側

```txt
1. mode allocationの重みとbudgetを設定する
2. debug時のみmode distributionをreadbackする
3. 通常フレームでは全strand readbackをしない
```

---

## GPU側

```txt
1. StrandStateを読む
2. Certificateを読む
3. Riskを計算する
4. 各modeのCounterfactualOutcomeを計算する
5. ModeDecisionを決定する
6. mode別にstrandをcompactする
7. PhysicsIsland / RodRepair taskを生成する
8. indirect dispatch argsを生成する
```

GPUバッファ：

```cpp
StructuredBuffer<StrandState> g_StrandStates;
StructuredBuffer<StrandDecodeCertificate> g_Certificates;
RWStructuredBuffer<StrandRisk> g_Risks;
RWStructuredBuffer<ModeDecision> g_ModeDecisions;
RWStructuredBuffer<uint32_t> g_ModeCounters;
RWStructuredBuffer<DispatchArgs> g_DispatchArgs;
```

---

# fallback設計

## fallback条件

```txt
1. predicted total cost > budget
2. p99 frame timeが連続して閾値超過
3. mode switching countが高すぎる
4. predicted failureとactual failureの差が大きい
5. certificate false safeが増えた
```

## fallback動作

```txt
F0: 全体budgetを保守的に下げる
F1: RodRepairの上限数を制限する
F2: PhysicsIslandの最大サイズを制限する
F3: 安全strandをQSOnlyまたはSkipへ降格する
F4: Oracle予測を使わずrisk-based heuristicへ戻す
```

---

# 予想される失敗例

## 1. 高速運動で予測が外れる

原因：

- 入力特徴量が現在フレーム中心で、将来接触を読めない

対策：

- velocity historyを追加
- previous certificate historyを追加
- 近傍strandのrisk統計を追加

---

## 2. mode switchingが多すぎてpoppingする

原因：

- 各frameで最小score modeを選びすぎる

対策：

- mode hysteresisを入れる
- switching penaltyを上げる
- minimum mode lifetimeを設定する

---

## 3. 予算を超える

原因：

- PhysicsIslandやRodRepairが集中する

対策：

- island budget capを設ける
- repair task countを制限する
- priority / costで降格する

---

## 4. 危険strandをSkipしてしまう

原因：

- predicted failureが低く見積もられる

対策：

- false safe penaltyを導入
- certificateValueが高いstrandはSkip禁止
- contactRiskが高いstrandは最低QSOnly以上

---


# 実装受け入れ条件

## 必須ユニットテスト

```txt
Test_CBMA_BudgetRespect:
    frameBudgetMsを小さくしたとき、predictedFrameCostMs <= frameBudgetMs になることを確認する。

Test_CBMA_CertificatePromotion:
    certificateValueが高いstrandがSkipにならないことを確認する。

Test_CBMA_Hysteresis:
    riskが閾値付近で揺れてもmodeが毎frame振動しないことを確認する。

Test_CBMA_Ablation:
    NoCBMA時は固定LOD、CBMA時はrisk/costに応じてmode分布が変わることを確認する。
```

## Definition of Done

```txt
1. 全strandにModeDecisionが出る。
2. predicted costとactual costをログに出せる。
3. mode countを毎frame集計できる。
4. 予算超過時のdowngrade順が決まっている。
5. Common_Evaluation_and_Baseline.mdのAblation_NoCBMAと比較できる。
```

## 実装指示

```txt
この設計書に従って、Counterfactual Budgeted Mode Allocationを実装してください。
rule-based predictorまたは軽量回帰predictorで、各modeのpredictedError、predictedFailureProbability、predictedCostMsを毎フレーム計算してください。
次にgreedy allocationでframe budgetに収め、certificateが危険なstrandを強制昇格してください。
mode switching抑制のためにhysteresisとminimum lifetimeを実装してください。
```

---

# 論文での主張文

```txt
本研究では、各strandの計算モードを固定LODや単純なrisk閾値で選ぶのではなく、
各modeを選択した場合の誤差、破綻確率、計算コストを反実仮想的に予測し、
固定60FPS予算下でmode allocationを行う。

これにより、同じ計算予算内で、現行プロジェクトのheuristicな処理よりも
高い品質と低い破綻率を実現する。
```

---

# 詳細実装定義

この章では、`Counterfactual Budgeted Mode Allocation` を固定LODではなく、strand単位の予測・割当・安定化問題として実装するための詳細を定義する。

## 1. Mode一覧

runtimeで扱うmodeは以下とする。

| Mode | 意味 | 主な入力 | 主な出力 | 使用条件 |
|---|---|---|---|---|
| `Skip` | 前フレーム状態を再利用 | previous state | render state | 安全かつ低重要度 |
| `QSOnly` | 準静的decodeのみ | microtype | decoded points | 通常の低リスクstrand |
| `DynamicResidual` | 簡易動的補正 | state + residual | corrected points | 動きがあるが低接触 |
| `PhysicsIsland` | island solverへ投入 | risky list | island candidate | 接触・編集・局所破綻 |
| `RodRepair` | segment repairへ投入 | certificate failure | repaired candidate | 局所segment破綻 |
| `FloatFallback` | 非圧縮状態で処理 | full float state | safe render | 数値破綻・decode不能 |

`Skip` は最も安いが、certificate failure中のstrandでは禁止する。

## 2. CounterfactualOutcome構造

各strandについて、すべての候補modeの予測結果を持つ。

```cpp
struct CounterfactualOutcome
{
    uint32_t strandId;
    uint32_t mode;

    float predictedError;
    float predictedFailureProbability;
    float predictedCostMs;
    float predictedBandwidthMB;
    float predictedMemoryMB;
    float predictedVisualImportance;
    float predictedCertificateValue;

    uint32_t requiredCertificateMask;
    uint32_t forbiddenCertificateMask;
    uint32_t reasonBits;
};
```

`reasonBits` には、選択理由または除外理由を入れる。

```cpp
enum ModeReasonBits : uint32_t
{
    MODE_REASON_LOW_RISK          = 1 << 0,
    MODE_REASON_HIGH_CONTACT      = 1 << 1,
    MODE_REASON_EDIT_AFFECTED     = 1 << 2,
    MODE_REASON_CERT_FAILURE      = 1 << 3,
    MODE_REASON_BUDGET_LIMIT      = 1 << 4,
    MODE_REASON_HYSTERESIS        = 1 << 5,
    MODE_REASON_MIN_LIFETIME      = 1 << 6,
    MODE_REASON_FALLBACK_REQUIRED = 1 << 7
};
```

## 3. Risk計算

Riskは単一の経験則ではなく、複数要素から合成する。

```txt
risk = w_cert * certificateRisk
     + w_contact * contactRisk
     + w_edit * editRisk
     + w_motion * motionRisk
     + w_visibility * visualImportance
     + w_history * recentFailureRisk
```

各項目：

```txt
certificateRisk:
  certificateBitsとresidual upper boundから計算する。

contactRisk:
  collision objectとの距離、相対速度、過去のpenetrationから計算する。

editRisk:
  editBits、編集強度、編集からの経過frame数から計算する。

motionRisk:
  root velocity、head angular velocity、strand tip velocityから計算する。

visualImportance:
  screen coverage、camera distance、silhouette contributionから計算する。

recentFailureRisk:
  直近Nフレームのcertificate failure、repair failure、fallback回数から計算する。
```

## 4. Candidate Outcome予測

### 4.1 Skip

```txt
predictedCostMs = near zero
predictedError = previousError + motionRisk + editRisk
predictedFailureProbability = high if certificate failure or contact risk exists
forbidden if certificateBits != 0 or editBits != 0
```

### 4.2 QSOnly

```txt
predictedCostMs = decodeCost(profile, pointCount)
predictedError = decodeErrorUpperBound + motionResidual
predictedFailureProbability = contactRisk * 0.5 + certificateRisk
```

### 4.3 DynamicResidual

```txt
predictedCostMs = decodeCost + residualUpdateCost
predictedError = max(decodeError, dynamicResidualPrediction)
predictedFailureProbability = contactRisk * 0.7 + motionRisk * 0.3
```

### 4.4 PhysicsIsland

```txt
predictedCostMs = islandBaseCost + neighborCount * islandMemberCost
predictedError = predictedPostIslandResidual
predictedFailureProbability = depends on island size, contact risk, previous island reject rate
required if multi-strand contact risk is high
```

### 4.5 RodRepair

```txt
predictedCostMs = repairBaseCost + segmentCount * iterationCount * projectionCost
predictedError = predictedPostRepairResidual
predictedFailureProbability = high if penetration involves multiple strands
required for localized bend/twist/length certificate failure
```

### 4.6 FloatFallback

```txt
predictedCostMs = floatDecodeCost + fullStateCost
predictedError = low for decode error, not necessarily low for contact
predictedFailureProbability = numeric-safe but budget-expensive
required for NaN/Inf recovery or invalid payload
```

## 5. 割当アルゴリズム

割当は厳密最適化でなくてよい。卒業研究実装では、以下のgreedy + repair方式を標準とする。

```txt
AllocateModes:
  1. 各strandのmandatory modeを決める。
  2. mandatory costをbudgetから差し引く。
  3. 残りstrandについて、最低許容modeを仮選択する。
  4. 各upgrade候補のbenefit/costを計算する。
  5. benefit/costが高い順に予算内でupgradeする。
  6. 予算超過した場合、危険度が低く品質損失が小さい順にdowngradeする。
  7. hysteresisとminimum lifetimeに違反する切替を戻す。
  8. 最終的なModeDecisionBufferを書く。
```

benefitは以下で計算する。

```txt
benefit = errorReduction * visualImportance
        + failureReduction * riskWeight
        + certificateImprovement * certificateWeight
        - switchingPenalty
```

## 6. HysteresisとMinimum Lifetime

modeの切替はpoppingやちらつきを起こすため、以下を入れる。

```txt
Promotion:
  newModeBenefit > currentModeBenefit + promotionMargin

Demotion:
  currentModeCostがbudget圧迫している、かつrisk < demotionThreshold

Minimum Lifetime:
  一度PhysicsIslandまたはRodRepairへ昇格したstrandは、minLifetimeFramesの間はSkipへ落とさない。
```

例：

```txt
if previousMode == PhysicsIsland and framesSinceModeChange < 3:
    forbid Skip
    forbid QSOnly unless certificate is clean for 2 consecutive frames
```

## 7. Budget処理

budgetは時間だけではない。

```txt
Frame Budget:
  gpuHairBudgetMs以下にする。

Bandwidth Budget:
  Microtype decode、float fallback、render composeの帯域を合計する。

Memory Budget:
  IslandTask、RepairTask、DecodedPointBufferの一時使用量を上限以下にする。

Task Count Budget:
  maxIslandCount、maxRepairTasksを超えない。
```

予算超過時の降格順：

```txt
1. visualImportanceが低いDynamicResidual -> QSOnly
2. riskが低いQSOnly -> Skip
3. small residualのRodRepair -> 次フレーム継続task
4. low priority PhysicsIsland -> DynamicResidual
5. FloatFallbackは最後に検討する。数値安全性が必要な場合は降格しない。
```

## 8. ModeDecision構造

```cpp
struct ModeDecision
{
    uint32_t strandId;
    uint16_t selectedMode;
    uint16_t previousMode;

    float selectedCostMs;
    float selectedError;
    float selectedFailureProbability;
    float selectedCertificateValue;

    uint32_t reasonBits;
    uint16_t framesSinceModeChange;
    uint16_t lockFramesRemaining;
};
```

`lockFramesRemaining` はmode切替抑制に使う。

## 9. 実装時に必要な集計値

毎フレーム以下を集計する。

```txt
modeCount[mode]
modeCostSum[mode]
predictedCostSum
actualCostSum
forcedDowngradeCount
forcedUpgradeCount
skipForbiddenCount
hysteresisBlockedSwitchCount
budgetOverflowCount
```

これらは評価ログに出す。

## 10. テストケース詳細

### 10.1 危険strand Skip禁止テスト

```txt
Input:
  certificate failureを持つstrand集合
Expected:
  selectedMode != Skip
  reasonBitsにMODE_REASON_CERT_FAILUREが立つ
```

### 10.2 予算内割当テスト

```txt
Input:
  1000 strand、固定のpredictedCost、frameBudgetMs
Expected:
  sum(selectedCostMs) <= frameBudgetMs
  mandatory modeが降格されない
```

### 10.3 Hysteresisテスト

```txt
Input:
  riskが閾値付近で振動するstrand
Expected:
  modeが毎フレーム切り替わらない
  minLifetimeFramesが守られる
```

### 10.4 Counterfactualログテスト

```txt
Input:
  複数mode候補を持つstrand
Expected:
  選択modeだけでなく、非選択modeのpredictedError/costもsample logに出る
```

## 11. 実装時に禁止する簡略化

```txt
1. 距離だけでmodeを決めること。
2. 選ばなかったmodeの予測値を捨てること。
3. 予算超過時に危険strandを先に降格すること。
4. mode切替の履歴を持たないこと。
5. actual costとの差分を記録しないこと。
```