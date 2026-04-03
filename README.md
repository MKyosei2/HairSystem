
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

The viewer project copies only the active particle-runtime shaders (`HairSim.hlsl`, `HairParticle.hlsl`, `DebugLine.hlsl`, and `ScalpMesh.hlsl`) to the output directory automatically and uses `./` as the shader search path while debugging.

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
- **dense uniform grid + spatial hash + adaptive spatial hash + sorted grid を切替できる GPU 自己衝突 broadphase**
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
  - dense uniform grid / spatial hash / adaptive spatial hash / sorted grid 切替付き GPU self collision
  - legacy 近傍ベース self collision fallback
  - Geometry Shader による 3D リボン描画
  - Geometry Shader による tube preview 描画
  - stretch / bend compliance の調整キー
- **runtime angular XPBD bend**
- **bitonic sort + sorted cell start/end を使う runtime sorted-grid broadphase**

## 今回あえて入れていないもの

- CPU ソルバ
- 旧 `hair_core` / `solver_lab` の試作コード
- Maya / Blender / Unreal 向け実運用アダプタ
- Alembic SDK 連携による本格 import/export
- 完全 GPU prefix sum / radix sort への更なる最適化（現状は DX11 向け bitonic sorted-grid 実装）
- scalp mesh への production quality skeletal bind
- cards / meshes LOD

## ビルド

### Windows + Visual Studio 2022

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

実行ファイルは `build/apps/dx11_viewer/Debug/` 近辺に生成されます。シェーダーファイルは CMake により実行ディレクトリへコピーされます。

### CLI broadphase debug

```bat
hairgpu_cli broadphase-stats generated_scaffold\asset_normalized.hairbin 0.085 dense
hairgpu_cli broadphase-stats generated_scaffold\asset_normalized.hairbin 0.085 hash 32768
hairgpu_cli broadphase-stats generated_scaffold\asset_normalized.hairbin 0.085 adaptive 32768
hairgpu_cli broadphase-stats generated_scaffold\asset_normalized.hairbin 0.085 sorted
hairgpu_cli broadphase-export generated_scaffold\asset_normalized.hairbin broadphase_debug.csv 0.085 adaptive 32768 256
```

このコマンドは dense grid / spatial hash / adaptive spatial hash / sorted grid の occupied bins, radix-sorted cell ranges, candidate pairs, strand-aware culling, bucket occupancy を比較するための CPU 参照統計を出します。viewer 側では sorted mode に対応する **runtime bitonic sort + cell start/end broadphase** も追加済みです。


### CLI cache / scalp animation / XPBD authoring

```bat
hairgpu_cli haircache-rest generated_scaffold\asset_normalized.hairbin rest_pose.haircache 24 8
hairgpu_cli haircache-info rest_pose.haircache
hairgpu_cli sample-haircache-frame rest_pose.haircache 0.125 sampled_frame.hairbin 1
hairgpu_cli export-bone-anim-template generated_scaffold\asset_normalized.hairbin bones_anim.csv 16 24
hairgpu_cli build-scalp-anim-cache generated_scaffold\asset_normalized.hairbin bones_anim.csv scalp_anim.haircache 1
hairgpu_cli relax-angular-xpbd generated_scaffold\asset_normalized.hairbin xpbd_relaxed.hairbin 0.45 0.0005 8 1 0 0.12
```

この経路では **full-strand hair cache**, **CSV ベースの bone animation → scalp pose → animated hair cache**, **offline angular XPBD bend relaxation** をコア/CLI だけで検証できます。


## 複数PCでの最安・最速テスト配布

このリポジトリには、**内部テスト向けの最安・最速署名フロー**を追加しています。

- `docs/FASTEST_INTERNAL_TEST_SIGNING.md`
- `tools/signing/Create-InternalTestCodeSigningCert.ps1`
- `tools/signing/Install-InternalTestCodeSigningCert.ps1`
- `tools/signing/Sign-HairSystemBuild.ps1`
- `tools/signing/Create-HairSystemTestBundle.ps1`

要点だけ言うと、**自己署名のテスト用コード署名証明書を作り、署名済みビルド + 公開証明書を配る**流れです。これは内部テストでは最も安くて速いですが、**Smart App Control や組織管理の App Control / AppLocker を必ず通す方法ではありません**。詳細は `docs/FASTEST_INTERNAL_TEST_SIGNING.md` を見てください。

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
- G: GPU broadphase self collision / legacy fallback を切替
- B: dense grid / spatial hash / adaptive spatial hash / sorted grid broadphase を切替
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

1. sorted-grid broadphase を prefix-sum / radix sort / GPU debug readback までさらに高度化
2. scalp mesh の barycentric bind と production skeletal bind へ拡張
3. hair cache / animated scalp cache / groom I/O の追加
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
- `Ribbon` は GPU line-list + geometry shader、`Tube Preview` は GPU instanced tube 描画に変更済み（通常フレームでは CPU readback を行わない）
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


## CPU authoring / collision additions

This pass also adds core-library and CLI-side implementations for several previously missing production-path helpers:

- angular bend relaxation for offline cleanup / post import repair
- scalp mesh collision projection
- scalp SDF collision projection
- scalp SDF CSV export for inspection / debugging

New CLI commands:

```
hairgpu_cli relax-angular-bend <input> <output.hairbin> [maxBendRadians] [iterations] [smoothing] [preserveLengths=1] [affectRootAdjacent=1]
hairgpu_cli collide-scalp-mesh <input> <output.hairbin> [shellDistance] [stiffness] [iterations] [affectRoots=0] [allPoints=1]
hairgpu_cli collide-scalp-sdf <input> <output.hairbin> [resolution] [shellDistance] [stiffness] [iterations] [affectRoots=0] [allPoints=1] [padding]
hairgpu_cli export-scalp-sdf <input> <output.csv> [resolution] [padding]
```


## v8 additions

- **GPU debug line rendering**
  - collider wireframes, scalp wireframe, floor/grid bounds, guide/follower skeletons, root markers, and follow links now render from GPU buffers
  - no per-frame particle readback is needed for the normal debug overlay path
- **On-screen render statistics**
  - the runtime HUD now shows ribbon / voxel / tube draw statistics
  - current hair draw-call, vertex, primitive, instance, and GPU debug-line counts are visible in the UI
- `DebugLine.hlsl` was upgraded to consume structured debug-line descriptors plus live hair/scalp position buffers

- GPU scalp mesh draw now fetches animated scalp positions from `m_scalpPositions` in the vertex shader, so normal runtime rendering no longer needs a CPU-updated scalp mesh vertex stream.
- HUD now shows GPU timestamp timings (frame/sim/hair/debug) plus D3D11 pipeline statistics for hair and debug passes.
