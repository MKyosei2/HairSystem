# Novelty 04 安全寄り完全版企画書兼設計書

# Constraint-Monotone Local Commit for GPU Hair Editing

## 制約違反ベクトルの単調改善・局所性・編集意図保持に基づく毛髪編集確定方式

---

# 0. 文書情報

| 項目           | 内容                                                                                 |
| ------------ | ---------------------------------------------------------------------------------- |
| 対象プロジェクト     | HairSystem                                                                         |
| Novelty番号    | Novelty 04                                                                         |
| 提案名称         | Constraint-Monotone Local Commit for GPU Hair Editing                              |
| 略称           | CMLC                                                                               |
| 実装言語         | C++17、HLSL Shader Model 5.0                                                        |
| Graphics API | DirectX 11                                                                         |
| 正式ビルド        | Release x64                                                                        |
| 表示解像度        | 1920×1080                                                                          |
| 表示髪本数        | 100,000本                                                                           |
| Guide本数      | 4,000本                                                                             |
| Follower本数   | 96,000本                                                                            |
| 点数           | 128 points／strand                                                                  |
| 固定時間刻み       | 1/60秒                                                                              |
| Solver反復数    | 6回                                                                                 |
| 対象GPU        | GTX 1660級以上                                                                        |
| 他Novelty依存   | なし                                                                                 |
| 主な評価対象       | Unsafe Commit、単調性違反、局所性違反、編集意図消失、編集レイテンシ                                           |
| 主新規性         | 編集候補と修復後状態の制約違反をベクトルとして比較し、制約ごとの単調改善、局所性、Version一致、編集意図保持を満たす結果だけを確定するCommit規則     |
| 新規性に含めないもの   | Candidate Buffer、GPU Compute、局所修復、PBD、局所Solver、Undo／Redo、Picking、Brush、Dirty Range |
| 安全性の意味       | 本設計で定義した制約、Collider、編集操作、実験条件内におけるCommit安全性                                        |
| 形式証明との関係     | 本方式の検証記録は実装上のEvidence Recordであり、数学的または形式手法上の完全証明ではない                               |

---

# 1. 研究題目

## 1.1 日本語題目

制約違反の単調改善と局所性検証に基づくGPU毛髪編集確定方式

## 1.2 英語題目

Constraint-Monotone Local Commit for GPU Hair Editing

---

# 2. 安全寄りに変更した中心方針

従来案では、以下の広い処理連鎖を研究の中心としていた。

```text
Candidate
→ Certificate
→ Repair
→ Local Solver
→ Commit
```

しかし、これらの個別要素は一般的な編集システム、最適化、物理シミュレーション、データベースのTransaction処理などと概念的に重なる。

そのため、本研究ではこれら自体を新規性としない。

本研究の中心は、次のCommit規則に限定する。

```text
候補状態を作る
    ↓
制約違反をベクトルとして評価する
    ↓
任意の既存修復処理を適用する
    ↓
修復前後の制約違反ベクトルを比較する
    ↓
各Hard制約が悪化していないことを確認する
    ↓
新しい違反が発生していないことを確認する
    ↓
編集範囲外が変更されていないことを確認する
    ↓
編集意図が最低限残っていることを確認する
    ↓
条件をすべて満たす場合だけCommitする
```

新規性として検証する対象は、修復手法そのものではなく、

> **複数の物理制約を単一スコアへ潰さず、制約違反ベクトルの部分順序、局所性記録、Version記録、編集意図保持条件を用いてCommit可否を決定する方式**

である。

---

# 3. 研究背景

高密度毛髪編集では、編集または修復後の結果を一つの総合スコアだけで評価すると、ある制約の改善によって別の制約の悪化が隠れる可能性がある。

例えば、次の二つの状態を考える。

編集候補：

[
V_c=
(
E_l=0.8,
E_b=1.4,
E_p=0.0
)
]

修復後：

[
V_r=
(
E_l=0.2,
E_b=0.7,
E_p=1.2
)
]

ここで、

* (E_l)：長さ誤差
* (E_b)：曲げ誤差
* (E_p)：侵入誤差

である。

重み付き総合値だけを見ると、長さと曲げの改善によって、侵入の新規発生が隠れる可能性がある。

しかし、毛髪がCollider内部へ侵入した状態は、他の誤差が改善していてもCommitすべきではない。

本研究では、制約違反を単一値へ完全には集約しない。

各制約を独立成分として保持し、修復後に新しい違反が発生していないこと、Hard制約が悪化していないことを確認する。

---

# 4. 研究目的

本研究の目的は、高密度毛髪編集において、修復処理の種類に依存せず使用できる安全側のCommit規則を構築することである。

具体的には、以下を実現する。

1. 編集候補状態を確定状態から隔離する
2. 物理違反を複数成分のベクトルとして記録する
3. 修復前と修復後の違反ベクトルを比較する
4. Hard制約の新規違反を禁止する
5. 修復対象外の制約悪化を禁止する
6. 許可された副作用をOperator Contractとして明示する
7. 編集範囲外への書込みを検出する
8. 古いCandidateのCommitを防止する
9. 編集意図が失われた結果を成功扱いしない
10. 条件を満たした結果だけを確定状態へCommitする
11. 判定根拠をEvidence Recordとして保存する
12. 同じ操作を再現して判定を確認できるようにする

---

# 5. 研究課題

## RQ1：単一スコアとの比較

制約違反ベクトルを用いる方式は、重み付き総合スコアだけを用いる方式よりUnsafe Commitを減らせるか。

## RQ2：単調改善条件

修復後の制約違反が、許可された成分を除いて単調に改善または不変であることを要求すると、新しい破綻のCommitを防止できるか。

## RQ3：局所性

編集範囲外への書込みを検証することで、局所編集による意図しない広域変形を検出できるか。

## RQ4：編集意図

物理誤差だけでなく編集意図保持を評価することで、修復によって編集結果が消失する問題を検出できるか。

## RQ5：実行コスト

