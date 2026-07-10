# 固定60FPS制約下における

# 静的・動的セグメントグループ型髪システム

## 点線点グループ型データ構造による部分再計算・局所編集・実時間髪再現

### 企画書兼実装設計書 完全版

---

## 0. この設計書について

この設計書は、論文にそのまま載せる文章ではない。
この設計書の目的は、これを読んだ人が「何を作ればよいのか」「どのようなコードを書けばよいのか」「どのように評価すればよいのか」を理解し、すぐに実装に入れるようにすることである。

そのため、専門的な研究説明よりも、実装に必要な考え方、データ構造、処理の流れ、クラス構成、評価方法を中心に説明する。

本研究の中心は、髪の毛を毎フレームすべて計算するのではなく、髪を小さなまとまりに分け、そのまとまりごとに「動かすか」「前回の結果をそのまま使うか」を切り替えることである。

ここで使う小さなまとまりを、本設計では **セグメントグループ** と呼ぶ。

---

## 1. 何を作るのか

本研究で作るものは、ゲームなどで使えるリアルタイム髪システムである。

ただし、単に髪を揺らすだけのシステムではない。
本研究で作るものは、髪の内部データの持ち方を変えるシステムである。

普通の髪システムでは、髪の毛一本をたくさんの点の列として持つ。

```text
p0 - p1 - p2 - p3 - p4 - ...
```

この場合、髪が動くたびに多くの点を更新する必要がある。
髪の本数が増えるほど、計算量も増える。

本研究では、この点列をそのまま扱うのではなく、次のように `点-線-点` のまとまりに分ける。

```text
[p0 - p1]
[p1 - p2]
[p2 - p3]
[p3 - p4]
...
```

この `[p0 - p1]` のようなまとまりを、**セグメントグループ** と呼ぶ。

そして、各セグメントグループごとに、

```text
このグループは動かす必要がある
このグループは前回の結果を使えばよい
このグループは根元の移動だけ反映すればよい
このグループは隣とのつなぎ目だけ直せばよい
```

という判断を行う。

これにより、髪全体を毎フレーム全部計算するのではなく、本当に必要な部分だけを計算できる。

---

## 2. 本研究の一番大事な考え方

本研究の一番大事な考え方は、次の一文で表せる。

> 髪の毛を点列ではなく、点線点セグメントグループ列として持ち、各グループを静的・動的に切り替えることで、動かない部分は再計算せず、必要な部分だけを再計算する。

ここでいう **静的** とは、「今フレームでは新しく計算しなくてよい」という意味である。
完全に止まっているという意味ではない。

たとえば、キャラクターの頭が少し動いた場合でも、髪の形そのものが変わらないなら、前回保存した髪の方向や長さを使って、新しい位置だけを軽く復元できる。
このような場合も、本研究では静的に近い扱いにする。

反対に、手に当たった、肩にめり込んだ、髪を編集した、急に動いた、つなぎ目が不自然になった、という場合は、その部分だけ動的に計算する。

---

## 3. なぜこの方式が必要なのか

髪はとても重い表現である。
一本一本が細く、本数が多く、さらに曲がったり、揺れたり、体や服に当たったりする。

髪全体を毎フレーム高精度に計算すれば、見た目は良くなる。
しかし、それをゲーム内で毎フレーム行うと重すぎる。

特に、Nintendo Switch級や少し古いPCのような環境では、髪だけに多くの時間を使うことはできない。
ゲームには髪以外にも、キャラクター、背景、エフェクト、UI、サウンド、ゲーム処理がある。

そのため、本研究では次の方針を取る。

```text
全部を正確に計算するのではなく、
見た目に影響が大きい部分だけを計算する。

動いていない部分は、
最後に動いたときの結果を再利用する。

遠くにある髪や見えない髪は、
できるだけ更新しない。
```

これにより、ゲーム全体として60FPSを維持することを目指す。

---

## 4. 目標

本システムの目標は、次の通りである。

### 4.1 機能面の目標

本システムでは、以下を実現する。

