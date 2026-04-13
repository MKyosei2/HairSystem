# HairSystem

HairSystem is a research-oriented hair runtime/codebase centered on:

- guide / follower hair representation
- active-set corrective runtime under a fixed frame budget
- DX11 viewer for interactive inspection on Windows
- CLI tools for proposal-aligned evaluation, ablation, and export

This snapshot now includes a **root CMake entry point**, **Windows helper scripts that work from the current repo layout**, and a **one-shot runtime suite pipeline** for reproducible final-stage comparisons.

## Repo layout

- `src/core` — canonical CMake core library sources
- `apps/cli` — canonical CMake CLI app
- `apps/dx11_viewer` — DX11 viewer (Windows only)
- `tools/final_stage` — final evaluation/import/report scripts
- `tools/research` — phase/research automation
- `scripts/windows` — helper scripts for configure/build/open/signing
- `docs/sample_assets` — template manifests and presets

## Quick start

### Windows (Visual Studio 2022)

```bat
scripts\windows\OpenInVS2022.bat
scripts\windows\Build_Debug_x64.bat
```

These helpers now generate a solution via CMake automatically when a checked-in `HairSystem.sln` is not present.

### Cross-platform CLI build

```bash
cmake --preset cli-debug
cmake --build --preset cli-debug
```

### Windows CMake build

```bat
cmake --preset windows-vs2022-debug
cmake --build --preset windows-vs2022-debug
```

## Final-stage runtime suite

Use the runtime suite runner to ingest existing telemetry or launch the viewer on Windows and produce:

- SQLite database
- Excel workbook
- JSON / CSV / Markdown summaries
- optional comparison charts (when matplotlib is available)

Template manifest:

- `docs/sample_assets/final_runtime_suite_template.json`

Example:

```bash
python tools/final_stage/hairgpu_runtime_suite.py \
  --config docs/sample_assets/final_runtime_suite_template.json
```

Windows batch wrapper:

```bat
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json
```

## Proposal-aligned core evaluation

```bash
python tools/final_stage/hairgpu_core_eval.py \
  --cli build/cli-debug/apps/cli/hairgpu_cli \
  --input docs/sample_assets/proposal_edit_stack.csv \
  --teacher-dir out_suite/teacher \
  --bundle-dir out_suite/bundle \
  --output-dir out/core_eval
```

Outputs include:

- manifest JSON
- final project summary JSON
- Markdown report

## Current scope of this patch

This patch focuses on high-leverage repo/runtime automation gaps that can be implemented safely in a non-Windows environment:

- unified root build entry (`CMakeLists.txt`, `CMakePresets.json`)
- fixed Windows helper scripts/root path handling
- reproducible runtime comparison/import pipeline
- richer Excel + Markdown summaries
- template manifests for repeatable evaluation

It does **not** claim to fully finish Windows-only DX11 feature work such as viewer-side strand picking, GPU-only active-set compaction, or full runtime SDF collider parity. Those still require implementation and validation on a Windows DX11 toolchain.

## Latest viewer additions
- Real Alembic SDK builds are enabled by default when `ALEMBIC_ROOT` / `ALEMBIC_DIR` is available.
- Strand gizmo editing modes: `1` tip translate, `2` whole-strand translate, `3` root translate.
- Hold left mouse on a selected strand gizmo axis to drag.
- `Ctrl+L` loads a scalp bone animation CSV; matching `*_bones.csv` / `.bones.csv` sidecars auto-load on asset import.
