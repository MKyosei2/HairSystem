# Viewer Region Form Stage

この段階では、DX11 viewer の Quick Edit Form から、選択レイヤの領域そのものを直接編集できるようにした。

追加内容:
- Center U
- Center V
- Radius U
- Radius V
- Inner Radius
- Angle Center (deg 表示)
- Angle Half Extent (deg 表示)

反映内容:
- 選択レイヤから Quick Edit Form へ領域パラメータを同期
- Quick Edit Form から inline edit stack CSV へ領域パラメータを書き戻し
- Replace Selected / Append Layer で region 情報を保持
- Edit Influence overlay の preview summary に center 情報を表示

設計メモ:
- center は 0..1 の UV
- radius は 0.01..0.60 にクランプ
- inner radius は radius の最小値に対して 95% 以下へクランプ
- angle center は -180..180 deg 相当
- angle half extent は 0..180 deg 相当

これにより、ellipse / rectangle / sector の位置・広がり・角度まで viewer 上で調整できる。