制約ベクトル、局所性、Version、編集意図の検証を追加しても、対話的編集が可能な処理時間を維持できるか。

---

# 6. 主新規性

本研究の主新規性は、次の一点に限定する。

> 毛髪編集候補および修復後状態について、長さ、曲げ、侵入、境界、速度不連続などの制約違反をベクトルとして保持し、修復Operatorごとに許可された副作用を考慮した制約単調性、編集範囲外の非変更、Version一致、編集意図保持を満たす場合だけ局所Commitを許可する方式。

---

# 7. 新規性として主張しないもの

以下は既存技術または一般的実装技術として扱う。

* Candidate State
* Transaction
* Double Buffer
* GPU Compute Shader
* Sparse Buffer
* Local Repair
* PBD
* XPBD
* Local Solver
* Projection
* Smoothing
* Picking
* Brush
* Gizmo
* Undo／Redo
* Version Counter
* Dirty Range
* Follower Reconstruction
* Preview表示
* Telemetry
* Hash
* Ground Truth比較
* Commit／Rejectという名称
* Evidence Recordという名称

本研究で検証するのは、これらの個別要素ではなく、制約ベクトルとOperator Contractを用いたCommit判定規則である。

---

# 8. 用語定義

## 8.1 Persistent State

現在正式に確定している毛髪状態。

[
X_s
]

## 8.2 Candidate State

ユーザー編集を適用した未確定状態。

[
X_c
]

## 8.3 Repaired State

任意の修復OperatorをCandidateへ適用した状態。

[
X_r
]

## 8.4 Violation Vector

各制約の違反量を成分として持つベクトル。

[
V(X)
====

(
v_l,
v_b,
v_t,
v_p,
v_{bp},
v_{bt},
v_v
)
]

* (v_l)：長さ違反
* (v_b)：曲げ違反
* (v_t)：ねじれ違反
* (v_p)：侵入違反
* (v_{bp})：境界位置違反
* (v_{bt})：境界接線違反
* (v_v)：速度不連続

## 8.5 Operator Contract

修復Operatorが、

* 改善対象とする制約
* 変更を許される制約
* 悪化を禁止される制約
* 最大副作用量
* 書込み可能範囲

を宣言する記録。

## 8.6 Evidence Record

Commit判定に使用した情報を保存する記録。

形式証明ではなく、再検証可能な実測記録である。

---

# 9. 制約違反ベクトル

## 9.1 定義

[
V(X)
====

\begin{bmatrix}
\bar{E}_l(X)\
\bar{E}_b(X)\
\bar{E}*t(X)\
\bar{E}*p(X)\
\bar{E}*{bp}(X)\
\bar{E}*{bt}(X)\
\bar{E}_v(X)
\end{bmatrix}
]

各成分は許容値で正規化する。

[
\bar{E}_k
=========

\frac{E_k}{\tau_k}
]

[
\bar{E}_k\leq1
]

であれば、その制約は許容範囲内とする。

## 9.2 単一スコアの扱い

デバッグや可視化のために、

[
S(X)
====

\sum_k w_k\bar{E}_k(X)
]

を出力してもよい。

ただし、Commit判定には使用しない。

Commit判定は各成分を独立に評価する。

---

# 10. 長さ違反

Segment (i) の現在長を (l_i)、Rest Lengthを (l_i^0) とする。

[
e_{l,i}
=======

\frac{|l_i-l_i^0|}
{\max(l_i^0,\varepsilon)}
]

Support Range内の最大値を、

[
E_l
===

\max_i e_{l,i}
]

とする。

[
\bar{E}_l
=========

\frac{E_l}{\tau_l}
]

---

# 11. 曲げ違反

隣接Segment方向を (d_{i-1},d_i) とする。

[
\theta_i
========

\cos^{-1}
\left(
\operatorname{clamp}
(d_{i-1}\cdot d_i,-1,1)
\right)
]

基準角度を (\theta_i^0) とする。

[
E_b
===

\max_i
|\theta_i-\theta_i^0|
]

[
\bar{E}_b
=========

\frac{E_b}{\tau_b}
]

---

# 12. 侵入違反

SDF値を (D(P_i)) とする。

[
E_p
===

\max_i
\max(0,-D(P_i))
]

[
\bar{E}_p
=========

\frac{E_p}{\tau_p}
]

Collider内部に新たに侵入した場合は、他の成分に関係なくCommit禁止とする。

---

# 13. 境界違反

## 13.1 境界位置

[
E_{bp}
======

\max
\left(
|P_s-P_s^{expected}|,
|P_e-P_e^{expected}|
\right)
]

[
\bar{E}_{bp}
============

\frac{E_{bp}}{\tau_{bp}}
]

## 13.2 境界接線

[
E_{bt}
======

\max
\left(
1-d_s\cdot d_s^{expected},
1-d_e\cdot d_e^{expected}
\right)
]

[
\bar{E}_{bt}
============

\frac{E_{bt}}{\tau_{bt}}
]

---

# 14. 速度不連続

Commit後位置を (P_i^{new})、前フレーム位置を (P_i^{prev})、時間刻みを (\Delta t) とする。

[
v_i^{new}
=========

\frac{P_i^{new}-P_i^{prev}}{\Delta t}
]

許容速度を (v_{\max}) とする。

[
E_v
===

\max_i
\max
\left(
0,
|v_i^{new}|-v_{\max}
\right)
]

[
\bar{E}_v
=========

\frac{E_v}{\tau_v}
]

Rest Shape編集では、Previous PositionをCommit位置へ揃える方式を標準とする。

---

# 15. 編集意図保持

Candidateと修復後状態の差を測定する。

[
E_{\mathrm{intent}}
===================

\frac{1}{|\Omega_{\mathrm{edit}}|}
\sum_{i\in\Omega_{\mathrm{edit}}}
|
P_i^r-P_i^c
|
]

ただし、単純な差だけでは、元の編集量が小さい場合と大きい場合を区別できない。

