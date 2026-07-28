# Novelty 01 規範的実装契約・評価証拠仕様

## 0. 本文書の位置付け

本書は、`設計書/01_TopNovelty_NewStrandDataType.md`を置き換えるものではなく、同文書で不足していた以下を規範的に補う。

- 現在の実装状態と設計要件の対応
- Baseline／Ablationの実行契約
- S01～S18のCanonical Scenario契約
- 固定パラメータとFreeze手順
- GPU Resource／Binding契約
- Payload Lifetimeの厳密仕様
- Telemetry Traceability
- Ground Truth／Shadow／Performance測定の分離
- 統計評価計画
- End-to-End VRAM計上
- 実装完了証拠の管理
- `overlay/`を含むSource of Truth

矛盾がある場合、実装上の事実はソースコード、実験上の規約は本書、研究主張は`01_TopNovelty_NewStrandDataType.md`を優先する。

---

# 1. 文書情報

| 項目 | 内容 |
|---|---|
| 対象リポジトリ | `MKyosei2/HairSystem` |
| 対象ブランチ | `Novelty01` |
| 基準Main | `8d19bb546f3505fe25436d6ffa5b390a9b83897a` |
| 実装確認コミット | `a0521e258acdbd9d71262172d6d2ba208c338eeb` |
| 実装バージョン | 69.2 |
| 提案名 | Segment-Group Adaptive Payload Type |
| 略称 | SG-APT |
| 固定環境 | Windows / DirectX 11 / VS2022 / Release x64 |
| 固定負荷 | 100,000 strands / 4,000 guides / 96,000 followers / 128 points |
| 固定時間刻み | 1/60秒 |
| Solver反復 | 6 |
| 状態 | ソース統合済み、Windows実ビルド・実機Formal未完了 |

## 1.1 状態表記

実装状態は次の語だけを使用する。

| 状態 | 意味 |
|---|---|
| Not Implemented | ソースがない |
| Source Present | 型・関数・Shaderが存在する |
| Connected | Frame Pipelineへ接続されている |
| Build Passed | Release x64全体ビルド成功 |
| Shader Passed | 全Entry PointのFXCコンパイル成功 |
| Smoke Passed | S01、S06、S11、S17、S18の短時間実行成功 |
| Formal Passed | 規定回数の正式評価合格 |
| Hardware Passed | GTX 1660級実機評価合格 |

`Source Present`または`Connected`を「完成」「検証済み」と表現してはならない。

---

# 2. Source of Truthと統合方式

## 2.1 実行対象

実際のビルド・実行対象は以下とする。

- `apps/`
- `include/`
- `tools/`
- `tests/`
- `benchmarks/`
- `.github/`
- プロジェクトファイル

## 2.2 `overlay/`の扱い

`overlay/`は配布・適用パッケージ用の複製であり、正式実験時のSource of Truthではない。

1. Visual Studio、Python Runner、CIから`overlay/`を参照しない。
2. 同名ファイルが実行対象側と`overlay/`側に存在する場合、実行対象側を正とする。
3. MainへのPR前に、`overlay/`はGit管理対象から除外するか、Release Artifactへ移動する。
4. 実行対象と`overlay/`の内容一致を研究証拠として使用しない。
5. 実験Manifestには`overlay/`を含めない。

## 2.3 現在の暫定統合

現在の`GpuHairViewer.cpp`は、既存Version 68経路をLegacy名で保持し、P経路だけをSG-APTへルーティングする暫定統合である。`#define private`による宣言注入は最終アーキテクチャではない。

## 2.4 最終統合条件

Formal開始前に以下を満たす。

- `#define private`を廃止する
- `GpuHairViewer.h`へ正式宣言する
- Runtime所有権を通常のメンバーとして定義する
- `HairNoveltyTelemetry.h`へ正式列を定義する
- Shader LoaderとMSBuildへ全Entry Pointを登録する
- Legacy／Proposal切替を型安全なDispatcherへ置換する
- Shutdown、Reset、Single Step、Experiment Tickを正式経路へ接続する

---

# 3. 実装真実表

