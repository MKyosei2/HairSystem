# Novelty 01: New Strand Data Type ― 安全版完全設計書

- 文書版: 01-Safe v3.4 Stable Runtime
- 基準ブランチ: `Novelty`
- 基準コミット: `375a7cce081625ec185cbce3dee449e66fd4f13b`（61.67）
- 対象: GPU常駐型HairSystemにおけるSegment Group統合状態表現
- 本文書の役割: 新規性本体の設計と、再現可能で安定した評価・動作環境の契約を一体で定義する

## 1. 研究目的

本研究は、Hair Strandを単なる点列ではなく、共有点の所有関係、静的・動的状態、復旧情報、証明情報を統合したSegment Groupとして扱う。状態を別配列へ分散させず、処理に必要な意味をデータ表現へ統合することで、GPU常駐のまま静的再利用、動的更新、境界修復、予測Wake、圧縮実行を切り替える。

新規性の中心は「Sleep機能」「Cache機能」「Compaction機能」の単体ではない。点―線―点単位のSegment Groupが、次の情報を同じレコード上で保持し、各段階が同じ意味論を共有する統合表現にある。

- 静的／動的／修復待ち／Rejectなどの状態
- 共有点の親子所有権と方向責任
- Parent、Collider、Edit、TopologyのVersion
- 静的再利用のCertificate
- Predictive WakeとVelocity Recoveryに必要な履歴
- GPU Compaction対象とRepair対象の分類情報

## 2. 主Formal評価条件

主結果は次の固定条件だけで取得する。異なる規模や反復数の結果を主結果へ混在させない。

| 項目 | 固定値 |
|---|---:|
| Guide Strand | 4,000本 |
| Follower / Guide | 24本 |
| 表示Strand合計 | 100,000本 |
| Particle / Guide | 128点 |
| Solver Iteration | 6回 |
| 解像度 | 1920×1080 |
| Fixed Timestep | 1/60秒 |
| VSync | OFF |
| Present Sync Interval | 0 |
| Warm-up | 600 Frame以上 |
| Measurement | 6,000 Frame以上 |
| Seed | 5種類以上 |
| Build | Release x64 |

100,000本は `4000 × (1 Guide + 24 Follower)` で構成する。補助スケーラビリティ試験を行う場合は、主Formal結果と別表・別グラフ・別Statusで扱う。

## 3. 比較方法

| Code | 比較内容 |
|---|---|
| B0 | 全量動的更新 |
| B1 | Strand単位Sleep |
| B2 | Block単位Sleep |
| B3 | Segment Threshold Sleep |
| B4 | Segment Hysteresis Sleep |
| B5 | Segment Cache |
| B6 | Segment GPU Compaction |
| B7 | Active-setのみ |
| B8 | 外部State Arrayを使う同等機能構成 |
| P | Segment Group内にStateを統合した提案手法 |

B8とPは状態格納方式以外の機能Flagを一致させる。これにより、速度差・メモリ差・安定性差を「統合データ表現」の差として比較できるようにする。

## 4. Formal Asset契約

正式測定では、起動時の手続き生成AssetやCache AssetへのFallbackを禁止する。正式Assetは次の手順で固定する。

1. Noveltyブランチ上で全修正をCommitする。
2. WorktreeがCleanであることを確認する。
3. `.hairbin`、Topology、Generator Settingsを生成する。
4. `freeze_novelty01_asset.py`で各SHA-256、Size、Commit SHAを記録する。
5. `freezeStatus = FROZEN`のManifestを出力する。
6. Formal Run開始前と終了後にAsset Hashを再検証する。

テンプレートManifestは証拠ではない。`TEMPLATE_REQUIRES_LOCAL_FREEZE`のまま正式測定へ使用した場合は、Formal Contract Invalidとする。

## 5. 安定60 FPSの定義

安定60 FPSは平均FPSだけでは判定しない。各Runの既定Gateを次とする。

| 指標 | 合格条件 |
|---|---:|
| Average FPS | 60.0以上 |
| 60 FPS Ratio | 0.95以上 |
| 16.6667ms超過率 | 0.05以下 |
| p95 Frame Time | 16.6667ms以下 |
| p99 Frame Time | 20.0ms以下 |
| 最大Frame Time | 50.0ms以下 |
| 100ms超Frame | 0 |
| GPU Timing Coverage | 0.99以上 |

`60 FPS Ratio`はFrame Timeが16.6667ms以下であった測定Frameの割合とする。平均60 FPSだけを満たし、p95や超過率を満たさないRunは安定60 FPSとは認めない。

## 6. 証拠有効性と目標達成の分離

測定が正しく完了したかと、仮説を支持したかを分離する。

### Evidence Status

- `complete_valid`: 必要成果物、Hash、Frame数、CSV/JSONL整合、Hardware、Timeline、Asset、Flag、Metric Coverageが有効
- `complete_invalid`: 欠落、改ざん、不一致、Synthetic Metric、NaN/Inf、Fatal Failureなどで証拠として使用不能

### Stable 60 FPS Status

- `achieved`: Evidenceが有効で、全性能Gateを満たす
- `not_achieved`: Evidenceは有効だが性能Gate未達、またはEvidenceが無効

### Exit Code

| Code | 意味 |
|---:|---|
| 0 | Evidence有効・安定60 FPS達成 |
| 1 | Evidence有効・目標未達 |
| 2 | Formal Contract無効 |
| 3 | Runtime失敗／Timeout／非0終了 |
| 4 | 成果物整合性失敗 |
| 5 | 設定失敗 |

目標未達の有効データを削除しない。研究上は「有効な反証または限界データ」として保持する。

## 7. Execution Fingerprint

