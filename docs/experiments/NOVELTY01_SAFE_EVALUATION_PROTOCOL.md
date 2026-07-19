# Novelty01 Safe Evaluation Protocol v3.4

## 1. 適用範囲

本Protocolは、Segment Group統合状態表現の性能、品質、決定性、長時間安定性を検証する正式手順である。主測定条件は4,000 Guide、24 Follower/Guide、100,000表示Strand、128 Particle/Guide、6 Solver Iteration、1920×1080、Fixed 1/60秒、VSync OFFに固定する。

## 2. 測定前条件

- `Novelty`ブランチ
- Clean worktree
- Release x64
- Frozen Asset Manifest
- 指定Hardware Profile一致
- 20 GiB以上の空き容量
- Local Drive出力
- AC電源、高性能Power Scheme
- Native、Shader、Python、Validator Self-test成功

## 3. Formal Matrix

10手法（B0～B8、P）×18Scenario（S01～S18）×5Seedを最低構成とする。通常Runは900件である。Determinism Replayと30分／2時間／8時間Runは別に追加する。

## 4. Frame取得

- Warm-up: 600 Frame以上
- Measurement: 6,000 Frame以上
- Source Frame Indexは単調増加
- CSVとJSONLを同時出力
- 同一Frame数・同一IndexであることをValidatorが確認
- GPU Timestamp Delayを6 Frame確保
- GPU Timing Coverage 99%以上

## 5. 性能Gate

- Average FPS ≥ 60.0
- 60 FPS Ratio ≥ 0.95
- Frame Budget Overrun Rate ≤ 0.05
- p95 ≤ 16.6667 ms
- p99 ≤ 20.0 ms
- Maximum ≤ 50.0 ms
- Frames over 100 ms = 0

## 6. 証拠Gate

性能未達と証拠無効を混同しない。欠落、Hash不一致、Synthetic Metric、NaN/Inf、Fatal Failure、Frame不一致、Hardware不一致はEvidence Invalidである。正しい測定で性能未達の場合はEvidence Valid／Goal Not Achievedとして保存する。

## 7. Asset Freeze

テンプレートManifestから、ローカルの実Asset、Topology、Generator Settingsを使って`frozen_asset_manifest.json`を生成する。Freeze時のCommit SHAと正式実行時のHEADを一致させる。Freeze後のAsset変更は禁止する。

## 8. Execution Fingerprint

Commit、EXE、Shader、Asset、Topology、Generator Settings、Timeline、Method Settings、Hardware Profile、GPU、Driver、OS、PythonをHashへ含める。Resumeは完全一致とArtifact再検証を要求する。

## 9. Scenario Assertion

Assertionは各Scenario JSONに置く。Eventには`id`を付与する。Event数、Collision、Wake、Repair、Transition、OscillationをScenarioごとに確認する。Timeline自体のSHA-256も固定する。

## 10. 長時間試験

| Test | Scenario | 時間 |
|---|---|---:|
| L30 | Walk | 30分 |
| L2H | Strong Wind | 2時間 |
| L8H | State Thrashing | 8時間 |

VRAM slope ≤1 MB/min、RAM slope ≤2 MB/min、p99劣化率≤1.10、Unresolved Query slope=0を要求する。

## 11. 再現成果物

Suite Summaryを最終書込みした後にRepro Manifestを生成し、以降Hash対象を変更しない。最後にManifestを検証してZIP化する。

## 12. 推奨コマンド

```bat
REM 1. Assetをローカルで固定
python tools\final_stage\freeze_novelty01_asset.py ^
  --template-manifest docs\sample_assets\novelty01_100k_asset_manifest.json ^
  --output-manifest out\novelty01_baseline\frozen_asset_manifest.json ^
  --asset C:\HairFormalAssets\novelty01_100k.hairbin ^
  --generator-settings C:\HairFormalAssets\generator_settings.json ^
  --topology-file C:\HairFormalAssets\topology.bin ^
  --operator <operator-name>

REM 2. Smoke
call tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\final_runtime_suite_template.json smoke

REM 3. Full
call tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\final_runtime_suite_template.json full
```

## 13. 禁止事項

- Debug Buildの数値を正式結果へ使用する
- 24 FPS Gateを60 FPS達成として扱う
- Template Asset Manifestを正式証拠へ使用する
- 異なるCommit、EXE、Shader、Driverの結果をResumeで流用する
- Repro Manifest生成後にSummaryを書き換える
- Network Driveへ直接正式Telemetryを保存する
- 8時間Runの途中成果物をCOMPLETEとして扱う

