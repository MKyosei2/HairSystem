# Stage 9: Alembic hook-up, GPU compaction/indirect, stronger temporal/predictor training

Implemented in this patch:
- Alembic public save wrapper now routes to the SDK path when `HAIRGPU_WITH_ALEMBIC` is enabled.
- Added overload handling for `HairAlembicSettings` in the public API path.
- Expanded Alembic compatibility manifest generation for non-SDK builds.
- Temporal guide network training upgraded with validation split, LR decay, minimum LR, and early stopping.
- Active-set predictor training upgraded with validation split, LR decay, minimum LR, early stopping, and positive-class weighting.
- DX11 viewer path extended with GPU-side follower tier compaction, GPU-generated indirect dispatch args, and compacted follower dispatch consumption in `FollowCS`.

Verified in this environment:
- Linux/CMake build for `hairgpu_core` and `hairgpu_cli` succeeded.
- `hybrid-train-bundle` succeeded with the stronger temporal/predictor training path.
- `hybrid-run-bundle` succeeded and wrote summary JSON.
- `HAIRGPU_WITH_ALEMBIC=ON` configuration was accepted, but the environment does not have the Alembic SDK installed, so CMake fell back to compatibility-only `.abc` support.

Not verifiable here:
- Real Alembic SDK round-trip at runtime (SDK absent here).
- Windows/DX11 execution of the new GPU compaction + `DispatchIndirect` path.
