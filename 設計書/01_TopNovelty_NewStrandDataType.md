# 01. 新しいデータ型系：Physics-Certified Strand Data Type 完全版

---
version: Top Novelty Implementation Design v1.0
status: implementation-ready design
language: C++17 + HLSL compute shader
primary_runtime: GPU hair runtime / DX11-compatible compute path
novelty_category: new_data_type
not_included: paper strategy, publication framing
---

# 1. 実装新規性の一文

髪1本を単なる `float3 points[32]` や curve ではなく、**decode前に物理安全性・修復範囲・fallback先・編集stale状態を判定できる certificate-carrying strand primitive** として実装する。

# 2. これは何が新しいのか

普通の髪データは、まず点列やcurveをdecodeし、その後で物理チェック、衝突チェック、修復、fallbackを行う。

この実装では順序を逆にする。

```txt
従来:
  encoded strand / point array
    -> full decode
    -> length / bend / twist / penetration check
    -> repair / fallback decision

提案:
  StrandMicrotypeHeader + certificate summary
    -> header-only safety query
    -> safe decode / typed repair / island escalation / fallback
```

つまり、髪1本のデータ型そのものが、処理経路を決める。

# 3. 弱い実装と強い実装

## 弱い実装

```txt
髪用の新しいstructを作る。
点列を量子化して保存する。
metadataを少し持たせる。
```

これは単なるフォーマット実装であり、新規性は弱い。

## トップ会議級の実装新規性

```txt
髪1本の型が、次をdecode前に答えられる。

1. このstrandは安全にdecodeしてよいか
2. decodeすると物理破綻する可能性があるか
3. どのsegmentが最悪か
4. どの範囲だけrepairすればよいか
5. どのrepair operatorを呼ぶべきか
6. island solverへ昇格すべきか
7. float fallbackへ落とすべきか
8. 編集によりcertificateがstaleになっているか
```

# 4. 最終成果物

この研究の成果物は、以下を含む1つのデータ型システムである。

```txt
PhysicsCertifiedStrand
  - encoded geometry
  - quantization profile
  - certificate bits
  - physical residual bounds
  - worst segment information
  - repair band
  - fallback semantics
  - edit stale semantics
  - GPU header-only query
  - CPU full verifier
  - false-safe audit
```

# 5. ディレクトリ構成

```txt
include/strand/data/PhysicsCertifiedStrand.h
include/strand/data/StrandCertificateBits.h
include/strand/data/StrandQuantizationProfile.h
include/strand/data/StrandCertificatePolicy.h
include/strand/data/StrandDecodeResult.h
include/strand/data/StrandVerifier.h

src/strand_data/PhysicsCertifiedStrandBuilder.cpp
src/strand_data/PhysicsCertifiedStrandDecode.cpp
src/strand_data/PhysicsCertifiedStrandVerifier.cpp
src/strand_data/StrandCertificateQuery.cpp
src/strand_data/StrandFalseSafeAudit.cpp
src/strand_data/StrandResidualBounds.cpp

shaders/StrandDataCommon.hlsli
shaders/QueryStrandCertificateCS.hlsl
shaders/SafeDecodeStrandCS.hlsl
shaders/AuditFalseSafeCS.hlsl

tests/TestPhysicsCertifiedStrand.cpp
tests/TestStrandCertificateQuery.cpp
tests/TestStrandFalseSafeAudit.cpp
tests/TestStrandResidualBounds.cpp

tools/analyze_strand_certificate_quality.py
```

# 6. C++ 定数

```cpp
#pragma once
#include <cstdint>

namespace strand::data {
static constexpr uint32_t kMaxPointsPerStrand = 32;
static constexpr uint32_t kMaxSegmentsPerStrand = kMaxPointsPerStrand - 1;
static constexpr uint32_t kQuantizationProfileCount = 8;
static constexpr uint32_t kInvalidU32 = 0xffffffffu;
static constexpr float kEpsilon = 1.0e-6f;
}
```

# 7. Certificate bits

