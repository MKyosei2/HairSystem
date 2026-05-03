# 2.2 Guide/Follower型の髪シミュレーション

Guide/Follower型の髪シミュレーションは、リアルタイム髪表現において広く用いられてきた代表的な高速化手法である。この方式では、全ての髪を直接シミュレーションするのではなく、少数の代表的なガイドストランドを物理計算し、その結果を用いて多数のフォロワーストランドを補間または変形させる。これにより、計算対象となるストランド数を大幅に削減できるため、リアルタイム用途に適している。[要引用]

この手法の利点は、計算負荷を抑えながら、髪全体としての大まかな動きを表現できる点である。特に、髪全体が同じ方向に揺れるような状況や、細かなストランド単位の接触が重要でない場面では、少数のガイドストランドによる近似でも十分な視覚品質が得られる場合がある。また、ガイドストランドのみを物理計算すればよいため、CPUやGPUの負荷を一定範囲に制御しやすい。

一方で、Guide/Follower型には明確な限界がある。第一に、フォロワーストランドはガイドストランドの補間結果として扱われるため、各ストランドが独立した物理状態を持ちにくい。これにより、一本単位または局所領域単位の編集に対して、編集内容を物理的に保持したり、局所的な破綻を検出したりすることが難しくなる。第二に、接触や貫通が局所的に発生した場合、ガイドストランドがその接触を代表していなければ、フォロワーストランド側の破綻を正確に修正できない。

本研究は、Guide/Follower型の発想を完全に否定するものではない。計算量削減のために、すべてのストランドを常に高精度物理で解かないという点では同じ問題意識を持つ。しかし、本研究ではフォロワーストランドを単なる表示用の補間対象としては扱わない。各ストランドはpersistent strand IDを持ち、圧縮データ型、状態、危険度、証明値、計算モードを保持する。そのため、低コストな処理対象であっても、必要な場合には局所物理化や修復処理へ昇格できる。


---

# 参考URL

- Real-time Hair Simulation on GPU  
  https://www.ha.ai.kyutech.ac.jp/research/hair/index.html

- Real-time Hair Simulation with Efficient Hair Style Preservation PDF  
  https://giordi91.github.io/papers/Real-timeHairSimulationwithEf%EF%AC%81cientHairStylePreservation.pdf

- Real-time Hair Simulation with Efficient Hair Style Preservation video  
  https://www.youtube.com/watch?v=r3PgxbTi3OA

- AMD TressFX  
  https://gpuopen.com/tressfx/

- TressFX GitHub  
  https://github.com/GPUOpen-Effects/TressFX

- TressFX 5.0 Developer Guide PDF  
  https://gpuopen.com/download/TressFX5.0_DeveloperGuide.pdf
