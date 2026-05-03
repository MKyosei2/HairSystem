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
