# 第5章　実装

## 5.1 実装環境

提案手法は，既存のHairSystemへ統合する形で実装した。実装対象はWindows上で動作するDirectX 11ベースのHairSystem Viewerであり，CPU側をC++，GPU側をHLSL Compute Shaderで構成する。

主な実装環境を表5.1に示す。

| 項目 | 内容 |
|---|---|
| 対象環境 | Windows |
| 開発環境 | Visual Studio 17系 |
| Platform Toolset | v143 |
| CPU側言語 | C++17 |
| GPU側言語 | HLSL |
| Graphics API | Direct3D 11 |
| Compute Shader | Shader Model 5.0 |
| Build | x64 Release |
| 主なLink Library | `d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib` |
| 固定時間刻み | 1/60 s |
| Solver反復数 | 6 |
| 正式評価解像度 | 1920 × 1080 |
| VSync | 無効 |

HairSystemの固定評価構成は，Guide 4,000本，Follower 96,000本，合計100,000本，1本当たり128点である。1本のGuideの隣接点間をSegment Groupとするため，Guide側のSegment Group総数は，

\[
4000 \times (128-1)=508000
\]

となる。

提案手法の主要な実装Source of Truthは次のとおりである。

- `include/hairgpu/SegmentGroupAdaptivePayload.h`
- `apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl`
- `apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl`
- `apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl`
- `apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl`

---

## 5.2 HairSystem全体構成

HairSystemは，大きくCPU制御部，GPU Simulation部，Follower生成部，Render部から構成される。

提案手法Pは`ExperimentMethodId::P_IntegratedSegmentGroup`として既存処理から分離している。P以外の経路では既存のLegacy処理を使用し，PのときだけSG-APT Runtimeを経由する。

全体構成を以下に示す。

```text
GpuHairViewer
│
├─ Input / Scenario / Edit
│
├─ Legacy Hair Runtime
│   └─ B0等の従来経路
│
└─ Novelty01RuntimeState
    ├─ Segment Group Core / Extension
    ├─ Block / Reuse Descriptor
    ├─ Static Cache
    ├─ Dynamic Payload × 4 Generation
    ├─ Payload Owner
    ├─ Dynamic / Static / Repair List
    ├─ Guide / Follower / Render Dirty Range
    ├─ Indirect Dispatch Arguments
    ├─ Ground Truth / Telemetry
    └─ Persistent Render Cache
```

提案手法の1Frame処理は，`ExecuteNovelty01ExperimentTick()`から`ExecuteNovelty01Complete()`へ入り，Runtimeの初期化確認，入力Version更新，状態分類，GPU List生成，部分Simulation，Dirty Range伝播，Follower更新，Render Cache更新の順に実行される。

Runtimeの初期化に失敗した場合や，必要なSG-APT状態を取得できない場合には，提案手法を無理に継続せず`StepFullDynamic()`へFallbackする。

---

## 5.3 CPU側処理

CPU側の主要な役割は，GPU上の全Segment Groupを逐次処理することではなく，Runtime Resourceの管理，入力状態のVersion化，Shader Dispatchの制御，Fast Path選択，Telemetryの回収である。

### 5.3.1 Runtime管理

`GpuHairViewer::Novelty01RuntimeState`は，SG-APTで使用するStructured Buffer，Compute Shader，Counter，Readback Resourceを保持する。

RuntimeはSimulation Reset VersionおよびTopology Versionを監視しており，これらが変化した場合は既存Runtimeを破棄して再構築する。これにより，古いTopologyに対応するGroup IndexやDependencyを誤って利用することを防ぐ。

### 5.3.2 入力Versionの管理

CPU側では，Root，Force，Collider，Topology，Edit，Dependency等の変化を検出し，`RuntimeConstants`へVersionを設定する。GPU側はこれらを前回値と比較し，BlockまたはGroupをDirty化する。

重要なのは，CPUが毎Frame508,000 Groupすべての状態を直接判定しないことである。細かな判定はGPU側で行い，CPUは変更原因とRuntime制御情報を供給する。

### 5.3.3 非同期Readback

