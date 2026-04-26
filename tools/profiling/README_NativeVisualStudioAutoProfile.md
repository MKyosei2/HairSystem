# HairGPU Auto Profile: Native Visual Studio Runner

This version uses a native C++ post-build executable, `hairgpu_auto_profile_runner.exe`.
It does not execute PowerShell, so it is not affected by PowerShell ExecutionPolicy,
script blocking, Constrained Language Mode, or Mark-of-the-Web script restrictions.

When `hairgpu_dx11_viewer` finishes building in Visual Studio/CMake, the post-build step runs:

```text
hairgpu_auto_profile_runner.exe --exe <viewer.exe> --source-dir <repo>
```

The runner creates `profile_results/` before launching the viewer. Therefore the folder is created even if the viewer fails to start, times out, or does not export telemetry.

Default benchmark settings:

```text
HAIRGPU_PROFILE_WARMUP_FRAMES=30
HAIRGPU_PROFILE_MEASURE_FRAMES=180
HAIRGPU_PROFILE_TIMEOUT_SECONDS=90
```

Override them as environment variables before building if needed.

To skip post-build profiling:

```text
set HAIRGPU_DISABLE_AUTO_PROFILE_POST_BUILD=1
```

Expected output:

```text
profile_results/
  LATEST.txt
  yyyyMMdd_HHmmss_<Config>_<Platform>/
    AUTO_PROFILE_STARTED.txt
    00_current/
    01_no_readback/
    02_no_self_collision/
    03_low_solver/
    04_no_debug_draw/
    05_render_light/
    RESULT.md
    RESULT.csv
```
