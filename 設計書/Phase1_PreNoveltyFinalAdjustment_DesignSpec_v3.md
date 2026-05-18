# Phase1 Pre-Novelty Final Adjustment Design Spec

version: v3.0  
status: phase-1 implementation-ready design document  
scope: 新規性01〜05を導入する前の最終調整  
default_assumption: 最終的には01〜05の5つすべてを実装できる状態にする  
minimum_assumption: 教授相談により最低1つの新規性だけを実装する可能性がある  
encoding: UTF-8 with BOM / CRLF  

---

## 0. この資料の位置づけ

この資料は、**フェーズ1：新規性導入前の最終調整**を定義する設計書兼企画書である。

01〜05は、それぞれ独立した新規性を持つ企画書兼設計書であり、フェーズ2〜フェーズ6で実装する対象である。

本資料は、01〜05の内容を置き換えない。  
本資料は、01〜05を実装する前に、現在のHairGPU / HairSystemプロジェクトを、新規性を安全に受け入れられる状態へ整えるための資料である。

つまり、この資料は次を扱う。

```txt
- baseline保護
- 既存実験基盤の継承
- 01〜05用feature flag
- 01〜05用telemetry placeholder
- global budgetの共通受け皿
- CSV / JSONL / summary / SQLite / Excelの列整備
- GpuHairViewer側のno-op hook
- フェーズ2〜6へ進むための完了条件
- フェーズ7で自動実験・データ収集・論文執筆へ進むための準備
```

この資料は、6つ目の新規性ではない。

---

## 1. 全体製作フロー

本研究の製作フローは、次の7フェーズで扱う。

| フェーズ | 内容 | この資料との関係 |
|---|---|---|
| フェーズ1 | 新規性導入前最終調整 | **本資料の対象** |
| フェーズ2 | 05の新規性実装 | 本資料の完了後に開始 |
| フェーズ3 | 01〜04のいずれかの新規性実装 | 本資料の完了後に開始 |
| フェーズ4 | 01〜04のいずれかの新規性実装 | 本資料の完了後に開始 |
| フェーズ5 | 01〜04のいずれかの新規性実装 | 本資料の完了後に開始 |
| フェーズ6 | 01〜04のいずれかの新規性実装 | 本資料の完了後に開始 |
| フェーズ7 | 実験・自動データ収集・論文執筆 | 実装済み新規性を評価 |

フェーズ3〜6は、必ずしも01〜04に固定で一対一対応しない。  
実装順は、教授との相談、残り時間、実装難易度、研究としての見せ方によって決める。

例：

```txt
例A:
  フェーズ2 = 05 GPU-Resident Strand Runtime
  フェーズ7 = 05だけを評価して論文執筆

例B:
  フェーズ2 = 01 Physics-Certified Strand Data Type
  フェーズ3 = 05 GPU-Resident Strand Runtime
  フェーズ7 = 01+05を評価して論文執筆

例C:
  フェーズ2〜6 = 01〜05をすべて実装
  フェーズ7 = 全組み合わせ実験と論文執筆
```

フェーズ7は、必ずフェーズ6の後にしか来ないわけではない。  
フェーズ3〜6の途中で、十分な実装対象と評価対象が揃った場合、そこからフェーズ7へ移ってよい。

---

## 2. フェーズ1の目的

フェーズ1の目的は、**新規性を入れる前に、プロジェクト側の受け皿を完成させること**である。

フェーズ1で行うことは以下。

```txt
1. 現在のBaseline_CurrentProjectが動くことを確認する。
2. 既存の実験基盤を確認し、作り直さず継承する。
3. 01〜05をすべてON/OFFできるfeature flagを用意する。
4. 01〜05をすべて記録できるtelemetry列を用意する。
5. 既存650 case基盤を壊さず、必要ならfinal5用実験manifestを追加する。
6. GpuHairViewerに01〜05のno-op hookを置く。
7. CSV / JSONL / summary / SQLite / Excelに新規性用placeholder列を出す。
8. feature flag OFF時に既存挙動が変わらないことを保証する。
9. フェーズ2〜6でどの新規性を選んでも、すぐ実装に入れる状態にする。
```

フェーズ1で行わないことは以下。

```txt
1. 01のcertificate本体を完成させる。
2. 02の物理solver / repair / islandを完成させる。
3. 03のcertificate-aware renderingを完成させる。
4. 04のper-strand editingを完成させる。
5. 05のGPU-resident runtime本体を完成させる。
```

ただし、フェーズ1では、これらを後で差し込めるhook、flag、telemetry、budget、実験列を用意する。

---