Fast Path選択やTelemetry取得のためにGPU CounterをCPUへ戻す必要があるが，同期Readbackを毎Frame行うとGPU Pipelineを停止させる可能性がある。

そこで実装では複数のReadback SlotをRingとして保持し，発行から一定Frame経過したSlotについて`D3D11_MAP_FLAG_DO_NOT_WAIT`で取得を試みる。現在の実装では8 Slotを保持し，少なくとも4 Frame経過したReadbackを非同期に回収する。

取得できなかった場合はGPU完了を待ってCPUをBlockせず，次回へ持ち越す。

### 5.3.4 Fast Path選択

CPU側では，Readback済みCounterとGPU Pass時間のEWMAを用いてPartial経路とFull Dynamic経路を選択する。

主な条件は以下である。

```text
Dynamic Ratio高
OR Payload Overflow
OR Full Dynamic Request
OR Repair Failure
OR False Static
OR Topology/Edit/Dependencyの重大変更
OR Proposal GPU Time >= 0.98 × Full Dynamic GPU Time
```

また，常にFull Dynamicへ固定されProposal経路を再評価できなくなることを避けるため，一定周期でProposal Probeを行う。実装では120 Frame周期のProbeを持つ。

---

## 5.4 GPU側処理

GPU側の中心実装は`HairSegmentGroupAdaptivePayloadRuntime.hlsl`である。Direct3D 11 Compute Shaderでは1 Pass当たり使用可能なUAV数に制限があるため，1つのHLSLを複数のBinding ModeとしてCompileし，各PassのResource Bindingを分離している。

主要Binding Modeを表5.2に示す。

| Mode | 主な処理 |
|---:|---|
| 0 | Block構築，Invalidation，Descriptor生成，状態分類，Transition |
| 1 | Two-level Scan，Stable Compaction，Dynamic / Static / Repair List生成 |
| 2 | Payload管理，Static適用，Repair，Cache更新，Shadow Validation |
| 3 | Dynamic Guide Solver，Constraint，Collision，Self-Collision，Watchdog |
| 4 | Root Binding，Follower局所再構築 |
| 5 | Follower Dirty Flag生成 |
| 6 | Shared Point Candidate生成・安定Resolve |
| 7 | Payload Owner / Epoch検証，Retire，Reuse |
| 8 | Guide / Follower / Render Dirty Range伝播 |
| 9 | Ground Truth比較，不変条件検証 |
| 10 | Indirect Dispatch Argument生成，比較用Migration |
| 11 | Atomic Compaction比較経路 |
| 12 | Formal Scenario入力注入 |

各Pass終了後にはSRV，UAV，Constant Buffer，Compute ShaderをUnbindし，同一ResourceをSRVとUAVへ同時Bindしない。UAVは`u0`～`u7`の範囲に収める。

### 5.4.1 Block判定

`N01BuildBlocksCS`はBlockのBounds，最大移動量，Collision Margin，Residual，Boundary Error等を集約する。

`Feature_IncrementalBlocks`が有効で，

```text
block.validFlags != 0
AND block.dirtyFlags == 0
```

である場合，Block内部を再走査せず`BlockSkipped` Counterを増加させてReturnする。

`N01InvalidateBlocksCS`ではRoot，Force，Topology，Edit，Dependency，Collider，Descriptor Age等からBlockをDirty化する。

### 5.4.2 Group分類

`N01DescribeClassifyCS`では，各Groupについて，

- Position Error
- Velocity Displacement
- Acceleration Displacement
- Root Translation
- Root Rotation
- Force Displacement
- Collider Displacement
- Collision Margin
- Length Error
- Boundary Error
- Descriptor Age
- Version整合性

を確認し，`StaticReuse`，`StaticTransform`，`Dynamic`，`Repair`，`ForcedDynamic`等の候補状態を決定する。

Cleanかつ有効なBlockではこの詳細判定自体を省略し，既存状態からList用Flagだけを生成する。

---

## 5.5 データ構造

SG-APTの主要データ型を表5.3に示す。

