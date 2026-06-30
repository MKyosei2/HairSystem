# 01. 新しいデータ型系

# Physics-Certified Segment-Group Hair Data Structure

## 処理経路を内包した髪データ構造の企画書兼設計書 最新版

---

## 1. 概要

本企画では、髪を単なる点列、curve、または物理計算用の粒子列として扱うのではなく、各strand内部を `Segment Group` 単位に分解し、それぞれのgroupが実行時の処理経路を持つ髪データ構造を設計・実装する。

ここでいう処理経路とは、各Segment Groupが以下のどの状態で扱われるべきかを指す。

```txt
- Static cacheを再利用する
- rootやstrand全体の移動だけを反映する
- Dynamic solveで再計算する
- Deferred Correctionで一時補正する
- Repairで局所修復する
- Fallbackとして通常のfloat点列処理へ戻す
- 異常値としてRejectする
```

既存の髪シミュレーションでは、髪を点列・segment・rod・guide/follower構造として表現し、毎フレーム物理計算や描画処理を行うことが多い。しかし、実際のリアルタイム制作・編集・描画では、すべての髪の全segmentが毎フレーム同じ重要度で動くわけではない。

本設計では、髪strand全体ではなく、strand内部のSegment Group単位で「処理が必要な部分」と「安全に再利用できる部分」を判定する。これにより、動いていないgroupはStatic cacheとして再利用し、編集・衝突・外力・境界破綻の可能性があるgroupのみをDynamic solve、Repair、Fallbackへ送る。

本研究の中心は、単なる新しい形状表現ではない。
既存のsegment-based hair representationを基盤としながら、各Segment Groupに以下を持たせる点にある。

```txt
- 実行時状態
- 物理安全性の判定情報
- cache再利用可能性
- 編集・衝突・外力によるstale状態
- Repair / Fallbackへの移行条件
- false-static検出用telemetry
```

つまり、本研究では「髪データ型が、描画や物理計算の前に、自身の処理経路を決定できる」ことを目指す。

---

## 2. 本研究の主張

本研究では、髪strandを構成する点列をSegment Group単位に分解し、各groupが物理状態だけでなく、Static再利用、Dynamic再計算、Deferred Correction、Repair、Fallbackといった実行時の処理経路を持つ髪データ構造を提案する。

既存の髪シミュレーションでは、strand、guide、cluster単位で計算量を削減する手法が多い。これに対し、本研究ではstrand内部のgroup単位で処理状態を切り替える。

さらに、strand-level certificateとgroup-level certificateを組み合わせることで、髪全体の大まかな動的必要性と、各groupの再計算必要性を段階的に判定する。

これにより、動いていない部分はStatic cacheとして再利用し、編集・衝突・外力・境界破綻の可能性がある部分のみをDynamic solveまたはRepairへ送る。また、誤ってStatic化されたgroupをfalse-staticとして計測することで、単なる高速化ではなく、安全な再利用が行えているかを評価する。

---

## 3. 実装新規性の一文

本研究では、既存のsegment-based hair representationを基盤としつつ、strand内部のSegment Groupごとにcertificate、cache validity、runtime stateを持たせ、Static再利用、Dynamic再計算、Deferred Correction、Repair、Fallbackをdecode前または軽量query段階で選択する、処理経路選択型の髪データ構造を実装する。

---

## 4. 研究・実装の核

本研究の核は、以下である。

```txt
髪1本のデータ型が、decode前または軽量query段階で処理経路を決める。

さらに、髪1本全体ではなく、
点-線-点のSegment Group単位で、
Static再利用するか、Dynamic再計算するか、
一時補正するか、Repair / Fallbackへ送るかを判断する。
```

本設計では、髪の形状を保存するだけでなく、髪データそのものに「どの処理を実行すべきか」というruntime decisionを持たせる。

このデータ構造は、以下を判定できることを目指す。

```txt
1. このstrand全体は動く可能性があるか
2. このstrand内のどのgroupだけ再計算すべきか
3. どのgroupはStatic cacheを使えるか
4. どのgroupはStatic transformedで済むか
5. どのgroupは一時補正で対応できるか
6. どのgroupはRepairが必要か
7. どのgroupはFallbackが必要か
8. どのgroupは衝突リスクを持つか
9. どのgroupは編集によってstaleになっているか
10. どのgroupはfalse-staticの危険があるか
```

---

## 5. 既存研究との差分

本研究では、以下の既存技術を前提として扱う。

```txt
- 髪を点列・segment・rodとして扱う表現
- guide/follower型の髪表現
- PBD / XPBD による制約ベース物理
- 髪LOD、cluster、active setによる計算削減
- cache reuse、sleeping、fallback
- GPU multi-pass simulation
```

これらは既に存在する技術であり、本研究の新規性として主張しない。

本研究の差分は、以下に置く。

```txt
1. strand全体ではなく、strand内部のSegment Group単位で処理状態を切り替える
2. 各groupにcertificate、cache validity、runtime stateを持たせる
3. Static / Dynamic / Deferred / Repair / Fallbackを同一データ構造内で管理する
4. false-staticを明示的に計測し、Static化の危険性を評価する
5. Static / Dynamic混在境界の誤差をtelemetryとして評価する
6. CPU queryとGPU queryのdecision一致を検証する
```

