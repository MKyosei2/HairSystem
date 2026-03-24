# Hair System Phase 1 (Visual Studio / C++)

この ZIP は、フェーズ1として **純C++の hair_core 静的ライブラリ** と、
**Win32 GUI で挙動確認できる solver_lab アプリ** を含んでいます。

## すぐ試す手順

1. ZIP を展開
2. `HairSystem.sln` を Visual Studio 2022 で開く
3. 必要なら **Retarget solution** を実行
4. 構成を **Debug | x64** にする
5. `solver_lab` をスタートアッププロジェクトにしてビルド
6. 実行

## 含まれるもの

- `hair_core`
  - 1本の髪を「ノード列のストランド」として扱う最小ソルバ
  - Verlet 積分
  - 長さ制約
  - 簡易曲げ制約
  - 2D円コライダ + 床衝突
- `solver_lab`
  - Win32 GUI
  - 再生 / 停止 / 1フレーム進める / リセット
  - ストランド数、セグメント数、重力、減衰、反復回数、曲げ剛性を GUI で確認

## 画面の見方

- 左: シミュレーション表示
- 右: コントロールパネル
  - **Start / Pause**: 再生・停止
  - **Step**: 停止中に 1 ステップ進める
  - **Reset**: 現在設定で初期化
  - **Gravity**: 重力
  - **Damping**: 減衰
  - **Iterations**: 制約反復回数
  - **Bend**: 曲げ拘束の強さ
  - **Strands**: 髪の本数
  - **Segments**: 1本あたりの節点数

## 目的

この段階では、
**「髪1本 = 1本の論理ストランド」** を C++ でライブラリ化し、
表示は GUI 上のライン描画で確認する構成にしています。

今後の流れ:
- フェーズ2: 衝突・束感・デバッグ機能の拡張
- フェーズ3: キャッシュ出力や DCC / UE 連携
- フェーズ4: 3D 化、メッシュ化、各ツール用アダプタ

## フォルダ構成

- `hair_core/` : 純C++ライブラリ
- `solver_lab/` : Win32 GUI 検証アプリ
- `CMakeLists.txt` : 将来の統合用
- `HairSystem.sln` : Visual Studio ソリューション

## 注意

- Windows / Visual Studio 2022 想定です。
- Win32 GUI を使っているため、最初の確認環境は Windows です。
- このフェーズは 2D 可視化ですが、コア側のデータ構造は 3D ベクトルで持っています。
