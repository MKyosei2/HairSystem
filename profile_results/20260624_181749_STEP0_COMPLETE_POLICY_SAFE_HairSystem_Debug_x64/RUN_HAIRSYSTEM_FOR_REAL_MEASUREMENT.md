# Run real Step 0 measurement
This build created `profile_results/PENDING_STEP0_REAL.ini`.
Next step: launch HairSystem.exe normally, not through a post-build runner.
When the viewer starts, it detects the pending file, runs all Step0 presets internally, writes real frames.csv/summary.json for each preset, then writes RESULT_REAL.md and RESULT_REAL.csv.
If Windows policy blocks HairSystem.exe itself, real measurement is impossible until the executable is allowed.
TargetPath: `C:\Users\kyose\source\repos\HairSystem\bin\Debug\HairSystem\HairSystem.exe`
RunDir: `C:\Users\kyose\source\repos\HairSystem\profile_results\20260624_181749_STEP0_COMPLETE_POLICY_SAFE_HairSystem_Debug_x64`
WarmupFrames: 120
MeasureFrames: 600
