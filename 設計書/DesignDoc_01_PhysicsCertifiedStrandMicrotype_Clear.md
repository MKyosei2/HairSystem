# Physics-Certified Strand Microtype 設計書

## 0. まずここだけ読む

### この機能を一言で言うと
`float32` の点列をそのまま持つのではなく、**圧縮表現と物理的な安全判定を一体化した strand 表現**を追加する。

### なぜ必要か
10万本規模の髪を全点 `float32` で持つと、VRAMとGPU帯域が重くなる。単に圧縮するだけだと、壊れているstrandを検出できない。そこで、圧縮表現の中に `decode error`、`length residual`、`bend residual`、`twist residual`、`certificateBits` を持たせる。

### 実装で作るもの
| 種別 | 作るもの | 実装先 |
|---|---|---|
| CPUデータ構造 | Microtype header / payload / certificate | `include/hairgpu/StrandMicrotype.h` |
| CPU処理 | Build / Decode / Certificate計算 | `src/hairgpu_core/StrandMicrotype.cpp` |
| GPU処理 | Decode / PartialDecode / Certificate評価 | `apps/dx11_viewer/shaders/HairMicrotype.hlsl` |
| viewer接続 | GPU buffer作成、flag反映、telemetry | `GpuHairViewer.h/.cpp` |
| 評価出力 | decode error、fallback、bytes/strand | `hairgpu_final_db.py`, `hairgpu_final_excel.py` |

### 入力 → 処理 → 出力
```txt
HairAsset strand points
  -> BuildStrandMicrotype()
  -> StrandMicrotypeHeader + MicrotypePayload
  -> GPU Decode / CertificateOnlyDecode
  -> CertificateBuffer + DecodedPointBuffer
  -> CBMA / Island / Repair / Render / Log
```

### 最初に実装する順番
```txt
1. StrandMicrotype.h にstructを定義する
2. StrandMicrotype.cpp でCPU build/decode/certificateを作る
3. 単体テストでround-tripを確認する
4. GpuHairViewerにbufferとfeature flagを追加する
5. HairMicrotype.hlslでGPU decode/certificateを追加する
6. telemetryにmicrotype列を追加する
```

### 完了条件
```txt
enableMicrotype=false: 既存表示と既存ログが変わらない
enableMicrotype=true : Microtype bufferが作られ、decode error / certificate / fallback がログに出る
StaticGroom        : compression ratio と decode error を比較できる
```

### この設計書の読み方
まずこの章と「実装先ファイル早見表」を読む。細かい数式、量子化profile、fallback条件は後半の詳細仕様を参照する。

---
---

# 実装先ファイル早見表

## 新規追加するファイル

| 種別 | ファイル | 目的 |
|---|---|---|
| C++ header | `include/hairgpu/StrandMicrotype.h` | MicrotypeのCPU/GPU共通struct、certificate、encode/decode APIを定義する |
| C++ source | `src/hairgpu_core/StrandMicrotype.cpp` | CPU側のBuildMicrotype、DecodeMicrotype、certificate計算、fallback判定を実装する |
| HLSL | `apps/dx11_viewer/shaders/HairMicrotype.hlsl` | GPU decode、partial decode、certificate-only decode、edit invalidation passを実装する |
| Test | `tests/Test_StrandMicrotype.cpp` | round-trip、certificate、edit invalidation、fallbackを検証する |
| Test data 任意 | `docs/sample_assets/microtype_test_cases.json` | straight/arc/spiral/high curvatureのテスト条件を固定する |

## 変更する既存ファイル

| ファイル | 変更内容 |
|---|---|
| `src/hairgpu_core/CMakeLists.txt` | `StrandMicrotype.cpp` を `hairgpu_core` に追加する |
| `apps/dx11_viewer/CMakeLists.txt` | `HairMicrotype.hlsl` をviewer実行ディレクトリへコピー対象に追加する |
| `apps/dx11_viewer/src/GpuHairViewer.h` | Microtype用GPU buffer、feature flag、telemetry fieldsを追加する |
| `apps/dx11_viewer/src/GpuHairViewer.cpp` | shader load、buffer作成、feature flag反映、telemetry出力、fallback counter更新を追加する |
| `include/hairgpu/Types.h` | 共通ID型が不足する場合のみ `StrandId`、`SegmentId` を追加する |
| `tools/final_stage/hairgpu_final_db.py` | decode/certificate/fallback項目をDBへ取り込む |
| `tools/final_stage/hairgpu_final_excel.py` | Microtype評価列をExcel/CSV出力へ追加する |

