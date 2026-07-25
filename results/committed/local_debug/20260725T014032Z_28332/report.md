# HairSystem local debugger capture

> **Evidence class:** `diagnostic_debugger`  
> This F5/local-debugger result is diagnostic only. Formal paper evidence must be produced by `Run_Final_Runtime_Suite.bat` with the frozen asset and strict environment gate.

## Run identity

- Run: `local_debug`
- Captured frames: 811
- Viewer exit code: 0
- Branch: ``
- Source commit before data commit: ``
- Git available: `False`
- Working tree dirty at report time: `None`

## Performance

| Metric | Mean | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|
| Frame time (ms) | 11.728 | 14.418 | 18.931 | 291.018 |
| Simulation (ms) | 0.202 | 0.589 | 0.828 | 1.072 |
| GPU hair (ms) | 0.471 | 2.818 | 11.473 | 12.258 |
| FPS | 86.107 | 95.956 | 95.956 | 470.102 |

GPU timing non-zero coverage: `6.289%`

## Per-second realtime evidence

| Metric | Mean | Minimum | p95 | Maximum |
|---|---:|---:|---:|---:|
| Render FPS | 84.667 | 53.296 | 91.577 | 91.619 |
| Simulation Hz | 56.985 | 40.224 | 60.493 | 60.834 |
| GPU throughput FPS | 175.627 | 93.722 | 246.617 | 254.902 |
| Dropped simulation ms | 20.000 | 0.000 | 103.333 | 133.333 |

Realtime-status counts: `{'simulation_lag': 2, 'realtime_60': 8}`

## Novelty 01 diagnostics

| Metric | Mean | Maximum |
|---|---:|---:|
| Static cached groups | 0.0 | 0.0 |
| Static transformed groups | 0.0 | 0.0 |
| Dynamic groups | 0.0 | 0.0 |
| Repair-required groups | 0.0 | 0.0 |
| Static/dynamic boundary max error (mm) | n/a | 0.000 |
| False-static rate | n/a | n/a |

## Graphs

- [Frame and GPU time](frame_time.svg)
- [Per-second realtime status](per_second_realtime.svg)
- [Segment-group states](segment_group_state.svg)
- [GPU pass breakdown](gpu_pass_breakdown.svg)

## Interpretation

The proposed Novelty 01 unit is a point-line-point segment group. Static groups reuse their last valid solved result and are not recomputed until a head, collision, edit, boundary, or safety event wakes them. Dynamic groups are compacted and solved on the GPU. Shared intermediate points require directional/hemisphere ownership, and unsafe reuse must become repair or fallback rather than false-static acceptance.
