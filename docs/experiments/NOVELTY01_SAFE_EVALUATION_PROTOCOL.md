# Novelty 01 Safe Evaluation Protocol v4.0

## 0. 目的

本書は、SG-APTの正式評価を再現可能かつ恣意性の少ない形で実行するための規範的手順を定める。研究主張、実装契約、評価証拠の対応は`設計書/01A_SGAPT_Implementation_Contract_and_Evidence.md`に従う。

---

# 1. 適用範囲

主測定条件は以下へ固定する。

- 4,000 Guides
- 24 Followers / Guide
- 100,000 Display Strands
- 128 Points / Strand
- 6 Solver Iterations
- 1920 × 1080
- Fixed 1/60秒
- VSync Off
- Release x64

性能、品質、Payload Lifetime、決定性、長時間安定性、Fallback、End-to-End更新量を同一のExecution Fingerprintで評価する。

---

# 2. Formal実行禁止条件

以下のいずれかに該当する場合、正式評価を開始しない。

- Working Treeに未記録変更がある
- 実行CommitとCase ManifestのCommitが異なる
- Release x64 Buildが未成功
- FXC Shader Self-Testが未成功
- Frozen Asset Hashが不一致
- Adapter Identityが未取得
- Timeline Hashが未取得
- Requested／Applied／Observedが一致しない
- Telemetry Availabilityが不足
- Same-Frame Readbackが発生
- Fatal Failureが発生
- `overlay/`を実行対象としている
- Canonical Scenario Injectionが確認できない
- 20 GiB未満の空き容量
- Network Driveへ直接出力している
- AC電源／高性能Power Schemeでない

---

# 3. 固定環境

| 項目 | 固定値 |
|---|---|
| Build | Release x64 |
| Resolution | 1920 × 1080 |
| VSync | Off |
| Fixed dt | 1/60 sec |
| Solver Iterations | 6 |
| Guides | 4,000 |
| Followers | 96,000 |
| Display Strands | 100,000 |
| Points per Strand | 128 |
| Guide Groups | 508,000 |
| CPU Readback | Formal Performanceでは無効 |
| Seed | Case JSONで固定 |
| Camera | Case JSONで固定 |
| Asset | Frozen Asset Hashで固定 |
| GPU Timestamp Delay | 6 frames以上 |
| GPU Timing Coverage | 99%以上 |

---

# 4. 実験バージョン固定

正式評価開始時に以下を`experiment_manifest.json`へ保存する。

```json
{
  "repository": "MKyosei2/HairSystem",
  "branch": "Novelty01",
  "commit": "<exact commit sha>",
  "designContract": "設計書/01A_SGAPT_Implementation_Contract_and_Evidence.md",
  "assetHash": "<sha256>",
  "topologyHash": "<sha256>",
  "generatorSettingsHash": "<sha256>",
  "timelineRootHash": "<sha256>",
  "telemetrySchema": "<schema id>",
  "parameterFreezeId": "<id>",
  "buildConfiguration": "Release x64",
  "compiler": "<MSVC version>",
  "windowsSdk": "<version>",
  "driver": "<version>",
  "adapter": "<exact DXGI name>",
  "python": "<version>"
}
```

Execution FingerprintにはCommit、EXE、Shader、Asset、Topology、Generator Settings、Timeline、Method Settings、Hardware Profile、GPU、Driver、OS、Pythonを含める。

ResumeはFingerprint完全一致と既存Artifact再検証を要求する。

---

# 5. Asset Freeze

テンプレートManifestではなく、ローカルの実Asset、Topology、Generator Settingsを用いて`frozen_asset_manifest.json`を生成する。

Freeze時Commitと正式実行時HEADを一致させる。Freeze後にAsset、Topology、Generator Settingsを変更した場合、すべての比較結果を再取得する。