```text
髪を点線点セグメントグループとして管理する
各グループを静的または動的に切り替える
静的グループは前回の結果を再利用する
根元だけ動いた場合は、保存済み方向と長さで復元する
編集された部分だけを優先的に更新する
接触や貫通が起きそうな部分だけを補正する
静的部分と動的部分の境界を自然につなぐ
guideを中心に計算し、followerはguideから再構成する
既存の評価システムにそのまま接続する
```

### 4.2 性能面の目標

ゲーム全体は60FPSで動かす。
60FPSでは、1フレームに使える時間は約16.67msである。

ただし、髪だけが16.67msを使ってはいけない。
ゲーム全体の中で髪に使える時間は、かなり小さく見積もる必要がある。

本設計では、低性能環境向けに次を目標にする。

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

---

## 5. 用語説明

この設計書で使う言葉を先に整理する。

### 5.1 点

髪の毛を形作る位置である。
3D空間上の `x, y, z` 座標を持つ。

```text
p0, p1, p2, ...
```

### 5.2 線

点と点の間をつなぐ部分である。
実際には、始点と終点、方向、長さを持つ。

### 5.3 セグメントグループ

`点-線-点` のまとまりである。

```text
[p0 - p1]
```

本研究では、このセグメントグループを髪の最小更新単位にする。
つまり、点一個ずつではなく、点と線を含む小さなグループ単位で「動かす」「再利用する」を判断する。

### 5.4 静的グループ

今フレームでは新しく計算しなくてよいグループである。
前回の結果をそのまま使うか、根元の移動だけ反映して軽く復元する。

### 5.5 動的グループ

今フレームで計算が必要なグループである。
編集、接触、貫通、急な動きなどがある場合に動的になる。

### 5.6 境界補正

静的グループと動的グループのつなぎ目を自然につなぐ処理である。
静的な部分と動的な部分が混ざると、境目に段差が出る可能性がある。
それを軽く直す処理を境界補正と呼ぶ。

### 5.7 guide

計算の中心になる髪である。
すべての髪を直接計算すると重いため、代表となる髪だけを計算する。

### 5.8 follower

guideについてくる表示用の髪である。
followerは見た目の密度を増やすために使う。
原則として、followerには重い物理計算をしない。

---

## 6. システム全体の流れ

毎フレーム、システムは次の順番で動く。

```text
1. キャラクターや頭の位置を更新する
2. 髪の根元位置を更新する
3. ユーザ編集やアニメーションの影響を反映する
4. 各セグメントグループの危険度を計算する
5. 各グループを静的・動的・境界補正に分類する
6. 処理時間の予算内で、動的に計算するグループ数を決める
7. 静的グループは前回結果を再利用する
8. 根元移動だけのグループは保存済み方向と長さで復元する
9. 動的グループだけを計算する
10. 静的と動的の境界を補正する
11. guideの結果からfollowerを再構成する
12. 距離や見え方に応じて描画方法を選ぶ
13. 既存の評価システムへログを出す
```

この流れの中心は、4〜10である。
つまり、各セグメントグループをどう分類し、どこだけ計算するかが重要になる。

---

## 7. セグメントグループ型データ構造

### 7.1 基本構造

各セグメントグループは、次の情報を持つ。

```text
どの髪に属するか
その髪の中で何番目のグループか
始点
終点
方向
長さ
太さ
現在の始点と終点
前回確定した始点と終点
今の状態
編集の影響度
接触の危険度
貫通の危険度
境界の誤差
キャッシュが使えるか
前回の非衝突判定がまだ有効か
```

### 7.2 C++でのデータ構造イメージ

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

この構造体が、本研究の中心になる。

---

## 8. 状態の種類

各セグメントグループは、毎フレーム次のどれかの状態になる。

### 8.1 StaticCached

前回の結果をそのまま使う状態である。

使う条件は次の通り。

```text
編集されていない
接触しそうにない
貫通していない
周囲の髪とのつながりが自然
根元も大きく動いていない
前回のキャッシュが有効
```

