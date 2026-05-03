# 6.4 比較対象 / 6.5 評価指標

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


---

# 6.5 評価指標

本研究では、提案手法の有効性を多面的に評価するため、性能、メモリ、物理品質、certificate、mode allocation、physics island、rod repair、GPU常駐制御、制作現場向けの指標を用いる。本研究の目的は単に平均FPSを向上させることではなく、十万本規模の髪を低メモリで保持し、固定60FPS制約下で局所編集、接触処理、破綻修復を安定して行うことである。そのため、評価指標も、速度だけでなく、品質、安定性、修復成功率、編集応答性を含めて定義する。

## 6.5.1 性能指標

性能指標では、実行速度とフレーム時間の安定性を評価する。リアルタイム用途では平均フレーム時間だけでなく、p95やp99のフレーム時間が重要である。制作中のプレビューでは、一部フレームのみが大きく遅延しても操作性が低下するため、フレーム時間分布を記録する。

使用する性能指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| average frame time | ms | 全フレームの平均処理時間 |
| p50 frame time | ms | フレーム時間の中央値 |
| p95 frame time | ms | 95 percentileのフレーム時間 |
| p99 frame time | ms | 99 percentileのフレーム時間 |
| worst frame time | ms | 最大フレーム時間 |
| FPS | frames/sec | 実測FPS |
| 60FPS pass rate | % | 16.67ms以内だったフレームの割合 |
| GPU time | ms | GPU処理時間 |
| CPU time | ms | CPU処理時間 |
| dispatch count | count/frame | 1フレームあたりのdispatch数 |
| indirect dispatch count | count/frame | 1フレームあたりのindirect dispatch数 |
| synchronization stalls | count/frame | CPU-GPU同期の発生回数 |

本研究では、特に以下を重要指標とする。

```txt
p95 frame time
p99 frame time
60FPS pass rate
CPU time
GPU time
synchronization stalls
```

合格基準の例として、p95 frame timeが16.67ms以下、60FPS pass rateが95%以上であることを目標とする。ただし、実験環境や髪アセットの規模によって難易度が変わるため、最終的にはBaseline_CurrentProjectに対する改善率も併せて評価する。

## 6.5.2 メモリ・帯域指標

メモリ・帯域指標では、Physics-Certified Strand Microtypeによるデータ量削減効果を評価する。十万本規模の髪では、位置情報だけでなく、速度、前フレーム位置、certificate、mode decision、island task、repair taskなどのデータも必要になる。そのため、strand単位のデータ量と総VRAM使用量を測定する。

使用するメモリ指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| bytes per strand | bytes | strand 1本あたりの総データ量 |
| total strand memory | MB | strand表現に必要な総メモリ |
| dynamic state memory | MB | 動的状態に必要なメモリ |
| certificate memory | MB | certificate関連メモリ |
| island task memory | MB | island task bufferのメモリ |
| repair task memory | MB | repair task bufferのメモリ |
| total VRAM usage | MB | 全体のVRAM使用量 |
| bandwidth per frame | MB/frame | 1フレームあたりの読み書き量 |
| compression ratio | x | 現行float系表現に対する圧縮率 |
| decode bandwidth | MB/s | microtype decode時の帯域 |

特に重要な指標は以下である。

```txt
bytes per strand
total VRAM usage
bandwidth per frame
compression ratio
```

Proposed_FullSystemがBaseline_CurrentProjectおよびAblation_NoMicrotypeよりも低いbytes per strandとVRAM使用量を示した場合、microtypeによるデータ量削減が有効であると判断する。

## 6.5.3 物理品質指標

物理品質指標では、髪形状が物理的にどの程度安定しているかを評価する。単にフレーム時間が短くても、髪が伸びすぎたり、曲がりすぎたり、物体を貫通したりする場合、制作現場では使用しにくい。そのため、長さ、曲げ、ねじれ、貫通に関する指標を測定する。

使用する品質指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| visual error | normalized error | 基準形状または高品質参照との差 |
| root error | cm | root位置誤差 |
| tip error | cm | tip位置誤差 |
| length residual | ratio | rest lengthからのずれ |
| bend residual | ratio | rest bendからのずれ |
| twist residual | ratio | rest twistからのずれ |
| penetration depth | cm | 最大貫通深さ |
| penetration count | count | 貫通したstrand数 |
| collision predicate error | % | 圧縮表現上の衝突判定誤差 |
| temporal popping | score | フレーム間の急激な変化量 |
| jitter | score | 微小振動の強さ |

