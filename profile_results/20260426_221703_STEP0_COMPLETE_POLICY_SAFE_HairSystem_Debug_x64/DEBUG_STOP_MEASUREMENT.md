# HairGPU Step0 Debug Stop Measurement

This file is written continuously while Step0 is running and again when the viewer exits gracefully.
Use this file when Visual Studio debugging is stopped before all presets complete.

Reason: `live_update_debug_stop_safe`

Timestamp: `20260426_221855`

Current preset: `02_no_self_collision`

Preset index: 3 / 7

Sample count in current preset: 17

## Current preset summary

| Metric | Value |
|---|---:|
| Avg FPS | 2.98738 |
| Avg frame ms | 334.741 |
| P95 frame ms | 115.585 |
| Worst frame ms | 4289.05 |
| Avg simulation ms | 15.0742 |
| Avg GPU frame ms | 3900.06 |
| Avg GPU simulation ms | 3393.32 |
| Avg GPU hair draw ms | 147.432 |
| Avg ActiveSet ms | 0 |
| Readback frames | 17 |
| Avg readback particles | 1.3056e+07 |
| Avg hair primitives | 2.5908e+07 |

## Latest frame

| Metric | Value |
|---|---:|
| Frame ms | 105.481 |
| FPS | 9.48039 |
| Simulation ms | 15.622 |
| GPU frame ms | 4391.82 |
| GPU simulation ms | 3832.56 |
| GPU hair draw ms | 162.773 |
| CPU readback mode | 1 |
| Self collision | 0 |
| Solver iterations | 12 |
| Hair primitives | 25908000 |

## How to use this file

- If `RESULT_REAL.md` exists, use that as the complete Step0 result.
- If `RESULT_REAL.md` is missing but this file has samples, Step0 partially measured the current preset.
- If both this file and the preset frames CSV have zero samples, the viewer did not reach the frame recording point.