そこで、編集保持率を定義する。

[
R_{\mathrm{intent}}
===================

1-
\frac{
\sum_i|P_i^r-P_i^c|
}{
\sum_i|P_i^c-P_i^s|+\varepsilon
}
]

[
0\leq R_{\mathrm{intent}}\leq1
]

Commit条件として、

[
R_{\mathrm{intent}}
\geq
R_{\min}
]

を要求する。

---

# 16. 局所性条件

編集許可範囲を、

[
\Omega_{\mathrm{allow}}
=======================

\Omega_{\mathrm{edit}}
\cup
\Omega_{\mathrm{guard}}
]

とする。

範囲外点について、

[
\forall i\notin\Omega_{\mathrm{allow}},
\quad
P_i^r=P_i^s
]

を要求する。

浮動小数点誤差を考慮し、

[
|P_i^r-P_i^s|
\leq
\tau_{\mathrm{outside}}
]

を許容条件とする。

局所性違反量を、

[
E_{\mathrm{outside}}
====================

\max_{i\notin\Omega_{\mathrm{allow}}}
|P_i^r-P_i^s|
]

とする。

[
E_{\mathrm{outside}}>
\tau_{\mathrm{outside}}
]

の場合はCommit禁止とする。

---

# 17. Operator Contract

修復Operatorごとに、制約成分への影響を宣言する。

## 17.1 Contract Mode

```cpp
enum class ConstraintEffectPolicy : uint32_t
{
    MustImprove = 0u,
    MustNotWorsen,
    MayWorsenWithinBound,
    NotEvaluated
};
```

## 17.2 Operator Contract構造体

```cpp
struct EditOperatorContract
{
    uint32_t operatorId;
    uint32_t writablePointBegin;
    uint32_t writablePointEnd;

    ConstraintEffectPolicy lengthPolicy;
    ConstraintEffectPolicy bendPolicy;
    ConstraintEffectPolicy twistPolicy;
    ConstraintEffectPolicy penetrationPolicy;
    ConstraintEffectPolicy boundaryPositionPolicy;
    ConstraintEffectPolicy boundaryTangentPolicy;
    ConstraintEffectPolicy velocityPolicy;

    float maximumLengthIncrease;
    float maximumBendIncrease;
    float maximumTwistIncrease;
    float maximumPenetrationIncrease;
    float maximumBoundaryPositionIncrease;
    float maximumBoundaryTangentIncrease;
    float maximumVelocityIncrease;
};
```

---

# 18. Operator Contract例

## 18.1 Length Repair

| 制約                | 方針                   |
| ----------------- | -------------------- |
| Length            | MustImprove          |
| Penetration       | MustNotWorsen        |
| Boundary Position | MayWorsenWithinBound |
| Bend              | MayWorsenWithinBound |
| Velocity          | MustNotWorsen        |

## 18.2 Bend Repair

| 制約                | 方針                   |
| ----------------- | -------------------- |
| Bend              | MustImprove          |
| Length            | MustNotWorsen        |
| Penetration       | MustNotWorsen        |
| Boundary Position | MayWorsenWithinBound |
| Velocity          | MustNotWorsen        |

## 18.3 Penetration Repair

| 制約          | 方針                   |
| ----------- | -------------------- |
| Penetration | MustImprove          |
| Length      | MayWorsenWithinBound |
| Bend        | MayWorsenWithinBound |
| Boundary    | MustNotWorsen        |
| Velocity    | MustNotWorsen        |

## 18.4 Uniform Smooth

比較対象として使用する。

Operator Contractを持たず、単純に全成分を事後検査する。

---

# 19. 制約単調性

Candidateの違反ベクトルを、

[
V_c=V(X_c)
]

修復後の違反ベクトルを、

[
V_r=V(X_r)
]

とする。

## 19.1 MustImprove

制約 (k) がMustImproveの場合、

[
V_{r,k}
\leq
V_{c,k}
-------

\delta_k
]

を要求する。

(\delta_k) は最小改善量である。

## 19.2 MustNotWorsen

[
V_{r,k}
\leq
V_{c,k}
+
\epsilon_k
]

を要求する。

## 19.3 MayWorsenWithinBound

[
V_{r,k}
\leq
V_{c,k}
+
b_k
]

かつ、

[
V_{r,k}
\leq1
]

を要求する。

## 19.4 新規違反禁止

Candidate時に安全だった制約が、修復後に違反状態へ移行することを禁止する。

[
V_{c,k}\leq1
\land
V_{r,k}>1
\Rightarrow
\operatorname{Reject}
]

---

# 20. Commit条件

次をすべて満たす場合だけCommitする。

1. CandidateとPersistentが分離されている
2. Source Versionが一致する
3. Topology Versionが一致する
4. Collider Versionが一致する
5. CandidateとRepaired StateにNaN／Infがない
6. すべてのHard制約が許容範囲内
7. 各Operator Contractを満たす
8. 新規違反が発生していない
9. 編集範囲外の変更が許容値以下
10. 編集意図保持率が下限以上
11. Commit対象Point数が一致する
12. 同一Guideへの競合Transactionがない
13. Pending Undo Recordが有効
14. Capacity Overflowがない
15. Evidence Recordが完全である

形式的には、

[
\operatorname{Commit}
=====================

\operatorname{VersionValid}
\land
\operatorname{Finite}
\land
\operatorname{HardConstraintsValid}
\land
\operatorname{OperatorContractValid}
\land
\operatorname{NoNewViolation}
\land
\operatorname{LocalityValid}
\land
\operatorname{IntentValid}
\land
\operatorname{EvidenceComplete}
]

である。

---

# 21. Commitしない条件

以下の一つでも成立すればRejectする。

* Hard制約違反
* 新規違反発生
* MustImprove制約が改善していない
* MustNotWorsen制約が悪化
* 副作用上限超過
* 編集範囲外書込み
* 編集意図保持率不足
* Version不一致
* Topology不一致
* Collider Version不一致
* NaN
* Inf
* Capacity Overflow
* 競合Transaction
* Evidence Record不足
* Ground Truth再評価でUnsafe

