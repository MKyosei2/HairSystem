# 固定60FPS制約下における静的・動的セグメントグループ型髪システム

## 点線点グループ型データ構造による部分再計算・局所編集・実時間髪再現

### 企画書兼実装設計書 完全版

---

## 0. 対象リポジトリ・ブランチ・バージョン

本設計書は、以下のリポジトリ・ブランチを対象とする。

```text
Repository : MKyosei2/HairSystem
Branch     : Novelty
Version    : 61.42
Commit     : 07d4ca8dc3ad9cf16fdb71d02b0619c5bed11a0b
対象ファイル : 設計書/01_TopNovelty_NewStrandDataType.md
```

本設計書の目的は、卒業研究における主要新規性である **静的・動的セグメントグループ型髪データ構造** を、実装可能なレベルまで具体化することである。

本研究は、単に髪を軽く計算する研究ではない。
また、単にActiveSet、LOD、GPU処理、Guide/Follower、PBD、XPBD、cache reuseを組み合わせる研究でもない。

本研究の中心は、髪の内部表現そのものを変えることである。

---

## 1. 研究テーマ

本研究のテーマは、次のように定義する。

```text
固定60FPS制約下における
静的・動的セグメントグループ型髪データ構造の研究
```

より具体的には、髪の毛一本を単なる点列として扱うのではなく、`点 - 線 - 点` の小さなグループに分割し、各グループごとに「再計算するか」「前回結果を再利用するか」「根元移動だけを反映するか」「境界だけ補正するか」を判断できるデータ型を設計・実装する。

このデータ型を用いることで、髪全体を毎フレーム完全に物理計算するのではなく、本当に動かす必要がある部分だけを動的に計算し、動かない部分は最後に確定した結果を再利用する。

---

## 2. 本研究で作るもの

本研究で作るものは、ゲームやリアルタイム編集環境で使用できる髪システムである。

ただし、中心となる成果物は単なる髪シミュレーション機能ではない。
本研究で作る中心成果物は、以下の新しい髪データ型である。

```text
静的・動的セグメントグループ型髪データ構造
```

通常の髪データは、次のような点列として表現される。

```text
p0 - p1 - p2 - p3 - p4 - ...
```

この方式では、髪が動くたびに多くの点を更新する必要がある。
髪の本数が増えるほど、点数も線分数も増え、物理計算、衝突判定、描画準備の負荷が大きくなる。

本研究では、この点列をそのまま毎フレーム処理するのではなく、次のような `点 - 線 - 点` のまとまりに分ける。

```text
[p0 - p1]
[p1 - p2]
[p2 - p3]
[p3 - p4]
...
```

この `[p0 - p1]` のようなまとまりを、本研究では **セグメントグループ** と呼ぶ。

各セグメントグループは、単なる幾何情報ではなく、実行時に以下のような状態を持つ。

```text
StaticCached       : 前回の結果をそのまま使う
StaticTransformed  : 形は変えず、根元移動だけ反映する
DynamicSolve       : 今フレームで計算する
BoundaryRepair     : 静的部分と動的部分の境界を補正する
Invalid            : キャッシュが危険なので再計算へ送る
```

これにより、髪全体を一律に処理するのではなく、髪一本の内部で静的な部分と動的な部分を混在させることができる。

---

## 3. 本研究のメインコア

本研究のメインコアは、次の一文で表せる。

```text
髪の毛を点列ではなく、点線点セグメントグループ列として保持し、
各グループを静的・動的に切り替えることで、
動かない部分は再計算せず、必要な部分だけを再計算する。
```

ここで重要なのは、髪を線分に分けること自体ではない。
重要なのは、`点 - 線 - 点` の各グループが、以下の情報を持つことである。

```text
現在位置
前回確定位置
方向ベクトル
長さ
半径
静的・動的状態
編集影響度
接触危険度
貫通危険度
境界誤差
キャッシュ有効性
衝突安全証明情報
```

つまり、本研究のセグメントグループは、単なる線分ではなく、**自分が今フレームにどう処理されるべきかを判断するための情報を持つ髪データ単位**である。

---

## 4. なぜこのデータ型が必要なのか

髪は、ゲームやリアルタイムCGにおいて非常に重い表現である。

