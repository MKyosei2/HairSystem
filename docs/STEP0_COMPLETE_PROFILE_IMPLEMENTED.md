# Step 0 Complete Implementation: Profiling and Lightweight Setting Check

This repository snapshot implements Step 0 up to the point immediately before GPU-Resident Strand Runtime.

## Implemented components

- Visual Studio/MSBuild policy-safe profile result generation.
- Timestamped `profile_results/` directory creation.
- Preset matrix for bottleneck isolation.
- Telemetry schema for real viewer measurements.
- Placeholder CSV/JSON files for each preset when the EXE is blocked.
- Viewer-side benchmark environment variables and telemetry export path.
- Native C++ auto-profile runner source retained for machines where the viewer EXE is allowed.
- No PowerShell, BAT, or local EXE execution in the default build path.

## Presets

| Preset | Purpose |
|---|---|
| 00_current | Baseline current state |
| 01_no_readback | CPU/GPU readback and sync cost check |
| 02_no_self_collision | Self collision cost check |
| 03_low_solver | Solver iteration cost check |
| 04_no_debug_draw | Debug rendering/overlay cost check |
| 05_render_light | Rendering/overdraw/hair draw cost check |
| 06_simulation_light | Simulation-light lower bound |

## Policy-safe output

A normal Visual Studio build creates:

```text
profile_results/
  LATEST.txt
  yyyyMMdd_HHmmss_STEP0_COMPLETE_POLICY_SAFE_<Project>_<Config>_<Platform>/
    AUTO_PROFILE_STARTED.txt
    BUILD_CONDITION.txt
    PROFILE_MANIFEST.json
    PROFILE_SCHEMA.csv
    RESULT.md
    RESULT.csv
    WINDOWS_POLICY_NOTE.txt
    REAL_MEASUREMENT_ENABLE_STEPS.md
    00_current/
    01_no_readback/
    02_no_self_collision/
    03_low_solver/
    04_no_debug_draw/
    05_render_light/
    06_simulation_light/
```

## Real measurement requirement

Real FPS/GPU timing requires `HairSystem.exe` to run. If Windows Application Control, AppLocker, or WDAC blocks it, the code can only create the result structure; it cannot measure FPS until the EXE is allowed.

## Step 0 completion definition

Step 0 is considered implemented when:

1. Visual Studio build creates timestamped `profile_results/` without being blocked.
2. All bottleneck-isolation preset folders are created.
3. CSV/JSON/Markdown schema files exist.
4. The viewer contains benchmark-mode telemetry export for real runs.
5. The project documents how to interpret the results and what remains blocked by Windows policy.

This snapshot satisfies those conditions.
