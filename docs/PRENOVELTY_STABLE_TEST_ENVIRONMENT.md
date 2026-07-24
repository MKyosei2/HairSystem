# Pre-novelty stable test environment

## Scope

This document describes the corrected non-novelty test/runtime environment for
`prenovelty-ready`. It does not claim Novelty 01 performance. Formal evidence is
accepted only when the frozen workload, environment checks, per-run validator,
artifact hashes, and suite-level gates all pass.

## Fixed workload

- Release x64, 1920 x 1080, VSync off
- fixed time step: 1/60 second
- 600 warm-up frames and 6,000 measured frames
- 4,000 guides, 24 followers per guide, 100,000 displayed strands
- 128 particles per guide and 6 solver iterations
- five deterministic seeds for the complete matrix
- frozen asset source branch: `prenovelty-ready` or `Novelty`

## Required execution order

```bat
set HAIRGPU_FREEZE_OPERATOR=Your_Name

tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  prepare-asset

tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  prenovelty-complete ^
  out\prenovelty_complete
```

`prenovelty-complete` executes the asset preparation, the 12-case smoke matrix,
20 real Viewer startup/render/shutdown repetitions, determinism replays, and the
30-minute long-run set. A non-zero exit code means the environment is not frozen
for thesis measurement.

## Formal outputs

Each run writes the following files to the selected `out` directory:

- `<run>_frames.csv` and `<run>_telemetry.jsonl`: frame evidence
- `<run>_settings.json`: requested/applied/observed settings
- `<run>_summary.json`: native aggregate and failure counters
- `<run>_environment.json`: adapter, driver, power, temperature and clocks
- `<run>_process_memory.jsonl`: RAM, VRAM, handle, GDI, USER and thread samples
- `<run>_visual.bmp`: post-measurement back-buffer capture
- `<run>_visual_metrics.json`: coverage, bounds, luminance and perceptual hash
- `<run>_validation.json`: validity and quality-gate result
- `<run>_artifact_manifest.json`: size and SHA-256 for every artifact
- `<run>_COMPLETE.json`, `_INCOMPLETE.json`, `_ERROR.json`: run state

Suite-level outputs are `suite_metrics.csv`, `suite_metrics.json`,
`final_runtime_suite.sqlite`, `suite_post_gates.json`, `suite_summary.json`, and
`final_runtime_suite_artifacts.zip`.

## Acceptance order

1. `suite_preflight.json` has no problems.
2. Every expected run has `_COMPLETE.json`; no `_ERROR.json`, `_INCOMPLETE.json`,
   `.run.lock`, or `_RUNNING.json` remains.
3. Every `<run>_validation.json` has `valid=true`.
4. Every artifact size and SHA-256 matches its manifest.
5. `suite_post_gates.json` has `passed=true`.
6. `suite_summary.json` has `postSuiteGatesPassed=true` and the expected run count.

A run may be valid evidence while failing only a performance target; runtime,
integrity, visual, environment, or availability errors make the run invalid.

## Suite-level stability gates

The corrected suite rejects:

- formal/determinism/long-run case-type mixing
- missing or inconsistent deterministic hashes
- RAM or VRAM growth beyond the configured slope
- handle, GDI object, USER object, or thread leakage
- unresolved GPU timestamp queries
- p99 frame-time degradation during a soak
- missing, blank, full-screen, collapsed, or low-variance visual captures
- catastrophic visual divergence from the paired B0 scenario/seed baseline

The visual capture is rendered once after measured frames and copied before
`Present` on the flip-discard swap chain. It therefore does not contaminate the
measured frame set and does not depend on undefined post-Present contents.

## Diagnostic F5 data

Visual Studio F5 output remains diagnostic only:

```text
results/committed/local_debug/<UTC timestamp>_<PID>/
```

The report is generated even when `git.exe` is absent or push authentication
fails. Only a successfully postprocessed directory may be auto-committed.
`files.sha256` includes `postprocess_status.json`; failed postprocessing is left
local and is never pushed automatically.

## Evidence retention

The self-hosted RTX 4070 Ti SUPER formal workflow uploads the complete output for
90 days. Before that expiry, copy the accepted suite ZIP and the workflow run URL
to the thesis evidence archive. GTX 1660 projection remains theoretical until a
real target-hardware run passes the same contract.
