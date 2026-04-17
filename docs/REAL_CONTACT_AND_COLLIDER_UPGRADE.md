# Real Contact and Collider Upgrade

This upgrade moves the hybrid runtime closer to a production-style corrective pipeline.

## Implemented

- Added runtime support for extra collider primitives:
  - plane
  - AABB / box
  - sampled SDF grids
- Unified corrective-physics collision resolution through one collider-set path.
- Upgraded guide contact scoring to read from the full collider set instead of only sphere/capsule proxy tests.
- Added guide penetration scoring derived from actual corrective penetration depth.
- Upgraded uncertainty to combine:
  - temporal motion
  - rest displacement
  - contact score
  - penetration score
  - root/model error score
  - smoothed carry-over from the previous frame
- Added bundle parameters:
  - `penetrationGain`
  - `temporalCoherenceGain`
  - `uncertaintySmoothingAlpha`
- Persisted the new bundle parameters in `hybrid_bundle.cfg` and `hybrid_bundle_summary.json`.
- Extended frame CSV outputs for teacher/runtime with guide contact / penetration / uncertainty aggregates.
- Added CLI-side collider config loading with `--colliders`.

## Verified path

The following flow was built and exercised successfully on the CMake/Linux path:

1. `hybrid-teacher-dataset`
2. `hybrid-train-bundle`
3. `hybrid-run-bundle`

## Still out of scope for this patch

- Alembic SDK-backed `.abc` import/export
- DX11 shader-side collider parity for new collider types
- GPU cluster compaction / indirect dispatch
- full neural prior / temporal residual networks beyond the current reduced-model path
- mesh-to-SDF baking toolchain
