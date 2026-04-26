# HairGPU Policy-Safe Visual Studio Profile Results

This version avoids launching `HairSystem.exe` during Post-Build.

Some Windows environments use Application Control, AppLocker, or WDAC and block newly built local EXE files. When that happens, Visual Studio can show:

```text
Application Control policy blocked this file.
```

If the executable is blocked, no code can collect real FPS/GPU timing because the viewer cannot run. This package therefore guarantees that `profile_results/` is created by MSBuild/CMake without executing the viewer.

## What Visual Studio does after build

After `hairgpu_dx11_viewer` builds successfully, CMake runs:

```text
cmake -P tools/profiling/HairGpuCreateProfileResults.cmake
```

This creates:

```text
profile_results/
  LATEST.txt
  yyyyMMdd_HHmmss_<Config>_<Platform>_policy_safe/
    AUTO_PROFILE_STARTED.txt
    00_current/
    01_no_readback/
    02_no_self_collision/
    03_low_solver/
    04_no_debug_draw/
    05_render_light/
    RESULT.md
    RESULT.csv
    WINDOWS_POLICY_NOTE.txt
```

## Important limitation

Real FPS values are only possible when `HairSystem.exe` is allowed to run by Windows policy. Until then, `RESULT.md` reports `NOT_AVAILABLE_UNTIL_VIEWER_EXECUTION_IS_ALLOWED`.

## What not to press

Do not use `Local Windows Debugger` while the policy blocks the EXE. That button tries to start `HairSystem.exe` and Windows will block it again.

Use:

```text
Build > Build Solution
```

Then open `profile_results/LATEST.txt`.
