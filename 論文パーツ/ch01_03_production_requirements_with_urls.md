# 1.3 制作現場における要求

ゲームやアニメーションなどの制作現場では、髪シミュレーションに対して単なる物理的正確性だけでなく、制作工程に適した操作性と安定性が求められる。制作中のアーティストは、髪型の形状、束感、長さ、流れ、接触挙動を確認しながら調整する必要がある。そのため、最終レンダリング時だけでなく、編集作業中にも高速で安定したプレビューが必要となる。

第一の要求は、高密度な髪を扱えることである。少数のガイドストランドだけでは、細かな髪の重なり、束の崩れ、接触時の局所的な変形を十分に表現できない場合がある。しかし、高密度な髪をそのまま全物理処理することは重いため、高密度性と計算効率を両立する設計が必要である。

第二の要求は、局所編集への対応である。髪制作では、全体の髪型だけでなく、前髪、毛先、耳周辺、肩に触れる部分など、局所的な領域を調整することが多い。このとき、編集した部分だけが自然に変化し、周囲の髪と連続的につながることが求められる。また、編集によって発生した局所的な破綻を、自動的に検出し修復できることが望ましい。

第三の要求は、接触と貫通の抑制である。髪は頭部、肩、首、衣服、アクセサリ、手などと接触する。制作中にこれらの接触が頻繁に発生する場合、髪が物体を貫通したり、不自然に跳ねたりすると、手作業での修正が必要になる。したがって、髪が接触する可能性のある局所領域を検出し、必要な場合だけ高精度な物理処理や修復処理を行う仕組みが重要である。

第四の要求は、処理時間の安定性である。制作現場では平均FPSが高いだけでは不十分であり、一部のフレームで処理時間が大きく増加することは避ける必要がある。したがって、平均フレーム時間だけでなく、p95、p99、最悪フレーム時間を制御する必要がある。また、CPUとGPUの同期やreadbackが頻繁に発生するとフレーム時間が不安定になるため、可能な限りGPU上で処理を完結させることが望ましい。

本研究では、これらの制作現場の要求を満たすため、髪をストランド単位で管理しながら、必要なデータ量と物理解像度を実行時に割り当てる方式を採用する。これにより、高密度、低メモリ、リアルタイム、局所編集、接触修復を同時に満たす髪物理ランタイムの実現を目指す。


---

# 参考URL：制作現場・実用ワークフローに関する資料

本節で述べた制作現場の要求は、以下の公開資料・実エンジン資料・実務紹介記事を参考にしている。

## リアルタイム確認・高速反復

- Unreal Engine Spotlight: Behind the scenes on Weta Digital's real-time hair and fur short Meerkat  
  https://www.unrealengine.com/spotlights/behind-the-scenes-on-weta-digital-s-real-time-hair-and-fur-short-meerkat

- Unreal Engine Blog: Webinar: Creating Realistic Hair and Feathers in Unreal Engine  
  https://www.unrealengine.com/blog/webinar-creating-realistic-hair-and-feathers-in-unreal-engine

- AWN: New Epic Games white paper, Grooming for Real-Time Realism: Hair and Fur with Unreal Engine  
  https://www.awn.com/news/new-epic-games-white-paper-grooming-real-time-realism-hair-and-fur-unreal-engine

## DCC連携・Alembic・groom workflow

- Unreal Engine Tech Blog: An early look at next-generation real-time hair and fur  
  https://www.unrealengine.com/tech-blog/an-early-look-at-next-generation-real-time-hair-and-fur

- Unreal Engine Documentation: Hair Rendering and Simulation  
  https://dev.epicgames.com/documentation/unreal-engine/hair-rendering-and-simulation-in-unreal-engine

- Unreal Engine 4.27 Documentation: Hair Rendering  
  https://dev.epicgames.com/documentation/en-us/unreal-engine/hair-rendering?application_version=4.27

## GPU hair / 実用ライブラリ / パフォーマンス

- AMD GPUOpen: TressFX  
  https://gpuopen.com/tressfx/

- AMD GPUOpen: TressFX 5.0 for Unreal Engine 5  
  https://gpuopen.com/learn/tressfx-5-0-unreal-engine-5-update/

- AMD GPUOpen TressFX GitHub  
  https://github.com/GPUOpen-Effects/TressFX

- TressFX 5.0 Developer Guide PDF  
  https://gpuopen.com/download/TressFX5.0_DeveloperGuide.pdf

## 実務・ユーザー視点での性能問題

- Unreal Engine Forum: UE - How to improve Groom hair performance?  
  https://forums.unrealengine.com/t/ue-how-to-improve-groom-hair-performance/511120

- fxguide: UE4 real-time hair advances  
  https://www.fxguide.com/fxfeatured/ue4-real-time-hair-advances/

- Theseus: Fur Simulation thesis, real-time simulation and animator workflow discussion  
  https://www.theseus.fi/bitstream/10024/863065/2/Hautakangas_Anu.pdf
