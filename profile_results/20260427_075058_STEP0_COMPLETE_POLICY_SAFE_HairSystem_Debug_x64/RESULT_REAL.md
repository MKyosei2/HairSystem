# HairGPU Step 0 Real Measurement Result

Viewer execution: COMPLETED
Real FPS measurement: AVAILABLE
WarmupFrames: 0
MeasureFrames: 8

## Summary

| Preset | Avg FPS | Avg Frame ms | P95 Frame ms | Worst Frame ms | Avg Sim ms | Avg GPU Frame ms | Avg GPU Hair ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| 00_current | 2.01563 | 496.124 | 1085.91 | 2307 | 96.657 | 147.903 | 19.9356 |
| 01_no_readback | 1.57556 | 634.695 | 293.839 | 3902.26 | 93.1551 | 624.882 | 52.585 |
| 02_no_self_collision | 1.51304 | 660.921 | 242.548 | 4452.05 | 58.605 | 688.592 | 66.2413 |
| 03_low_solver | 2.05197 | 487.337 | 99.3086 | 3450.22 | 14.1899 | 108.17 | 32.626 |
| 04_no_debug_draw | 2.3154 | 431.892 | 118.719 | 2849.18 | 21.1125 | 2118.18 | 121.996 |
| 05_render_light | 2.34413 | 426.597 | 127.789 | 2867.17 | 19.3228 | 2072.33 | 142.117 |
| 06_simulation_light | 2.61441 | 382.495 | 170.286 | 2513.59 | 24.5118 | 1853.79 | 109.705 |
| 06_simulation_light | 2.87289 | 348.082 | 170.286 | 2513.59 | 22.8015 | 1919 | 112.597 |

## Automatic bottleneck guess

Baseline average frame ms: 496.124

Largest frame-time reduction preset: **06_simulation_light**

Frame-time reduction: 148.042 ms

Interpretation:

- 01_no_readback wins: GPU to CPU readback / synchronization is the main bottleneck.
- 02_no_self_collision wins: self collision / broadphase / solve is the main bottleneck.
- 03_low_solver wins: solver iteration count is the main bottleneck.
- 04_no_debug_draw wins: debug visualization is the main bottleneck.
- 05_render_light wins: hair draw / overdraw / render path is the main bottleneck.
- 06_simulation_light wins: combined simulation-side cost dominates.

Severity: PRIMARY BOTTLENECK CANDIDATE
