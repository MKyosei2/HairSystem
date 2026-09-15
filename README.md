# HairSystem

HairSystem は、3D キャラクターの高密度な髪を GPU 上でシミュレーションし、
編集・再構築・描画までをリアルタイムに扱うための研究用プロトタイプです。

卒業研究では、Guide / Follower 構造を前提に、局所編集時の更新範囲を
Segment Group 単位で制御する SG-APT（Segment Group Adaptive Payload
Transition）を実装し、従来の全体更新 Baseline（B0）と提案方式（P）を比較します。

## 主要構成

- `include/hairgpu/` — 共通データ型、編集、検証、SG-APT 契約
- `src/hairgpu_core/` — HairSystem のコア実装
- `apps/dx11_viewer/` — DirectX 11 Viewer / GPU Runtime / HLSL
- `benchmarks/` — 正式評価条件
- `tools/final_stage/` — Smoke / Pilot / Formal / Report 用ツール
- `tests/final_stage/` — 契約テストと回帰テスト
- `docs/` — 実装・評価仕様
- `論文パーツ/` — 卒業論文用テキスト・図表

## 固定評価条件

正式評価では、比較条件を固定して B0 と P を同一入力で実行します。

- Guide: 4,000
- Follower: 96,000
- Total strands: 100,000
- Points / strand: 128
- Solver iterations: 6
- Resolution: 1920 × 1080
- Fixed step: 1 / 60 s

この 4,000 Guide は「最適値」や「標準値」を意味せず、比較のために固定した条件です。

## Thesis Practical Evaluation

局所編集の実用評価は以下を記録します。

- 実際に変更された Point
- 直接影響を受けた Guide / Segment Group / Block
- P で局所無効化が成立したか
- 安全条件を満たせない場合の Full Dynamic fallback
- 既存 frame / Novelty01 telemetry と結合した更新範囲・再利用・性能

実験定義は `benchmarks/thesis_practical/`、
実装説明は `docs/THESIS_PRACTICAL_EVALUATION.md` を参照してください。

## 評価上の注意

- B0 は特定の一論文をそのまま再実装したものではなく、本研究の比較用 Baseline です。
- Formal の数値は Release x64 の正式 Suite から取得します。
- Debug / F5 / 可視化 Overlay は診断・発表用であり、正式 Performance 値には使用しません。
- 局所更新が安全に成立しない場合は、速度を優先せず Full Dynamic へ戻します。

## Build

Windows / Visual Studio / DirectX 11 を前提とします。

```powershell
msbuild HairSystem.sln /m /p:Configuration=Release /p:Platform=x64
```

正式実験を行う前に、`tests/final_stage/` と各 preflight / smoke を先に実行してください。
