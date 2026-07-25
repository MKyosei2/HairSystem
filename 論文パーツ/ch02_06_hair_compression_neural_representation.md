# 2.6 Hair Compression / Neural Hair Representation

高密度な髪を扱うためには、物理計算だけでなく、データ表現の効率化も重要である。髪は多数のストランドと多数のセグメントから構成されるため、各点をfloat32の位置列として保持すると、メモリ使用量とGPU帯域が大きくなる。さらに、速度、幅、法線、前フレーム位置、編集状態などを保持する場合、必要なデータ量はさらに増加する。このため、髪の圧縮表現や低次元表現は、リアルタイム髪処理において重要な研究領域である。

Hair CompressionやNeural Hair Representationでは、髪形状を低次元のパラメータ、latent code、テクスチャ、ニューラル特徴量などで表現する手法が提案されている。これらの手法は、髪形状やappearanceを効率的に保存し、必要に応じて復元することを目的とする。特に、ニューラル表現を用いることで、高密度な髪の複雑な形状を比較的少ないデータで表現できる可能性がある。[要引用]

しかし、既存の圧縮表現の多くは、見た目や形状の再構成を主な目的としている。そのため、圧縮表現上で物理的な安全性を直接評価することは必ずしも主眼ではない。たとえば、圧縮後の形状が視覚的には近くても、ストランド長、曲げ、ねじれ、接触判定において物理的な誤差が大きい場合がある。また、圧縮表現をdecodeしてからでなければ物理判定ができない場合、リアルタイム用途ではdecodeコストと帯域が問題になる。

本研究では、この問題に対して、Physics-Certified Strand Microtypeを導入する。これは単なる量子化やlatent表現ではなく、root frame、arc-length、curvature、torsion、residual、certificateを持つ髪専用の圧縮データ型である。重要なのは、圧縮表現上で長さ、曲げ、ねじれ、decode誤差に基づくcertificateを評価できる点である。これにより、圧縮表現を物理処理やmode allocationと直接接続できる。


---

# 参考URL

- Neural Strands: Learning Hair Geometry and Appearance from Multi-View Images  
  https://arxiv.org/abs/2207.14067

- Neural Strands ECCV 2022 PDF  
  https://www.ais.uni-bonn.de/papers/ECCV_2022_Rosu.pdf

- Neural Strands ACM/Springer page  
  https://dl.acm.org/doi/10.1007/978-3-031-19827-4_5

- NeuWigs: A Neural Dynamic Model for Volumetric Hair Capture and Animation  
  https://arxiv.org/abs/2212.00613

- NeuWigs project page  
  https://ziyanw1.github.io/neuwigs/

- NeuWigs CVPR 2023 paper PDF  
  https://openaccess.thecvf.com/content/CVPR2023/papers/Wang_NeuWigs_A_Neural_Dynamic_Model_for_Volumetric_Hair_Capture_and_CVPR_2023_paper.pdf
