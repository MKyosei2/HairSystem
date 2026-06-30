# 第2章 参考文献URL一覧

本文中の番号は以下に対応する。最終提出時には、著者名、発表年、会議名、ページ番号、DOI表記を大学または提出先の形式に合わせて整える。

## [1] Super-Helices for Predicting the Dynamics of Natural Hair

Bertails, F., Audoly, B., Cani, M.-P., Querleux, B., Leroy, F., and Lévêque, J.-L.  
Super-Helices for Predicting the Dynamics of Natural Hair.  
ACM Transactions on Graphics, 2006.  
https://doi.org/10.1145/1141911.1141929

## [2] Discrete Elastic Rods

Bergou, M., Wardetzky, M., Robinson, S., Audoly, B., and Grinspun, E.  
Discrete Elastic Rods.  
ACM Transactions on Graphics, 2008.  
https://doi.org/10.1145/1360612.1360662

## [3] Position Based Elastic Rods

Umetani, N., et al.  
Position Based Elastic Rods.  
ACM SIGGRAPH / related publication, 2014.  
https://doi.org/10.1145/2601097.2601103

## [4] Fast Simulation of Inextensible Hair and Fur

Müller, M., Kim, T.-Y., and Chentanez, N.  
Fast Simulation of Inextensible Hair and Fur.  
VRIPHYS, 2012.  
https://matthias-research.github.io/pages/publications/FTLHairFur.pdf

## [5] Position Based Dynamics

Müller, M., Heidelberger, B., Hennix, M., and Ratcliff, J.  
Position Based Dynamics.  
Journal of Visual Communication and Image Representation, 2007.  
https://matthias-research.github.io/pages/publications/posBasedDyn.pdf

## [6] XPBD: Position-Based Simulation of Compliant Constrained Dynamics

Macklin, M., Müller, M., and Chentanez, N.  
XPBD: Position-Based Simulation of Compliant Constrained Dynamics.  
MIG, 2016.  
https://matthias-research.github.io/pages/publications/XPBD.pdf

## [7] Adaptive Wisp Tree: A Multiresolution Control Structure for Simulating Dynamic Clustering in Hair Motion

Ward, K., Lin, M. C., Lee, J., Fisher, S., and Macri, D.  
Adaptive Wisp Tree: A Multiresolution Control Structure for Simulating Dynamic Clustering in Hair Motion.  
ACM Symposium on Computer Animation, 2003.  
https://doi.org/10.1145/846276.846288

## [8] GPU Gems 2: Hair Animation and Rendering in the Nalu Demo

Nguyen, H. and Donnelly, W.  
Hair Animation and Rendering in the Nalu Demo.  
GPU Gems 2, Chapter 23.  
https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-23-hair-animation-and-rendering-nalu-demo

## [9] Particle-Based Fluid Simulation for Interactive Applications

Müller, M., Charypar, D., and Gross, M.  
Particle-Based Fluid Simulation for Interactive Applications.  
ACM Symposium on Computer Animation, 2003.  
https://matthias-research.github.io/pages/publications/sca03.pdf

---

## 本章での使い方

[1]〜[4]は、髪やロッドを点列、segment、curve、rodとして扱う既存研究を説明するために使用する。  
[5]〜[6]は、PBD / XPBD、制約残差、実時間物理の安定化を説明するために使用する。  
[7]〜[8]は、guide/follower、wisp、cluster、LOD、GPU髪実装を説明するために使用する。  
[9]は、髪以外の粒子シミュレーションにおける部分更新や実時間処理の背景として使用する。

本研究では、これらの研究と同じことを新規性として主張しない。第2章では、これらを既存の基盤として整理した上で、本研究の差分が「Segment Groupをruntime decision unitとして扱うこと」「certificateとcache validityを処理経路選択に接続すること」「false-static率と境界誤差で安全な再利用を評価すること」にあると説明する。