| ID | 要件 | 主な実装位置 | 現在状態 | Formal完了条件 |
|---|---|---|---|---|
| N01-001 | Fixed Core固定Index | `SegmentGroupAdaptivePayload.h` | Source Present | 30分間Index Hash不変 |
| N01-002 | Optional Dynamic Payload | 同上／Runtime HLSL | Connected | Static Groupの有効Payload 0 |
| N01-003 | Payload Owner | `PayloadOwner`／Owner Validation | Connected | Duplicate Owner 0 |
| N01-004 | 4世代遅延再利用 | `kPayloadGenerationCount=4` | Connected | Early Reuse 0 |
| N01-005 | Promote／Demote | Runtime HLSL | Connected | 遷移全ケース試験合格 |
| N01-006 | Static最終形状保存 | Static Cache更新 | Connected | Ground Truth誤差内 |
| N01-007 | Version拒否 | Descriptor／Validation | Connected | S18で古いCache再利用0 |
| N01-008 | Root剛体変換 | Root Local Cache | Connected | S02／S03で長さ異常0 |
| N01-009 | Hysteresis | Runtime Constants | Connected | S13でThrashing低減 |
| N01-010 | Minimum Residence | Runtime Constants | Connected | 安全昇格は遅延0 |
| N01-011 | Transition Budget | Runtime HLSL | Connected | Safety Transition欠落0 |
| N01-012 | Hard／Soft Descriptor | Reuse Descriptor | Connected | False Static率を計測 |
| N01-013 | Block Invalidation | Block Descriptor | Connected | Root移動で全Dirty禁止 |
| N01-014 | Block Skip | Indirect Dispatch | Connected | 実Dispatch削減を確認 |
| N01-015 | Shared Point Stable Reduce | Shared Point Pass | Connected | 3回Hash一致 |
| N01-016 | Baseline同品質Solver | Dynamic Pass | Source Present | B0同条件比較合格 |
| N01-017 | Follower依存伝播 | Dirty Range Pass | Connected | Dirty Follower範囲一致 |
| N01-018 | Render Dirty Range | Render Range Pass | Source Present | 実描画SRV／Drawへ接続 |
| N01-019 | Full Dynamic Fast Path | Cost Predictor | Connected | S11でB1悪化5%以内 |
| N01-020 | Watchdog／Fallback | Runtime／Evidence | Connected | S18で表示継続 |
| N01-021 | Ground Truth | Legacy B0隔離実行 | Connected | Quality Runのみ有効 |
| N01-022 | Shadow Dynamic | Shadow Pass | Connected | False Static検出率を保存 |
| N01-023 | Formal Telemetry | `17_...formal_evidence.inl` | Connected | CSV／JSONL Schema検査 |
| N01-024 | B1～B8 | Variant Dispatcher | Source Present | Execution Contract照合 |
| N01-025 | A1～A15 | Feature Mask | Source Present | 各要素だけが変化 |
| N01-026 | S01～S18 | Design Scenario Injection | Source Present | Canonical契約照合 |
| N01-027 | 30分試験 | Matrix `soak` | Source Present | 108,000 frames完走 |
| N01-028 | 決定性試験 | Matrix `determinism` | Source Present | Hash一致率99.9%以上 |
| N01-029 | GTX1660試験 | Matrix `gtx1660` | Source Present | Adapter Identity一致 |
| N01-030 | Release x64 Build | VS2022 | Not Implemented as evidence | Build Log保存 |

---

# 4. Baseline実行契約

全方式で共通する条件は、同一Asset、同一Camera、同一Seed、1/60秒、6反復、同一Collider、同一Render設定とする。

