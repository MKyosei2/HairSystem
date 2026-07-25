# 2.3 Neural Hair Simulation

近年、ニューラルネットワークを用いた髪シミュレーションや髪形状生成が研究されている。Neural Hair Simulationでは、従来の物理ソルバを直接実行する代わりに、姿勢、速度、頭部運動、髪型パラメータなどを入力として、髪の静的形状や動的変形を推定する。これにより、従来の物理計算よりも高速に髪の形状を生成できる可能性がある。[要引用]

ニューラル手法の一つの方向は、準静的な髪形状の推定である。この種の手法では、現在の頭部姿勢や髪型情報から、重力や体型に応じた自然な髪の垂れ方を推定する。別の方向として、動的な髪の揺れや残差をニューラルネットワークで予測する手法もある。これらは、従来の物理ベース手法と比較して高速であり、複雑な髪の動きを学習的に表現できる可能性を持つ。

一方で、ニューラル手法には課題もある。ニューラルネットワークの出力は物理的な制約を必ずしも満たすとは限らず、長さ、曲げ、ねじれ、貫通などの物理的な安全性を明示的に保証することは難しい。また、学習データに含まれない姿勢、編集操作、接触状況に対して、予測が不安定になる可能性がある。制作現場で重要となる局所編集や局所修復に対して、ニューラル手法がどのように応答するかも設計に依存する。

本研究では、ニューラル的な予測を否定するのではなく、その役割を再定義する。本研究が目指すのは、ニューラルネットワークによって髪形状そのものを直接置き換えることではない。重要なのは、各ストランドに対して、どの計算モードを選択すべきか、どのモードを選んだ場合にどの程度の誤差、破綻確率、計算コストが発生するかを予測することである。つまり、本研究における予測器は、髪の最終形状を直接生成するものではなく、物理計算の実体化を制御するための反実仮想的なスケジューラとして機能する。


---

# 参考URL

- Quaffure: Real-Time Quasi-Static Neural Hair Simulation  
  https://arxiv.org/abs/2412.10061

- Quaffure project page  
  https://tuurstuyck.github.io/quaffure/quaffure.html

- Neuralocks: Real-Time Dynamic Neural Hair Simulation  
  https://arxiv.org/html/2507.05191v1

- HairFormer: Transformer-Based Dynamic Neural Hair Simulation  
  https://arxiv.org/abs/2507.12600

- HairFormer PDF  
  https://hanyuc.com/data/papers/hairformer.pdf

- NeuWigs: A Neural Dynamic Model for Volumetric Hair Capture and Animation  
  https://arxiv.org/abs/2212.00613

- NeuWigs project page  
  https://ziyanw1.github.io/neuwigs/

- NeuWigs CVPR 2023 paper PDF  
  https://openaccess.thecvf.com/content/CVPR2023/papers/Wang_NeuWigs_A_Neural_Dynamic_Model_for_Volumetric_Hair_Capture_and_CVPR_2023_paper.pdf
