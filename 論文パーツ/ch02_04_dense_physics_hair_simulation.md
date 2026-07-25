# 2.4 Dense Physics Hair Simulation

Dense Physics Hair Simulationは、多数のストランドを物理モデルに基づいて直接シミュレーションする手法である。髪の伸び、曲げ、ねじれ、衝突、束化などを明示的に扱うことで、高品質な髪の挙動を表現できる。特に、接触が多い場面や、髪同士の相互作用が視覚的に重要な場面では、単純な補間やニューラル予測よりも物理ベースの手法が有効である。[要引用]

Dense Physics系の手法では、各ストランドを点列やセグメント列として扱い、質点ばねモデル、位置ベースダイナミクス、拡張質量ばねモデル、連続体的な近似などを用いて髪の変形を計算する。これにより、ガイドストランドだけでは表現しにくい局所的な接触や、髪束の変形を扱うことができる。また、GPUを用いた並列処理によって、従来よりも多くのストランドをリアルタイムに近い速度で処理する研究も存在する。

しかし、Dense Physics Hair Simulationには大きな課題がある。それは、計算量とメモリ量がストランド数およびセグメント数に強く依存することである。十万本規模の髪をすべて高精度に物理計算すると、計算時間、メモリ使用量、帯域の負荷が非常に大きくなる。特に、接触判定や衝突応答を含む場合、局所的な密度が高い領域で処理時間が急増しやすい。

本研究は、Dense Physicsを全体に常時適用するのではなく、必要な局所領域だけに一時的に実体化する立場を取る。具体的には、接触、編集、破綻予測によって危険と判定されたpersistent strand ID集合を、Self-Materializing Strand Physics Islandsとして生成する。これにより、全髪をdense physicsで解くのではなく、危険な実ストランド集合だけを局所物理空間として扱う。

この点で、本研究はDense Physics Hair Simulationとは異なる。Dense Physics系の研究が「高密度な髪をどのように高速に解くか」を主題とするのに対し、本研究は「高密度物理をどのストランド集合に、いつ、どの予算で実体化するか」を主題とする。


---

# 参考URL

- Augmented Mass-Spring model for Real-Time Dense Hair Simulation  
  https://arxiv.org/abs/2412.17144

- Augmented Mass-Spring Model for Real-Time Dense Hair Simulation ICCV 2025 PDF  
  https://openaccess.thecvf.com/content/ICCV2025/papers/H._Augmented_Mass-Spring_Model_for_Real-Time_Dense_Hair_Simulation_ICCV_2025_paper.pdf

- AMS project video  
  https://www.youtube.com/watch?v=vxY-jMUpYfA

- Interactive Hair Simulation on the GPU using ADMM  
  https://dl.acm.org/doi/fullHtml/10.1145/3588432.3591551

- Real-time Hair Simulation with Efficient Hair Style Preservation PDF  
  https://giordi91.github.io/papers/Real-timeHairSimulationwithEf%EF%AC%81cientHairStylePreservation.pdf

- AMD TressFX  
  https://gpuopen.com/tressfx/
