# HAIR_BASELINE_VISUAL_STABILITY_V17_V15_BASE_CROWN_SMOOTHED_20260724

Corrective integration built strictly on the V15 depth-occluded single-volume baseline.

## Why V16 did not improve the image
V16 was accidentally generated from a pre-V15 working tree. Its `SampleAsset.cpp` restored the old large radial-volume coefficients, undoing V15's single-volume reduction.

## V17 changes
- Preserve the V15 depth-only scalp occluder.
- Preserve V15 single-volume radial coefficients.
- Suppress side-part flow in the crown region.
- Smooth crown directions toward a projected downward field.
- Shorten and weaken the near-root high-curvature interval.
- Reduce root highlight without reducing follower count.
- Use a new cache key and an early title marker for verification.
