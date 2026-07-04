# 処理経路選択型 Segment-Group Hair Runtime

## Certificate-Driven Segment-Group Hair Runtime の企画書兼設計書

---

## 0. 何も知らない人向けの説明

髪の毛のシミュレーションは、とても重い処理である。
人の髪は本数が多く、1本の髪も複数の点や線分で構成されているため、すべての髪を毎フレーム完全に物理計算すると、ゲームやリアルタイム編集環境では処理が間に合わない。

しかし、実際にはすべての髪のすべての部分が毎瞬間大きく動いているわけではない。
たとえば、髪の根元付近はほとんど動いていないが、毛先だけ揺れている場合がある。また、編集ツールで一部だけを触っている場合、触っていない部分は前の形を再利用しても見た目に問題がないことがある。

そこで本研究では、髪1本をさらに小さな区間に分ける。
この小さな区間を **Segment Group** と呼ぶ。

各Segment Groupは、単に形を表すだけではなく、

* 前の計算結果をそのまま使ってよいか
* 少し補正すればよいか
* 物理計算をやり直すべきか
* 局所的に修復すべきか
* 軽量処理を諦めて通常処理に戻すべきか

を判断するための情報を持つ。

つまり本研究は、髪の毛を単なる点列や線分として扱うのではなく、**髪の各部分が自分の処理方法を選べるようにするデータ構造**を作る研究である。

さらに、本研究では単に処理を軽くするだけではない。
「動かなくてよい」と判断して前回結果を再利用した部分が、実は本当は動かすべきだった場合、その判断ミスを **false-static** として記録する。

これにより、単に速いだけではなく、**安全に軽量化できているか**を評価する。

---

## 1. 研究の概要

本研究では、既存の髪シミュレーションで用いられてきた点列、segment、rod、guide/follower、LOD、PBD/XPBDなどの考え方を前提とし、その上で、髪strand内部のSegment Groupごとに実行時の処理経路を選択できるデータ構造を設計・実装する。

従来の髪シミュレーションでは、髪を点列、curve、segment、rod、またはguide/follower構造として扱い、毎フレーム物理計算や描画処理を行うことが多い。また、計算量を削減するために、髪1本、髪束、guide、cluster、LOD単位で処理量を減らす手法も存在する。

しかし、実際のリアルタイム制作・編集・描画では、髪1本の内部でも処理の必要性は一様ではない。

同じstrandの中でも、

* 根元付近は前回結果を再利用できる
* 中間部は一時補正が必要である
* 毛先は物理計算が必要である
* 衝突や編集が入った部分だけ修復が必要である

といった状態が起こりうる。

本研究では、髪1本全体を一つの処理単位として扱うのではなく、strand内部をSegment Group単位に分解し、各groupが以下の処理状態を持てるようにする。

```txt
STATIC_CACHED
STATIC_TRANSFORMED
DYNAMIC_SOLVE
DEFERRED_CORRECTION
REPAIR_REQUIRED
FALLBACK_FLOAT
REJECTED
```

これにより、動いていない部分はStatic cacheとして再利用し、編集・衝突・外力・境界破綻の可能性がある部分のみをDynamic solve、Deferred Correction、Repair、Fallbackへ送る。

---

## 2. 本研究の主張

本研究の主張は、髪をsegmentとして表すことではない。
また、PBD、XPBD、LOD、guide/follower、cache reuse、GPU multi-passを新規に提案するものでもない。

本研究の主張は、既存のsegment-based hair representationを前提とし、**髪strand内部のSegment Groupをruntime decision unitとして扱う**点にある。

具体的には、各Segment Groupに以下の情報を持たせる。

```txt
- runtime state
- certificate
- cache validity
- dependency version
- edit stale 情報
- collision risk
- residual 情報
- static / dynamic boundary error
- false-static 検出用 telemetry
```

そして、これらの情報をもとに、各groupを以下のいずれかの処理経路へ分類する。

```txt
- 前回結果を再利用する
- root移動だけを反映する
- 物理計算を行う
- 一時補正を行う
- 局所修復する
- 通常のfloat点列処理へ戻す
- 異常値として拒否する
```

