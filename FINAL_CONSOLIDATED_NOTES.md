# Consolidated Final Markdown Notes

This file consolidates the Markdown files found in the current workspace and the uploaded PATCH_MANIFEST.zip.

Policy used for consolidation:
- identical-content duplicates were merged
- original filenames were preserved in each section header
- file bodies were kept as-is except for section separators

## Included source files

- README.md
- README_FIXED_COMPLETE_VISFIX5.md
- STAGE7_ACTIVESET_COMPARE_AND_RUNTIME_METRICS.md
- STAGE8_INTEROP_AND_ABC_COMPAT_EXPORT.md
- TEMPORAL_NEURAL_STAGE.md
- TIERED_RUNTIME_AND_ABC_COMPAT_STAGE.md
- STAGE9_PRIORITY3_IMPLEMENTATION_NOTES.md (duplicates merged: STAGE9_PRIORITY3_IMPLEMENTATION_NOTES.md)
- STAGE10_ALEMBIC_COMPLETION_NOTES.md (duplicates merged: STAGE10_ALEMBIC_COMPLETION_NOTES.md)

## Source: README.md


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


## Source: README_FIXED_COMPLETE_VISFIX5.md

HairSystem VISFIX5 complete source package

What changed:
- GpuHairViewer now recomputes asset bounds and auto-frames the camera after startup, reset, rebuild, and import.
- Render path estimates visible particle count every frame and triggers recovery when visibility looks broken.
- Window title now includes a build marker: "Hair GPU Viewer VISFIX5".
- Particle shader has a larger minimum radius and stronger visibility.

How to verify you are running the new build:
1. Clean the solution.
2. Delete bin, bin-int, and .vs if needed.
3. Rebuild on Windows.
4. Launch the rebuilt dx11 viewer EXE directly from the build output folder.
5. Confirm the title bar starts with: Hair GPU Viewer VISFIX5

Files changed:
- apps/dx11_viewer/src/GpuHairViewer.h
- apps/dx11_viewer/src/GpuHairViewer.cpp
- apps/dx11_viewer/shaders/HairParticle.hlsl
- bin/Debug/hairgpu_dx11_viewer/HairParticle.hlsl
- bin/Debug/hairgpu_dx11_viewer/shaders/HairParticle.hlsl


## Source: STAGE7_ACTIVESET_COMPARE_AND_RUNTIME_METRICS.md

# Stage 7: Active-set predictor, compare pipeline, and runtime metric completion

This stage pushes the remaining research-facing gaps that can be implemented in the current environment.

## Added in this stage

### 1) Learned active-set predictor
- Added `ActiveSetPredictor` to `HybridRuntime.h` / `HybridRuntime.cpp`
- Added predictor feature extraction from runtime signals:
  - contact score
  - penetration score
  - model error
  - motion / acceleration related signals
  - visibility / cluster extent context
- Added predictor inference and blend-driven score boost before `EvaluateActiveSetPlan(...)`
- Added predictor training during `TrainHybridModelBundle(...)`
- Added predictor save/load:
  - `active_set.predictorbin`
- Added predictor metrics to bundle summary/config

### 2) Runtime variant toggles
`FullHybridRuntimeSettings` now supports explicit research variants:
- `enableResidualModel`
- `enableSparseCorrectivePhysics`
- `enableTemporalNetworks`
- `enableActiveSetPredictor`
- `useTeacherPhysicsProxy`

These toggles allow emulating the evaluation variants used in the research plan:
- Pure Physics proxy
- Prior + Residual only
- Active-set Physics only
- Full Hybrid

### 3) Runtime metric completion
Filled the previously incomplete runtime report values:
- `averageGuidePredictorRisk`
- `maxGuidePredictorRisk`
- `averageEstimatedFrameMilliseconds`
- `p95EstimatedFrameMilliseconds`
- `maxEstimatedFrameMilliseconds`
- `hitRate60Fps`

Also extended per-frame CSV output with:
- `estimatedFrameMs`
- `avgGuidePredictorRisk`
- `maxGuidePredictorRisk`

### 4) CLI restore/additions
Added or restored:
- `hybrid-train-bundle`
- `hybrid-bundle-info`
- `hybrid-run-bundle`
- `hybrid-compare`

`hybrid-compare` runs four variants and writes:
- `hybrid_compare_summary.csv`
- `hybrid_compare_summary.json`