## 3. 5つすべてを前提にする理由

最終的に実装する新規性が最低1つになったとしても、フェーズ1では最大5つすべてを前提にする。

理由は以下である。

```txt
1. 途中で実装対象が増えても設計を作り直さないため。
2. 03 Rendering と 04 Editing が後付けで消えないようにするため。
3. telemetry schemaを途中で変えないため。
4. DB / Excel / 論文表を後から作り直さないため。
5. 最低1つだけ実装した場合でも、残り4つが将来拡張として自然に残るため。
```

したがって、フェーズ1では以下を必須とする。

```txt
- 01〜05すべてのfeature flagを用意する。
- 01〜05すべてのtelemetry列を用意する。
- 01〜05すべてのno-op hookを用意する。
- 01〜05すべてのON/OFF状態をsummaryに出す。
```

---

## 4. 01〜05の一覧

| 番号 | 新規性名 | フェーズ1で用意する受け皿 |
|---|---|---|
| 01 | Physics-Certified Strand Data Type | microtype / certificate flag, telemetry, hook |
| 02 | Budgeted Self-Materializing Strand Physics | risk / repair / island / fallback flag, telemetry, hook |
| 03 | Budgeted Certificate-Aware Strand Rendering | render certificate / render budget flag, telemetry, hook |
| 04 | Certificate-Preserving Per-Strand Editing | edit command / edit certificate flag, telemetry, hook |
| 05 | GPU-Resident Strand Runtime | gpu resident / indirect dispatch flag, telemetry, hook |

フェーズ1では中身を完成させない。  
フェーズ1では「差し込み口」を完成させる。

---

## 5. 現プロジェクトに既にある実験基盤の扱い

現在のプロジェクトには、Non-Novelty Infrastructureの一部以上が既に入っている前提で扱う。

以下は、存在確認してから継承・拡張する。

| 項目 | 想定ファイル | フェーズ1での扱い |
|---|---|---|
| 650 case generator | `tools/final_stage/generate_experiment_650.py` | 既存があれば作り直さない |
| 650 suite | `results/exp_650/runtime_suite_650.json` | 既存実験として維持 |
| final run wrapper | `tools/final_stage/hairgpu_final_run.py` | env追加のみ |
| runtime suite runner | `tools/final_stage/hairgpu_runtime_suite.py` | resume/filter/fail-policyを維持 |
| DB schema | `tools/final_stage/schema.sql` | 列削除禁止。末尾追加のみ |
| DB importer | `tools/final_stage/hairgpu_final_db.py` | 旧CSVでも落ちないようにする |
| Excel exporter | `tools/final_stage/hairgpu_final_excel.py` | 既存sheetを維持 |
| viewer config | `apps/dx11_viewer/src/GpuHairViewer.h` | 既存structを拡張 |
| viewer telemetry | `apps/dx11_viewer/src/GpuHairViewer.cpp` | 既存出力を拡張 |
| frame outputs | `frames.csv`, `frames.jsonl`, `summary.json` | schema互換を維持 |

実装者は以下をしてはいけない。

```txt
1. 既存の650 case generatorを上書きする。
2. 既存runtime_suite_650.jsonをfinal5用に破壊する。
3. 既存CSV列を削除する。
4. 既存DB列を削除する。
5. baseline pathを消す。
```

---

## 6. 旧flagと正式flagのズレ

現在の実験基盤は、以下のような旧5軸を前提にしている可能性がある。

```txt
enableMicrotype
enableCBMA
enablePhysicsIslands
enableRodRepair
enableGpuResident
```

しかし、本研究の最終的な5つの新規性は以下である。

```txt
01: enableMicrotype
02: enableBudgetedPhysics
03: enableCertificateRender
04: enablePerStrandEdit
05: enableGpuResident
```

つまり、旧実験軸では02が内部機能に分解され、03と04が明確な軸になっていない。

フェーズ1ではこれを以下の方針で解決する。

```txt
1. 旧flagは削除しない。
2. 正式な01〜05用flagを追加する。
3. 旧flagと正式flagの対応表を作る。
4. 既存exp_650はlegacy実験として残す。
5. 必要ならfinal5用の新しいsuiteを追加する。
```

---

## 7. 正式feature flag

フェーズ1で、以下の正式flagを定義する。

```cpp
struct HairNoveltyFeatureFlags
{
    uint32_t enableMicrotype = 0;          // 01
    uint32_t enableBudgetedPhysics = 0;    // 02
    uint32_t enableCertificateRender = 0;  // 03
    uint32_t enablePerStrandEdit = 0;      // 04
    uint32_t enableGpuResident = 0;        // 05

    // legacy / 02 internal compatibility
    uint32_t enableCBMA = 0;
    uint32_t enablePhysicsIslands = 0;
    uint32_t enableRodRepair = 0;

    uint32_t enableNoNoveltyBaseline = 0;
};
```

