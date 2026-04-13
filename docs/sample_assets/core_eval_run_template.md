# Core evaluation quick template

Example:

```bash
python tools/final_stage/hairgpu_core_eval.py \
  --cli build/cli-debug/apps/cli/hairgpu_cli \
  --input docs/sample_assets/proposal_edit_stack.csv \
  --teacher-dir out_suite/teacher \
  --bundle-dir out_suite/bundle \
  --output-dir out/core_eval \
  --max-active-clusters 6 \
  --max-active-guides 64 \
  --target-frame-ms 16.67
```

Generated artifacts:

- `core_eval_manifest.json`
- `final_project_summary.json`
- `final_project_report.md`
