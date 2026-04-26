# Step 0 Complete Auto Profile / Lightweight Setting Check

This package implements the full Step 0 preparation stage before GPU-Resident Strand Runtime work.

Step 0 goal:

1. Always create `profile_results/` from Visual Studio build without launching a blocked EXE.
2. Create timestamped preset folders for bottleneck isolation.
3. Record build metadata, preset metadata, result schema, and policy status.
4. Keep a real benchmark path available for machines where `HairSystem.exe` is allowed.
5. Avoid PowerShell, BAT, and newly built EXE execution in the default Visual Studio build path.

## Default Visual Studio behavior

Use only:

```text
Build > Build Solution
```

Do not press F5 / Local Windows Debugger if Windows Application Control blocks the executable.

Default behavior uses only MSBuild built-in tasks:

- `MakeDir`
- `WriteLinesToFile`

It creates:

```text
profile_results/
  LATEST.txt
  yyyyMMdd_HHmmss_STEP0_COMPLETE_POLICY_SAFE_<Project>_<Config>_<Platform>/
    AUTO_PROFILE_STARTED.txt
    BUILD_CONDITION.json
    PROFILE_MANIFEST.json
    PROFILE_SCHEMA.csv
    RESULT.md
    RESULT.csv
    WINDOWS_POLICY_NOTE.txt
    REAL_MEASUREMENT_ENABLE_STEPS.md
    00_current/
    01_no_readback/
    02_no_self_collision/
    03_low_solver/
    04_no_debug_draw/
    05_render_light/
    06_simulation_light/
```

## Why real FPS may still be unavailable

If Windows shows:

```text
Application Control policy blocked this file.
```

then no code can measure real FPS from that machine until the executable is allowed. This package therefore separates:

- policy-safe result generation, which always runs during build;
- real viewer execution, which only works after Windows policy allows the EXE.

## Real benchmark mode after the EXE is allowed

The viewer already supports:

```text
HAIRGPU_BENCHMARK=1
HAIRGPU_BENCHMARK_WARMUP=120
HAIRGPU_BENCHMARK_FRAMES=600
HAIRGPU_AUTO_TELEMETRY=1
HAIRGPU_TELEMETRY_DIR=<preset output directory>
HAIRGPU_TELEMETRY_RUN_NAME=<preset name>
```

When execution is allowed, use the native runner or run the viewer with these variables.

The required CSV columns for Step 0 diagnosis are already exported by `GpuHairViewer::ExportPerformanceCsv`.

## Diagnosis rule

Compare `00_current` against each preset:

- big improvement in `01_no_readback` means CPU/GPU readback or sync is a bottleneck;
- big improvement in `02_no_self_collision` means self collision/broadphase is a bottleneck;
- big improvement in `03_low_solver` means solver iteration count is a bottleneck;
- big improvement in `05_render_light` means rendering/overdraw/hair draw is a bottleneck;
- big improvement in `06_simulation_light` means the lower-bound draw path is much lighter than current simulation.

A practical threshold:

```text
>= 5 ms improvement: suspicious
>= 10 ms improvement: strong bottleneck candidate
>= 20 ms improvement: primary bottleneck candidate
```