| ID | 実行ルート | Payload | Static判定 | Compaction | Follower | Render | SG-APT固有機構 |
|---|---|---|---|---|---|---|---|
| B0 | Version 68 Legacy | 全Group相当 | なし | なし | 全更新 | 全更新 | なし |
| B1 | Optimized Full Dynamic | 全Dynamic | なし | Indirect | 共通最適化 | 共通最適化 | なし |
| B2 | Sleeping Flag | 全Group保持 | Sleep Flag | なし | 共通 | 共通 | なし |
| B3 | Active List | 全Group保持 | 単純判定 | Active List | 共通 | 共通 | なし |
| B4 | Separate Buffers | Static／Dynamic別 | 単純判定 | 移送List | 共通 | 共通 | 固定Indexなし |
| B5 | Fixed Core＋Payload | Optional | 速度閾値のみ | Stable可 | 共通 | 共通 | Lifetime／Descriptorなし |
| B6 | SG-APT No Block | Optional | SG-APT | 全Group評価 | 局所 | 局所 | Block Skipなし |
| B7 | Guide Local Only | Optional | SG-APT | Stable | 全Follower | 全Render | 依存伝播なし |
| B8 | Strongest Practical | 全Groupまたは非統合Sidecar | Sleeping＋Block | Stable | 局所 | 局所 | 統合データ型・Lifetimeなし |
| P | Proposed | Optional | Hard／Soft Descriptor | Stable Prefix Sum | 局所 | 局所 | 全SG-APT |

## 4.1 B8とPの分離条件

B8とPで共通化してよいもの：

- GPU Resident Buffer
- Persistent Buffer
- Dirty Range
- Follower局所更新
- Indirect Dispatch／Draw
- LOD
- Frustum Culling
- GPU Timer

Pだけが使用してよいもの：

- Fixed Core＋Optional Payloadの統合表現
- Payload Version／Owner／Lifetime
- SG-APT状態遷移
- SG-APT不変条件
- SG-APT固有Fallback

B8がPのCore、Payload Owner、Payload Generationを参照した場合、そのCaseは無効とする。

## 4.2 Baseline検査

各Case開始時に以下をManifestへ出力する。

- Variant ID
- Feature Mask
- Solver Route
- Payload Layout
- Fixed Index有無
- Follower更新方式
- Render更新方式
- Fast Path有無
- Ground Truth有無

---

# 5. Ablation契約

A1～A15は、一度に一要素だけを無効化する。複数要素が連鎖的に無効になる場合、依存関係をManifestへ記録する。

| ID | 無効化対象 | 主要比較指標 |
|---|---|---|
| A1 | Optional Payload | Payload Memory／帯域 |
| A2 | Fixed Index | State Copy Bytes／遷移時間 |
| A3 | Version Check | False Static／古いCache使用 |
| A4 | Delayed Free | Owner不整合／再利用事故 |
| A5 | Hysteresis | Thrashing率 |
| A6 | Minimum Residence | 遷移数／反応遅延 |
| A7 | Static Cache | 再構築量／誤差 |
| A8 | Incremental Block | Descriptor時間 |
| A9 | Stable Shared Point | Hash一致率 |
| A10 | Follower Dependency | Follower更新点数 |
| A11 | Render Dirty Range | Render再生成頂点数 |
| A12 | Fast Path | 全動的p95 |
| A13 | Shadow Dynamic | False Static検出率 |
| A14 | Watchdog | 異常復帰率 |
| A15 | Stable Compaction | 決定性／Compaction時間 |

---

# 6. Canonical Scenario契約

`novelty01_design_scenario`を正式なScenario IDとする。既存Viewerの`S02_Breathing`等は互換上のRuntime Aliasであり、研究上の意味を定義しない。Canonical注入が実行されなかったCaseは無効とする。

| ID | Canonical入力 | 主対象 | 期待する観測 |
|---|---|---|---|
| S01 | 完全静止 | Static Reuse | Payload減少、Cache Hit増加 |
| S02 | Root平行移動 | Static Transform | 全Group無条件Dirty禁止 |
| S03 | Root回転 | Root Local Cache | Segment長維持 |
| S04 | 弱風 | Descriptor | 部分Dynamic |
| S05 | 通常頭部運動 | End-to-End | 通常負荷p95 |
| S06 | 局所衝突 | Block Invalidation | 局所Dynamic化 |
| S07 | 複数Collider | Swept Query | Dirty Block限定 |
| S08 | 一本単位編集 | Edit Version | 対象周辺のみ更新 |
| S09 | 連続Brush編集 | Transition Budget | 安全昇格遅延0 |
| S10 | 強風 | Fast Path | Dynamic Ratio増加 |
| S11 | 全Group Dynamic | Full Dynamic | B1比悪化5%以内 |
| S12 | Static／Dynamic境界Stress | Shared Point | 境界誤差抑制 |
| S13 | Thrashing Stress | Hysteresis | 往復遷移低減 |
| S14 | 複数Guide依存Follower | CSR Dependency | 局所Follower更新 |
| S15 | 30分試験 | Leak／Stability | 108,000 frames完走 |
| S16 | 決定性 | Stable Reduce／Scan | Hash一致率99.9%以上 |
| S17 | Payload Pool Stress | Overflow | 不正Index 0、Fallback成功 |
| S18 | Version／Descriptor異常 | Watchdog | 古いCache利用0、表示継続 |