## 原則触らないファイル

```txt
apps/hairgpu_cli/src/main.cpp
src/hairgpu_core/AssetIO.cpp
src/hairgpu_core/Metrics.cpp
```

ただし、CLIでもMicrotype buildを検証したい場合は、`apps/hairgpu_cli/src/main.cpp` に読み取り専用の検証コマンドを追加してよい。

---

# 既存コードとの接続方針

## C++ coreへの接続

`StrandMicrotype.h/cpp` は、既存の `HairAsset` を直接破壊しない。  
既存assetから別bufferとして `StrandMicrotype` と `MicrotypePayload` を生成する。

```cpp
namespace hairgpu
{
    struct StrandMicrotypeBuildInput;
    struct StrandMicrotypeBuildOutput;
    struct StrandMicrotypeHeader;
    struct StrandDecodeCertificate;

    StrandMicrotypeBuildOutput BuildStrandMicrotype(
        const HairAsset& asset,
        uint32_t strandId,
        const StrandMicrotypeBuildSettings& settings);

    bool DecodeStrandMicrotypeCpu(
        const StrandMicrotypeHeader& header,
        std::span<const std::byte> payload,
        std::vector<Vec3>& outPoints);

    StrandDecodeCertificate EvaluateStrandMicrotypeCertificate(
        std::span<const Vec3> source,
        std::span<const Vec3> decoded,
        const StrandMicrotypeBuildSettings& settings);
}
```

## DX11 viewerへの接続

`GpuHairViewer.cpp` では、既存のhair buffersとは別に以下を作る。

```txt
m_microtypeHeaderBuffer
m_microtypePayloadBuffer
m_microtypeCertificateBuffer
m_microtypeFallbackCounterBuffer
m_microtypeDecodedPointBuffer
```

`HAIRGPU_ENABLE_MICROTYPE=1` または `RuntimeSettings.enableMicrotype=true` のときだけ有効化する。  
無効時は既存のguide/follower pathを維持する。

## HLSL pass接続

`HairMicrotype.hlsl` に最低限以下のentry pointを定義する。

```txt
BuildMicrotypeDecodeCS
EvaluateMicrotypeCertificateCS
PartialMicrotypeDecodeCS
ApplyMicrotypeEditInvalidationCS
```

卒業研究実装では、CPUでMicrotype build、GPUでdecode/certificate評価を行う構成を標準とする。GPU上のbuildは必須ではない。

---

# 実装タスク

## Task 1: C++ data structureを追加する

実装先:

```txt
include/hairgpu/StrandMicrotype.h
src/hairgpu_core/StrandMicrotype.cpp
src/hairgpu_core/CMakeLists.txt
```

完了条件:

```txt
1. header/sourceがビルド対象に入っている。
2. HairAssetから全strandのMicrotypeを生成できる。
3. strandIdとpayloadOffsetが安定している。
4. 0長segment、NaN、payload不正を検出できる。
```

## Task 2: CPU encode/decode/certificateを実装する

完了条件:

```txt
1. straight/arc/spiral/high curvature strandでround-tripできる。
2. decodeError、lengthResidual、bendResidual、twistResidualが計算される。
3. threshold超過時にcertificateBitsが立つ。
4. fallback reasonが返る。
```

## Task 3: DX11 bufferとshaderを接続する

実装先:

```txt
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer.cpp
apps/dx11_viewer/shaders/HairMicrotype.hlsl
apps/dx11_viewer/CMakeLists.txt
```

完了条件:

```txt
1. Microtype bufferをGPUへアップロードできる。
2. GPU decode結果をrender/telemetry用bufferへ書ける。
3. certificate counterをGPU上で集計できる。
4. feature flag OFFで既存表示が変わらない。
```

## Task 4: telemetryを拡張する

追加する列:

```txt
microtype_enabled
microtype_bytes_per_strand
microtype_payload_bytes
microtype_decode_error_avg
microtype_length_residual_avg
microtype_bend_residual_avg
microtype_twist_residual_avg
microtype_certificate_failure_count
microtype_fallback_count
```

