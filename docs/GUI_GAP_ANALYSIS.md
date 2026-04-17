# GUI Gap Analysis (current snapshot)

## もともと足りなかったもの

このプロジェクトは GPU シミュレーションと DX11 表示のベースはかなり進んでいましたが、
**研究用ツールとして「GUI で調整しながら確認する」用途**には次が不足していました。

1. **GUI パラメータパネル**
   - 主要なシミュレーション値をマウス操作で変更できなかった
   - キーボードショートカット依存だった

2. **状態の可視化**
   - 現在の render mode / self collision / strand 数 / guide 数などが UI として見えなかった
   - 値の変化を見ながら調整するワークフローになっていなかった

3. **研究向けの操作性**
   - play / pause / reset が GUI ボタン化されていなかった
   - self collision や scalp motion の ON/OFF をチェックボックスで切り替えられなかった

4. **描画領域と操作領域の分離**
   - 画面全体がレンダリング領域で、UI を常設する前提になっていなかった
   - 将来的な inspector / debug panel を追加しにくかった

5. **卒研向けデモ性**
   - 外部説明時に「ここで何を操作できるか」が直感的ではなかった
   - 実験条件の再現・比較がやりづらかった

## 今回実装したもの

### Win32 ベース GUI パネル
- 右側に常設のパラメータパネルを追加
- ボタン、チェックボックス、コンボボックス、スライダーで操作可能

### GUI 化した操作項目
- Play / Pause
- Reset
- Self Collision ON/OFF
- Grid Broadphase ON/OFF
- Scalp Motion ON/OFF
- Render Mode 切り替え（Ribbon / Tube Preview）
- Width Scale
- Gravity
- Damping
- Wind Strength
- Solver Iterations
- Follow Stiffness
- Self Collision Radius
- Self Collision Stiffness
- Grid Cell Size
- Stretch Compliance
- Bend Compliance

### 画面構成の改善
- レンダリング領域と GUI 領域を分離
- 右側 UI パネルのぶんだけレンダリング viewport を縮小
- マウスドラッグはレンダリング領域だけでカメラ操作するように調整

### 状態表示
- 現在の strand 数、guide 数、follower 数、render mode、self collision 状態などをステータス欄に表示
- GUI 操作とキーボード操作の両方で UI 表示が同期されるようにした

## まだ足りないもの

今回で **「GUI でシミュレーションを触って確認する基礎」** は入っていますが、
最終目的までにはまだ次が必要です。

1. **アセット読み込み UI**
   - 現状は sample hybrid groom 固定
   - 将来は Alembic / USD / 独自 .hair の読み込み UI が必要

2. **保存 / 読み戻し**
   - パラメータプリセット保存
   - 実験条件の export/import

3. **記録機能**
   - フレームキャプチャ
   - キャッシュ書き出し
   - 動画・静止画 export

4. **詳細デバッグ表示**
   - collider 可視化
   - root bind 可視化
   - guide / follower の色分け
   - grid broadphase の debug draw

5. **本格 GUI フレームワーク化**
   - 今回は純 Win32 controls
   - 将来的には Dear ImGui などへ移行すると inspector / debug / profiler を足しやすい

6. **研究用評価 UI**
   - FPS / ms per frame
   - solver iteration ごとの比較
   - error / silhouette / collision metrics

## 今の位置づけ

この変更後の状態は、
**「GPU 髪シミュレーションの研究用 GUI ビューア」**
としては十分に次の段階へ進めるベースです。

ここから先は、
- hair asset I/O
- debug visualization
- cache/export
- cards / LOD / game approximation

を積むことで、映画寄りプレビューと卒研向けゲーム転用の両方に進めます。
