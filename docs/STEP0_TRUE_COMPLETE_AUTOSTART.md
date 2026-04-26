# Step0 True Complete Autostart

This package changes Step0 so that the viewer does not depend on a pending file being found.

## Acceptance criteria

Step0 is considered complete only when the executable actually runs and creates:

- `RESULT_REAL.md`
- `RESULT_REAL.csv`
- non-empty `*_frames.csv` files for `00_current` through `06_simulation_light`

Build-only `RESULT.md` is not the measurement result.

## Runtime behavior

When `HairSystem.exe` starts, it searches for:

- `PENDING_STEP0_REAL.ini`
- `STEP0_FORCE_RUN.ini`
- latest Step0 run folders under `profile_results`

If no pending file is found, the viewer now creates an automatic run directory next to the executable:

```text
bin/<Config>/HairSystem/profile_results/<timestamp>_STEP0_REAL_AUTOSTART/
```

It writes:

- `PENDING_STEP0_REAL.ini`
- `STEP0_AUTOSTART_CREATED.txt`
- `STEP0_PENDING_SEARCH_LOG.txt`

Then it immediately enters Step0 real measurement.

## Presets measured

- `00_current`
- `01_no_readback`
- `02_no_self_collision`
- `03_low_solver`
- `04_no_debug_draw`
- `05_render_light`
- `06_simulation_light`

## Disable autostart

Set this only for normal manual viewing:

```bat
set HAIRGPU_DISABLE_STEP0_REAL=1
```

## If no files appear

If none of the following files appear after launching `HairSystem.exe`:

- `STEP0_RUNTIME_BOOT.txt`
- `REAL_MEASUREMENT_STARTED.txt`
- `STEP0_AUTOSTART_CREATED.txt`
- `STEP0_PENDING_SEARCH_LOG.txt`

then `HairSystem.exe` did not reach the viewer code. In that case Windows Application Control / AppLocker / WDAC is blocking the executable before Step0 code can run.