髪は本数が多い。
一本一本が複数の点を持つ。
さらに、風、重力、頭の動き、体や服との接触、他の髪との干渉、編集操作など、多くの要因で形が変化する。

髪全体を毎フレーム高精度に計算すれば、見た目は良くなる。
しかし、少し古いPCやNintendo Switch級の環境では、髪だけに多くの処理時間を使うことはできない。

60FPSでは、1フレームに使える時間は約16.67msである。
しかし、その16.67msは髪だけの時間ではない。
ゲームには、キャラクター、背景、アニメーション、エフェクト、UI、サウンド、入力処理、ゲームロジックなどが存在する。

そのため、本研究では以下の考え方を取る。

```text
すべてを毎フレーム正確に計算しない。
見た目や安全性に影響する部分だけを計算する。
動いていない部分は、最後に動いたときの結果を再利用する。
根元移動だけなら、保存済み方向と長さで復元する。
編集・接触・貫通がある部分だけを動的に計算する。
```

これにより、ゲーム全体として固定60FPSを維持しながら、髪の見た目、編集性、接触補正を両立させることを目指す。

---

## 5. 既存技術との関係

本研究は、既存の髪シミュレーション研究やリアルタイム物理技術を否定するものではない。
むしろ、それらを前提として、その上に新しいデータ型を提案する。

既存技術として、以下のものがある。

```text
髪を点列・線分・ロッドとして扱う手法
PBD / XPBD による制約解法
Guide/Follower型の髪表現
LODによる計算量削減
GPUによる並列処理
sleeping / cache reuse / partial update
粒子・布・流体・剛体における部分更新
```

これらはすべて重要な既存技術である。

しかし、本研究の主役はそれらではない。

本研究の主役は、髪一本の内部にある `点 - 線 - 点` グループを、実行時の処理経路選択単位として扱う新しいデータ型である。

従来のLODやGuide/Followerでは、髪一本、髪束、guide、cluster、距離、画面上の重要度といった単位で処理量を変えることが多い。
一方、本研究では、髪一本の内部にある各セグメントグループごとに状態を持たせる。

たとえば、同じ髪一本の中でも、以下のような状態が同時に存在できる。

```text
根元付近     : StaticTransformed
中間部       : StaticCached
接触付近     : DynamicSolve
境界付近     : BoundaryRepair
毛先         : DynamicSolve
```

このように、髪一本の内部で静的部分と動的部分を混在させることが、本研究の特徴である。

---

## 6. データ型の基本構造

本研究で扱う基本単位は、`点 - 線 - 点` のセグメントグループである。

```text
SegmentGroup = start point + line segment + end point
```

ただし、実装上は点と線を単に持つだけでは不十分である。
各グループは、以下の情報を持つ。

```cpp
enum class SegmentGroupState : uint8_t
{
    StaticCached = 0,
    StaticTransformed = 1,
    DynamicSolve = 2,
    BoundaryRepair = 3,
    Invalid = 4
};

struct SegmentGroup
{
    uint32_t strandIndex;
    uint32_t groupIndex;

    uint32_t startPointIndex;
    uint32_t endPointIndex;

    Vec3 currentStart;
    Vec3 currentEnd;

    Vec3 cachedStart;
    Vec3 cachedEnd;

    Vec3 direction;
    float length;
    float radius;

    SegmentGroupState state;

    float editWeight;
    float contactRisk;
    float penetrationRisk;
    float boundaryError;
    float motionRisk;
    float visibilityWeight;

    uint32_t cacheValid;
    uint32_t collisionCertificateValid;
};
```

この構造体が、本研究の中心である。

`currentStart` と `currentEnd` は現在フレームの位置である。
`cachedStart` と `cachedEnd` は最後に確定した安全な位置である。
`direction` と `length` は、線分の方向と長さである。
`state` は、今フレームにこのグループをどう処理するかを表す。
`editWeight`、`contactRisk`、`penetrationRisk`、`boundaryError` などは、動的に計算すべきかどうかを判断するための値である。

---

## 7. 点・線・点の共有点の扱い

髪は連続した構造であるため、隣接するセグメントグループは点を共有する。

```text
Group 0 : [p0 - p1]
Group 1 : [p1 - p2]
Group 2 : [p2 - p3]
```

ここで、`p1` はGroup 0の終点であり、Group 1の始点でもある。