```bat
python tools\final_stage\freeze_novelty01_asset.py ^
  --template-manifest docs\sample_assets\novelty01_100k_asset_manifest.json ^
  --output-manifest out\novelty01_baseline\frozen_asset_manifest.json ^
  --asset C:\HairFormalAssets\novelty01_100k.hairbin ^
  --generator-settings C:\HairFormalAssets\generator_settings.json ^
  --topology-file C:\HairFormalAssets\topology.bin ^
  --operator <operator-name>
```

---

# 6. BuildとShader検証

Visual Studio 2022 Developer Command Promptから実行する。

```bat
msbuild HairSystem.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m
```

必須条件：

- Error 0
- Warning一覧を保存
- Viewer EXE生成
- PDB生成
- PostBuild Shader Copy成功

Novelty01 Preflight：

```bat
python tools\final_stage\hairgpu_novelty01_preflight.py --repo . --require-fxc
```

契約テスト：

```bat
python -m pytest tests\final_stage\test_novelty01_complete_contract.py -q
```

Native、Shader、Python、Validator Self-Testの実行コマンドと結果をBuild Artifactへ保存する。

---

# 7. Formal Matrix

最低構成は次のとおりとする。

```text
10手法（B0～B8、P）
× 18 Scenario（S01～S18）
× 5 Seeds
= 900 Runs
```

A1～A15、Determinism Replay、30分／2時間／8時間Runは別に追加する。

Case ID：

```text
N01_<profile>_<variant>_<scenario>_seed<NN>_r<NN>
```

各Caseには次を必須とする。

- `method`
- `novelty01_variant`
- `novelty01_design_scenario`
- `scenario_timeline`
- `warmup_frames`
- `measurement_frames`
- `fixed_timestep_seconds`
- `solver_iterations`
- `seed`
- `camera`
- `payload_capacity`
- `feature_mask`
- `ground_truth`
- `measurement_mode`
- `required_adapter_name_regex`

---

# 8. Canonical Scenario

`novelty01_design_scenario`がS01～S18の意味を決定する。既存ViewerのRuntime Scenario名は互換Aliasであり、正式な研究定義ではない。

各Timeline JSONに次を含める。

```json
{
  "schema": "novelty01-canonical-scenario-v1",
  "scenarioId": "S01",
  "description": "完全静止",
  "durationFrames": 6000,
  "fixedTimeStepSeconds": 0.016666666666666666,
  "seed": 1,
  "events": [],
  "expected": {
    "canonicalInjectionRequired": true,
    "fatalFailureCount": 0
  }
}
```

Eventには一意な`id`を付ける。Event数、Collision、Wake、Repair、Transition、OscillationをScenario Assertionで検証する。Timeline SHA-256が異なるRunを同一集計へ混ぜない。

---

# 9. 実行Profile

## 9.1 Smoke

- Warm-up：120 frames
- Measurement：360 frames
- Repetition：1
- Ground Truth：有効
- 対象：PのS01、S06、S11、S17、S18

一つでも失敗した場合、Formalへ進まない。

## 9.2 Quality

- Warm-up：600 frames
- Measurement：6,000 frames
- Seeds：5
- Repetition：1以上
- Ground Truth：有効
- 性能合否には使用しない

## 9.3 Performance

- Warm-up：600 frames
- Measurement：6,000 frames
- Seeds：5
- Repetition：3を推奨
- Ground Truth：無効
- Same-Frame Readback：0

## 9.4 Determinism

- Warm-up：600 frames
- Measurement：6,000 frames
- Repetition：3以上
- 同一Seed
- 同一Adapter／Driver

## 9.5 Long Run

| Test | Scenario | 時間 |
|---|---|---:|
| L30 | S15または通常運動 | 30分 |
| L2H | 強風 | 2時間 |
| L8H | Thrashing Stress | 8時間 |

長時間Gate：

- VRAM slope ≤ 1 MB/min
- RAM slope ≤ 2 MB/min
- p99劣化率 ≤ 1.10
- Unresolved Query slope = 0
- Invalid Owner／Index = 0