これにより、髪データそのものが、描画や物理計算の前に、どの処理を実行すべきかを判断できるようになる。

---

## 3. 本研究の新規性の位置づけ

### 3.1 新規性として主張しないもの

以下は既存研究・既存技術として扱い、本研究の新規性として主張しない。

```txt
- 髪を点列、segment、curve、rodとして扱うこと
- guide/follower型の髪表現
- 髪LOD、wisp、cluster、multiresolution表現
- PBD / XPBD による制約ベース物理
- Elastic Rods による細長い物体の表現
- GPU multi-pass simulation
- cache reuse
- sleeping
- partial update
- active set
- fallback
- residual評価
```

これらは髪シミュレーション、布、流体、剛体、パーティクルなどの分野で広く扱われてきた考え方である。

### 3.2 本研究の差分

本研究の差分は、以下にある。

```txt
1. strand全体ではなく、strand内部のSegment Group単位で処理状態を切り替える
2. 各Segment Groupにcertificate、cache validity、runtime stateを持たせる
3. Static / Dynamic / Deferred / Repair / Fallbackを同一データ構造内で管理する
4. false-staticを明示的に計測し、Static化判断の危険性を評価する
5. Static / Dynamic混在境界の誤差をtelemetryとして評価する
6. CPU queryとGPU queryのdecision一致を検証する
```

したがって、本研究は「髪をsegmentで表す研究」ではなく、
**髪strand内部のsegment groupに処理経路選択機能を持たせるruntime hair data structureの研究**である。

---

## 4. 研究のコア

本研究のコアは、以下である。

```txt
髪1本をSegment Groupの集合として扱い、
各Segment Groupが、
Static再利用するか、
Dynamic再計算するか、
Deferred Correctionで一時補正するか、
Repairするか、
Fallbackするかを、
実行時に判断できるようにする。
```

このとき重要なのは、Segment Groupを単なる形状単位として扱わないことである。

Segment Groupは、以下を判断するための単位である。

```txt
1. このgroupは前回結果を再利用できるか
2. このgroupはroot移動だけで済むか
3. このgroupは物理計算が必要か
4. このgroupは一時補正で見た目を保てるか
5. このgroupは局所修復が必要か
6. このgroupは通常処理へ戻すべきか
7. このgroupは異常値として拒否すべきか
8. このgroupは編集によって古くなっているか
9. このgroupは衝突リスクを持つか
10. このgroupはfalse-staticの危険があるか
```

---

## 5. Segment Group の定義

### 5.1 基本単位

本研究では、髪1本の中にある点と線分の組をSegment Groupとして扱う。

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

### 5.2 group長とresampling

入力データの点間距離がばらつく場合、事前処理としてstrand単位で等間隔resamplingを行う。

```txt
入力点列
  -> strand単位で全長を計算
  -> 必要group数を決定
  -> 等間隔resampling
  -> Segment Groupを生成
```

これにより、Static cache、Dynamic判定、Repair範囲、GPU処理を単純化する。

---

## 6. Segment Group State

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

### 6.1 STATIC_CACHED

`STATIC_CACHED` は、前回の結果を再利用する状態である。
この状態では、groupの物理計算を再実行しない。

基本式は以下である。

```txt
end = start + cachedDirection * groupLength
```

`cachedDirection` は前回計算時に保存された方向ベクトルである。
`groupLength` はresamplingによって決定されたstrand内のgroup長である。

この状態の目的は、動いていないgroupの再計算を避けることである。

### 6.2 STATIC_TRANSFORMED

`STATIC_TRANSFORMED` は、髪形状は前回結果を使うが、rootやstrand全体の移動だけを反映する状態である。

たとえば、キャラクターの頭部が少し移動したが、髪そのものの形状変化は小さい場合に使用する。

これは完全な物理結果ではなく、処理予算が不足している場合や、形状変化が小さい場合の安全な近似状態として扱う。

### 6.3 DYNAMIC_SOLVE

`DYNAMIC_SOLVE` は、通常の物理計算が必要な状態である。

以下の場合にDynamicになる。