```cpp
namespace strand::data {

enum StrandCertificateBits : uint32_t
{
    CERT_NONE                 = 0,

    // fatal
    CERT_NUMERIC_NAN_INF      = 1u << 0,
    CERT_PAYLOAD_INVALID      = 1u << 1,
    CERT_ROOT_FRAME_INVALID   = 1u << 2,
    CERT_PROFILE_INVALID      = 1u << 3,
    CERT_OVERFLOW             = 1u << 4,

    // repairable physical failures
    CERT_DECODE_ERROR         = 1u << 5,
    CERT_LENGTH_RESIDUAL      = 1u << 6,
    CERT_BEND_RESIDUAL        = 1u << 7,
    CERT_TWIST_RESIDUAL       = 1u << 8,
    CERT_PENETRATION          = 1u << 9,

    // runtime/edit state
    CERT_EDIT_STALE           = 1u << 10,
    CERT_CONTACT_RISK         = 1u << 11,
    CERT_MOTION_DIVERGENCE    = 1u << 12,
    CERT_RENDER_IMPORTANT     = 1u << 13,

    // uncertainty
    CERT_BOUND_INCONCLUSIVE   = 1u << 14,
};

static constexpr uint32_t CERT_FATAL_MASK =
    CERT_NUMERIC_NAN_INF |
    CERT_PAYLOAD_INVALID |
    CERT_ROOT_FRAME_INVALID |
    CERT_PROFILE_INVALID |
    CERT_OVERFLOW;

static constexpr uint32_t CERT_REPAIRABLE_MASK =
    CERT_DECODE_ERROR |
    CERT_LENGTH_RESIDUAL |
    CERT_BEND_RESIDUAL |
    CERT_TWIST_RESIDUAL |
    CERT_PENETRATION |
    CERT_EDIT_STALE;

}
```

# 8. 基本データ構造

```cpp
namespace strand::data {

struct QuantizedPointI16
{
    int16_t x;
    int16_t y;
    int16_t z;
    int16_t pad;
};

struct QuantizationProfile
{
    uint16_t profileId;
    uint16_t flags;
    float rootScaleMm;
    float tipScaleMm;
    float maxDecodeErrorMm;
    float maxLengthResidualRatio;
    float maxBendResidualRad;
    float maxTwistResidualRad;
};

struct PhysicsCertifiedStrandHeader
{
    uint32_t strandId;
    uint16_t pointCount;
    uint16_t segmentCount;

    float rootPosition[3];
    float rootFrame[3][3];

    uint16_t quantizationProfileId;
    uint16_t flags;

    float rootRadius;
    float tipRadius;
    float restLength;

    uint32_t certificateBits;

    uint16_t worstSegmentIndex;
    uint16_t repairSegmentStart;
    uint16_t repairSegmentEnd;
    uint16_t fallbackMode;

    float decodeResidualBoundMm;
    float lengthResidualBoundRatio;
    float bendResidualBoundRad;
    float twistResidualBoundRad;
    float penetrationResidualBoundMm;
    float editDeltaBoundMm;

    float certificateValue;
};

struct PhysicsCertifiedStrand
{
    PhysicsCertifiedStrandHeader header;
    QuantizedPointI16 qPoints[kMaxPointsPerStrand];
};

struct SourceStrand32
{
    uint32_t strandId;
    uint32_t pointCount;
    float p[kMaxPointsPerStrand][3];
    float frame[kMaxPointsPerStrand][3][3];
    float rootRadius;
    float tipRadius;
};

struct DecodedStrand32
{
    uint32_t strandId;
    uint32_t pointCount;
    float p[kMaxPointsPerStrand][3];
    float frame[kMaxPointsPerStrand][3][3];
};

struct HeaderOnlyQueryResult
{
    uint32_t strandId;
    uint32_t certificateBits;
    uint32_t decision;
    uint16_t worstSegmentIndex;
    uint16_t repairSegmentStart;
    uint16_t repairSegmentEnd;
    uint16_t fallbackMode;
    float certificateValue;
    float risk;
};

}
```

# 9. Decision enum