旧flagと正式flagの関係は以下。

| 旧flag | 正式flag上の意味 |
|---|---|
| `enableMicrotype` | 01 |
| `enableCBMA` | 02のmode allocation |
| `enablePhysicsIslands` | 02のisland |
| `enableRodRepair` | 02のrepair |
| `enableGpuResident` | 05 |

03と04は新規に正式flagを追加する。

```txt
HAIRGPU_ENABLE_CERTIFICATE_RENDER
HAIRGPU_ENABLE_PER_STRAND_EDIT
```

---

## 8. フェーズ1で追加する共通ファイル

フェーズ1では、以下の共通ファイルを追加する。

```txt
include/hairgpu/HairNoveltyFlags.h
include/hairgpu/HairNoveltyTelemetry.h
include/hairgpu/GlobalHairBudget.h

src/hairgpu_core/HairNoveltyFlags.cpp
src/hairgpu_core/HairNoveltyTelemetry.cpp
src/hairgpu_core/GlobalHairBudget.cpp
```

これらは新規性本体ではない。

| ファイル | 役割 |
|---|---|
| `HairNoveltyFlags.h/.cpp` | 01〜05のflagと旧flag互換 |
| `HairNoveltyTelemetry.h/.cpp` | 01〜05の共通telemetry placeholder |
| `GlobalHairBudget.h/.cpp` | 01〜05同時ON時の共通budget |

---

## 9. `HairNoveltyFlags.h` 仕様

作成ファイル：

```txt
include/hairgpu/HairNoveltyFlags.h
```

内容：

```cpp
#pragma once
#include <cstdint>

namespace hairgpu
{
    struct HairNoveltyFeatureFlags
    {
        uint32_t enableMicrotype = 0;
        uint32_t enableBudgetedPhysics = 0;
        uint32_t enableCertificateRender = 0;
        uint32_t enablePerStrandEdit = 0;
        uint32_t enableGpuResident = 0;

        uint32_t enableCBMA = 0;
        uint32_t enablePhysicsIslands = 0;
        uint32_t enableRodRepair = 0;

        uint32_t enableNoNoveltyBaseline = 0;
    };

    HairNoveltyFeatureFlags NormalizeNoveltyFlags(
        uint32_t enableMicrotype,
        uint32_t enableBudgetedPhysics,
        uint32_t enableCertificateRender,
        uint32_t enablePerStrandEdit,
        uint32_t enableGpuResident,
        uint32_t enableCBMA,
        uint32_t enablePhysicsIslands,
        uint32_t enableRodRepair,
        uint32_t enableNoNoveltyBaseline);

    uint32_t BuildTopLevelNoveltyMask(const HairNoveltyFeatureFlags& flags);
}
```

作成ファイル：

```txt
src/hairgpu_core/HairNoveltyFlags.cpp
```

内容：

```cpp
#include "hairgpu/HairNoveltyFlags.h"

namespace hairgpu
{
    HairNoveltyFeatureFlags NormalizeNoveltyFlags(
        uint32_t enableMicrotype,
        uint32_t enableBudgetedPhysics,
        uint32_t enableCertificateRender,
        uint32_t enablePerStrandEdit,
        uint32_t enableGpuResident,
        uint32_t enableCBMA,
        uint32_t enablePhysicsIslands,
        uint32_t enableRodRepair,
        uint32_t enableNoNoveltyBaseline)
    {
        HairNoveltyFeatureFlags f = {};
        f.enableMicrotype = enableMicrotype;
        f.enableBudgetedPhysics = enableBudgetedPhysics;
        f.enableCertificateRender = enableCertificateRender;
        f.enablePerStrandEdit = enablePerStrandEdit;
        f.enableGpuResident = enableGpuResident;
        f.enableCBMA = enableCBMA;
        f.enablePhysicsIslands = enablePhysicsIslands;
        f.enableRodRepair = enableRodRepair;
        f.enableNoNoveltyBaseline = enableNoNoveltyBaseline;

        if (f.enableCBMA || f.enablePhysicsIslands || f.enableRodRepair)
            f.enableBudgetedPhysics = 1u;

        if (f.enableBudgetedPhysics)
        {
            if (!f.enableCBMA) f.enableCBMA = 1u;
            if (!f.enablePhysicsIslands) f.enablePhysicsIslands = 1u;
            if (!f.enableRodRepair) f.enableRodRepair = 1u;
        }

        if (f.enableNoNoveltyBaseline)
        {
            f = {};
            f.enableNoNoveltyBaseline = 1u;
        }

        return f;
    }

    uint32_t BuildTopLevelNoveltyMask(const HairNoveltyFeatureFlags& f)
    {
        uint32_t mask = 0u;
        if (f.enableMicrotype) mask |= 1u << 0;
        if (f.enableBudgetedPhysics) mask |= 1u << 1;
        if (f.enableCertificateRender) mask |= 1u << 2;
        if (f.enablePerStrandEdit) mask |= 1u << 3;
        if (f.enableGpuResident) mask |= 1u << 4;
        return mask;
    }
}
```

