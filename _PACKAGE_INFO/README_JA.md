# HairSystem 非新規性領域・緊急性能改善パッケージ

## 固定条件

- 表示髪本数: 100,000本
- Guide: 4,000本
- Follower: 96,000本（24本/Guide）
- 粒子数/ストランド: 128
- 合計粒子数: 12,800,000
- Noveltyの方式判定、Static/Dynamic切替、Certificate、Repair、Physics Islands、
  Active-set選択方式などは変更していません。

## 収録する変更済み既存ソース

1. `apps/dx11_viewer/CMakeLists.txt`
2. `apps/dx11_viewer/src/GpuHairViewer.h`
3. `apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl`
4. `apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl`
5. `apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl`
6. `apps/dx11_viewer/src/GpuHairViewer_parts/06_render_mesh_and_visibility_recovery.inl`

## 実装した10項目

1. Release/RelWithDebInfoを`/O2 + /Zi + /DEBUG`に統一し、PDBを維持した最適化実行に対応。
2. 通常Viewerの固定ステップを1描画フレーム最大1回に制限。
3. `FollowCS`をSolver反復ループ外へ移動し、最終Guide状態に対して1回だけ実行。
4. `IntegrateCS`をGuide粒子だけへ限定。更新を間引くFollowerはGPU内CopyResourceで保持。
5. 12,800,000粒子のVisibility用Full CPU Readbackを既定でOFF。
6. Self Collision BroadphaseをSpatial Hashへ固定し、Bitonic Sortを比較専用経路へ隔離。
7. Reset時の4本の巨大状態バッファをCPU再アップロードせずGPU内コピーで復元。
8. 既存`AssetIO`のHair Binary APIを再利用した検証付きアセットキャッシュをViewer初期化へ追加。
9. 通常ViewerのGPU Timingを16フレームに1回、Window Titleを0.2秒間隔へ変更。
   正式実験モードのGPU Timingは毎フレームのまま。
10. Self Collisionの`GridParticleNext`を全粒子分ではなくGuide粒子分だけ確保。

## 変更しなかったファイル

- `HairSim.hlsl`: Dispatch対象の変更だけで成立するため、シェーダーの物理式を変更していません。
- `05_active_set_scoring_and_runtime_update.inl`: HeaderのReadback既定値をOFFにすると既存ガードが機能します。
- `SampleAsset.cpp` / `AssetIO.cpp`: 既存のBinary Load/Save APIをViewerから利用するため変更不要です。

## 適用

ZIPをリポジトリルートへ展開し、同じパスのファイルを上書きしてください。

Visual Studioでは次の順で実行します。

1. 構成を`Release`または`RelWithDebInfo`、プラットフォームを`x64`にする。
2. 「ソリューションのクリーン」。
3. 「ソリューションのリビルド」。
4. 初回起動では正式アセットを生成し、実行ファイル隣の`asset_cache`へ保存。
5. 2回目以降は検証済み`.hairbin`を直接読み込む。

アセット生成式や形状を将来変更した場合は、`asset_cache`フォルダを削除するか、
キャッシュ名の`cache_v1`を更新してください。

## 検証範囲

実施済み:
- Noveltyブランチの基準blob SHA照合
- 必須変更マーカー検査
- 100,000本・12,800,000粒子の固定値検査
- Follow Dispatch一回化検査
- Full Integrate除去検査
- CPU巨大Upload除去検査
- 文字コード、競合マーカー、ZIP整合性検査

未実施:
- Windows Release x64の実ビルド
- D3DCompileによるHLSL実コンパイル
- 実GPU上のフレーム時間・VRAM・描画品質比較
