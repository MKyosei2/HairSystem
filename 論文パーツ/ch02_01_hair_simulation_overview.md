# 2.1 髪シミュレーションの概要

髪シミュレーションは、多数の細長いストランドを時間的に変形させる問題である。一本の髪は、通常、点列またはセグメント列として表現され、各点または各セグメントに対して位置、速度、質量、長さ、曲げ、ねじれ、幅などの情報を持たせる。髪全体は、これらのストランドが多数集まった集合として表現される。写実的な髪表現を目指す場合、髪の本数は数万本から十万本規模になり、各ストランドを複数の点で離散化するため、計算対象の数は非常に大きくなる。

髪シミュレーションでは、主に伸び、曲げ、ねじれ、衝突、摩擦、重力、慣性、空気抵抗などの要素を扱う必要がある。特に、髪は細長く柔軟であり、頭部運動や外部物体との接触に対して複雑な変形を示す。また、多数のストランドが互いに近接するため、髪同士の接触や束化も視覚的な自然さに影響する。これらの性質をすべて高精度に扱うことは、オフラインシミュレーションでは可能であっても、リアルタイム用途では大きな負担となる。

この問題に対して、既存研究ではさまざまな方向から高速化や近似が行われてきた。代表的な方向として、少数のガイドストランドをシミュレーションし、多数のフォロワー髪を補間するGuide/Follower型の手法がある。また、近年ではニューラルネットワークを用いて髪の静的形状や動的変形を推定するNeural Hair Simulationも研究されている。さらに、物理的な品質を重視するDense Physics Hair Simulationや、曲げやねじれを高精度に扱うRod-based Hair、Cosserat Rod系の手法も存在する。加えて、高密度髪の保存や描画を効率化するためのHair CompressionやNeural Hair Representation、GPU上で大量の髪を処理するGPU-based Hair Runtimeも重要な研究領域である。

本研究は、これらの関連研究を踏まえ、髪を単に高速に変形させるのではなく、ストランドごとにデータ表現と物理処理を割り当てる問題として再定義する。特に、圧縮データ型、反実仮想的な計算割当、自己実体化する局所物理島、証明付き局所修復、GPU常駐制御を統合することで、高密度かつ編集可能な髪を固定フレーム予算下で扱うことを目指す。


---

# 参考URL

- Quaffure: Real-Time Quasi-Static Neural Hair Simulation  
  https://arxiv.org/abs/2412.10061

- Neuralocks: Real-Time Dynamic Neural Hair Simulation  
  https://arxiv.org/html/2507.05191v1

- HairFormer: Transformer-Based Dynamic Neural Hair Simulation  
  https://arxiv.org/abs/2507.12600

- Augmented Mass-Spring model for Real-Time Dense Hair Simulation  
  https://arxiv.org/abs/2412.17144

- Stable Cosserat Rods project page  
  https://graphics.cs.utah.edu/research/projects/stable-cosserat-rods/

- Neural Strands: Learning Hair Geometry and Appearance from Multi-View Images  
  https://arxiv.org/abs/2207.14067

- Unreal Engine Hair Rendering and Simulation documentation  
  https://dev.epicgames.com/documentation/unreal-engine/hair-rendering-and-simulation-in-unreal-engine
