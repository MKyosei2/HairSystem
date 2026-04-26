# Step 0 Real Measurement Complete Notes

This version implements the remaining Step 0 path needed to actually separate runtime bottlenecks.

## What is implemented

- MSBuild creates `profile_results/` and timestamped Step0 run directories.
- MSBuild writes pending trigger files to:
  - repository `profile_results/PENDING_STEP0_REAL.ini`
  - executable directory `PENDING_STEP0_REAL.ini`
  - executable directory `profile_results/PENDING_STEP0_REAL.ini`
  - fallback `STEP0_FORCE_RUN.ini`
- The viewer searches for pending files from both current directory and executable directory, then walks parent directories.
- If the pending file is missing, the viewer uses `profile_results/LATEST.txt` or newest Step0 directory as a fallback and generates `PENDING_STEP0_REAL.auto.ini`.
- The viewer writes `STEP0_PENDING_SEARCH_LOG.txt` next to the executable if no pending file is found.
- Once pending is detected, the viewer writes `STEP0_RUNTIME_BOOT.txt` to the run directory.
- The viewer runs the full preset sequence internally:
  - `00_current`
  - `01_no_readback`
  - `02_no_self_collision`
  - `03_low_solver`
  - `04_no_debug_draw`
  - `05_render_light`
  - `06_simulation_light`
- The viewer exports real measured `frames.csv`, `summary.json`, `RESULT_REAL.csv`, and `RESULT_REAL.md`.

## Completion condition

Step0 is complete only when the timestamped run directory contains:

- `STEP0_RUNTIME_BOOT.txt`
- `REAL_MEASUREMENT_STARTED.txt`
- `RESULT_REAL.md`
- `RESULT_REAL.csv`
- measured frames in each preset folder

If only `RESULT.md` exists, that is still build preflight only.

## If no real measurement starts

Check the executable folder for:

- `STEP0_PENDING_SEARCH_LOG.txt`
- `STEP0_REAL_NOT_STARTED.md`

Those files prove the executable started but did not find a pending trigger.

If neither file appears, the executable itself did not start or Windows blocked it before the viewer code ran.