実装先:

```txt
apps/dx11_viewer/src/GpuHairViewer.cpp
functions:
  ExportPerformanceCsv
  ExportRuntimeReport
  frames.jsonl export path
```

## Task 5: testを追加する

```txt
Test_Microtype_RoundTrip_Static
Test_Microtype_HighCurvatureFallback
Test_Microtype_EditInvalidation
Test_Microtype_PayloadBounds
```

---

# 受け入れ条件

```txt
1. `enableMicrotype=false` では既存viewerの出力が変わらない。
2. `enableMicrotype=true` ではMicrotype bufferが作成され、certificateログが出る。
3. certificate failure strandがSkip扱いにならず、fallback counterへ反映される。
4. StaticGroomでdecode errorとcompression ratioを出せる。
5. 650パターン実験のログschemaにMicrotype項目が入る。
```

この設計書は、既存プロジェクトへ実装するために必要な構造、入出力、接続先、受け入れ条件を記述する。  
評価指標と比較対象は `Common_Evaluation_and_Baseline.md` を参照する。

---


# 0. 実装定義

この機能は「圧縮」だけではなく、**圧縮表現上で物理的安全性を観測できるstrand-native data type**として実装する。  
実装時は、`float32` 点列を単に `int8/fp16` にするのではなく、以下の不変条件を守らせる。

```txt
Invariant:
1. Microtypeはroot frame、arc-length、curvature、torsion、residual、certificateを持つ。
2. full decodeしなくても、decode error、length residual、bend residual、twist residualの上界を推定できる。
3. certificateに失敗したstrandは、partial decodeまたはFloatFallbackへ送れる。
4. edit後はeditBitsを立て、local re-encodeまたはresidual更新の対象にする。
5. MicrotypeはCBMA、PhysicsIsland、RodRepair、GPU runtimeから同じIDで参照できる。
```

## 0.1 既存技術との差分

既存の髪表現・エンジン実装では、strandをpolyline、curve、guide/interpolated hair、またはGPU bufferとして扱うことが多い。  
本機能の差分は、表現そのものにcertificateを持たせ、runtimeのmode選択やrepairの入力として使う点である。

```txt
Novelty Boundary:
既存: 髪を描画・シミュレーションしやすい点列/curve/bufferとして保持する。
本研究: 髪を物理残差とfallback可能性を持つcertificate付きmicrotypeとして保持する。
```

## 0.2 実装範囲

本設計書で求める実装は、統合runtimeにそのまま接続できる実装である。
実装は以下をすべて満たすこと。

```txt
Implementation Scope:
1. HairAsset内の全strandを、persistent strandIdを保ったままStrandMicrotypeへ変換する。
2. root frame、arc-length、curvature、torsion、residual、certificate、editBitsを必ず保持する。
3. CPU build pathとGPU decode pathの両方を実装し、GPU runtimeから直接参照できるbuffer layoutにする。
4. full decode、partial decode、render decode、repair用segment decodeを分けて実装する。
5. decode前にresidual upper boundを評価し、危険strandをCBMA、PhysicsIsland、RodRepair、FloatFallbackへ送れるようにする。
6. edit command適用後は、対象strandだけeditBitsを更新し、local re-encodeまたはFloatFallback状態へ遷移させる。
7. certificateBitsと数値residualを共通ログへ出し、評価設計書のmetricsに接続する。
8. 量子化誤差、長さ残差、曲げ残差、ねじれ残差、fallback発生数をunit/integration testで検証する。
```

実装では「圧縮できること」だけを成功条件にしない。
圧縮表現が、後続のmode allocation、island生成、rod repair、GPU resident runtimeに渡せる**証明書付きstrand表現**として動作することを成功条件にする。

---

# 目的

10万本規模の髪を、通常の `float32` 点列として保持するのではなく、髪専用の圧縮データ型として保持する。

本機能の目的は以下である。

1. strand 1本あたりのデータ量を大幅に削減する
2. GPU帯域を削減する
3. 圧縮表現上で物理的な安全性を判定できるようにする
4. 必要な場合だけ部分的にdecodeできるようにする
5. 編集・物理・修復・certificate と接続できるstrand-native表現を作る

