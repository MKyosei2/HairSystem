# Hair baseline visual-stability V5

This patch is limited to baseline asset generation, follower collision clearance and rendering. It does not change Novelty 01 state classification, certificates, predictive wake, static/dynamic switching or repair semantics.

## Fixed

- Rebalanced crown/front root quotas so the procedural 100k asset no longer concentrates 75% of strands into two small scalp regions.
- Reduced the authored 1.6 mm strand root width and excessive 9-10 mm root shell.
- Limited the follower scalp-normal guard to the first 12% of a strand.
- Added a hair-only depth-test/no-depth-write state for alpha-blended ribbons.
- Reduced preview follower density and alpha while preserving full 100k formal instance draws.
- Removed world-origin fallback for invalid positions, preventing long radial triangles.
- Added root width/alpha opening to avoid a solid crown plate.
- Bumped the procedural cache contract to v5 so old helmet/curtain assets cannot be reused.
