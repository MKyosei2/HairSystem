# Step0 True Partial Measurement Fix

This version changes Step0 from a delayed benchmark to an immediate, crash-tolerant measurement pipeline.

## What changed

- Step0 defaults to `warmupFrames=0` and `measureFrames<=60` unless `HAIRGPU_STEP0_FULL_LENGTH=1` is set.
- The viewer flushes `*_frames.csv` every frame.
- The viewer writes `STEP0_HEARTBEAT.txt` every frame.
- The viewer writes `RESULT_REAL_PARTIAL.md` while running.
- If the viewer exits before all presets complete, it writes `STEP0_ABORTED_OR_CLOSED_BEFORE_COMPLETE.md`.
- A run no longer needs to survive 120 warmup frames before any useful data appears.

## Completion criteria

Full Step0 completion:

- `RESULT_REAL.md`
- `RESULT_REAL.csv`
- measured rows in all preset `*_frames.csv` files

Partial Step0 completion:

- `RESULT_REAL_PARTIAL.md`
- at least one preset `*_frames.csv` with measured rows
- `STEP0_HEARTBEAT.txt`

If none of those appear, `HairSystem.exe` did not reach the viewer frame loop.
