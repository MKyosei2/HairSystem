
## Visual Studio 2022 native solution

This repository now includes a native Visual Studio solution in addition to CMake:

- `HairSystem_GPUBase.sln`
- `src/hairgpu_core/hairgpu_core.vcxproj`
- `apps/dx11_viewer/hairgpu_dx11_viewer.vcxproj`

Recommended first step on Windows:

1. Open `HairSystem_GPUBase.sln`
2. Select `Debug | x64`
3. Set `hairgpu_dx11_viewer` as the startup project
4. Build and run

The viewer project copies `HairSim.hlsl`, `HairRibbon.hlsl`, and `HairTube.hlsl` to the output directory automatically and uses `./` as the shader search path while debugging.

Helper scripts:

- `build_scripts/OpenInVS2022.bat`
- `build_scripts/Build_Debug_x64.bat`

# HairSystem GPU Base

GPU 物理計算を前提にした髪システムのベースプロジェクトです。Visual Studio + CMake を起点に、将来的に Unreal Engine / Maya / Blender 側のアダプタを追加できるように、**ライブラリ層**と **DX11 サンプルビューア**を分離しています。

## この ZIP で意図していること

このリポジトリは「完成品」ではなく、**本格開発を始めるための整理済みベース**です。元の ZIP にあった CPU ミニソルバと GPU 実験コードの二重構造は捨て、以下を優先して再構成しています。

- GPU シミュレーションを中心に据えた設計
- guide / follower / root bind / widths / scalp mesh を持てるデータモデル
- 3D カメラ + GPU リボン描画のサンプル
- tube preview 描画の入口
- GPU 上の guide simulation + follower reconstruction
- GPU root bind pass
- sample scalp skinning を使った bind 更新の受け皿
- **uniform grid broadphase 付き GPU 自己衝突**
- 近傍ベース legacy fallback を残した自己衝突切り替え
- **compliance 付きの XPBD-ready constraint path**
- 将来の Unreal / Maya / Blender 連携を見据えた `hairgpu_core` ライブラリ
- 旧プロトタイプ資産を含まない、配布しやすいクリーン構成

## 今回入っているもの

- `src/hairgpu_core`
  - エンジン非依存のコアデータ構造
  - sample scalp mesh
  - root binding 評価関数
  - guide + follower のサンプルグルーム生成
  - segment render data 生成
  - scalp skin weight のサンプル生成
- `apps/dx11_viewer`
  - Win32 + Direct3D11 サンプルビューア
  - GPU compute による guide hair simulation
  - GPU compute による follower reconstruction
  - GPU root bind
  - sample scalp skinning による頭皮アニメーション
  - sphere + capsule collider
  - uniform grid broadphase + GPU self collision
  - legacy 近傍ベース self collision fallback
  - Geometry Shader による 3D リボン描画
  - Geometry Shader による tube preview 描画
  - stretch / bend compliance の調整キー

## 今回あえて入れていないもの

- CPU ソルバ
- 旧 `hair_core` / `solver_lab` の試作コード
- Maya / Blender / Unreal 向け実運用アダプタ
- Alembic / USD I/O 実装
- voxel / hash / sort ベースへの broadphase 高度化
- scalp mesh への production quality skeletal bind
- cards / meshes LOD

## ビルド

### Windows + Visual Studio 2022

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

実行ファイルは `build/apps/dx11_viewer/Debug/` 近辺に生成されます。シェーダーファイルは CMake により実行ディレクトリへコピーされます。

## ビューア操作

- 左ドラッグ: カメラ回転
- マウスホイール: ズーム
- Space: 再生 / 停止
- R: リセット
- 1: 毛束幅を細く
- 2: 毛束幅を太く
- 3: 自己衝突 ON/OFF
- 4: scalp skinning motion ON/OFF
- 5/6: 自己衝突半径を調整
- 7/8: stretch compliance を調整
- 9/0: bend compliance を調整
- G: grid broadphase self collision / legacy fallback を切替
- T: ribbon / tube preview を切替
- - / +: self collision grid cell size を調整

## 今のベースで重要な位置づけ

これは **完成版の髪システム** ではなく、次の本格実装を始めるためのベースです。

- 物理計算の主経路は GPU
- 正準表現は strand
- follower は「全部をフルシミュレーション」する代わりに、まず GPU 再構成で見せる
- 3D 表現は line ではなく ribbon / tube preview を起点にする
- scalp bind と self collision も GPU パスに寄せる
- XPBD への移行で必要になる compliance と lambda バッファの置き場を先に用意している

## 次にやるべきこと

1. broadphase を radix sort / hash grid 系へ高度化
2. scalp mesh の barycentric bind と production skeletal bind へ拡張
3. cache / groom I/O の追加
4. tube preview を本格 tube / cards / mesh LOD へ拡張
5. Unreal / Maya / Blender アダプタの追加

詳細は `docs/ARCHITECTURE.md` と `docs/IMPLEMENTED_NOW.md` にあります。


## Visual Studio troubleshooting

If you see `D8016: /ZI and /Gy- cannot be specified together`, use the included updated solution where projects explicitly set `DebugInformationFormat=ProgramDatabase (/Zi)`. That error prevents `hairgpu_core.lib` from being produced, which then causes `LNK1104` in the viewer.


## Added fast-track research GUI

The DX11 viewer now includes a right-side Win32 control panel so the project can be used as a small interactive hair-simulation tool instead of a keyboard-only sample.

Implemented in this pass:

- Play / Pause / Reset buttons
- Preset selection (`Rapunzel Preview`, `Calm Studio`, `Windy Demo`, `Heavy Grounded`, `Game-ish LOD`)
- Asset rebuild controls for guide count, followers per guide, and particles per strand
- Render mode combo (`Ribbon`, `Tube Preview`)
- Checkboxes for self collision, grid broadphase, scalp motion, collider debug, and scalp wireframe
- Sliders for width, gravity, damping, wind, solver iterations, follow stiffness, self collision radius/stiffness, grid cell size, stretch compliance, and bend compliance
- Save / Load settings to `.ini`
- Export current frame strand positions to `.csv`
- Runtime stats panel with FPS, frame time, sim time, and asset stats
- DX11 debug line drawing for collider wireframes and scalp wireframe

### GUI workflow

1. Run the DX11 viewer.
2. Use the right panel to pick a preset.
3. Change guide/follower/particle counts and click `Apply Asset`.
4. Tune solver values with the sliders.
5. Enable collider or scalp debug drawing when checking motion and contact.
6. Save a parameter setup with `Save Settings` or export the current strand positions with `Export CSV`.


## Continued fast-track additions

This pass adds a few more demo- and graduation-project-friendly features to the DX11 viewer:

- `Step` button and keyboard single-step (`N`) for paused inspection
- `Color Debug` combo (`Natural`, `Guide / Follower`, `Width Heatmap`)
- `Show Grid Bounds` and `Show Floor Guide` debug overlays
- `Export Perf CSV` to save accumulated frame and simulation timings
- runtime stats now show current simulation-step cost and averages over collected samples

### Extra shortcuts

- `N`: single-step while paused
- `C`: cycle color debug mode
- `F`: toggle floor guide
- `H`: toggle grid-bounds debug


## New import + debug features
- **Import Hair...** button in the DX11 viewer
- Real **curves CSV** import path
- **Alembic import hook** with SDK-required stub path
- **Guide / follower skeleton overlay**
- **Root markers** and **follow links** debug view

See `docs/ALEMBIC_AND_GUIDE_DEBUG.md` and `docs/sample_assets/import_curves_template.csv`.
