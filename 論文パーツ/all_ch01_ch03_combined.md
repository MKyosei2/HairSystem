# 1.1 研究背景

近年のゲーム、アニメーション、映像制作、仮想空間表現において、キャラクターの外見品質は作品全体の印象を大きく左右する要素となっている。その中でも髪は、顔や衣服と同様にキャラクターの個性、動き、質感を表現する重要な対象である。髪の揺れ方、束感、接触時の変形、頭部運動に対する追従性は、視覚的な自然さに直接影響する。特に長髪や高密度な髪型では、わずかな不自然さや貫通、過度な硬さが視聴者に強い違和感を与えるため、髪の表現品質は制作現場において重要な技術課題である。

一方で、髪は計算対象として非常に複雑である。人体や衣服と比較して、髪は細長い多数のストランドから構成され、それぞれが曲げ、伸び、ねじれ、衝突、摩擦、束化などの物理的性質を持つ。写実的な表現を目指す場合、数万本から十万本規模の髪を扱う必要があり、さらに各ストランドを複数のセグメントで表現する必要がある。そのため、すべての髪を高精度な物理シミュレーションで毎フレーム解くことは、計算時間、メモリ使用量、GPU帯域の面で大きな負担となる。

従来、リアルタイム用途では、代表的なガイドストランドのみをシミュレーションし、残りの髪を補間する手法や、髪型全体を近似的に変形させる手法が用いられてきた。これらの手法は計算量を削減できる一方で、各ストランド単位の編集、局所的な接触、細かな破綻修復を扱うには限界がある。また、近年ではニューラルネットワークを用いた髪形状の予測や、GPUを用いた高密度な髪処理も研究されているが、制作現場で求められる「高密度」「リアルタイム」「局所編集」「物理的安全性」「低メモリ」を同時に満たすことは依然として難しい。

本研究では、この問題に対して、髪を単なる点列や一様な物理対象として扱うのではなく、各ストランドを個別の状態、証明値、計算モードを持つ対象として扱う立場を取る。特に、十万本規模の髪を固定フレーム予算下で扱うため、データ表現、計算割当、局所物理化、修復保証、GPU常駐制御を統合した新しい髪物理ランタイムを設計する。

---

# 1.2 髪シミュレーションにおける問題

髪シミュレーションにおける本質的な問題は、対象となる要素数の多さと、必要とされる物理的な複雑さが同時に存在する点にある。髪をストランドの集合として表現する場合、一本のストランドは複数の点またはセグメントから構成される。十万本規模の髪に対して、位置、速度、幅、法線、前フレーム位置、編集状態、衝突状態を保持すると、必要なメモリ量とメモリ帯域は急速に増加する。

このような高密度な髪に対して、すべてのストランドを同じ精度で毎フレーム物理計算することは現実的ではない。リアルタイム用途では60FPSを目標とする場合、1フレームあたりの処理時間は約16.67ms以内に収める必要がある。しかし、十万本規模の髪に対して曲げ、伸び、衝突、ねじれをすべて高精度に解くと、この時間予算を容易に超過する。また、平均フレーム時間が小さくても、一部フレームで大きく処理時間が増加すると、制作中のプレビューやインタラクションに支障をきたす。

既存の高速化手法としては、ガイドストランドを用いる方法がある。この方法では、一部の代表的な髪のみをシミュレーションし、その他の髪は補間によって表現する。しかし、この方法では、非ガイドストランドが独立した物理状態を持ちにくく、一本単位の編集や局所的な接触応答を高精度に扱うことが難しい。また、髪の局所的な破綻、貫通、曲げすぎ、伸びすぎなどを、個別ストランド単位で検出・修復する仕組みも不十分になりやすい。

さらに、単純なLODや距離ベースの処理削減では、実際に物理計算が必要な箇所を正確に選ぶことができない。カメラから遠い髪であっても接触や絡みによって破綻している場合があり、逆にカメラに近い髪であっても安定しており高精度処理を必要としない場合がある。したがって、髪シミュレーションでは、見た目の重要度だけでなく、物理的な危険度、編集状態、過去の修復履歴、計算コストを考慮して、ストランドごとに処理方法を選択する必要がある。

本研究が対象とする問題は、単に髪の動きを高速に計算することではない。高密度な髪を低メモリで保持し、各ストランドに対して必要な物理処理を選択し、危険な領域だけを局所的に高精度化し、残った破綻を証明値に基づいて修復することで、固定フレーム予算内に収まる編集可能な髪物理システムを実現することである。