本研究では、この共有点を単純な一点として雑に扱わない。
共有点は、前後のグループの境界として扱う。

```text
前グループの終点
次グループの始点固定位置
```

次のグループを計算するときは、前のグループの最終位置を始点として使う。

```cpp
group[i + 1].currentStart = group[i].currentEnd;
```

これにより、髪の根元から毛先に向かって、順番に位置を復元できる。

共有点付近では、前の線分方向と次の線分方向が異なる場合がある。
そのため、本研究では共有点を境界として、前後の方向領域を分けて扱う。
考え方としては、共有点を中心に、前方向と後方向の半球領域を分ける。

```text
[p0 - p1] の終点側半球
[p1 - p2] の始点側半球
```

これにより、同じ共有点であっても、前グループの終点としての役割と、次グループの始点としての役割を分けることができる。

この考え方は、静的グループと動的グループが隣り合う場合に重要である。
境界点をどちらのグループがどのように扱うかを明確にすることで、静的部分と動的部分のつなぎ目の破綻を減らす。

---

## 8. グループ長の扱い

本研究では、髪一本の中では各セグメントグループの長さを基本的に同じにする。

```text
strand A:
[p0 - p1] = L
[p1 - p2] = L
[p2 - p3] = L

strand B:
[q0 - q1] = M
[q1 - q2] = M
[q2 - q3] = M
```

ただし、髪ごとに長さが違うことは許可する。
つまり、strand Aのグループ長とstrand Bのグループ長が違ってもよい。

この設計により、一本の髪の中では処理単位が均一になり、GPU上での並列処理やバッファ管理がしやすくなる。
また、方向ベクトルと長さを保存しておけば、根元から順番に復元しやすくなる。

---

## 9. StaticCached

`StaticCached` は、前回確定した結果をそのまま使う状態である。

使う条件は以下の通りである。

```text
編集されていない
接触しそうにない
貫通していない
境界が自然である
根元が大きく動いていない
前回キャッシュが有効である
```

処理内容は単純である。

```cpp
group.currentStart = group.cachedStart;
group.currentEnd   = group.cachedEnd;
```

この状態では、物理計算を行わない。
描画用データも再利用できる場合は、そのまま再利用する。

`StaticCached` を多く使えるほど、髪処理は軽くなる。
本研究では、静的再利用率を高く保つことが60FPS維持の重要な条件となる。

---

## 10. StaticTransformed

`StaticTransformed` は、髪の形は変えずに、根元や頭の移動だけを反映する状態である。

たとえば、キャラクターの頭が少し動いたが、髪の形そのものは大きく変わらない場合に使う。

この場合、保存済みの方向と長さを使って、始点から終点を復元する。

```text
次の点 = 今の点 + 保存済み方向 × 長さ
```

コードでは次のようになる。

```cpp
group.currentEnd = group.currentStart + group.direction * group.length;
```

髪一本全体を復元する場合は、根元から毛先に向かって順番に処理する。

```cpp
void ReconstructStrandFromRoot(
    const SegmentGroupStrand& strand,
    SegmentGroupBuffer& buffer)
{
    Vec3 current = strand.rootPosition;

    for (uint32_t i = 0; i < strand.groupCount; ++i)
    {
        SegmentGroup& group =
            buffer.groups[strand.firstGroupIndex + i];

        group.currentStart = current;
        group.currentEnd =
            current + group.direction * group.length;

        current = group.currentEnd;
    }
}
```

この処理により、髪全体を物理計算しなくても、根元移動に追従した位置を軽く復元できる。

---

## 11. DynamicSolve

`DynamicSolve` は、今フレームで実際に計算が必要な状態である。

以下のような場合に使う。

```text
髪が編集された
手や体に当たりそう
肩や服にめり込んでいる
急に動いた
前後のグループとのつながりが不自然
静的キャッシュが安全ではない
近距離で画面上目立つ
```

`DynamicSolve` では、以下の処理を行う。

```text
長さを保つ
根元側を固定する
曲がりすぎを抑える
接触していたら押し戻す
貫通していたら外へ出す
計算後の方向と長さを保存する
```

コードイメージは以下である。