```cpp
namespace strand::data {

enum StrandHeaderDecision : uint32_t
{
    DECISION_SAFE_DECODE = 0,
    DECISION_TYPED_REPAIR = 1,
    DECISION_ISLAND_ESCALATION = 2,
    DECISION_FLOAT_FALLBACK = 3,
    DECISION_REJECT_PAYLOAD = 4,
};

}
```

# 10. Build settings

```cpp
namespace strand::data {

struct PhysicsCertifiedStrandBuildSettings
{
    QuantizationProfile profiles[kQuantizationProfileCount];
    uint32_t profileCount = 0;

    float maxDecodeErrorMm = 0.25f;
    float maxLengthResidualRatio = 0.02f;
    float maxBendResidualRad = 0.65f;
    float maxTwistResidualRad = 0.75f;
    float penetrationToleranceMm = 0.05f;
    float editStaleToleranceMm = 0.10f;

    uint16_t minRepairRadiusSegments = 1;
    uint16_t maxRepairRadiusSegments = 6;

    float islandEscalationCertificateValue = 3.0f;
    float floatFallbackCertificateValue = 10.0f;
};

}
```

# 11. C++ API

```cpp
namespace strand::data {

bool ValidateSourceStrand32(const SourceStrand32& src);

float ComputeRestLength(const SourceStrand32& src);

uint16_t SelectQuantizationProfile(
    const SourceStrand32& src,
    const PhysicsCertifiedStrandBuildSettings& settings);

PhysicsCertifiedStrand BuildPhysicsCertifiedStrand32(
    const SourceStrand32& src,
    const PhysicsCertifiedStrandBuildSettings& settings,
    const SignedDistanceFieldView* sdf);

DecodedStrand32 DecodePhysicsCertifiedStrand32(
    const PhysicsCertifiedStrand& strand,
    const PhysicsCertifiedStrandBuildSettings& settings);

HeaderOnlyQueryResult QueryHeaderOnlyCpu(
    const PhysicsCertifiedStrandHeader& header,
    const PhysicsCertifiedStrandBuildSettings& settings);

bool VerifyNoFalseSafeCpu(
    const PhysicsCertifiedStrand& encoded,
    const SourceStrand32& source,
    const SignedDistanceFieldView* sdf,
    const PhysicsCertifiedStrandBuildSettings& settings);

}
```

# 12. Residual計算

```cpp
namespace strand::data {

float ComputeDecodeResidualMm(
    const SourceStrand32& source,
    const DecodedStrand32& decoded);

float ComputeLengthResidualRatio(
    const SourceStrand32& source,
    const DecodedStrand32& decoded,
    uint16_t* worstSegmentOut);

float ComputeBendResidualRad(
    const SourceStrand32& source,
    const DecodedStrand32& decoded,
    uint16_t* worstSegmentOut);

float ComputeTwistResidualRad(
    const SourceStrand32& source,
    const DecodedStrand32& decoded,
    uint16_t* worstSegmentOut);

float ComputePenetrationResidualMm(
    const DecodedStrand32& decoded,
    const SignedDistanceFieldView& sdf,
    uint16_t* worstSegmentOut);

void ComputeAdaptiveRepairBand(
    uint16_t worstSegment,
    uint16_t segmentCount,
    float certificateValue,
    const PhysicsCertifiedStrandBuildSettings& settings,
    uint16_t* repairStartOut,
    uint16_t* repairEndOut);

}
```

# 13. Header-only decision rule

```cpp
namespace strand::data {

inline uint32_t DecideFromHeader(
    const PhysicsCertifiedStrandHeader& h,
    const PhysicsCertifiedStrandBuildSettings& s)
{
    if ((h.certificateBits & CERT_FATAL_MASK) != 0)
        return DECISION_REJECT_PAYLOAD;

    if (h.certificateValue >= s.floatFallbackCertificateValue)
        return DECISION_FLOAT_FALLBACK;

    if (h.certificateValue >= s.islandEscalationCertificateValue)
        return DECISION_ISLAND_ESCALATION;

    if ((h.certificateBits & CERT_REPAIRABLE_MASK) != 0)
        return DECISION_TYPED_REPAIR;

    return DECISION_SAFE_DECODE;
}

}
```