処理内容は単純である。

```cpp
currentStart = cachedStart;
currentEnd   = cachedEnd;
```

この状態では、物理計算をしない。
描画用のデータも再利用できるなら、そのまま使う。

### 8.2 StaticTransformed

形は変えずに、根元や頭の移動だけ反映する状態である。

たとえば、キャラクターが少し動いたが、髪の形自体は変わっていない場合に使う。

保存済みの方向と長さを使い、始点から終点を復元する。

```text
次の点 = 今の点 + 保存済み方向 × 長さ
```

コードでは次のようになる。

```cpp
currentEnd = currentStart + direction * length;
```

これにより、重い物理計算をせずに髪の位置を更新できる。

### 8.3 DynamicSolve

今フレームで計算が必要な状態である。

次のような場合に使う。

```text
髪が編集された
手や体に当たりそう
肩や服にめり込んでいる
急に動いた
前後のグループとのつながりが不自然
静的キャッシュが安全ではない
近距離で画面上目立つ
```

この状態では、長さ保持、曲げ補正、接触補正、貫通解消などを行う。

ただし、全髪ではなく、この状態になったグループだけを計算する。

### 8.4 BoundaryRepair

静的グループと動的グループの境界を直す状態である。

たとえば、前のグループはStaticCachedで、次のグループはDynamicSolveだった場合、境目が少しずれる可能性がある。

そのとき、接続点や方向を軽く補正して、自然につなぐ。

これは高品質な物理計算ではなく、軽い補正でよい。

### 8.5 Invalid

静的キャッシュが使えない状態である。

たとえば、前回は安全だったが、今回は近くに衝突体が来た場合、キャッシュを信じると危険である。
その場合はInvalidにして、DynamicSolveへ送る。

---

## 9. 共有点の扱い

髪のグループは、隣同士で点を共有する。

```text
Group 0: [p0 - p1]
Group 1: [p1 - p2]
```

ここで `p1` は、Group 0の終点であり、Group 1の始点でもある。

本研究では、この共有点をただの一つの点として雑に扱わない。
次のように役割を分ける。

```text
前グループの終点
次グループの始点固定位置
```

つまり、次のグループを計算するときは、前のグループの終点を始点として使う。

```cpp
group[i + 1].currentStart = group[i].currentEnd;
```

これにより、根元から毛先に向かって、順番に髪の位置を復元できる。

---

## 10. 髪一本を復元する処理

髪一本が全体的に動く場合、根元から順番に計算する。

```cpp
void ReconstructStrandFromRoot(
    const SegmentGroupStrand& strand,
    SegmentGroupBuffer& buffer)
{
    Vec3 current = strand.rootPosition;

    for (uint32_t i = 0; i < strand.groupCount; ++i)
    {
        SegmentGroup& group = buffer.groups[strand.firstGroupIndex + i];

        group.currentStart = current;
        group.currentEnd = current + group.direction * group.length;

        current = group.currentEnd;
    }
}
```

この処理は、本研究で非常に重要である。

なぜなら、髪全体を物理計算しなくても、保存済みの方向と長さがあれば、根元から順番に位置を復元できるからである。

---

## 11. 状態分類の考え方

毎フレーム、すべてのセグメントグループに対して、どの状態にするかを決める。

わかりやすく言うと、次の順番で考える。

```text
キャッシュが壊れているか？
編集されたか？
接触しそうか？
貫通しているか？
境界が不自然か？
根元だけ動いたか？
何もなければ前回結果を使う
```

コードのイメージは次の通り。

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

## 12. 危険度スコア

DynamicSolveが必要そうなグループが多い場合でも、すべてを計算してはいけない。
処理時間を超えてしまうからである。

そこで、各グループに危険度スコアをつける。

危険度スコアとは、簡単に言うと「どれくらい今すぐ直すべきか」を表す数字である。

危険度は、次の要素から決める。

```text
編集されたか
接触しそうか
貫通しているか
境界が不自然か
急に動いたか
画面上で目立つか
```

