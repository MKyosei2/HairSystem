# Manual patch notes for Novelty 61.39

## 61.39で残っていた問題

1. summary JSONの出力位置が壊れていた
   - `false_static_count` などが `exports` オブジェクト内に混ざっていた
   - JSONとして不正になる

2. `runtime_frame_metrics` にSegment Group metricsが保存されていなかった

3. summary queryに `AVG(false_static_rate)` などが入っていなかった

4. runtime suiteのMethod/Scenario表に新metricsが出ていなかった

5. `metric_definitions.json` に同じmetric名が重複していた

## このパッチの方針

- `HairNoveltyTelemetry.h` はクリーンなものに差し替え
- summary JSONは `exports` の直前にSegment Group metricsを出力
- DBは既存INSERTを大きく壊さず、追加UPDATEでframe metricsを保存
- summary queryにはAVG列を追加
- metric definitionsは重複を削除して論文用に定義を統一
