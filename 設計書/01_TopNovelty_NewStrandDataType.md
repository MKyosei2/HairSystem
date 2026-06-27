# 01. 新しいデータ型系

# Physics-Certified Strand Data Type with Segment-Group Static/Dynamic Cache

# 企画書兼設計書 最新版

---

## 1. 概要

本企画では、髪の毛を単なる点列やcurveとして扱うのではなく、髪1本を `点 - 線 - 点` の `Segment Group` 集合として扱い、各groupごとにStatic / Dynamic / Repair / Fallbackを切り替えられる新しい髪データ型を設計・実装する。

従来の髪表現では、髪1本を構成する点列を毎フレームdecodeし、その後に物理計算、衝突判定、修復、描画を行う。しかし、実際には動いていない部分まで再計算してしまうため、リアルタイム編集や大量の髪描画では計算コストが大きくなる。

本設計では、髪1本を構成する各線分を `Segment Group` として管理し、動く必要がないgroupは前回の結果をStatic cacheとして再利用する。一方で、編集・衝突・風・プレイヤー移動・アニメーションなどにより変化が必要なgroupだけをDynamicとして再計算する。

さらに、strand-level certificateとgroup-level certificateを組み合わせることで、髪全体の大まかな動的必要性と、髪1本内部の細かい再計算必要性を二段階で判定する。

---

## 2. 実装新規性の一文

髪1本を単なる点列ではなく、`点 - 線 - 点` のSegment Group集合として扱い、strand-level certificateで大まかな処理方針を決め、group-level certificateでStatic再利用・Dynamic再計算・Deferred Correction・Repair・Fallbackを決定する、物理証明付きの新しい髪データ型を実装する。

---

## 3. 研究・実装の核

本研究の核は、以下である。

```txt
髪1本のデータ型が、decode前に処理経路を決める。

さらに、髪1本全体ではなく、
点-線-点のSegment Group単位で、
Static再利用するか、Dynamic再計算するか、
補正で一時対応するか、Repair / Fallbackへ送るかを判断する。
```

このデータ型は、decode前または軽量query段階で以下を判定できることを目指す。

```txt
1. このstrand全体は動く可能性があるか
2. このstrand内のどのgroupだけ再計算すべきか
3. どのgroupはStatic cacheを使えるか
4. どのgroupは一時補正で対応できるか
5. どのgroupはRepairが必要か
6. どのgroupはFallbackが必要か
7. どのgroupは衝突リスクを持つか
8. どのgroupは編集によってstaleになっているか
9. どのgroupはfalse-staticの危険があるか
```

---

## 4. Segment Group の定義

### 4.1 基本単位

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

基本的には、1group = 1segmentである。
ただし、将来的な拡張として、複数segmentを1groupとして扱えるように、内部表現はrange形式で保持する。

---

## 5. group長とresampling

各Segment Groupの長さは、髪1本の中では同じ長さとして扱う。

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

これにより、Static cache、Dynamic判定、repair範囲、GPU処理を単純化する。

---

## 6. 共有点の半球分割

隣接するgroupでは、終点と始点が共有される。

```txt
Group 0 : P0 - P1
Group 1 : P1 - P2
```

このとき、`P1` はGroup 0の終点であり、Group 1の始点でもある。

本設計では、この共有点を完全に二重計算するのではなく、半球的に分けて扱う。

半球分割の基準は、髪の毛をすべて重力を無視して上方向に伸ばした基準姿勢とする。
この基準姿勢において、共有点を上半分と下半分に分ける。

```txt
共有点P_i:

上半分:
  rootからtip方向へ向かう側の影響範囲

下半分:
  root側、または1つ前のgroup側の影響範囲
```

この考え方を `Joint Hemisphere Split` と呼ぶ。

重要なのは、半球分割は毎フレームの変形状態ではなく、重力を無視して上方向へ伸ばしたrest姿勢を基準に定義する点である。

---

## 7. Static / Dynamic 状態

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

---