| 型 | サイズ | 主な役割 |
|---|---:|---|
| `SegmentGroupCore` | 64 B | 固定Group ID，Point範囲，Block，State，Dirty Flag |
| `CoreExtension` | 80 B | Payload Index/Version，各種Version，Transition履歴 |
| `StaticCache` | 64 B | Root Local形状，前回形状，Cache Version |
| `DynamicPayload` | 64 B | Velocity，Lambda，Residual，動的一時状態 |
| `PayloadOwner` | 32 B | Group所有者，Version，Generation，Epoch |
| `ReuseDescriptor` | 80 B | 再利用判定用誤差，Collision Margin，Hard/Soft Flag |
| `BlockDescriptor` | 64 B | Block範囲，Dirty，有効性，Bounds，最大誤差 |
| `GuideDirtyRange` | 16 B | GuideのDirty Segment範囲 |
| `FollowerDirtyRange` | 32 B | FollowerのDirty範囲，親Guide，Guard Band |
| `RenderDirtyRange` | 16 B | 描画更新Point範囲 |
| `FollowerDependencyEntry` | 32 B | Guide→Follower依存範囲 |

### 5.5.1 Fixed Core

`SegmentGroupCore`には，Topologyが変化しない限り固定されるGroup情報を格納する。主な値は，

- `strandId`
- `localGroupIndex`
- `startPointIndex`
- `endPointIndex`
- `parentGroupIndex`
- `childGroupIndex`
- `blockIndex`
- `restLength`
- `restDirection`
- `state`
- `dirtyFlags`

である。

Static / Dynamicが切り替わっても，このCoreのIndexは変更しない。

### 5.5.2 Dynamic Payload

`DynamicPayload`には動的Solverでのみ必要な値を格納する。

```text
previousEndPosition
distanceLambda
velocity
bendLambda
constraintResidual
collisionResidual
predictedPositionErrorMm
boundaryErrorMm
solverFlags
collisionFlags
allocationEpoch
retireEpoch
```

Static Groupでは有効なPayloadを持たないことを不変条件とする。

### 5.5.3 Payload Owner

`PayloadOwner`はPayload Slotの所有関係を管理する。Group Index，Payload Version，Generation，Allocation Epoch，Retire Epoch，Safe Reuse Epochを保持する。

Payload Generationは4世代を用い，Retire直後のSlotをGPU参照終了前に再利用しない。

---

## 5.6 Follower生成・更新処理

Follower更新はBinding Mode 4，5，8を中心として実装している。

### 5.6.1 Dirty Follower判定

`N01BuildFollowerFlagsCS`では各Followerの親Guideを取得し，次のいずれかに該当する場合にDirtyとする。

- Parent Guideが不正
- Root / Topology / Edit / Dependency等の全体Dirty条件がある
- Parent Guide内のいずれかのGroupが`StaticReuse`以外
- Parent GuideのGroupにDirty Flagが残る
- Baseline Fallback Flagがある

### 5.6.2 Dirty Range伝播

Binding Mode 8の`N01PropagateFollowerRenderRangesCS`では，Parent Guideの`GuideDirtyRange`をFollowerへ変換する。

Dependency Versionが一致する場合，Guide RangeへGuard Bandを加えた範囲をFollower Rangeとする。

```text
FollowerBegin = max(0, GuideBegin - GuardBand)
FollowerEnd   = min(MaxSegment, GuideEnd + GuardBand)
```

Dependencyが不正な場合は，

```text
FollowerBegin = 0
FollowerEnd   = LastSegment
```

として全範囲Fallbackする。

### 5.6.3 Follower局所再構築

`N01FollowerLocalCS`ではDirty Followerだけを対象とし，さらに`FollowerDirtyRange`内のPointだけを更新する。

各Pointでは，Parent Guideの現在位置，Rest Position，FollowerのRest Local Offsetを用い，

```text
Follower Rest Shape
+ Guide deformation × FollowWeight
+ Clump adjustment
+ Per-follower noise
```

からTarget Positionを生成する。

その後，前Frame速度，Damping，Response，Collision処理を適用し，Follower PositionとPrevious Positionを更新する。

このため，Follower単位の選択に加え，Follower内部のセグメント範囲まで更新を局所化している。

---

