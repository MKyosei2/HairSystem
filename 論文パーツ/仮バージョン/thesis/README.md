# 卒業論文 第1章〜第10章 Markdown・図表パッケージ

## 情報源ポリシー

このパッケージで実験値・図・グラフ・システム固有の仕様に使用した元情報は **`thesis_7032_20260919_195059_703.zip` のみ**である。

- Source ZIP SHA-256: `0427f5ff361740fc00c966569b7d927b0bab518760d3e236922c7f61d1b7dfe9`
- Formal session: `thesis_7032_20260919_195059_703`
- Formal commit: `21e206250d902912fc429c0c024dcc992f522466`
- Session status: `FORMAL_COMPLETE_VALID`
- Final Gate: `PASS`
- 外部Web / インターネット: **不使用**
- 外部画像: **不使用**
- 外部ベンチマーク値: **不使用**
- 実測値の推測補完: **不使用**

## 収録章

1. `Chapter01_Introduction` — はじめに
2. `Chapter02_RelatedWork` — 関連研究
3. `Chapter03_ProblemAndDesign` — 問題設定と設計方針
4. `Chapter04_ProposedMethod` — 提案手法
5. `Chapter05_Implementation` — 実装
6. `Chapter06_EvaluationPlan` — 評価計画
7. `Chapter07_Results` — 結果
8. `Chapter08_Discussion` — 考察
9. `Chapter09_LimitationsFuture` — 限界と今後の課題
10. `Chapter10_Conclusion` — 結論

各章にはMarkdown本文、`README.md`、`figures/`、必要に応じて `data/` を含む。

## 実装章の数式について

正式ZIPにはソースコード本文そのものは含まれていない。そのため第4・5章では、設定値、Telemetry field、Runtime Counter、Asset構造から確認できる内容を**数学的抽象モデル**として記述した。未記録の閾値・係数・量子化ビット数などを推定していない。

## 関連研究について

元ZIPには外部論文の書誌情報がない。虚偽引用を避けるため第2章は技術分類と本研究の位置づけまで記述し、特定論文の引用は生成していない。提出前に必要な引用箇所は `CITATION_GAPS.md` にまとめた。

## 正式結果の扱い

本パッケージは、Pを成功したように見せる補正を行っていない。正式結果では全45ペアでB0高速45、P高速0である。PerformanceではFull Dynamic 100%、Microtype Decode 0%、Physics Island Candidate 0%、GPU-Resident Task 0%であり、Physics IslandsとRod Repairは正式P設定でOFFである。

したがって第8〜10章は、「提案構成の実装と形式的評価は成立したが、現実装では性能改善を実証できなかった」という負の結果を中心に構成している。