## 8. STATIC_CACHED

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
- dependency version
```

フルの座標を持つのは基本的に根元のみとし、それ以外の点はrootまたは前groupの確定点から、方向ベクトルとgroup長によって復元する。

---

## 9. STATIC_TRANSFORMED

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

---

## 10. DYNAMIC_SOLVE

`DYNAMIC_SOLVE` は、物理計算が必要な状態である。

以下の場合にDynamicになる。

```txt
- プレイヤーが前フレームから動いた
- root transformが変化した
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

## 11. DEFERRED_CORRECTION

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
end = start + cachedDirection * groupLength
```

この補正は、本計算の代わりではなく、処理が間に合わない場合や、1から再計算する前の緊急用・補助用として使う。

処理予算に余裕が出た場合、Deferred Correction状態のgroupを本計算へ回し、cacheとcertificateを更新する。

```txt
Deferred Correction:
  1. 隣接Dynamic groupの結果を受け取る
  2. startを更新する
  3. cachedDirection * groupLengthでendを仮更新する
  4. 見た目の破綻を防ぐ
  5. 後続フレームで余裕があればDYNAMIC_SOLVEまたはREPAIR_REQUIREDへ送る
```

---

## 12. REPAIR_REQUIRED

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

---

## 13. FALLBACK_FLOAT

`FALLBACK_FLOAT` は、certificate付きの軽量処理では安全に扱えない場合に、通常のfloat点列処理へ戻す状態である。

以下の場合にFallbackへ送る。

```txt
- certificateValueが閾値を超えた
- residualが大きすぎる
- repairで復元できない
- root / payload / profileに深刻な問題がある
- false-staticが繰り返し発生した
```

---

## 14. REJECTED

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

## 15. strand-level certificate と group-level certificate

本設計では、certificateをstrand単位とgroup単位のハイブリッド構造にする。

### 15.1 strand-level certificate

strand-level certificateは、髪1本または髪のエリア単位で、大まかな処理方針を決める。

役割は以下である。

```txt
- このstrand全体が動く可能性があるか
- rootが動いたか
- プレイヤー移動の影響を受けたか
- アニメーションの影響を受けたか
- 外力の影響を受けたか
- 編集対象に含まれるか
- group-level queryを実行すべきか
- strand全体をStatic候補にできるか
- strand全体をDynamic候補にすべきか
```

### 15.2 group-level certificate

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

## 16. strand判定とgroup判定の優先関係

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

ただし、fatal / collision danger / edit operationなど安全性に関わる条件は、通常のstrand-level判定を上書きできる。

---

## 17. Script overrideの優先順位

Script overrideは、長期的な拡張として、strandまたはgroup単位でStatic / Dynamicを強制できる仕組みである。

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

つまり、`force static` が指定されていても、衝突危険や編集操作、fatalな異常がある場合はStaticにしない。

---

## 18. Static cacheの基本方針

Static cacheは、メモリ量を抑えるため、最小限の情報を保持する。

基本的に保持する情報は以下である。

```txt
- root position
- cachedDirection
- tangent
- dependencyVersion
```

補助的に必要な場合のみ以下を持つ。

```txt
- lastUpdatedFrame
- cachedState
- previousCollisionState
- certificateValue
```

フルのworld座標を全点で保持するのではなく、rootのみ実座標を保持し、各groupはベクトル系で補う。

```txt
P0 = rootPosition
P1 = P0 + cachedDirection[0] * groupLength
P2 = P1 + cachedDirection[1] * groupLength
P3 = P2 + cachedDirection[2] * groupLength
...
```

この構造により、Staticな髪のメモリ量を抑えつつ、必要なときだけworld positionを復元できる。

---

## 19. root移動時の物理前簡易伝播

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

## 20. Adaptive Strand/Group Parallel Policy

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

## 21. 共有点の書き込み競合対策

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

## 22. Static / Dynamic 混在時の補正

長期的には、以下のような状態が発生する。

```txt
Group 0 : Static
Group 1 : Dynamic
Group 2 : Static
```

このとき、Group 1だけが動くと、Group 2がStaticのまま取り残され、不自然な見た目になる可能性がある。

そのため、Static groupであっても、1つ前のgroupが変化した場合は、以下の補正を行う。

```txt
correctedStart = previousGroup.end
correctedEnd   = correctedStart + cachedDirection * groupLength
```

この補正は、完全な物理計算ではなく、見た目の破綻や固定バグを防ぐための補助処理である。

その後、Dynamic groupの計算が終わり、処理予算に余裕がある場合に、補正されたStatic groupを本計算へ回してcacheを更新する。

---

## 23. collision checkの頻度

collision checkは、group状態によって頻度と精度を変える。

```txt
STATIC_CACHED:
  collider versionが変わっていなければ省略