Resumeで古い結果を再利用しないため、Runごとに次をCanonical JSON化してSHA-256を計算する。

- Commit SHA
- Executable SHA-256
- Shader Bundle SHA-256
- Asset Manifest SHA-256
- Asset SHA-256
- Topology SHA-256
- Generator Settings SHA-256
- Scenario Timeline SHA-256
- Method Settings SHA-256
- Hardware Profile名とProfile定義Hash
- GPU名、Vendor/Device ID、Driver Version
- OS Build、Python Version

`--resume`は現在のFingerprintと既存結果のFingerprintが完全一致し、Artifact Manifestの全Hashが再検証できた場合だけ再利用する。

## 8. Hardware Profile契約

主評価は `low_gtx1660_reference` を用いる。

- GTX 1660 / SUPER / Ti級
- Dedicated VRAM 5,800～7,000 MiB
- CPU物理6 Core以上
- System RAM 32 GiB以下
- AC電源接続
- 高性能系Power Scheme
- 1920×1080、VSync OFF
- GPU温度88℃以下
- 測定中温度差12℃以下
- GPU Clock Drop 15%以下

Profileと実測値が一致しないRunは、別Hardwareの参考結果として保存できるが、主Formal結果へ含めない。

## 9. Scenario Assertion

S01～S18のTimeline自身をAssertionの正本とする。Suite Configへ同じAssertionを重複記述しない。

全Scenarioで次を検証する。

- Eventに安定した`id`を付与する
- `exactAppliedEventCount`と実測Event数を一致させる
- Timelineの最終Frameまで完走する
- 必要に応じてCollision候補、Wake、Repair、State Transitionの最低数を検証する
- All StaticではState Oscillation上限を検証する
- Event数0のScenarioでも、連続Root MotionやWindにより期待されるState Transitionを検証する

単に「1回以上状態が変わった」だけで意図したScenarioを通過したとは扱わない。Timeline HashとScenario IDもFingerprintへ含める。

## 10. Telemetryと成果物

各Runは少なくとも次を出力する。

- Case JSON
- Frame CSV
- Frame JSONL
- Summary JSON
- Settings JSON
- Environment JSON
- Hardware JSON
- Validation JSON
- Artifact Manifest JSON
- Stdout / Stderr
- COMPLETE Marker

CSVとJSONLはFrame数、`sourceFrameIndex`、必須Fieldを照合する。次を検出した場合はEvidence Invalidとする。

- 行欠落、重複、順序逆転
- CSV/JSONL不一致
- NaN、Infinity
- `syntheticMetricCount > 0`
- `fatalFailure != 0`
- GPU Timing Coverage不足
- Requested／Applied／Observed Flag不一致
- Manifest Hash不一致

## 11. Repro Manifest

Suiteの全Reportを生成した後、Summaryを最終書込みし、その後にRepro Manifestを最後に生成する。Manifest作成後はHash対象ファイルを書き換えない。

順序は次とする。

1. Run結果集計
2. CSV、SQLite、Markdown生成
3. Suite Summary最終書込み
4. 全成果物Hash計算
5. Repro Manifest書込み
6. Repro Manifest再検証
7. ZIP生成

## 12. Failure Injection Self-test

実測前に次の失敗を自動注入し、Validatorが拒否することを確認する。

- CSV最終行欠落
- JSONL行数欠落
- CSV/JSONLのFrame Index不一致
- Manifest改ざん
- Fingerprint不一致
- Asset／Timeline／Settings Hash不一致
- GPU Timing Coverage 98.9%
- Synthetic Metric混入
- Fatal Frame
- NaN／Infinity
- 未対応Goal
- SummaryをManifest生成後に書き換えるケース

Self-testが1件でも失敗した場合、正式Suiteを開始しない。

## 13. 長時間安定性

次の順にSoak Testを行う。

1. 30分: Walk
2. 2時間: Strong Wind
3. 8時間: State Thrashing

長時間Runでは次を合格条件とする。

- Timeoutなし
- Viewer非0終了なし
- D3D11 Device Removed／Resetなし
- NaN／Infinityなし
- VRAM Peak 6,144 MB以下
- VRAM増加傾向 1 MB/min以下
- RAM増加傾向 2 MB/min以下
- p99劣化率1.10以下
- Unresolved Query増加傾向0
- RUNNING Markerなし、COMPLETE Markerあり
- Artifact Manifest整合

RunnerはWindowsのSystem/Display Sleepを抑止し、Timeout時はProcess Treeを終了する。Run Lockにより同じCaseの二重実行を禁止する。Network Driveは正式出力先として拒否し、開始前に20 GiB以上の空き容量を要求する。

## 14. 実行順序

1. Release x64 Rebuild
2. Native ABI / workload self-test
3. Shader load / compile self-test
4. Python compile
5. Python self-test
6. unittest
7. Strict dry-run preflight
8. Smoke: B0 × S01 × Seed 1103
9. Baseline比較
10. 全Formal Matrix
11. Determinism Replay
12. 30分、2時間、8時間Soak

## 15. 完了判定

次を全て満たした時点で「安定したテスト・動作環境が成立した」と判定する。

- 固定Formal条件が全ファイルで一致している
- Frozen Assetが実データで作成されている
- Hardware Profileが実測値と一致している
- 10手法×18Scenario×5Seedが欠落なく実行される
- ResumeがFingerprint完全一致の場合だけ働く
- Evidence StatusとGoal Statusが分離されている
- Repro Manifestが自己整合する
- Failure Injectionを全件拒否できる
- 8時間試験でRuntime Failure、Device Reset、Memory Leak傾向がない
- 結果ZIPを別PCでHash検証できる