## 5.7 Block Certificate実装

本論文でBlock Certificateと呼ぶ機構は，実装では主に`BlockDescriptor`，`ReuseDescriptor`，`CoreExtension`のVersion群，Dirty Flagによって構成している。

### 5.7.1 BlockDescriptor

`BlockDescriptor`は次の値を持つ。

```text
firstGroupIndex
groupCount
dirtyFlags
validFlags
boundsMin / boundsMax
maximumMotionMm
minimumCollisionMarginMm
maximumResidual
maximumBoundaryErrorMm
descriptorVersion
lastUpdatedFrame
```

Blockの再評価が完了すると`validFlags`を有効化し，Dirty FlagをClearする。

次Frameで新しいDirty Sourceが存在せず，Descriptorが有効な場合，`N01BuildBlocksCS`と`N01DescribeClassifyCS`はBlock内部の詳細再計算を省略できる。

### 5.7.2 Invalidation

`N01InvalidateBlocksCS`では，以下の条件からBlockを無効化する。

- Runtime Force Changed
- Topology Changed
- Edit Changed
- Dependency Changed
- Descriptor Version不一致
- Descriptor Maximum Age超過
- Swept Collider Boundsとの交差

Colliderについて，移動したColliderがBlockから十分離れている場合まで一律Dirtyにしないよう，Swept AABBとの交差判定を用いる。

### 5.7.3 Group側の再利用条件

BlockがDirtyで詳細判定が必要になった場合，`ReuseDescriptor`でGroup単位の状態を再判定する。

Hard FlagにはNon-Finite，Boundary，Topology，Edit，Dependency，Force，Collider，Payload等を含める。Hard Flagが存在する場合はStatic維持よりRepairまたはDynamicを優先する。

---

## 5.8 Fast Path / Slow Path実装

実装では，部分更新経路を**Slow Path / Proposal Path**，管理処理を省略して既存の全動的Solverを実行する経路を**Full Dynamic Fast Path**として切り替える。

### 5.8.1 Proposal Path

Proposal Pathでは概ね次の処理を行う。

```text
Block Invalidation
→ Descriptor / Classification
→ Scan / Compaction
→ State Transition
→ Payload Lifecycle
→ Static Transform / Dynamic Solve / Repair
→ Shared Point Resolve
→ Guide Dirty Range
→ Follower / Render Dirty Range
→ Follower Local Reconstruction
→ Render Cache Partial Update
```

動的領域が少ない場合は，この管理コストと引き換えにSolver，Follower，Renderの更新量を削減できる。

### 5.8.2 Full Dynamic Fast Path

以下の状況では`useFullDynamic`を有効にする。

- Warm-up中
- Full Dynamic Hold中
- Topology / Edit / Dependencyの重大変更
- Dynamic Ratioが閾値以上
- Payload Overflow
- `Counter_FullDynamicRequested`が発生
- Repair Failure
- False Static
- Proposal GPU時間がFull Dynamic GPU時間の98%以上

Fast PathではSG-APTの細かな部分更新管理を継続せず，全Guideを既存Dynamic Solverで処理する。

### 5.8.3 Hysteresis

動的率の基本閾値は，

```text
Enter Full Dynamic : 0.70
Exit Full Dynamic  : 0.55
```

とする。

さらに，重大な変更や異常検出直後はHold Frameを設定し，1 FrameごとにPartial / Full Dynamicが往復することを避ける。

---

## 5.9 描画処理

描画連携は`Novelty01RenderCacheState`によるPersistent Render Position Cacheとして実装している。

### 5.9.1 Persistent Render Cache

`Novelty01RenderCacheState`は，

- `renderPositions`
- Constant Buffer
- Guide Copy Compute Shader
- Follower Copy Compute Shader
- Point Count
- Simulation Reset Version
- Topology Version

を保持する。

Proposal経路では，Simulation Positionを毎Frame全コピーするのではなく，Render Cacheを前Frameから継続保持する。

### 5.9.2 部分更新

現在の実装では，Proposal FrameごとにGuide点はRender Cacheへ反映する。一方Followerについては`RenderDirtyRange`を参照し，DirtyなPoint Rangeだけを更新する。