単なる `int8` や `fp16` 量子化ではなく、髪の物理特性を持つ専用データ型を作る。

---

# 解決する問題

現行プロジェクトでは、strandの形状を点列やfloat系の状態として保持するため、以下の問題がある。

1. 10万本規模になるとVRAM使用量が大きい
2. 位置・速度・前フレーム位置・幅・法線・状態を持つと帯域が増える
3. 全点をdecodeまたは参照しないと物理的な安全性を判定しにくい
4. 圧縮した場合でも、圧縮表現と物理判定が分離している
5. 編集後の局所差分を効率的に保持しにくい

本設計では、点列をそのまま持つのではなく、以下を持つmicrotypeへ変換する。

- root frame
- arc-length
- curvature packet
- torsion packet
- residual packet
- certificate bits
- edit bits

これにより、圧縮表現そのものが物理判定に使えるようにする。

---

# 入力と出力

## 入力

```cpp
struct StrandMicrotypeBuildInput
{
    uint32_t strandId;
    StrandRange strand;
    const HairAsset* asset;

    uint32_t segmentCount;
    float lengthTolerance;
    float bendTolerance;
    float twistTolerance;
    float decodeTolerance;

    bool preserveWidths;
    bool preserveEditMask;
};
```

入力に必要な情報：

- 元の `HairAsset`
- strand範囲
- rest points
- point widths
- root binding
- rest length
- curvature/tangent情報
- 許容誤差
- segment数

---

## 出力

```cpp
struct StrandMicrotypeBuildOutput
{
    StrandMicrotype microtype;
    StrandDecodeCertificate certificate;

    float decodeError;
    float lengthResidual;
    float bendResidual;
    float twistResidual;

    bool success;
};
```

出力するもの：

- 圧縮済み `StrandMicrotype`
- certificate
- 各種residual
- 成功/失敗

---

# データ構造

## StrandMicrotype

```cpp
struct StrandMicrotype
{
    uint32_t strandId;
    uint32_t rootBindingIndex;

    PackedRootFrame rootFrame;
    PackedArcLength arcLength;
    PackedCurvature curvature;
    PackedTorsion torsion;
    PackedResidual residual;

    uint16_t certificateBits;
    uint16_t editBits;

    uint32_t payloadOffset;
    uint32_t payloadSize;
};
```

---

## PackedRootFrame

```cpp
struct PackedRootFrame
{
    uint16_t px;
    uint16_t py;
    uint16_t pz;

    uint16_t qx;
    uint16_t qy;
    uint16_t qz;
    uint16_t qw;
};
```

仕様：

- root positionは局所AABB内で16bit量子化
- root orientationはnormalized quaternionとして16bit量子化
- quaternionは復元時にnormalizeする

---

## PackedArcLength

```cpp
struct PackedArcLength
{
    uint16_t totalLength;
    uint16_t segmentCount;
    uint16_t lengthScale;
    uint16_t flags;
};
```

仕様：

- 全長を16bit量子化
- segmentCountはdecode時の再サンプリング数
- lengthScaleはgroom単位またはasset単位のscaleに対する相対値

---

## PackedCurvature

```cpp
struct PackedCurvature
{
    uint32_t offset;
    uint16_t coefficientCount;
    uint16_t quantizationScale;
};
```

payloadには曲率係数を格納する。

実装では以下の保持形式を提供する。

```cpp
std::vector<int8_t> curvatureCoefficients;
```

---

## PackedTorsion

```cpp
struct PackedTorsion
{
    uint32_t offset;
    uint16_t coefficientCount;
    uint16_t quantizationScale;
};
```

payloadにはねじれ係数を格納する。

---

## PackedResidual

```cpp
struct PackedResidual
{
    uint32_t offset;
    uint16_t residualCount;
    uint16_t quantizationScale;
};
```

payloadには、基底表現で表しきれない局所差分を格納する。

---

## StrandDecodeCertificate

```cpp
struct StrandDecodeCertificate
{
    float lengthResidual;
    float bendResidual;
    float twistResidual;
    float decodeResidual;

    float certificateValue;
    bool pass;
};
```

---

# 数式

元のstrand点列を以下とする。

