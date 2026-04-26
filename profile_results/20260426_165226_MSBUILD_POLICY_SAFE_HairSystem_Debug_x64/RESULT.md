# HairGPU Policy-Safe Profile Results
CreatedAt: 2026-04-26 16:52:26
Timestamp: 20260426_165226
RunDir: C:\Users\kyose\source\repos\HairSystem\profile_results\20260426_165226_MSBUILD_POLICY_SAFE_HairSystem_Debug_x64
profile_results creation: PASS
Visual Studio/MSBuild build hook: PASS
PowerShell: NOT_USED
BAT: NOT_USED
local runner EXE: NOT_USED
HairSystem.exe execution: SKIPPED_BY_DEFAULT
real FPS measurement: NOT_AVAILABLE_UNTIL_WINDOWS_POLICY_ALLOWS_THE_EXE
## Why this exists
Windows is blocking the built HairSystem.exe by Application Control/AppLocker/WDAC. Therefore this build hook intentionally does not launch any newly built local executable. It only creates the expected profile_results folder and placeholder result files using MSBuild built-in file tasks.
## Output naming
Each build creates a timestamped folder using yyyyMMdd_HHmmss, for example 20260426_153012_MSBUILD_POLICY_SAFE_HairSystem_Debug_x64.
## Next step for real profiling
Allow the built HairSystem.exe by Windows policy, or run on an unrestricted PC. After the EXE is allowed, enable the real benchmark mode and run the viewer with HAIRGPU_BENCHMARK=1.
