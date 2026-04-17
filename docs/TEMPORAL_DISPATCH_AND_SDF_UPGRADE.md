# Temporal + Dispatch + SDF Toolchain Upgrade

This pass pushed three remaining areas forward without requiring external SDKs:

## 1) Mesh/SDF toolchain

New core APIs:
- `BakeSdfGridFromScalpMesh(...)`
- `ExportSdfGridColliderCsv(...)`

New CLI commands:
- `hairgpu_cli export-scalp-sdf-collider <input> <output_grid.csv> [resolution] [padding]`
- `hairgpu_cli bake-obj-sdf <scalp.obj> <output_grid.csv> [resolution] [padding]`

Why this matters:
- The old `export-scalp-sdf` path wrote visualization/debug slices only.
- The new commands emit the same dense-grid CSV format that the hybrid runtime collider loader can consume.
- This closes the gap between scalp mesh authoring and runtime SDF collision input.

## 2) Active-set dispatch / compaction export

New core APIs:
- `BuildActiveSetDispatchPlan(...)`
- `WriteActiveSetDispatchPlanJson(...)`

New CLI command:
- `hairgpu_cli active-set-dispatch <input> <output.json> [guidesPerCluster] [maxGuideRootDistance] [maxActiveClusters] [maxActiveGuides] [promoteThreshold]`

Runtime integration:
- When `hybrid-run-bundle` is run with `exportPerFrameHairBins=1`, the runtime now also emits
  `full_hybrid_dispatch_XXXX.json` files.

Why this matters:
- It is still CPU-side, not final GPU indirect dispatch.
- But it now exports a compacted per-frame dispatch shape with:
  - selected full-sim clusters
  - guide/strand offsets
  - full/reduced/frozen/cached strand lists
  - corrective strand lists
- This is the exact kind of intermediate artifact needed before a GPU indirect-dispatch implementation.

## 3) Temporal runtime prediction hooks

Bundle fields added:
- `priorVelocityPredictionGain`
- `residualVelocityPredictionGain`

Runtime changes:
- Prior guide inputs can now be forward-predicted from the previous smoothed guide state.
- Residual guide inputs can now include previous-frame guide velocity.
- These values are saved/loaded in `hybrid_bundle.cfg` and written into the summary JSON.

Notes:
- On the tiny bundled sample sequence the learned gains clamp to 0 because the residual motion is very small.
- The path is implemented and becomes active on more dynamic teacher data.

## Verified commands

The following were built and executed successfully on Linux/CMake:
- `export-scalp-sdf-collider`
- `bake-obj-sdf`
- `active-set-dispatch`
- `hybrid-run-bundle ... exportPerFrameHairBins=1`

Generated examples:
- `out_runtime_test/test_scalp_grid.csv`
- `out_runtime_test/test_scalp_grid_from_obj.csv`
- `out_runtime_test/test_dispatch.json`
- `out_runtime_test2/full_hybrid_dispatch_0000.json`
