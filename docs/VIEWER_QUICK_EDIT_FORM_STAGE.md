# VIEWER QUICK EDIT FORM STAGE

この段階では、DX11 viewer の inline edit stack をそのまま残しつつ、
CSV 行を直接編集しなくても主要パラメータを触れる **Quick Edit Form** を追加した。

## 追加内容

- `Quick Edit Form` セクション
- `Mode` コンボ
  - Ellipse
  - Rectangle
  - Sector
- `Feather` スライダ
- `Length Scale` スライダ
- `Replace First` ボタン
- `Append Layer` ボタン

## 使い方

1. `Quick Edit Form` でモードを選ぶ
2. `Feather` と `Length Scale` を調整する
3. `Replace First` で先頭レイヤを置き換える
4. `Append Layer` で末尾に新しいレイヤを追加する
5. 追加後は inline edit stack に自動反映され、そのまま apply される

## 挙動

- `Load Edit Stack...` で CSV を読み込むと、先頭レイヤから quick form に値を反映する
- `Reset Inline` や settings 復元時にも quick form は同期される
- quick form は **CSV を隠す完全GUI** ではなく、
  **viewer 上で主要な region mode / feather / lengthScale を素早くいじる補助フォーム** として実装している

## 制約

- 今回の quick form は先頭レイヤ同期ベース
- `center/radius/angle/groupId/rootOffset/tipOffset` などの全列編集までは未対応
- Windows / DX11 実機での最終確認は別途必要

## 次段階

- レイヤ一覧 UI
- 選択レイヤ切替
- center / radius / angle / offset のフォーム化
- scalp 上の gizmo / brush とフォームの連動
