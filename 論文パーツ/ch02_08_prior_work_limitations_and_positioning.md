# 2.8 既存研究の限界と本研究の位置づけ

これまで述べたように、髪シミュレーションに関する既存研究は、計算量削減、ニューラル予測、高密度物理、ロッドモデル、圧縮表現、GPU処理など、多様な方向から発展してきた。それぞれの研究は重要な成果を持つ一方で、十万本規模の編集可能髪を固定60FPS制約下で扱うという観点では、なお複数の課題が残る。

Guide/Follower型の手法は、計算量を削減するうえで有効である。しかし、フォロワーストランドが独立した物理状態や編集状態を持ちにくいため、ストランド単位の局所編集や局所接触の修復には限界がある。Neural Hair Simulationは高速な形状予測や動的予測を可能にするが、物理的な安全性や局所的な失敗検出を明示的に保証するものではない。Dense Physics Hair Simulationは高品質な接触応答や変形を実現できるが、十万本規模の全髪に常時適用するには計算量が大きい。Rod-based Hairは曲げやねじれを高精度に扱えるが、全ストランドに毎フレーム適用するには重い。Hair Compressionはデータ量を削減できるが、圧縮表現上で物理的な証明値を直接扱うことは十分に考慮されていない。GPU-based Hair Runtimeは実行効率を高めるが、mode allocation、physics island生成、rod repair task生成、certificate更新をすべてGPU上で閉ループ化する設計は限定的である。

本研究は、これらの既存研究を置き換えるのではなく、それぞれの限界を踏まえて、髪の扱い方そのものを再定義する。すなわち、髪を全体として一様に解く対象ではなく、各ストランドが個別のデータ表現、物理状態、危険度、証明値、計算モードを持つ対象として扱う。そして、ストランドごとに、どの程度のデータ量で保持し、どの程度の物理処理を実体化するかを、固定フレーム予算下で決定する。

本研究の位置づけは、Physics-Certified Strand Microtype、Counterfactual Budgeted Mode Allocation、Self-Materializing Strand Physics Islands、Certificate-Driven Rod Repair Calculus、GPU-Resident Strand Runtimeを統合したstrand-native virtualized hair physics frameworkである。この枠組みにより、髪シミュレーションを「全体ソルバ」から「ストランドごとのメモリ解像度と物理解像度の実行時割当」へと再定義する。


---

# 参考URL

## Neural / quasi-static / dynamic hair

- Quaffure: Real-Time Quasi-Static Neural Hair Simulation  
  https://arxiv.org/abs/2412.10061

- Neuralocks: Real-Time Dynamic Neural Hair Simulation  
  https://arxiv.org/html/2507.05191v1

- HairFormer: Transformer-Based Dynamic Neural Hair Simulation  
  https://arxiv.org/abs/2507.12600

## Dense physics / GPU physics

- Augmented Mass-Spring model for Real-Time Dense Hair Simulation  
  https://arxiv.org/abs/2412.17144

- Interactive Hair Simulation on the GPU using ADMM  
  https://dl.acm.org/doi/fullHtml/10.1145/3588432.3591551

## Rod / Cosserat rod

- Stable Cosserat Rods project page  
  https://graphics.cs.utah.edu/research/projects/stable-cosserat-rods/

- Stable Cosserat Rods ACM DL  
  https://dl.acm.org/doi/abs/10.1145/3721238.3730618

## Compression / representation

- Neural Strands  
  https://arxiv.org/abs/2207.14067

- NeuWigs  
  https://arxiv.org/abs/2212.00613

## Real-time production / GPU runtime

- Unreal Engine Hair Rendering and Simulation  
  https://dev.epicgames.com/documentation/unreal-engine/hair-rendering-and-simulation-in-unreal-engine

- AMD TressFX  
  https://gpuopen.com/tressfx/