---

## 10. `HairNoveltyTelemetry.h` 仕様

作成ファイル：

```txt
include/hairgpu/HairNoveltyTelemetry.h
```

内容：

```cpp
#pragma once
#include <cstdint>

namespace hairgpu
{
    struct HairNoveltyFrameTelemetry
    {
        uint32_t frameIndex = 0;
        uint32_t measuredFrameIndex = 0;
        uint32_t topLevelNoveltyMask = 0;

        uint32_t cpuReadbackCount = 0;
        uint32_t overflowCount = 0;
        uint32_t budgetDropCount = 0;
        uint32_t fallbackCount = 0;
        uint32_t deferCount = 0;

        uint32_t microtypeDecodedCount = 0;
        uint32_t safeDecodeCount = 0;
        uint32_t certificateFailureCount = 0;
        uint32_t falseSafeCount = 0;
        uint32_t typedRepairRequestCount = 0;
        uint32_t floatFallbackRequestCount = 0;
        uint32_t rejectPayloadCount = 0;

        uint32_t riskyStrandCount = 0;
        uint32_t repairTaskCount = 0;
        uint32_t repairCommitCount = 0;
        uint32_t repairRejectCount = 0;
        uint32_t repairDeferCount = 0;
        uint32_t islandCandidateCount = 0;
        uint32_t islandCount = 0;
        uint32_t islandMemberCount = 0;
        uint32_t islandCommitCount = 0;
        uint32_t islandRejectCount = 0;
        uint32_t physicsFallbackCount = 0;
        float maxPenetrationMm = 0.0f;

        uint32_t renderCertificateCount = 0;
        uint32_t renderSkipCount = 0;
        uint32_t renderCoarseOpacityCount = 0;
        uint32_t renderStrandRasterCount = 0;
        uint32_t renderHighQualityShadowCount = 0;
        uint32_t renderHighQualitySpecularCount = 0;
        uint32_t renderFullQualityCount = 0;
        uint32_t renderDroppedFullQualityCount = 0;
        float p95ShadowErrorBound = 0.0f;
        float p95SpecularErrorBound = 0.0f;
        float temporalFlickerScore = 0.0f;

        uint32_t editOperationCount = 0;
        uint32_t editRepairTaskCount = 0;
        uint32_t editIslandEscalationCount = 0;
        uint32_t editCommitCount = 0;
        uint32_t editRejectCount = 0;
        uint32_t editPreviewFullQualityCount = 0;
        float editLatencyMs = 0.0f;

        uint32_t gpuResidentTaskCount = 0;
        uint32_t riskyCompactionCount = 0;
        uint32_t dispatchIndirectCount = 0;
        uint32_t drawIndirectCount = 0;
        uint32_t delayedTelemetryReadCount = 0;
    };

    inline HairNoveltyFrameTelemetry MakeZeroNoveltyTelemetry()
    {
        return HairNoveltyFrameTelemetry{};
    }
}
```

作成ファイル：

```txt
src/hairgpu_core/HairNoveltyTelemetry.cpp
```

内容：

```cpp
#include "hairgpu/HairNoveltyTelemetry.h"
```

この `.cpp` は現時点では空実装でよい。  
ビルド単位を安定させるために作成する。

---

## 11. `GlobalHairBudget.h` 仕様

作成ファイル：

```txt
include/hairgpu/GlobalHairBudget.h
```

内容：