したがって、本研究は「髪をsegmentで表す研究」ではなく、「髪strand内部のsegment groupに処理経路選択機能を持たせるruntime hair data structure」の研究である。

---

## 6. Segment Group の定義

### 6.1 基本単位

本設計では、髪1本の中にある `点 - 線 - 点` を1つの `Segment Group` とする。

```txt
1 group = P_i - Segment_i - P_i+1
```

髪1本が以下の点列で構成されている場合、

```txt
P0 - P1 - P2 - P3 - P4 - ...
```

Segment Groupは以下のように定義される。

```txt
Group 0 : P0 - P1
Group 1 : P1 - P2
Group 2 : P2 - P3
Group 3 : P3 - P4
...
```

初期実装では、基本的に `1 group = 1 segment` とする。

ただし、将来的な拡張として、複数segmentを1groupとして扱えるように、内部表現はrange形式で保持する。

---

## 7. group長とresampling

各Segment Groupの長さは、同一strand内では等間隔として扱う。

ただし、髪の毛ごとに全体長は異なってよい。
そのため、group長はstrand単位で決定する。

```txt
strand A:
  groupLength = 2.0mm

strand B:
  groupLength = 3.5mm

strand C:
  groupLength = 1.2mm
```

入力データの点間距離がばらつく場合は、事前処理として等間隔resamplingを行う。

```txt
入力点列
  -> strand単位で全長を計算
  -> 必要group数を決定
  -> 等間隔resampling
  -> Segment Groupを生成
```

これにより、Static cache、Dynamic判定、Repair範囲、GPU処理を単純化する。

---

## 8. Joint Hemisphere Split

隣接するgroupでは、終点と始点が共有される。

```txt
Group 0 : P0 - P1
Group 1 : P1 - P2
```

このとき、`P1` はGroup 0の終点であり、Group 1の始点でもある。

本設計では、この共有点を完全に二重計算するのではなく、rest姿勢を基準に影響範囲を分ける。これを `Joint Hemisphere Split` と呼ぶ。

```txt
共有点P_i:

上半分:
  rootからtip方向へ向かう側の影響範囲

下半分:
  root側、または1つ前のgroup側の影響範囲
```

半球分割の基準は、毎フレームの変形状態ではなく、重力を無視して上方向へ伸ばしたrest姿勢とする。

この分割は、group単位の並列処理や、Static / Dynamic混在時の境界補正に使用する。

---

## 9. Segment Group State

各Segment Groupは、以下の状態を持つ。

```cpp
enum SegmentGroupState
{
    STATIC_CACHED,
    STATIC_TRANSFORMED,
    DYNAMIC_SOLVE,
    DEFERRED_CORRECTION,
    REPAIR_REQUIRED,
    FALLBACK_FLOAT,
    REJECTED
};
```

各状態の意味は以下である。

```txt
STATIC_CACHED:
  前回の計算結果を方向ベクトルとして再利用する。

STATIC_TRANSFORMED:
  髪形状は再利用し、rootやstrand全体の移動だけを反映する。

DYNAMIC_SOLVE:
  通常の物理計算、constraint解決、collision checkを行う。

DEFERRED_CORRECTION:
  本計算の代わりに一時補正を行い、次フレーム以降に本計算へ送る。

REPAIR_REQUIRED:
  groupまたは周辺groupのみを局所修復する。

FALLBACK_FLOAT:
  certificate付き軽量処理では安全に扱えないため、通常のfloat点列処理へ戻す。

REJECTED:
  payload破損、NaN/Inf、profile invalidなどにより処理不能と判断する。
```

---

## 10. STATIC_CACHED

`STATIC_CACHED` は、完全に前回結果を再利用する状態である。

この状態では、groupの物理計算を再実行しない。

基本式は以下である。

```txt
end = start + cachedDirection * groupLength
```

ここで、`start` は1つ前のgroupの確定済みend positionを使う。
`cachedDirection` は前回計算時に保存された方向ベクトルである。
`groupLength` はresamplingによって決定されたstrand内共通のgroup長である。

Static groupでは、すべての点のworld座標を保持するのではなく、基本的に以下を保持する。

```txt
- root position
- cachedDirection
- tangent
- dependencyVersion
```

フルの座標を持つのは基本的にrootのみとし、それ以外の点はrootまたは前groupの確定点から、方向ベクトルとgroup長によって復元する。

---

## 11. STATIC_TRANSFORMED

`STATIC_TRANSFORMED` は、形状は前回結果を使うが、rootやstrand全体の移動分だけを反映する状態である。

これは、プレイヤー移動や頭部移動などで髪全体が移動したが、髪形状そのものを再計算する必要が低い場合に使用する。

```txt
root移動
  -> root差分を取得
  -> 既存cachedDirectionを維持
  -> rootからtip方向へ位置を復元
  -> 軽量collision check
  -> 問題がなければ描画
```

`STATIC_TRANSFORMED` は完全な物理結果ではない。
処理予算が不足している場合、または形状変化が小さい場合の安全な近似状態として扱う。

---

## 12. DYNAMIC_SOLVE

