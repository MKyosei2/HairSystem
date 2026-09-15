# Thesis Practical Evaluation

## 目的

局所編集に対して、単純な Frame Time だけではなく、

- 実際に編集された範囲
- Segment Group / Block の更新範囲
- 再利用できた範囲
- Full Dynamic fallback
- B0 と P の同一入力比較

を記録する。

## 実装経路

手動編集と正式実験は、可能な限り同じ EditStack / Sparse Edit 経路を使用する。

```text
Edit input
  -> EditStack
  -> ApplyEditStackToGuideFamiliesSparse
  -> changedPointIndices
  -> direct Segment Groups
  -> affected Blocks
  -> SG-APT
  -> Follower / Render dependency
```

`changedPointIndices` を直接編集範囲の根拠とし、単なる選択範囲を
「実際に変更された範囲」とは扱わない。

## B0 / P

- B0: 比較用の Full Dynamic baseline
- P: 安全に局所無効化できる場合だけ対象 Block を `Dirty_Edit` として処理
- 前提を満たせない場合: 既存の Full Dynamic path へ戻る

P が常に部分更新される、常に高速になる、とは仮定しない。

## Counter ABI

既存 Novelty01 の 64 Counter ABI は変更しない。

Thesis Practical Evaluation は別の Edit Episode CSV を出し、
既存 frame telemetry / Novelty01 telemetry と Report 側で結合する。

## Case 定義

`benchmarks/thesis_practical/*.json` を唯一の Case 定義とする。
Runner 内に T00-T07 の数値を重複して持たせない。

## 出力

`HAIRGPU_THESIS_EDIT_EVAL=1` のとき、

`<run_name>_thesis_edits.csv`

を生成する。

既存の正式 Runner が生成する Frame / Novelty01 telemetry と
`hairgpu_thesis_practical_report.py` で結合する。

## 実験時の原則

- Formal Performance: Release x64
- 可視化 Overlay / Debug Capture: Formal Performance では無効
- B0 / P は同一 Asset / Scenario / Seed
- 未取得値は 0 とみなさない
- Fallback は速度低下ではなく安全動作として別評価する
