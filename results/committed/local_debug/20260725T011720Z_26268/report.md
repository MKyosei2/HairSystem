# HairSystem local debugger capture

> **Evidence class:** `diagnostic_debugger`  
> This F5/local-debugger result is diagnostic only. Formal paper evidence must be produced by `Run_Final_Runtime_Suite.bat` with the frozen asset and strict environment gate.

## Run identity

- Run: `local_debug`
- Captured frames: 582
- Viewer exit code: 0
- Branch: ``
- Source commit before data commit: ``
- Git available: `False`
- Working tree dirty at report time: `None`

## Performance

| Metric | Mean | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|
| Frame time (ms) | 11.890 | 14.868 | 16.666 | 325.932 |
| Simulation (ms) | 0.480 | 0.578 | 0.707 | 76.393 |
| GPU hair (ms) | 0.493 | 2.504 | 11.506 | 12.342 |
| FPS | 85.366 | 99.993 | 99.993 | 876.578 |

GPU timing non-zero coverage: `6.357%`

## Per-second realtime evidence

| Metric | Mean | Minimum | p95 | Maximum |
|---|---:|---:|---:|---:|
| Render FPS | 84.084 | 55.702 | 91.894 | 92.154 |
| Simulation Hz | 55.913 | 37.473 | 60.601 | 60.858 |
| GPU throughput FPS | 173.747 | 93.826 | 224.463 | 226.261 |
| Dropped simulation ms | 28.571 | 0.000 | 113.333 | 133.333 |

Realtime-status counts: `{'simulation_lag': 2, 'realtime_60': 5}`

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