`DYNAMIC_SOLVE` は、物理計算が必要な状態である。

以下の場合にDynamicになる。

```txt
- player / camera / root transformが変化した
- scalp / bone animationが変化した
- 風などの外力を受けている
- 編集操作中である
- colliderが更新された
- 隣接groupが大きく変化した
- certificateがstaleになった
- 衝突リスクがある
- Static補正だけでは見た目が破綻する
```

Dynamic groupでは、通常の物理計算、constraint解決、collision check、cache更新を行う。

---

## 13. DEFERRED_CORRECTION

`DEFERRED_CORRECTION` は、本計算をすぐに行わず、一時的な補正で見た目の破綻を防ぐ状態である。

例えば、以下のような状態を考える。

```txt
Group 0 : Static
Group 1 : Dynamic
Group 2 : Static
```

このとき、Group 1が動いた影響でGroup 2の始点が変化する可能性がある。
しかし、Group 2を毎回完全に再計算すると重くなる。

そこで、Group 2は一時的に以下の式で補正する。

```txt
correctedStart = previousGroup.end
correctedEnd   = correctedStart + cachedDirection * groupLength
```

この補正は、本計算の代わりではなく、処理が間に合わない場合や、1から再計算する前の緊急用・補助用として使う。

```txt
Deferred Correction:
  1. 隣接Dynamic groupの結果を受け取る
  2. startを更新する
  3. cachedDirection * groupLengthでendを仮更新する
  4. 見た目の破綻を防ぐ
  5. 後続フレームで余裕があればDYNAMIC_SOLVEまたはREPAIR_REQUIREDへ送る
```

---

## 14. REPAIR_REQUIRED

`REPAIR_REQUIRED` は、完全な再計算ではなく、groupまたは周辺groupのみを修復する状態である。

対象例は以下である。

```txt
- 長さ制約が少し破綻した
- bend residualが閾値を超えた
- 軽いpenetrationが発生した
- Static / Dynamic境界が不自然になった
- Deferred Correctionが複数フレーム続いた
```

StaticとDynamicが混在する場合、Dynamic groupの前後groupをrepair候補に入れる。

```txt
Dynamic group = i

repair candidate:
  group i - 1
  group i
  group i + 1
```

Repairは、全strandをFallbackする前の安全弁である。

---

## 15. FALLBACK_FLOAT

`FALLBACK_FLOAT` は、certificate付きの軽量処理では安全に扱えない場合に、通常のfloat点列処理へ戻す状態である。

以下の場合にFallbackへ送る。

```txt
- certificateValueが閾値を超えた
- residualが大きすぎる
- repairで復元できない
- root / payload / profileに深刻な問題がある
- false-staticが繰り返し発生した
```

Fallbackは失敗ではなく、安全性を優先するための明示的な処理経路である。

---

## 16. REJECTED

`REJECTED` は、payload破損やNaN/Infなどにより、処理不能な状態である。

```txt
- NaN / Inf
- payload invalid
- root frame invalid
- quantization profile invalid
- overflow
```

この状態は、Script overrideよりも優先される。

---

## 17. strand-level certificate と group-level certificate

本設計では、certificateをstrand単位とgroup単位のハイブリッド構造にする。

ただし、ここでいうcertificateは、数学的に完全な証明ではない。
本研究におけるcertificateは、長さ誤差、曲げ残差、衝突リスク、編集stale、cache validityなどをまとめたruntime validity descriptorである。

### 17.1 strand-level certificate

strand-level certificateは、髪1本または髪のエリア単位で、大まかな処理方針を決める。

役割は以下である。

```txt
- このstrand全体が動く可能性があるか
- rootが動いたか
- player / camera motionの影響を受けたか
- animationの影響を受けたか
- 外力の影響を受けたか
- 編集対象に含まれるか
- group-level queryを実行すべきか
- strand全体をStatic候補にできるか
- strand全体をDynamic候補にすべきか
```

### 17.2 group-level certificate

group-level certificateは、strand-levelの結果を受けて、各groupの細かい処理状態を決める。

役割は以下である。

```txt
- このgroupはStatic cacheを使えるか
- このgroupはStatic transformedで済むか
- このgroupはDeferred Correctionで一時対応できるか
- このgroupはDynamic solveが必要か
- このgroupはRepairが必要か
- このgroupはFallbackが必要か
- このgroupは編集によりstaleか
- このgroupは衝突リスクを持つか
```

---

## 18. certificateValue

group-level decisionでは、certificate bitsだけでなく、連続値としての `certificateValue` を使用する。

基本式は以下とする。

```txt
certificateValue =
    w_length   * lengthResidualRatio
  + w_bend     * bendResidualRad
  + w_twist    * twistResidualRad
  + w_pen      * penetrationResidualMm
  + w_motion   * motionDeltaMm
  + w_edit     * editDeltaMm
  + w_cache    * cacheStaleValue
  + w_boundary * staticDynamicBoundaryErrorMm
```

各項目の意味は以下である。

