# 2.7 GPU-based Hair Runtime

高密度な髪をリアルタイムに処理するためには、GPUを用いた並列処理が不可欠である。髪シミュレーションでは、ストランドごとの処理、セグメントごとの処理、衝突判定、補間、描画用頂点生成など、多くの処理が並列化に適している。そのため、リアルタイム髪処理では、compute shaderやGPU bufferを用いて大量の髪データを処理する手法が重要となる。[要引用]

GPU-based Hair Runtimeでは、ストランドの位置更新、衝突判定、LOD、描画用データ生成などをGPU上で行うことで、CPU負荷を削減し、データ転送を抑えることができる。また、GPU上でcompactionやindirect dispatchを用いることで、可変数の処理対象を効率的に扱える。たとえば、重要なストランドだけを抽出して処理したり、描画対象の数に応じてDrawIndirectを行ったりする設計が考えられる。

しかし、GPUを用いるだけでは、高密度な編集可能髪におけるすべての問題は解決しない。CPU側でmode decisionやtask generationを行う場合、GPU上の状態をCPUへreadbackする必要が生じ、同期コストが発生する。また、island生成やrepair task生成のように、毎フレーム可変数の処理対象を扱う場合、CPU-GPU間のデータ往復が増えるとp99フレーム時間が悪化しやすい。

本研究では、GPU-Resident Strand Runtimeを導入する。このruntimeでは、strand state、microtype、certificate、risk、mode decision、physics island、repair taskをGPU上のbufferとして保持する。そして、risk計算、mode allocation、compaction、island生成、repair task生成、dispatch argument生成をGPU上で行う。CPUは主にパラメータ設定、入力更新、debug時の統計取得を担当し、通常時には全ストランド状態のreadbackを行わない。


---

# 参考URL

- Unreal Engine Hair Rendering and Simulation documentation  
  https://dev.epicgames.com/documentation/unreal-engine/hair-rendering-and-simulation-in-unreal-engine

- Unreal Engine 4.27 Hair Rendering documentation  
  https://dev.epicgames.com/documentation/en-us/unreal-engine/hair-rendering?application_version=4.27

- Unreal Engine Tech Blog: An early look at next-generation real-time hair and fur  
  https://www.unrealengine.com/tech-blog/an-early-look-at-next-generation-real-time-hair-and-fur

- AMD TressFX  
  https://gpuopen.com/tressfx/

- AMD TressFX GitHub  
  https://github.com/GPUOpen-Effects/TressFX

- TressFX 5.0 for Unreal Engine 5 update  
  https://gpuopen.com/learn/tressfx-5-0-unreal-engine-5-update/

- TressFX 5.0 Developer Guide PDF  
  https://gpuopen.com/download/TressFX5.0_DeveloperGuide.pdf
