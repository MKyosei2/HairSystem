# Pre-novelty stable test environment

## Scope

This document defines the non-novelty runtime and measurement environment for
`prenovelty-ready`. It does not claim Novelty 01 performance. Formal evidence is accepted
only when the frozen workload, environment checks, every run validator, artifact hashes,
and all suite-level gates pass.

## Fixed workload

- Release x64, 1920x1080, VSync off
- fixed time step: 1/60 second
- 600 warm-up frames and 6,000 measured frames
- 4,000 guides and 24 followers per guide
- 100,000 displayed strands
- 128 particles per guide and 6 solver iterations
- five deterministic seeds for the complete matrix
- frozen asset source branch: `prenovelty-ready` or `Novelty`

## One-command complete run

```bat
set HAIRGPU_FREEZE_OPERATOR=Your_Name

tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  prenovelty-complete ^
  out\prenovelty_complete
```

`prenovelty-complete` now separates every phase so suite summaries and SQLite databases
cannot overwrite each other:

```text
out/prenovelty_complete/
├─ smoke/
├─ startup20/
├─ determinism/
├─ soak30/
├─ complete_manifest.json
├─ prenovelty_complete_evidence.zip
└─ prenovelty_complete_evidence.zip.sha256
```

Execution order:

1. build Release x64 and prepare/verify the frozen asset;
2. run the 12-case pre-novelty smoke matrix;
3. run 20 real Viewer startup/render/shutdown repetitions;
4. run deterministic replays;
5. run the four 30-minute long-run cases;
6. validate all four phase directories;
7. write one completion manifest and one portable evidence ZIP.

A non-zero exit code means the environment is not accepted for thesis measurement.

## Phase outputs

Formal phases write per-run files such as:

- `<run>_frames.csv` and `<run>_telemetry.jsonl`;
- `<run>_settings.json`;
- `<run>_summary.json`;
- `<run>_environment.json`;
- `<run>_process_memory.jsonl`;
- `<run>_visual.bmp` and `<run>_visual_metrics.json`;
- `<run>_validation.json`;
- `<run>_artifact_manifest.json`;
- `<run>_COMPLETE.json`, `_INCOMPLETE.json`, or `_ERROR.json`.

Each formal phase also writes:

```text
suite_preflight.json
suite_dry_run.json
suite_metrics.csv
suite_metrics.json
final_runtime_suite.sqlite
suite_post_gates.json
suite_summary.json
final_runtime_suite_artifacts.zip
```

`startup20` writes `startup20_summary.json` in its own directory.

## Complete manifest

`complete_manifest.json` is the final acceptance record. It contains:

- branch and commit SHA;
- clean-worktree result;
- suite configuration SHA-256;
- frozen asset manifest SHA-256;
- pass/fail state for smoke, startup20, determinism, and soak30;
- selected, completed, valid, and quality-pass counts;
- hashes and sizes of each phase summary;
- any remaining `_ERROR`, `_INCOMPLETE`, `_RUNNING`, or `.run.lock` markers;
- the SHA-256 of the manifest itself;
- the final evidence ZIP filename and the SHA-256 sidecar filename.

The complete run passes only when all four phases pass and no incomplete/error marker remains.

## Acceptance order

1. `complete_manifest.json` has `status="COMPLETE"` and `passed=true`.
2. Repository branch and commit SHA are present, and the worktree is clean.
3. The frozen asset manifest exists and its SHA-256 is recorded.
4. Smoke has all expected runs completed and valid.
5. `startup20` has exactly 20 successful repetitions.
6. Determinism replays have matching non-zero hashes and no mismatch.
7. All four 30-minute runs pass resource and frame-stability gates.
8. No `_ERROR.json`, `_INCOMPLETE.json`, `_RUNNING.json`, or `.run.lock` remains.
9. `prenovelty_complete_evidence.zip` and its `.sha256` sidecar exist.

A run may be valid evidence while missing only a future performance target. Runtime,
integrity, visual, environment, availability, determinism, or resource-leak failures make
the phase invalid.

## Stability gates

The suite rejects:

- formal/determinism/long-run case-type mixing;
- missing or inconsistent determinism hashes;
- RAM or VRAM growth beyond the configured slope;
- handle, GDI object, USER object, or thread leakage;
- unresolved GPU timestamp queries;
- p99 frame-time degradation during a soak;
- missing, blank, full-screen, collapsed, or low-variance visual captures;
- catastrophic visual divergence from the paired B0 scenario/seed baseline.

Visual capture occurs after measured frames and before `Present`, so it does not contaminate
the measured frame set or depend on undefined post-Present contents.

## Diagnostic F5 data

F5 is interactive by default and does not automatically capture or push data. To create a
diagnostic capture, supply explicit `--local-capture` arguments as described in
`docs/LOCAL_DEBUG_CAPTURE.md`.

Diagnostic output is stored under:

```text
results/committed/local_debug/<UTC timestamp>_<PID>/
```

It remains `formalEvidence=false` and must not be mixed with formal thesis values.

## CI and retention

CI validates the committed source directly. It must not patch source files inside the
workflow before testing them.

The self-hosted RTX 4070 Ti SUPER workflow uploads:

- the full pre-novelty or Novelty output directory;
- the complete frozen asset bundle, not only its manifest;
- the completion manifest and final evidence ZIP when using `prenovelty-complete`.

Artifacts are retained for 90 days. Before expiry, copy the accepted ZIP, frozen bundle,
workflow URL, commit SHA, and configuration to the thesis evidence archive.

GTX 1660 projection remains theoretical until a real target-hardware run passes the same
contract.