```cpp
#pragma once
#include <cstdint>
#include "hairgpu/HairNoveltyFlags.h"

namespace hairgpu
{
    struct GlobalHairFrameBudget
    {
        float targetFrameMs = 16.6f;
        float totalHairBudgetMs = 8.0f;

        float dataBudgetMs = 1.0f;
        float physicsBudgetMs = 3.0f;
        float renderBudgetMs = 4.0f;
        float editBudgetMs = 1.0f;
        float runtimeBudgetMs = 0.8f;

        uint32_t maxDecodedStrands = 65536;
        uint32_t maxRiskyStrands = 16384;
        uint32_t maxRepairTasks = 8192;
        uint32_t maxIslandEdges = 32768;
        uint32_t maxIslandMembers = 16384;

        uint32_t maxRenderTasks = 65536;
        uint32_t maxFullQualityRenderStrands = 2048;
        uint32_t maxHighQualityShadowStrands = 4096;
        uint32_t maxHighQualitySpecularStrands = 4096;

        uint32_t maxEditOperations = 512;
        uint32_t maxEditRepairTasks = 2048;
        uint32_t maxEditPreviewFullQuality = 1024;

        uint32_t cpuReadbackAllowed = 1;
    };

    GlobalHairFrameBudget BuildGlobalHairFrameBudget(
        const HairNoveltyFeatureFlags& flags);
}
```

作成ファイル：

```txt
src/hairgpu_core/GlobalHairBudget.cpp
```

内容：

```cpp
#include "hairgpu/GlobalHairBudget.h"
#include <algorithm>

namespace hairgpu
{
    GlobalHairFrameBudget BuildGlobalHairFrameBudget(
        const HairNoveltyFeatureFlags& flags)
    {
        GlobalHairFrameBudget b = {};

        if (!flags.enableMicrotype)
        {
            b.dataBudgetMs = 0.0f;
            b.maxDecodedStrands = 0;
        }

        if (!flags.enableBudgetedPhysics)
        {
            b.physicsBudgetMs = 0.0f;
            b.maxRiskyStrands = 0;
            b.maxRepairTasks = 0;
            b.maxIslandEdges = 0;
            b.maxIslandMembers = 0;
        }

        if (!flags.enableCertificateRender)
        {
            b.renderBudgetMs = 0.0f;
            b.maxRenderTasks = 0;
            b.maxFullQualityRenderStrands = 0;
            b.maxHighQualityShadowStrands = 0;
            b.maxHighQualitySpecularStrands = 0;
        }

        if (!flags.enablePerStrandEdit)
        {
            b.editBudgetMs = 0.0f;
            b.maxEditOperations = 0;
            b.maxEditRepairTasks = 0;
            b.maxEditPreviewFullQuality = 0;
        }

        if (flags.enableGpuResident)
            b.cpuReadbackAllowed = 0;

        if (flags.enableMicrotype &&
            flags.enableBudgetedPhysics &&
            flags.enableCertificateRender &&
            flags.enablePerStrandEdit &&
            flags.enableGpuResident)
        {
            b.physicsBudgetMs = std::min(b.physicsBudgetMs, 2.5f);
            b.renderBudgetMs = std::min(b.renderBudgetMs, 3.5f);
            b.editBudgetMs = std::min(b.editBudgetMs, 0.8f);

            b.maxRiskyStrands = std::min<uint32_t>(b.maxRiskyStrands, 8192);
            b.maxRepairTasks = std::min<uint32_t>(b.maxRepairTasks, 4096);
            b.maxIslandEdges = std::min<uint32_t>(b.maxIslandEdges, 16384);
            b.maxIslandMembers = std::min<uint32_t>(b.maxIslandMembers, 8192);
            b.maxFullQualityRenderStrands =
                std::min<uint32_t>(b.maxFullQualityRenderStrands, 1024);
            b.maxEditPreviewFullQuality =
                std::min<uint32_t>(b.maxEditPreviewFullQuality, 512);
        }

        return b;
    }
}
```

---

## 12. GpuHairViewerへのフェーズ1追加

### 12.1 `GpuHairViewer.h`

既存includeに追加する。

```cpp
#include "hairgpu/HairNoveltyFlags.h"
#include "hairgpu/HairNoveltyTelemetry.h"
#include "hairgpu/GlobalHairBudget.h"
```

`GpuHairViewer` classのprivate memberへ追加する。

```cpp
hairgpu::HairNoveltyFeatureFlags m_noveltyFlags = {};
hairgpu::GlobalHairFrameBudget m_globalHairBudget = {};
hairgpu::HairNoveltyFrameTelemetry m_noveltyTelemetry = {};
```

`PerformanceSample`に、`HairNoveltyFrameTelemetry`と同等の列を追加する。  
既にある列は重複させず、不足分だけ追加する。

### 12.2 `GpuHairViewer.cpp`

既存の環境変数読み込み関数に、以下を追加する。

