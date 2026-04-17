# Active-Set Runtime Foundation

この段階では **最終的な hybrid runtime そのもの** ではなく、研究本題を安全に差し込むための **planning/orchestration layer** を追加しています。

## 目的

既存の GPU guide/follower runtime を壊さずに、毎フレーム

1. どの guide cluster が危険か
2. どの cluster にだけ corrective physics を当てるか
3. そのときの budget をどう守るか

を決める CPU 側の基盤を先に固定することです。

## 追加された API

- `include/hairgpu/ActiveSet.h`
- `src/hairgpu_core/ActiveSet.cpp`

主な機能:

- 既存 `HairAsset` から guide cluster を構築
- follower を親 guide cluster に追従させて cluster family を作る
- motion / displacement / visibility / silhouette / uncertainty / contact proxy を cluster score に集約
- `maxActiveClusters` / `maxActiveGuides` / `maxActiveStrands` で budget 制御
- corrective pass 候補を JSON に書き出し

## 研究本題への差し込み方

次に viewer / runtime 側でやることは次の順です。

1. current / previous guide roots を毎フレーム `ActiveSetRuntimeInput` に流す
2. uncertainty / contact / penetration proxy を GPU readback か counters から入れる
3. `EvaluateActiveSetPlan(...)` を呼ぶ
4. 選ばれた cluster だけ corrective physics dispatch する
5. それ以外は既存 guide/follower path を維持する

## いま残していること

- corrective physics solver そのもの
- GPU 側 cluster compaction / indirect dispatch
- uncertainty を reduced model 誤差や neural confidence と結びつける実装
- penetration depth / hand-object contact などの本番入力

つまり、**研究の本丸を入れるための受け皿はできたが、本丸そのものはまだこれから** という段階です。

## CLI

```bat
hairgpu_cli active-set-plan generated_scaffold\asset_normalized.hairbin active_set_plan.json 8 0.085 6 64
```

このコマンドは現在の asset から rest-pose ベースの初期 active-set plan を出力します。
研究統合時はここに runtime の guide roots / uncertainty / contact を入れていけば、そのままフレームごとの計画器として使えます。