---

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

---

# 1.4 本研究の目的

本研究の目的は、十万本規模の編集可能な髪を固定60FPS制約下で扱うための、strand-native virtualized hair physics framework を提案することである。本研究では、髪を単なる点列の集合や、全体を一様に解く物理対象として扱うのではなく、各ストランドが個別の状態、圧縮表現、計算モード、危険度、証明値を持つ対象として扱う。そして、各ストランドに対して、必要なメモリ解像度と物理解像度を実行時に割り当てる。

従来の髪シミュレーションでは、計算量を削減するためにガイドストランドやLODが用いられることが多い。しかし、このような手法では、非ガイドストランドの個別性、編集状態、局所的な破綻を十分に扱えない場合がある。一方ですべてのストランドを高精度な物理シミュレーションで解くと、十万本規模ではメモリと計算時間の両面で実用的ではない。したがって、本研究では「全体を同じ精度で解く」のではなく、「必要なストランドに必要な処理だけを実体化する」ことを基本方針とする。

本研究では、この目的を達成するために五つの構成要素を導入する。第一に、髪専用の圧縮データ型である Physics-Certified Strand Microtype を用いて、ストランドの形状と物理的安全性を低データ量で保持する。第二に、Counterfactual Budgeted Mode Allocation により、各ストランドに対して複数の計算モードを選んだ場合の誤差、破綻確率、計算コストを予測し、固定フレーム予算内で処理を割り当てる。第三に、Self-Materializing Strand Physics Islands により、危険な実ストランド集合だけを一時的な局所物理空間として生成する。第四に、Certificate-Driven Rod Repair Calculus により、破綻した局所セグメントを証明値に基づいて修復する。第五に、これらをGPU上で閉ループ化する GPU-Resident Strand Runtime を構築する。

これらの構成要素は、それぞれ独立した機能としても意味を持つが、本研究ではそれらを統合することで、髪シミュレーションを固定された全体ソルバではなく、ストランドごとのメモリ解像度と物理解像度を実行時に割り当てる問題として再定義する。

---

# 1.5 本研究の貢献

本研究の主な貢献は、十万本規模の編集可能髪を固定フレーム予算下で扱うために、髪のデータ表現、計算割当、局所物理化、修復保証、GPU常駐制御を統合した点にある。従来の髪シミュレーションでは、物理モデルの精度、ニューラル予測の高速化、LODによる計算削減、GPU処理などが個別に研究されてきた。本研究では、それらを単に組み合わせるのではなく、ストランド単位でメモリ解像度と物理解像度を割り当てる新しいランタイム構造として再設計する。

第一の貢献は、Physics-Certified Strand Microtype である。これは、髪を通常のfloat32点列として保持するのではなく、root frame、arc-length、curvature、torsion、residual、certificateを持つ髪専用の圧縮データ型として表現するものである。この表現は単なる量子化ではなく、圧縮表現上で長さ、曲げ、ねじれ、decode誤差に基づく物理的安全性を評価できる点に特徴がある。

第二の貢献は、Counterfactual Budgeted Mode Allocation である。これは、各ストランドについて、複数の計算モードを選んだ場合の誤差、破綻確率、計算コストを予測し、固定60FPS予算下で処理モードを割り当てる方式である。従来の距離ベースLODや単純なリスク閾値とは異なり、各モードを選択した場合の結果を反実仮想的に評価する点に特徴がある。

第三の貢献は、Self-Materializing Strand Physics Islands である。これは、接触や編集によって危険になったpersistent strand ID集合を、一時的な局所物理空間として生成する方式である。提案するislandは、ガイドクラスタや固定空間分割ではなく、実ストランドの近接グラフに基づく集合として定義され、merge、split、budget割当、commit判定を持つ。

第四の貢献は、Certificate-Driven Rod Repair Calculus である。これは、破綻したストランドまたは局所セグメント帯に対して、長さ、曲げ、ねじれ、貫通残差からなるcertificateを定義し、その値に基づいて修復範囲、iteration数、成功判定を決定する方式である。これにより、full rod solverを全髪に適用することなく、必要な局所領域だけをJITに修復できる。

第五の貢献は、GPU-Resident Strand Runtime である。これは、上記のmicrotype、mode allocation、physics island、rod repairをGPU上で閉ループ化し、CPU readbackを極力避けながら実行するためのランタイムである。この実行基盤により、risk計算、mode決定、compaction、island生成、repair task生成、indirect dispatchをGPU上で行うことを目指す。