```cpp
const uint32_t enableCertificateRender =
    ReadEnvBool(L"HAIRGPU_ENABLE_CERTIFICATE_RENDER") ? 1u : 0u;

const uint32_t enablePerStrandEdit =
    ReadEnvBool(L"HAIRGPU_ENABLE_PER_STRAND_EDIT") ? 1u : 0u;

m_noveltyFlags = hairgpu::NormalizeNoveltyFlags(
    m_experimentCase.enableMicrotype ? 1u : 0u,
    0u,
    enableCertificateRender,
    enablePerStrandEdit,
    m_experimentCase.enableGpuResident ? 1u : 0u,
    m_experimentCase.enableCBMA ? 1u : 0u,
    m_experimentCase.enablePhysicsIslands ? 1u : 0u,
    m_experimentCase.enableRodRepair ? 1u : 0u,
    0u);

m_globalHairBudget = hairgpu::BuildGlobalHairFrameBudget(m_noveltyFlags);
```

frame loopの冒頭に追加する。

```cpp
m_noveltyTelemetry = hairgpu::MakeZeroNoveltyTelemetry();
m_noveltyTelemetry.frameIndex = m_globalFrameIndex;
m_noveltyTelemetry.measuredFrameIndex = m_measuredFrameIndex;
m_noveltyTelemetry.topLevelNoveltyMask =
    hairgpu::BuildTopLevelNoveltyMask(m_noveltyFlags);
```

`PerformanceSample sample`作成箇所で、`m_noveltyTelemetry`をsampleへコピーする。

---

## 13. GpuHairViewerに置くno-op hook

フェーズ1では、以下のhookを置く。

```cpp
void GpuHairViewer::EvaluateMicrotypeCertificates()
{
    if (!m_noveltyFlags.enableMicrotype)
        return;
}

void GpuHairViewer::ExecuteBudgetedPhysics()
{
    if (!m_noveltyFlags.enableBudgetedPhysics)
        return;
}

void GpuHairViewer::ExecuteCertificateAwareRendering()
{
    if (!m_noveltyFlags.enableCertificateRender)
        return;
}

void GpuHairViewer::ExecutePerStrandEditing()
{
    if (!m_noveltyFlags.enablePerStrandEdit)
        return;
}

void GpuHairViewer::ExecuteGpuResidentRuntime()
{
    if (!m_noveltyFlags.enableGpuResident)
        return;
}
```

これらはフェーズ1ではno-opでよい。  
フェーズ2〜6で、対象新規性の本体をここに差し込む。

RuntimeFrame上の推奨順序：

```txt
1. ApplyEditCommands / ExecutePerStrandEditing
2. EvaluateMicrotypeCertificates
3. ExecuteBudgetedPhysics
4. ExecuteGpuResidentRuntime
5. ExecuteCertificateAwareRendering
6. Draw
7. WriteTelemetry
```

05を完全実装した後は、02/03/04の一部taskは05側へ移してよい。  
フェーズ1ではhookの存在を優先する。

---

## 14. CSV / JSONL / summaryへの追加

フェーズ1では、01〜05が未実装でも列だけは出す。

### 14.1 CSV

`ExportPerformanceCsv()` で、既存列を削除せず、末尾に以下を追加する。

```txt
top_level_novelty_mask,
cpu_readback_count,
overflow_count,
budget_drop_count,
fallback_count,
defer_count,

microtype_decoded_count,
safe_decode_count,
certificate_failure_count,
false_safe_count,
typed_repair_request_count,
float_fallback_request_count,
reject_payload_count,

risky_strand_count,
repair_task_count,
repair_commit_count,
repair_reject_count,
repair_defer_count,
island_candidate_count,
island_count,
island_member_count,
island_commit_count,
island_reject_count,
physics_fallback_count,
max_penetration_mm,

render_certificate_count,
render_skip_count,
render_coarse_opacity_count,
render_strand_raster_count,
render_high_quality_shadow_count,
render_high_quality_specular_count,
render_full_quality_count,
render_dropped_full_quality_count,
p95_shadow_error_bound,
p95_specular_error_bound,
temporal_flicker_score,

edit_operation_count,
edit_repair_task_count,
edit_island_escalation_count,
edit_commit_count,
edit_reject_count,
edit_preview_full_quality_count,
edit_latency_ms,

gpu_resident_task_count,
risky_compaction_count,
dispatch_indirect_count,
draw_indirect_count,
delayed_telemetry_read_count
```

### 14.2 JSONL

`ExportPerformanceJsonl()` がある場合、同じ列を各frame objectに出す。

### 14.3 summary.json

`summary.json`に以下を追加する。