## 6.1 Scenario Manifest必須項目

- Canonical ID
- Runtime Alias
- Timeline JSON Hash
- 開始／終了Frame
- 入力値
- 対象Guide／Group
- Seed
- 期待Dynamic Ratio範囲
- 期待Fallback単位
- 必須Telemetry列
- 合格条件

## 6.2 S17追加条件

- Payload CapacityをManifestへ記録
- Overflow予定Frameを記録
- Invalid Payload Index 0
- Duplicate Owner 0
- Safe Stopは継続不能時だけ許可
- Frame Fallback後の復帰条件を記録

---

# 7. Frozen Parameter Table

以下はVersion 69.2の現在値であり、Calibration完了までは`Provisional`とする。Formal開始前にコミットHashを付けて`Frozen`へ変更する。

| パラメータ | 現在値 | 単位 | 状態 |
|---|---:|---|---|
| groupsPerBlock | 32 | groups | Provisional |
| scanThreadCount | 256 | threads | Frozen by ABI |
| counterCount | 64 | counters | Frozen by ABI |
| dispatchArgumentCount | 8 | records | Frozen by ABI |
| payloadGenerationCount | 4 | generations | Provisional |
| descriptorMaximumAge | 64 | frames | Provisional |
| minimumResidenceFrames | 12 | frames | Provisional |
| consecutiveStaticFrames | 8 | frames | Provisional |
| staticTransitionBudget | 32768 | groups/frame | Provisional |
| payloadRetireDelayFrames | 4 | frames | Provisional |
| staticPositionThreshold | 0.10 | mm | Provisional |
| dynamicPositionThreshold | 0.20 | mm | Provisional |
| staticVelocityThreshold | 0.10 | mm/frame相当 | Provisional |
| dynamicVelocityThreshold | 0.20 | mm/frame相当 | Provisional |
| lengthErrorThreshold | 0.02 | ratio | Provisional |
| boundaryErrorThreshold | 2.0 | mm | Provisional |
| collisionSafetyPadding | 2.0 | mm | Provisional |
| shadowErrorThreshold | 0.50 | mm | Provisional |
| fullDynamicEnterRatio | 0.70 | ratio | Provisional |
| fullDynamicExitRatio | 0.55 | ratio | Provisional |
| fullDynamicDecisionFrames | 8 | frames | Provisional |
| solverIterations | 6 | iterations | Frozen |
| deltaTime | 1/60 | seconds | Frozen |
| hashBucketCount | 32768 | buckets | Provisional |
| selfCollisionRadius | 0.012 | m | Provisional |
| gridCellSize | 0.085 | m | Provisional |

## 7.1 Freeze規則

- Calibration CaseとFormal Caseを分離する
- Formal結果を見て値を変更しない
- Freeze日、コミット、担当者、理由を記録する
- Freeze後の変更は新しいExperiment Versionとして扱う

---

# 8. GPU Resource Contract

## 8.1 構造体Stride

| 型 | Stride |
|---|---:|
| SegmentGroupCore | 64 bytes |
| CoreExtension | 80 bytes |
| StaticCache | 64 bytes |
| DynamicPayload | 64 bytes |
| PayloadOwner | 32 bytes |
| ReuseDescriptor | 80 bytes |
| BlockDescriptor | 64 bytes |
| SharedPointCandidate | 48 bytes |
| ScanRecord | 16 bytes |
| GuideDirtyRange | 16 bytes |
| FollowerDirtyRange | 32 bytes |
| RenderDirtyRange | 16 bytes |
| FollowerDependencyEntry | 32 bytes |
| GroundTruthMetric | 32 bytes |
| DispatchArguments | 16 bytes |

