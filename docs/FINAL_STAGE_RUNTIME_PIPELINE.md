# Final Stage Runtime Pipeline

This stage replaces proxy-only runtime collection with DX11 viewer telemetry that can be ingested directly into SQLite and exported to Excel.

## What is added
- Automatic runtime telemetry export from `hairgpu_dx11_viewer`
- Summary JSON and per-frame CSV emission controlled by environment variables
- A Python pipeline that can launch the viewer, wait for telemetry output, ingest results into SQLite, and export an Excel workbook
- SQL comparison and ablation sheets generated from runtime telemetry

## Environment variables used by the viewer
- `HAIRGPU_AUTO_TELEMETRY=1`
- `HAIRGPU_TELEMETRY_DIR=<output directory>`
- `HAIRGPU_TELEMETRY_RUN_NAME=<run identifier>`

When enabled, the viewer writes:
- `<run_name>_frames.csv`
- `<run_name>_summary.json`

## Example workflow
1. Build `hairgpu_dx11_viewer` in Visual Studio (`x64`, `Debug` or `Release`).
2. Run the Python pipeline:
   - launch the viewer
   - close the viewer after the capture session
   - pipeline ingests runtime telemetry into SQLite
   - pipeline exports `final_runtime_report.xlsx`

## Notes
- This is real viewer telemetry export, not the earlier proxy runtime generator.
- The viewer still needs to be executed on Windows because DX11 is required.
- The Python side is cross-platform for ingest/export, but launching the viewer is Windows-oriented.
