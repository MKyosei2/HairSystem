
---

# 61. Version 69.4 完全実装追補

## 61.1 目的

Version 69.4では、Version 69.3までに残っていた次の実装上の不足を解消する。

1. B1とB8がSG-APT内部表現を共有し過ぎる問題
2. Baselineごとの許可機構と禁止機構を実行時に検査できない問題
3. Windows Release x64、FXC、Native Self-Testを継続的に実行するGateがない問題
4. 実験結果、ビルド結果、Shader結果、実機結果を一つのMerge Gateで判定できない問題
5. 取得できなかった指標を0として扱う危険

この追補はファイル数削減を目的としない。`overlay/`および`replacement/`の整理は後続の圧縮工程へ分離し、Version 69.4では実行Source of Truthがそれらを参照しないことだけを必須とする。

## 61.2 実行Source of Truth

正式な実行Source of Truthは以下に限定する。

```text
apps/dx11_viewer/src/
apps/dx11_viewer/shaders/
include/hairgpu/
tools/final_stage/
benchmarks/novelty01/
tests/final_stage/
docs/experiments/
docs/sample_assets/
設計書/
HairSystem.vcxproj
.github/workflows/novelty01-formal-gate.yml
```

`overlay/`および`replacement/`が残る場合でも、次を満たさなければならない。

* `HairSystem.vcxproj`が参照しない
* Runtime Shader探索対象に含めない
* Python正式Runnerが参照しない
* Formal結果のSource Hash計算対象に含めない
* MainへMergeする前の圧縮工程で削除候補として扱う

## 61.3 Variant Execution Contract

各Variantは`VariantExecutionContract`を持つ。

```cpp
struct VariantExecutionContract
{
    ExecutionRoute route;
    PayloadPolicy payloadPolicy;
    DependencyUpdatePolicy followerPolicy;
    DependencyUpdatePolicy renderPolicy;
    uint64_t requiredFeatures;
    uint64_t forbiddenFeatures;
    bool usesSgaptRepresentation;
    bool usesSgaptPayloadLifetime;
    bool requiresDedicatedPerFrameRoute;
};
```

Runtime開始時に、Feature Maskが`requiredFeatures`をすべて含み、`forbiddenFeatures`を一つも含まないことを検査する。違反時は推測による継続を禁止し、Formal Failureとする。

## 61.4 B1独立経路

B1は次の条件を満たす独立Full Dynamic Baselineとする。

* SG-APT Core Bufferを生成しない
* Optional Payload Poolを生成しない
* Payload Owner Tableを生成しない
* Static CacheおよびReuse Descriptorを生成しない
* SG-APT Compactionを実行しない
* 既存のGPU Resident Full Dynamic Solverを使用する
* CPU Readbackを正式計測中は無効にする
* `n01UsesSgaptRepresentation = 0`を記録する

B1でSG-APT Payload活動が1件でも観測された場合、そのRunは無効とする。

## 61.5 B8独立経路

B8は、一般的な実用最適化を組み合わせるがSG-APT表現を使用しない。

許可する機構：

* Guide／Follower
* 既存Active Set
* 既存Tier Update
* GPU Resident Buffer
* Persistent Render Buffer
* 一般的Dirty Range
* Indirect Draw
* LOD

禁止する機構：

* Fixed Core＋Optional Payload
* Payload Owner Table
* Payload Epoch／Retire／Safe Reuse
* SG-APT Hard／Soft Descriptor
* SG-APT Transition Budget
* SG-APT Versioned Static Cache

B8も`n01UsesSgaptRepresentation = 0`を必須とし、Payload活動が観測された場合は比較対象として失格とする。

## 61.6 Controlled Baseline Adapter

B2～B7は同一のGPU Pass実装を比較用Adapterとして共有してよい。ただし、各方式の禁止機構は実行時Contractで無効化され、TelemetryへFeature MaskとExecution Routeを保存する。

共有はコード再利用のためであり、提案方式固有機構を有効にしたまま比較することを許可するものではない。

## 61.7 Canonical ScenarioのBaseline入力

B1およびB8にも、Pと同じ外部入力クラスを適用する。

* Root Motion
* Wind
* Collider Motion
* User Edit Invalidation
* Soak Motion
* Deterministic Motion

ただしS18のSG-APT内部Version／Descriptor破壊はB1およびB8へ注入しない。これはBaselineまで共有状態を破壊しないことを確認するためである。

## 61.8 Telemetry Availability

次のAvailabilityを追加する。

```text
HairTelemetryAvailability_Novelty01Sgapt
HairTelemetryAvailability_Novelty01Baseline
```

独立Baselineでは以下を保存する。

```text
n01ExperimentVariant
n01ExecutionRoute
n01UsesSgaptRepresentation = 0
n01IndependentBaselineFrameCount
n01FeatureMask
```