---

# 22. Evidence Record

## 22.1 目的

Commitした理由を再検証可能にする。

Evidence Record自体を新規性とは主張しない。

## 22.2 構造体

```cpp
struct EditEvidenceRecord
{
    uint32_t operationId;
    uint32_t guideId;
    uint32_t operatorId;
    uint32_t finalDecision;

    uint32_t sourceFrame;
    uint32_t sourcePositionVersion;
    uint32_t sourceRestVersion;
    uint32_t sourceTopologyVersion;
    uint32_t sourceColliderVersion;

    uint32_t editPointBegin;
    uint32_t editPointEnd;
    uint32_t allowedPointBegin;
    uint32_t allowedPointEnd;

    float violationBefore[7];
    float violationAfter[7];

    float intentRetention;
    float outsideRangeMaxDeltaMm;
    float maximumPointDeltaMm;
    float editLatencyMs;

    uint32_t newViolationBits;
    uint32_t operatorContractFailureBits;
    uint32_t rejectReasonBits;
    uint32_t availabilityBits;

    uint64_t sourceStateHash;
    uint64_t candidateStateHash;
    uint64_t repairedStateHash;
    uint64_t committedStateHash;
};
```

## 22.3 保存項目

* 入力操作
* 対象Guide
* 対象Point Range
* 許可書込みRange
* 修復Operator
* 修復前ベクトル
* 修復後ベクトル
* Version
* Hash
* Intent Retention
* Locality Error
* Commit／Reject理由
* GPU Timing

---

# 23. Candidate State

Candidateは研究の主新規性ではないが、安全な判定の前提として必要である。

全12,800,000点を複製せず、対象GuideとGuard Bandだけを保持する。

```cpp
struct EditCandidatePointGpu
{
    uint32_t operationId;
    uint32_t guideId;
    uint32_t localPointIndex;
    uint32_t globalPointIndex;

    float beforePosition[3];
    float candidateWeight;

    float candidatePosition[3];
    float repairedWeight;

    float repairedPosition[3];
    float reserved;
};
```

---

# 24. Transaction State

```text
Idle
Queued
SourceCaptured
CandidateApplied
CandidateEvaluated
RepairQueued
RepairApplied
RepairEvaluated
ContractEvaluated
LocalityEvaluated
IntentEvaluated
CommitReady
Committed
Rejected
Fallback
Invalid
```

---

# 25. 状態遷移

```text
Idle
  ↓
Queued
  ↓
SourceCaptured
  ↓
CandidateApplied
  ↓
CandidateEvaluated
  ├─ CandidateSafe ───────→ ContractEvaluated
  ├─ Repairable ──────────→ RepairQueued
  ├─ HardUnsafe ──────────→ Rejected
  └─ Invalid ─────────────→ Fallback

RepairQueued
  ↓
RepairApplied
  ↓
RepairEvaluated
  ↓
ContractEvaluated
  ├─ ContractPass ────────→ LocalityEvaluated
  └─ ContractFail ────────→ Rejected

LocalityEvaluated
  ├─ LocalityPass ────────→ IntentEvaluated
  └─ LocalityFail ────────→ Rejected

IntentEvaluated
  ├─ IntentPass ──────────→ CommitReady
  └─ IntentFail ──────────→ Rejected

CommitReady
  ↓
Committed
```

---

# 26. 修復処理の位置付け

本研究では、修復Operatorそのものを新規性としない。

最低限、以下を実装する。

* Length Projection
* Bend Relaxation
* Penetration Projection
* Boundary Blend

修復Operatorを変更しても、同じCommit規則を利用できる設計とする。

局所Solverは任意の比較対象またはFallbackとして扱い、主新規性へ含めない。

---

# 27. 他Noveltyからの独立

以下を参照しない。

* Segment Group Runtime State
* Static Certificate
* Static／Dynamic切替
* Budgeted Physics
* Physics Island
* Render Certificate
* Render Budget
* GPU Resident Global Scheduler

Novelty 04専用に保持するもの：

* Candidate Point Buffer
* Violation Vector Buffer
* Operator Contract Buffer
* Evidence Record Buffer
* Dirty Range Buffer
* Commit Result Buffer
* Telemetry Counter Buffer

---

# 28. 04専用Dirty Range

```cpp
struct EditDirtyRangeGpu
{
    uint32_t guideId;
    uint32_t pointBegin;
    uint32_t pointEnd;
    uint32_t flags;
};
```

Commit成功時のみ生成する。

Reject時は生成しない。

Follower更新は該当Guide Familyだけを対象とする。

---

# 29. GPUリソース

```cpp
StructuredBuffer m_editOperationBuffer;
StructuredBuffer m_editTransactionBuffer;
StructuredBuffer m_editCandidatePointBuffer;
StructuredBuffer m_editViolationBeforeBuffer;
StructuredBuffer m_editViolationAfterBuffer;
StructuredBuffer m_editOperatorContractBuffer;
StructuredBuffer m_editEvidenceRecordBuffer;
StructuredBuffer m_editCommitResultBuffer;
StructuredBuffer m_editDirtyRangeBuffer;
StructuredBuffer m_editCounterBuffer;
StructuredBuffer m_editGroundTruthBuffer;
```

Indirect Args：

```cpp
ComPtr<ID3D11Buffer> m_editEvaluateDispatchArgs;
ComPtr<ID3D11UnorderedAccessView> m_editEvaluateDispatchArgsUav;

ComPtr<ID3D11Buffer> m_editRepairDispatchArgs;
ComPtr<ID3D11UnorderedAccessView> m_editRepairDispatchArgsUav;

ComPtr<ID3D11Buffer> m_editCommitDispatchArgs;
ComPtr<ID3D11UnorderedAccessView> m_editCommitDispatchArgsUav;
```

---

# 30. HLSL

新規ファイル：