```txt
lengthResidualRatio:
  group長制約の破綻度

bendResidualRad:
  曲げ制約の残差

twistResidualRad:
  ねじれ制約の残差

penetrationResidualMm:
  colliderや他の髪とのpenetration量

motionDeltaMm:
  前フレームとの差分

editDeltaMm:
  編集操作による変化量

cacheStaleValue:
  cacheの古さ、依存version不一致、watchdog検出結果

staticDynamicBoundaryErrorMm:
  Static / Dynamic境界の不連続誤差
```

`certificateValue` が小さいgroupはStatic reuse候補となる。
一定以上の場合はDynamic solve、Repair、Fallbackへ送る。

---

## 19. certificate bits

本設計で使用するcertificate bitsは以下である。

```cpp
enum SegmentGroupCertificateBits : uint32_t
{
    CERT_NONE                 = 0,

    // fatal
    CERT_NUMERIC_NAN_INF      = 1u << 0,
    CERT_PAYLOAD_INVALID      = 1u << 1,
    CERT_ROOT_FRAME_INVALID   = 1u << 2,
    CERT_PROFILE_INVALID      = 1u << 3,
    CERT_OVERFLOW             = 1u << 4,

    // physical / repairable
    CERT_DECODE_ERROR         = 1u << 5,
    CERT_LENGTH_RESIDUAL      = 1u << 6,
    CERT_BEND_RESIDUAL        = 1u << 7,
    CERT_TWIST_RESIDUAL       = 1u << 8,
    CERT_PENETRATION          = 1u << 9,

    // runtime state
    CERT_EDIT_STALE           = 1u << 10,
    CERT_CONTACT_RISK         = 1u << 11,
    CERT_MOTION_DIVERGENCE    = 1u << 12,
    CERT_RENDER_IMPORTANT     = 1u << 13,
    CERT_STATIC_CACHE_VALID   = 1u << 14,
    CERT_STATIC_CACHE_STALE   = 1u << 15,
    CERT_DEFERRED_CORRECTION  = 1u << 16,
    CERT_WATCHDOG_REQUIRED    = 1u << 17,

    // uncertainty
    CERT_BOUND_INCONCLUSIVE   = 1u << 18
};
```

---

## 20. strand判定とgroup判定の優先関係

基本的には、strand-levelの方が上位判定である。

```txt
strand-level:
  大まかな処理方針を決める

group-level:
  strand-levelの結果を受けて細かく調整する
```

ただし、group-levelは最終的なStatic / Dynamic / Repair / Fallbackの決定に関与する。

つまり、strand-levelが大まかな処理範囲を決め、group-levelがその中で細部を調整する。

例：

```txt
strand-level:
  このstrandは基本Static候補

group-level:
  group 0 = Static
  group 1 = Dynamic
  group 2 = Deferred Correction
  group 3 = Static
```

ただし、fatal、collision danger、edit operationなど安全性に関わる条件は、通常のstrand-level判定を上書きできる。

---

## 21. Script overrideの優先順位

Script overrideは、strandまたはgroup単位でStatic / Dynamicを強制できる仕組みである。

ただし、overrideが安全性を壊さないよう、優先順位を明確に定義する。

```txt
fatal / reject
> collision danger
> edit operation
> force dynamic
> force static
> normal certificate decision
```

他の優先順位と矛盾する場合も、この順序を最優先とする。

つまり、`force static` が指定されていても、衝突危険、編集操作、fatalな異常がある場合はStaticにしない。

---

## 22. 基本データ構造

### 22.1 SegmentGroupRange

```cpp
struct SegmentGroupRange
{
    uint16_t pointStart;
    uint16_t pointEnd;
    uint16_t segmentStart;
    uint16_t segmentEnd;
};
```

初期実装では、基本的に以下となる。

```txt
pointStart   = i
pointEnd     = i + 1
segmentStart = i
segmentEnd   = i
```

---

### 22.2 SegmentGroupCache

```cpp
struct SegmentGroupCache
{
    float cachedDirection[3];
    float cachedTangent[3];

    uint32_t dependencyVersion;
    uint32_t lastUpdatedFrame;

    uint32_t cachedState;
    float certificateValue;
};
```

基本方針として、全点のworld座標は持たない。
root以外は、`cachedDirection * groupLength` により復元する。

---

### 22.3 SegmentGroupCertificate

```cpp
struct SegmentGroupCertificate
{
    uint32_t strandId;
    uint16_t groupId;

    uint16_t pointStart;
    uint16_t pointEnd;

    uint32_t state;
    uint32_t certificateBits;

    float groupLength;

    float motionDeltaMm;
    float editDeltaMm;
    float lengthResidualRatio;
    float bendResidualRad;
    float twistResidualRad;
    float penetrationResidualMm;
    float staticDynamicBoundaryErrorMm;

    float certificateValue;

    uint32_t cachedFrameIndex;
    uint32_t dependencyVersion;

    uint32_t overrideState;
};
```

---

### 22.4 StrandCertificate

```cpp
struct StrandCertificate
{
    uint32_t strandId;

    uint32_t strandCertificateBits;
    uint32_t strandDecision;

    uint16_t groupStart;
    uint16_t groupCount;

    uint32_t staticGroupCount;
    uint32_t staticTransformedGroupCount;
    uint32_t dynamicGroupCount;
    uint32_t deferredCorrectionGroupCount;
    uint32_t repairGroupCount;
    uint32_t fallbackGroupCount;

    float rootMotionDeltaMm;
    float externalForceValue;
    float editInfluenceValue;
    float strandRiskValue;

    uint32_t dependencyVersion;
};
```

