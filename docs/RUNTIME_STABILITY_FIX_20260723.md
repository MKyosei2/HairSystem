# Hair GPU Viewer runtime stability repair (2026-07-23)

## Corrected failure paths

1. Interactive startup now applies the conservative `Stable Benchmark Wig` values before GPU resources are created.
2. The old count-only asset cache is invalidated. A cached asset is accepted only after structural and benchmark-wig shape validation.
3. Interactive rendering is paced to the 60 Hz fixed solver cadence and DXGI maximum frame latency is one. Formal throughput runs remain unpaced.
4. A wall-clock discontinuity of 50 ms or more zeros Verlet velocity before one fixed recovery tick.
5. Follower reconstruction uses continuous guide-local deformation transfer instead of tangent-to-tangent half-turn rotation.

## Release x64 verification

- Delete is unnecessary: the new cache file uses `sample_hybrid_cache_v4_stable_roots_*`.
- Start the Viewer and confirm the title reports `preset=Stable Benchmark Wig`.
- At idle, `renderFPS` and `simHz` should remain close to 60, not the monitor refresh rate.
- `droppedSimMs` should stop growing during normal playback. A debugger break may increase it once, but the hair should resume without exploding.
- Rotate the camera and run for at least 20 minutes. The crown should retain follower coverage and no radial fan/slab pose should appear.
- Run the formal smoke and long-run suites after the visual check.

## Evidence limitation

The existing `nanStateCount` and `infiniteStateCount` fields are exported but are not yet wired to a dedicated asynchronous GPU invalid-state counter. The stability guard still repairs invalid guides in interactive mode; formal evidence should not claim measured NaN/Infinity coverage until that counter is implemented.