```cpp
void SolveDynamicGroup(
    SegmentGroup& group,
    const ColliderSet& colliders,
    const DynamicSolveSettings& settings)
{
    SolveLengthConstraint(group);

    if (group.contactRisk > settings.contactThreshold ||
        group.penetrationRisk > settings.penetrationThreshold)
    {
        SolveCollisionForGroup(group, colliders, settings);
    }

    SolveBendApproximation(group, settings);

    Vec3 delta = group.currentEnd - group.currentStart;
    group.length = Length(delta);
    group.direction = Normalize(delta);

    group.cachedStart = group.currentStart;
    group.cachedEnd   = group.currentEnd;
    group.cacheValid  = 1;
}
```

重要なのは、全髪をDynamicSolveにしないことである。
DynamicSolveは高コストなので、必要なグループだけに限定する。

---

## 12. BoundaryRepair

`BoundaryRepair` は、静的グループと動的グループの境界を自然につなぐための状態である。

たとえば、以下のような状態を考える。

```text
Group 0 : StaticCached
Group 1 : DynamicSolve
```

この場合、Group 0は前回結果を使い、Group 1は今フレームで新しく計算される。
そのため、共有点や方向がずれ、境界に不自然な折れや段差が出る可能性がある。

そこで、境界付近だけを軽く補正する。

```cpp
void ApplyBoundaryRepair(
    SegmentGroup& prev,
    SegmentGroup& current)
{
    Vec3 joint =
        0.5f * (prev.currentEnd + current.currentStart);

    prev.currentEnd = joint;
    current.currentStart = joint;

    Vec3 blendedDir =
        Normalize(prev.direction + current.direction);

    current.currentEnd =
        current.currentStart + blendedDir * current.length;

    current.boundaryError =
        Length(prev.currentEnd - current.currentStart);
}
```

`BoundaryRepair` は高品質な物理計算ではない。
目的は、静的部分と動的部分の境界破綻を軽く直すことである。

---

## 13. Invalid

`Invalid` は、静的キャッシュを信じると危険な状態である。

たとえば、前回は安全だったが、今回フレームで衝突体が近づいた場合、前回のキャッシュをそのまま使うと髪が体や手を貫通する可能性がある。

以下のような場合にInvalidになる。

```text
キャッシュが古い
衝突体が近くに移動した
編集範囲に入った
隣接グループが大きく動いた
境界誤差が大きい
NaN / Infなど異常値がある
```

Invalidになったグループは、DynamicSolveまたはFallback処理へ送る。

---

## 14. 状態分類

毎フレーム、各セグメントグループに対して状態分類を行う。

判断順序は以下である。

```text
キャッシュが壊れているか
編集されたか
接触しそうか
貫通しているか
境界が不自然か
根元だけ動いたか
何もなければ前回結果を使う
```

コードイメージは以下である。

```cpp
SegmentGroupState ClassifySegmentGroup(
    const SegmentGroup& group,
    const RuntimeContext& context)
{
    if (!group.cacheValid)
    {
        return SegmentGroupState::Invalid;
    }

    if (group.editWeight > context.editThreshold)
    {
        return SegmentGroupState::DynamicSolve;
    }

    if (group.contactRisk > context.contactThreshold)
    {
        return SegmentGroupState::DynamicSolve;
    }

    if (group.penetrationRisk > context.penetrationThreshold)
    {
        return SegmentGroupState::DynamicSolve;
    }

    if (group.boundaryError > context.boundaryThreshold)
    {
        return SegmentGroupState::BoundaryRepair;
    }

    if (context.rootMoved)
    {
        return SegmentGroupState::StaticTransformed;
    }

    return SegmentGroupState::StaticCached;
}
```

この分類が、毎フレームの処理量を決める。

---

## 15. 危険度スコア

DynamicSolve候補が多い場合でも、すべてを計算してはいけない。
処理時間を超えてしまうからである。

そこで、各グループに危険度スコアを付ける。

危険度スコアは、以下の要素から計算する。

```text
編集されたか
接触しそうか
貫通しているか
境界が不自然か
急に動いたか
画面上で目立つか
```

コードイメージは以下である。

```cpp
float ComputeSegmentGroupRisk(const SegmentGroup& group)
{
    return
        0.30f * group.editWeight +
        0.20f * group.contactRisk +
        0.25f * group.penetrationRisk +
        0.15f * group.boundaryError +
        0.05f * group.motionRisk +
        0.05f * group.visibilityWeight;
}
```

