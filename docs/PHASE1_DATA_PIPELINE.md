# Phase 1 Expansion: Instrumentation + SQL + Auto Input + Excel

This stage adds a lightweight experiment-data pipeline around the existing `hairgpu_cli` toolchain.

## Scope
- Register experiment metadata with a form
- Execute baseline CLI measurements
- Generate repeatable proxy runtime/frame metrics
- Store runs in SQLite
- Export comparison workbooks to Excel

## Entry points
- `tools/phase1/hairgpu_phase1_app.py` — Streamlit form + launcher
- `tools/phase1/hairgpu_phase1_cli.py` — command-line workflow
- `tools/phase1/hairgpu_phase1_db.py` — SQLite schema and helpers
- `tools/phase1/hairgpu_phase1_excel.py` — Excel exporter
- `tools/phase1/schema.sql` — SQL schema snapshot

## Minimal workflow
1. Build `hairgpu_cli`
2. Start the form: `streamlit run tools/phase1/hairgpu_phase1_app.py`
3. Fill experiment name / method / scenario / asset / notes
4. Choose frame count and target FPS
5. Click run
6. Review generated SQLite rows and Excel workbook in `phase1_outputs/`

## What runs automatically
The pipeline now calls three CLI steps:
- `metrics-json` for asset-level structural metrics
- `validate` for validation and health checks
- `phase1-proxy-run` for repeatable frame-level Phase 1 runtime proxies

`phase1-proxy-run` writes:
- `*_phase1_runtime_summary.json`
- `*_frame_metrics.csv`

The Python pipeline imports both into SQLite and then regenerates the Excel workbook.

## Stored data
The SQLite schema tracks:
- experiments
- scenarios
- methods
- runs
- metrics_summary
- metrics_frame

`metrics_summary` now includes:
- runtime mode
- frame count
- target FPS
- FPS@60 ratio
- p95 / worst frame time
- peak RAM
- active cluster summary
- penetration summary
- silhouette / clump / edit latency proxies

`metrics_frame` stores one row per generated frame.

## Current limitation
This Phase 1 expansion still does **not** hook into the DX11 viewer runtime directly.
The inner measurement layer is currently a **proxy runtime estimator inside `hairgpu_cli`**. It is deterministic and useful for experiment bookkeeping, SQL integration, and Excel comparisons, but it is not a replacement for final viewer/GPU runtime instrumentation.

## Intended next step
Replace or augment `phase1-proxy-run` with viewer-side runtime logging once the runtime instrumentation hooks are ready. The SQL/Excel side can stay as-is.
