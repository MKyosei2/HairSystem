# Tiered runtime + `.abc` compatibility stage

This stage pushes two still-missing runtime-facing pieces forward.

## Added now

### 1. Active-set tiers now affect the actual full-hybrid runtime output
Previously, the planner already produced `Full / Reduced / Frozen / Cached`, but the offline `RunFullHybridRuntime(...)` path still wrote essentially one corrected hybrid result for every strand.

That gap is now closed in the core runtime:

- `Full`
  - keeps the sparse-corrected result
- `Reduced`
  - keeps the reduced hybrid prediction without the extra frozen/cached reuse step
- `Frozen`
  - reuses the previous frame strand pose and root-follows the current root
- `Cached`
  - reuses a cache snapshot pose and root-follows the current root
  - the cache snapshot is refreshed on a configurable interval

This makes the runtime output match the active-set planner semantics much more closely.

### 2. Second-order temporal prediction gains
The hybrid bundle now carries:

- `priorAccelerationPredictionGain`
- `residualAccelerationPredictionGain`

The runtime now applies both velocity-based and acceleration-based prediction on top of the prior / residual guide inputs.

### 3. Bundle parameters for frozen/cached tiers
The hybrid bundle now stores:

- `frozenStrandBlend`
- `frozenRootFollow`
- `cachedStrandBlend`
- `cachedRootFollow`
- `cachedFrameRefreshInterval`

These are saved to / loaded from `hybrid_bundle.cfg` and included in JSON summaries.

### 4. Practical `.abc` compatibility fallback
This is **not real Alembic SDK import**.

But `.abc` loading now sniffs the file contents and transparently falls back to:

- USDA
- hair binary (`.hairbin` payload)
- curves CSV

when those assets were renamed or passed through compatibility workflows.

That means `.abc` is still not true binary Alembic in this build, but the path is more usable for interchange/debugging than a hard stub.

## Verified in this environment

- Linux CMake build with DX11 viewer disabled: success
- `hybrid-train-bundle`: success
- `hybrid-run-bundle`: success
- `.hairbin` renamed to `.abc` then loaded through `active-set-plan`: success

## Still not finished

- real Alembic SDK-backed `.abc` import/export
- DX11-side consumption of the dispatch pack / GPU buffers
- true GPU-generated indirect dispatch from runtime counters
- full neural prior / temporal residual beyond the reduced-model path
