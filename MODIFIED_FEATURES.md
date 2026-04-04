# HairSystem patch additions (2026-04-05, advanced pass)

This patch pushes the hybrid runtime closer to the design document's completed form while staying compatible with the current codebase.

## Added / extended in this pass

### Hybrid runtime metrics and adaptive behavior
- Added runtime report fields for:
  - p99 estimated frame time
  - frame-budget hit rate
  - budget violation count
  - active-set saturation count
  - average applied residual blend
  - estimated average / peak memory usage
- Extended runtime summary JSON export with the above metrics.
- Added per-frame export columns for:
  - applied residual blend
  - estimated memory MB
- Added budget-aware adaptive residual blending so the runtime can slightly reduce residual influence when recent estimated frame cost trends above the target.

### Hybrid ablation support
- Added `HybridAblationCaseReport` and `HybridAblationReport`.
- Added `RunHybridAblationStudy(...)` to produce A/B/C/D-style comparisons:
  - prior only
  - prior + residual only
  - active-set corrective only
  - full hybrid
- Added JSON ablation output with RMS / penetration / frame-time budget statistics.

### CLI surface
- Extended `hybrid-run-bundle` console output to show:
  - p99 estimated frame time
  - budget hit rate
  - estimated peak memory MB
- Added:
  - `hairgpu_cli hybrid-run-ablation <teacher_dir> <bundle_dir> <output.json> [targetFrameMs]`

## Files changed in this pass
- `include/hairgpu/HybridRuntime.h`
- `src/hairgpu_core/HybridRuntime.cpp`
- `apps/hairgpu_cli/src/main.cpp`
- `MODIFIED_FEATURES.md`

## Smoke-tested commands
- `hybrid-teacher-dataset`
- `hybrid-train-bundle`
- `hybrid-run-bundle`
- `hybrid-run-ablation`

## Notes
This patch intentionally advances the project toward the design document's "completed" shape without pretending that every research claim has been fully proven. In particular, this pass strengthens:
- the integrated runtime feel,
- the evaluation pipeline,
- the bounded-latency reporting story,
- and the comparison tooling needed for a thesis-style presentation.