STATIC_TRANSFORMED:
  軽量collision checkのみ行う

DYNAMIC_SOLVE:
  通常collision checkを行う

REPAIR_REQUIRED:
  詳細collision checkを行う
```

また、1秒以上状態が変わらなかったgroupに対しては、1秒ごとに確認を行う。

この1秒チェックは通常処理ではなく、主に以下の用途で使う。

```txt
- 静止時の整合性確認
- 髪の一部だけが固定位置に残るバグの検出
- 実際にバグが発生した場合の応急措置
- Static groupが長時間更新されていない場合の安全確認
```

ただし、以下の場合は1秒を待たずに即時チェックする。

```txt
- collider versionが変化した
- edit operationが入った
- playerが動いた
- root transformが変化した
- windが有効になった
- collision dangerが立った
```

この保険処理を `Static Watchdog` と呼ぶ。

---

## 24. Static Watchdog

`Static Watchdog` は、Static groupが不自然に固定されたままになることを防ぐための保険機構である。

主に静止時に1秒ごとに軽量チェックを行う。
ただし、動作中でも不自然な固定や周辺groupとの乖離が検出された場合は応急措置として実行する。

```txt
Static Watchdog:
  1. 長時間更新されていないStatic groupを検出
  2. 周辺groupとの差分を確認
  3. root / collider / edit / animation versionを確認
  4. 異常があればDeferred CorrectionまたはRepairへ送る
  5. 必要ならDynamic solveへ戻す
```

---

## 25. certificate bits

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

## 26. 基本データ構造

### 26.1 SegmentGroupRange

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

### 26.2 SegmentGroupCache

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

### 26.3 SegmentGroupCertificate

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

    float certificateValue;

    uint32_t cachedFrameIndex;
    uint32_t dependencyVersion;

    uint32_t overrideState;
};
```

---

### 26.4 StrandCertificate

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

### 26.5 PhysicsCertifiedSegmentGroupStrand

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

## 27. decision rule

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

    if ((g.certificateBits & CERT_PENETRATION) != 0)
        return REPAIR_REQUIRED;

    if ((g.certificateBits & CERT_EDIT_STALE) != 0)
        return DYNAMIC_SOLVE;

    if (g.overrideState == FORCE_DYNAMIC)
        return DYNAMIC_SOLVE;

    if (g.overrideState == FORCE_STATIC)
    {
        if ((g.certificateBits & CERT_CONTACT_RISK) == 0 &&
            (g.certificateBits & CERT_STATIC_CACHE_STALE) == 0)
        {
            return STATIC_CACHED;
        }
    }

    if (g.certificateValue >= s.floatFallbackCertificateValue)
        return FALLBACK_FLOAT;

    if ((g.certificateBits & CERT_DEFERRED_CORRECTION) != 0)
        return DEFERRED_CORRECTION;

    if ((g.certificateBits & CERT_STATIC_CACHE_VALID) != 0)
        return STATIC_CACHED;

    if ((g.certificateBits & CERT_STATIC_CACHE_STALE) != 0)
        return DYNAMIC_SOLVE;

    return DYNAMIC_SOLVE;
}
```

---

## 28. Cache Invalidation

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

## 29. 編集時の扱い

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

## 30. GPU実装方針

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

### 30.1 多数strand処理

```txt
- strand単位で並列化
- strand内groupは基本的に順序処理
- 共有点の競合を避ける
```

### 30.2 少数strand / 編集時処理

```txt
- group単位の並列処理を許可
- group result bufferへ候補を書き込む
- point bufferへの反映は別passで行う
- 共有点は前groupの結果を優先し、必要に応じて周辺推移から補正候補を作る
```

---

## 31. Telemetry

評価指標は既存のtelemetryを基本的に利用する。

ただし、今回の設計では `false_static` が重要になるため、既存telemetryに含まれているか確認する必要がある。

含まれていない場合は、以下を追加する。

```txt
false_static_count
false_static_rate
static_reuse_rate
static_transformed_rate
deferred_correction_count
deferred_correction_rate
watchdog_check_count
watchdog_repair_count
cache_hit_count
cache_miss_count
```

`false_static` の定義は以下である。

```txt
false_static:
  Static cacheを使ってよいと判定したが、
  実際にはDynamic solveまたはRepairが必要だったgroup
