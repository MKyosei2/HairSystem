# Visual Studio local debugger capture

## Purpose

This workflow captures diagnostic data automatically when `HairSystem` is started from the Visual Studio local debugger. It is designed for rapid development, regression detection, graph generation, and Git history during the pre-novelty and Novelty 01 implementation phases.

It does **not** replace the formal runtime suite.

| Launch path | Evidence class | May be used as formal paper evidence |
|---|---|---:|
| Visual Studio F5 / Local Windows Debugger | `diagnostic_debugger` | No |
| `Run_Final_Runtime_Suite.bat` with frozen asset and strict gate | `formal_evidence` | Yes, after all gates pass |

A debugger changes CPU scheduling, frame pacing, driver synchronization, and query resolution. Therefore the F5 graphs are useful for comparison and fault discovery, but their FPS and frame-time values must never be reported as the formal result.

## Novelty 01 relationship

The proposed data type uses a point-line-point segment group as its minimum state unit. Each group can be:

- static cached: reuse the last valid solved result without recomputation;
- static transformed: reuse cached local state while applying a valid parent/head transform;
- dynamic: compact and solve on the GPU;
- repair required: resolve shared-point, boundary, collision, certificate, or wake failures;
- fallback: reject unsafe compact data and use a safe representation.

Adjacent groups share the middle point. Directional responsibility and hemisphere ownership determine which side owns the shared point for a specific correction. Static reuse must be invalidated by head motion, collision risk, edit changes, boundary error, certificate failure, watchdog findings, or predictive wake.

The local debugger capture includes the same columns used to inspect these mechanisms:

- static/dynamic/repair group counts;
- cache hit, miss, and invalidation counts;
- shared-point conflicts and hemisphere resolutions;
- false-static rate;
- static/dynamic boundary error;
- predictive-wake precision, recall, and delay;
- repair success and recurrence;
- state transition and oscillation counts;
- pass-level GPU timings;
- full-position and direction-cache memory estimates;
- determinism hashes when available.

## Default Visual Studio behavior

For `Release | x64`, the project supplies these debugger arguments:

```text
--local-capture --local-capture-warmup 120 --local-capture-frames 1800 --local-capture-autocommit --local-capture-push
```

The Viewer:

1. starts normally from Visual Studio;
2. runs 120 warm-up frames;
3. records 1,800 diagnostic frames;
4. exits automatically;
5. writes CSV, JSONL, settings, summary, graphs, report, hashes, and context;
6. commits only the generated run directory;
7. pushes the current branch to `origin` when authentication is available.

The default output directory is:

```text
results/committed/local_debug/<UTC timestamp>_<process id>/
```

The exact run is isolated in its own directory. Source-code changes are not staged by the automatic data commit.

## Generated files

```text
capture_context.json
local_debug_frames.csv
local_debug_frames.jsonl
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

`run_manifest.json` always contains:

```json
{
  "evidenceClass": "diagnostic_debugger",
  "formalEvidence": false
}
```

## Graphs

### `frame_time.svg`

Plots frame time, simulation time, and resolved GPU hair time. It includes reference lines for:

- 60 FPS: 16.6667 ms;
- pre-novelty 24 FPS gate: 41.6667 ms.

### `segment_group_state.svg`

Plots static-cached, static-transformed, dynamic, and repair-required group counts. This graph becomes especially important after the Novelty 01 integrated segment-group runtime is enabled.

### `gpu_pass_breakdown.svg`

Shows the average GPU cost of classification, compaction, static transform, dynamic solve, boundary repair, watchdog, follower reconstruction, and hair draw.

SVG is used so graph generation has no third-party Python dependency and GitHub can display the result directly.

## Automatic Git commit safety

The postprocessor refuses to commit outside:

```text
results/committed/local_debug/
```

It uses a path-limited commit, so unrelated modified source files remain untouched. Generated local-debug data is explicitly re-included by `.gitignore` rules.

Automatic push requires an authenticated `origin`. When push fails, the local commit and files remain available and `postprocess_status.json` records `pushError`; graph and report generation remain successful. If `git.exe` is unavailable in the Visual Studio environment, report generation still completes and automatic commit/push is skipped. A failed postprocess run is never committed automatically.

`files.sha256` is regenerated after `postprocess_status.json` is written, so the success/failure status is covered by the run-directory integrity list.

## Disabling or changing capture

Remove the debugger arguments from:

```text
HairSystem project
  -> Properties
  -> Configuration Properties
  -> Debugging
  -> Command Arguments
```

Or use:

```text
--no-local-capture
```

Useful alternatives:

```text
--local-capture --local-capture-warmup 60 --local-capture-frames 600
--local-capture --local-capture-warmup 120 --local-capture-frames 1800 --local-capture-autocommit
--local-capture --local-capture-warmup 120 --local-capture-frames 1800 --local-capture-autocommit --local-capture-push
```

## Formal experiment workflow

Formal evidence remains separate:

```bat
tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  prenovelty-smoke ^
  out\prenovelty_smoke
```

After Novelty 01 implementation, use the final template and the B0-B8/P comparison modes. Formal runs must use the frozen asset, Release x64, fixed 1/60 timestep, 1920x1080, VSync off, strict hardware/environment checks, delayed GPU timing, and the formal validator.
