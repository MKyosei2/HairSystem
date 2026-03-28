# Implemented Now

## 実装済み

### GPU シミュレーション
- Verlet ベース積分
- GPU root bind pass
- **compliance 付き距離拘束（XPBD-ready path）**
- **compliance 付き bend smoothing（XPBD-ready path）**
- sphere collider
- capsule collider
- floor collider
- **uniform grid broadphase 付き GPU self collision**
- legacy 近傍ベース self collision fallback
- root 近傍の shock propagation / tether 補助

### GPU follower 処理
- guide strand と follower strand の分離
- follower ごとの parent guide metadata
- GPU compute による follower reconstruction
- rest offset ベースの clump 追従
- 簡易ノイズ追従
- follower root の scalp bind

### scalp / bind 系
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

- radix sort / hash / voxel broadphase への高度化
- production quality angular XPBD bend
- mesh / SDF collider
- animated scalp skinning / skeletal bind の本実装
- strand clustering / grooming tools
- Alembic / USD
- UE / Maya / Blender adapters
- cards / meshes LOD
- production cache format

## 重要な注意

この ZIP は **完全な髪システムそのものではなく、完成形へ進めるための GPU 中心ベース**です。  
ただし v6 では `guide simulation + follower reconstruction + GPU root bind + uniform grid broadphase self collision + XPBD-ready compliance path + sample scalp skinning` まで揃えてあり、次は broadphase 高度化 / 本格 skinning / I/O / production tube 化 に集中できる状態です。

## v7 additions

- Native Visual Studio 2022 solution and project files added
- DX11 viewer project now copies all HLSL files to the runtime output directory automatically
- Helper batch scripts added for opening and building the solution on Windows
