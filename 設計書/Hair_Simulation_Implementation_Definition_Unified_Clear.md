# 毛髪シミュレーション 実装定義書

## 0. まずここだけ読む

### この文書の役割
この文書は、5つの新規性を別々に見るためではなく、**1つのruntimeとしてどう接続するか** を決める統合仕様である。

### 全体像
```txt
HairAsset
  -> HairAssetAdapter / persistent strandId
  -> Microtype + Certificate
  -> Risk
  -> Counterfactual ModeDecision
  -> PhysicsIsland or RodRepair or Fallback
  -> Commit accepted state
  -> Render vertices
  -> JSONL / CSV / SQLite / thesis tables
```

### 実装先の大分類
| 層 | 主な役割 | 主な実装先 |
|---|---|---|
| Core型定義 | ID、settings、certificate、module別struct | `include/hairgpu/*.h` |
| CPU参照実装 | build、test、fallback、debug検証 | `src/hairgpu_core/*.cpp` |
| GPU pass | decode、risk、mode、island、repair、commit | `apps/dx11_viewer/shaders/*.hlsl` |
| viewer統合 | buffer作成、pass dispatch、telemetry | `GpuHairViewer.h/.cpp` |
| 実験基盤 | 650 cases、runner、DB、Excel | `tools/final_stage/*.py` |

### RuntimeFrameの標準順序
```txt
1. UploadSettingsAndScene
2. ApplyEditCommands
3. EvaluateMicrotypeCertificates
4. EstimateStrandRisk
5. PredictCounterfactualOutcomes
6. AllocateModesUnderBudget
7. CompactRiskyStrands
8. Build / Solve PhysicsIslands
9. Build / Solve RodRepairTasks
10. ApplyFloatFallbackIfNeeded
11. CommitAcceptedStates
12. ComposeRenderVertices
13. WriteEvaluationCounters
14. DrawIndirect
```

### 実装で守ること
```txt
- strandIdを全moduleで共通にする
- certificate failure strandをsilent successにしない
- 各moduleのON/OFFでもログschemaを変えない
- release評価pathに同期readbackを入れない
- buffer overflowは必ずcounterとflagに出す
- Baseline_CurrentProject と Combo_00000 は分けて扱う
```

### 650パターン実験との関係
```txt
32 feature combinations × 10 scenarios × 2 seeds = 640
Baseline_CurrentProject × 10 scenarios = 10
合計 650 cases
```

この統合仕様は、各caseが同じRuntimeSettings、同じScenarioConfig、同じログschemaで比較できることを前提にする。

### この文書の読み方
最初にこの章を読む。次に「実装先ファイル早見表」と「RuntimeFrame仕様」を読む。個別moduleの細部は各DesignDocを参照する。

---
# 実装先ファイル早見表

| 目的 | 追加・変更ファイル |
|---|---|
| Microtype | `include/hairgpu/StrandMicrotype.h`, `src/hairgpu_core/StrandMicrotype.cpp`, `apps/dx11_viewer/shaders/HairMicrotype.hlsl` |
| Mode Allocation | `include/hairgpu/ModeAllocation.h`, `src/hairgpu_core/ModeAllocation.cpp`, `apps/dx11_viewer/shaders/HairModeAllocation.hlsl` |
| Physics Islands | `include/hairgpu/PhysicsIslands.h`, `src/hairgpu_core/PhysicsIslands.cpp`, `apps/dx11_viewer/shaders/HairPhysicsIslands.hlsl` |
| Rod Repair | `include/hairgpu/RodRepair.h`, `src/hairgpu_core/RodRepair.cpp`, `apps/dx11_viewer/shaders/HairRodRepair.hlsl` |
| GPU Runtime | `include/hairgpu/StrandRuntime.h`, `src/hairgpu_core/StrandRuntime.cpp`, `apps/dx11_viewer/shaders/HairStrandRuntimeCommon.hlsl`, `apps/dx11_viewer/shaders/HairStrandRuntime.hlsl` |
| Viewer統合 | `apps/dx11_viewer/src/GpuHairViewer.h`, `apps/dx11_viewer/src/GpuHairViewer.cpp` |
| 実験・評価 | `tools/final_stage/hairgpu_final_run.py`, `tools/final_stage/hairgpu_runtime_suite.py`, `schema.sql`, `hairgpu_final_db.py`, `hairgpu_final_excel.py` |

---
---

# 実装仕様の読み方

この文書は、既存プロジェクト `S.zip` に対して、対象機能をどのファイルへ実装するかを明示するための実装仕様である。  
担当者や実装方法に依存せず、以下を優先する。

```txt
1. 既存の動作を壊さない。
2. 既存のDX11 viewer、hairgpu_core、final_stage toolsに接続する。
3. 新規実装は、C++ core、DX11 viewer integration、HLSL pass、telemetry、testの単位に分ける。
4. 評価ログschemaは全variantで共通にする。
5. 未実装の機能をsilent successにせず、0/null/counter/fallback reasonとして明示する。
```

この文書では、「実装すること」「実装先ファイル」「接続先」「完了条件」を同時に定義する。  
研究説明ではなく、コード修正に使う実装仕様として扱う。
# 実装先ファイル全体マップ

この統合定義書は、5つの新規性と650パターン評価基盤を既存 `S.zip` へ接続するための上位仕様である。  
実装先は以下に固定する。

## C++ coreに追加するファイル

| ファイル | 対応機能 |
|---|---|
| `include/hairgpu/StrandMicrotype.h` / `src/hairgpu_core/StrandMicrotype.cpp` | Physics-Certified Strand Microtype |
| `include/hairgpu/ModeAllocation.h` / `src/hairgpu_core/ModeAllocation.cpp` | Counterfactual Budgeted Mode Allocation |
| `include/hairgpu/PhysicsIslands.h` / `src/hairgpu_core/PhysicsIslands.cpp` | Self-Materializing Strand Physics Islands |
| `include/hairgpu/RodRepair.h` / `src/hairgpu_core/RodRepair.cpp` | Certificate-Driven Rod Repair Calculus |
| `include/hairgpu/StrandRuntime.h` / `src/hairgpu_core/StrandRuntime.cpp` | GPU-Resident Strand Runtime / 統合設定 / feature flags |

`src/hairgpu_core/CMakeLists.txt` へ上記 `.cpp` を追加する。

## DX11 viewerに追加するHLSL

| ファイル | 対応機能 |
|---|---|
| `apps/dx11_viewer/shaders/HairMicrotype.hlsl` | microtype decode / certificate |
| `apps/dx11_viewer/shaders/HairModeAllocation.hlsl` | risk / outcome / mode allocation |
| `apps/dx11_viewer/shaders/HairPhysicsIslands.hlsl` | risky compaction / island build / island solve |
| `apps/dx11_viewer/shaders/HairRodRepair.hlsl` | repair task / rod projection / repair certificate |
| `apps/dx11_viewer/shaders/HairStrandRuntimeCommon.hlsl` | 共通struct/bitmask/counter |
| `apps/dx11_viewer/shaders/HairStrandRuntime.hlsl` | indirect args / commit / render compose / evaluation |

`apps/dx11_viewer/CMakeLists.txt` へ上記 `.hlsl` をコピー対象として追加する。

## DX11 viewerで変更するファイル

| ファイル | 変更内容 |
|---|---|
| `apps/dx11_viewer/src/GpuHairViewer.h` | RuntimeSettings、RuntimeBuffers、feature flags、telemetry fields、debug ringを追加する |
| `apps/dx11_viewer/src/GpuHairViewer.cpp` | shader load、buffer作成、RuntimeFrame順序、dispatch、telemetry exportを追加する |
| `apps/dx11_viewer/src/AppMain.cpp` | 原則変更しない。必要な場合のみheadless引数をviewerへ渡す |

## final_stage toolsで変更するファイル

| ファイル | 変更内容 |
|---|---|
| `tools/final_stage/generate_experiment_650.py` | 650パターンmanifestを生成する。既に別設計書で追加対象 |
| `tools/final_stage/hairgpu_final_run.py` | variant/scenario/seed/feature flagsを環境変数でviewerへ渡す |
| `tools/final_stage/hairgpu_runtime_suite.py` | Proposed/Ablation/Comboの集計、resume、fail-policy、論文用表生成を行う |
| `tools/final_stage/schema.sql` | certificate/mode/island/repair/pass time/readback列を追加する |
| `tools/final_stage/hairgpu_final_db.py` | 拡張CSV/JSONLをSQLiteへ取り込む |
| `tools/final_stage/hairgpu_final_excel.py` | performance/quality/ablation/memory表を出力する |

---

# 統合実装順

```txt
1. `StrandRuntime.h/cpp` にRuntimeSettings、feature flags、variant decoderを追加する。
2. `GpuHairViewer` にvariant/scenario/seed/feature flagsの読み込みを追加する。
3. telemetry schemaを拡張し、未実装項目は0/nullで出す。
4. `StrandMicrotype` を実装し、Microtypeのbufferとcertificateをログへ接続する。
5. `ModeAllocation` を実装し、ModeDecisionBufferとmode countをログへ接続する。
6. `PhysicsIslands` を実装し、candidate/island/commit/rejectをログへ接続する。
7. `RodRepair` を実装し、repair task/status/residual before-afterをログへ接続する。
8. `HairStrandRuntime` のGPU常駐pass、indirect dispatch、commit、debug ringを接続する。
9. Proposed/Ablation/Comboを650パターンrunnerで回し、同一schemaで集計できることを確認する。
```

---

# 実装時の共通禁止事項

```txt
1. 既存Baseline pathを削除しない。
2. feature flag OFF時に既存出力が変わる変更を入れない。
3. certificate failureをログだけにして後続処理へ渡さない実装にしない。
4. GPU task overflowを無視しない。
5. ablationごとにログschemaを変えない。
6. 650パターン評価で手動操作が必要な実装にしない。
```

---

# 共通受け入れ条件

```txt
1. Baseline_CurrentProject、Proposed_FullSystem、Ablation、Combo_00000〜Combo_11111を同じrunnerから起動できる。
2. 全variantでframes.csv、frames.jsonl、summary.jsonのschemaが一致する。
3. Proposed_FullSystemでMicrotype、CBMA、Island、Repair、GPU Residentのcountがログに出る。
4. 各Ablationで無効化されたmoduleのcountは0またはnullとして出る。
5. 失敗時はerror.jsonまたはsummary内error fieldに残り、次caseへ進める。
```

この文書は、毛髪シミュレーションruntimeに関する研究上の位置づけ、統合実装定義、評価指標・比較対象を一つに統合した実装定義書である。
個別の履歴や段階的なロードマップではなく、最終的に実装が満たすべき構造、制御ループ、失敗処理、評価方法をまとめる。