\[
X_j = \{x_{j,0}, x_{j,1}, ..., x_{j,N}\}
\]

microtypeを以下とする。

\[
\mathcal{M}_j =
(r_j, F_j, L_j, \kappa_j, \tau_j, \rho_j, c_j)
\]

ここで、

- \(r_j\): root position
- \(F_j\): root frame
- \(L_j\): arc length
- \(\kappa_j\): curvature code
- \(\tau_j\): torsion code
- \(\rho_j\): residual packet
- \(c_j\): certificate bits

decodeは以下で定義する。

\[
\hat{x}_j(u)
=
r_j
+
F_j \Phi(u; L_j, \kappa_j, \tau_j)
+
D(\rho_j, u)
\]

certificateは以下で定義する。

\[
C_j =
\max
\left(
\frac{\epsilon_{\text{length}}}{\tau_{\text{length}}},
\frac{\epsilon_{\text{bend}}}{\tau_{\text{bend}}},
\frac{\epsilon_{\text{twist}}}{\tau_{\text{twist}}},
\frac{\epsilon_{\text{decode}}}{\tau_{\text{decode}}}
\right)
\]

成功条件：

\[
C_j \leq 1
\]

---

# アルゴリズム

## BuildMicrotype

```txt
function BuildMicrotype(asset, strandId):
    1. strand点列を取得する
    2. root位置とroot frameを計算する
    3. strandをarc-lengthで正規化する
    4. tangent列を計算する
    5. curvature列を計算する
    6. torsion列を計算する
    7. curvature/torsionを低次係数へ近似する
    8. 残差を計算する
    9. 各係数を量子化する
    10. microtype payloadへ格納する
    11. decodeして誤差を測る
    12. certificateを計算する
    13. certificateが通ればmicrotypeを採用する
    14. 失敗した場合はfallback modeを設定する
```

---

## DecodeMicrotype

```txt
function DecodeMicrotype(microtype, sampleCount):
    1. root frameを復元する
    2. arc-lengthを復元する
    3. curvature係数を復元する
    4. torsion係数を復元する
    5. 各uに対してcenterlineを復元する
    6. residual packetを加算する
    7. 点列を返す
```

---

## PartialDecode

```txt
function PartialDecode(microtype, u0, u1):
    1. 必要なu範囲だけ係数を読む
    2. 対応するsegmentだけ復元する
    3. collision / repair / render に渡す
```

---

# GPU/CPU処理フロー

## CPU側

```txt
1. HairAssetを読み込む
2. 各strandをBuildMicrotypeで圧縮する
3. microtype bufferを作る
4. payload bufferを作る
5. GPUへアップロードする
```

CPU側では、基本的に初期変換とデバッグ用decodeのみを行う。

---

## GPU側

```txt
1. StrandMicrotype bufferを読む
2. 必要なstrandだけpartial decodeする
3. risk / certificate / collision predicateを評価する
4. mode allocatorへ結果を渡す
5. renderに必要な点だけdecodeする
```

GPUバッファ：

```cpp
StructuredBuffer<StrandMicrotype> g_StrandMicrotypes;
StructuredBuffer<uint8_t> g_MicrotypePayload;
RWStructuredBuffer<DecodedStrandPoint> g_DecodedPoints;
RWStructuredBuffer<StrandDecodeCertificate> g_Certificates;
```

---

# fallback設計

## fallback条件

以下の場合はmicrotypeを採用しない、または一時的に高精度表現へ戻す。

```txt
1. certificateValue > 1
2. decodeResidual > threshold
3. lengthResidual > threshold
4. bendResidual > threshold
5. twistResidual > threshold
6. edit後にresidual packetで表現できない
7. collision predicate errorが高い
```

## fallback先

```txt
F0: microtypeのbit数を増やして再encode
F1: residual packetを増やす
F2: 該当strandだけfloat16点列に戻す
F3: 該当strandだけfloat32点列に戻す
F4: Physics Islandへ送る
F5: Rod Repairへ送る
```

---

# 予想される失敗例

## 1. 高曲率strandでdecode errorが増える

原因：

- 低次curvature係数で表現できない
- residual packetが不足する

対策：

- curvature coefficient countを増やす
- adaptive bit allocationを行う
- 高曲率strandだけfallbackする

---