# 14. HLSL common

```hlsl
#define CERT_NONE               0u
#define CERT_NUMERIC_NAN_INF    (1u << 0)
#define CERT_PAYLOAD_INVALID    (1u << 1)
#define CERT_ROOT_FRAME_INVALID (1u << 2)
#define CERT_PROFILE_INVALID    (1u << 3)
#define CERT_OVERFLOW           (1u << 4)
#define CERT_DECODE_ERROR       (1u << 5)
#define CERT_LENGTH_RESIDUAL    (1u << 6)
#define CERT_BEND_RESIDUAL      (1u << 7)
#define CERT_TWIST_RESIDUAL     (1u << 8)
#define CERT_PENETRATION        (1u << 9)
#define CERT_EDIT_STALE         (1u << 10)

#define DECISION_SAFE_DECODE      0u
#define DECISION_TYPED_REPAIR     1u
#define DECISION_ISLAND_ESCALATE  2u
#define DECISION_FLOAT_FALLBACK   3u
#define DECISION_REJECT_PAYLOAD   4u

struct PhysicsCertifiedStrandHeaderGpu
{
    uint strandId;
    uint pointSegmentPacked;
    float3 rootPosition;
    float3 rootFrame0;
    float3 rootFrame1;
    float3 rootFrame2;
    uint profileFlagsPacked;
    float rootRadius;
    float tipRadius;
    float restLength;
    uint certificateBits;
    uint segmentPacked;
    float decodeResidualBoundMm;
    float lengthResidualBoundRatio;
    float bendResidualBoundRad;
    float twistResidualBoundRad;
    float penetrationResidualBoundMm;
    float editDeltaBoundMm;
    float certificateValue;
};

struct HeaderOnlyQueryResultGpu
{
    uint strandId;
    uint certificateBits;
    uint decision;
    uint segmentPacked;
    float certificateValue;
    float risk;
};
```

# 15. GPU certificate query shader

```hlsl
cbuffer StrandDataSettingsCB : register(b0)
{
    uint g_StrandCount;
    float g_MaxDecodeErrorMm;
    float g_MaxLengthResidualRatio;
    float g_MaxBendResidualRad;
    float g_MaxTwistResidualRad;
    float g_PenetrationToleranceMm;
    float g_EditStaleToleranceMm;
    float g_IslandEscalationValue;
    float g_FloatFallbackValue;
};

StructuredBuffer<PhysicsCertifiedStrandHeaderGpu> g_Headers : register(t0);
RWStructuredBuffer<HeaderOnlyQueryResultGpu> g_QueryResults : register(u0);

[numthreads(128, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint strandId = tid.x;
    if (strandId >= g_StrandCount) return;

    PhysicsCertifiedStrandHeaderGpu h = g_Headers[strandId];

    uint bits = h.certificateBits;
    if (h.decodeResidualBoundMm > g_MaxDecodeErrorMm) bits |= CERT_DECODE_ERROR;
    if (h.lengthResidualBoundRatio > g_MaxLengthResidualRatio) bits |= CERT_LENGTH_RESIDUAL;
    if (h.bendResidualBoundRad > g_MaxBendResidualRad) bits |= CERT_BEND_RESIDUAL;
    if (h.twistResidualBoundRad > g_MaxTwistResidualRad) bits |= CERT_TWIST_RESIDUAL;
    if (h.penetrationResidualBoundMm > g_PenetrationToleranceMm) bits |= CERT_PENETRATION;
    if (h.editDeltaBoundMm > g_EditStaleToleranceMm) bits |= CERT_EDIT_STALE;

    float risk = h.decodeResidualBoundMm
        + 10.0f * h.lengthResidualBoundRatio
        + h.bendResidualBoundRad
        + h.twistResidualBoundRad
        + 0.1f * h.penetrationResidualBoundMm
        + h.editDeltaBoundMm;

    uint decision = DECISION_SAFE_DECODE;
    uint fatal = CERT_NUMERIC_NAN_INF | CERT_PAYLOAD_INVALID | CERT_ROOT_FRAME_INVALID | CERT_PROFILE_INVALID | CERT_OVERFLOW;
    uint repairable = CERT_DECODE_ERROR | CERT_LENGTH_RESIDUAL | CERT_BEND_RESIDUAL | CERT_TWIST_RESIDUAL | CERT_PENETRATION | CERT_EDIT_STALE;

    if ((bits & fatal) != 0) decision = DECISION_REJECT_PAYLOAD;
    else if (risk >= g_FloatFallbackValue) decision = DECISION_FLOAT_FALLBACK;
    else if (risk >= g_IslandEscalationValue) decision = DECISION_ISLAND_ESCALATE;
    else if ((bits & repairable) != 0) decision = DECISION_TYPED_REPAIR;

    HeaderOnlyQueryResultGpu outResult;
    outResult.strandId = h.strandId;
    outResult.certificateBits = bits;
    outResult.decision = decision;
    outResult.segmentPacked = h.segmentPacked;
    outResult.certificateValue = risk;
    outResult.risk = risk;

    g_QueryResults[strandId] = outResult;
}
```