---

### 22.5 PhysicsCertifiedSegmentGroupStrand

```cpp
struct PhysicsCertifiedSegmentGroupStrand
{
    uint32_t strandId;
    uint32_t pointCount;
    uint32_t groupCount;

    float rootPosition[3];
    float rootFrame[3][3];

    float groupLength;
    float rootRadius;
    float tipRadius;

    StrandCertificate strandCertificate;

    SegmentGroupCertificate* groupCertificates;
    SegmentGroupCache* groupCaches;
};
```

初期実装では、固定最大数で持ってよい。

```cpp
static constexpr uint32_t kMaxPointsPerStrand = 32;
static constexpr uint32_t kMaxGroupsPerStrand = kMaxPointsPerStrand - 1;
```

---

## 23. decision rule

group-level decisionは、Script overrideの優先順位を反映して決定する。

```cpp
inline uint32_t DecideSegmentGroupState(
    const SegmentGroupCertificate& g,
    const RuntimeSettings& s)
{
    if ((g.certificateBits & CERT_NUMERIC_NAN_INF) != 0)
        return REJECTED;

    if ((g.certificateBits & CERT_PAYLOAD_INVALID) != 0)
        return REJECTED;

    if ((g.certificateBits & CERT_ROOT_FRAME_INVALID) != 0)
        return REJECTED;

    if ((g.certificateBits & CERT_PROFILE_INVALID) != 0)
        return REJECTED;

    if ((g.certificateBits & CERT_PENETRATION) != 0)
    {
        if (g.penetrationResidualMm >= s.floatFallbackPenetrationMm)
            return FALLBACK_FLOAT;

        return REPAIR_REQUIRED;
    }

    if ((g.certificateBits & CERT_EDIT_STALE) != 0)
        return DYNAMIC_SOLVE;

    if (g.overrideState == FORCE_DYNAMIC)
        return DYNAMIC_SOLVE;

    if (g.overrideState == FORCE_STATIC)
    {
        if ((g.certificateBits & CERT_CONTACT_RISK) == 0 &&
            (g.certificateBits & CERT_STATIC_CACHE_STALE) == 0 &&
            g.certificateValue < s.forceStaticMaxCertificateValue)
        {
            return STATIC_CACHED;
        }
    }

    if (g.certificateValue >= s.floatFallbackCertificateValue)
        return FALLBACK_FLOAT;

    if ((g.certificateBits & CERT_DEFERRED_CORRECTION) != 0)
        return DEFERRED_CORRECTION;

    if ((g.certificateBits & CERT_STATIC_CACHE_VALID) != 0 &&
        g.certificateValue < s.staticCacheCertificateValue)
    {
        return STATIC_CACHED;
    }

    if ((g.certificateBits & CERT_STATIC_CACHE_STALE) != 0)
        return DYNAMIC_SOLVE;

    if (g.staticDynamicBoundaryErrorMm >= s.boundaryRepairThresholdMm)
        return REPAIR_REQUIRED;

    return DYNAMIC_SOLVE;
}
```

---

## 24. Cache Invalidation

Static cacheは、以下の条件で無効化される。

```txt
- root transform versionが変化した
- player motion versionが変化した
- animation versionが変化した
- wind versionが変化した
- edit versionが変化した
- collider versionが変化した
- 隣接groupがDynamicになった
- collision riskが閾値を超えた
- dependency versionが一致しない
- Static Watchdogで異常が検出された
```

ただし、root移動だけで形状変化が小さい場合は、完全Dynamicにはせず `STATIC_TRANSFORMED` として扱う。

---

## 25. Static Watchdog

Static Watchdogは、Static cacheの誤使用を検出するための安全機構である。

Static groupを毎フレーム完全に物理計算することは避けるが、一定間隔またはリスクが高い場合に軽量検査を行う。

```txt
Static Watchdog:
  1. Static groupを一定周期でサンプリングする
  2. 周辺groupとの差分を確認する
  3. root / collider / edit / animation versionを確認する
  4. certificateValueを再評価する
  5. 異常があればDeferred Correction、Repair、Dynamic solveへ送る
```

Watchdogによって、Static cacheが長期間固定されて見た目が破綻する問題を防ぐ。

---

## 26. false-static

本研究で最も重要な評価指標の1つが `false-static` である。

```txt
false-static:
  Static cacheを使ってよいと判定したが、
  実際にはDynamic solveまたはRepairが必要だったgroup
```

false-staticは、単なる高速化では見落とされやすい危険を測るための指標である。

例えば、Static reuse rateが高くても、false-static rateが高ければ、その高速化は安全ではない。

そのため、本研究では以下の両方を同時に評価する。

```txt
1. どれだけ再計算を避けられたか
2. 避けた再計算がどれだけ安全だったか
```

---

## 27. 編集時の扱い

本研究では、制作中・編集時の処理を重要な用途として扱う。

編集操作が入った場合、編集対象付近のgroupだけをDynamic化する。