以上の貢献により、本研究は髪シミュレーションを、全ストランドを一様に解く問題から、ストランドごとにデータ量と物理処理を実行時に制御する問題へと再定義する。

---

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

---

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

---

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

---

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

---

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

---

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

---

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

---

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

---

# 2章 参考URL一覧

第2章の関連研究で参照する主な論文・プロジェクトページ・実装資料の一覧である。BibTeX化する場合は、この一覧をもとに正式な出版情報へ置き換える。

## Guide/Follower型・GPU髪シミュレーション

- Real-time Hair Simulation on GPU  
  https://www.ha.ai.kyutech.ac.jp/research/hair/index.html

- Real-time Hair Simulation with Efficient Hair Style Preservation PDF  
  https://giordi91.github.io/papers/Real-timeHairSimulationwithEf%EF%AC%81cientHairStylePreservation.pdf

- Real-time Hair Simulation with Efficient Hair Style Preservation video  
  https://www.youtube.com/watch?v=r3PgxbTi3OA

- AMD TressFX  
  https://gpuopen.com/tressfx/

- AMD TressFX GitHub  
  https://github.com/GPUOpen-Effects/TressFX

## Neural Hair Simulation

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

## Dense Physics Hair Simulation

- Augmented Mass-Spring model for Real-Time Dense Hair Simulation  
  https://arxiv.org/abs/2412.17144

- Augmented Mass-Spring Model for Real-Time Dense Hair Simulation ICCV 2025 PDF  
  https://openaccess.thecvf.com/content/ICCV2025/papers/H._Augmented_Mass-Spring_Model_for_Real-Time_Dense_Hair_Simulation_ICCV_2025_paper.pdf

- AMS project video  
  https://www.youtube.com/watch?v=vxY-jMUpYfA

- Interactive Hair Simulation on the GPU using ADMM  
  https://dl.acm.org/doi/fullHtml/10.1145/3588432.3591551

## Rod-based Hair / Cosserat Rod

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

## Hair Compression / Neural Hair Representation

- Neural Strands: Learning Hair Geometry and Appearance from Multi-View Images  
  https://arxiv.org/abs/2207.14067

- Neural Strands ECCV 2022 PDF  
  https://www.ais.uni-bonn.de/papers/ECCV_2022_Rosu.pdf

- Neural Strands ACM/Springer page  
  https://dl.acm.org/doi/10.1007/978-3-031-19827-4_5

- NeuWigs: A Neural Dynamic Model for Volumetric Hair Capture and Animation  
  https://arxiv.org/abs/2212.00613

## GPU-based Hair Runtime / Engine Documents

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

- AMD TressFX 5.0 for Unreal Engine 5 update  
  https://gpuopen.com/learn/tressfx-5-0-unreal-engine-5-update/

- TressFX 5.0 Developer Guide PDF  
  https://gpuopen.com/download/TressFX5.0_DeveloperGuide.pdf

---

# 3.1 対象とする問題

本研究では、ゲームおよびアニメーション制作を想定した、高密度で編集可能な髪のリアルタイム物理処理を対象とする。具体的には、十万本規模のストランドを持つ髪型に対して、固定60FPS制約下で、表示、編集、接触処理、局所物理化、破綻修復を行う問題を扱う。ここでいうリアルタイムとは、単に平均フレーム時間が短いことではなく、p95およびp99のフレーム時間を含めて、制作中のインタラクションを妨げない安定性を持つことを意味する。

対象とする髪は、頭皮メッシュ上のroot bindingを持つストランド集合として表現される。各ストランドは、rest形状、幅、root位置、root法線、編集状態を持ち、必要に応じて動的状態や物理証明値を更新する。本研究では、髪を単なる描画用ジオメトリとしてではなく、個別の状態を持つpersistent strand objectとして扱う。このため、各ストランドはIDを持ち、前フレームの状態、現在の計算モード、危険度、certificate、編集フラグなどを保持できるものとする。

本研究が扱う問題は、すべての髪を高精度にシミュレーションすることではない。十万本規模の髪に対して、全ストランドを同じ物理精度で毎フレーム解くことは、メモリ使用量、GPU帯域、計算時間の観点から現実的ではない。そこで本研究では、各ストランドに対して、低コストな処理、高精度な局所物理、証明付き修復などの複数の処理モードを用意し、フレームごとの状況に応じて必要な処理を割り当てる。

