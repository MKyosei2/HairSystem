# HairSystem Formal Test 修正後ファイル

このZIPは、`MKyosei2/HairSystem` の `Novelty` ブランチで確認した元blob SHAと一致するファイルを土台に、P0/P1修正を適用した完全ファイルです。

## 収録数について

依頼上は5項目ですが、Viewer側の実装が2つの`.inl`へ分割されているため、正しく動作させるには6ファイル必要です。

- `10_phase1_telemetry_csv_jsonl_summary.inl`: 方式コード解析、method settings、assertion読込、Observed flags
- `11_experiment_env_and_performance_sample.inl`: CSV/JSONL、Observed設定、ハッシュ、False Static、最終判定
- `GpuHairViewer.h`: Requested/Applied/Observed保持領域
- `hairgpu_runtime_suite.py`: assertionキー統一と事前検証
- `hairgpu_final_run.py`: 全フラグ照合、CSV/JSONL整合性検証
- `HairNoveltyTelemetry.h`: 120行ごとの証拠チェックポイント

## 適用方法

ZIP内のディレクトリ構成を維持したまま、HairSystemリポジトリの同じパスへ上書きしてください。上書き前にGitブランチまたはバックアップを作成してください。

## 検証済み

- 6ファイルすべての修正前Git blob SHA一致
- Python 2ファイルの`py_compile`成功
- 必須修正文字列と出力列の静的確認

## 未実施

この環境ではWindowsのRelease x64ビルド、DirectX 11 Viewer実行、HLSLコンパイルは実施できていません。適用後はB0/Pの短時間Smoke Testを先に行ってください。
