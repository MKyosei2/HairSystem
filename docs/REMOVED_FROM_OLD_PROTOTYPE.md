# Removed From The Old Prototype

このベースでは、元の ZIP にあった以下を削除しました。

- CPU ベースの `hair_core` ミニソルバ
- 1 ファイル巨大構成の `solver_lab/src/Main.cpp`
- ビルド生成物 (`bin/`, `.vs/`, `.git/` など)
- 試作用 UI スライダ群
- 2D ラインレンダラ中心の旧ビューア構成

理由は、GPU 中心の本格開発ベースとしては以下の方が重要だったためです。

- ライブラリ層とアプリ層の明確な分離
- 3D カメラと 3D リボン描画
- 将来の engine / DCC adapter を見据えたデータモデル
- 配布しやすいクリーン ZIP