```txt
- root transformが大きく変化した
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

### 6.4 DEFERRED_CORRECTION

`DEFERRED_CORRECTION` は、本計算をすぐに行わず、一時的な補正で見た目の破綻を防ぐ状態である。

たとえば、以下のような状態を考える。

```txt
Group 0 : Static
Group 1 : Dynamic
Group 2 : Static
```

Group 1が動いた場合、Group 2の始点も変化する可能性がある。
しかし、Group 2を毎回完全に再計算すると重くなる。

そこで、Group 2は一時的に以下の式で補正する。

```txt
correctedStart = previousGroup.end
correctedEnd   = correctedStart + cachedDirection * groupLength
```

Deferred Correctionは本計算の代わりではない。
処理が間に合わない場合や、1から再計算する前の応急処置として使用する。

### 6.5 REPAIR_REQUIRED

`REPAIR_REQUIRED` は、完全な再計算ではなく、groupまたは周辺groupのみを局所修復する状態である。

対象例は以下である。

```txt
- 長さ制約が少し破綻した
- bend residualが閾値を超えた
- 軽いpenetrationが発生した
- Static / Dynamic境界が不自然になった
- Deferred Correctionが複数フレーム続いた
```

Repairは、全strandをFallbackする前の安全弁である。

### 6.6 FALLBACK_FLOAT

`FALLBACK_FLOAT` は、certificate付きの軽量処理では安全に扱えない場合に、通常のfloat点列処理へ戻す状態である。

以下の場合にFallbackへ送る。

```txt
- certificateValueが閾値を超えた
- residualが大きすぎる
- repairで復元できない
- root / payload / profileに深刻な問題がある
- false-staticが繰り返し発生した
```

Fallbackは失敗ではなく、安全性を優先するための処理経路である。

### 6.7 REJECTED

`REJECTED` は、payload破損やNaN/Infなどにより、処理不能な状態である。

```txt
- NaN / Inf
- payload invalid
- root frame invalid
- quantization profile invalid
- overflow
```

この状態は、他の処理状態よりも優先される。

---

## 7. certificate の定義

本研究では `certificate` という語を使用するが、これは数学的に完全な正しさを証明するものではない。

本研究におけるcertificateとは、以下をまとめた **runtime validity descriptor** である。

```txt
- 長さ制約の破綻度
- 曲げ残差
- ねじれ残差
- 衝突リスク
- penetration residual
- 編集によるstale状態
- cache validity
- motion divergence
- Static / Dynamic境界誤差
```

何も知らない人向けに説明すると、certificateは
**この髪の区間を前回の結果のまま使ってよいかを判断する安全チェック情報**
である。

---

## 8. certificateValue

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

## 9. decision rule

group-level decisionは、Script overrideや安全性条件の優先順位を反映して決定する。

優先順位は以下とする。

```txt
fatal / reject
> collision danger
> edit operation
> force dynamic
> force static
> normal certificate decision
```

つまり、ユーザーやスクリプトがStaticを強制していても、衝突危険、編集操作、fatalな異常がある場合はStaticにしない。

基本的な判定の考え方は以下である。

```cpp
inline uint32_t DecideSegmentGroupState(
    const SegmentGroupCertificate& g,
    const RuntimeSettings& s)
{
    if (g has fatal error)
        return REJECTED;

    if (g has severe penetration)
        return FALLBACK_FLOAT;

    if (g has repairable penetration)
        return REPAIR_REQUIRED;

    if (g is edit stale)
        return DYNAMIC_SOLVE;

    if (force dynamic)
        return DYNAMIC_SOLVE;

    if (force static is safe)
        return STATIC_CACHED;

    if (certificateValue is too high)
        return FALLBACK_FLOAT;

    if (deferred correction is requested)
        return DEFERRED_CORRECTION;

    if (static cache is valid and certificateValue is low)
        return STATIC_CACHED;

    if (static cache is stale)
        return DYNAMIC_SOLVE;

    if (static / dynamic boundary error is high)
        return REPAIR_REQUIRED;

    return DYNAMIC_SOLVE;
}
```

---

## 10. Cache Invalidation

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

## 11. Static Watchdog

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

## 12. false-static

本研究で最も重要な評価指標の1つが `false-static` である。

```txt
false-static:
  Static cacheを使ってよいと判定したが、
  実際にはDynamic solveまたはRepairが必要だったgroup