式のイメージは次の通り。

```text
危険度 =
  編集の影響
+ 接触の危険
+ 貫通の危険
+ 境界の不自然さ
+ 動きの大きさ
+ 画面上の目立ちやすさ
```

コードでは次のように書ける。

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

危険度が高いものから順に、時間予算内でDynamicSolveする。

---

## 13. 時間予算の考え方

ゲームでは、処理時間を守ることが非常に重要である。
60FPSでは、1フレームは約16.67msである。

しかし、髪だけで16.67msを使うことはできない。
そのため、髪には2.0〜3.5ms程度の予算を設定する。

髪処理の時間を、次のように考える。

```text
髪処理時間 = 固定処理時間 + 動的グループ1個あたりの時間 × 動的グループ数
```

記号で書くと、

```text
T = C + dK
```

である。

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

この式を使えば、今フレームで何個までDynamicSolveしてよいかを決められる。

---

## 14. 予算を超えそうなときのルール

髪処理が重くなりそうな場合は、品質を少し落としてでも60FPSを守る。

落とす順番は次の通り。

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

ただし、近距離で大きく見える貫通や、ユーザが編集中の部分は優先して処理する。

---

## 15. 毎フレームの処理全体

実装では、最終的に次のような更新関数を作る。

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

この関数が、システム全体の中心になる。

---

## 16. 必要なコードファイル

実装では、次のようなファイル構成にするとよい。

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

## 17. 各コードの役割

### 17.1 SegmentGroupHair

髪を点列からセグメントグループ列へ変換する。

主な役割は次の通り。

```text
髪一本を複数のSegmentGroupに分ける
各グループの始点と終点を決める
方向と長さを計算する
guideかfollowerかを記録する
初期状態をStaticCachedにする
```

### 17.2 SegmentGroupRuntime

毎フレームの処理全体を管理する。

主な役割は次の通り。

```text
根元位置を更新する
編集情報を反映する
危険度を計算する
状態を分類する
予算内でDynamicSolve対象を決める
静的グループを再利用する
動的グループだけ解く
境界補正を行う
followerを再構成する
評価用ログを出す
```

### 17.3 SegmentGroupClassifier

各グループの状態を決める。

```text
StaticCachedにするか
StaticTransformedにするか
DynamicSolveにするか
BoundaryRepairにするか
Invalidにするか
```

を判断する。

### 17.4 SegmentGroupSolver

DynamicSolveになったグループだけを計算する。

行う処理は次の通り。

```text
長さを保つ
根元側を固定する
曲がりすぎを抑える
衝突を直す
貫通を直す
計算後の方向と長さを保存する
```

### 17.5 SegmentGroupBoundaryRepair

静的グループと動的グループの境目を自然につなぐ。

行う処理は次の通り。

```text
接続点を合わせる
方向をなだらかにする
曲率の急変を抑える
境界誤差を記録する
```

### 17.6 SegmentGroupCollisionCertificate

静的グループを再利用してよいかを判断するための情報を管理する。

「前回は衝突していなかった」という情報が、今回も信じられるかを確認する。

### 17.7 SegmentGroupFollowerReconstruction

guideからfollowerを再構成する。

followerは見た目の密度を増やすための髪である。
物理計算は行わず、guideの位置や方向をもとに生成する。

### 17.8 SegmentGroupTelemetry

既存の評価システムへ値を出す。

新しい評価システムを作り直すのではなく、既存のCSV、JSON、SQLite DB、runtime suiteにそのまま値を流す。

---

## 18. DynamicSolveの中身

DynamicSolveでは、重すぎる物理計算は避ける。
Switch級の環境でも動かすため、最低限の補正に絞る。

行う処理は次の通り。

```text
長さを保つ
根元とのつながりを保つ
曲がりすぎを少し直す
接触していたら押し戻す
貫通していたら外へ出す
計算後の結果をキャッシュする
```