## 8.2 Resource台帳

各Bufferについて以下を実験Manifestへ出力する。

- Buffer名
- 要素型
- Stride
- 要素数
- 総Byte
- Usage
- BindFlags
- SRV／UAV Format
- Counter有無
- Ping-Pong世代
- 初期化Pass
- Read Pass
- Write Pass
- 解放時点

最低限のResource分類：

- Core
- Extension
- Static Cache
- Dynamic Payload × 4 generations
- Payload Owner × 4 generations
- Descriptor
- Block Descriptor
- Scan Flags／Prefix／Block Offset
- Dynamic／Static／Repair／Invalid List
- Shared Point Candidate
- Guide／Follower／Render Dirty Range
- Follower Dependency
- Ground Truth Position／Previous Position／Metric
- Persistent Render Position Cache
- Indirect Dispatch／Draw Arguments
- Self-Collision Grid
- Counter／Readback Ring

## 8.3 D3D11 Binding規則

1. 同一SubresourceをSRVとUAVへ同時Bindしない。
2. Pass終了時に使用したSRV、UAV、CB、ShaderをUnbindする。
3. UAV Slot数は8以下とする。
4. Indirect Argumentsを書いた後、UAVを解除してから`DispatchIndirect`または`Draw*Indirect`を呼ぶ。
5. Ground Truth BufferをProposalの書込先と共有しない。
6. Counter Readbackは遅延Ringを使用し、Formal中のSame-Frame Readbackを禁止する。

## 8.4 Entry Point契約

各Entry Pointを機械生成Manifestへ記録する。

- Entry Point名
- Binding Mode
- t0～tN
- u0～uN
- b0～bN
- Dispatch式
- Read Set
- Write Set
- 前提Pass
- 後続Pass
- Unbind Set

Manifestに存在しないEntry PointをFormal実行してはならない。

---

# 9. Payload Lifetime厳密仕様

## 9.1 状態

```text
Free -> Active -> Retired -> SafeToReuse -> Free
```

## 9.2 所有権不変条件

```text
payloadOwner[p].groupIndex == g
coreExtension[g].payloadIndex == p
payloadOwner[p].payloadVersion == coreExtension[g].payloadVersion
payloadOwner[p].generation == coreExtension[g].payloadGeneration
```

## 9.3 Allocate

1. Stable Compaction後のDynamic Rankを得る。
2. SafeToReuseまたはFree Slotだけを候補とする。
3. Ownerを先に予約する。
4. Payloadを初期化する。
5. Core Extensionを更新する。
6. Payload Valid Flagを最後に立てる。

## 9.4 Retire

1. Static Cacheへ最終結果を保存する。
2. Payload Valid Flagを下ろす。
3. Owner状態をRetiredへ変更する。
4. `retireEpoch`と`safeReuseEpoch`を設定する。
5. CoreのPayload IndexをInvalidへ変更する。

## 9.5 再利用条件

```text
currentEpoch >= safeReuseEpoch
かつ
payloadGenerationがGPU in-flight世代と重ならない
かつ
Owner Validationで参照Groupが0件
```

同一FrameでRetireと再Allocateを行わない。

## 9.6 Wraparound／Reset

- Epoch比較は符号なし差分で定義する
- Simulation Reset時に全OwnerをFreeへ戻す
- Device Lost時にPayload Poolを破棄して再生成する
- Ground TruthとProposalでPoolを共有しない

## 9.7 Overflow

1. 新規Demoteを停止
2. 既存Dynamicを維持
3. Baseline BufferへFallback
4. Frame Full Dynamicへ移行
5. Telemetry記録
6. 継続不能時だけSafe Stop

---

# 10. Telemetry Traceability Matrix

`0`と未取得を区別する。Availability Bitがない値を合格判定へ使用してはならない。

