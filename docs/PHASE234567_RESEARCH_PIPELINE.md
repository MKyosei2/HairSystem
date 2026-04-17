# Phase 2〜7 Proxy Research Pipeline

この追加では、既存の Phase 1 外枠の上に次を接続する。

- Phase 2: teacher dataset の自動生成と SQL 保存
- Phase 3: prior / residual の線形 proxy 学習とモデル保存
- Phase 4: active-set 選別のフレーム別集計
- Phase 5: corrective physics の proxy 効果推定
- Phase 6: external contacts の集計
- Phase 7: comparison / ablation の Excel 自動生成

## 実行

```bash
python tools/research/hairgpu_research_pipeline.py \
  --db-path out/research.db \
  --output-dir out/research_run \
  --cli-path build/hairgpu_cli \
  --asset-path docs/sample_assets/import_curves_template.csv \
  --experiment-name "Phase 2-7 Dry Run" \
  --scenario-code S3 \
  --method-code D
```

## 出力

- SQLite: `runs`, `metrics_summary`, `teacher_datasets`, `learned_models`, `active_set_evaluations`, `corrective_physics_evaluations`, `external_contact_evaluations`
- Teacher CSV / summary JSON
- Prior / Residual model JSON
- Active-set plan CSV / summary JSON
- Corrective / external contact summary JSON
- Phase 7 Excel workbook

## 注意

- これは **DX11 viewer 実計測ではなく proxy runtime** を使う。
- Prior / Residual は **研究導線を先に通すための線形 proxy 学習**。
- 評価表と ablation 表は SQLite の記録から自動生成される。
