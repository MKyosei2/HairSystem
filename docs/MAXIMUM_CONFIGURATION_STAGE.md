# Maximum Configuration Stage

This stage promotes the project from the submission-standard 128 segments/strand baseline to an adaptive maximum configuration.

## Runtime assumptions

- Development baseline: 16 segments/strand (17 points)
- Submission baseline: 128 segments/strand (129 points)
- Ideal local refinement: 1024 segments/strand (1025 points)
- Maximum adaptive refinement: 2048 segments/strand (2049 points)

## What changed

1. Asset IO no longer clamps imported strands to 64 points. Imported and retessellated assets can preserve up to the runtime maximum.
2. The CLI profile system now understands `standard128`, `ideal1024`, and `max2048` / `maximum` / `extreme` / `ultimate`.
3. The DX11 viewer adds two new presets:
   - Submission 128 Standard
   - Maximum Adaptive 2048
4. Hybrid bundle config persistence now stores segment-budget fields:
   - budgetStandardParticlesPerStrand
   - budgetMaxParticlesPerStrand
   - budgetMaxRefinementLevel
5. Viewer settings persistence now stores the same active-set segment budget fields.

## Interpretation

The project still runs a 128-segment submission baseline for the full asset. The new maximum configuration means that the planner is allowed to assign 256/512/1024/2048-segment local refinement to selected clusters without changing the global baseline.

## Important note

This patch extends data-path and planner support for the maximum configuration. It does **not** claim that every viewer path has already been fully profiled at the new 2048-segment ceiling on Windows hardware.
