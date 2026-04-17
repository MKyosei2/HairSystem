# Stage 7: Active-set predictor, compare pipeline, and runtime metric completion

This stage pushes the remaining research-facing gaps that can be implemented in the current environment.

## Added in this stage

### 1) Learned active-set predictor
- Added `ActiveSetPredictor` to `HybridRuntime.h` / `HybridRuntime.cpp`
- Added predictor feature extraction from runtime signals:
  - contact score
  - penetration score
  - model error
  - motion / acceleration related signals
  - visibility / cluster extent context
- Added predictor inference and blend-driven score boost before `EvaluateActiveSetPlan(...)`
- Added predictor training during `TrainHybridModelBundle(...)`
- Added predictor save/load:
  - `active_set.predictorbin`
- Added predictor metrics to bundle summary/config

### 2) Runtime variant toggles
`FullHybridRuntimeSettings` now supports explicit research variants:
- `enableResidualModel`
- `enableSparseCorrectivePhysics`
- `enableTemporalNetworks`
- `enableActiveSetPredictor`
- `useTeacherPhysicsProxy`

These toggles allow emulating the evaluation variants used in the research plan:
- Pure Physics proxy
- Prior + Residual only
- Active-set Physics only
- Full Hybrid

### 3) Runtime metric completion
Filled the previously incomplete runtime report values:
- `averageGuidePredictorRisk`
- `maxGuidePredictorRisk`
- `averageEstimatedFrameMilliseconds`
- `p95EstimatedFrameMilliseconds`
- `maxEstimatedFrameMilliseconds`
- `hitRate60Fps`

Also extended per-frame CSV output with:
- `estimatedFrameMs`
- `avgGuidePredictorRisk`
- `maxGuidePredictorRisk`

### 4) CLI restore/additions
Added or restored:
- `hybrid-train-bundle`
- `hybrid-bundle-info`
- `hybrid-run-bundle`
- `hybrid-compare`

`hybrid-compare` runs four variants and writes:
- `hybrid_compare_summary.csv`
- `hybrid_compare_summary.json`

## Verification performed
Tested successfully on Linux/CMake:
1. `hybrid-teacher-dataset`
2. `hybrid-train-bundle`
3. `hybrid-run-bundle`
4. `hybrid-compare`

## Remaining major items after this stage
These are still not fully completed in the current project:
- true Alembic SDK integration
- DX11-side consumption of dispatch / GPU buffer outputs
- true GPU-generated indirect dispatch
- larger external trainer / sequence model for the neural core

So this stage completes the research-facing compare/runtime/predictor path, but not the whole product-grade stack.