危険度が高いグループから順に、時間予算内でDynamicSolveする。

---

## 16. 時間予算

60FPSでは、1フレームは約16.67msである。

しかし、髪だけで16.67msを使うことはできない。
本研究では、少し古いPCやSwitch級の環境を想定し、髪処理全体の目標を以下のようにする。

```text
ゲーム全体                    : 16.67ms以内
髪ランタイム全体              : 2.0〜3.5ms以内
CPU側の髪処理                 : 0.3〜0.7ms以内
GPU側の髪更新・描画準備       : 1.5〜2.8ms以内
動的に計算するグループ率       : 全体の5〜15%以内
静的に再利用するグループ率     : 85%以上
followerへの物理計算          : 原則禁止
全髪への自己衝突判定           : 禁止
遠距離の髪                    : cardsまたは静的表示
不可視の髪                    : freeze
```

髪処理時間は、次のように考える。

```text
T = C + dK
```

```text
T : 髪処理全体の時間
C : 必ず発生する固定処理時間
d : DynamicSolve 1グループあたりの時間
K : DynamicSolveするグループ数
```

髪に使える時間を `B` とすると、

```text
C + dK <= B
```

を満たす必要がある。

つまり、

```text
K <= (B - C) / d
```

である。

この式を使い、今フレームでDynamicSolveしてよいグループ数を決める。

---

## 17. 予算超過時の品質低下ルール

髪処理が重くなりそうな場合は、品質を少し落としてでも60FPSを守る。

品質を落とす順番は以下とする。

```text
1. 見えていない髪を完全に止める
2. 遠い髪をStaticCachedに固定する
3. 遠い髪をhair cards表示にする
4. 中距離のfollower更新頻度を下げる
5. 中距離のDynamicSolveを禁止する
6. 近距離以外の接触判定をやめる
7. BoundaryRepairを次フレームへ回す
8. DynamicSolveする数を減らす
9. tube描画をribbon描画に落とす
10. ribbon描画をcards描画に落とす
```

ただし、以下は優先して処理する。

```text
近距離で大きく見える貫通
ユーザが編集中の部分
顔周りの髪
シルエットに大きく影響する髪
```

本研究では、遠くの髪の品質よりも、60FPS維持と近距離の破綻防止を優先する。

---

## 18. 毎フレームの処理全体

最終的な毎フレーム更新処理は、以下のようになる。

```cpp
void UpdateSegmentGroupHairRuntime(
    SegmentGroupBuffer& groups,
    std::vector<SegmentGroupStrand>& strands,
    const RuntimeInput& input,
    RuntimeOutput& output)
{
    UpdateRootPositions(strands, input);
    ApplyEditWeights(groups, input);
    UpdateVisibilityAndLod(groups, strands, input);
    UpdateContactAndPenetrationRisk(groups, input);

    std::vector<uint32_t> dynamicCandidates;
    std::vector<uint32_t> repairCandidates;

    for (uint32_t i = 0; i < groups.groups.size(); ++i)
    {
        SegmentGroup& group = groups.groups[i];

        group.state = ClassifySegmentGroup(group, input.context);

        if (group.state == SegmentGroupState::DynamicSolve ||
            group.state == SegmentGroupState::Invalid)
        {
            dynamicCandidates.push_back(i);
        }
        else if (group.state == SegmentGroupState::BoundaryRepair)
        {
            repairCandidates.push_back(i);
        }
    }

    SortByRisk(dynamicCandidates, groups);

    uint32_t maxDynamicCount =
        ComputeDynamicGroupBudget(input.hairBudgetMs);

    ApplyStaticGroups(groups, strands);
    SolveDynamicGroups(groups, dynamicCandidates, maxDynamicCount, input);
    ApplyBoundaryRepair(groups, repairCandidates);
    ValidateStaticGroups(groups, input);
    ReconstructFollowers(groups, strands, input);
    BuildRenderData(groups, strands, input);
    FillTelemetry(groups, output.telemetry);
}
```

この関数が、実装上の中心になる。

---

## 19. Guide / Followerとの関係

本研究では、すべての髪を直接物理計算しない。
計算する髪と、見た目を増やす髪を分ける。

```text
guide    : 計算する代表髪
follower : guideについてくる表示用髪
```

本設計では、原則としてguideだけをDynamicSolve対象にする。
followerには重い物理計算を行わない。

