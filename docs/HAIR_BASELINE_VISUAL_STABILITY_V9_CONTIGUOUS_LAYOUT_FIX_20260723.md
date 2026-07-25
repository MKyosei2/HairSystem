# Hair baseline visual stability V9: contiguous guide/follower layout

## Root cause

The procedural builder emitted each guide immediately followed by its followers:

```text
[guide0][followers of guide0][guide1][followers of guide1]...
```

The runtime, `FollowCS`, rendering LOD and telemetry require:

```text
[all guides][all followers]
```

Therefore most generated guides were simulated as followers, while many followers
inside the first `guideStrandCount` entries were treated as guides. This invalidated
parent-guide addressing and produced shell-like density, unstable clumps and changes
that appeared not to affect the preview.

## Fix

- Reorder the generated asset once after procedural authoring.
- Copy positions, widths, materials, root bindings and follow data in the same order.
- Reset stale segment-group topology after reordering.
- Validate that `[0, guideStrandCount)` contains only guides and the remainder only followers.
- Invalidate older binary caches with the `v9_contiguous_guides` cache generation.
- Expose `visualFix=V9` in the Viewer title.

This change is baseline asset/runtime correctness. It does not alter Novelty 01 state
classification, certificates, predictive wake, repair, or static/dynamic switching.