## 2. 編集後にmicrotypeが古くなる

原因：

- edit後の形状変化がbasis外になる

対策：

- editBitsを立てる
- local re-encodeを行う
- residual packetを更新する
- certificate失敗時はfloat fallback

---

## 3. collision predicateが誤る

原因：

- 圧縮表現上の近似形状と実形状がズレる

対策：

- collision用にconservative radiusを持つ
- certificateにcollisionPredicateResidualを追加する
- 危険strandはpartial decodeする

---

## 4. decode costが高い

原因：

- 毎フレーム全strandをdecodeしている

対策：

- render LOD用decodeとphysics decodeを分ける
- partial decodeを使う
- decode結果をcacheする
- mode allocatorでdecode必要strandを制限する

---


# 実装受け入れ条件

## 必須ユニットテスト

```txt
Test_Microtype_RoundTrip_Static:
    rest points -> BuildMicrotype -> DecodeMicrotype
    decodeError <= decodeTolerance を確認する。

Test_Microtype_LengthCertificate:
    Decode後のsegment length residualが閾値以下であることを確認する。

Test_Microtype_HighCurvatureFallback:
    高曲率strandでcertificate failureが出た場合、FloatFallback対象になることを確認する。

Test_Microtype_EditInvalidation:
    editBitsが立ったstrandはlocal re-encode対象になることを確認する。
```

## Definition of Done

```txt
1. Microtype build/decode/partial decodeが動く。
2. certificateBitsがdecode residual、length、bend、twistを反映する。
3. Common_Evaluation_and_Baseline.mdのStatic Groom ScenarioでBaselineと比較できる。
4. ログにdecodeError、lengthResidual、bendResidual、twistResidual、fallbackCountが出る。
5. 圧縮率とdecode errorを表にできる。
```

## 実装指示

```txt
この設計書に従って、C++/CUDAまたはCompute Shader向けにPhysics-Certified Strand Microtypeを実装してください。
struct定義、quantization helper、BuildMicrotype、DecodeMicrotype、PartialDecode、GPU decode path、certificate計算、unit/integration testを実装として作成してください。
既存のHairAsset点列を直接破壊せず、変換後のMicrotype bufferを別に持つ設計にしてください。
```

---

# 論文での主張文

```txt
本研究では、髪を従来のfloat32点列として保持するのではなく、
root frame、arc-length、curvature、torsion、residual、certificateを持つ
Physics-Certified Strand Microtypeとして表現する。

この表現は単なる量子化ではなく、圧縮表現上でlength、bend、twist、decode residualを評価でき、
必要に応じてpartial decodeやfallbackを行える点に特徴がある。

これにより、10万本規模の髪に対して、メモリ使用量とGPU帯域を削減しながら、
物理的な安全性を保つstrand-native表現を実現する。
```

---

# 詳細実装定義

この章では、`Physics-Certified Strand Microtype` を実装するためのbuffer layout、encode/decode処理、certificate計算、編集処理、テスト条件を具体化する。

## 1. Microtypeの設計方針

Microtypeは、単なる圧縮点列ではなく、以下の3種類の情報を同時に保持する。

```txt
Geometric Core:
  root frame、rest arc-length、segment direction、curvature、torsion

Physical Residual:
  length residual、bend residual、twist residual、velocity residual、collision residual

Runtime Certificate:
  decode error bound、residual bitmask、fallback reason、edit stale flag
```

この3種類を分離せず同一strandIdから参照できるようにする。後続のCBMAやrepairは、Microtypeをdecodeしなくてもresidualとcertificateを読めることを前提にする。

## 2. Buffer Layout

### 2.1 StrandMicrotypeHeader

```cpp
struct StrandMicrotypeHeader
{
    uint32_t strandId;
    uint32_t payloadOffset;
    uint16_t pointCount;
    uint16_t segmentCount;

    uint16_t rootFrameIndex;
    uint16_t materialId;

    uint16_t quantizationProfile;
    uint16_t flags;

    float restLength;
    float rootRadius;
    float tipRadius;

    uint32_t certificateBits;
    uint32_t editBits;
};
```

必須条件：

