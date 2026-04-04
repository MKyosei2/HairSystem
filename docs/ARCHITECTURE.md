# Architecture

## 目標

- 物理計算は GPU を主経路にする
- コアライブラリは DCC / エンジン非依存に保つ
- 表現は `strand curve` を正準にし、描画時に ribbon / tube / cards へ変換する
- リアルタイムでは guide simulation + follower reconstruction を基本にする
- root bind / self collision も GPU パス中心で発展させる
- self collision は uniform grid broadphase を起点に発展させる
- solver は **XPBD-ready** な形に寄せ、compliance と lambda バッファを持てる構造にする

## モジュール

### 1. hairgpu_core

責務:
- 髪アセットの正準データ表現
- シミュレーション設定
- コライダデータ
- ルートバインド情報
- ガイド毛 / フォロワー毛メタデータ
- scalp mesh と root bind の基礎関数
- scalp skin weight を含むテスト用アセット生成

### 2. Active-set planning layer

責務:
- guide strand を cluster 単位に束ねる
- motion / displacement / visibility / silhouette / uncertainty / contact proxy から cluster score を計算する
- フレーム予算に合わせて corrective physics 対象 cluster を選ぶ
- 既存 runtime に対して「どこだけ高価な補正を打つか」を渡す

### 3. dx11_viewer

責務:
- GPU バッファ作成
- Compute shader 実行
- root bind pass 実行
- guide simulation 実行
- follower reconstruction 実行
- uniform grid broadphase self collision
- legacy fallback self collision
- 3D ビュー表示
- デバッグ向けのリボン描画
- tube preview 描画
- sample scalp skinning の実験場

## データの考え方

### HairAsset
永続化対象。

- strands
- rest points
- point widths
- materials
- root bindings
- follow data
- scalp mesh
- scalp skin weights

### HairRuntime
実行時の GPU バッファとシミュレーション定数。

- current positions / previous positions
- rest positions
- follow metadata buffer
- scalp positions / triangles / root bindings
- distance / bend lambda buffer
- constants buffer
- collider set

### HairRenderSettings
見た目の切り替えパラメータ。

- ribbon / tube preview mode
- ribbon width scale
- shading controls
- future LOD settings

### HairSegmentRenderData
strand を GPU 描画向けの segment topology に変換した中間データ。

- particle index pair
- strand index
- future tube / card generator の共通入力

## 現在のランタイムフロー

1. sample scalp skinning を CPU 側で評価し GPU へ転送
2. root bind pass で各 strand root を GPU 上で再拘束
3. guide strands を GPU で積分
4. guide strands に compliance 付き距離拘束を反復適用
5. guide strands に compliance 付き bend smoothing を反復適用
6. guide strands に sphere / capsule / floor collision を適用
7. guide strands を uniform grid に binning
8. guide strands に grid broadphase self collision を適用
9. follower strands を GPU で parent guide から再構成
9. ribbon もしくは tube preview として描画

## このベースで未実装の主要機能

- active-set で選ばれた cluster に対する runtime corrective physics の GPU 実装
- detector に uncertainty / contact / penetration depth の実測入力を直結すること
- radix sort / hash / voxel broadphase への高度化
- production quality angular XPBD bend constraint
- animated scalp mesh の skeletal bind 本実装
- cache / Alembic / USD
- production tube renderer
- cards / meshes LOD
- UE / Maya / Blender アダプタ

## 将来の推奨追加順

1. `hairgpu_sim_gpu` を viewer から分離
2. self collision を radix sort / hash / voxel broadphase へ高度化
3. scalp mesh skinning + skeletal bind を production quality に拡張
4. `hairgpu_io` を追加
5. tube preview から production tube / cards 生成へ拡張
6. Unreal / Maya / Blender アダプタを追加

## Native Visual Studio layer

The repository now includes hand-authored Visual Studio 2022 solution and project files so Windows-first iteration does not depend on generating projects from CMake. The native `.sln` mirrors the same module split as CMake:

- `hairgpu_core` : static library
- `hairgpu_dx11_viewer` : Win32 DX11 sample viewer

This is intentionally additive. CMake remains the cross-platform build description for the core library, while `.sln/.vcxproj` gives a direct entry point for daily Visual Studio work.
