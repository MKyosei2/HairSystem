# Implemented Now

## 実装済み

### Active-set foundation
- **guide-cluster 生成 + budget 選別付き active-set planning layer**
- **motion / displacement / visibility / silhouette / uncertainty / contact proxy をまとめた cluster score**
- **corrective physics 候補 cluster の JSON export と CLI 経路**

### GPU シミュレーション
- Verlet ベース積分
- GPU root bind pass
- **compliance 付き距離拘束（XPBD-ready path）**
- **compliance 付き bend smoothing（XPBD-ready path）**
- sphere collider
- capsule collider
- floor collider
- **dense uniform grid broadphase 付き GPU self collision**
- **spatial hash broadphase 付き GPU self collision**
- **adaptive spatial hash broadphase 付き GPU self collision**
- **sorted-grid + bitonic sort + cell start/end 付き runtime GPU self collision**
- **radix sort + cell start/end CPU 参照 broadphase**
- legacy 近傍ベース self collision fallback
- root 近傍の shock propagation / tether 補助

### GPU 可視化

- **Ribbon preview は GPU line-list + geometry shader で描画**
- **Tube Preview は GPU instanced tube 描画で描画**
- 通常の Ribbon / Tube Preview では **CPU readback に戻さない**

### GPU follower 処理
- guide strand と follower strand の分離
- follower ごとの parent guide metadata
- GPU compute による follower reconstruction
- rest offset ベースの clump 追従
- 簡易ノイズ追従
- follower root の scalp bind

### scalp / bind 系
- offline scalp collision tools (closest-surface + SDF)
- offline angular bend relaxation tool
- **offline angular XPBD bend relaxation tool**
- **runtime angular XPBD bend**
- **full-strand hair cache read/write + time sampling**
- **CSV bone animation clip export/template + animated scalp hair cache build**
- scalp mesh データ
- root bind metadata
- root binding 評価関数
- **sample scalp skin weights**
- **sample 2-bone scalp skinning**
- animated scalp motion 用の GPU 入力バッファ
- root bind を毎フレーム GPU 上で再評価するパス

### 3D 描画
- orbit camera
- geometry shader による camera-facing ribbon
- geometry shader による tube preview
- 簡易ライティング

### 描画データ
- core 側の segment render data 生成
- viewer から独立した strand segment トポロジ生成

### コアデータ
- strand ranges
- point positions
- widths
- guide/follower metadata
- sample scalp mesh 生成
- sample hybrid groom 生成
- scalp skin weight のサンプル生成

## 未実装

- active-set で選ばれた cluster へ corrective physics pass を runtime で差し込む最終統合
- production 用の完全 GPU radix sort / cell start-end / prefix sum 最適化
- mesh / SDF collider の runtime GPU 統合
- animated scalp skinning / skeletal bind の production quality 実装
- strand clustering / grooming tools
- Alembic SDK / USD 本格連携
- UE / Maya / Blender adapters
- cards / meshes LOD

## 重要な注意

この ZIP は **完全な髪システムそのものではなく、完成形へ進めるための GPU 中心ベース**です。  
ただし現状でも `guide simulation + follower reconstruction + GPU root bind + dense/spatial-hash/adaptive-hash/sorted-grid broadphase self collision + runtime angular XPBD bend + radix-sorted CPU reference + strand-aware pair culling + XPBD-ready compliance path + offline angular XPBD cleanup + hair cache + animated scalp cache + sample scalp skinning` まで揃っており、次は完全 GPU radix / prefix sum 化、本格 runtime skinning、Alembic/USD 連携、production tube 化 に集中できる状態です。

## v7 additions

- Native Visual Studio 2022 solution and project files added
- DX11 viewer project now copies all HLSL files to the runtime output directory automatically
- Helper batch scripts added for opening and building the solution on Windows


## Added now: GPU debug overlays + render HUD

- GPU debug line rendering for:
  - floor guide
  - grid bounds
  - scalp wireframe
  - sphere / capsule collider wireframes
  - guide skeleton
  - follower skeleton
  - root markers
  - follower-to-guide links
- The normal debug path no longer depends on particle readback.
- The viewer HUD now exposes:
  - current draw calls
  - current hair vertices / primitives / instances
  - ribbon segment counts
  - voxel block / triangle estimates
  - tube template vertex counts and total triangle estimates
  - GPU debug line counts by category

- GPU scalp mesh draw now fetches animated scalp positions from `m_scalpPositions` in the vertex shader, so normal runtime rendering no longer needs a CPU-updated scalp mesh vertex stream.
- HUD now shows GPU timestamp timings (frame/sim/hair/debug) plus D3D11 pipeline statistics for hair and debug passes.
