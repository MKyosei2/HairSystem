# Pre-Novelty Baseline Collection

This path is for collecting a reproducible **development baseline before Novelty 01 is enabled**. It intentionally uses the existing Step0 real-measurement path so data collection can start immediately without waiting for the formal frozen-asset suite.

## One-command run

Open a Visual Studio 2022 Developer Command Prompt at the repository root and run:

```bat
git switch Novelty
git pull origin Novelty
tools\final_stage\Run_PreNovelty_Baseline_Today.bat 3 600 6000
```

Arguments are:

```text
Run_PreNovelty_Baseline_Today.bat <repetitions> <warmup frames> <measurement frames>
```

The default is three repetitions, 600 warm-up frames, and 6000 measured frames per Step0 preset.

## What the runner does

1. Builds `HairSystem.sln` as Release x64.
2. Explicitly disables the Novelty 01 / Segment Group feature flags and enables the no-novelty baseline flag.
3. Arms the existing Step0 real-measurement sequence.
4. Launches `bin\Release\HairSystem\HairSystem.exe` and waits for completion.
5. Verifies every preset has real positive frame samples, no fatal failure, no NaN state, and no infinite state.
6. Stops immediately if a repetition produced only placeholders, empty CSV files, or incomplete output.
7. Repeats the run and writes a session-level aggregate.

The Step0 presets are:

- `00_current`
- `01_no_readback`
- `02_no_self_collision`
- `03_low_solver`
- `04_no_debug_draw`
- `05_render_light`
- `06_simulation_light`

All presets inherit the explicit no-novelty feature state. The additional presets are diagnostic ablations for identifying readback, collision, solver, debug-render, render, and simulation bottlenecks.

## Output

Each real run remains under:

```text
profile_results/<timestamp>_STEP0_COMPLETE_POLICY_SAFE_HairSystem_Release_x64/
```

Validation files are added inside each run directory:

```text
PRENOVELTY_VALIDATION.json
PRENOVELTY_VALIDATION.md
```

The complete session is indexed under:

```text
profile_results/PRENOVELTY_SESSION_<timestamp>/
```

Important files:

```text
PRENOVELTY_SESSION_SUMMARY.json
PRENOVELTY_SESSION_SUMMARY.csv
PRENOVELTY_BASELINE_COMPLETE.txt
SOURCE_COMMIT.txt
SOURCE_WORKTREE_STATUS.txt
run_directories.txt
```

## Machine preparation

Before starting a long unattended run:

- connect AC power;
- select the Windows high-performance power mode;
- close browsers, game launchers, recording tools, and GPU overlays;
- disable sleep and automatic restart temporarily;
- ensure the locally built executable is allowed by Windows Application Control, AppLocker, WDAC, antivirus, and Smart App Control;
- do not use the machine during measurement.

## Evidence status

These results are intended to preserve the pre-implementation baseline immediately. They are marked `paperEvidenceEligible=false` because the formal 100k asset is not yet frozen and the full scenario matrix is not used.

Do not delete the data. After the formal asset is frozen, rerun B0 with the formal suite. The same commit and Step0 results remain useful for debugging, regression analysis, and explaining why the final formal configuration was selected.