以上をまとめると、本研究が対象とする問題は、十万本規模の髪を、限られたメモリと固定フレーム予算の中で、ストランド単位の状態を維持しながら、必要な箇所だけ高精度な物理処理へ昇格させる問題である。本研究ではこれを、strand-native virtualized hair physics problem として定義する。

---

# 3.2 想定する入力と出力

本研究で想定する入力は、髪シミュレーションおよび制作中の編集に必要な情報から構成される。基本となる入力は、髪アセット、頭皮メッシュ、ストランドのrest形状、root binding、各点の幅情報、衝突対象、カメラ情報、編集コマンドである。これらを用いて、各ストランドの初期状態を構築し、フレームごとに必要な物理処理を決定する。

髪アセットは、複数のストランドから構成される。各ストランドは、点列またはセグメント列として与えられ、頭皮メッシュ上のroot bindingに関連付けられる。root bindingには、対象となる三角形インデックス、バリセントリック座標、root位置、root法線などが含まれる。これにより、頭皮の変形や頭部運動に対して、各ストランドの基準位置を更新できる。

衝突対象としては、頭部、肩、首、衣服、アクセサリ、手などが考えられる。これらは、球、カプセル、SDF、三角形メッシュなどの形式で与えられる。本研究では、衝突対象との距離や貫通量を用いて、ストランドごとの接触リスクや修復必要性を評価する。また、制作中の編集入力として、局所的な移動、回転、長さ変更、曲率変更、束の編集などを想定する。

本研究の出力は、描画用の髪ジオメトリだけではない。各フレームの出力には、更新されたストランド状態、圧縮表現、mode decision、physics island task、rod repair task、certificate、性能ログが含まれる。描画用には、必要なストランドまたはセグメントがdecodeされ、render vertex bufferとして出力される。物理処理用には、各ストランドのrisk、certificate、mode、island ID、repair結果が更新される。

---

# 3.3 要求条件

本研究では、制作現場で利用可能な高密度髪物理ランタイムを目指すため、以下の要求条件を設定する。

第一の要求は、十万本規模のストランドを扱えることである。高品質なキャラクター表現では、多数の髪を表示し、局所的な束感や接触を表現する必要がある。そのため、本研究では、十万本規模の髪を対象とし、各ストランドがpersistent IDを持つことを前提とする。

第二の要求は、固定60FPS制約を満たすことである。60FPSでは、1フレームあたりの時間予算は約16.67msである。本研究では、平均フレーム時間だけでなく、p95、p99、最悪フレーム時間も評価対象とする。制作中のインタラクションでは、短時間のフレーム落ちでも操作感に影響するため、フレーム時間の安定性を重視する。

第三の要求は、メモリ使用量とGPU帯域を削減することである。十万本規模の髪をfloat32点列として保持すると、位置情報だけでも大きなメモリを必要とする。さらに、動的状態、速度、証明値、衝突情報を保持すると、VRAMと帯域の負荷が増える。そのため、本研究では、髪専用の圧縮データ型を導入し、bytes per strandを削減することを要求する。

第四の要求は、局所編集に対応することである。制作現場では、髪全体ではなく一部の髪束やストランド集合を編集することが多い。そのため、編集後の局所形状を保持し、周囲と自然に接続し、必要に応じて物理的に修復できる必要がある。

第五の要求は、接触および貫通を抑制することである。髪は頭部、肩、衣服、アクセサリなどと接触するため、局所的な貫通や不自然な跳ねを検出し修正する必要がある。本研究では、危険なストランド集合だけを局所物理化することで、全体計算を避けながら接触品質を高めることを要求する。

第六の要求は、破綻を検出し、修復結果を定量的に判定できることである。単に見た目を補正するだけでは、修復が成功したかどうかを判断できない。そのため、本研究ではcertificateを導入し、長さ、曲げ、ねじれ、貫通などの残差に基づいて安全性を評価する。

第七の要求は、CPU依存を削減することである。十万本規模の状態をCPUへ毎フレームreadbackすると、CPU-GPU同期によりフレーム時間が不安定になる。そのため、本研究では、risk計算、mode allocation、island生成、repair task生成、dispatch argument生成を可能な限りGPU上で行う。

---

# 3.4 現行プロジェクトの課題

本研究は、既存の現行プロジェクトを基盤として進める。現行プロジェクトでは、髪アセットの読み込み、ストランド補間、衝突判定、LOD、メトリクス、キャッシュ処理、CSV/Binary I/Oなど、多くの基礎機能がすでに実装されている。また、これまでの整理により、重複していた数式処理、補間処理、I/O処理、幾何処理の一部は共通化されている。そのため、現行プロジェクトは、本研究の実装基盤として十分な機能を持つ。