```json
{
  "phase": "phase1_pre_novelty_final_adjustment",
  "novelty_mapping_version": "final5_v1",
  "top_level_novelty_mask": 0,
  "novelty_flags": {
    "enableMicrotype": false,
    "enableBudgetedPhysics": false,
    "enableCertificateRender": false,
    "enablePerStrandEdit": false,
    "enableGpuResident": false,
    "enableCBMA": false,
    "enablePhysicsIslands": false,
    "enableRodRepair": false
  }
}
```

---

## 15. DB / Excelへの追加

### 15.1 `schema.sql`

既存tableの列を削除しない。

`runtime_runs` に追加する列：

```sql
top_level_novelty_mask INTEGER,
enable_budgeted_physics INTEGER,
enable_certificate_render INTEGER,
enable_per_strand_edit INTEGER,
phase TEXT,
novelty_mapping_version TEXT
```

`runtime_frame_metrics` に、CSVで追加した01〜05列を追加する。

### 15.2 `hairgpu_final_db.py`

以下を守る。

```txt
1. 旧CSVに新列がなくても落ちない。
2. 新CSVに列があれば取り込む。
3. 欠損値は0またはnullにする。
4. summary.jsonからnovelty_flagsを読む。
```

### 15.3 `hairgpu_final_excel.py`

フェーズ1では、最低限以下のsheetを出す。

```txt
Baseline_Check
FeatureFlag_Check
TelemetrySchema_Check
Final5_Readiness
```

フェーズ1では、性能比較の優劣を出す必要はない。  
フェーズ2〜6へ進めるかを確認する表でよい。

---

## 16. final5用manifest

既存の `exp_650` は壊さない。  
フェーズ1では、必要に応じてfinal5用manifestを別に作る。

作成候補：

```txt
tools/final_stage/generate_experiment_final5.py
results/exp_final5/experiments_final5.jsonl
results/exp_final5/runtime_suite_final5.json
```

final5の5軸：

```txt
01 enableMicrotype
02 enableBudgetedPhysics
03 enableCertificateRender
04 enablePerStrandEdit
05 enableGpuResident
```

bit表記：

```txt
Combo_10000 = 01 only
Combo_01000 = 02 only
Combo_00100 = 03 only
Combo_00010 = 04 only
Combo_00001 = 05 only
Combo_11111 = all five
```

フェーズ1では、`Combo_00000` と `Combo_11111` がno-opで起動できればよい。  
新規性本体の結果は、フェーズ2〜6で埋める。

---

## 17. フェーズ1の作業順序

実装者は以下の順で作業する。

```txt
Task 1:
  baseline起動確認

Task 2:
  telemetry出力確認

Task 3:
  既存exp_650確認

Task 4:
  HairNoveltyFlags追加

Task 5:
  HairNoveltyTelemetry追加

Task 6:
  GlobalHairBudget追加

Task 7:
  GpuHairViewerへflag / budget / telemetry member追加

Task 8:
  環境変数から03/04 flagを読む

Task 9:
  GpuHairViewerへ01〜05のno-op hookを置く

Task 10:
  CSV / JSONL / summaryに01〜05 placeholder列を出す

Task 11:
  DB / Excel import/exportをplaceholder列に対応させる

Task 12:
  final5 manifestを追加する

Task 13:
  Combo_00000とCombo_11111をno-op実行する

Task 14:
  フェーズ2〜6へ進めるか確認する
```

---

## 18. フェーズ1の完了条件

フェーズ1は、以下を満たしたら完了とする。

```txt
[ ] Baseline_CurrentProjectが起動する
[ ] feature flagを全OFFにしたとき、既存挙動が変わらない
[ ] 01〜05の正式flagが存在する
[ ] 旧flagとの互換がある
[ ] 03と04のflagが存在する
[ ] 01〜05のno-op hookがGpuHairViewerにある
[ ] 01〜05のtelemetry列がCSVに出る
[ ] 01〜05のtelemetry列がJSONLに出る
[ ] 01〜05の状態がsummary.jsonに出る
[ ] DB importが旧CSVと新CSVの両方に対応する
[ ] Excelでフェーズ1 readinessを確認できる
[ ] 既存exp_650が壊れていない
[ ] final5のCombo_00000とCombo_11111がno-opで実行できる
[ ] フェーズ2〜6でどの新規性を選んでも、最初に触るhookが分かる
```

---

## 19. フェーズ2〜6へ渡すもの

フェーズ1の成果物は以下である。

```txt
1. feature flag共通定義
2. telemetry共通定義
3. global budget共通定義
4. GpuHairViewer内のno-op hook
5. CSV / JSONL / summary / DB / Excelのplaceholder列
6. exp_650 legacy互換
7. exp_final5の実験入口
8. baseline保護確認結果
```