# 16. False-safe audit

false-safeとは、header-only queryでは safe と判定したが、full decode後に破綻していたケースである。

これは絶対に隠さない。

```cpp
struct FalseSafeAuditRecord
{
    uint32_t strandId;
    uint32_t headerDecision;
    uint32_t headerCertificateBits;
    uint32_t fullCertificateBits;
    float headerCertificateValue;
    float fullCertificateValue;
    float decodeResidualMm;
    float lengthResidualRatio;
    float bendResidualRad;
    float twistResidualRad;
    float penetrationResidualMm;
};
```

成功条件は以下。

```txt
false_safe_count == 0 を目標にする。
ただし、実装中は false_safe_count を必ずtelemetryに出す。
false-safeが出た場合は safe decode ではなく typed repair または fallback に寄せる。
```

# 17. Telemetry

```txt
case_id, frame, strand_count,
header_query_ms,
safe_decode_count,
typed_repair_count,
island_escalation_count,
float_fallback_count,
reject_payload_count,
false_safe_count,
false_safe_rate,
mean_certificate_value,
p95_certificate_value,
mean_decode_residual_mm,
max_decode_residual_mm,
mean_length_residual,
max_length_residual,
mean_bend_residual_rad,
max_bend_residual_rad,
mean_twist_residual_rad,
max_twist_residual_rad,
max_penetration_mm,
bandwidth_mb_frame
```

# 18. テスト

## Unit tests

```txt
TestSourceStrandValidation
TestQuantizationProfileSelection
TestEncodeDecodeRoundTrip
TestResidualBoundComputation
TestHeaderOnlyQuerySafe
TestHeaderOnlyQueryRepair
TestHeaderOnlyQueryIsland
TestHeaderOnlyQueryFallback
TestFalseSafeAudit
```

## GPU tests

```txt
TestGpuHeaderQueryMatchesCpu
TestGpuSafeDecodeMatchesCpu
TestGpuFalseSafeAuditMatchesCpu
```

# 19. 完了条件

この研究は、次を満たしたら完成とする。

```txt
1. 32点strandをPhysicsCertifiedStrandへ変換できる。
2. full decode前にheader-only queryで処理経路を決定できる。
3. safe / repair / island / fallback / reject の5分岐が動く。
4. GPU queryとCPU queryのdecisionが一致する。
5. false-safe auditが実装されている。
6. false_safe_countをtelemetryに出す。
7. 04 repairと03 islandへ渡すためのworstSegment / repairRange / certificateBitsが出る。
8. 05 GPU runtimeへ接続可能なquery result bufferを出力する。
```

# 20. この研究の核

この研究の核は、圧縮でも単なる型定義でもない。

```txt
髪1本のデータ型が、decode前に処理経路を決める。
```

この一点を外さなければ、新しいデータ型系としてトップ会議級の実装新規性になる。