コードのイメージは次の通り。

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

    group.direction = Normalize(group.currentEnd - group.currentStart);
    group.length = Length(group.currentEnd - group.currentStart);

    group.cachedStart = group.currentStart;
    group.cachedEnd = group.currentEnd;
    group.cacheValid = 1;
}
```

---

## 19. 境界補正の中身

静的な部分と動的な部分が隣り合うと、境目にズレが出ることがある。

たとえば、

```text
静的グループ: 前回の形を使っている
動的グループ: 今回新しく計算された
```

となると、接続点がずれる可能性がある。

そのため、境界補正では次を行う。

```text
二つのグループの接続点を合わせる
方向をなだらかにつなぐ
曲がり方が急に変わらないようにする
```

コードのイメージは次の通り。

```cpp
void ApplyBoundaryRepair(
    SegmentGroup& prev,
    SegmentGroup& current)
{
    Vec3 joint = 0.5f * (prev.currentEnd + current.currentStart);

    prev.currentEnd = joint;
    current.currentStart = joint;

    Vec3 blendedDir = Normalize(prev.direction + current.direction);
    current.currentEnd = current.currentStart + blendedDir * current.length;

    current.boundaryError = Length(prev.currentEnd - current.currentStart);
}
```

---

## 20. 静的キャッシュの安全確認

静的グループは便利だが、間違って使うと見た目が破綻する。

たとえば、本当は手に当たっているのに、前回の結果をそのまま使うと、髪が手を突き抜ける可能性がある。

そこで、静的グループには安全確認を入れる。

確認する内容は次の通り。

```text
前回の非衝突情報はまだ有効か
近くの衝突体が動いていないか
編集されていないか
隣のグループが大きく動いていないか
境界誤差が大きくないか
```

この確認に失敗した場合、そのグループはInvalidまたはDynamicSolveにする。

---

## 21. Static Watchdog

Static Watchdogは、静的グループを監視する安全装置である。

役割は次の通り。

```text
静的のまま使ってよいか確認する
静的キャッシュによる破綻を見つける
false staticを数える
必要ならDynamicSolveへ戻す
```

false staticとは、静的として扱ったが、本当は動的に計算すべきだった状態である。

これは非常に重要な評価項目である。
false staticが多いと、髪が破綻している可能性が高い。

---

## 22. guide / followerの扱い

髪をすべて直接計算すると重い。
そこで、計算する髪と、見た目を増やす髪を分ける。

```text
guide    : 計算する代表の髪
follower : guideについてくる表示用の髪
```

本設計では、原則としてguideだけを計算する。
followerには重い物理計算をしない。

followerは、guideの位置、方向、束方向、太さ、カール情報を使って再構成する。

これにより、少ない計算で髪の見た目密度を増やせる。

---

## 23. 編集とのつなぎ方

ユーザが髪を編集した場合、いきなり全髪を再計算しない。

編集の影響を受けたguideやSegmentGroupに、編集重みを付ける。

```text
編集された部分       : editWeightを高くする
編集領域の境界付近   : editWeightを少しだけ付ける
編集されていない部分 : editWeightを0に近くする
```

editWeightが高いグループはDynamicSolve候補になる。
境界付近のグループはBoundaryRepair候補になる。
編集されていない部分はStaticCachedまたはStaticTransformedのまま維持する。

この仕組みにより、編集した部分だけを更新し、触っていない部分はできるだけ保つ。

---

## 24. LODの考え方

LODとは、距離や見え方に応じて処理の重さを変える仕組みである。

近くで大きく見える髪は、ある程度きれいに計算する。
遠くの髪や見えない髪は、軽くする。

本設計では、次の4段階に分ける。

| 距離・状態  | 更新                  | 描画                |
| ------ | ------------------- | ----------------- |
| Near   | DynamicSolveを許可     | ribbon / 少数tube   |
| Mid    | StaticTransformed中心 | simplified ribbon |
| Far    | StaticCached中心      | hair cards        |
| Hidden | freeze              | 描画なし、またはcards     |

Switch級の環境では、LODは必須である。
LODなしで高密度の髪を毎フレーム処理することは避ける。

---

## 25. 描画の方針

描画も重くなりすぎないようにする。

距離ごとの方針は次の通り。

```text
近距離   : ribbon、必要なら少数tube
中距離   : simplified ribbon
遠距離   : hair cards
不可視   : 描画しない
```

全髪をtubeで描画するのは重い。
Switch級では避ける。

StaticCachedのグループは、描画用データもキャッシュしてよい。
StaticTransformedの場合は、保存済み方向と長さを使って軽く描画位置を復元する。

---

## 26. 既存評価システムをそのまま使う

本設計では、新しい評価システムを作り直さない。
既存コードにある評価システムをそのまま使う。

比較方式も既存のものを使う。

```text
prediction_only
active_set_runtime
proposal_hybrid
full_physics
```

本研究のSegmentGroup型データ構造は、主に `proposal_hybrid` または `active_set_runtime` の内部で使う。

---

## 27. 既存評価項目との対応

既存評価システムには、すでに本研究に必要な項目がある。
実装では、それらに値を入れる。

### 27.1 実時間性を見る項目

```text
frame_ms
simulation_ms
fps
gpu_hair_ms
vram_mb
bandwidth_mb_frame
```

これらは、システムが60FPSを維持できているかを見るために使う。

### 27.2 SegmentGroup型データ構造を見る項目

```text
total_segment_group_count
static_cached_group_count
static_transformed_group_count
dynamic_group_count
deferred_correction_group_count
repair_required_group_count
fallback_group_count
```

これらは、各フレームでどのくらいのグループが静的で、どのくらいが動的だったかを見るために使う。

### 27.3 静的キャッシュの安全性を見る項目

```text
false_static_count
false_static_rate
cache_hit_count
cache_miss_count
cache_invalidation_count
watchdog_check_count
watchdog_repair_count
```

これらは、静的キャッシュが安全に使えているかを見るために使う。

### 27.4 境界の自然さを見る項目

```text
static_dynamic_boundary_mean_error_mm
static_dynamic_boundary_max_error_mm
boundaryNaturalnessScore
```

これらは、静的部分と動的部分の境目が不自然になっていないかを見るために使う。

### 27.5 メモリ削減を見る項目

```text
direction_cache_memory_mb
full_position_memory_mb
direction_cache_memory_saving_rate
```

これらは、方向ベクトルと長さを使うことで、全点位置を毎回持つよりどれくらいメモリを節約できたかを見るために使う。

### 27.6 編集の良さを見る項目

```text
rmsPositionError
silhouetteError
clumpPreservationScore
rootLocalityScore
editLocalityScore
boundaryNaturalnessScore
editedStrandRatio
editToRunLatencyMilliseconds
```

これらは、編集結果が自然か、編集していない部分が保たれているか、編集後すぐに反映できているかを見るために使う。

---

## 28. Telemetryの出し方

毎フレーム、SegmentGroupの状態を数えて、既存のTelemetryに渡す。

コードイメージは次の通り。

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

このTelemetryを、既存のCSV、JSON、SQLite DBへ流す。

---

## 29. 評価時の見方

評価では、単にFPSだけを見ない。
次のように見る。

### 29.1 60FPSを守れているか

見る項目は次の通り。

```text
fps
frame_ms
gpu_hair_ms
simulation_ms
```

目標は、ゲーム全体で60FPSを維持することである。
髪単体では、2.0〜3.5ms以内を目標にする。

### 29.2 静的再利用が効いているか

見る項目は次の通り。

```text
groupStaticReuseRate
static_cached_group_count
static_transformed_group_count
dynamic_group_count
```

StaticCachedとStaticTransformedが多く、DynamicSolveが少なければ、狙い通りに動いている。

### 29.3 静的判定が間違っていないか

見る項目は次の通り。

```text
falseStaticRate
false_static_count
watchdog_repair_count
cache_invalidation_count
```

falseStaticRateが高い場合、静的にしてはいけない部分まで止めている可能性がある。

### 29.4 境界が自然か

見る項目は次の通り。

```text
staticDynamicBoundaryError
static_dynamic_boundary_mean_error_mm
static_dynamic_boundary_max_error_mm
boundaryNaturalnessScore
```

これらが悪い場合、静的部分と動的部分の境目が不自然になっている。

### 29.5 編集がうまくいっているか

見る項目は次の通り。

```text
editLocalityScore
editedStrandRatio
editToRunLatencyMilliseconds
clumpPreservationScore
silhouetteError
```

編集した部分だけが変わり、編集していない部分が保たれていれば成功である。

---

## 30. Switch級で守るべきルール

Switch級の低性能環境で60FPSを狙う場合、以下を必ず守る。

```text
followerに物理計算をしない
全髪self-collisionをしない
全髪tube描画をしない
毎フレーム全SegmentGroupをDynamicSolveにしない
遠距離髪はStaticCachedまたはcardsにする
不可視髪はfreezeする
接触判定はDynamic候補周辺だけにする
GPU readback前提の処理を避ける
髪予算を超えそうなら品質を落とす
```

優先順位は次の通り。

```text
1. 60FPS維持
2. 近距離の見た目
3. 編集部分の自然さ
4. 接触破綻の抑制
5. 遠距離品質
```

つまり、遠くの髪の品質より、ゲーム全体の安定性を優先する。

---

## 31. 失敗条件

次の状態になった場合、設計または実装を見直す。

```text
DynamicSolve率が常に30%以上になる
StaticCached/StaticTransformed率が70%未満になる
falseStaticRateが高い
staticDynamicBoundaryErrorMmが大きい
髪処理が3.5msを超える
follower再構成が重い
遠距離髪がDynamicSolveされ続ける
不可視髪が更新され続ける
collision判定が全髪対象になっている
全髪self-collisionが走っている
```

特に、Switch級で60FPSを狙う場合、全髪を毎フレーム再計算する設計になった時点で失敗である。

---

## 32. 実装時に守るルール

実装者は、次のルールを守る。

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

## 33. 最終的なクラス構成

完成形では、次のようなクラスを中心にする。

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

## 34. この研究で最終的に示したいこと

本研究で示したいことは、次の通りである。

髪全体を毎フレーム高精度に物理計算するのではなく、髪を `点-線-点` のセグメントグループ列として扱い、各グループを静的・動的に切り替えることで、必要な部分だけを計算できる。

静的グループは、最後に確定した結果を再利用する。
根元が動いた場合は、保存済み方向と長さで軽く復元する。
編集や接触がある部分だけをDynamicSolveする。
静的部分と動的部分の境界はBoundaryRepairでつなぐ。

この仕組みにより、少し古いPCやNintendo Switch級の環境でも、ゲーム全体として60FPSを維持しながら、髪の部分編集、接触補正、見た目密度の維持を目指す。

---

## 35. まとめ

本設計の肝は、ActiveSet補正そのものではない。
本設計の肝は、ActiveSet補正や部分再計算を可能にするための **静的・動的セグメントグループ型髪データ構造** である。

このデータ構造では、髪一本を点列ではなく、`点-線-点` のグループ列として扱う。
各グループは、StaticCached、StaticTransformed、DynamicSolve、BoundaryRepair、Invalidの状態を持つ。

動かない部分は再計算しない。
根元移動だけなら軽く復元する。
編集、接触、貫通、急な動きがある部分だけを計算する。
境界だけが不自然な場合は軽く補正する。

評価は、既存コードの評価システムをそのまま使う。
特に、`falseStaticRate`、`groupStaticReuseRate`、`groupDynamicRate`、`staticDynamicBoundaryErrorMm`、`directionCacheMemorySavingRate`、`gpu_hair_ms` を重視する。

この方式により、髪全体を毎フレーム重く計算するのではなく、必要な部分だけを更新する髪システムを作る。
その結果として、固定60FPS、低性能環境、部分編集、接触補正、ゲーム内利用を同時に成立させることを目指す。
