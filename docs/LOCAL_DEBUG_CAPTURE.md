# Visual Studio local debugger capture

## Purpose

Visual Studio F5 is a development and diagnosis path. It is useful for visual inspection,
regression discovery, graph generation, and checking the Novelty 01 telemetry columns, but
it is **not formal thesis evidence**.

| Launch path | Evidence class | Formal thesis evidence |
|---|---|---:|
| Visual Studio F5 / Local Windows Debugger | `diagnostic_debugger` | No |
| `Run_Final_Runtime_Suite.bat` with all strict gates | `formal_evidence` | Yes |

A debugger changes scheduling, frame pacing, synchronization, and GPU-query resolution.
Do not report F5 FPS or frame-time values as the formal result.

## Current default F5 behavior

`Release | x64` currently has no automatic command arguments. Pressing F5 starts the normal
interactive Viewer and does **not** automatically start capture, terminate the Viewer,
commit files, or push a branch.

This is intentional. A normal visual check must not unexpectedly close the Viewer or modify
Git history.

## Starting an explicit diagnostic capture

Add the following arguments temporarily in:

```text
HairSystem project
  -> Properties
  -> Configuration Properties
  -> Debugging
  -> Command Arguments
```

Recommended local-only command:

```text
--local-capture --local-capture-warmup 120 --local-capture-frames 1800
```

Optional local commit, without push:

```text
--local-capture --local-capture-warmup 120 --local-capture-frames 1800 --local-capture-autocommit
```

Automatic push is not the default. Use `--local-capture-push` only when the current branch,
remote authentication, and generated run have been reviewed.

Disable capture explicitly with:

```text
--no-local-capture
```

## Diagnostic output directory

```text
results/committed/local_debug/<UTC timestamp>_<process id>/
```

A successfully postprocessed run can contain:

```text
capture_context.json
local_debug_frames.csv
local_debug_frames.jsonl
local_debug_per_second.csv
local_debug_settings.json
local_debug_summary.json
frame_time.svg
segment_group_state.svg
gpu_pass_breakdown.svg
summary_metrics.csv
run_manifest.json
report.md
files.sha256
postprocess_status.json
```

`run_manifest.json` identifies the output as diagnostic:

```json
{
  "evidenceClass": "diagnostic_debugger",
  "formalEvidence": false
}
```

## Novelty 01 telemetry available for diagnosis

The capture path can record:

- static-cached, static-transformed, dynamic, and repair-required group counts;
- cache hits, misses, and invalidations;
- shared-point conflicts and directional-responsibility resolutions;
- false-static and static/dynamic-boundary errors;
- predictive-wake precision, recall, and delay;
- repair attempts, verified success, and recurrence;
- state transitions and oscillations;
- delayed per-pass GPU timings;
- memory estimates and determinism hashes when available.

Unavailable values must remain unavailable. They must not be replaced by synthetic zeroes.

## Commit and postprocessing safety

The postprocessor may stage only the exact generated run directory under:

```text
results/committed/local_debug/
```

A missing `git.exe`, unavailable authentication, or push failure must not prevent local
CSV, JSON, SVG, and Markdown generation. Failed or partial postprocessing remains local and
must not be committed automatically.

## Formal experiment path

Use the strict Release x64 suite for measurements used in the thesis:

```bat
tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  prenovelty-complete ^
  out\prenovelty_complete
```

Formal runs require the frozen workload, 1920x1080, fixed 1/60 timestep, VSync off, clean
Git state, hardware/environment checks, delayed GPU timing, artifact hashes, per-run
validation, and suite-level gates.