```txt
1. payloadOffsetはMicrotypePayloadBufferのbyte offsetである。
2. pointCountは2以上とする。
3. segmentCountはpointCount - 1とする。
4. certificateBitsは前回のencode/decodeで得た状態を保持する。
5. editBitsは編集コマンドの影響範囲を示す。
```

### 2.2 MicrotypePayload

payloadは固定長にしない。strand長や点数が異なるため、headerから可変長payloadを参照する。

```txt
Payload Layout per strand:
  PackedRootFrame      rootFrame
  PackedArcLength[]    restArcLengthDelta
  PackedCurvature[]    curvature
  PackedTorsion[]      torsion
  PackedResidual       residualSummary
  OptionalEditPayload  editPayload
```

payload内の配列は16byte境界に揃える。GPU decode時のcoalesced accessを優先するため、点数が小さいstrandをまとめる場合は、別途bucket化してよい。

### 2.3 PackedRootFrame

```cpp
struct PackedRootFrame
{
    half3 rootPosition;
    uint16_t packedRotation0;
    uint16_t packedRotation1;
    uint16_t packedRotation2;
    uint16_t packedRotation3;
};
```

root rotationはquaternionまたはorthonormal frameで保持する。decode後は必ず正規化し、frameの直交性を検査する。

### 2.4 PackedResidual

```cpp
struct PackedResidual
{
    half maxDecodeError;
    half maxLengthResidual;
    half maxBendResidual;
    half maxTwistResidual;
    half maxVelocityResidual;
    half maxPenetrationDepth;
    uint16_t worstSegmentIndex;
    uint16_t residualFlags;
};
```

`worstSegmentIndex` は、repair task生成時にsegment範囲を絞るために使う。

## 3. Encode処理

### 3.1 入力正規化

encode前に以下を行う。

```txt
1. 入力点列のNaN/Infを検査する。
2. point間距離が0に近いsegmentを検出する。
3. rootからtipへ向かう順序を確認する。
4. rest lengthを計算する。
5. root frameを決める。
6. 各segmentをroot local空間へ変換する。
```

0長segmentは、そのまま量子化しない。隣接segmentへ統合するか、`CERT_LENGTH_RESIDUAL` を立ててfallback対象にする。

### 3.2 曲率・ねじれの計算

segment方向を `t_i` とする。

```txt
bendAngle_i = acos(clamp(dot(t_i, t_{i+1}), -1, 1))
curvature_i = bendAngle_i / max(segmentLength_i, epsilon)
```

ねじれは、隣接frameの相対回転から計算する。frameが不安定になる短segmentでは、ねじれを直接保存せず、前後segmentの平均から補間してよい。ただし、その場合はresidualに不確実性を足す。

### 3.3 量子化profile

sceneごとに量子化設定を変えられるようにprofileを持つ。

| Profile | 用途 | curvature bits | torsion bits | length bits | 備考 |
|---|---|---:|---:|---:|---|
| `Q_LOW` | 遠景・低リスク | 8 | 8 | 12 | 最小帯域 |
| `Q_MEDIUM` | 通常 | 12 | 10 | 16 | 標準 |
| `Q_HIGH` | 接触・編集周辺 | 16 | 12 | 16 | repair前提 |
| `Q_FLOAT` | fallback | 32 | 32 | 32 | 非圧縮 |

量子化profileはstrand単位で持つ。segment単位で変えたい場合はpayloadが複雑になるため、卒業研究実装ではstrand単位を基本とする。

## 4. Decode処理

### 4.1 RenderDecode

描画用decodeは、視覚的に必要な点列を生成する。

```txt
RenderDecode(strandId):
  1. headerを読む。
  2. certificateBitsを読む。
  3. fatal bitがあればFloatFallbackを読む。
  4. root frameを復元する。
  5. arc length、curvature、torsionからsegment frameを復元する。
  6. 点列を生成する。
  7. widthとmaterialを付けてRenderVertexBufferへ出力する。
```

### 4.2 RepairDecode

repair用decodeは、対象segment範囲だけを復元する。

```txt
RepairDecode(strandId, segmentStart, segmentEnd):
  1. segmentStartの1つ前からsegmentEndの1つ後まで読む。
  2. 境界条件として外側segmentの位置と接線を固定する。
  3. repair solver用にposition、tangent、restLengthを生成する。
```