## 目次

1. 新規性の根拠・研究上の位置づけ
2. 統合実装定義
3. 共通評価指標・比較対象

---


## 第1部 新規性の根拠・研究上の位置づけ


### 新規性の根拠・研究上の位置づけ

この文書は、5つの設計書を論文として強く見せるための「新規性の根拠」をまとめる。  
個別設計書は実装指示として使う。本文中の関連研究章・提案手法章・考察章では、本ファイルの論理を参照する。

---

### 1. 中心主張

本研究の中心主張は、単に毛髪を高速に動かすことではない。  
中心は、**10万本規模のstrandを、圧縮表現・危険度予測・局所物理化・局所修復・GPU常駐制御を持つ閉ループとして扱うこと**である。

既存研究や既存エンジンは、主に以下のどれかを強化している。

1. 全体を高速に近似する
2. guide hairをシミュレーションし、残りを補間する
3. GPU上でstrandを描画・シミュレーションする
4. LODやカリングで負荷を下げる
5. ニューラルモデルで髪型・準静的変形・補間を近似する

本研究は、それらと競合する「もう一つのsolver」ではなく、**strand単位で、どの表現・どの物理・どの修復・どのfallbackを選ぶかを証明書付きで制御するruntime architecture**である。

したがって論文での新規性は、以下の一文に集約する。

```txt
本研究は、毛髪を単なる描画対象または一括物理対象として扱うのではなく、
各strandが圧縮表現、物理残差、危険度、計算モード、修復履歴を持つ
certificate-carrying runtime entityとして扱う点に新規性がある。
```

---

### 2. 既存研究の整理

#### 2.1 GPU hair rendering / simulation 系

TressFXやUnreal Groom系の技術は、GPUによる髪・毛皮の描画およびシミュレーション、LOD、エンジン統合を重視している。  
これらは制作システムとして非常に重要だが、研究主張としては「GPU上で髪を扱える」こと自体は既存である。

このため、本研究ではGPU実装そのものを新規性にしない。  
新規性は、GPU上に置いたstrand状態を、risk、mode、island、repair、certificateで閉ループ化する点に置く。

#### 2.2 guide hair / interpolation 系

多くのリアルタイム毛髪では、少数のguide hairをシミュレーションし、残りのrendered hairを補間する。  
この系統は高速だが、接触・局所編集・大変形では補間誤差やclumping、penetrationが問題になりやすい。

本研究はguide補間そのものを否定しない。  
ただし、全体をguide interpolationだけで押し切るのではなく、危険な実strand ID集合を検出し、その部分だけphysics islandやrod repairへ昇格させる。

#### 2.3 physics solver 系

FTL、mass-spring、Cosserat rod、DER、AMSなどは、髪の物理モデルやsolverの安定性を高める研究である。  
これらは「どう解くか」に強い。  
本研究は「どのstrandをいつ、どの精度で、どのsolverへ送るか」を扱う。

つまり、solverを置き換える研究ではなく、solverを選択・限定・修復・fallbackする制御層の研究である。

#### 2.4 neural hair 系

Neural InterpolationやQuaffureなどは、実時間性や一般化を狙う有力な方向である。  
ただし、学習分布外、接触の局所破綻、制作中の編集差分、物理的安全性の説明可能性は、依然として論文上の議論点にできる。

本研究はニューラル近似を使わない前提でも成立する。  
必要であれば、将来的にcounterfactual predictorの一部をニューラル化できるが、卒業研究ではrule-based / regression-based predictorで十分である。

---

### 3. 5つの新規性の役割

#### 3.1 Physics-Certified Strand Microtype

役割：髪をfloat点列ではなく、root frame、arc-length、curvature、torsion、residual、certificateを持つ専用表現として保持する。

新規性の言い方：

```txt
圧縮表現そのものに物理残差と安全判定を埋め込むことで、decode前に危険strandを検出し、必要なstrandだけをpartial decodeまたはfallbackできる。
```

論文で避けるべき言い方：

```txt
単にメモリを削減した。
```

強い言い方：

```txt
メモリ削減ではなく、圧縮表現をruntime制御の観測可能な状態にした。
```

#### 3.2 Counterfactual Budgeted Mode Allocation

役割：各strandに対して、Skip、QSOnly、DynamicResidual、PhysicsIsland、RodRepair、FloatFallbackの各modeを選んだ場合の誤差・破綻確率・コストを予測し、予算内で割り当てる。

新規性の言い方：

```txt
固定LODや距離ベース選択ではなく、各modeを選んだ場合の反実仮想結果を予測し、frame budgetを満たすようにstrandごとの計算モードを割り当てる。
```

論文で避けるべき言い方：

```txt
LODを工夫した。
```

強い言い方：

```txt
LODではなく、品質・失敗確率・計算時間を同時に制約するruntime resource allocation問題として毛髪計算を定式化した。
```

#### 3.3 Self-Materializing Strand Physics Islands

役割：接触・編集・破綻リスクが高いpersistent strand ID集合だけを、GPU上で一時的な局所物理空間として実体化する。

新規性の言い方：

```txt
guide clusterや固定gridではなく、危険strandの近接グラフconnected componentとして、そのフレームに必要なphysics islandを自己生成する。
```

論文で避けるべき言い方：

```txt
局所物理を使った。
```

強い言い方：

```txt
物理領域を事前に固定せず、runtime certificateとriskに応じて局所solverの対象自体を生成・結合・分割・消滅させる。
```

#### 3.4 Certificate-Driven Rod Repair Calculus

役割：破綻したstrandまたはsegment帯だけに対し、length、bend、twist、penetration residualを使って修復範囲・iteration数・成功判定を決める。

新規性の言い方：

```txt
full rod solverを常時全体へ適用するのではなく、certificateが失敗した局所segment帯だけをJITに修復し、修復結果を再びcertificateとしてruntimeへ戻す。
```

論文で避けるべき言い方：

```txt
破綻を直した。
```

強い言い方：

```txt
修復をポスト処理ではなく、証明可能なruntime制御ループの一部として定義した。
```

#### 3.5 GPU-Resident Strand Runtime

役割：Microtype、Mode Allocation、Physics Islands、Rod RepairをGPU上で閉ループ化する。

新規性の言い方：

```txt
CPUが毎フレーム判断するのではなく、risk、mode、compaction、island、repair、certificate更新をGPU上に常駐させ、DispatchIndirect / DrawIndirectで可変処理を実行する。
```

論文で避けるべき言い方：

```txt
GPUで実装した。
```

強い言い方：

```txt
可変数の危険strandと局所物理タスクをCPU readbackなしで閉ループ制御するruntimeを設計した。
```

---

### 4. 新規性の証明構造

論文中では、以下の順で議論すると強い。

```txt
1. 毛髪は本数が多く、全strandを高精度に処理できない。
2. 既存のリアルタイム手法は、guide補間、LOD、GPU最適化、軽量solver、ニューラル近似により負荷を下げる。
3. しかし、局所接触・局所編集・大変形では、どのstrandを高精度化すべきか、いつ修復すべきか、結果が安全かを統一的に扱いにくい。
4. そこで本研究では、各strandをcertificate付きruntime entityとして扱う。
5. certificateを使って、圧縮、mode選択、island生成、rod repair、fallbackを同じ制御ループに統合する。
6. これにより、現行プロジェクトと比較して、同じframe budget内で破綻率・p99 frame time・編集遅延・メモリ使用量を改善できるかを評価する。
```

---

### 5. 関連研究章での使い分け

第2章では、以下のように節を切るとよい。

```txt
2.1 Strand-based hair representation and rendering
2.2 Guide hair simulation and interpolation
2.3 Real-time physical hair solvers
2.4 Neural and data-driven hair models
2.5 Runtime LOD, budget control, and GPU scheduling
2.6 本研究の位置づけ
```

2.6では、次のようにまとめる。

```txt
既存研究は、表現、solver、補間、描画、学習モデルのいずれかを主対象にする傾向がある。
本研究は、それらの個別技術を置き換えるのではなく、strandごとの物理的安全性をcertificateとして持ち、
そのcertificateを用いて表現、計算モード、局所物理、修復、fallbackを統合制御する点に位置づけられる。
```

---

### 6. 卒業研究としての安全な主張範囲

強く主張してよいこと：

- 現行プロジェクトをbaselineとして、提案5要素の効果を測る
- 10万strand規模を想定した設計にする
- 60FPS制約を評価指標に含める
- certificateを使って破綻検出・mode選択・修復・fallbackを統一する
- 各要素のablationにより、どの新規性が効いたかを分けて示す

断言を避けるべきこと：

- 世界初である
- すべての既存研究より高品質である
- 物理的に完全に正しい
- すべての髪型で破綻しない
- どのGPUでも60FPSを保証する

安全で強い表現：

```txt
調査した既存研究・実装の範囲では、圧縮表現、反実仮想的mode割当、自己生成physics island、certificate-driven repair、GPU常駐制御を、strand単位のcertificateを中心に統合した例は確認できなかった。
本研究は、この統合制御を現行プロジェクト上に実装し、性能・品質・破綻率・編集遅延の観点から評価する。
```

---

### 7. 論文本文にそのまま使える新規性段落

```txt
本研究の新規性は、毛髪シミュレーションにおける個別の高速化手法ではなく、
strand単位の物理的安全性を中心としたruntime制御構造にある。
既存のリアルタイム毛髪手法では、少数のguide hairをシミュレーションして補間する方法、
GPU上でstrandを高速に描画・更新する方法、軽量な物理solverやニューラル近似を用いる方法が広く用いられてきた。
これらは計算量を削減する有効な方法である一方、局所的な接触、編集、大変形が発生したとき、
どのstrandを高精度化し、どの範囲を修復し、修復結果を安全とみなせるかを統一的に扱うことは難しい。

そこで本研究では、各strandをcertificate-carrying runtime entityとして扱う。
提案手法では、圧縮表現であるPhysics-Certified Strand Microtypeに物理残差を埋め込み、
Counterfactual Budgeted Mode Allocationにより各計算モードの誤差・破綻確率・計算コストを予測し、
危険なstrand集合のみをSelf-Materializing Strand Physics Islandsとして局所物理化する。
さらに、certificateに失敗したsegment帯に対してCertificate-Driven Rod Repair Calculusを適用し、
これら全体をGPU-Resident Strand RuntimeとしてCPU readbackを避けながら閉ループ化する。
これにより、10万本規模の髪に対し、固定frame budget内でメモリ、帯域、破綻率、編集応答性を同時に評価可能な設計を実現する。
```

