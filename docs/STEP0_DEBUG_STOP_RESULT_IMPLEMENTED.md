# Step0 Debug Stop Result Export

この版では、Visual Studio のデバッグ停止時やウィンドウを閉じた時でも、`profile_results` 内にその時点までの計測結果を残すようにした。

## 追加される出力

Step0実測中に随時更新されるファイル:

- `RESULT_REAL_PARTIAL.md`
- `DEBUG_STOP_MEASUREMENT.md`
- `DEBUG_STOP_MEASUREMENT.csv`
- `<preset>/<preset>_frames.csv`
- `<preset>/<preset>_summary_debug_stop.json`
- `<preset>/DEBUG_STOP_SNAPSHOT.txt`
- `<preset>/STEP0_HEARTBEAT.txt`

## 目的

以前の版では、デバッグ停止や途中終了のタイミングによっては `RESULT_REAL.md` が出ず、何フレーム測れたか分からなかった。
今回の版では、1フレームでも進めば最新の `frames.csv` と `DEBUG_STOP_MEASUREMENT.md` を毎フレーム更新する。

## 注意

Visual Studio の「デバッグの停止」が `TerminateProcess` 相当でプロセスを強制終了した場合、終了ハンドラは実行されない。
そのため、この版では終了時だけでなく、実行中に毎フレーム `DEBUG_STOP_MEASUREMENT.md` と `<preset>_frames.csv` を更新する。
