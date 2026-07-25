# 6.4 比較対象

本研究では、提案手法の有効性を確認するために、現行プロジェクトを基準とした複数の比較対象を設定する。本研究の目的は、既存プロジェクトに対して、データ表現、計算割当、局所物理化、局所修復、GPU常駐制御を追加した場合に、メモリ使用量、フレーム時間、物理品質、編集応答性がどのように変化するかを評価することである。そのため、比較対象は外部実装ではなく、同一プロジェクト内で機能を切り替えた複数の構成として定義する。

比較対象を同一プロジェクト内に統一する理由は、実装環境、髪アセット、GPU処理、描画条件、ログ出力形式を揃えるためである。異なる研究実装や外部ソフトウェアを直接比較すると、実装の最適化度、データ形式、レンダリング条件、計測方法が異なり、提案手法の効果だけを切り分けることが難しい。そこで本研究では、現行プロジェクトを基準として、各新規性を有効化または無効化したアブレーション構成を用意し、各要素の寄与を測定する。

## 6.4.1 Baseline_CurrentProject

`Baseline_CurrentProject` は、現行プロジェクトの実装をそのまま実行した構成である。この構成では、提案する五つの新規性、すなわち Physics-Certified Strand Microtype、Counterfactual Budgeted Mode Allocation、Self-Materializing Strand Physics Islands、Certificate-Driven Rod Repair Calculus、GPU-Resident Strand Runtime を導入しない。現行プロジェクトが持つ髪アセット処理、補間、衝突、LOD、メトリクス、I/Oなどをそのまま用いる。

この比較対象は、本研究における最も基本的な基準である。すべての改善率は、この構成と比較して算出する。特に、メモリ使用量、bytes per strand、平均フレーム時間、p95フレーム時間、p99フレーム時間、貫通量、編集遅延を記録し、提案手法によってどの程度改善したかを確認する。

記録名は以下とする。

```txt
Baseline_CurrentProject
```

## 6.4.2 Baseline_RefactoredOnly

`Baseline_RefactoredOnly` は、現行プロジェクトに対して、これまで行った数式処理、補間処理、I/O処理、幾何処理の共通化のみを適用した構成である。ただし、本研究で提案する五つの新規性は導入しない。この構成を用いることで、単なるコード整理による効果と、本研究の新規性による効果を分離する。

本研究では、既存コードの重複を減らし、補間、SDF、Scalp法線、パース処理、統計処理、OBJ出力などを共通化している。しかし、これらの共通化は主に保守性や可読性を高めるものであり、研究上の主要な提案ではない。そのため、提案手法の評価では、この構成を中間的な基準として用いる。

記録名は以下とする。

```txt
Baseline_RefactoredOnly
```

## 6.4.3 Ablation_NoMicrotype

`Ablation_NoMicrotype` は、Physics-Certified Strand Microtype を無効化した構成である。この構成では、現行プロジェクトの通常のstrand表現、すなわちfloat系の点列または既存のstrand bufferを使用する。他の提案要素は可能な限り有効にし、microtypeを使わないことによる影響を測定する。

この比較により、microtypeがメモリ使用量、GPU帯域、decode処理、certificate評価にどの程度寄与しているかを確認する。特に、bytes per strand、total VRAM、bandwidth per frame、frame time、visual error、length residual、bend residual、twist residualを比較する。

この構成でメモリ使用量や帯域が大きく増加し、Proposed_FullSystemでそれらが削減される場合、Physics-Certified Strand Microtypeがデータ量削減に有効であると判断できる。一方で、Proposed_FullSystemで品質劣化が大きい場合は、microtypeの圧縮誤差が問題である可能性を検討する。

記録名は以下とする。

```txt
Ablation_NoMicrotype
```

## 6.4.4 Ablation_NoCBMA

`Ablation_NoCBMA` は、Counterfactual Budgeted Mode Allocation を無効化した構成である。この構成では、計算モードの選択を、現行プロジェクトのheuristic、距離ベースLOD、単純なrisk閾値、または固定ルールによって行う。他の提案要素は可能な限り有効にする。

この比較により、反実仮想的なmode allocationが、限られたフレーム予算内でどの程度有効に計算資源を配分できるかを評価する。特に、p95 frame time、p99 frame time、60FPS pass rate、mode distribution、predicted errorとactual errorの差、failure prediction、penetration depth、certificate fail rateを比較する。