更新されなかったFollower RangeはPersistent Buffer内の前回値を保持する。

### 5.9.3 Full Copy条件

以下の場合は部分更新せず`CopyResource`でPosition Buffer全体をRender Cacheへコピーする。

```text
Render Cache未初期化
OR RenderDirtyRange Feature無効
OR 前FrameがFull Dynamic
OR Render Fallback
OR Dependency Fallback
```

したがって，描画側でも不完全なDirty情報によって古い位置が残ることを避けるFail-safe構成としている。

### 5.9.4 既存Rendererとの接続

最終描画では既存`RenderLegacy()`を再利用する。P経路では，描画直前に現在のPosition BufferとPersistent Render Cacheを一時的に入れ替え，既存RendererへRender Cacheを参照させ，描画後に元へ戻す。

これにより既存のRibbon等の描画実装を全面的に書き換えず，SG-APTのDirty Rangeを描画入力へ接続している。

---

## 5.10 実装上の制約

本実装には以下の制約がある。

### 5.10.1 Direct3D 11のResource制約

Compute Shader 5.0では使用可能なUAV数が限られる。このためSG-APT処理をBinding Modeへ分割し，各Passで`u0`～`u7`以内に収めている。

また，同一Resourceを同時にSRVとUAVへBindしないため，各Pass終了後にCompute Shader，SRV，UAV，Constant Bufferを明示的にUnbindする必要がある。

### 5.10.2 Fixed Group Index

SG-APTはTopologyが不変な間，Segment Group Indexが固定であることを前提とする。Topologyが変更された場合は既存のCore，Dependency，Cache，Payload対応をそのまま利用できないためRuntime再構築が必要になる。

### 5.10.3 Payload Pool容量

Dynamic Payloadは固定容量Poolとして実装している。正式設定ではPayload Capacityを全Group数の50%とする。Dynamic GroupがPool容量を超える場合，不正Indexを割り当てずFull Dynamic Fallbackへ移る。

このためPayload容量を小さくするとVRAM使用量は減るが，動的領域が広いSceneではFallback率が上昇する。

### 5.10.4 Delayed Reuse

Payloadは複数Generationで管理するため，RetireしたSlotを即時再利用できない。現在の基本設定では4 Generation，Retire Delay 4 Frameである。

これはGPU上の古い参照との競合を避けるために必要な制約である。

### 5.10.5 Dirty Rangeの正当性

Follower / Render部分更新はDependency Versionが正しいことを前提とする。Versionが一致しない場合は局所更新を継続せず，全Follower Rangeまたは全Render Buffer更新へFallbackする。

したがってDependency情報を持たないAssetへそのまま局所更新を適用することはできない。

### 5.10.6 Render側Guide更新

現実装のPersistent Render Cacheでは，FollowerはDirty Rangeのみ更新する一方，Guide点はProposal FrameごとにRender Cacheへコピーする。したがってRender更新量をGuide点まで完全にDirty Range化した実装ではない。

本研究の主対象はGuide 4,000本に対してFollower 96,000本を持つ構成であり，特にFollower側の全更新を抑えることを優先している。

### 5.10.7 管理オーバーヘッド

SG-APTではBlock判定，Descriptor生成，Scan / Compaction，Payload管理，Dirty Range伝播が追加される。このため動的領域が大きいSceneでは必ずしも部分更新が有利とは限らない。

この制約に対し，本実装ではFull Dynamic Fast Pathを設け，部分更新が不利な状態では既存Solverへ戻る。

### 5.10.8 Formal条件と一般化

本実装は，100,000本，Guide 4,000本，Follower 96,000本，128点/strand，1920×1080，1/60秒を正式評価条件としている。

したがって本章で示す実装構成はこれらの条件で動作するよう検証しているが，異なるGPU，異なる髪密度，異なる点数，異なる描画方式に対して同じ性能になることを保証するものではない。

以上の制約を踏まえ，本実装では，局所更新を適用できる場合のみSG-APTを利用し，安全性または処理効率を保証できない場合はFull Dynamicまたは全範囲更新へFallbackする構成とした。
