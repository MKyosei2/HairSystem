# Step0 unfinished preset completion fix

This version changes Step0 from a long benchmark to a quick-complete diagnostic pass by default.

## Default policy

- warmupFrames = 0
- measureFrames = 8 per preset
- all seven presets are attempted in one HairSystem.exe launch
- frames.csv is flushed every frame
- DEBUG_STOP_MEASUREMENT.md/csv is updated while running

## Why

Previous results captured 00_current, 01_no_readback, and part of 02_no_self_collision, but later presets such as low_solver, no_debug_draw, render_light, and simulation_light often did not run before manual debug stop. Step0 exists to compare every diagnostic preset, so this version prioritizes finishing the preset matrix quickly.

## Full-length mode

After the quick pass works, use either of these for longer measurements:

- set MSBuild property `HairGpuStep0MeasureFrames=600`
- set runtime environment variable `HAIRGPU_STEP0_FULL_LENGTH=1`

## Completion condition

Step0 is complete when `RESULT_REAL.md`, `RESULT_REAL.csv`, and each preset `*_frames.csv` contains measured rows. If the viewer is stopped early, use `DEBUG_STOP_MEASUREMENT.md` and `RESULT_REAL_PARTIAL.md`.