指標が存在しない場合、0を代入して利用可能と扱ってはならない。対応するAvailabilityがない0は未取得値として扱う。

## 61.9 Windows Formal Source Gate

`.github/workflows/novelty01-formal-gate.yml`はWindows Server 2022で以下を実行する。

1. Python契約テスト
2. Source Preflight
3. 53 HLSL Entry PointのFXC `cs_5_0`コンパイル
4. Resource Audit
5. Release x64 Rebuild
6. Native Self-Test
7. Shader Self-Test
8. Evidence Artifact保存

保存対象は次のとおりである。

```text
novelty01_preflight.json
fxc_entrypoints.json
novelty01_resource_audit.json
novelty01_pytest.log
release_x64_build.log
native_self_test.log
```

## 61.10 Final Merge Gate

`hairgpu_novelty01_gate.py`は以下をFail Closedで判定する。

* 必須Artifactがすべて存在する
* Source Preflightが合格している
* FXC Failureが0件である
* Release x64 Build Logが存在し、エラーを含まない
* Native Self-Testが合格している
* Formal MatrixにB0、B1、B8、Pが存在する
* Proposal Run Failureが0件である
* Invalid Payload Indexが0件である
* Duplicate Payload Ownerが0件である
* NaN／Infが0件である
* Fatal Failureが0件である
* Determinism Mismatchが0件である
* B1／B8のSG-APT Representation使用が0である
* B1／B8のSG-APT Payload活動が0である
* False Static率が0.1%以下である
* Pのp95がB8より改善している
* GTX 1660を主張する場合、該当Adapter実測が存在する

未取得指標は合格値0ではなくGate Failureとする。

## 61.11 Version 69.4 完了状態

Source実装完了は次の状態で表す。

```text
Source Present
Connected
Static Contract Passed
Windows Build Pending
FXC Pending
D3D11 Runtime Pending
Formal Matrix Pending
30-Minute Soak Pending
GTX1660 Hardware Pending
```

本パッケージ作成環境ではWindows実行ができないため、`Windows Build Pending`以降を合格済みとは記載しない。

## 61.12 Version 69.4 Merge禁止条件

以下の一つでも該当する場合はMainへMergeしない。

```text
VariantExecutionContract違反
B1／B8でSG-APT Payload活動を観測
Release x64 Build未合格
53 Entry PointのFXC証拠不足
Native Self-Test未合格
Invalid Payload Index > 0
Duplicate Payload Owner > 0
NaN／Inf > 0
S17 Overflow Fallback失敗
S18 Recovery失敗
Determinism Mismatch > 0
30分Memory Leak
GTX1660実測なしで60FPS達成を主張
```

## 61.13 PerformanceSample／CSV／JSONL追跡

SG-APT Counterは`HairNoveltyFrameTelemetry`へ格納しただけでは正式証拠にならない。次の経路をすべて必須とする。

```text
GPU Counter Readback
→ HairNoveltyFrameTelemetry
→ PerformanceSample
→ Performance CSV
→ Performance JSONL
→ Matrix Summary
→ Formal Report
→ Merge Gate
```

B1およびB8については、`n01ExecutionRoute`、`n01UsesSgaptRepresentation`、`n01IndependentBaselineFrameCount`、Payload活動数をCSVおよびJSONLへ必ず出力する。Matrixはこれらが未取得の場合、0として補完せずRun Failureとする。

## 61.14 品質測定と性能測定の完全分離

Formal Suiteは次のProfileを別Process／別出力へ分離する。

```text
formal       Ground Truth有効。誤差、False Static、Collisionを評価
performance  Ground Truth無効。Frame Time、GPU Pass時間を評価
determinism  同一入力反復。State／Compaction／Position Hashを評価
soak         108,000 measured frames。30分安定性を評価
gtx1660      Adapter名とLocal VRAM条件を検証して性能を評価
```

Formal ReportはFalse Static等を`formal`から取得し、Frame Time比較を`performance`から取得する。Ground Truth追加負荷を性能結果へ混入させてはならない。

## 61.15 Formal Suite Orchestrator

`hairgpu_novelty01_formal_suite.py`および`Run_Novelty01_Formal_Matrix_And_Gate.bat`は、Source／Build／FXC検査後に全Profileを直列実行する。生成する必須Summaryは次のとおりである。

```text
novelty01_matrix_summary.json
novelty01_performance_matrix_summary.json
novelty01_determinism_summary.json
novelty01_soak_summary.json
novelty01_gtx1660_summary.json
novelty01_formal_report.json
novelty01_merge_gate.json
```

決定性、30分Soak、GTX1660のいずれかが未実施の場合、最終Merge GateはFail Closedとする。