本研究では、特に以下を重要視する。

```txt
length residual
bend residual
twist residual
penetration depth
penetration count
temporal popping
```

Physics IslandsやRod Repairを導入した構成でpenetration depthやresidualが低下する場合、局所物理化と修復処理が品質改善に寄与したと判断する。

## 6.5.4 Certificate指標

Certificate指標では、本研究で導入する物理的安全性の証明値が有効に機能しているかを評価する。certificateは、length residual、bend residual、twist residual、penetration residual、decode residualなどを統合した値であり、各ストランドまたは局所segment帯が安全かどうかを判定するために用いる。

使用するcertificate指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| certificate value | scalar | 総合証明値 |
| certificate pass rate | % | C <= 1 の割合 |
| certificate fail rate | % | C > 1 の割合 |
| false safe rate | % | 安全と判定したが実際には破綻した割合 |
| false unsafe rate | % | 危険と判定したが実際には安全だった割合 |
| repair success rate | % | repair後にC <= 1になった割合 |
| repair fallback rate | % | repair失敗でfallbackした割合 |
| certificate improvement | scalar | 修復前後のcertificate改善量 |

特に重要な指標は以下である。

```txt
false safe rate
certificate pass rate
repair success rate
certificate improvement
```

false safe rateは、「安全と判定したにもかかわらず実際には破綻していた割合」であり、最も危険な失敗である。この値が高い場合、certificateの閾値または定義を見直す必要がある。

## 6.5.5 Mode Allocation指標

Mode Allocation指標では、Counterfactual Budgeted Mode Allocationが適切に計算資源を割り当てているかを評価する。提案手法では、各ストランドに対して複数の計算モードを候補とし、誤差、破綻確率、計算コストを予測してmodeを選択する。そのため、選択されたmode分布と予測精度を記録する。

使用する指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| mode distribution | % | 各modeの選択割合 |
| mode switching count | count/frame | 1フレームあたりのmode変更回数 |
| mode switching cost | ms | mode変更による追加コスト |
| predicted error vs actual error | RMSE / correlation | 誤差予測の精度 |
| predicted cost vs actual cost | RMSE / correlation | コスト予測の精度 |
| failure prediction AUC | AUC | 破綻予測の精度 |
| quality per millisecond | score/ms | 時間あたり品質 |
| allocation optimality gap | % | 参照割当との差 |

特に重要な指標は以下である。

```txt
predicted error vs actual error
predicted cost vs actual cost
failure prediction AUC
p99 frame time
quality per millisecond
```

Ablation_NoCBMAとProposed_FullSystemを比較し、Proposed_FullSystemで同じフレーム予算下の品質が高く、p99 frame timeが安定している場合、mode allocationが有効であると判断する。

## 6.5.6 Physics Island指標

Physics Island指標では、Self-Materializing Strand Physics Islandsが局所接触や局所破綻に対して有効かを評価する。islandは、危険なpersistent strand ID集合を一時的な局所物理空間として生成する仕組みであるため、islandの数、サイズ、寿命、生成コスト、commit成功率を測定する。

使用する指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| island count | count/frame | 1フレームあたりのisland数 |
| average island size | strands | islandあたりの平均strand数 |
| max island size | strands | 最大islandサイズ |
| island lifetime | frames | islandが継続したフレーム数 |
| merge count | count/frame | island merge回数 |
| split count | count/frame | island split回数 |
| island build time | ms | island構築時間 |
| island solve time | ms | island solver時間 |
| island commit success rate | % | island結果を採用できた割合 |
| boundary discontinuity | cm or score | island境界の不連続性 |

特に重要な指標は以下である。

```txt
penetration depth
island commit success rate
boundary discontinuity
island solve time
max island size
```

Ablation_NoIslandsと比較して、Proposed_FullSystemでpenetration depthやpenetration countが低下し、boundary discontinuityが許容範囲内であれば、physics islandが有効であると判断する。

## 6.5.7 Rod Repair指標

Rod Repair指標では、Certificate-Driven Rod Repair Calculusが局所的な破綻を修復できているかを評価する。Rod Repairは、破綻したstrandまたはsegment帯に対して、length、bend、twist、penetration residualを改善することを目的とする。

