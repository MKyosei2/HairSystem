# MSBuild policy-safe profile_results generation

This version creates `profile_results/` from Visual Studio build without launching any newly built local executable.

It is intended for PCs where Windows Application Control/AppLocker/WDAC blocks `HairSystem.exe`.

## What changed in this version

Each build now writes a timestamp into both the output folder name and the result files.

Folder format:

```txt
profile_results/
  20260426_153012_MSBUILD_POLICY_SAFE_<Project>_<Configuration>_<Platform>/
```

The same timestamp is also written to:

```txt
LATEST.txt
AUTO_PROFILE_STARTED.txt
RESULT.md
RESULT.csv
*/RUN_STARTED.txt
```

## How it works

A top-level `Directory.Build.targets` is added to the repository root.
MSBuild imports this file automatically for projects under the repository.
After a successful build, it uses only built-in MSBuild tasks:

- `MakeDir`
- `WriteLinesToFile`

It does not run:

- `HairSystem.exe`
- `hairgpu_auto_profile_runner.exe`
- PowerShell
- batch files
- downloaded tools

## Visual Studio operation

Use only:

- Build > Build Solution
- or right-click project > Build

Do not press Local Windows Debugger / F5 while the policy blocks the EXE. F5 explicitly starts `HairSystem.exe`, so Windows will show the same block dialog again.

## Output

After build:

```txt
profile_results/
  LATEST.txt
  20260426_153012_MSBUILD_POLICY_SAFE_<Project>_<Configuration>_<Platform>/
    AUTO_PROFILE_STARTED.txt
    WINDOWS_POLICY_NOTE.txt
    RESULT.md
    RESULT.csv
    00_current/
    01_no_readback/
    02_no_self_collision/
    03_low_solver/
    04_no_debug_draw/
    05_render_light/
```

## Real FPS measurement

This policy-safe mode cannot measure real FPS because Windows is blocking the actual viewer EXE. Real measurements require the EXE to be allowed by policy or running on an unrestricted PC.