## Verification performed
Tested successfully on Linux/CMake:
1. `hybrid-teacher-dataset`
2. `hybrid-train-bundle`
3. `hybrid-run-bundle`
4. `hybrid-compare`

## Remaining major items after this stage
These are still not fully completed in the current project:
- true Alembic SDK integration
- DX11-side consumption of dispatch / GPU buffer outputs
- true GPU-generated indirect dispatch
- larger external trainer / sequence model for the neural core

So this stage completes the research-facing compare/runtime/predictor path, but not the whole product-grade stack.


## Source: STAGE8_INTEROP_AND_ABC_COMPAT_EXPORT.md

# Stage 8: ABC compatibility export and external-training interop

This stage closes the biggest remaining gaps that are still realistically implementable in the current Linux/no-Alembic-SDK environment.

## Added in this stage

### 1) `.abc` compatibility export
- `SaveHairAssetToAlembic(...)` no longer hard-fails in non-SDK builds.
- In non-`HAIRGPU_WITH_ALEMBIC` builds it now writes:
  - a USDA-compatible payload directly into the `.abc` path
  - a `.hairbin` sidecar (`<file>.abc.hairbin`)
  - a `.compat.json` manifest (`<file>.abc.compat.json`)
- Because the `.abc` loader already sniffs USDA / hairbin / CSV payloads, this gives a practical round-trip path for interchange/debugging.

### 2) Temporal network CSV interop
Added:
- `ExportTemporalGuideNetworkToCsvDirectory(...)`
- `ImportTemporalGuideNetworkFromCsvDirectory(...)`

CLI:
- `temporal-export-csv <input.temporalbin> <output_dir>`
- `temporal-import-csv <input_dir> <output.temporalbin>`

Exported files include:
- `metadata.csv`
- `input_mean.csv`
- `input_std.csv`
- `target_mean.csv`
- `target_std.csv`
- `hidden_weights.csv`
- `hidden_bias.csv`
- `output_weights.csv`
- `output_bias.csv`

This makes the learned temporal stage editable/retrainable outside the binary serializer.

### 3) Active-set predictor CSV interop
Added:
- `ExportActiveSetPredictorToCsvDirectory(...)`
- `ImportActiveSetPredictorFromCsvDirectory(...)`

CLI:
- `active-set-predictor-export-csv <input.predictorbin> <output_dir>`
- `active-set-predictor-import-csv <input_dir> <output.predictorbin>`

Exported files:
- `metadata.csv`
- `weights.csv`
- `README.txt`

This gives a simple external-training / manual-tuning path for the learned active-set stage.

### 4) Bundle-level interop package
CLI added:
- `hybrid-bundle-export-interop <bundle_dir> <output_dir>`
- `hybrid-bundle-import-interop <base_bundle_dir> <interop_dir> <output_bundle_dir>`

This exports/imports:
- `prior_temporal_csv/`
- `residual_temporal_csv/`
- `active_set_predictor_csv/`
- `interop_manifest.json`

Import applies edited interop payloads back onto an existing bundle and saves a new bundle directory.

### 5) CLI addition for ABC compatibility export
Added:
- `asset-export-abc-compat <input> <output.abc>`

## Verification performed
Tested successfully on Linux/CMake:
1. `asset-export-abc-compat generated_scaffold_v5/asset_normalized.hairbin /mnt/data/work_v8_test/asset_compat.abc`
2. `info /mnt/data/work_v8_test/asset_compat.abc`
3. `temporal-export-csv out_bundle_stage6/prior.temporalbin /mnt/data/work_v8_test/prior_temporal_csv`
4. `temporal-import-csv /mnt/data/work_v8_test/prior_temporal_csv /mnt/data/work_v8_test/prior_roundtrip.temporalbin`
5. `active-set-predictor-export-csv /mnt/data/work_v6/test_stage7/bundle/active_set.predictorbin /mnt/data/work_v8_test/predictor_csv`
6. `active-set-predictor-import-csv /mnt/data/work_v8_test/predictor_csv /mnt/data/work_v8_test/active_set_roundtrip.predictorbin`
7. `hybrid-bundle-export-interop /mnt/data/work_v6/test_stage7/bundle /mnt/data/work_v8_test/bundle_interop`
8. `hybrid-bundle-import-interop /mnt/data/work_v6/test_stage7/bundle /mnt/data/work_v8_test/bundle_interop /mnt/data/work_v8_test/bundle_roundtrip`
9. `hybrid-bundle-info /mnt/data/work_v8_test/bundle_roundtrip`