followerは、guideの位置、方向、束方向、太さ、カール情報などを使って再構成する。

```text
guideのSegmentGroup結果
+ followerの相対オフセット
+ clump情報
+ curl情報
+ width情報
= followerの描画位置
```

これにより、見た目の髪密度を保ちながら、物理計算対象を抑える。

---

## 20. LODと表示方針

距離や見え方に応じて、髪の更新と描画を切り替える。

| 状態     | 更新                  | 描画                |
| ------ | ------------------- | ----------------- |
| Near   | DynamicSolveを許可     | ribbon / 少数tube   |
| Mid    | StaticTransformed中心 | simplified ribbon |
| Far    | StaticCached中心      | hair cards        |
| Hidden | freeze              | 描画なし、またはcards     |

全髪をtubeで描画するのは重い。
少し古いPCやSwitch級では避ける。

近距離ではribbonを基本とし、必要な部分だけtube表現を使う。
中距離では簡略化ribbonを使う。
遠距離ではhair cardsを使う。
不可視の場合は更新しない。

---

## 21. 衝突判定と安全確認

静的グループは便利だが、誤って再利用すると破綻する。

たとえば、本当は手に当たっているのに、前回の結果を使い続けると、髪が手を突き抜ける可能性がある。

そのため、静的グループには安全確認を入れる。

確認する内容は以下である。

```text
前回の非衝突情報はまだ有効か
近くの衝突体が動いていないか
編集されていないか
隣のグループが大きく動いていないか
境界誤差が大きくないか
```

この確認に失敗した場合、そのグループはInvalidまたはDynamicSolveにする。

---

## 22. Static Watchdog

Static Watchdogは、静的グループを監視する安全装置である。

役割は以下である。

```text
静的のまま使ってよいか確認する
静的キャッシュによる破綻を見つける
false staticを数える
必要ならDynamicSolveへ戻す
```

false staticとは、静的として扱ったが、本当は動的に計算すべきだった状態である。

本研究では、false staticを重要な評価項目にする。
false staticが多い場合、静的再利用によって見た目や物理挙動が破綻している可能性が高い。

---

## 23. 評価項目

本研究では、単にFPSだけを評価しない。
以下の観点で評価する。

### 23.1 実時間性

```text
fps
frame_ms
simulation_ms
gpu_hair_ms
vram_mb
bandwidth_mb_frame
```

60FPSを維持できているか、髪処理が2.0〜3.5ms以内に収まっているかを見る。

### 23.2 セグメントグループ状態

```text
total_segment_group_count
static_cached_group_count
static_transformed_group_count
dynamic_group_count
deferred_correction_group_count
repair_required_group_count
fallback_group_count
```

各フレームで、どの程度のグループが静的再利用され、どの程度が動的計算されたかを見る。

### 23.3 静的再利用の安全性

```text
false_static_count
false_static_rate
cache_hit_count
cache_miss_count
cache_invalidation_count
watchdog_check_count
watchdog_repair_count
```

静的キャッシュが安全に使えているかを見る。

### 23.4 境界の自然さ

```text
static_dynamic_boundary_mean_error_mm
static_dynamic_boundary_max_error_mm
boundaryNaturalnessScore
```

静的グループと動的グループの境界が不自然になっていないかを見る。

### 23.5 メモリ効率

```text
direction_cache_memory_mb
full_position_memory_mb
direction_cache_memory_saving_rate
```

全点位置を毎回保持する場合と比較し、方向ベクトルと長さによる保持でどの程度メモリを削減できたかを見る。

### 23.6 編集品質

```text
rmsPositionError
silhouetteError
clumpPreservationScore
rootLocalityScore
editLocalityScore
editedStrandRatio
editToRunLatencyMilliseconds
```

編集した部分だけが自然に変化し、編集していない部分が保たれているかを見る。

---

## 24. Telemetry出力

毎フレーム、SegmentGroupの状態を集計し、既存のCSV、JSON、SQLite DB、runtime suiteへ出力する。

コードイメージは以下である。