この構成でframe timeのばらつきや破綻量が増加し、Proposed_FullSystemでそれらが減少する場合、Counterfactual Budgeted Mode Allocationが計算予算制御と品質維持に有効であると判断できる。

記録名は以下とする。

```txt
Ablation_NoCBMA
```

## 6.4.5 Ablation_NoIslands

`Ablation_NoIslands` は、Self-Materializing Strand Physics Islands を無効化した構成である。この構成では、危険なstrand集合に対する局所的なphysics island生成を行わない。接触や編集に対する処理は、現行プロジェクトの衝突処理、単純active set、またはglobal処理に任せる。

この比較により、physics islandが接触破綻や局所的な物理品質にどの程度寄与しているかを評価する。特に、penetration depth、penetration count、islandなしでのcontact failure、boundary discontinuity、frame time、repair task countを比較する。

この構成で接触破綻が増加し、Proposed_FullSystemでpenetration depthやpenetration countが低下する場合、Self-Materializing Strand Physics Islandsが局所接触処理に有効であると判断できる。

記録名は以下とする。

```txt
Ablation_NoIslands
```

## 6.4.6 Ablation_NoRodRepair

`Ablation_NoRodRepair` は、Certificate-Driven Rod Repair Calculus を無効化した構成である。この構成では、破綻したstrandまたは局所segment帯に対して、certificateに基づくrod repairを行わない。必要に応じて、現行プロジェクトの単純projection、clamp、または未修復状態を用いる。

この比較により、rod repairが局所的な長さ、曲げ、ねじれ、貫通残差の改善にどの程度寄与しているかを評価する。特に、length residual、bend residual、twist residual、penetration residual、repair success rate、certificate pass rate、visual poppingを比較する。

この構成でcertificate failureやresidualが増加し、Proposed_FullSystemでそれらが低下する場合、Certificate-Driven Rod Repair Calculusが局所破綻の修復に有効であると判断できる。

記録名は以下とする。

```txt
Ablation_NoRodRepair
```

## 6.4.7 Ablation_NoGpuResident

`Ablation_NoGpuResident` は、GPU-Resident Strand Runtime を無効化した構成である。この構成では、mode decision、island task generation、repair task generation、またはdebug統計取得などをCPU側で行う。必要に応じてGPU処理は使用するが、GPU上で閉ループ制御を完結させない。

この比較により、GPU常駐制御がCPU time、CPU-GPU同期、readback量、p99 frame timeにどの程度寄与しているかを評価する。特に、CPU time、GPU time、CPU-GPU readback bytes、synchronization stalls、dispatch overhead、p95 frame time、p99 frame timeを比較する。

この構成でCPU timeやp99 frame timeが増加し、Proposed_FullSystemでそれらが低下する場合、GPU-Resident Strand Runtimeがフレーム時間安定化に有効であると判断できる。

記録名は以下とする。

```txt
Ablation_NoGpuResident
```

## 6.4.8 Proposed_FullSystem

`Proposed_FullSystem` は、本研究で提案する五つの構成要素をすべて有効化した最終構成である。すなわち、Physics-Certified Strand Microtype、Counterfactual Budgeted Mode Allocation、Self-Materializing Strand Physics Islands、Certificate-Driven Rod Repair Calculus、GPU-Resident Strand Runtimeをすべて使用する。

この構成は、本研究の最終的な評価対象である。Baseline_CurrentProjectおよび各Ablationと比較することで、提案手法全体の効果と、各構成要素の寄与を確認する。

記録名は以下とする。

```txt
Proposed_FullSystem
```

## 6.4.9 比較対象のまとめ

比較対象を以下にまとめる。

| 記録名 | 内容 | 目的 |
|---|---|---|
| Baseline_CurrentProject | 現行プロジェクトそのまま | 基準値の取得 |
| Baseline_RefactoredOnly | 共通化済み、提案5要素なし | refactor効果の分離 |
| Ablation_NoMicrotype | microtypeなし | データ量削減効果の確認 |
| Ablation_NoCBMA | counterfactual allocationなし | mode割当効果の確認 |
| Ablation_NoIslands | physics islandsなし | 局所物理化の効果確認 |
| Ablation_NoRodRepair | rod repairなし | 証明付き修復の効果確認 |
| Ablation_NoGpuResident | GPU常駐制御なし | CPU依存削減効果の確認 |
| Proposed_FullSystem | 提案5要素すべて有効 | 最終性能の確認 |

これらの比較対象により、本研究では、提案手法全体の有効性だけでなく、各構成要素がどの指標に寄与しているかを分析する。