---

### 参考資料URL

本Zipでは、以下を既存技術の位置づけ確認に用いた。

- AMD GPUOpen, TressFX: https://gpuopen.com/tressfx/
- AMD GPUOpen-Effects/TressFX GitHub: https://github.com/GPUOpen-Effects/TressFX
- Epic Games, An early look at next-generation real-time hair and fur: https://www.unrealengine.com/tech-blog/an-early-look-at-next-generation-real-time-hair-and-fur
- Epic Games, Groom Strands / LOD documentation: https://dev.epicgames.com/documentation/unreal-engine/groom-strands-in-unreal-engine
- Müller, Kim, Chentanez, Fast Simulation of Inextensible Hair and Fur, VRIPHYS 2012: https://diglib.eg.org/items/11db9962-07b8-46a1-ab46-ecc9ee05086d
- Hsu et al., Real-time Physically Guided Hair Interpolation, SIGGRAPH 2024: https://kuiwuchn.github.io/hairInterp.html
- Lyu et al., Real-Time Hair Simulation with Neural Interpolation, TVCG 2020: https://mlchai.com/publication/lyu2020real/
- Amador H. et al., Augmented Mass-Spring Model for Real-Time Dense Hair Simulation, 2025: https://arxiv.org/html/2412.17144v3
- Stuyck et al., Quaffure: Real-Time Quasi-Static Neural Hair Simulation, 2025: https://arxiv.org/html/2412.10061v1

---

### 8. 実装定義における研究貢献の切り分け

本研究の貢献は、個別の数値solverや単独の圧縮形式ではなく、strand単位の状態をcertificateで接続するruntime設計にある。論文では、以下のように貢献を切り分ける。

#### 8.1 貢献1: Certificate付きstrand表現

```txt
対象:
  Physics-Certified Strand Microtype

主張:
  strandを単なる点列ではなく、decode error、length residual、bend residual、twist residual、penetration riskを持つruntime entityとして扱う。

実装で示すこと:
  1. full decodeなしに危険strandを検出できる。
  2. residualの種類別に後続処理へ送れる。
  3. 編集後にstale状態を検出できる。
```

#### 8.2 貢献2: 反実仮想に基づくmode割当

```txt
対象:
  Counterfactual Budgeted Mode Allocation

主張:
  各strandについて、選択したmodeだけでなく、選択しなかったmodeの予測結果も持ち、frame budget内で品質と安全性を配分する。

実装で示すこと:
  1. Skip、QSOnly、DynamicResidual、PhysicsIsland、RodRepair、FloatFallbackの候補を比較する。
  2. certificate failure strandを単純に低コストmodeへ落とさない。
  3. 予測costと実測costの差分を次フレームへ反映する。
```

#### 8.3 貢献3: 自己生成される局所物理island

```txt
対象:
  Self-Materializing Strand Physics Islands

主張:
  固定clusterやguide hair単位ではなく、runtime riskから実strand ID集合を生成し、connected componentとして局所物理空間を作る。

実装で示すこと:
  1. 接触・編集・certificate failureからcandidate strand集合を作る。
  2. 近接グラフによりislandを生成する。
  3. island solve結果をcertificateでcommit/rejectする。
```

#### 8.4 貢献4: Certificate駆動の局所Rod Repair

```txt
対象:
  Certificate-Driven Rod Repair Calculus

主張:
  rod solverを全体solverとして使うのではなく、certificate failureが示すsegment帯にだけJIT修復として使う。

実装で示すこと:
  1. repair対象がstrand全体ではなくsegment範囲である。
  2. length、bend、twist、penetrationの失敗理由別に修復する。
  3. 修復後にcertificateを再計算し、失敗時は明示的にfallbackする。
```

#### 8.5 貢献5: GPU常駐の閉ループruntime

```txt
対象:
  GPU-Resident Strand Runtime

主張:
  GPU上で髪を更新するだけではなく、risk、mode、island、repair、certificate、renderをCPU readbackなしで接続する。

実装で示すこと:
  1. risky strand compactionをGPU上で行う。
  2. island taskとrepair taskをGPU上で生成する。
  3. DispatchIndirect / DrawIndirectで可変数処理を行う。
  4. debug readbackは遅延ring bufferに限定する。
```

### 9. 既存技術との差分を説明する表現

論文では、以下のような対比で説明する。

| 既存の見方 | 本研究の見方 | 実装上の違い |
|---|---|---|
| 髪は点列またはcurve | 髪はcertificate付きruntime entity | strandごとにresidualとfallback状態を持つ |
| LODは距離や画面占有率で決める | modeは反実仮想cost/errorで決める | 非選択modeの予測結果も保存する |
| 局所処理は固定clusterで行う | 局所処理はriskから生成されるislandで行う | connected componentを毎フレーム生成する |
| rod solverは高品質solver | rod solverはcertificate failureの局所repair | segment範囲とiteration数をcertificateで決める |
| GPUは演算場所 | GPUは制御ループ全体の常駐場所 | task生成・割当・commit判定までGPUで行う |

### 10. 主張しないこと

新規性を過剰に広げないため、以下は主張しない。

```txt
1. 世界初のGPU hair simulationであるとは主張しない。
2. 世界初のguide hair補間であるとは主張しない。
3. rod solver自体を新規物理モデルとして主張しない。
4. neural hairの代替として万能であるとは主張しない。
5. すべての髪型・すべての接触条件で最良であるとは主張しない。
```

代わりに、以下を中心に主張する。

```txt
本研究は、既存の圧縮、LOD、局所solver、GPU実装を単体で置き換えるのではなく、
strand単位のcertificateを介して、表現選択・mode選択・局所物理化・局所修復・fallbackを接続するruntime architectureを定義する。
```

### 11. 論文での説明順序

論文本文では、次の順序で説明すると論理が通りやすい。

```txt
1. 既存のリアルタイム毛髪では、guide補間、LOD、GPU処理が重要である。
2. しかし、局所接触、編集、破綻検出、fallback判断はstrand単位に閉じていないことが多い。
3. そこで、本研究ではstrandをcertificate付きruntime entityとして扱う。
4. certificateにより、危険strandを検出し、計算modeを割り当てる。
5. 危険strand集合はphysics islandとして一時的に実体化する。
6. 局所segment破綻はrod repairで修復する。
7. これらをGPU常駐のframe loopとして接続する。
8. 評価では、速度だけでなくcertificate failure、fallback、p99、ablationを見る。
```

### 12. 実装定義書との対応

| 論文上の主張 | 対応する実装定義書 | 評価で見る指標 |
|---|---|---|
| certificate付きstrand表現 | `DesignDoc_01` | VRAM、decode error、certificate failure |
| 反実仮想mode割当 | `DesignDoc_02` | budget違反、mode分布、cost prediction error |
| 自己生成island | `DesignDoc_03` | island commit rate、penetration、boundary discontinuity |
| 局所rod repair | `DesignDoc_04` | repair success rate、residual before/after |
| GPU常駐runtime | `DesignDoc_05` | p99 frame time、readback count、pass time |
| 統合制御 | `99_Integrated_Implementation_Definition` | ablation、相乗効果、fallback reason |


---


## 第2部 統合実装定義


### 統合実装定義書

この文書は、毛髪シミュレーションruntimeの統合実装を定義する。  
ここでは、実装が満たすべき構造、入出力、制御ループ、失敗処理、評価ログを規定する。

---

### 1. 中心定義

実装では、10万本規模のstrandを以下のような **certificate-carrying runtime entity** として扱う。

```txt
strandId
  -> StrandMicrotype
  -> StrandDecodeCertificate
  -> StrandRisk
  -> Counterfactual ModeDecision
  -> optional PhysicsIsland
  -> optional RodRepair
  -> final decoded/render state
  -> evaluation log
```

実装では、髪は単なるfloat点列、単なるGPU buffer、単なるguide interpolation対象ではない。  
各strandは、圧縮表現、物理残差、危険度、計算mode、局所物理化、局所修復、fallback履歴を持つ。

```txt
System Claim:
本実装は、毛髪をstrand単位のcertificate付きruntime entityとして扱い、
risk -> mode -> island -> repair -> certificate -> renderをGPU常駐で閉ループ化する。
```

---

### 2. 必須モジュール

実装は以下の6モジュールで構成する。

```txt
1. HairAssetAdapter
2. PhysicsCertifiedStrandMicrotype
3. CounterfactualBudgetedModeAllocation
4. SelfMaterializingStrandPhysicsIslands
5. CertificateDrivenRodRepair
6. GpuResidentStrandRuntime
```

#### 2.1 HairAssetAdapter

既存プロジェクトのHairAsset、Groom、Guide、Render Strand、Collision Scene、Edit Commandを、提案runtimeの共通ID空間に変換する層である。

```cpp
struct HairRuntimeAsset
{
    uint32_t strandCount;
    uint32_t pointCount;

    GPUBuffer restPoints;
    GPUBuffer restWidths;
    GPUBuffer rootBindings;
    GPUBuffer strandRanges;

    GPUBuffer strandMicrotypes;
    GPUBuffer microtypePayload;

    GPUBuffer persistentStrandIds;
};
```

必須条件：

```txt
1. 元Assetのstrand indexとruntime strandIdを1対1で対応させる。
2. edit、simulation、render、evaluation logで同じstrandIdを使う。
3. guide hairとrender hairを分けている場合でも、評価対象の実strand IDを保持する。
4. Asset変換時にMicrotype buildと初期certificate計算を実行する。
```

#### 2.2 PhysicsCertifiedStrandMicrotype

髪の基礎表現である。詳細は `DesignDoc_01_PhysicsCertifiedStrandMicrotype.md` に従う。

実装で必ず提供するAPI：

```cpp
StrandMicrotypeBuildOutput BuildMicrotype(const StrandMicrotypeBuildInput& input);
DecodeOutput DecodeMicrotype(const StrandMicrotype& microtype, DecodeMode mode);
DecodeOutput PartialDecode(const StrandMicrotype& microtype, uint32_t segmentStart, uint32_t segmentEnd);
StrandDecodeCertificate EvaluateDecodeCertificate(const DecodeOutput& decoded);
void ApplyEditInvalidation(uint32_t strandId, const EditCommand& command);
```

Microtypeは、render用decode、risk評価用軽量decode、RodRepair用segment decode、FloatFallback復元、評価ログ用residual算出に使う。

#### 2.3 CounterfactualBudgetedModeAllocation

各strandについて、candidate modeを選んだ場合の品質・失敗確率・コストを予測し、budget内でmodeを割り当てる。詳細は `DesignDoc_02_CounterfactualBudgetedModeAllocation.md` に従う。