```text
apps/dx11_viewer/shaders/HairConstraintMonotoneEdit.hlsl
```

## 30.1 Entry Point

```text
ClearEditCountersCS
BuildEditTransactionsCS
CaptureCandidatePointsCS
ApplyEditOperationsCS

EvaluateCandidateViolationVectorCS
ClassifyCandidateCS

BuildRepairTasksCS
ApplyLengthRepairCS
ApplyBendRepairCS
ApplyPenetrationRepairCS
ApplyBoundaryRepairCS

EvaluateRepairedViolationVectorCS
EvaluateOperatorContractCS
EvaluateNewViolationsCS
EvaluateLocalityCS
EvaluateIntentRetentionCS
EvaluateVersionCS

BuildCommitDecisionCS
CommitAcceptedEditsCS
BuildEditDirtyRangesCS
BuildEditPreviewCategoriesCS
ReduceEditEvidenceCS
BuildEditDispatchArgsCS
```

---

# 31. GPU実行順序

```text
Phase 01  UI操作収集
Phase 02  Edit Operation生成
Phase 03  Source Version保存
Phase 04  Candidate Point抽出
Phase 05  Candidateへ編集適用
Phase 06  Violation Vector Before生成
Phase 07  Repair必要性分類
Phase 08  Repair Operator選択
Phase 09  Operator Contract取得
Phase 10  Repair実行
Phase 11  Violation Vector After生成
Phase 12  Operator Contract検証
Phase 13  新規違反検証
Phase 14  局所性検証
Phase 15  Intent Retention検証
Phase 16  Version再検証
Phase 17  Evidence完全性検証
Phase 18  Commit／Reject決定
Phase 19  Accepted PointだけPersistentへ反映
Phase 20  Previous Position更新
Phase 21  必要時Rest Position更新
Phase 22  Dirty Range生成
Phase 23  Follower Family更新
Phase 24  Undo Record確定
Phase 25  Evidence Record保存
Phase 26  Delayed Readback
```

---

# 32. HLSL Commit禁止事項

* Commit Pass以外でPersistent Positionへ書き込まない
* CandidateとPersistentを同じUAVへBindしない
* 単一スコアだけでCommitしない
* Evidence Record未完成の状態をCommitしない
* Version未確認の状態をCommitしない
* Repair後評価前にCommitしない
* 局所性評価前にCommitしない
* Intent評価前にCommitしない
* 0件Dispatchで不正書込みしない

---

# 33. C++追加ファイル

## 33.1 `include/hairgpu/ConstraintMonotoneHairEdit.h`

追加内容：

* Violation Vector
* Operator Contract
* Transaction State
* Evidence Record
* Commit Decision
* Settings
* CPU評価API
* CPU Ground Truth API
* GPU互換構造体

## 33.2 `src/hairgpu_core/ConstraintMonotoneHairEdit.cpp`

追加内容：

* Length違反計算
* Bend違反計算
* Penetration違反計算
* Boundary違反計算
* Velocity違反計算
* Intent Retention計算
* Locality計算
* Operator Contract評価
* New Violation検出
* Commit Policy
* Ground Truth再評価
* Hash生成
* Unit Test用純粋関数

## 33.3 `apps/dx11_viewer/src/GpuHairViewer_parts/14a_constraint_monotone_edit.inl`

追加内容：

* GPU Resource生成
* Shader Dispatch
* Operation Queue
* Evidence Readback
* Commit結果処理
* Undo確定
* Follower更新
* Preview
* Telemetry

## 33.4 `apps/dx11_viewer/shaders/HairConstraintMonotoneEdit.hlsl`

追加内容：

* 全GPU評価Pass
* Operator Contract
* Commit Decision
* Evidence Record
* Telemetry

---

# 34. 修正ファイル

## 34.1 `include/hairgpu/Authoring.h`

追加：

```cpp
bool BuildConstraintMonotoneEditOperations(
    const HairAsset& asset,
    const EditLayer& layer,
    const RuntimeEditState& runtimeState,
    uint32_t sourceFrame,
    std::vector<ConstraintMonotoneEditOperation>& outOperations,
    std::string& outError);
```

## 34.2 `src/hairgpu_core/Authoring.cpp`

修正：

* 既存Gizmo／Brush操作をOperationへ変換
* Parent Guide解決
* Edit Range生成
* Guard Band生成
* Proposal Mode時の直接Commit禁止
* Direct Commit Baseline維持

## 34.3 `include/hairgpu/HairNoveltyFlags.h`

追加：

```cpp
uint32_t enableEditCandidateIsolation = 0u;
uint32_t enableEditViolationVector = 0u;
uint32_t enableEditOperatorContract = 0u;
uint32_t enableEditMonotoneCommit = 0u;
uint32_t enableEditLocalityCheck = 0u;
uint32_t enableEditIntentCheck = 0u;
uint32_t enableEditEvidenceRecord = 0u;
uint32_t enableEditGroundTruth = 0u;
```

## 34.4 `src/hairgpu_core/HairNoveltyFlags.cpp`

修正：

* 04 Flag正規化
* 不正な組合せ拒否
* Requested／Applied／Observed
* Ablation Variant
* Standalone動作

## 34.5 `include/hairgpu/HairNoveltyTelemetry.h`

追加：

* Violation Vector Before／After
* Monotonicity Failure
* New Violation
* Locality Failure
* Intent Failure
* Evidence Incomplete
* Unsafe Commit
* Pass GPU時間

## 34.6 `src/hairgpu_core/HairNoveltyTelemetry.cpp`

修正：

* Aggregate
* p50／p95／p99
* Availability
* CSV
* JSONL
* Formal Summary

## 34.7 `apps/dx11_viewer/src/GpuHairViewer.h`

追加：

* GPU構造体
* Buffer
* Shader
* Constant Buffer
* Indirect Args
* Readback Ring
* Settings
* Operation Queue
* Evidence State
* Method宣言