一方で、研究としての新規性と制作現場での要求を満たすには、現行プロジェクトにはまだ複数の課題がある。第一の課題は、データ表現である。現行のストランド表現は、基本的に点列やfloat系の状態を中心としており、十万本規模の髪を扱う場合、メモリ使用量とGPU帯域が大きくなる。圧縮や共通化によるコード整理は進んでいるが、髪専用の物理保証付きデータ型にはなっていない。

第二の課題は、計算モードの割当である。現行プロジェクトにはLODやriskに基づく処理は存在するが、各ストランドに対して複数の計算モードを選んだ場合の誤差、破綻確率、計算コストを反実仮想的に予測し、固定フレーム予算下で割り当てる仕組みはまだ十分ではない。

第三の課題は、局所物理化である。現行プロジェクトには衝突やLOD処理の基盤はあるが、危険なpersistent strand ID集合を、フレームごとに生成、結合、分割、消滅するphysics islandとして扱う仕組みは未完成である。

第四の課題は、修復の定量的な成功判定である。局所的な破綻に対して補正を行うだけでは、修復が成功したかどうかを次フレーム以降の制御に利用しにくい。長さ、曲げ、ねじれ、貫通残差を統合したcertificateを定義し、その値に基づいて修復範囲、iteration数、成功判定、fallbackを制御する必要がある。

第五の課題は、GPU常駐制御である。十万本規模の髪では、CPU側で全ストランドの状態を読み戻して判断することは現実的ではない。現行プロジェクトを制作現場向けの高密度リアルタイム髪ランタイムへ発展させるには、risk計算、mode allocation、island生成、repair task生成、dispatch argument生成をGPU上で行い、CPU依存を極力減らす必要がある。

---

# 3.5 設計方針

本研究の基本方針は、髪を全体として一様に解く対象ではなく、ストランドごとにデータ量と物理処理を割り当てる対象として扱うことである。十万本規模の髪では、すべてのストランドを同じ精度で保持し、同じ物理処理を毎フレーム実行することは効率的ではない。そこで本研究では、各ストランドがpersistent ID、圧縮表現、状態、危険度、certificate、計算モードを持つものとし、状況に応じて必要な処理だけを実体化する。

第一の設計方針は、データ表現を変えることである。従来のようにストランドをfloat32点列として保持するのではなく、root frame、arc-length、curvature、torsion、residual、certificateを持つPhysics-Certified Strand Microtypeとして保持する。これにより、メモリ使用量とGPU帯域を削減しながら、圧縮表現上で物理的な安全性を評価できるようにする。

第二の設計方針は、計算モードの選択を最適化問題として扱うことである。各ストランドに対して、skip、QSOnly、DynamicResidual、PhysicsIsland、RodRepairなどのモードを用意し、それぞれを選んだ場合の誤差、破綻確率、計算コストを予測する。そして、固定フレーム予算内で全ストランドの処理モードを割り当てる。

第三の設計方針は、局所物理を自己実体化させることである。全髪を高精度物理で解くのではなく、接触や編集によって危険になったpersistent strand ID集合だけを、Self-Materializing Strand Physics Islandsとして生成する。

第四の設計方針は、局所破綻をcertificateに基づいて修復することである。破綻したストランドまたはセグメント帯に対して、長さ、曲げ、ねじれ、貫通残差からなるcertificateを計算し、その値に基づいて修復範囲、iteration数、成功判定を決定する。

第五の設計方針は、GPU上で閉ループ化することである。十万本規模のストランド状態をCPUへ毎フレームreadbackすることは避ける必要がある。そのため、risk計算、mode allocation、compaction、island生成、repair task生成、dispatch argument生成をGPU上で行い、CPU依存を極力減らす。

---

# 3.6 本研究で扱う5つの構成要素

本研究では、十万本規模の編集可能髪を固定60FPS制約下で扱うために、五つの構成要素を導入する。これらは個別にも意味を持つが、統合することで、髪をストランドごとに制御可能な仮想化物理対象として扱うランタイムを構成する。

第一の構成要素は、Physics-Certified Strand Microtypeである。これは、髪を通常のfloat32点列として保持するのではなく、root frame、arc-length、curvature、torsion、residual、certificateを持つ圧縮データ型として表現するものである。このmicrotypeは、単にデータ量を削減するための量子化ではなく、圧縮表現上で長さ、曲げ、ねじれ、decode誤差を評価し、物理的な安全性をcertificateとして保持できる点に特徴がある。