## What is still not fully finished
These remain outside what can be honestly completed in this environment without extra SDKs/toolchains:
- true binary Alembic SDK integration (`AbcGeom` reader/writer)
- DX11-side consumption/compilation/run validation of dispatch/gpu buffers
- true GPU-generated indirect dispatch on the actual Windows/DX11 runtime path

So this stage clears the remaining **interop/export/import** work that was still practical here, but not the external-SDK and Windows-only runtime integration pieces.


## Source: TEMPORAL_NEURAL_STAGE.md

# Temporal Neural Stage

This stage adds a lightweight learned temporal layer on top of the existing reduced-model hybrid runtime.

## What was added

- `TemporalGuideNetwork` in `include/hairgpu/TemporalModel.h` and `src/hairgpu_core/TemporalModel.cpp`
- Binary save/load:
  - `prior.temporalbin`
  - `residual.temporalbin`
- Hybrid bundle integration:
  - `HybridModelBundle::priorTemporalNetwork`
  - `HybridModelBundle::residualTemporalNetwork`
  - `priorTemporalBlend`
  - `residualTemporalBlend`
- Hybrid training report integration:
  - `priorTemporalEvaluation`
  - `residualTemporalEvaluation`
- Runtime integration:
  - prior temporal correction is blended into prior guide prediction
  - residual temporal correction is blended into residual guide input

## Model shape

The temporal layer is a shared point-wise MLP over guide points.

Input features per guide point:
- current displacement from rest (xyz)
- velocity (xyz)
- acceleration (xyz)
- normalized point position along strand
- normalized strand index
- rest-space distance from strand root
- displacement magnitude

Output:
- predicted/rest-relative guide-point displacement (xyz)

Root points are locked by default.

## Training wiring

### Prior temporal network
- input sequence: raw selected guide points from teacher frames
- target sequence: selected guide points from the smoothed prior sequence

### Residual temporal network
- input sequence: base residual guide input
  - `rest + (teacherGuide - priorGuide) * residualGuideGain`
- target sequence: selected teacher guide points for the residual model layout

## Runtime wiring

### Prior path
The existing velocity/acceleration extrapolation remains.
Then the temporal prior network predicts corrected guide positions and blends them by `priorTemporalBlend`.

### Residual path
The existing residual guide input remains.
Then the temporal residual network predicts corrected guide positions and blends them by `residualTemporalBlend`.

## Verification

Verified on Linux build:
- `hybrid-teacher-dataset`
- `hybrid-train-bundle`
- `hybrid-run-bundle`
- `hybrid-bundle-info`

Generated bundle files include:
- `prior.temporalbin`
- `residual.temporalbin`

Observed stage-6 bundle summary:
- `priorTemporalRms = 0.000898`
- `residualTemporalRms = 0.001727`
- `priorTemporalBlend = 0.457180`
- `residualTemporalBlend = 0.517271`

## Remaining limitations

This is still not a full production neural hair stack.
What remains outside this stage:
- real Alembic SDK integration
- DX11 consumption of dispatch/gpu buffers
- true GPU-generated indirect dispatch
- a larger sequence model / offline trainer beyond this lightweight in-engine MLP layer


## Source: TIERED_RUNTIME_AND_ABC_COMPAT_STAGE.md

# Tiered runtime + `.abc` compatibility stage

This stage pushes two still-missing runtime-facing pieces forward.

## Added now

### 1. Active-set tiers now affect the actual full-hybrid runtime output
Previously, the planner already produced `Full / Reduced / Frozen / Cached`, but the offline `RunFullHybridRuntime(...)` path still wrote essentially one corrected hybrid result for every strand.

That gap is now closed in the core runtime:

- `Full`
  - keeps the sparse-corrected result
- `Reduced`
  - keeps the reduced hybrid prediction without the extra frozen/cached reuse step
- `Frozen`
  - reuses the previous frame strand pose and root-follows the current root
- `Cached`
  - reuses a cache snapshot pose and root-follows the current root
  - the cache snapshot is refreshed on a configurable interval

This makes the runtime output match the active-set planner semantics much more closely.

### 2. Second-order temporal prediction gains
The hybrid bundle now carries:

- `priorAccelerationPredictionGain`
- `residualAccelerationPredictionGain`

The runtime now applies both velocity-based and acceleration-based prediction on top of the prior / residual guide inputs.

### 3. Bundle parameters for frozen/cached tiers
The hybrid bundle now stores:

- `frozenStrandBlend`
- `frozenRootFollow`
- `cachedStrandBlend`
- `cachedRootFollow`
- `cachedFrameRefreshInterval`

These are saved to / loaded from `hybrid_bundle.cfg` and included in JSON summaries.

### 4. Practical `.abc` compatibility fallback
This is **not real Alembic SDK import**.

But `.abc` loading now sniffs the file contents and transparently falls back to:

- USDA
- hair binary (`.hairbin` payload)
- curves CSV

when those assets were renamed or passed through compatibility workflows.

That means `.abc` is still not true binary Alembic in this build, but the path is more usable for interchange/debugging than a hard stub.

## Verified in this environment

- Linux CMake build with DX11 viewer disabled: success
- `hybrid-train-bundle`: success
- `hybrid-run-bundle`: success
- `.hairbin` renamed to `.abc` then loaded through `active-set-plan`: success

## Still not finished

- real Alembic SDK-backed `.abc` import/export
- DX11-side consumption of the dispatch pack / GPU buffers
- true GPU-generated indirect dispatch from runtime counters
- full neural prior / temporal residual beyond the reduced-model path


## Source: STAGE9_PRIORITY3_IMPLEMENTATION_NOTES.md

Merged duplicates: STAGE9_PRIORITY3_IMPLEMENTATION_NOTES.md

# Stage 9: Alembic hook-up, GPU compaction/indirect, stronger temporal/predictor training

Implemented in this patch:
- Alembic public save wrapper now routes to the SDK path when `HAIRGPU_WITH_ALEMBIC` is enabled.
- Added overload handling for `HairAlembicSettings` in the public API path.
- Expanded Alembic compatibility manifest generation for non-SDK builds.
- Temporal guide network training upgraded with validation split, LR decay, minimum LR, and early stopping.
- Active-set predictor training upgraded with validation split, LR decay, minimum LR, early stopping, and positive-class weighting.
- DX11 viewer path extended with GPU-side follower tier compaction, GPU-generated indirect dispatch args, and compacted follower dispatch consumption in `FollowCS`.

Verified in this environment:
- Linux/CMake build for `hairgpu_core` and `hairgpu_cli` succeeded.
- `hybrid-train-bundle` succeeded with the stronger temporal/predictor training path.
- `hybrid-run-bundle` succeeded and wrote summary JSON.
- `HAIRGPU_WITH_ALEMBIC=ON` configuration was accepted, but the environment does not have the Alembic SDK installed, so CMake fell back to compatibility-only `.abc` support.

Not verifiable here:
- Real Alembic SDK round-trip at runtime (SDK absent here).
- Windows/DX11 execution of the new GPU compaction + `DispatchIndirect` path.


## Source: STAGE10_ALEMBIC_COMPLETION_NOTES.md

Merged duplicates: STAGE10_ALEMBIC_COMPLETION_NOTES.md

# Stage 10 notes

Implemented in this patch:

- Public `HairAlembicSettings` + `AlembicRoundTripReport` API in `AssetIO.h`
- `RoundTripHairAssetThroughAlembic(...)` helper for save/load/self-test validation
- Stronger Alembic CMake discovery in `cmake/FindAlembic.cmake`
- CLI command:
  - `alembic-roundtrip-selftest <input> <temp_output.abc> [report.txt]`
- Alembic scalp metadata round-trip support through user properties:
  - normals
  - uvs
  - skin bone indices
  - skin bone weights
  - boneCount

Verified in this environment:

- Linux/CMake build of `hairgpu_core` and `hairgpu_cli`
- `alembic-roundtrip-selftest` on a sample `.hairbin`
- Compatibility `.abc` export/load round-trip with report output

Important limitation:

- Real binary Alembic SDK execution is still environment-dependent.
  This patch wires the SDK path and adds self-test/reporting, but a true SDK-enabled run still requires
  `HAIRGPU_WITH_ALEMBIC=ON` plus an installed Alembic SDK and a matching toolchain.
- DX11 runtime / Windows GPU execution was not verified in this Linux environment.