```

これは、本研究の安全性を確認するために必ず計測する。

---

## 32. テスト項目

### 32.1 Unit Test

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
```

### 32.2 GPU Test

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

### 32.3 Runtime Test

```txt
TestStandingNoAnimationStaticReuse
TestPlayerMotionStaticTransformed
TestWindForcesDynamicGroups
TestEditOnlyTargetGroupsDynamic
TestColliderUpdateInvalidatesCache
TestStaticDynamicDeferredCorrection
TestStaticWatchdogFindsFrozenGroup
TestAdaptiveStrandGroupParallelPolicy
```

---

## 33. 完了条件

本実装は、以下を満たしたら完了とする。

```txt
1. 髪1本をSegment Group集合として構築できる
2. 各groupを点-線-点として扱える
3. 等間隔resamplingでgroup長を揃えられる
4. 共有点をJoint Hemisphere Splitで扱える
5. strand-level certificateで大まかな処理方針を判定できる
6. group-level certificateでStatic / Dynamic / Deferred / Repair / Fallbackを判定できる
7. Static groupをend = start + cachedDirection * groupLengthで復元できる
8. root移動時に物理前簡易伝播ができる
9. Dynamic groupだけを再計算できる
10. Static / Dynamic混在時にDeferred Correctionで見た目破綻を防げる
11. 余裕が出たときにDeferred groupを本計算へ回せる
12. collision check頻度を状態ごとに切り替えられる
13. Static Watchdogで固定バグを検出できる
14. Adaptive Strand/Group Parallel Policyを実装できる
15. group result bufferとApplyGroupResult passで共有点競合を回避できる
16. false_safe_countをtelemetryに出せる
17. false_static_countをtelemetryに出せる
18. CPU queryとGPU queryのdecisionが一致する
19. 既存HairSystem runtimeに接続できる
```

---

## 34. 実装順序

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
- watchdog check count
- frame time
- edit latency
- collision risk
```

---

## 35. この研究の最終的な主張

本研究では、髪1本を単なる点列やcurveとして毎フレーム全計算するのではなく、点-線-点のSegment Group集合として扱う。

各groupは、物理安全性、編集状態、衝突リスク、cache再利用可能性を持ち、Static / Dynamic / Deferred Correction / Repair / Fallbackを切り替える。

動いていない部分は前回結果を方向ベクトルとして再利用し、必要な点位置はrootまたは前groupの確定位置から復元する。
一方で、編集・衝突・外力などで変化が必要な部分だけをDynamic再計算する。

さらに、処理が間に合わない場合やStatic / Dynamicが混在する場合には、Deferred Correctionによって一時的に見た目の破綻を防ぎ、余裕が出たときに本計算へ回す。

最終的に、リアルタイムな髪編集・描画・物理判定において、見た目の破綻を抑えつつ、計算量とメモリ量を削減できる新しい髪データ型を実装する。

---

## 36. 一文まとめ

髪1本を点-線-点のSegment Group集合として扱い、strand単位とgroup単位のcertificateによって、動く必要がある部分だけをDynamic再計算し、それ以外はStatic cacheまたはDeferred Correctionとして再利用・補正する、物理証明付きの新しい髪データ型を実装する。