第二の構成要素は、Counterfactual Budgeted Mode Allocationである。これは、各ストランドについて、複数の計算モードを選択した場合の誤差、破綻確率、計算コストを予測し、固定フレーム予算下で処理モードを割り当てる方式である。単純なLODやリスク閾値ではなく、各modeを選んだ場合の反実仮想的な結果に基づいて処理を決定する。

第三の構成要素は、Self-Materializing Strand Physics Islandsである。これは、接触、編集、破綻予測によって危険と判定されたpersistent strand ID集合を、一時的な局所物理空間として生成する方式である。islandは、ガイドクラスタや固定空間分割ではなく、実ストランドの近接グラフに基づいて定義され、merge、split、solver budget、commit certificateを持つ。

第四の構成要素は、Certificate-Driven Rod Repair Calculusである。これは、破綻したストランドまたは局所セグメント帯を、長さ、曲げ、ねじれ、貫通残差に基づいて修復する方式である。repairの対象範囲、iteration数、成功判定はcertificateによって決定され、修復が失敗した場合はPhysics Islandやfallbackへ戻す。

第五の構成要素は、GPU-Resident Strand Runtimeである。これは、前述の四つの構成要素をGPU上で閉ループ化する実行基盤である。risk計算、mode allocation、compaction、island生成、repair task生成、dispatch argument生成をGPU上で行うことで、CPU readbackを極力避け、フレーム時間の安定性を高める。

これら五つの構成要素は、役割が異なる。Microtypeはデータ量を削減し、Mode Allocationは計算予算を割り当て、Physics Islandsは局所接触を解決し、Rod Repairは残った局所破綻を修復し、GPU Runtimeは全体を閉ループとして実行する。

---

# 3.7 本研究の新規性の整理

本研究の新規性は、髪シミュレーションを単に高速化することではなく、十万本規模の髪をストランドごとに管理し、メモリ解像度と物理解像度を実行時に割り当てる枠組みとして再定義する点にある。本研究では、四つの主新規性と一つの副新規性を設定する。

第一の主新規性は、Physics-Certified Strand Microtypeである。従来の圧縮表現や量子化は、主に形状やappearanceを少ないデータ量で再構成することを目的としている。これに対し、本研究のmicrotypeは、圧縮表現上で長さ、曲げ、ねじれ、decode誤差を評価し、certificateとして保持する。したがって、単なる低ビット表現ではなく、物理的安全性を持つ髪専用のデータ型である点に新規性がある。

第二の主新規性は、Counterfactual Budgeted Mode Allocationである。従来のLODやactive setは、距離、可視性、リスクなどに基づいて処理量を削減することが多い。これに対し、本研究では、各ストランドについて複数の計算モードを選んだ場合の誤差、破綻確率、計算コストを予測し、固定60FPS予算下でmodeを割り当てる。このように、計算モード選択を反実仮想的な制約付き最適化として扱う点に新規性がある。

第三の主新規性は、Self-Materializing Strand Physics Islandsである。従来の局所処理やactive setは、あらかじめ定義された領域や重要度に基づくことが多い。これに対し、本研究では、危険なpersistent strand ID集合が、近接グラフに基づいて一時的なphysics islandとして自己実体化する。islandは、merge、split、budget、commit certificateを持ち、実ストランド集合として扱われる。

第四の主新規性は、Certificate-Driven Rod Repair Calculusである。従来のロッドソルバは、髪や繊維状物体を高精度に解くための物理モデルとして用いられることが多い。これに対し、本研究では、ロッドソルバを全体に常用するのではなく、破綻した局所セグメント帯に対してJITに適用し、その修復範囲、iteration数、成功判定をcertificateで制御する。修復を定量的な証明値に基づくcalculusとして扱う点に新規性がある。

副新規性は、GPU-Resident Strand Runtimeである。GPUを用いた髪処理自体は既存研究や実装にも見られるが、本研究では、microtype decode、certificate評価、risk計算、mode allocation、island生成、repair task生成、indirect dispatchをGPU上で閉ループ化する。これにより、CPU readbackを極力避け、十万本規模の髪を固定フレーム予算下で扱う実行基盤を構築する。

以上の新規性は、それぞれ独立した技術要素として意味を持つ。しかし、本研究の最も重要な点は、それらを統合することで、髪を全体ソルバで一様に解く対象から、ストランドごとにデータ量と物理処理を実行時に割り当てる対象へと変えることである。

---