repair用decodeでは、全strandをdecodeしない。segment範囲の前後1segmentを境界条件として含める。

### 4.3 CertificateOnlyDecode

CBMAやrisk評価では、完全な点列は不要である。

```txt
CertificateOnlyDecode(strandId):
  1. PackedResidualを読む。
  2. certificateBitsを読む。
  3. editBitsを読む。
  4. upper boundのみ返す。
```

## 5. Certificate計算

### 5.1 判定項目

```txt
Decode Certificate:
  decodeError <= decodeErrorThreshold
  lengthResidual <= lengthResidualThreshold
  bendResidual <= bendResidualThreshold
  twistResidual <= twistResidualThreshold
  penetrationDepth <= penetrationThreshold
  no NaN/Inf
```

### 5.2 上界計算

各residualは、実測値と量子化誤差の上界を足して計算する。

```txt
residualUpperBound = measuredResidual + quantizationErrorBound + editStalenessPenalty
```

`editStalenessPenalty` は、編集後にlocal re-encodeが終わっていない場合に加える。

### 5.3 certificateBits更新

```txt
if decodeErrorUpperBound > threshold:
    certificateBits |= CERT_DECODE_ERROR
if lengthResidualUpperBound > threshold:
    certificateBits |= CERT_LENGTH_RESIDUAL
if bendResidualUpperBound > threshold:
    certificateBits |= CERT_BEND_RESIDUAL
if twistResidualUpperBound > threshold:
    certificateBits |= CERT_TWIST_RESIDUAL
if penetrationDepth > threshold:
    certificateBits |= CERT_PENETRATION
```

bitを立てるだけで終わらせない。必ず後続のModeDecisionに渡す。

## 6. 編集コマンド処理

編集コマンドは以下の形式で受ける。

```cpp
struct HairEditCommand
{
    uint32_t commandType;
    uint32_t affectedStrandStart;
    uint32_t affectedStrandCount;
    float3 center;
    float radius;
    float strength;
    uint32_t frameIndex;
};
```

編集が当たったstrandでは以下を行う。

```txt
1. editBitsを立てる。
2. certificateBitsにCERT_EDIT_STALEを立てる。
3. 影響segment範囲を推定する。
4. local re-encode可能ならpayloadの一部を更新する。
5. 変形が大きい場合は一時的にQ_HIGHまたはQ_FLOATへ昇格する。
6. re-encode後にcertificateを再評価する。
```

## 7. Fallback条件

Microtypeからfallbackする条件は以下である。

```txt
Immediate FloatFallback:
  - NaN/Inf検出
  - pointCount不正
  - payloadOffset不正
  - restLengthが極端に小さい
  - decode後のroot frameが非直交

RepairまたはIslandへ送る条件:
  - bend/twist/length residualが閾値超過
  - penetrationが局所的
  - edit staleが局所範囲に限られる

PartialDecodeで継続できる条件:
  - decode errorが小さい
  - residualが一部segmentに集中している
  - root frameとarc lengthが安全
```

## 8. テストケース詳細

### 8.1 Encode/Decode一致テスト

```txt
Input:
  straight strand, arc strand, spiral strand, high curvature strand
Expected:
  decode errorがprofileごとの上限以下
  length residualが閾値以下
  certificateBitsが期待値と一致
```

### 8.2 Certificate失敗テスト

```txt
Input:
  量子化誤差が大きいstrand
  0長segmentを含むstrand
  急角度bendを含むstrand
  NaNを含むstrand
Expected:
  対応するcertificate bitが立つ
  unsafe strandがSkipされない
  fallback reasonがログに残る
```

### 8.3 編集後再評価テスト

```txt
Input:
  Local brush editを当てたstrand集合
Expected:
  affected strandだけeditBitsが立つ
  local re-encode後にCERT_EDIT_STALEが落ちる
  re-encode不能strandはFloatFallbackへ送られる
```

## 9. 実装時に禁止する簡略化

```txt
1. 全strandを毎frame full decodeしてcertificateを計算すること。
2. certificateBitsをログ専用にしてmode allocationに渡さないこと。
3. edit後に全assetを再encodeすること。
4. guide hairだけを評価してrender strandのIDを捨てること。
5. residualを単一scalarに潰してlength/bend/twistの区別を消すこと。
```