# Stage 8: ABC compatibility export and external-training interop

This stage closes the biggest remaining gaps that are still realistically implementable in the current Linux/no-Alembic-SDK environment.

## Added in this stage

### 1) `.abc` compatibility export
- `SaveHairAssetToAlembic(...)` no longer hard-fails in non-SDK builds.
- In non-`HAIRGPU_WITH_ALEMBIC` builds it now writes:
  - a USDA-compatible payload directly into the `.abc` path
  - a `.hairbin` sidecar (`<file>.abc.hairbin`)
  - a `.compat.json` manifest (`<file>.abc.compat.json`)
- Because the `.abc` loader already sniffs USDA / hairbin / CSV payloads, this gives a practical round-trip path for interchange/debugging.

### 2) Temporal network CSV interop
Added:
- `ExportTemporalGuideNetworkToCsvDirectory(...)`
- `ImportTemporalGuideNetworkFromCsvDirectory(...)`

CLI:
- `temporal-export-csv <input.temporalbin> <output_dir>`
- `temporal-import-csv <input_dir> <output.temporalbin>`

Exported files include:
- `metadata.csv`
- `input_mean.csv`
- `input_std.csv`
- `target_mean.csv`
- `target_std.csv`
- `hidden_weights.csv`
- `hidden_bias.csv`
- `output_weights.csv`
- `output_bias.csv`

This makes the learned temporal stage editable/retrainable outside the binary serializer.

### 3) Active-set predictor CSV interop
Added:
- `ExportActiveSetPredictorToCsvDirectory(...)`
- `ImportActiveSetPredictorFromCsvDirectory(...)`

CLI:
- `active-set-predictor-export-csv <input.predictorbin> <output_dir>`
- `active-set-predictor-import-csv <input_dir> <output.predictorbin>`

Exported files:
- `metadata.csv`
- `weights.csv`
- `README.txt`

This gives a simple external-training / manual-tuning path for the learned active-set stage.

### 4) Bundle-level interop package
CLI added:
- `hybrid-bundle-export-interop <bundle_dir> <output_dir>`
- `hybrid-bundle-import-interop <base_bundle_dir> <interop_dir> <output_bundle_dir>`

This exports/imports:
- `prior_temporal_csv/`
- `residual_temporal_csv/`
- `active_set_predictor_csv/`
- `interop_manifest.json`

Import applies edited interop payloads back onto an existing bundle and saves a new bundle directory.

### 5) CLI addition for ABC compatibility export
Added:
- `asset-export-abc-compat <input> <output.abc>`

## Verification performed
Tested successfully on Linux/CMake:
1. `asset-export-abc-compat generated_scaffold_v5/asset_normalized.hairbin /mnt/data/work_v8_test/asset_compat.abc`
2. `info /mnt/data/work_v8_test/asset_compat.abc`
3. `temporal-export-csv out_bundle_stage6/prior.temporalbin /mnt/data/work_v8_test/prior_temporal_csv`
4. `temporal-import-csv /mnt/data/work_v8_test/prior_temporal_csv /mnt/data/work_v8_test/prior_roundtrip.temporalbin`
5. `active-set-predictor-export-csv /mnt/data/work_v6/test_stage7/bundle/active_set.predictorbin /mnt/data/work_v8_test/predictor_csv`
6. `active-set-predictor-import-csv /mnt/data/work_v8_test/predictor_csv /mnt/data/work_v8_test/active_set_roundtrip.predictorbin`
7. `hybrid-bundle-export-interop /mnt/data/work_v6/test_stage7/bundle /mnt/data/work_v8_test/bundle_interop`
8. `hybrid-bundle-import-interop /mnt/data/work_v6/test_stage7/bundle /mnt/data/work_v8_test/bundle_interop /mnt/data/work_v8_test/bundle_roundtrip`
9. `hybrid-bundle-info /mnt/data/work_v8_test/bundle_roundtrip`

## What is still not fully finished
These remain outside what can be honestly completed in this environment without extra SDKs/toolchains:
- true binary Alembic SDK integration (`AbcGeom` reader/writer)
- DX11-side consumption/compilation/run validation of dispatch/gpu buffers
- true GPU-generated indirect dispatch on the actual Windows/DX11 runtime path

So this stage clears the remaining **interop/export/import** work that was still practical here, but not the external-SDK and Windows-only runtime integration pieces.