| RQ | 指標 | Counter／Source | 出力列 | 集計 | 主判定 |
|---|---|---|---|---|---|
| RQ1 | Payload使用数 | Counter_PayloadActive | payloadActiveCount | p50/p95/max | B2より削減 |
| RQ1 | Allocation／Retire／Reuse | Counter 32～34 | payloadAllocated等 | total/frame | Leak 0 |
| RQ2 | Group Read／Write Bytes | Counter 53～54 | groupReadBytes等 | total/frame | B3より削減 |
| RQ2 | Payload Read／Write Bytes | Counter 55～56 | payloadReadBytes等 | total/frame | Static時削減 |
| RQ3 | Transition Bytes | Counter 52／57 | transitionBytes／stateCopyBytes | total/frame | B4より削減 |
| RQ4 | Position RMSE | GroundTruthMetric | positionRmseMm | avg/p95/max | Freeze閾値内 |
| RQ4 | False Static | Counter 46 | falseStaticRate | count/attempt | 0.1%以下 |
| RQ4 | False Dynamic | Counter 31 | falseDynamicRate | count/sample | 記録必須 |
| RQ5 | End-to-End | GPU Query | totalHairGpuMs／frameMs | avg/p95/p99 | B8より改善 |
| RQ5 | Follower更新 | Counter 43 | followerRebuiltPoints | total/frame | A10比較 |
| RQ5 | Render更新 | Counter 44 | renderRegeneratedVertices | total/frame | A11比較 |
| RQ6 | Full Dynamic | Counter 23 | fullDynamicRequested | rate | S11で成功 |
| Safety | Invalid Payload | Counter 38 | invalidPayloadIndex | total | 0 |
| Safety | Duplicate Owner | Counter 37 | duplicatePayloadOwner | total | 0 |
| Safety | NaN／Inf | Counter 59／60 | nanCount／infCount | total | 0 |
| Safety | Penetration | Counter 61 | penetrationCount | total/max | 重大0 |

## 10.1 値の種別

- Measured：GPU Counter、GPU Query、実Readback
- Derived：Measuredから式で算出
- Estimated：モデル推定。Formal合否には使用しない
- Unavailable：Availability Bitなし
- Invalid：取得失敗またはSchema不一致

---

# 11. Ground Truth／Shadow／Performanceの分離

## 11.1 Quality Run

- Ground Truth有効
- Proposal入力確定前の同一状態を隔離Bufferへ複製
- 同一dt、反復、Compliance、Collision、Root条件、数値精度、Solver順序
- 性能合否には使用しない
- Position、Length、Boundary、Collision、False Staticを評価

## 11.2 Performance Run

- Ground Truth無効
- ProposalまたはBaseline単体
- Same-Frame Readback禁止
- GPU TimerとFrame Timeのみで性能判定

## 11.3 Shadow Run

- Static Groupの一部だけをDynamic計算
- 表示へ使用しない
- 閾値付近、長時間Static、Collision Margin小、過去False Staticを優先
- Shadow実行コストを別列で保存

## 11.4 Determinism Run

- 同一Asset、Seed、入力、Adapter、Driver
- 3回以上
- State Sequence Hash
- Compaction Sequence Hash
- Final Position Hash

## 11.5 Soak Run

- 108,000 measured frames
- Ground Truthは間引き実行可
- 間隔をManifestへ記録
- VRAM、Owner、Payload Active、Retired数を時系列保存

---

# 12. 統計評価計画

- Warm-up：600 framesを原則とする
- Formal測定：6,000 frames × 3 repetitions以上
- Determinism：同一条件3回以上
- Seed：固定Seedのpaired比較に加え、複数Seedを補助評価する
- 報告：平均、中央値、p95、p99、最大、95% bootstrap CI
- 比較：B8対Pはpaired comparison
- 品質：median、p95、maximumを併記
- 失敗Run：除外せず失敗率として記録
- 中断Run：完走Runへ混ぜず、Failureとして別集計
- 外れ値：除外しない。原因を分類する
- Driver／温度／Clock／電源設定を記録する
- Formal中の他プロセス負荷を記録する