```txt
edit brush / gizmo / tool
  -> edit influence areaを取得
  -> 対象groupにCERT_EDIT_STALEを付与
  -> 対象groupをDYNAMIC_SOLVEへ変更
  -> 周辺groupをDEFERRED_CORRECTIONまたはREPAIR_REQUIREDへ変更
  -> 本計算完了後にcacheを更新
```

編集対象外のgroupは、可能な限りStatic cacheを利用する。
ただし、隣接Dynamic groupの影響で不自然になる場合はDeferred Correctionを挟む。

---

## 28. root移動時の物理前簡易伝播

髪1本が丸ごと移動する場合、rootからtip方向へ簡易的な位置伝播を行う。

これは最終物理結果ではなく、最終物理結果の1つ前の補助用である。
また、処理が間に合わない場合の応急措置としても利用する。

```txt
Pre-Physics Propagation:
  1. rootの移動差分を取得
  2. XYZの+/-方向の差分を算出
  3. rootからtip方向へ仮位置を伝播
  4. groupごとの仮位置を生成
  5. collision / certificateを確認
  6. 間に合えばDynamic solveへ送る
  7. 間に合わなければ補助結果として描画に使う
```

この処理は、物理結果を完全に置き換えるものではない。
あくまで、Dynamic solve前の予測、または処理遅延時のfallback的補助として扱う。

---

## 29. Adaptive Strand/Group Parallel Policy

本設計では、並列処理の単位を固定しない。

多くの髪を同時に処理する場合は、strand単位の並列処理を優先する。
この場合、同じ髪の中のgroupは基本的に順序を守って処理する。

```txt
多くの髪を処理する場合:
  strand単位で並列
  strand内groupは基本的に順序処理
```

一方で、処理対象のstrandが少ない場合、または1本の髪を重点的に編集している場合は、group単位の並列処理も許可する。

```txt
少数の髪を処理する場合:
  group単位の並列処理を許可
```

この方針を `Adaptive Strand/Group Parallel Policy` と呼ぶ。

---

## 30. 共有点の書き込み競合対策

GPU上でgroup単位に処理する場合、隣接groupが共有点を書き換える可能性がある。

例：

```txt
Group 0 : P0 - P1
Group 1 : P1 - P2
```

このとき、P1はGroup 0の終点であり、Group 1の始点である。

基本方針として、P1はGroup 0の結果を確定値として扱い、Group 1はその結果を始点として利用する。

また、point bufferへの反映はgroup計算中に直接行わず、別passで行う。

```txt
GroupSolve pass:
  groupごとの結果候補を作る

ApplyGroupResult pass:
  point bufferへ反映する
```

1本の髪に対してgroup並列処理を行う場合は、周辺groupの数値推移をもとに仮の結果候補を作る。
この候補は最終結果ではなく、補正または緊急用として扱う。

---

## 31. collision checkの頻度

collision checkは、group状態によって頻度と精度を変える。

```txt
STATIC_CACHED:
  collider versionが変わっていなければ省略

STATIC_TRANSFORMED:
  軽量collision checkのみ行う

DYNAMIC_SOLVE:
  通常collision checkを行う

REPAIR_REQUIRED:
  対象groupと周辺groupのみ重点的にcheck

FALLBACK_FLOAT:
  通常のfloat点列処理に戻してcheck
```

これにより、すべてのgroupに同じ精度のcollision checkを行うのではなく、危険なgroupを優先的に検査する。

---

## 32. GPU実装方針

GPUでは、以下のpassに分ける。

```txt
1. StrandCertificateQueryCS
2. SegmentGroupCertificateQueryCS
3. PrePhysicsPropagationCS
4. StaticCacheReuseCS
5. DynamicGroupSolveCS
6. DeferredCorrectionCS
7. GroupRepairCS
8. CollisionCheckCS
9. ApplyGroupResultCS
10. RenderBufferBuildCS
```

### 32.1 多数strand処理

```txt
- strand単位で並列化
- strand内groupは基本的に順序処理
- 共有点の競合を避ける
```

### 32.2 少数strand / 編集時処理

```txt
- group単位の並列処理を許可
- group result bufferへ候補を書き込む
- point bufferへの反映は別passで行う
- 共有点は前groupの結果を優先し、必要に応じて周辺推移から補正候補を作る
```

### 32.3 CPU / GPU decision一致

本研究では、CPU queryとGPU queryのdecision一致を重要な検証項目とする。

```txt
CPU DecideSegmentGroupState
  == GPU SegmentGroupCertificateQueryCS decision
```

CPUとGPUでdecisionがずれる場合、telemetryや再現実験の信頼性が下がる。
そのため、同一入力に対してCPU/GPUのstate decisionが一致することをUnit TestおよびRuntime Testで確認する。

---

## 33. Telemetry

評価指標は、以下を基本とする。

```txt
false_static_count
false_static_rate

total_segment_group_count
static_cached_group_count
static_transformed_group_count
dynamic_group_count
deferred_correction_group_count
repair_required_group_count
fallback_group_count

group_static_reuse_rate
group_dynamic_rate
deferred_correction_rate

watchdog_check_count
watchdog_repair_count

cache_hit_count
cache_miss_count
cache_invalidation_count

static_dynamic_boundary_mean_error_mm
static_dynamic_boundary_max_error_mm

direction_cache_memory_mb
full_position_memory_mb
direction_cache_memory_saving_rate

frame_time_ms
edit_latency_ms
penetration_residual_mm
length_residual_ratio
```