実装で必ず提供するAPI：

```cpp
void EvaluateStrandRiskGPU(const RuntimeBuffers& buffers, const RuntimeInput& input);
void PredictCounterfactualOutcomesGPU(const RuntimeBuffers& buffers, const RuntimeSettings& settings);
void AllocateModesGPU(const RuntimeBuffers& buffers, const RuntimeSettings& settings);
void UpdateModeHysteresisGPU(const RuntimeBuffers& buffers, uint32_t frameIndex);
```

modeは以下に固定する。

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

必須条件：

```txt
1. certificate failure strandはSkip不可。
2. contact/edit/certificate riskが高いstrandはPhysicsIslandまたはRodRepair候補に入れる。
3. 予算超過時は、危険度が低く品質劣化が小さいstrandから降格する。
4. 前フレームmodeを使い、mode poppingを抑える。
```

#### 2.4 SelfMaterializingStrandPhysicsIslands

危険strand集合をGPU上でcompactし、近接グラフのconnected componentとして局所物理islandを生成する。詳細は `DesignDoc_03_SelfMaterializingStrandPhysicsIslands.md` に従う。

実装で必ず提供するAPI：

```cpp
void CompactIslandCandidatesGPU(const RuntimeBuffers& buffers);
void BuildIslandSpatialHashGPU(const RuntimeBuffers& buffers);
void BuildConnectedComponentsGPU(const RuntimeBuffers& buffers);
void SolvePhysicsIslandsGPU(const RuntimeBuffers& buffers, const CollisionScene& collisionScene);
void CommitOrRejectIslandsGPU(const RuntimeBuffers& buffers);
```

必須条件：

```txt
1. islandはpersistent strandIdの集合として作る。
2. islandは固定clusterではなく、runtime riskから生成する。
3. island solver結果はcertificateでcommit/rejectする。
4. rejectされたislandはRodRepairまたはFloatFallbackへ送る。
5. island生成数とmember数は上限を持ち、上限到達時はログに残す。
```

#### 2.5 CertificateDrivenRodRepair

certificate failureが出たstrand/segment帯だけに、局所ロッド修復を適用する。詳細は `DesignDoc_04_CertificateDrivenRodRepair.md` に従う。

実装で必ず提供するAPI：

```cpp
void CompactRepairTasksGPU(const RuntimeBuffers& buffers);
void BuildRepairTaskRangesGPU(const RuntimeBuffers& buffers);
void SolveRodRepairGPU(const RuntimeBuffers& buffers, const CollisionScene& collisionScene);
void EvaluateRepairCertificateGPU(const RuntimeBuffers& buffers);
void CommitOrFallbackRepairGPU(const RuntimeBuffers& buffers);
```

必須条件：

```txt
1. RepairTaskはstrand単位ではなくsegmentStart/segmentEndを持つ。
2. length/bend/twist/penetration residualを修復前後で比較する。
3. 修復後certificateValue <= threshold の場合だけcommitする。
4. 失敗時はfallbackRequiredを明示し、silent failureを禁止する。
```

#### 2.6 GpuResidentStrandRuntime

上記5要素をGPU常駐で閉ループ化するruntimeである。詳細は `DesignDoc_05_GpuResidentStrandRuntime.md` に従う。

実装で必ず提供するAPI：

```cpp
class HairStrandRuntime
{
public:
    bool Initialize(const HairRuntimeAsset& asset, const RuntimeSettings& settings);
    RuntimeOutput ExecuteFrame(const RuntimeInput& input);
    void Resize(uint32_t strandCount, uint32_t maxPointCount);
    void ReadbackDebugStatsDelayed(uint32_t frameDelay);
    void Shutdown();
};
```

必須条件：

```txt
1. 通常frame pathにCPU-GPU同期readbackを入れない。
2. 可変数taskはGPU上でcompactし、DispatchIndirectで処理する。
3. debugStatsはring bufferでN frame遅延readbackする。
4. ablation切替はRuntimeSettingsで行う。
```

---

### 3. RuntimeSettings

実装は、提案手法・baseline・ablationを同じ実装上で切り替えられる必要がある。

```cpp
struct RuntimeSettings
{
    bool enableMicrotype;
    bool enableCBMA;
    bool enablePhysicsIslands;
    bool enableRodRepair;
    bool enableGpuResidentRuntime;

    bool enableFloatFallback;
    bool enableDebugValidation;
    bool enableDelayedReadback;

    float frameBudgetMs;
    float gpuBudgetMs;
    float memoryBudgetMB;
    float bandwidthBudgetMB;

    float decodeTolerance;
    float lengthTolerance;
    float bendTolerance;
    float twistTolerance;
    float penetrationTolerance;

    float riskThreshold;
    float islandRiskThreshold;
    float repairCertificateThreshold;

    uint32_t maxIslandCount;
    uint32_t maxMembersPerIsland;
    uint32_t maxRepairTasks;
    uint32_t modeMinimumLifetime;
    uint32_t debugReadbackLatency;
};
```

必須preset：

```txt
Baseline_CurrentProject
Baseline_RefactoredOnly
Proposed_FullSystem
Ablation_NoMicrotype
Ablation_NoCBMA
Ablation_NoIslands
Ablation_NoRodRepair
Ablation_NoGpuResident
```

---

### 4. RuntimeFrame仕様

これは、1フレーム内で満たすべき制御構造である。

```cpp
RuntimeOutput HairStrandRuntime::ExecuteFrame(const RuntimeInput& input)
{
    UploadFrameConstants(input);
    UploadEditCommands(input.editCommands);
    UploadCollisionScene(input.collisionScene);

    ApplyEditInvalidationGPU(buffers);
    EvaluateMicrotypeCertificatesGPU(buffers);
    EvaluateStrandRiskGPU(buffers, input);

    PredictCounterfactualOutcomesGPU(buffers, settings);
    AllocateModesGPU(buffers, settings);
    UpdateModeHysteresisGPU(buffers, input.frameIndex);

    CompactIslandCandidatesGPU(buffers);
    BuildIslandSpatialHashGPU(buffers);
    BuildConnectedComponentsGPU(buffers);
    SolvePhysicsIslandsGPU(buffers, input.collisionScene);
    CommitOrRejectIslandsGPU(buffers);

    CompactRepairTasksGPU(buffers);
    BuildRepairTaskRangesGPU(buffers);
    SolveRodRepairGPU(buffers, input.collisionScene);
    EvaluateRepairCertificateGPU(buffers);
    CommitOrFallbackRepairGPU(buffers);

    DecodeFinalStrandStatesGPU(buffers);
    ComposeRenderVerticesGPU(buffers);
    WriteFrameDebugStatsGPU(buffers);

    return BuildRuntimeOutput(buffers);
}
```

実装では、失敗しても処理を黙って止めない。必ず以下のいずれかへ遷移させる。

```txt
1. lower-cost modeへの降格
2. PhysicsIslandへの昇格
3. RodRepairへの修復依頼
4. FloatFallbackへの復元
5. 次フレーム継続task化
6. debug validation errorとしてログ記録
```

---

### 5. GPU Buffer Contract

全モジュールは、以下のbuffer contractを共有する。

```cpp
struct RuntimeBuffers
{
    GPUBuffer strandMicrotypes;
    GPUBuffer microtypePayload;

    GPUBuffer strandStates;
    GPUBuffer certificates;
    GPUBuffer risks;
    GPUBuffer counterfactualOutcomes;
    GPUBuffer modeDecisions;

    GPUBuffer islandCandidateIds;
    GPUBuffer islandTasks;
    GPUBuffer islandMembers;
    GPUBuffer strandToIsland;

    GPUBuffer repairCandidateIds;
    GPUBuffer repairTasks;
    GPUBuffer repairCertificates;

    GPUBuffer decodedPoints;
    GPUBuffer renderVertices;

    GPUBuffer dispatchArgs;
    GPUBuffer drawArgs;
    GPUBuffer debugStats;
};
```

必須ルール：

```txt
1. buffer indexはstrandId基準にする。
2. 可変長配列はoffset/count形式で参照する。
3. compact bufferにはcounter bufferを持たせ、counterはGPU上で更新する。
4. DispatchIndirect argsはGPU上で生成する。
5. CPU readbackが必要なdebugStatsはring bufferへ書く。
```

---

### 6. Certificate共通仕様

全モジュールは、certificateを共通の安全判定として扱う。

```cpp
struct UnifiedStrandCertificate
{
    float decodeResidual;
    float lengthResidual;
    float bendResidual;
    float twistResidual;
    float penetrationResidual;
    float temporalResidual;

    float certificateValue;
    uint32_t certificateBits;
    uint32_t failureReasonBits;
};
```

certificateValueは以下の形で統一する。

```txt
certificateValue <= 1.0 なら許容範囲内
certificateValue >  1.0 ならfailure
```

```txt
certificateValue = max(
    decodeResidual / decodeTolerance,
    lengthResidual / lengthTolerance,
    bendResidual / bendTolerance,
    twistResidual / twistTolerance,
    penetrationResidual / penetrationTolerance,
    temporalResidual / temporalTolerance
)
```

failureReasonBits：

```txt
Bit 0: DecodeResidualFailure
Bit 1: LengthResidualFailure
Bit 2: BendResidualFailure
Bit 3: TwistResidualFailure
Bit 4: PenetrationFailure
Bit 5: TemporalFailure
Bit 6: EditInvalidated
Bit 7: BudgetForcedFallback
```

---

### 7. 失敗処理

実装で禁止すること：

```txt
1. certificate failureを無視してrenderする。
2. 予算超過時に危険strandから先に削る。
3. GPU task overflowを黙って破棄する。
4. repair失敗後に成功扱いでcommitする。
5. CPU readback同期でframeを止める。
6. baselineとproposalで異なる評価ログ形式を使う。
```

実装のfallback優先度：

```txt
1. PartialDecode
2. QSOnly / DynamicResidualへの降格
3. PhysicsIslandへの昇格
4. RodRepair
5. FloatFallback
6. LastStableState
```

FloatFallbackは失敗ではなく、runtimeが破綻を検知して安全側へ倒した記録として扱う。論文では、fallbackCountが品質・安定性・計算予算のトレードオフ指標になる。

---

### 8. ログ仕様

実装は、全実験で同じJSONL schemaを使う。