フェーズ2〜6の実装者は、対象新規性の設計書を読み、以下のhookへ実装を差し込む。

| 新規性 | フェーズ1が用意するhook |
|---|---|
| 01 | `EvaluateMicrotypeCertificates` |
| 02 | `ExecuteBudgetedPhysics` |
| 03 | `ExecuteCertificateAwareRendering` |
| 04 | `ExecutePerStrandEditing` |
| 05 | `ExecuteGpuResidentRuntime` |

---

## 20. フェーズ7との関係

フェーズ7では、実験・データ収集・論文執筆を行う。

フェーズ7は、次のどの状態からでも入れる。

```txt
1. 01だけ実装済み
2. 05だけ実装済み
3. 01+05実装済み
4. 01+02+05実装済み
5. 01〜05全て実装済み
```

フェーズ1でtelemetryと実験基盤を整えておくことで、フェーズ7では実装済みmoduleだけを使って自動データ収集できる。

フェーズ7で必要な出力は以下。

```txt
1. baseline vs implemented module
2. feature flag ON/OFF比較
3. p50 / p95 / p99 frame time
4. cpu_readback_count
5. overflow / fallback / defer
6. module固有のtelemetry
7. 論文用CSV / Excel / table
```

---

## 21. 最初のPR単位

フェーズ1の最初のPRは以下にする。

```txt
PR-Phase1-00:
  Add final5 novelty flags, telemetry placeholders, and global budget scaffold
```

含めるファイル：

```txt
include/hairgpu/HairNoveltyFlags.h
include/hairgpu/HairNoveltyTelemetry.h
include/hairgpu/GlobalHairBudget.h
src/hairgpu_core/HairNoveltyFlags.cpp
src/hairgpu_core/HairNoveltyTelemetry.cpp
src/hairgpu_core/GlobalHairBudget.cpp
src/hairgpu_core/CMakeLists.txt
apps/dx11_viewer/src/GpuHairViewer.h
apps/dx11_viewer/src/GpuHairViewer.cpp
```

このPRでやらないこと：

```txt
1. 01の本体実装
2. 02の本体実装
3. 03の本体実装
4. 04の本体実装
5. 05の本体実装
```

このPRは、01〜05を入れる前の器だけを作る。

---

## 22. フェーズ1でやってはいけないこと

```txt
1. 01〜05のうち1つだけに最適化した土台を作る。
2. 03と04を後回しにしてflagやtelemetryを用意しない。
3. 既存のexp_650をfinal5用に破壊する。
4. 旧flagを削除する。
5. 既存CSV列を削除する。
6. 既存DB列を削除する。
7. feature flag OFF時の挙動を変える。
8. baselineを比較不能にする。
9. 未実装moduleを成功扱いにする。
10. フェーズ1で新規性本体まで完成させようとして範囲を膨らませる。
```

---

## 23. 最終的な資料構成

最終提出では、この資料を以下の位置に置く。

```txt
Phase1_PreNoveltyFinalAdjustment_DesignSpec_v3.md
01_PhysicsCertifiedStrandDataType.md
02_BudgetedSelfMaterializingStrandPhysics.md
03_BudgetedCertificateAwareStrandRendering.md
04_CertificatePreservingPerStrandEditing.md
05_GpuResidentStrandRuntime.md
```

この資料は、01〜05の前に読む。

本文での説明例：

```txt
本研究では、毛髪runtimeに対して5つの新規性を設計した。
フェーズ1では、それらの新規性を導入する前に、既存HairGPUプロジェクトのbaseline、feature flag、telemetry、実験基盤、DB/Excel出力を整備した。
これにより、フェーズ2〜6で01〜05のいずれか、または全てを段階的に実装し、フェーズ7で自動実験・データ収集・論文執筆へ進める構成とした。
```

---

## 24. 結論

本資料は、フェーズ1専用の設計書兼実装仕様書である。

フェーズ1では、新規性本体を実装しない。  
代わりに、01〜05を最大5つすべて実装できるように、現在のプロジェクトを整える。

この資料が完了すれば、次の状態になる。

```txt
- baselineが保護されている
- 01〜05のflagが揃っている
- 01〜05のtelemetry列が揃っている
- 既存実験基盤が壊れていない
- final5実験入口がある
- 03と04も最初から評価軸に入っている
- 最低1つだけ実装しても論文評価に進める
- 最大5つ全て実装しても同じ土台で評価できる
```

この状態をもって、フェーズ2〜6の新規性実装へ進む。
