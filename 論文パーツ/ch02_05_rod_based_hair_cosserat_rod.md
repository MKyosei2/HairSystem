# 2.5 Rod-based Hair / Cosserat Rod

髪は細長い弾性体として見ることができるため、ロッドモデルを用いた表現は髪シミュレーションにおいて重要である。特に、Cosserat Rodのようなロッドモデルは、曲げ、ねじれ、伸びなどを高精度に扱うことができる。髪や繊維状物体の物理挙動を正確に表現するうえで、ロッドベースの手法は有力な選択肢である。[要引用]

Rod-based HairやCosserat Rod系の手法では、ストランドを連続的または離散的なロッドとして扱い、各セグメントの長さ、曲率、ねじれ、フレームを考慮して変形を計算する。このような手法は、単純な質点ばねモデルよりも、髪の曲げやねじれの物理的性質を忠実に表現しやすい。また、安定なロッドソルバを用いることで、高剛性の髪や大きな時間刻みに対しても破綻しにくいシミュレーションを実現できる。

しかし、ロッドソルバを十万本規模の髪全体に毎フレーム適用することは、リアルタイム用途では重い。特に、すべてのストランドに対して曲げ、ねじれ、衝突を含むロッド制約を解く場合、計算時間が大きくなり、固定60FPSの予算に収めることが難しくなる。また、制作中の局所編集や一時的な接触破綻に対して、全体を高精度ロッドソルバで解き直すことは過剰である場合が多い。

本研究では、ロッドソルバを全体の主ソルバとして常用するのではなく、破綻したストランドまたは局所セグメント帯だけに適用する。具体的には、Certificate-Driven Rod Repair Calculusを導入し、長さ、曲げ、ねじれ、貫通残差からなるcertificateに基づいて、修復範囲、iteration数、成功判定を決定する。これにより、必要な局所領域だけにロッド修復を適用し、修復後の結果が許容範囲内にあるかを定量的に判定する。


---

# 参考URL

- Stable Cosserat Rods project page  
  https://graphics.cs.utah.edu/research/projects/stable-cosserat-rods/

- Stable Cosserat Rods paper PDF  
  https://jerryhsu.io/wp-content/uploads/2025/05/Sig25__Stable_Cosserat_Rods.pdf

- Stable Cosserat Rods ACM DL  
  https://dl.acm.org/doi/abs/10.1145/3721238.3730618

- Stable Cosserat Rods GitHub  
  https://github.com/jerry060599/StableCosseratRods

- Stable Cosserat Rods SIGGRAPH 2025 video  
  https://www.youtube.com/watch?v=nCKlL65AjVc
