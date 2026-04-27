# HairGPU Step0 Debug Stop Measurement

This file is written continuously while Step0 is running and again when the viewer exits gracefully.
Use this file when Visual Studio debugging is stopped before all presets complete.

Reason: `run_loop_exit_or_debug_stop`

Timestamp: `20260427_075239`

Current preset: `06_simulation_light`

Preset index: 9 / 7

Sample count in current preset: 9

## Current preset summary

| Metric | Value |
|---|---:|
| Avg FPS | 2.87289 |
| Avg frame ms | 348.082 |
| P95 frame ms | 170.286 |
| Worst frame ms | 2513.59 |
| Avg simulation ms | 22.8015 |
| Avg GPU frame ms | 1919 |
| Avg GPU simulation ms | 1721.32 |
| Avg GPU hair draw ms | 112.597 |
| Avg ActiveSet ms | 0 |
| Readback frames | 0 |
| Avg readback particles | 0 |
| Avg hair primitives | 2.5908e+07 |

## Latest frame

| Metric | Value |
|---|---:|
| Frame ms | 72.7771 |
| FPS | 13.7406 |
| Simulation ms | 9.119 |
| GPU frame ms | 2440.65 |
| GPU simulation ms | 2211.28 |
| GPU hair draw ms | 135.731 |
| CPU readback mode | 0 |
| Self collision | 0 |
| Solver iterations | 3 |
| Hair primitives | 25908000 |

## How to use this file

- If `RESULT_REAL.md` exists, use that as the complete Step0 result.
- If `RESULT_REAL.md` is missing but this file has samples, Step0 partially measured the current preset.
- If both this file and the preset frames CSV have zero samples, the viewer did not reach the frame recording point.