```

何も知らない人向けに説明すると、false-staticとは、
**髪の一部を「動かなくていい」と判断して止めたのに、本当は動かすべきだったという判断ミス**
である。

Static reuse rateが高くても、false-static rateが高ければ、その高速化は安全ではない。

そのため、本研究では以下の両方を同時に評価する。

```txt
1. どれだけ再計算を避けられたか
2. 避けた再計算がどれだけ安全だったか
```

---

## 13. 編集時の扱い

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
ただし、隣接Dynamic groupの影響で不自然になる場合はDeferred CorrectionまたはRepairを挟む。

---

## 14. root移動時のPre-Physics Propagation

髪1本が丸ごと移動する場合、rootからtip方向へ簡易的な位置伝播を行う。

これは最終物理結果ではなく、Dynamic solve前の補助処理である。
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

---

## 15. Joint Hemisphere Split

隣接するgroupでは、終点と始点が共有される。

```txt
Group 0 : P0 - P1
Group 1 : P1 - P2
```

このとき、P1はGroup 0の終点であり、Group 1の始点でもある。

GPU上でgroup単位に処理する場合、隣接groupが共有点を書き換える可能性がある。
そのため、本研究では共有点の影響範囲をrest姿勢を基準に分ける。

これを `Joint Hemisphere Split` と呼ぶ。

ただし、これは本研究の中心的な新規性ではなく、Segment Groupを安全に並列処理するための補助設計である。

---

## 16. Adaptive Strand/Group Parallel Policy

本設計では、並列処理の単位を固定しない。

多くの髪を同時に処理する場合は、strand単位の並列処理を優先する。

```txt
多数の髪を処理する場合:
  strand単位で並列
  strand内groupは基本的に順序処理
```

一方で、処理対象のstrandが少ない場合、または1本の髪を重点的に編集している場合は、group単位の並列処理も許可する。

```txt
少数の髪を処理する場合:
  group単位の並列処理を許可
```

この方針を `Adaptive Strand/Group Parallel Policy` と呼ぶ。

ただし、これも本研究の中心的な新規性ではなく、実装上の最適化方針として扱う。

---

## 17. GPU実装方針

GPUでは、以下のpassに分けて処理する。

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

専門外向けに説明すると、処理の流れは以下である。

```txt
判定
  -> 再利用できる部分は再利用
  -> 必要な部分だけ再計算
  -> 間に合わない部分は一時補正
  -> 壊れそうな部分は修復
  -> 最後に描画用バッファを作る
```

本研究では、GPU化そのものを新規性としない。
重要なのは、CPU上とGPU上で同じdecisionが得られることである。

```txt
CPU DecideSegmentGroupState
  == GPU SegmentGroupCertificateQueryCS decision
```

CPUとGPUでdecisionがずれる場合、telemetryや再現実験の信頼性が下がる。
そのため、CPU/GPU decision一致を重要な検証項目とする。

---

## 18. 基本データ構造

### 18.1 SegmentGroupRange

```cpp
struct SegmentGroupRange
{
    uint16_t pointStart;
    uint16_t pointEnd;
    uint16_t segmentStart;
    uint16_t segmentEnd;
};
```

### 18.2 SegmentGroupCache

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

### 18.3 SegmentGroupCertificate

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

### 18.4 StrandCertificate

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

### 18.5 SegmentGroupHairStrand

```cpp
struct SegmentGroupHairStrand
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

---

## 19. 評価指標

本研究では、以下のtelemetryを出力する。

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

特に重要な指標は以下の4つである。

| 指標                                 | 測るもの                | 専門外向けの意味           |
| ---------------------------------- | ------------------- | ------------------ |
| frame_time_ms                      | 処理時間                | 60FPSを守れるか         |
| false_static_rate                  | Static判断の失敗率        | 止めてはいけない部分を止めていないか |
| static_dynamic_boundary_error_mm   | Static/Dynamic境界の誤差 | 髪の途中が不自然に折れていないか   |
| direction_cache_memory_saving_rate | メモリ削減率              | どれだけ軽く保存・処理できたか    |

