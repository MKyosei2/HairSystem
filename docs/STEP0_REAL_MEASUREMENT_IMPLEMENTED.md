# Step 0 Real Measurement Implementation

This version completes the remaining Step 0 profiling path.

## What changed

The previous policy-safe build target only created `profile_results/` without launching `HairSystem.exe`. That proved MSBuild output generation worked, but it could not measure FPS.

This version keeps the policy-safe build behavior, but additionally creates:

```text
profile_results/PENDING_STEP0_REAL.ini
```

When `HairSystem.exe` is launched normally, the viewer detects this pending file and runs the real Step 0 measurement internally.

## Runtime behavior

On startup, the viewer searches for `profile_results/PENDING_STEP0_REAL.ini` in the current directory and ancestor directories. If found, it enters Step 0 real benchmark mode.

The viewer then runs these presets in a single process:

1. `00_current`
2. `01_no_readback`
3. `02_no_self_collision`
4. `03_low_solver`
5. `04_no_debug_draw`
6. `05_render_light`
7. `06_simulation_light`

Each preset uses the configured warmup and measurement frame counts. After each preset, the viewer writes real CSV/JSON output into that preset folder.

After all presets finish, the viewer writes:

```text
RESULT_REAL.csv
RESULT_REAL.md
REAL_MEASUREMENT_STARTED.txt
PENDING_STEP0_REAL.completed.ini
```

## Important limitation

Real FPS measurement still requires `HairSystem.exe` itself to be allowed by Windows policy. This implementation no longer starts the EXE from MSBuild or a post-build runner. The build only creates the pending file. The real measurement happens only after the user launches the viewer normally.

If Windows Application Control / AppLocker / WDAC blocks `HairSystem.exe`, no code-only solution can measure real FPS until the executable is allowed.

## How to use

1. In Visual Studio, run `Build > Build Solution`.
2. Confirm that `profile_results/PENDING_STEP0_REAL.ini` exists.
3. Launch `HairSystem.exe` normally.
4. Wait until the viewer runs all presets and closes.
5. Open `profile_results/<timestamp>_STEP0_COMPLETE_.../RESULT_REAL.md`.

## Bottleneck interpretation

The viewer compares `00_current` with the other presets:

- If `01_no_readback` improves frame time: readback / CPU-GPU synchronization is heavy.
- If `02_no_self_collision` improves frame time: self collision is heavy.
- If `03_low_solver` improves frame time: solver iterations are heavy.
- If `04_no_debug_draw` improves frame time: debug visualization is heavy.
- If `05_render_light` improves frame time: hair drawing / overdraw is heavy.
- If `06_simulation_light` improves frame time: combined simulation-side cost dominates.