## 9.6 GTX1660

- Exact Adapter Identity必須
- GTX 1660、GTX 1660 SUPER、GTX 1660 Tiは別集計
- VRAM容量とDriverを記録
- 理論換算値を実測値へ混ぜない

---

# 10. Matrix実行例

Smoke：

```bat
python tools\final_stage\hairgpu_novelty01_matrix.py ^
  --repo . ^
  --viewer build\bin\Release\HairGpuViewer.exe ^
  --template docs\sample_assets\final_runtime_suite_template.json ^
  --timeline-root benchmarks\novelty01\sgapt_scenarios ^
  --output artifacts\novelty01\smoke ^
  --profile smoke ^
  --variants P ^
  --scenarios S01,S06,S11,S17,S18 ^
  --require-fxc
```

正式品質評価：

```bat
python tools\final_stage\hairgpu_novelty01_matrix.py ^
  --repo . ^
  --viewer build\bin\Release\HairGpuViewer.exe ^
  --template docs\sample_assets\final_runtime_suite_template.json ^
  --timeline-root benchmarks\novelty01\sgapt_scenarios ^
  --output artifacts\novelty01\formal ^
  --profile formal ^
  --variants B0,B1,B2,B3,B4,B5,B6,B7,B8,P ^
  --scenarios all ^
  --repetitions 3 ^
  --require-fxc
```

Ablation：

```bat
python tools\final_stage\hairgpu_novelty01_matrix.py ^
  --repo . ^
  --viewer build\bin\Release\HairGpuViewer.exe ^
  --template docs\sample_assets\final_runtime_suite_template.json ^
  --timeline-root benchmarks\novelty01\sgapt_scenarios ^
  --output artifacts\novelty01\ablation ^
  --profile formal ^
  --variants A1,A2,A3,A4,A5,A6,A7,A8,A9,A10,A11,A12,A13,A14,A15 ^
  --scenarios S01,S05,S11,S12,S13,S14,S17,S18 ^
  --repetitions 3 ^
  --require-fxc
```

既存Suiteとの連携：

```bat
call tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\final_runtime_suite_template.json smoke

call tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\final_runtime_suite_template.json full
```

---

# 11. Ground Truth契約

Ground TruthはProposalと同じ入力状態から開始し、別Bufferで実行する。

一致必須：

- dt
- Solver Iterations
- Integration
- Root Constraint
- Distance Constraint
- Bend Constraint
- Compliance
- External Force
- Collider
- Self Collision設定
- Solver順序
- Precision

Ground TruthのGPU時間はProposal Performanceへ加算しない。Quality Runの総実行時間として別列へ保存する。

Ground Truth未取得時、Position RMSE、False Static、False Dynamic、Follower Position Error、Render Geometry Errorを未判定とする。未取得値を0として扱わない。

---

# 12. Telemetry検査

必須ファイル：

- CSV
- JSONL
- Summary JSON
- Requested Settings
- Applied Settings
- Observed Settings
- Adapter Identity
- Environment
- Result Integrity

必須条件：

- Availability Bitが指標に対応
- Measured Frame Indexが単調増加
- Source Frame Indexが追跡可能
- CSVとJSONLのFrame数・Index一致
- NaN／Infを0へ変換しない
- 未取得値を明示的Unavailableにする
- Counter Readback遅延を記録
- Counter Source Frameを記録
- GPU Timing Coverage 99%以上

---

# 13. 各Scenarioの必須Gate