使用する指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| repair task count | count/frame | 1フレームあたりのrepair task数 |
| repair success rate | % | 修復成功率 |
| repair failure rate | % | 修復失敗率 |
| repair fallback rate | % | fallbackへ送られた割合 |
| residual reduction per ms | score/ms | 単位時間あたり残差改善量 |
| length residual after repair | ratio | 修復後の長さ残差 |
| bend residual after repair | ratio | 修復後の曲げ残差 |
| twist residual after repair | ratio | 修復後のねじれ残差 |
| penetration after repair | cm | 修復後の貫通量 |
| visual popping after repair | score | 修復による視覚的急変 |

特に重要な指標は以下である。

```txt
repair success rate
certificate improvement
residual reduction per ms
visual popping after repair
```

Ablation_NoRodRepairと比較して、Proposed_FullSystemでcertificate pass rateが高く、residualが低下し、poppingが増加していない場合、rod repairが有効であると判断する。

## 6.5.8 GPU常駐制御指標

GPU常駐制御指標では、GPU-Resident Strand RuntimeがCPU依存をどの程度削減できているかを評価する。本研究では、risk計算、mode allocation、island生成、repair task生成、dispatch argument生成をGPU上で行うことを目指す。そのため、CPU-GPU readback量や同期回数を測定する。

使用する指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| CPU-GPU readback bytes | bytes/frame | 1フレームあたりreadback量 |
| CPU-GPU upload bytes | bytes/frame | 1フレームあたりupload量 |
| synchronization stalls | count/frame | 同期停止回数 |
| GPU compaction time | ms | compaction処理時間 |
| indirect dispatch overhead | ms | indirect dispatchの追加コスト |
| GPU-side task build time | ms | GPU上でtask生成にかかった時間 |
| debug readback interval | frames | debug readback間隔 |

特に重要な指標は以下である。

```txt
CPU-GPU readback bytes
synchronization stalls
CPU time
p99 frame time
```

Ablation_NoGpuResidentと比較して、Proposed_FullSystemでCPU time、readback bytes、synchronization stalls、p99 frame timeが低下する場合、GPU-Resident Strand Runtimeが有効であると判断する。

## 6.5.9 制作現場向け指標

制作現場向け指標では、提案手法が実際の編集作業やプレビューに有効かを評価する。本研究はゲームやアニメーション制作を想定しているため、単に物理的な数値が良いだけでなく、編集操作に対する応答性や視覚的安定性が重要である。

使用する指標は以下である。

| 指標 | 単位 | 説明 |
|---|---:|---|
| edit latency | ms | 編集入力から反映までの遅延 |
| local edit preservation | score | 編集形状が保持された度合い |
| manual correction count | count | 手作業修正が必要だった箇所 |
| artist-visible popping | subjective score | 視覚的に目立つ破綻 |
| stable editing duration | sec | 破綻なく編集できた時間 |
| interactive frame stability | % | 編集中に60FPSを維持できた割合 |

特に重要な指標は以下である。

```txt
edit latency
interactive frame stability
manual correction count
artist-visible popping
```

これらの指標により、提案手法が単なる計算高速化ではなく、制作中の操作性改善に寄与しているかを確認する。

## 6.5.10 評価指標のまとめ

本研究で用いる主要評価指標を以下にまとめる。

| 分類 | 主要指標 |
|---|---|
| 性能 | p95 frame time, p99 frame time, 60FPS pass rate |
| メモリ | bytes per strand, total VRAM usage, bandwidth per frame |
| 品質 | length residual, bend residual, twist residual, penetration depth |
| Certificate | certificate pass rate, false safe rate, repair success rate |
| Mode Allocation | predicted error, predicted cost, failure prediction AUC |
| Islands | island count, island size, boundary discontinuity, commit success |
| Rod Repair | residual reduction, certificate improvement, visual popping |
| GPU Runtime | CPU-GPU readback bytes, synchronization stalls, CPU time |
| 制作現場 | edit latency, manual correction count, interactive stability |

これらの指標を用いることで、本研究では、提案手法が現行プロジェクトと比較して、メモリ削減、計算時間安定化、物理品質向上、局所修復、CPU依存削減、制作現場での操作性向上に寄与するかを総合的に評価する。
