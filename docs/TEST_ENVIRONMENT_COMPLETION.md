# HairSystem Formal Test Environment Completion

## Scope

This document covers the stable build, execution, measurement, recovery, and evidence environment for the `Novelty` branch. It does not claim that Novelty 01 itself has reached its performance or quality targets. It defines when a run is valid evidence and prevents invalid runs from being mixed into the thesis data.

## Fixed formal workload

The formal gate rejects any drift from the following values:

- Build: `Release | x64`
- Resolution: `1920 x 1080`
- Fixed time step: `1 / 60 s`
- Warm-up: `600` frames
- Measurement: `6000` frames
- Guide strands: `4000`
- Followers per guide: `24`
- Display strands: `100000`
- Particles per strand: `128`
- Solver iterations: `6`
- VSync: disabled
- Present sync interval: `0`
- Seeds: at least five unique values
- Methods: `B0` through `B8`, plus `P`
- Scenarios: `S01` through `S18`

## First-time preparation

Use a Visual Studio 2022 Developer Command Prompt at the repository root.

```bat
set HAIRGPU_FREEZE_OPERATOR=Your_Name
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json prepare-asset
```

This performs a Release x64 rebuild, native self-test, shader self-test, Python syntax and unit tests, then creates and verifies the canonical bundle at:

```text
benchmarks/novelty01/frozen_asset_bundle/
```

The bundle contains:

- `asset/novelty01_100k.hairbin`
- `topology/novelty01_100k_topology.bin`
- `settings/generator_settings.json`
- `frozen_asset_manifest.json`

The asset, topology, settings, and bundle hashes are verified before every formal suite.

## Recommended execution order

Run these commands in order. Do not begin the next stage when the previous stage fails.

```bat
REM 1. Proposal path smoke test
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json proposal-smoke

REM 2. Baseline/proposal comparison smoke test
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json comparison-smoke

REM 3. High-risk scenario set
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json critical

REM 4. Cross-replay determinism
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json determinism

REM 5. Thirty-minute stability
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json soak30

REM 6. Two-hour stability
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json soak2h

REM 7. Eight-hour stability
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json soak8h

REM 8. Full B0-B8/P x S01-S18 x five-seed matrix
tools\final_stage\Run_Final_Runtime_Suite.bat docs\sample_assets\final_runtime_suite_template.json full
```

## What the final gate enforces

`tools/final_stage/hairgpu_formal_gate.py` rejects a run when any of the following is true:

- the branch or Git worktree is not valid;
- the frozen asset is missing or modified;
- the host does not match the formal hardware profile;
- AC power or the required Windows power plan is unavailable;
- the Viewer used a software adapter;
- requested, applied, and observed method flags differ;
- requested and observed workload values differ;
- commit, executable, shader, timeline, manifest, or asset identity is missing;
- the Viewer does not classify the result as a complete real measurement;
- telemetry contains synthetic data, same-frame readback, NaN, infinity, fatal failure, or scenario assertion failure;
- any selected run is incomplete or fails its quality gate;
- determinism hashes differ between replays;
- a long run exceeds memory slopes, VRAM slopes, p99 degradation, or delayed GPU-query coverage limits;
- GPU temperature or clock evidence is missing or outside the configured limits.

## Output that proves completion

The output directory is not considered complete unless all of these exist:

```text
suite_summary.json
suite_metrics.json
final_runtime_suite.sqlite
final_runtime_suite_artifacts.zip
formal_host_preflight.json
formal_environment_gate.json
formal_environment_gate.md
FORMAL_ENVIRONMENT_COMPLETE.txt
```

`FORMAL_ENVIRONMENT_COMPLETE.txt` is written only when the final gate finds no issue.

## Crash and resume behavior

Each run uses:

- an exclusive run lock;
- `RUNNING`, `INCOMPLETE`, `ERROR`, and `COMPLETE` state files;
- stale-lock recovery;
- atomic JSON writes;
- SHA-256 artifact manifests;
- an execution fingerprint covering the case, executable, shader bundle, timeline, asset, and source commit;
- resume only when the previous complete artifacts and execution fingerprint still match.

A terminated or corrupted run is never reused as complete evidence.

## CI split

### Shared Windows runner

`.github/workflows/windows-build-test.yml` validates:

- Release x64 compilation;
- native ABI and workload self-test;
- Python syntax;
- Python self-tests;
- final-stage unit tests.

### Dedicated GTX 1660 runner

`.github/workflows/formal-gpu-regression.yml` is manual and requires a self-hosted runner with these labels:

```text
self-hosted, Windows, X64, HairSystem, GTX1660
```

Performance, determinism, and soak evidence must come from this dedicated runner or an equivalent manually prepared GTX 1660 machine. Shared GitHub runners are not valid performance evidence.

## Development-only exception

For runner development on other hardware, the host contract can be skipped explicitly:

```bat
set HAIRGPU_SKIP_HARDWARE_CONTRACT=1
```

Results produced with that override are development diagnostics only and must not be used as formal thesis evidence.

## Completion definition

The test environment is complete when:

1. a clean clone builds Release x64;
2. `prepare-asset` succeeds;
3. proposal, comparison, critical, and determinism modes pass;
4. 30-minute, 2-hour, and 8-hour modes pass;
5. the full matrix completes;
6. every output directory contains `FORMAL_ENVIRONMENT_COMPLETE.txt`;
7. the source commit and all artifact hashes are preserved with the thesis data.