## 34.8 `apps/dx11_viewer/src/GpuHairViewer.cpp`

追加：

```cpp
#include "hairgpu/ConstraintMonotoneHairEdit.h"
#include "GpuHairViewer_parts/14a_constraint_monotone_edit.inl"
```

## 34.9 `GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl`

修正：

* Buffer生成
* Constant Buffer生成
* Shader Compile
* Shader Self Test
* Readback Staging生成

## 34.10 `GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl`

修正：

* `ExecutePerStrandEditing()`を正式実装
* 他Noveltyを呼ばない
* Reset時のTransaction破棄
* Readback処理
* GPU Timing

## 34.11 `GpuHairViewer_parts/04_constant_buffers_and_active_set_sync.inl`

修正：

* Edit専用Constant生成
* Threshold
* Version
* Capacity
* Operator Contract設定

Active Setは使用しない。

## 34.12 `GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl`

修正：

* Candidate Preview
* Reject Preview
* Commit Ready Preview
* Persistent非破壊表示

## 34.13 `GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl`

修正：

* Edit Pass Timing
* Evidence Readback Timing
* Same-frame Readback検出

## 34.14 `GpuHairViewer_parts/08_ui_state_sync_and_controls.inl`

修正：

* Violation Vector表示
* Operator Contract表示
* Locality Error表示
* Intent Retention表示
* Commit／Reject理由
* Unsafe Commit表示

## 34.15 `GpuHairViewer_parts/09_settings_load_save_and_export.inl`

修正：

* 04 Settings
* Threshold File
* Contract File
* Formal Lock
* Settings Hash

## 34.16 `GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl`

修正：

* 04 Telemetry全項目
* Evidence Record
* Availability
* Formal Gate

## 34.17 `GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl`

修正：

* N04 Variant
* Scenario
* Threshold Hash
* Contract Hash
* Requested／Applied／Observed
* Formal Status

## 34.18 `GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl`

修正：

* Persistent直接書込み禁止
* Candidate Operation更新
* Mouse Release時Commit判定
* Escape時Candidate破棄
* Baseline時のみ直接Commit

## 34.19 `GpuHairViewer_parts/13_edit_stack_inline_layers_and_masks.inl`

修正：

* LayerをOperation Batchへ変換
* Guide単位判定
* Reject Guideの状態維持
* 決定的Operation順序

## 34.20 `GpuHairViewer_parts/14_scalp_overlay_and_window_messages.inl`

修正：

* Transaction開始／更新／終了
* Cancel
* Workspace切替時Reject
* Window Close時Reject

## 34.21 `GpuHairViewer_parts/15_shutdown_and_tail.inl`

修正：

* Pending Transaction破棄
* Evidence Flush
* Readback Drain
* Resource Release

## 34.22 `apps/dx11_viewer/shaders/HairIndirectArgs.hlsl`

修正：

* Evaluate／Repair／Commit Dispatch Args
* Overflow Clamp
* 0件Dispatch

## 34.23 `apps/dx11_viewer/shaders/HairStrandInstanced.hlsl`

修正：

* Candidate／Reject／Commit Ready表示
* Before／After比較
* Formal時Debug無効化

## 34.24 Build Files

修正対象：

```text
src/hairgpu_core/CMakeLists.txt
apps/dx11_viewer/CMakeLists.txt
HairSystem.vcxproj
HairSystem.vcxproj.filters
```

追加対象：

```text
ConstraintMonotoneHairEdit.cpp
ConstraintMonotoneHairEdit.h
HairConstraintMonotoneEdit.hlsl
```

---

# 35. 設定ファイル

## 35.1 `docs/sample_assets/novelty04_thresholds.json`

```json
{
  "schema_version": 2,
  "length_tolerance_ratio": 0.015,
  "bend_tolerance_radians": 0.20,
  "penetration_tolerance_mm": 0.05,
  "boundary_position_tolerance_mm": 0.25,
  "boundary_tangent_tolerance": 0.03,
  "velocity_tolerance": 0.10,
  "intent_retention_minimum": 0.60,
  "outside_range_tolerance_mm": 0.001,
  "minimum_improvement_ratio": 0.05
}
```

## 35.2 `docs/sample_assets/novelty04_operator_contracts.json`

```json
{
  "schema_version": 1,
  "operators": [
    {
      "name": "length_repair",
      "length": "must_improve",
      "bend": "may_worsen_within_bound",
      "penetration": "must_not_worsen",
      "boundary_position": "may_worsen_within_bound",
      "boundary_tangent": "may_worsen_within_bound",
      "velocity": "must_not_worsen"
    },
    {
      "name": "bend_repair",
      "length": "must_not_worsen",
      "bend": "must_improve",
      "penetration": "must_not_worsen",
      "boundary_position": "may_worsen_within_bound",
      "boundary_tangent": "may_worsen_within_bound",
      "velocity": "must_not_worsen"
    },
    {
      "name": "penetration_repair",
      "length": "may_worsen_within_bound",
      "bend": "may_worsen_within_bound",
      "penetration": "must_improve",
      "boundary_position": "must_not_worsen",
      "boundary_tangent": "must_not_worsen",
      "velocity": "must_not_worsen"
    }
  ]
}
```

---

# 36. 比較対象

## B0：Direct Commit

編集結果を直接Persistentへ反映する。

## B1：Candidate＋Single Threshold

一つの誤差だけで判定する。

## B2：Candidate＋Weighted Score

複数誤差を重み付き和へ集約して判定する。

## B3：Vector Threshold

各制約を個別判定するが、修復前後の単調性を見ない。

## B4：Vector＋Monotonicity

単調性は検証するが、局所性を検証しない。

## B5：Vector＋Monotonicity＋Locality

編集意図保持を検証しない。

## P：Proposed CMLC

* Violation Vector
* Operator Contract
* Component-wise Monotonicity
* New Violation Prohibition
* Locality Check
* Version Check
* Intent Retention
* Evidence Record
* Safe Commit

---

# 37. アブレーション