```json
{
  "frame": 120,
  "scenario": "ContactHeavy",
  "variant": "Proposed_FullSystem",
  "strand_count": 100000,
  "frame_time_ms": 15.8,
  "gpu_time_ms": 12.4,
  "cpu_time_ms": 2.1,
  "p99_frame_time_ms": 18.6,
  "vram_mb": 768,
  "bandwidth_mb": 410,
  "certificate_failure_count": 320,
  "fallback_count": 41,
  "mode_counts": {
    "Skip": 12000,
    "QSOnly": 62000,
    "DynamicResidual": 21000,
    "PhysicsIsland": 3200,
    "RodRepair": 1500,
    "FloatFallback": 300
  },
  "island_count": 54,
  "island_member_count": 3200,
  "repair_task_count": 1500,
  "repair_success_rate": 0.91,
  "max_penetration_mm": 1.2,
  "mean_decode_error_mm": 0.21
}
```

必須集計：

```txt
1. p50/p95/p99 frame_time_ms
2. mean/max certificateValue
3. certificate_failure_count
4. fallback_count
5. mode_counts
6. island_count / island_member_count
7. repair_task_count / repair_success_rate
8. VRAM / bandwidth
9. visual/physical quality metrics
```

---

### 9. テスト仕様

実装には、単体テストと統合テストの両方を含める。

#### 9.1 単体テスト

```txt
Test_Microtype_RoundTrip_Static
Test_Microtype_HighCurvatureFallback
Test_Microtype_EditInvalidation
Test_CBMA_BudgetRespected
Test_CBMA_CertificateFailureCannotSkip
Test_CBMA_ModeHysteresis
Test_Island_ConnectedComponents
Test_Island_CommitRejectByCertificate
Test_RodRepair_ReducesResidual
Test_RodRepair_FallbackOnFailure
Test_GPU_NoSynchronousReadbackInRelease
```

#### 9.2 統合テスト

```txt
Test_Runtime_StaticGroom_StableCertificates
Test_Runtime_FastHeadMotion_BudgetStable
Test_Runtime_ContactHeavy_IslandAndRepairTriggered
Test_Runtime_LocalEditing_EditBitsAndReencode
Test_Runtime_Dense100K_NoBufferOverflow
Test_Runtime_AblationSwitchesProduceComparableLogs
```

#### 9.3 成功条件

```txt
1. Proposed_FullSystemが全scenarioで実行できる。
2. Baseline_CurrentProjectと同じscenario・同じlog schemaで比較できる。
3. 各ablationをRuntimeSettingsだけで切り替えられる。
4. certificate failure、fallback、task overflowがすべてログに記録される。
5. release pathに同期readbackが残っていない。
```

---

### 10. 実装補助へ渡す実装指示

以下の文面を、実装時に参照する統合指示として使う。

```txt
以下の設計書群を読み、統合runtimeとして動作する実装を作成してください。

参照する設計書:
- 00_Novelty_Rationale_and_Positioning.md
- Common_Evaluation_and_Baseline.md
- DesignDoc_01_PhysicsCertifiedStrandMicrotype.md
- DesignDoc_02_CounterfactualBudgetedModeAllocation.md
- DesignDoc_03_SelfMaterializingStrandPhysicsIslands.md
- DesignDoc_04_CertificateDrivenRodRepair.md
- DesignDoc_05_GpuResidentStrandRuntime.md
- 99_Integrated_Implementation_Definition.md

実装の必須条件:
1. strandIdを全モジュールで共通にしてください。
2. Microtype、certificate、risk、mode decision、island、repair、render stateをGPU buffer contractで接続してください。
3. RuntimeSettingsでBaseline、Proposed、Ablationを切り替えられるようにしてください。
4. 通常frame pathではCPU-GPU同期readbackを禁止してください。
5. certificate failure、fallback、task overflow、repair failureを必ずログ化してください。
6. Common_Evaluation_and_Baseline.mdのJSONL schemaに沿って評価ログを出してください。
7. unit testとintegration testを含めてください。
8. 実装内にTODOとして主要機能を残さず、少なくとも安全に動作する代替処理またはfallbackを実装してください。

出力に含めるもの:
- 追加・変更するファイル一覧
- 主要struct/class/functionの完全な定義
- RuntimeFrameの実装
- GPU passまたはcompute shaderの疑似コード/実コード
- RuntimeSettings preset
- JSONL logging実装
- unit/integration test
- baseline/proposed/ablation切替方法
```

---

### 11. 論文本文に反映する実装説明

論文では、実装を以下のように説明する。

```txt
本研究の実装は、髪を単なる点列または一括solverの入力として扱わず、各strandをcertificate付きruntime entityとして扱う。
各frameでは、Microtype上で物理残差を評価し、riskを推定し、各modeの反実仮想的な品質・失敗確率・計算コストを予測する。
そのうえで、frame budget内に収まるようにmodeを割り当て、危険strand集合のみをphysics islandまたはrod repairへ送る。
すべての判定結果はcertificateとして再評価され、成功した状態のみがrender stateへcommitされる。
この処理はGPU常駐buffer上で閉ループ化され、CPU readbackによる同期を通常frame pathから排除する。
```

---

### 12. 詳細実装定義

この章では、統合runtimeをコードへ落とすための詳細を定義する。ここでの目的は、各DesignDocの機能を個別に置くのではなく、1つのframe loop内で矛盾なく接続することである。

#### 12.1 Runtime全体の責務分離

runtimeは、以下の責務を明確に分ける。

```txt
Asset Build Layer:
  Groom/Asset入力をpersistent strandId付きのHairRuntimeAssetへ変換する。

Persistent State Layer:
  フレームをまたいで保持されるstrand state、mode state、certificate stateを管理する。

Transient Task Layer:
  当該フレームでのみ有効なrisky list、island task、repair task、indirect argsを生成する。

Solver Layer:
  Microtype decode、local physics、rod repair、fallbackを実行する。

Commit Layer:
  certificate判定に通った結果だけをpersistent stateまたはrender stateへ反映する。

Evaluation Layer:
  同期readbackを避けつつ、フレーム統計と評価ログを蓄積する。
```

各layerはbufferを直接共有してよいが、書き込み権限は1フレーム内で一意に決める。同一bufferへ複数passが書く場合は、append buffer、atomic counter、またはdouble bufferを使い、書き込み競合を避ける。

#### 12.2 RuntimeFrameの処理順

1フレームの処理順は以下で固定する。

```txt
RuntimeFrame(frameIndex):
  1. UploadSettingsAndScene()
  2. ApplyEditCommands()
  3. UpdateRootTransforms()
  4. EvaluateMicrotypeCertificates()
  5. EstimateStrandRisk()
  6. PredictCounterfactualOutcomes()
  7. AllocateModesUnderBudget()
  8. CompactRiskyStrands()
  9. BuildPhysicsIslands()
 10. SolvePhysicsIslands()
 11. BuildRepairTasks()
 12. SolveRodRepairTasks()
 13. ApplyFloatFallbackIfNeeded()
 14. CommitAcceptedStates()
 15. ComposeRenderVertices()
 16. WriteEvaluationCounters()
 17. IssueDrawIndirect()
```

この順序で重要なのは、`CommitAcceptedStates()` までpersistent stateを確定させないことである。Microtype decode、island solve、repair solveはcandidate stateへ書き込み、certificate判定で成功したものだけをcommitする。

#### 12.3 Persistent Buffer一覧

統合runtimeで常駐させるbufferは以下とする。

| Buffer名 | 粒度 | 読み取りpass | 書き込みpass | 内容 |
|---|---:|---|---|---|
| `StrandMicrotypeBuffer` | strand | certificate/decode/repair | asset build/edit re-encode | 圧縮strand表現 |
| `MicrotypePayloadBuffer` | variable | decode/partial decode | asset build/edit re-encode | 可変長payload |
| `StrandStateBufferA/B` | strand | risk/mode/render | commit | position, velocity, flags |
| `CertificateBuffer` | strand | risk/mode/log | certificate/repair/island | residualとbitmask |
| `RiskBuffer` | strand | mode/compaction/log | risk estimation | failure probability等 |
| `ModeDecisionBuffer` | strand | decode/island/repair/render | mode allocation | 選択modeと理由 |
| `IslandTaskBuffer` | island | island solve/log | island build | island単位task |
| `IslandMemberBuffer` | variable | island solve | island build | islandのmember strandId |
| `RepairTaskBuffer` | task | repair solve/log | repair build | segment単位repair task |
| `DecodedPointBuffer` | point | repair/render | decode/island/repair | render/solver用点列 |
| `RenderVertexBuffer` | vertex | draw | render compose | 描画頂点 |
| `RuntimeCounterBuffer` | scalar/array | indirect/log | all passes | append数、pass統計 |
| `EvaluationRingBuffer` | frame | delayed readback | log pass | 評価統計 |

`StrandStateBufferA/B` はping-pongする。前フレームの状態を読みながら同じbufferへ書かない。

#### 12.4 ID体系

ID体系は以下で統一する。

```cpp
using StrandId = uint32_t;     // asset変換後に固定されるstrand ID
using PointId  = uint32_t;     // global point index
using SegmentId = uint16_t;    // strand内segment index
using IslandId = uint32_t;     // frame-local island ID
using RepairTaskId = uint32_t; // frame-local repair task ID
```

必須規則：

```txt
1. StrandIdはasset load後に変化させない。
2. islandIdとrepairTaskIdはframe-localでよい。
3. ログにはstrandIdを出すが、全strandを毎frame出さない。
4. debug時はsampled strandId listを固定seedで選び、比較可能性を保つ。
5. guide hairとrender hairが分かれる場合でも、評価対象はpersistent strandIdで記録する。
```

#### 12.5 Certificate Bitmask共通定義

各moduleのcertificateはbitmaskを共有する。

```cpp
enum CertificateBits : uint32_t
{
    CERT_OK                    = 0,
    CERT_DECODE_ERROR          = 1 << 0,
    CERT_LENGTH_RESIDUAL       = 1 << 1,
    CERT_BEND_RESIDUAL         = 1 << 2,
    CERT_TWIST_RESIDUAL        = 1 << 3,
    CERT_PENETRATION           = 1 << 4,
    CERT_VELOCITY_SPIKE        = 1 << 5,
    CERT_EDIT_STALE            = 1 << 6,
    CERT_ISLAND_REJECTED       = 1 << 7,
    CERT_REPAIR_FAILED         = 1 << 8,
    CERT_BUDGET_FORCED_DOWN    = 1 << 9,
    CERT_FLOAT_FALLBACK        = 1 << 10,
    CERT_NUMERIC_NAN_INF       = 1 << 11
};
```

`CERT_OK` はbitが0であることを意味する。複数bitが同時に立つ場合は、優先度の高いbitから処理する。

優先度：

