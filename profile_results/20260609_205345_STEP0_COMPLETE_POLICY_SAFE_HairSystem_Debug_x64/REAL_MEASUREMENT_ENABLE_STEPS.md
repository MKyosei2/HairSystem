# Enabling real Step 0 FPS measurement
The policy-safe build step has created the full result structure. Actual FPS/GPU timings require HairSystem.exe to run.
## Required condition
Windows Application Control / AppLocker / WDAC must allow the built executable under `C:\Users\kyose\source\repos\HairSystem\bin\Debug\HairSystem\HairSystem.exe`.
## Environment used by the viewer benchmark path
`HAIRGPU_BENCHMARK=1`
`HAIRGPU_BENCHMARK_WARMUP=120`
`HAIRGPU_BENCHMARK_FRAMES=600`
`HAIRGPU_AUTO_TELEMETRY=1`
`HAIRGPU_TELEMETRY_DIR=<preset_folder>`
`HAIRGPU_TELEMETRY_RUN_NAME=<preset_name>`
## Presets
00_current, 01_no_readback, 02_no_self_collision, 03_low_solver, 04_no_debug_draw, 05_render_light, 06_simulation_light.
## Diagnosis thresholds
>= 5 ms improvement: suspicious
>= 10 ms improvement: strong bottleneck candidate
>= 20 ms improvement: primary bottleneck candidate.