| ID  | 無効化項目             | 評価目的        |
| --- | ----------------- | ----------- |
| A1  | Candidate隔離       | 直接書込みとの差    |
| A2  | Vector評価          | 単一スコアとの差    |
| A3  | Operator Contract | 修復副作用管理     |
| A4  | MustImprove       | 修復が実際に改善したか |
| A5  | MustNotWorsen     | 他制約悪化の検出    |
| A6  | New Violation禁止   | 新規破綻の検出     |
| A7  | Locality Check    | 範囲外変更       |
| A8  | Intent Retention  | 編集意図消失      |
| A9  | Version Check     | 古いCandidate |
| A10 | Evidence Complete | 判定根拠不足      |
| A11 | Sparse Candidate  | 全量Copyとの差   |
| A12 | Delayed Readback  | CPU同期の影響    |

最低必須：

```text
A2, A3, A5, A6, A7, A8, A9
```

---

# 38. 評価シナリオ

| ID  | 内容                        |
| --- | ------------------------- |
| S01 | 毛先微小移動                    |
| S02 | 毛先大幅移動                    |
| S03 | 中間Segment移動               |
| S04 | Root近傍編集                  |
| S05 | 急曲げ                       |
| S06 | 長さ伸長                      |
| S07 | Collider接近                |
| S08 | Collider侵入                |
| S09 | 境界折れ                      |
| S10 | Length RepairによるBend悪化    |
| S11 | Bend Repairによる侵入発生        |
| S12 | Penetration Repairによる長さ悪化 |
| S13 | 範囲外書込み注入                  |
| S14 | Intent消失Repair            |
| S15 | Version不一致                |
| S16 | Topology不一致               |
| S17 | Collider Version不一致       |
| S18 | NaN注入                     |
| S19 | Inf注入                     |
| S20 | Capacity Overflow         |
| S21 | 同一Guide競合編集               |
| S22 | Brush複数Guide              |
| S23 | Undo／Redo連続               |
| S24 | 30分Stress                 |

---

# 39. 評価指標

## 39.1 Commit安全性

* Unsafe Commit Count
* Hard Violation Commit Count
* New Violation Commit Count
* Operator Contract Failure Commit Count
* Locality Violation Commit Count
* Version Mismatch Commit Count
* NaN／Inf Commit Count

## 39.2 制約単調性

* MustImprove Failure Count
* MustNotWorsen Failure Count
* Allowed Side-effect Exceed Count
* Component-wise Improvement Ratio
* New Violation Rate

## 39.3 局所性

* Outside Range Maximum Delta
* Outside Range Modified Point Count
* Locality Reject Count

## 39.4 編集意図

* Intent Retention
* Candidate-to-Commit Distance
* Intent Reject Count
* Accept率

## 39.5 性能

* Candidate Capture GPU Time
* Violation Before GPU Time
* Repair GPU Time
* Violation After GPU Time
* Contract Evaluation GPU Time
* Locality Evaluation GPU Time
* Intent Evaluation GPU Time
* Commit GPU Time
* End-to-End Latency
* p50／p95／p99 Frame Time

---

# 40. Formal成功条件

以下はFrozen Suite内で満たす条件であり、一般世界での完全安全を意味しない。

1. Unsafe Commit Countが0
2. New Violation Commit Countが0
3. Operator Contract Failure Commit Countが0
4. Locality Violation Commit Countが0
5. Version Mismatch Commit Countが0
6. NaN／Inf Commit Countが0
7. Committed EditのEvidence Record欠損が0
8. Reject時のPersistent State変更が0
9. Same-frame CPU Readbackが0
10. GPU／CPU Commit Decision一致率が99.9%以上
11. 30分試験で継続的Memory増加がない
12. p95 Preview Latencyが16.67ms以下
13. 単一Guide編集のp95 Commit結果通知が3Frame以内

---

# 41. Test追加

```text
tests/final_stage/test_constraint_monotone_edit_runtime.py
tests/final_stage/test_constraint_monotone_edit_shader.py
tests/final_stage/test_constraint_monotone_edit_contracts.py
tests/final_stage/test_constraint_monotone_edit_telemetry.py
tests/final_stage/test_constraint_monotone_edit_formal_suite.py
tests/final_stage/test_constraint_monotone_edit_standalone.py
```

## 41.1 必須検査

* Commit Pass以外でPersistentへ書かない
* Weighted ScoreだけでCommitしない
* Operator Contractが存在する
* MustImprove判定が存在する
* MustNotWorsen判定が存在する
* New Violation判定が存在する
* Locality Checkが存在する
* Intent Checkが存在する
* Version Checkが存在する
* Evidence Recordが存在する
* 他Noveltyを参照しない

---

# 42. Native Self Test

```text
TestViolationVectorLength
TestViolationVectorBend
TestViolationVectorPenetration
TestViolationVectorBoundary
TestViolationVectorVelocity

TestOperatorContractMustImprove
TestOperatorContractMustNotWorsen
TestOperatorContractAllowedSideEffect
TestNewViolationDetection

TestLocalityPass
TestLocalityFailure
TestIntentRetentionPass
TestIntentRetentionFailure

TestVersionMismatchReject
TestNaNReject
TestInfReject
TestCapacityReject

TestEvidenceComplete
TestEvidenceIncompleteReject
TestCommitDecisionCpuGpuAgreement
TestStandaloneNovelty04
```

---

# 43. 実装順序

## Phase 0：Baseline固定

* Asset固定
* Threshold Schema
* Operator Contract Schema
* Seed
* Resolution
* Build
* Solver条件

## Phase 1：CPU Commit規則

* Violation Vector
* Operator Contract
* Monotonicity
* New Violation
* Locality
* Intent
* Version
* Evidence Record

## Phase 2：Candidate隔離

* Operation Queue
* Sparse Candidate
* Persistent非破壊
* Reject

## Phase 3：GPU Violation Vector

* Length
* Bend
* Penetration
* Boundary
* Velocity