特に重要なのは、以下である。

```txt
false_static_rate:
  Static化判断の危険性を測る

group_static_reuse_rate:
  どれだけ再計算を避けられたかを測る

static_dynamic_boundary_error_mm:
  Static / Dynamic混在時の境界破綻を測る

direction_cache_memory_saving_rate:
  フル座標保持と比較したメモリ削減率を測る
```

---

## 34. 比較対象

本研究では、以下の比較対象を用意する。

```txt
Baseline A: Full Dynamic Solve
  すべてのgroupを毎フレームDynamic solveする。

Baseline B: Strand-level Static/Dynamic
  strand単位でStatic / Dynamicを切り替える。
  group-level certificateは使用しない。

Baseline C: Direction Cache Only
  cachedDirectionによる再利用は行うが、certificate、watchdog、false-static検出は行わない。

Baseline D: No Deferred Correction
  Static / Dynamic混在時の一時補正を無効化する。

Baseline E: No Watchdog
  Static Watchdogを無効化する。

Baseline F: No Repair
  Repairを行わず、DynamicまたはFallbackのみで処理する。

Proposed:
  strand-level certificateとgroup-level certificateを併用し、
  Static / Dynamic / Deferred / Repair / Fallbackをgroup単位で切り替える。
```

---

## 35. 評価仮説

本研究で検証する仮説は以下である。

```txt
H1:
  ProposedはFull Dynamic Solveと比較してframe timeを削減できる。

H2:
  ProposedはStrand-level Static/Dynamicよりもfalse-static rateを低く抑えられる。

H3:
  ProposedはDirection Cache OnlyよりもStatic cache再利用の安全性が高い。

H4:
  Deferred Correctionにより、Static / Dynamic混在境界の誤差を低減できる。

H5:
  Static Watchdogにより、長時間Static化されたgroupの破綻を検出できる。

H6:
  direction cacheにより、full position保持よりもメモリ使用量を削減できる。

H7:
  CPU queryとGPU queryのdecisionは一致する。
```

---

## 36. テスト項目

### 36.1 Unit Test

```txt
TestSegmentGroupBuild
TestSegmentGroupEqualLengthResampling
TestJointHemisphereSplit
TestStaticCacheReuse
TestStaticCacheEndFromDirection
TestStaticTransformedPropagation
TestDeferredCorrection
TestStaticWatchdog
TestCacheInvalidation
TestStrandLevelCertificate
TestGroupLevelCertificate
TestScriptOverridePriority
TestFalseStaticAudit
TestCertificateValueComputation
```

### 36.2 GPU Test

```txt
TestGpuStrandQueryMatchesCpu
TestGpuGroupQueryMatchesCpu
TestGpuPrePhysicsPropagation
TestGpuStaticCacheReuse
TestGpuDynamicGroupSolve
TestGpuDeferredCorrection
TestGpuApplyGroupResult
TestGpuCollisionCheck
TestGpuRenderBufferBuild
```

### 36.3 Runtime Test

```txt
TestStandingNoAnimationStaticReuse
TestPlayerMotionStaticTransformed
TestWindForcesDynamicGroups
TestEditOnlyTargetGroupsDynamic
TestColliderUpdateInvalidatesCache
TestStaticDynamicDeferredCorrection
TestStaticWatchdogFindsFrozenGroup
TestAdaptiveStrandGroupParallelPolicy
TestFalseStaticUnderSuddenMotion
TestFallbackWhenCertificateTooHigh
```

---

## 37. 完了条件

本実装は、以下を満たしたら完了とする。

```txt
1. 髪1本をSegment Group集合として構築できる
2. 各groupを点-線-点として扱える
3. 等間隔resamplingでgroup長を揃えられる
4. 共有点をJoint Hemisphere Splitで扱える
5. strand-level certificateで大まかな処理方針を判定できる
6. group-level certificateでStatic / Dynamic / Deferred / Repair / Fallbackを判定できる
7. certificateValueを定量的に計算できる
8. Static groupをend = start + cachedDirection * groupLengthで復元できる
9. root移動時に物理前簡易伝播ができる
10. Dynamic groupだけを再計算できる
11. Static / Dynamic混在時にDeferred Correctionで見た目破綻を防げる
12. 余裕が出たときにDeferred groupを本計算へ回せる
13. collision check頻度を状態ごとに切り替えられる
14. Static Watchdogで固定バグを検出できる
15. Adaptive Strand/Group Parallel Policyを実装できる
16. group result bufferとApplyGroupResult passで共有点競合を回避できる
17. false_static_countをtelemetryに出せる
18. false_static_rateをtelemetryに出せる
19. static_dynamic_boundary_error_mmをtelemetryに出せる
20. direction_cache_memory_saving_rateをtelemetryに出せる
21. CPU queryとGPU queryのdecisionが一致する
22. 既存HairSystem runtimeに接続できる
23. 比較実験でFull Dynamic、strand-level、cache-only、no-deferred、no-watchdogとの差を示せる
```

