# HairGPU Step 0 Complete Profile / Lightweight Setting Check
CreatedAt: 2026-06-24 18:09:42
Timestamp: 20260624_180942
RunDir: C:\Users\kyose\source\repos\HairSystem\profile_results\20260624_180942_STEP0_COMPLETE_POLICY_SAFE_HairSystem_Debug_x64
Project: HairSystem
Configuration: Debug
Platform: x64
Target: C:\Users\kyose\source\repos\HairSystem\bin\Debug\HairSystem\HairSystem.exe
## Status
profile_results creation: PASS
preset directories creation: PASS
Visual Studio/MSBuild hook: PASS
PowerShell: NOT_USED
BAT: NOT_USED
Local EXE execution during build: NOT_USED_BY_DEFAULT
Real measurement trigger files: WRITTEN_TO_REPO_AND_TARGETDIR
real FPS measurement: PENDING_UNTIL_HAIRSYSTEM_EXE_IS_LAUNCHED_AND_ALLOWED
pending file: profile_results/PENDING_STEP0_REAL.ini and C:\Users\kyose\source\repos\HairSystem\bin\Debug\HairSystem\PENDING_STEP0_REAL.ini
## What is fully implemented
The repository now has the full Step 0 folder structure, preset matrix, telemetry schema, benchmark environment contract, and policy-safe Visual Studio build hook. The viewer-side benchmark path exports frame CSV and runtime JSON when the executable is allowed to run. This build also writes PENDING_STEP0_REAL.ini, STEP0_FORCE_RUN.ini, and target-dir fallback files so startup detection does not silently fail.
## Why this file is not the real result
This RESULT.md is build-only preflight. It is NOT the Step0 measurement result. The real result is RESULT_REAL.md, which appears only after HairSystem.exe starts and completes the embedded benchmark. If it does not appear after launching HairSystem.exe, inspect STEP0_PENDING_SEARCH_LOG.txt next to the executable. If HairSystem.exe is blocked by Windows policy, true FPS measurement is impossible until the executable is allowed.
## Presets
| Preset | Purpose | Diagnosis after real run |
|---|---|---|
| 00_current | Current settings | Baseline |
| 01_no_readback | Disable readback / ActiveSet runtime | Improvement means readback or CPU/GPU sync is heavy |
| 02_no_self_collision | Disable self collision | Improvement means self collision or broadphase is heavy |
| 03_low_solver | solverIterations=3 | Improvement means solver iteration cost is heavy |
| 04_no_debug_draw | Disable debug draw | Improvement means debug visualization is heavy |
| 05_render_light | Lighter render settings | Improvement means draw/overdraw/hair rendering is heavy |
| 06_simulation_light | No readback, no self collision, low solver | Lower bound for simulation-light condition |
## Diagnosis thresholds after real run
>= 5 ms improvement: suspicious
>= 10 ms improvement: strong bottleneck candidate
>= 20 ms improvement: primary bottleneck candidate
## Next research step
Once real execution is allowed, compare 00_current to the six presets and use the largest frame-time reduction to decide whether Step 1 should prioritize readback removal, self-collision gating, solver budget control, or rendering LOD.