```cpp
void FillSegmentGroupTelemetry(
    const SegmentGroupBuffer& groups,
    SegmentGroupTelemetryFrame& telemetry)
{
    telemetry.totalSegmentGroupCount = groups.groups.size();

    for (const SegmentGroup& group : groups.groups)
    {
        switch (group.state)
        {
        case SegmentGroupState::StaticCached:
            telemetry.staticCachedGroupCount++;
            telemetry.cacheHitCount++;
            break;

        case SegmentGroupState::StaticTransformed:
            telemetry.staticTransformedGroupCount++;
            telemetry.cacheHitCount++;
            break;

        case SegmentGroupState::DynamicSolve:
            telemetry.dynamicGroupCount++;
            telemetry.cacheMissCount++;
            break;

        case SegmentGroupState::BoundaryRepair:
            telemetry.repairRequiredGroupCount++;
            break;

        case SegmentGroupState::Invalid:
            telemetry.fallbackGroupCount++;
            telemetry.cacheInvalidationCount++;
            break;
        }

        telemetry.staticDynamicBoundaryMaxErrorMm =
            max(telemetry.staticDynamicBoundaryMaxErrorMm,
                group.boundaryError * 1000.0f);
    }

    telemetry.falseStaticRate =
        telemetry.totalSegmentGroupCount > 0
            ? float(telemetry.falseStaticCount) /
              float(telemetry.totalSegmentGroupCount)
            : 0.0f;
}
```

新しい評価システムを一から作るのではなく、既存の評価基盤へ接続する。

---

## 25. 必要なコードファイル

実装では、以下のファイル構成にする。

```text
include/hairgpu/SegmentGroupHair.h
src/hairgpu_core/SegmentGroupHair.cpp

include/hairgpu/SegmentGroupRuntime.h
src/hairgpu_core/SegmentGroupRuntime.cpp

include/hairgpu/SegmentGroupClassifier.h
src/hairgpu_core/SegmentGroupClassifier.cpp

include/hairgpu/SegmentGroupSolver.h
src/hairgpu_core/SegmentGroupSolver.cpp

include/hairgpu/SegmentGroupBoundaryRepair.h
src/hairgpu_core/SegmentGroupBoundaryRepair.cpp

include/hairgpu/SegmentGroupCollisionCertificate.h
src/hairgpu_core/SegmentGroupCollisionCertificate.cpp

include/hairgpu/SegmentGroupFollowerReconstruction.h
src/hairgpu_core/SegmentGroupFollowerReconstruction.cpp

include/hairgpu/SegmentGroupTelemetry.h
src/hairgpu_core/SegmentGroupTelemetry.cpp
```

---

## 26. 最終的な中心クラス

完成形では、以下のクラスを中心にする。

```cpp
class SegmentGroupHairRuntime
{
public:
    bool InitializeFromHairAsset(const HairAsset& asset);

    void Update(
        const SegmentGroupRuntimeInput& input,
        SegmentGroupRuntimeOutput& output);

    const SegmentGroupBuffer& GetGroups() const;
    const std::vector<SegmentGroupStrand>& GetStrands() const;

private:
    void UpdateRootTransforms(const SegmentGroupRuntimeInput& input);
    void ApplyEditWeights(const SegmentGroupRuntimeInput& input);
    void UpdateLodAndVisibility(const SegmentGroupRuntimeInput& input);
    void UpdateRiskScores(const SegmentGroupRuntimeInput& input);

    void ClassifyGroups();
    void SelectDynamicGroupsUnderBudget();
    void ApplyStaticGroups();
    void SolveDynamicGroups();
    void ApplyBoundaryRepair();
    void ValidateStaticCertificates();
    void ReconstructFollowers();
    void BuildRenderData();
    void FillTelemetry(SegmentGroupRuntimeOutput& output);

private:
    SegmentGroupBuffer m_groups;
    std::vector<SegmentGroupStrand> m_strands;

    std::vector<uint32_t> m_dynamicCandidates;
    std::vector<uint32_t> m_repairCandidates;
    std::vector<uint32_t> m_deferredCandidates;

    SegmentGroupRuntimeContext m_context;
    SegmentGroupTelemetryFrame m_telemetry;
};
```

このクラスが、01設計の中心になる。

---

## 27. GPU実装方針

GPU実装では、以下のようなpass構成を想定する。

```text
SegmentGroupBuildCS
SegmentGroupCertificateQueryCS
SegmentGroupStateClassifyCS
StaticCacheReuseCS
StaticTransformCS
DynamicGroupSolveCS
BoundaryRepairCS
ApplyGroupResultCS
FollowerReconstructionCS
RenderBufferBuildCS
TelemetryReductionCS
```

