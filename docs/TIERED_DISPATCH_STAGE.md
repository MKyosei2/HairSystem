# Tiered Dispatch Stage

This stage replaces the previous full-range follower dispatch with a tier-aware dispatch count.

## What changed

- `FollowCS` now maps thread `id.x` to a sparse follower index using:
  - `FollowerUpdateStride`
  - `FollowerUpdatePhase`
- The CPU now dispatches only the active follower subset for the current simulation LOD tier.
- Guide solver iterations now honor the LOD-reduced `guideSolverIterations` loop count, not just the constants buffer.

## Why this matters

The previous simulation LOD still launched threads for the full follower range and relied on shader early-outs.
This version reduces useless thread launches and makes the follower pass closer to a true tiered update path.

## Remaining step

The next big step is splitting guide / follower tiers into separate buffers or indirect dispatch streams so that the pipeline no longer relies on stride/phase addressing inside a shared follower buffer.