```txt
1. CERT_NUMERIC_NAN_INF
2. CERT_PENETRATION
3. CERT_LENGTH_RESIDUAL
4. CERT_BEND_RESIDUAL
5. CERT_TWIST_RESIDUAL
6. CERT_DECODE_ERROR
7. CERT_EDIT_STALE
8. CERT_ISLAND_REJECTED
9. CERT_REPAIR_FAILED
10. CERT_BUDGET_FORCED_DOWN
11. CERT_FLOAT_FALLBACK
```

#### 12.6 State Transition

strand状態は以下の状態機械で管理する。

```txt
Compressed
  -> DecodedCandidate
  -> PhysicsCandidate
  -> RepairCandidate
  -> Accepted
  -> Rendered

Compressed
  -> CertificateFailed
  -> PhysicsIslandQueued
  -> IslandRejected
  -> RepairQueued
  -> RepairRejected
  -> FloatFallback
  -> Rendered
```

状態遷移規則：

```txt
1. CompressedからRenderedへ直接進む場合でもcertificateを通す。
2. CertificateFailedのstrandはSkip不可とする。
3. IslandRejectedはRepairQueuedへ送る。ただしpenetrationが多strand絡みなら次フレームもIsland候補に残す。
4. RepairRejectedはFloatFallbackへ送る。ただしbudget不足のみが原因なら次フレーム継続taskにしてよい。
5. FloatFallbackは永久状態ではない。Nフレーム連続でcertificateが改善したらCompressedへ戻す。
```

#### 12.7 RuntimeSettings詳細

統合設定は以下のカテゴリに分ける。

```cpp
struct RuntimeSettings
{
    uint32_t targetFps;              // 60を標準値とする
    float frameBudgetMs;             // 16.67ms等
    float gpuHairBudgetMs;           // hair runtimeへ割り当てるGPU時間
    float memoryBudgetMB;
    float bandwidthBudgetMB;

    float decodeErrorThreshold;
    float lengthResidualThreshold;
    float bendResidualThreshold;
    float twistResidualThreshold;
    float penetrationThreshold;
    float velocitySpikeThreshold;

    float contactRadius;
    float islandMergeRadius;
    uint32_t maxIslandCount;
    uint32_t maxIslandMembers;
    uint32_t maxRepairTasks;

    uint32_t modeMinLifetimeFrames;
    float modeHysteresisMargin;
    float riskPromotionThreshold;
    float riskDemotionThreshold;

    uint32_t repairMaxIterations;
    uint32_t repairMinIterations;
    float repairCommitThreshold;

    bool enableMicrotype;
    bool enableCBMA;
    bool enableIslands;
    bool enableRodRepair;
    bool enableGpuResident;
    bool enableDebugReadback;
};
```

初期値はsceneごとに調整してよいが、評価では必ずログへ保存する。

#### 12.8 Pass間の依存関係

```txt
EvaluateMicrotypeCertificates
  reads:  StrandMicrotypeBuffer, MicrotypePayloadBuffer, StrandStateBufferA
  writes: CertificateBuffer

EstimateStrandRisk
  reads:  CertificateBuffer, CollisionScene, EditCommandBuffer, StrandStateBufferA
  writes: RiskBuffer

PredictCounterfactualOutcomes
  reads:  RiskBuffer, CertificateBuffer, RuntimeCostHistory
  writes: CounterfactualOutcomeBuffer

AllocateModesUnderBudget
  reads:  CounterfactualOutcomeBuffer, RuntimeSettings
  writes: ModeDecisionBuffer, RuntimeCounterBuffer

BuildPhysicsIslands
  reads:  ModeDecisionBuffer, RiskBuffer, CollisionScene
  writes: IslandTaskBuffer, IslandMemberBuffer, DispatchArgs

SolvePhysicsIslands
  reads:  IslandTaskBuffer, IslandMemberBuffer, StrandStateBufferA
  writes: StrandStateCandidateBuffer, CertificateBuffer

BuildRepairTasks
  reads:  CertificateBuffer, ModeDecisionBuffer, StrandStateCandidateBuffer
  writes: RepairTaskBuffer, DispatchArgs

SolveRodRepairTasks
  reads:  RepairTaskBuffer, StrandStateCandidateBuffer
  writes: StrandStateCandidateBuffer, CertificateBuffer

CommitAcceptedStates
  reads:  CandidateBuffers, CertificateBuffer
  writes: StrandStateBufferB, DecodedPointBuffer
```

#### 12.9 異常値処理

実装は、NaN、Inf、極端な速度、極端な伸長を検出した場合に描画破綻を起こさないこと。

```txt
NaN/Inf検出時:
  1. CERT_NUMERIC_NAN_INFを立てる。
  2. 該当strandを前フレームのaccepted stateへ戻す。
  3. 次フレームはFloatFallbackまたはrepair対象にする。
  4. release buildではassert停止しない。
  5. debug buildではstrandIdとpass名をring bufferへ記録する。
```

#### 12.10 実装単位の受け入れ条件

統合runtimeとして、以下を満たすこと。

```txt
1. 10万strand入力でpersistent strandIdが全passで一致する。
2. CPU同期readbackなしで通常frameを進められる。
3. certificate failure strandがSkipされない。
4. island/repiar/fallbackのいずれかで失敗が明示処理される。
5. evaluation logにmode count、certificate count、island count、repair count、fallback countが出る。
6. ablation flagで各moduleを無効化できる。
7. 同じseed、同じasset、同じsettingsで再現可能なログが得られる。
8. GPU buffer overflow時はsilent corruptionではなく、明示的にoverflow bitとcounterを記録する。
```


---


## 第3部 共通評価指標・比較対象


### 共通評価指標・比較対象設計書

このMarkdownは、5つの新規性すべてに共通する評価指標と比較対象を定義する。  
各個別設計書では評価指標・比較対象を重複して書かず、本ファイルを参照する。

対象となる5つの新規性は以下である。

1. Physics-Certified Strand Microtype
2. Counterfactual Budgeted Mode Allocation
3. Self-Materializing Strand Physics Islands
4. Certificate-Driven Rod Repair Calculus
5. GPU-Resident Strand Runtime

比較対象は、既存論文ではなく、**今のプロジェクトの現行実装**を主基準とする。  
必要に応じて、現行実装内で機能を無効化したアブレーション条件も比較対象に含める。

---


### 0. 評価方針

この評価設計書の目的は、単に「速くなった」「メモリが減った」を示すことではない。  
5つの新規性が、**certificateを中心にしたstrand単位のruntime制御**として有効かを検証する。

評価では、以下の3区分を必ず分ける。

1. **現行プロジェクトとの差分**  
   現行実装に対して、提案統合システムがどの程度改善したかを測る。

2. **単なるリファクタとの差分**  
   コード整理や共通化だけで改善したのではないことを、`Baseline_RefactoredOnly` で分ける。

3. **5要素それぞれの寄与**  
   `Ablation_NoMicrotype` から `Ablation_NoGpuResident` までを使い、どの新規性がどの指標に効いたかを示す。

#### 0.1 論文で検証する仮説

```txt
H1: Microtypeにより、品質劣化を許容範囲に抑えながらVRAM使用量とGPU帯域を削減できる。
H2: Counterfactual Budgeted Mode Allocationにより、固定LODよりも同じframe budget下で破綻率を下げられる。
H3: Self-Materializing Strand Physics Islandsにより、全体solverを使わず接触・編集領域の破綻を局所的に減らせる。
H4: Certificate-Driven Rod Repairにより、full rod solverを全体へ適用せず局所破綻を修復できる。
H5: GPU-Resident Runtimeにより、CPU readback由来のp99 frame time悪化を抑えられる。
H6: 5要素を統合した場合、単体改善の合計ではなく、certificateを介した閉ループとして相乗効果が出る。
```

#### 0.2 評価で必須の実験セット

実装の提案手法として評価するため、以下は必須とする。

```txt
Required Experiment Set:
1. Baseline_CurrentProject
2. Baseline_RefactoredOnly
3. Proposed_FullSystem
4. Ablation_NoMicrotype
5. Ablation_NoCBMA
6. Ablation_NoIslands
7. Ablation_NoRodRepair
8. Ablation_NoGpuResident
```

各実験は、少なくとも以下の3シナリオで行う。

```txt
Required Scenarios:
1. Static Groom Scenario
2. Fast Head Motion Scenario
3. Contact-Heavy Scenario
```

Local Editing、Dense 100K、Production Editingは、評価の補助シナリオとして追加し、提案手法が局所編集・高密度・制作操作でも破綻しにくいことを確認する。

#### 0.3 結果表で必ず見る主指標

主指標は以下の5つに絞る。  
表が増えすぎる場合でも、この5つは必ず本文で議論する。

```txt
Primary Metrics:
1. p50 / p95 / p99 frame time
2. VRAM usage
3. max / average penetration depth
4. certificate failure rate
5. edit-to-stable latency
```

補助指標として、decode error、mode switch count、island count、repair success rate、CPU readback countを使う。

---

### 1. 比較対象

#### 1.1 Baseline A: Current Project Full Runtime

今のプロジェクトの現行実装をそのまま実行したもの。

目的：

- 提案5要素を導入する前の基準性能を得る
- 現行プロジェクトのframe time、VRAM、品質、破綻量を測る
- すべての改善率の基準値にする

測定対象：

- 現在のHairAsset表現
- 現在のruntime処理
- 現在のcollision / sampling / lod / metrics
- 現在のGPU/CPU処理フロー

記録名：

```txt
Baseline_CurrentProject
```

---

#### 1.2 Baseline B: Current Project + Existing Refactor Only

これまで行った数式・補間・I/O共通化だけを含み、新規性5要素は入れない比較条件。

目的：

- 単なるコード整理による差分と、研究的な新規性による差分を分ける
- refactorによる副作用がないか確認する

記録名：

```txt
Baseline_RefactoredOnly
```

---

#### 1.3 Ablation 1: Without Microtype

Physics-Certified Strand Microtype を無効化し、現行のfloat系strand bufferを使う。

記録名：

```txt
Ablation_NoMicrotype
```

目的：

- microtypeによるメモリ削減・帯域削減・frame time改善を測る
- 圧縮による品質劣化を切り分ける

---

#### 1.4 Ablation 2: Without Counterfactual Allocation

Counterfactual Budgeted Mode Allocation を無効化し、現行のheuristic selectorまたは固定LODで処理する。

記録名：

```txt
Ablation_NoCBMA
```

目的：

- 反実仮想mode割当の効果を測る
- 同じ予算下でmode選択の品質差を測る

---

#### 1.5 Ablation 3: Without Physics Islands

Self-Materializing Strand Physics Islands を無効化し、現行のglobal処理または単純active setで処理する。

記録名：

```txt
Ablation_NoIslands
```

目的：