ただし、GPU multi-pass自体が研究の主役ではない。
GPUは、本研究の新しいデータ型を60FPSで動かすための実装基盤である。

重要なのは、CPU側とGPU側でSegmentGroupの状態判断が一致することである。

```text
CPU判定結果 == GPU判定結果
```

これが一致しないと、評価結果や再現実験の信頼性が下がる。

---

## 28. 実装時に守るルール

実装時には、以下を必ず守る。

```text
01の中心はSegmentGroup型データ構造である
ActiveSetはSegmentGroupを選ぶための補助である
StaticCachedを積極的に使う
根元移動のみならStaticTransformedで済ませる
編集・接触・貫通・境界誤差がある部分だけDynamicSolveにする
DynamicSolve候補が多い場合は危険度順に処理する
予算を超えた候補はDeferredCorrectionまたはBoundaryRepairへ回す
StaticとDynamicの境界にはBoundaryRepairを入れる
followerには物理計算をしない
LODとfreezeを前提にする
評価は既存のruntime suiteとmetric definitionsをそのまま使う
新しい評価システムを勝手に作らない
```

---

## 29. 失敗条件

以下の状態になった場合、設計または実装を見直す。

```text
DynamicSolve率が常に30%以上になる
StaticCached / StaticTransformed率が70%未満になる
falseStaticRateが高い
staticDynamicBoundaryErrorMmが大きい
髪処理が3.5msを超える
follower再構成が重い
遠距離髪がDynamicSolveされ続ける
不可視髪が更新され続ける
collision判定が全髪対象になっている
全髪self-collisionが走っている
全髪tube描画をしている
```

特に、少し古いPCやSwitch級で60FPSを狙う場合、全髪を毎フレーム再計算する設計になった時点で失敗である。

---

## 30. 成功条件

本研究が成功したと言える条件は以下である。

```text
髪処理が2.0〜3.5ms以内に収まる
ゲーム全体で60FPSを維持できる
DynamicSolve率が5〜15%程度に収まる
StaticCached / StaticTransformed率が85%以上になる
falseStaticRateが低い
静的・動的境界の破綻が目立たない
編集した部分だけが自然に変化する
編集していない部分が保たれる
followerを使って見た目密度を維持できる
遠距離や不可視髪を軽量化できる
```

---

## 31. 本研究で最終的に示したいこと

本研究で最終的に示したいことは、次の通りである。

髪全体を毎フレーム高精度に物理計算しなくても、髪を `点 - 線 - 点` のセグメントグループ列として扱い、各グループを静的・動的に切り替えることで、必要な部分だけを計算できる。

静的グループは、最後に確定した結果を再利用する。
根元が動いた場合は、保存済み方向と長さで軽く復元する。
編集や接触がある部分だけをDynamicSolveする。
静的部分と動的部分の境界はBoundaryRepairでつなぐ。

この仕組みにより、少し古いPCやNintendo Switch級の環境でも、ゲーム全体として60FPSを維持しながら、髪の部分編集、接触補正、見た目密度の維持を目指す。

---

## 32. まとめ

本研究の肝は、ActiveSet補正そのものではない。
本研究の肝は、ActiveSet補正や部分再計算を可能にするための **静的・動的セグメントグループ型髪データ構造** である。

このデータ構造では、髪一本を点列ではなく、`点 - 線 - 点` のグループ列として扱う。
各グループは、StaticCached、StaticTransformed、DynamicSolve、BoundaryRepair、Invalidの状態を持つ。

動かない部分は再計算しない。
根元移動だけなら軽く復元する。
編集、接触、貫通、急な動きがある部分だけを計算する。
境界だけが不自然な場合は軽く補正する。

評価では、特に以下を重視する。

```text
falseStaticRate
groupStaticReuseRate
groupDynamicRate
staticDynamicBoundaryErrorMm
directionCacheMemorySavingRate
gpu_hair_ms
fps
```

この方式により、髪全体を毎フレーム重く計算するのではなく、必要な部分だけを更新する髪システムを作る。
その結果として、固定60FPS、低性能環境、部分編集、接触補正、ゲーム内利用を同時に成立させることを目指す。
