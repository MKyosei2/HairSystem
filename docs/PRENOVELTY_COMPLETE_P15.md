# Pre-novelty complete P15 acceptance flow

## Target

- Repository: `MKyosei2/HairSystem`
- Base branch: `prenovelty-ready`
- Base commit: `8dcc8300b5db99b9f549e8318b2cdc3d88d01100` (`P14`)
- Workload: Release x64, 1920x1080, fixed 1/60 s, VSync off, 4,000 guides,
  24 followers per guide, 100,000 displayed strands, 128 particles, 6 iterations.

## One command

```bat
set HAIRGPU_FREEZE_OPERATOR=Your_Name

tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  prenovelty-complete ^
  out\prenovelty_complete
```

`HAIRGPU_FREEZE_OPERATOR` is required only when the frozen bundle does not exist yet or when
an explicit `refreeze-asset` is requested. An existing locked bundle is verified and reused;
it is never regenerated implicitly by `prenovelty-complete`.

## Acceptance phases

1. `smoke`: 12 B0/B6/B8 representative runs.
2. `startup20`: 20 real Viewer start/render/WM_CLOSE repetitions.
3. `coverage18`: B0/B6/B8 across all 18 scenarios at seed 1103, exactly 54 runs.
4. `determinism`: configured deterministic replay matrix.
5. `soak30`: four configured 30-minute resource and frame-stability runs.
6. Detailed database import, Excel export, SVG graph generation, and immutable packaging.

Every selected run must be complete, valid, and pass the effective pre-novelty quality gate.
Any `_ERROR`, `_INCOMPLETE`, `_RUNNING`, or `.run.lock` marker rejects the package.

## Frozen Asset policy

`prepare-asset` now means **ensure and verify**:

- When no bundle exists, it is generated once and locked.
- When a lock exists, file names, sizes, manifest SHA-256, and bundle-tree SHA-256 must match.
- Regeneration requires the explicit command below.

```bat
set HAIRGPU_FREEZE_OPERATOR=Your_Name
tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\prenovelty_runtime_suite.json ^
  refreeze-asset
```

## Final outputs

```text
out/prenovelty_complete/
├─ smoke/
├─ startup20/
├─ coverage18/
├─ determinism/
├─ soak30/
├─ analysis/
│  ├─ validated_evidence.sqlite
│  ├─ formal_results.xlsx
│  ├─ formal_report.md
│  ├─ postprocess_manifest.json
│  └─ graphs/
│     ├─ method_average_fps.svg
│     ├─ scenario_p99_frame_ms.svg
│     └─ method_average_hair_gpu_ms.svg
├─ frozen_asset_bundle_snapshot.zip
├─ frozen_asset_bundle_snapshot.zip.sha256
├─ complete_manifest.json
├─ prenovelty_complete_evidence.zip
└─ prenovelty_complete_evidence.zip.sha256
```

The accepted package requires `complete_manifest.json` to contain:

```json
{
  "status": "COMPLETE",
  "passed": true
}
```

## GTX 1660 statement

The RTX 4070 Ti SUPER to GTX 1660 result remains a conservative theoretical projection.
It is not physical GTX 1660 evidence. The thesis should state this limitation explicitly and
list execution of the same frozen workload and complete contract on a physical GTX 1660-class
machine as future work.