---

## 38. 実装順序

### Phase 1: CPUデータ構造

```txt
- SegmentGroupRange
- SegmentGroupCache
- SegmentGroupCertificate
- StrandCertificate
- PhysicsCertifiedSegmentGroupStrand
```

### Phase 2: group生成

```txt
- 入力strandの等間隔resampling
- 点-線-点group生成
- groupLength計算
- Joint Hemisphere Split情報付与
```

### Phase 3: Static cache

```txt
- cachedDirection保存
- tangent保存
- dependencyVersion保存
- end = start + cachedDirection * groupLength
- rootから各点を復元
```

### Phase 4: certificate判定

```txt
- strand-level certificate
- group-level certificate
- certificateValue計算
- Script override priority
- Cache invalidation
```

### Phase 5: root移動・補助伝播

```txt
- Pre-Physics Propagation
- STATIC_TRANSFORMED
- 処理遅延時の補助描画
```

### Phase 6: Dynamic / Deferred / Repair

```txt
- DYNAMIC_SOLVE
- DEFERRED_CORRECTION
- REPAIR_REQUIRED
- Static / Dynamic境界補正
```

### Phase 7: GPU化

```txt
- StrandCertificateQueryCS
- SegmentGroupCertificateQueryCS
- PrePhysicsPropagationCS
- StaticCacheReuseCS
- DynamicGroupSolveCS
- DeferredCorrectionCS
- ApplyGroupResultCS
- RenderBufferBuildCS
```

### Phase 8: telemetry / 評価

```txt
- static reuse rate
- dynamic group rate
- deferred correction rate
- false static count
- false static rate
- watchdog check count
- static dynamic boundary error
- frame time
- edit latency
- collision risk
- memory saving rate
```

---

## 39. 関連研究との差分説明用メモ

本研究は、以下の研究・技術と近い部分を持つ。

```txt
Discrete Elastic Rods
https://doi.org/10.1145/1360612.1360662

Super-Helices for Predicting the Dynamics of Natural Hair
https://doi.org/10.1145/1141911.1141929

Fast Simulation of Inextensible Hair and Fur
https://matthias-research.github.io/pages/publications/FTLHairFur.pdf

Position Based Dynamics
https://matthias-research.github.io/pages/publications/posBasedDyn.pdf

XPBD: Position-Based Simulation of Compliant Constrained Dynamics
https://matthias-research.github.io/pages/publications/XPBD.pdf

Position Based Elastic Rods
https://doi.org/10.1145/2601097.2601103

Adaptive Wisp Tree
https://doi.org/10.1145/846276.846288

GPU Gems 2: Hair Animation and Rendering in the Nalu Demo
https://developer.nvidia.com/gpugems/gpugems2/part-iii-high-quality-rendering/chapter-23-hair-animation-and-rendering-nalu-demo
```

ただし、本研究ではこれらと同じことを新規性として主張しない。

本研究の位置づけは以下である。

```txt
既存研究:
  髪を点列、segment、rod、guide/follower、clusterとして表現し、
  物理計算、LOD、描画、編集を行う。

本研究:
  既存のsegment-based hair representationを前提とし、
  strand内部のSegment Groupに処理状態、certificate、cache validityを持たせ、
  Static reuse、Dynamic solve、Deferred Correction、Repair、Fallbackを
  group単位で切り替えるruntime hair data structureを提案する。
```

---

## 40. この研究の最終的な主張

本研究では、髪1本を単なる点列やcurveとして毎フレーム全計算するのではなく、点-線-点のSegment Group集合として扱う。

ただし、Segment Groupは単なる形状単位ではない。
各groupは、物理安全性、編集状態、衝突リスク、cache再利用可能性、runtime stateを持ち、Static / Dynamic / Deferred Correction / Repair / Fallbackを切り替える処理経路選択単位である。

動いていない部分は前回結果を方向ベクトルとして再利用し、必要な点位置はrootまたは前groupの確定位置から復元する。
一方で、編集・衝突・外力などで変化が必要な部分だけをDynamic再計算する。

さらに、処理が間に合わない場合やStatic / Dynamicが混在する場合には、Deferred Correctionによって一時的に見た目の破綻を防ぎ、余裕が出たときに本計算へ回す。

本研究では、Static cacheの危険性を無視しない。
誤ってStatic化されたgroupをfalse-staticとして計測し、Static / Dynamic境界誤差、watchdog repair、cache invalidation、memory saving rateとあわせて評価する。

最終的に、リアルタイムな髪編集・描画・物理判定において、見た目の破綻を抑えつつ、計算量とメモリ量を削減できる、処理経路選択型の髪データ構造を実装する。

---

## 41. 一文まとめ

本研究では、既存のsegment-based hair representationを基盤とし、髪strand内部のSegment Groupごとにcertificate、cache validity、runtime stateを持たせることで、Static再利用、Dynamic再計算、Deferred Correction、Repair、Fallbackをdecode前または軽量query段階で選択し、false-static率と境界誤差を計測しながら安全な再利用を行う、処理経路選択型の髪データ構造を実装する。