- island生成による接触品質改善を測る
- 局所物理化によるframe time削減を測る

---

#### 1.6 Ablation 4: Without Rod Repair

Certificate-Driven Rod Repair Calculus を無効化し、破綻strandを修復しない、または単純projectionのみで処理する。

記録名：

```txt
Ablation_NoRodRepair
```

目的：

- certificate付き修復の効果を測る
- repair failure detection の必要性を測る

---

#### 1.7 Ablation 5: Without GPU-Resident Runtime

GPU常駐制御を無効化し、CPU側でmode decisionやtask generationを行う。

記録名：

```txt
Ablation_NoGpuResident
```

目的：

- CPU readback削減効果を測る
- GPU-side compaction / indirect dispatch の効果を測る

---

#### 1.8 Proposed Full System

5つの新規性をすべて有効化した統合条件。

記録名：

```txt
Proposed_FullSystem
```

目的：

- 統合時の統合性能を測る
- 制作現場で意味のある「革命性」を示す
- 単体ではなく閉ループとしての効果を測る

---

### 2. 共通評価シナリオ

すべての評価は、以下のシナリオで実行する。

#### 2.1 Static Groom Scenario

髪型を固定し、頭部運動も編集も加えない。

目的：

- baselineのメモリ・decode・render costを見る
- microtype単体の圧縮誤差を見る
- temporal instabilityがない状態での基準値を取る

---

#### 2.2 Fast Head Motion Scenario

頭部を高速に回転・移動させる。

目的：

- dynamic residual
- mode allocation
- physics island
- rod repair

の安定性を見る。

---

#### 2.3 Local Editing Scenario

一部の髪束またはstrand群に局所編集を加える。

目的：

- edit locality
- local update latency
- edit後の破綻量
- repairの必要性
- microtype update cost

を見る。

---

#### 2.4 Contact-Heavy Scenario

髪が肩、首、手、アクセサリ、衣服、またはscalp周辺と強く接触する状況。

目的：

- penetration depth
- island生成効果
- rod repair効果
- certificate failure rate

を見る。

---

#### 2.5 Dense 100K Scenario

100,000 strands、可能なら各strand 64〜128 segment程度で実行する。

目的：

- 60FPS制約
- total VRAM
- bandwidth
- GPU dispatch overhead

を見る。

---

#### 2.6 Production Editing Scenario

制作現場を想定し、以下を同時に行う。

- 10万本表示
- 局所編集
- 頭部運動
- 接触
- camera movement
- LOD変動

目的：

- 統合システムとしての現場有用性を測る
- 全新規性の相互補完を示す

---

### 3. 共通評価指標

#### 3.1 性能指標

| 指標 | 単位 | 説明 |
|---|---:|---|
| average frame time | ms | 平均フレーム時間 |
| p50 frame time | ms | 中央値 |
| p95 frame time | ms | 95 percentile |
| p99 frame time | ms | 99 percentile |
| worst frame time | ms | 最大フレーム時間 |
| FPS | frames/sec | 実測FPS |
| 60FPS pass rate | % | 16.67ms以内だったフレーム割合 |
| GPU time | ms | GPU処理時間 |
| CPU time | ms | CPU処理時間 |
| dispatch count | count/frame | GPU dispatch数 |
| indirect dispatch count | count/frame | indirect dispatch数 |
| compaction time | ms | GPU compaction時間 |
| synchronization stalls | count/frame | CPU-GPU同期回数 |

合格基準例：

```txt
p95 frame time <= 16.67ms
60FPS pass rate >= 95%
CPU readback per frame <= debug時以外0 bytes
```

---

#### 3.2 メモリ・帯域指標

| 指標 | 単位 | 説明 |
|---|---:|---|
| bytes per strand | bytes | strand 1本あたりの総データ量 |
| total strand memory | MB | strand全体のVRAM |
| dynamic state memory | MB | 動的状態のVRAM |
| certificate memory | MB | certificate関連メモリ |
| island task memory | MB | island task buffer |
| repair task memory | MB | repair task buffer |
| bandwidth per frame | MB/frame | 1フレームあたり読み書き量 |
| compression ratio | x | float32基準の圧縮率 |
| decode bandwidth | MB/s | microtype decode時の帯域 |

合格基準例：

```txt
bytes per strand <= current project の 25%
total strand memory <= current project の 35%
visual errorが許容範囲内であること
```

---

#### 3.3 品質・物理指標

| 指標 | 単位 | 説明 |
|---|---:|---|
| visual error | normalized error | baselineまたは高品質参照との差 |
| root error | cm | root位置誤差 |
| tip error | cm | tip位置誤差 |
| length residual | ratio | rest lengthからのずれ |
| bend residual | ratio | rest bendからのずれ |
| twist residual | ratio | rest twistからのずれ |
| penetration depth | cm | 最大貫通深さ |
| penetration count | count | 貫通strand数 |
| collision predicate error | % | 圧縮表現上の衝突判定誤差 |
| temporal popping | score | フレーム間の急変量 |
| jitter | score | 低振幅の揺れ誤差 |

合格基準例：

```txt
length residual <= threshold
bend residual <= threshold
penetration depth <= current project より低い
temporal popping <= current project と同等以下
```

---

#### 3.4 Certificate指標

| 指標 | 単位 | 説明 |
|---|---:|---|
| certificate value | scalar | 総合証明値 |
| certificate pass rate | % | C <= 1 の割合 |
| certificate fail rate | % | C > 1 の割合 |
| false safe rate | % | 安全と判定したが実際には破綻した割合 |
| false unsafe rate | % | 危険と判定したが実際には安全だった割合 |
| repair success rate | % | repair後にC<=1になった割合 |
| repair fallback rate | % | repair失敗でisland等へ戻した割合 |

最重要指標：

```txt
false safe rate
```

これは、「壊れているのに安全と判断する」最悪ケースなので、低く抑える必要がある。

---

#### 3.5 Mode Allocation指標

| 指標 | 単位 | 説明 |
|---|---:|---|
| mode distribution | % | QS/Island/Repair等の割合 |
| mode switching count | count/frame | mode変更回数 |
| mode switching cost | ms | mode変更によるコスト |
| predicted error vs actual error | correlation / RMSE | 予測誤差の精度 |
| predicted cost vs actual cost | correlation / RMSE | コスト予測精度 |
| failure prediction AUC | AUC | 破綻予測精度 |
| quality per millisecond | score/ms | 品質対時間効率 |
| allocation optimality gap | % | brute forceまたはoracle参照との差 |

---

#### 3.6 Island指標

| 指標 | 単位 | 説明 |
|---|---:|---|
| island count | count/frame | island数 |
| average island size | strands | 平均member数 |
| max island size | strands | 最大member数 |
| island lifetime | frames | island継続時間 |
| merge count | count/frame | merge回数 |
| split count | count/frame | split回数 |
| island build time | ms | island構築時間 |
| island solve time | ms | island solver時間 |
| island commit success rate | % | island結果を採用できた割合 |
| boundary discontinuity | cm or score | island境界の不連続性 |

---

#### 3.7 制作現場向け指標

| 指標 | 単位 | 説明 |
|---|---:|---|
| edit latency | ms | 編集入力から反映までの遅延 |
| local edit preservation | score | 編集した形状が保持された度合い |
| manual correction count | count | 手作業修正が必要だった箇所 |
| artist-visible popping | subjective score | 視覚的な破綻 |
| stable editing duration | sec | 破綻なく編集できた時間 |
| interactive frame stability | % | 編集中に60FPSを維持できた割合 |

---

### 4. 結果表テンプレート

#### 4.1 性能結果表

| Method | avg ms | p95 ms | p99 ms | worst ms | 60FPS pass | GPU ms | CPU ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| Baseline_CurrentProject | | | | | | | |
| Baseline_RefactoredOnly | | | | | | | |
| Proposed_FullSystem | | | | | | | |

---

#### 4.2 メモリ結果表

| Method | bytes/strand | total VRAM | compression ratio | bandwidth/frame |
|---|---:|---:|---:|---:|
| Baseline_CurrentProject | | | | |
| Ablation_NoMicrotype | | | | |
| Proposed_FullSystem | | | | |

---

#### 4.3 品質結果表

| Method | visual error | length residual | bend residual | twist residual | penetration depth | certificate pass |
|---|---:|---:|---:|---:|---:|---:|
| Baseline_CurrentProject | | | | | | |
| Proposed_FullSystem | | | | | | |

---

#### 4.4 アブレーション結果表

| Method | p95 ms | VRAM | visual error | penetration | certificate pass | edit latency |
|---|---:|---:|---:|---:|---:|---:|
| Proposed_FullSystem | | | | | | |
| Ablation_NoMicrotype | | | | | | |
| Ablation_NoCBMA | | | | | | |
| Ablation_NoIslands | | | | | | |
| Ablation_NoRodRepair | | | | | | |
| Ablation_NoGpuResident | | | | | | |

---

### 5. 結果と考察の書き方ルール

#### 5.1 結果章で書くこと

結果章では、数値を中心に書く。

例：

```txt
Proposed_FullSystem は Baseline_CurrentProject と比較して bytes/strand を XX% 削減した。
p95 frame time は XX ms であり、60FPS制約である16.67msを下回った。
Ablation_NoMicrotype ではVRAM使用量が増加し、bandwidth/frameも増加した。
```

#### 5.2 考察章で書くこと

考察章では、なぜその結果になったかを書く。

例：

```txt
Microtypeによりstrand位置列を直接保持しないため、VRAM使用量と帯域が減少した。
一方で、高曲率strandではresidual packetが不足し、decode errorが増加した。
このため、今後は曲率に応じたadaptive bit allocationが必要である。
```

---

### 6. 実験ログ形式

実装時は、各フレームで以下をCSVまたはJSONLに記録する。

```json
{
  "frame": 120,
  "method": "Proposed_FullSystem",
  "scenario": "ContactHeavy",
  "strandCount": 100000,
  "avgFrameMs": 14.2,
  "gpuMs": 12.8,
  "cpuMs": 1.1,
  "vramMB": 210.5,
  "bytesPerStrand": 96,
  "penetrationDepth": 0.31,
  "lengthResidual": 0.018,
  "bendResidual": 0.044,
  "certificatePassRate": 0.982,
  "islandCount": 18,
  "repairTaskCount": 420,
  "modeQS": 0.71,
  "modeIsland": 0.06,
  "modeRepair": 0.01,
  "modeSkip": 0.22
}
```

---

### 7. 最重要主張

本評価の目的は、以下を示すことである。

