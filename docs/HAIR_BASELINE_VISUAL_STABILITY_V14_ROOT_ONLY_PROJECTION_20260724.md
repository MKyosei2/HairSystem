# Hair Baseline Visual Stability V14

This patch removes the persistent double-bulge / double-shell appearance.

Root cause:
- `RebuildRootRestPositionsFromScalp` projected every point of every strand outside the head volume.
- That produced a second spherical inner shell beneath the follower layer.

Changes:
- Restrict head-volume projection to the first 10% of each strand.
- Reduce procedural crown lift and scalp-conforming root travel.
- Move the part slightly off-center and reduce symmetric split strength.
- Reduce Guide preview width/alpha while keeping all 96,000 Followers visible.
- Preserve Novelty 01 state switching and formal experiment settings.