## Phase 4：GPU Operator Contract

* MustImprove
* MustNotWorsen
* Side-effect Bound
* New Violation

## Phase 5：Locality／Intent

* Allowed Range
* Outside Delta
* Intent Retention

## Phase 6：Safe Commit

* Version再確認
* Evidence完全性
* Commit／Reject
* Undo確定
* Dirty Range

## Phase 7：既存Repair接続

* Length Repair
* Bend Repair
* Penetration Repair
* Boundary Repair

Repair自体はPhase 6以前のCommit規則と独立して検証する。

## Phase 8：Formal評価

* Baseline
* Ablation
* Stress
* Ground Truth
* Analyzer
* DB

---

# 44. Go／No-Go

## Gate 1：研究核

### Go

* Violation Vectorがある
* Operator Contractがある
* Component-wise判定がある
* New Violation禁止がある
* LocalityとIntentを判定する

### No-Go

* 最終的に一つの総合スコアだけで判定する
* Repair成功フラグだけでCommitする
* Candidate＋Repair＋Commitだけを新規性とする

## Gate 2：Standalone

### Go

* Novelty 04だけで動作
* 他NoveltyのBuffer、Shader、APIを要求しない

### No-Go

* 01のCertificateが必要
* 02のIslandが必要
* 03のRender Certificateが必要
* 05のSchedulerが必要

## Gate 3：Commit

### Go

* Unsafe CommitがFrozen Suiteで0
* Contract Failure Commitが0
* Locality Violation Commitが0

### No-Go

* Weighted Scoreで危険を相殺
* Evidence不足でもCommit
* Version未確認でCommit

---

# 45. 論文用表現

## 45.1 研究目的

本研究では、高密度毛髪システムにおける編集結果の確定判定を対象とする。編集候補および修復後状態について、長さ、曲げ、侵入、境界、速度不連続などの制約違反をベクトルとして保持し、修復Operatorごとに定義した改善対象、悪化禁止対象および許容副作用を検証する。さらに、編集範囲外の非変更、Version一致、編集意図保持を確認し、すべての条件を満たす場合だけ確定状態へ反映する。

## 45.2 貢献

本研究の貢献は、毛髪編集結果を単一の重み付き誤差へ集約せず、制約違反ベクトルとして評価し、Operator Contractに基づく制約単調性、新規違反禁止、局所性、Version一致、編集意図保持をCommit条件として統合した点にある。修復Operatorそのものではなく、異なる修復手法へ共通に適用できるCommit判定規則を検証する。

## 45.3 限定表現

本研究は、Candidate State、GPU Compute、局所修復、PBD、局所Solver、Undo／Redoなどの個別技術を新規性として主張しない。また、本研究で用いるEvidence Recordは形式証明ではない。本研究が主張する安全性は、定義した制約、Collider、編集操作、Frozen Evaluation Suiteの範囲に限定される。

---

# 46. 使用禁止表現

* 世界初
* 完全に安全
* 形式的に証明された
* あらゆる編集を安全化
* すべての物理破綻を防止
* 自己衝突を完全解決
* あらゆるRepairへ無条件適用可能
* 必ず60FPS
* すべてのGPUで高速
* 編集意図を完全保持
* 既存研究に存在しないと証明した
* Certificateにより絶対安全
* 数学的保証がある

---

# 47. Fallback Positioning

極めて近い方式が既存研究に確認された場合は、次の位置付けへ変更する。

> 高密度GPU Hair Editingにおける、制約違反ベクトル、修復副作用、局所性、編集意図を含むCommit判定方式の実装・比較評価。

評価上の貢献：

* 単一スコアとの比較
* Component-wise Commit評価
* Operator副作用評価
* New Violation評価
* Locality評価
* Intent Retention評価
* 100,000本表示環境での性能
* DirectX 11実装
* 30分Stress
* CPU／GPU判定一致

---

# 48. 最終的な研究位置付け

本研究は、新しい毛髪編集UIを提案するものではない。

また、新しい修復Solverや新しいPBD制約を提案するものでもない。

研究対象は、編集または修復された毛髪状態を確定してよいかを判断するCommit規則である。

編集候補と修復後状態について、複数の物理違反をベクトルとして保持する。

修復Operatorごとに、改善すべき制約、悪化してはならない制約、一定範囲内で副作用を許可する制約を定義する。

修復後に、各制約がOperator Contractを満たすこと、新しい違反が発生していないこと、編集範囲外が変更されていないこと、Versionが一致すること、編集意図が一定以上保持されていることを確認する。

すべての条件を満たす結果だけをPersistent Stateへ反映する。

中心となる処理は次である。

[
\boxed{
\text{Violation Vector}
\rightarrow
\text{Operator Contract}
\rightarrow
\text{Component-wise Monotonicity}
\rightarrow
\text{No New Violation}
\rightarrow
\text{Locality}
\rightarrow
\text{Intent Retention}
\rightarrow
\text{Commit / Reject}
}
]

---

# 49. 結論

本研究では、高密度GPU毛髪編集における確定判定方式として、Constraint-Monotone Local Commitを設計する。

従来案のようにCandidate、Repair、Local Solver、Commitという広い処理全体を新規性とはしない。

編集候補および修復後状態の物理違反を複数成分のベクトルとして保持し、単一の重み付き値による相殺を防ぐ。

修復Operatorごとに、改善対象、悪化禁止対象、許容副作用をOperator Contractとして定義する。

修復後に各Contractを満たすこと、新しい物理違反が発生していないこと、編集範囲外が変更されていないこと、Versionが一致すること、編集意図が一定以上保持されていることを確認する。

すべての条件を満たす場合だけ、編集結果をPersistent StateへCommitする。

本研究の安全性主張は、定義した制約、Collider、編集操作、実験条件内に限定する。

この限定により、新規性の中心を広い編集システムから、制約ベクトルと修復副作用を用いた局所Commit判定規則へ絞り、既存編集技術との重複リスクを抑える。