---

## 20. 比較対象

本研究では、以下の比較対象を用意する。

```txt
Baseline A: Full Dynamic Solve
  すべてのgroupを毎フレームDynamic solveする。

Baseline B: Strand-level Static/Dynamic
  strand単位でStatic / Dynamicを切り替える。
  group-level certificateは使用しない。

Baseline C: Direction Cache Only
  cachedDirectionによる再利用は行うが、
  certificate、watchdog、false-static検出は行わない。

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

専門外向けに言い換えると、以下の比較である。

```txt
全部計算する方法
髪1本単位で切り替える方法
前回結果の再利用だけを行う方法
一時補正を使わない方法
監視機能を使わない方法
修復機能を使わない方法
提案手法
```

---

## 21. 評価仮説

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

## 22. テスト項目

### 22.1 Unit Test

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

### 22.2 GPU Test

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

### 22.3 Runtime Test

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

## 23. 完了条件

本研究は、以下を満たしたら完了とする。

### 23.1 最低限成功条件

```txt
1. 髪1本をSegment Group集合として構築できる
2. 各groupを点-線-点として扱える
3. group-level certificateを計算できる
4. Static / Dynamic / Deferred / Repair / Fallbackを判定できる
5. Static groupを cachedDirection * groupLength で復元できる
6. false_static_countをtelemetryに出せる
7. false_static_rateをtelemetryに出せる
8. static_dynamic_boundary_error_mmをtelemetryに出せる
9. Full Dynamic Solveとの比較ができる
10. Strand-level Static/Dynamicとの比較ができる
```

### 23.2 研究として強くなる条件

```txt
1. Static Watchdogで固定バグを検出できる
2. Deferred Correctionで境界破綻を軽減できる
3. Repairで局所的な破綻を修復できる
4. direction_cache_memory_saving_rateを計測できる
5. CPU queryとGPU queryのdecision一致を確認できる
6. 60FPS制約下で処理時間の削減を示せる
```

### 23.3 将来拡張条件

```txt
1. 複数segmentを1groupとして扱える
2. より高度なcollision checkを導入できる
3. より複雑な編集操作に対応できる
4. DCCツールやゲームエンジンとの接続を行える
5. より大規模な髪データで検証できる
```

---

## 24. 実装順序

### Phase 1: CPUデータ構造

```txt
- SegmentGroupRange
- SegmentGroupCache
- SegmentGroupCertificate
- StrandCertificate
- SegmentGroupHairStrand
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

## 25. 関連研究との差分説明用メモ

本研究は、以下の研究・技術と近い部分を持つ。

```txt
Super-Helices for Predicting the Dynamics of Natural Hair
https://doi.org/10.1145/1141911.1141929

Discrete Elastic Rods
https://doi.org/10.1145/1360612.1360662

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

Particle-Based Fluid Simulation for Interactive Applications
https://matthias-research.github.io/pages/publications/sca03.pdf
```

ただし、本研究ではこれらと同じことを新規性として主張しない。

既存研究は、髪を点列、segment、rod、guide/follower、clusterとして表現し、物理計算、LOD、描画、編集を行う。

本研究は、既存のsegment-based hair representationを前提とし、strand内部のSegment Groupに処理状態、certificate、cache validityを持たせ、Static reuse、Dynamic solve、Deferred Correction、Repair、Fallbackをgroup単位で切り替えるruntime hair data structureを提案する。

---

## 26. この研究の最終的な主張

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

## 27. 一文まとめ

本研究では、既存のsegment-based hair representationを基盤とし、髪strand内部のSegment Groupごとにcertificate、cache validity、runtime stateを持たせることで、Static再利用、Dynamic再計算、Deferred Correction、Repair、Fallbackを軽量query段階で選択し、false-static率とStatic/Dynamic境界誤差を計測しながら安全な部分再利用を行う、処理経路選択型の髪データ構造を実装する。
