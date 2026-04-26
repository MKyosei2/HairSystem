# HairGPU Auto Profile

This folder adds automatic profiling for the DX11 viewer.

## What happens after build

When `hairgpu_dx11_viewer` is built through CMake on Windows, the post-build step launches the viewer in benchmark mode and runs the presets in `profile_presets.json`.

The viewer automatically:

1. warms up for `HAIRGPU_PROFILE_WARMUP_FRAMES` frames, default `120`,
2. measures `HAIRGPU_PROFILE_MEASURE_FRAMES` frames, default `600`,
3. exports JSON and CSV telemetry,
4. closes itself,
5. runs `Analyze-HairGpuProfile.ps1`,
6. writes `profile_results/<timestamp>_<config>_<platform>/RESULT.md` and `RESULT.csv`.

## How to skip post-build profiling

Set this environment variable before building:

```bat
set HAIRGPU_DISABLE_AUTO_PROFILE_POST_BUILD=1
```

## Useful variables

```bat
set HAIRGPU_PROFILE_WARMUP_FRAMES=120
set HAIRGPU_PROFILE_MEASURE_FRAMES=600
set HAIRGPU_PROFILE_TIMEOUT_SECONDS=180
```

## Manual run

```bat
tools\profiling\Run_AutoProfile_AfterBuild.bat path\to\hairgpu_dx11_viewer.exe
```

## Presets

- `00_current`: current default state
- `01_no_readback`: CPU readback off
- `02_no_self_collision`: self collision off
- `03_low_solver`: solver iterations = 3
- `04_no_debug_draw`: debug drawing off
- `05_render_light`: lighter render path

Use the difference from `00_current` to locate the dominant bottleneck.