```txt
今のプロジェクトの現行実装と比較して、
提案5要素を統合したシステムは、
メモリ使用量、frame time、接触破綻、編集遅延を同時に改善する。

特に、Microtypeはメモリと帯域を削減し、
CBMAは限られた時間予算を必要なstrandへ割り当て、
Physics Islandsは局所接触を解決し、
Rod Repairは残った破綻をcertificate付きで修復し、
GPU-Resident RuntimeはこれらをCPU readbackなしで閉ループ化する。
```


---

### 8. 推奨ログスキーマ

実装では、各frameごとに以下のJSONL形式でログを出す。  
論文の結果表はこのログから自動生成できるようにする。

```json
{
  "run_id": "Proposed_FullSystem_ContactHeavy_001",
  "frame": 120,
  "scenario": "ContactHeavy",
  "variant": "Proposed_FullSystem",
  "strand_count": 100000,
  "frame_time_ms": 15.82,
  "gpu_time_ms": 14.91,
  "cpu_time_ms": 0.74,
  "p99_window_frame_time_ms": 16.41,
  "vram_mb": 742.3,
  "bandwidth_mb": 118.5,
  "decode_error_avg": 0.0018,
  "penetration_depth_avg": 0.0009,
  "penetration_depth_max": 0.0081,
  "certificate_failure_rate": 0.013,
  "mode_counts": {
    "skip": 42000,
    "qs_only": 39000,
    "dynamic_residual": 14000,
    "physics_island": 3200,
    "rod_repair": 1600,
    "float_fallback": 200
  },
  "island_count": 42,
  "repair_task_count": 1600,
  "repair_success_rate": 0.91,
  "cpu_readback_count": 0,
  "edit_to_stable_latency_ms": null
}
```

#### 8.1 ログ実装ルール

```txt
1. debug buildでは毎frameログを取る。
2. release評価ではN frameごと、またはring bufferからまとめて書き出す。
3. CPU readbackが発生する場合は必ずcountする。
4. certificate failureはstrand単位とsegment単位を分けて集計する。
5. frame timeは平均ではなくp95/p99を重視する。
```

---

### 9. 論文での結果記述テンプレート

```txt
[結果]
Proposed_FullSystemはBaseline_CurrentProjectに対して、p99 frame timeをX%、VRAM使用量をY%、certificate failure rateをZ%改善した。
また、Ablation_NoCBMAでは同じframe budget下でfailure rateが増加したため、単なる高速化ではなくmode allocationが品質維持に寄与していることが分かる。

[考察]
Microtypeは主にメモリ・帯域へ寄与し、CBMAは予算超過と破綻率のバランスへ寄与した。
Physics IslandsとRod RepairはContact-Heavy Scenarioで特に効果が大きく、GPU-Resident Runtimeはp99 frame timeとCPU readbackの削減に寄与した。
```

---

### 10. 詳細評価定義

この章では、評価を再現可能にするため、scene条件、ログ項目、集計方法、表の作り方を具体的に定義する。

#### 10.1 評価で固定する条件

各実験では、以下を固定する。

```txt
Asset:
  strandCount
  pointCount distribution
  material設定
  root binding設定

Runtime Settings:
  frameBudgetMs
  gpuHairBudgetMs
  certificate thresholds
  risk weights
  maxIslandCount
  maxRepairTasks

Scene:
  camera path
  head motion path
  collision object path
  edit command script

Measurement:
  warmup frame数
  measurement frame数
  random seed
  GPU timestamp availability
```

評価表には、少なくとも `strandCount`、`avgPointPerStrand`、`measurementFrames`、`GPU名`、`resolution` を書く。

#### 10.2 Warmupと計測区間

```txt
Warmup:
  最初の120 frameは計測から除外する。

Measurement:
  その後の600 frameを集計する。

Exception:
  卒業研究の実装環境で600 frameが重すぎる場合は300 frameでもよいが、全条件で同じframe数にする。
```

warmupを入れる理由は、shader compile、buffer初期化、mode履歴、island履歴の影響を除くためである。

#### 10.3 シナリオスクリプト

##### 10.3.1 Static Groom Scenario

```txt
目的:
  Microtypeの帯域削減とdecode品質を測る。

条件:
  head motionなし
  collisionなし
  editなし
  cameraは固定または低速移動

見る指標:
  VRAM MB
  bandwidth MB/frame
  decode error
  certificate failure rate
  render time
```

##### 10.3.2 Fast Head Motion Scenario

```txt
目的:
  dynamic residual、mode allocation、p99 frame timeを測る。

条件:
  root transformに高速回転を入れる
  collisionは最小限
  editなし

見る指標:
  p95/p99 frame time
  velocity spike count
  mode switching count
  fallback count
```

##### 10.3.3 Contact-Heavy Scenario

```txt
目的:
  Physics IslandとRod Repairの有効性を測る。

条件:
  球またはカプセルcollisionを髪に近づける
  接触領域が局所的に移動する

見る指標:
  penetration depth
  island commit/reject rate
  repair success rate
  certificate failure rate
```

##### 10.3.4 Local Editing Scenario

```txt
目的:
  editBits、local re-encode、fallback制御を測る。

条件:
  brush editを一定範囲に適用する
  編集半径と強度をログに記録する

見る指標:
  edit affected strand count
  local re-encode count
  edit stale frames
  popping score
```

##### 10.3.5 Dense 100K Scenario

```txt
目的:
  10万strand規模でのmemory、bandwidth、budget制御を測る。

条件:
  strandCountを100000付近に設定する
  point数分布を記録する

見る指標:
  VRAM MB
  p99 frame time
  buffer overflow count
  mode distribution
```

#### 10.4 指標の定義式

##### 10.4.1 Certificate Failure Rate

```txt
certificateFailureRate = failedStrandCount / strandCount
```

`failedStrandCount` は、`certificateBits != 0` のstrand数である。ただし、`CERT_FLOAT_FALLBACK` だけのstrandを含めるかどうかは、表の脚注で明記する。

##### 10.4.2 Repair Success Rate

```txt
repairSuccessRate = repairCommittedCount / max(1, repairTaskCount)
```

deferred taskは成功に含めない。

##### 10.4.3 Island Commit Rate

```txt
islandCommitRate = islandCommitCount / max(1, islandCount)
```

巨大化によって分割されたislandは、分割後のisland数で数える。

##### 10.4.4 Popping Score

popping scoreは、フレーム間の急な位置変化を正規化して測る。

```txt
poppingScore = mean_over_visible_points( max(0, displacement - expectedMotion) )
```

`expectedMotion` はroot motionと前フレーム速度から推定する。

##### 10.4.5 Prediction Error

CBMAのcost予測精度を測る。

```txt
costPredictionError = abs(predictedCostMs - actualCostMs) / max(actualCostMs, epsilon)
```

modeごとに平均とp95を出す。

#### 10.5 JSONLログの詳細schema

1 frameにつき1行を基本とする。

```json
{
  "run_id": "string",
  "scenario": "ContactHeavy",
  "condition": "Proposed_FullSystem",
  "frame": 123,
  "strand_count": 100000,
  "avg_points_per_strand": 16.2,
  "gpu_hair_time_ms": 6.41,
  "frame_time_ms": 14.9,
  "vram_mb": 820.5,
  "bandwidth_mb_frame": 310.2,
  "certificate_failure_count": 231,
  "certificate_bits_count": {
    "decode": 4,
    "length": 80,
    "bend": 91,
    "twist": 33,
    "penetration": 60
  },
  "mode_count": {
    "skip": 52000,
    "qs_only": 41000,
    "dynamic_residual": 5400,
    "physics_island": 980,
    "rod_repair": 430,
    "float_fallback": 190
  },
  "island_count": 22,
  "island_commit_count": 18,
  "island_reject_count": 4,
  "repair_task_count": 430,
  "repair_committed_count": 360,
  "repair_rejected_count": 50,
  "repair_deferred_count": 20,
  "fallback_count": 190,
  "overflow_flags": 0,
  "settings_hash": "string"
}
```

per-strand詳細ログは別ファイルにsampled subsetとして出す。

```json
{
  "run_id": "string",
  "frame": 123,
  "strand_id": 4812,
  "selected_mode": "RodRepair",
  "previous_mode": "PhysicsIsland",
  "risk": 0.81,
  "certificate_bits": 20,
  "reason_bits": 12,
  "predicted_cost_ms": 0.0021,
  "actual_cost_ms_estimate": 0.0025,
  "residual_before": {
    "length": 0.04,
    "bend": 0.12,
    "twist": 0.03,
    "penetration": 0.02
  },
  "residual_after": {
    "length": 0.01,
    "bend": 0.04,
    "twist": 0.02,
    "penetration": 0.0
  }
}
```

#### 10.6 比較表の最小構成

論文本文には、以下の表を入れる。

##### 10.6.1 Performance Table

| Condition | p50 ms | p95 ms | p99 ms | GPU hair ms | readback count |
|---|---:|---:|---:|---:|---:|
| Baseline_CurrentProject | | | | | |
| Baseline_RefactoredOnly | | | | | |
| Proposed_FullSystem | | | | | |

##### 10.6.2 Quality Table

| Condition | certificate fail % | penetration avg | penetration p95 | popping score | fallback count |
|---|---:|---:|---:|---:|---:|
| Baseline_CurrentProject | | | | | |
| Proposed_FullSystem | | | | | |
| Ablation_NoIslands | | | | | |
| Ablation_NoRodRepair | | | | | |

##### 10.6.3 Ablation Table

| Condition | p99 ms | fail % | island commit % | repair success % | interpretation |
|---|---:|---:|---:|---:|---|
| Proposed_FullSystem | | | | | |
| Ablation_NoMicrotype | | | | | |
| Ablation_NoCBMA | | | | | |
| Ablation_NoIslands | | | | | |
| Ablation_NoRodRepair | | | | | |
| Ablation_NoGpuResident | | | | | |

#### 10.7 成功判定の書き方

結果が理想通りでなくても、以下の観点で整理する。

```txt
1. どのmoduleがどの指標を改善したか。
2. どのscenarioで効果が大きいか。
3. どのscenarioでは効果が小さいか。
4. p99悪化がある場合、どのpassが原因か。
5. certificate failureが減ったのか、fallbackで隠しただけなのか。
6. mode allocationが危険strandを保護できたか。
7. islandとrepairの役割分担が成立したか。
```

#### 10.8 評価時に禁止する比較

```txt
1. 提案手法だけ軽いassetで測り、baselineを重いassetで測ること。
2. p50だけを示してp99を隠すこと。
3. certificate failureを無視して見た目だけで成功とすること。
4. fallback数が増えたのに品質改善としてだけ主張すること。
5. ablationでログschemaを変えること。
```


---