## 12.1 有意性より先に満たす条件

- Schema完全
- Availability完全
- Adapter一致
- Frozen Asset一致
- Build／Commit一致
- Fatal Failure 0
- NaN／Inf 0

---

# 13. End-to-End VRAM計上

以下をSteady-StateとPeakに分けて記録する。

- Current／Previous／Rest Position
- Core／Extension／Static Cache
- Payload × 4 generations
- Payload Owner × 4 generations
- Descriptor／Block
- Prefix Sum／Lists
- Shared Point Candidate
- Follower Dependency／Dirty Range
- Persistent Render Cache
- Ground Truth Position／Previous／Metrics
- Shadow Buffer
- Self-Collision Grid
- Telemetry Readback Ring
- GPU Query
- Render Target／Depth／OIT

## 13.1 必須報告

| モード | Steady VRAM | Peak VRAM |
|---|---:|---:|
| B0 | 必須 | 必須 |
| B8 | 必須 | 必須 |
| P Performance | 必須 | 必須 |
| P Quality＋Ground Truth | 必須 | 必須 |
| P Soak | 必須 | 必須 |

Main MemoryとDXGI Local／Non-Local Usageを分離する。

---

# 14. 実装完了証拠

チェックボックスだけで完了扱いにしない。各要件へ以下を紐付ける。

- Requirement ID
- Source File
- Function／Entry Point
- Build Log
- Shader Log
- Test Command
- Result Artifact
- Adapter／Driver
- Commit
- Result Hash
- Status

## 14.1 Artifact命名

```text
N01_<profile>_<variant>_<scenario>_r<NN>/
  case.json
  environment.json
  adapter.json
  settings_requested.json
  settings_applied.json
  settings_observed.json
  telemetry.csv
  telemetry.jsonl
  summary.json
  preflight.json
  build.log
  shader.log
  hashes.json
```

## 14.2 Formal合格に必要な証拠

- Release x64 Build Log
- FXC全Entry Point Log
- Native Self-Test
- Shader Self-Test
- Preflight
- B0／B8／P
- B2～B7
- A1～A15
- S01～S18
- 30分Soak
- Determinism
- GTX1660実機

---

# 15. 先行方式との差分表

| 方式 | Fixed Core | Optional Payload | Group内混在 | Lifetime | Fixed Index | Version Cache | Formal Fallback |
|---|---:|---:|---:|---:|---:|---:|---:|
| Sleeping | × | × | △ | × | ― | △ | △ |
| Active List | × | × | ○ | × | ○ | △ | △ |
| Static／Dynamic Buffer | × | △ | ○ | △ | × | △ | △ |
| B8 | × | ×または非統合 | ○ | × | ○ | ○ | ○ |
| SG-APT | ○ | ○ | ○ | ○ | ○ | ○ | ○ |

この表の各比較行は、最終論文で参考文献または比較実装へ紐付ける。

---

# 16. Formal開始Gate

以下がすべて満たされるまで正式評価を開始しない。

- [ ] 文書の基準Commitが実行Commitと一致
- [ ] `overlay/`を実行対象から除外
- [ ] `#define private`暫定統合を解消
- [ ] Release x64 Build Passed
- [ ] 全Shader Entry Point Passed
- [ ] Frozen Parameter Table確定
- [ ] Canonical S01～S18 Timeline固定
- [ ] B0～B8 Execution Manifest確認
- [ ] Telemetry Availability完全
- [ ] Ground TruthとPerformance Run分離
- [ ] Payload Owner／Version／Generation検査合格
- [ ] Render Dirty Rangeが実描画へ接続
- [ ] S17 Overflow Smoke合格
- [ ] S18 Fault Injection Smoke合格

---

# 17. 変更管理

Formal開始後に以下を変更した場合、Experiment Versionを更新して全比較を再実行する。

- データ構造Stride
- Feature Mask
- Payload Capacity／Generation
- 状態遷移閾値
- Solver条件
- Scenario入力
- Telemetry Schema
- Follower Dependency
- Render更新方式
- Fast Path判定

結果を見てから有利なCaseだけを再実行してはならない。