| Scenario | 必須Gate |
|---|---|
| S01 | Cache Hit取得、Payload削減 |
| S02 | 全Block無条件Dirty禁止 |
| S03 | Segment Length異常0 |
| S04 | 部分Dynamic化確認 |
| S05 | End-to-End p95取得 |
| S06 | 局所BlockだけDynamic化 |
| S07 | Swept Collider候補取得 |
| S08 | 対象外GuideのDirty増加を制限 |
| S09 | Safety Transition遅延0 |
| S10 | Fast Path判断を記録 |
| S11 | Frame Full Dynamic成功 |
| S12 | Shared Point未解決0 |
| S13 | Hysteresis有効時Thrashing低減 |
| S14 | Follower Dependency Version一致 |
| S15 | Memory Leak Suspect 0 |
| S16 | Hash一致率99.9%以上 |
| S17 | Invalid Index 0、Duplicate Owner 0 |
| S18 | 古いCache再利用0、Fallback成功 |

---

# 14. Formal性能判定

```text
Average Frame Time <= 16.6667 ms
60 FPS Ratio >= 0.95
Frame Budget Overrun Rate <= 0.05
p95 Frame Time <= 16.6667 ms
p99 Frame Time <= 20.0 ms
Maximum Frame Time <= 50.0 ms
100 ms以上Frame = 0
```

研究上の最低条件：

- PはB2よりPayload使用量を削減
- PはB4よりState Copy Bytesを削減
- Pは通常ScenarioでB8よりEnd-to-Endを改善
- S11でB1比p95悪化5%以内を目標
- Payload Leak 0
- NaN／Inf 0
- Invalid Payload Index 0
- Duplicate Payload Owner 0

性能未達と証拠無効を混同しない。

- 正しい測定で性能未達：Evidence Valid / Goal Not Achieved
- Hash不足、Synthetic Metric、Fatal Failure、Hardware不一致：Evidence Invalid

---

# 15. 統計処理

- B8対Pは同一Seedのpaired比較
- Frame Timeは平均、中央値、p95、p99、最大
- 95% bootstrap confidence intervalを使用
- 品質誤差はmedian、p95、maximum
- 失敗Runは除外せずFailure Rateへ含める
- Warm-up Frameは集計へ含めない
- 外れ値は削除せず原因分類する
- Driver、温度、Clock変動を記録する

---

# 16. Artifact完全性

Case終了時に`hashes.json`を生成し、以下のSHA-256を保存する。

- Viewer EXE
- HLSL Source
- Case JSON
- Timeline JSON
- Frozen Asset
- Topology
- Generator Settings
- Telemetry CSV
- Telemetry JSONL
- Summary JSON
- Adapter JSON
- Environment JSON

Suite Summaryを最終書込みした後にRepro Manifestを生成し、以降Hash対象を変更しない。最後にManifestを検証してZIP化する。

---

# 17. 中断・失敗規則

以下は即時Failureとする。

- Device Removed
- Shader Compile Failure
- Fatal Runtime Failure
- Invalid Payload Index
- Duplicate Active Owner
- NaN／Inf
- Ground Truth Buffer共有
- Same-Frame Readback
- Timeline未適用
- Requested／Applied／Observed不一致
- Result Hash不足

Fallbackが成功して表示継続した場合でも、Fallback発火理由と適用単位を記録する。Fallbackを隠して正常Runとして扱わない。

---

# 18. 禁止事項

- Debug Buildの数値を正式結果へ使用する
- 24 FPS Gateを60 FPS達成として扱う
- Template Asset Manifestを正式証拠へ使用する
- 異なるCommit、EXE、Shader、Driverの結果をResumeで流用する
- Repro Manifest生成後にSummaryを書き換える
- Network Driveへ直接正式Telemetryを保存する
- 長時間Runの途中成果物をCOMPLETEとして扱う
- Availabilityなしの0を実測0として扱う
- Ground Truth込み時間をProposal単体性能として扱う

---

# 19. Mainへ統合する条件

- Formal開始Gate全通過
- B0 Regressionなし
- S01～S18 Smoke合格
- Release x64 Build合格
- FXC全Entry Point合格
- `overlay/`整理完了
- `#define private`暫定統合廃止
- Telemetry Schema固定
- Parameter Freeze完了
- PR上でBuild／Runtime Artifact確認

これらを満たすまでNovelty01からMainへマージしない。
