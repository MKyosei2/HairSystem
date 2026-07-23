# Hair Baseline Visual Stability V12 — Dense Root Coverage

## Scope

This revision fixes the baseline asset and renderer only. It does not alter
Novelty 01 segment-group state classification, certificates, wake prediction,
repair, or cache-state semantics.

## Root cause

The asset contained 100,000 strands, but 96,000 follower roots were displaced
only inside the same source triangle as each guide. The result was 4,000 dense
root clusters with visible empty cells between them. In addition, the normal
preview multiplied `RenderStrandAlpha=0.14` by a follower alpha scale of 0.46,
so each follower contributed only about 0.064 alpha before tapering.

The window title reported the asset strand count but not the instance count
actually submitted to the GPU, making it difficult to distinguish a density
problem from a draw-count problem.

## Changes

- Follower bindings now spread across adjacent scalp triangles through UV-space
  offsets and are resolved back to the scalp mesh.
- Parent-guide locality is retained; only the root sampling footprint changes.
- The side-part field uses a stable projected world-left/right direction instead
  of a rotating azimuthal tangent.
- Part-flow strength and the authored part width are reduced.
- Interactive preview still draws every follower.
- Root width and alpha coverage are restored to a dense but transparent range.
- Asset cache generation is bumped to V12.
- The title reports:
  - asset guides/followers/total strands
  - particle count
  - actual draw instance count
  - actual draw vertex count
  - V12 root-layout marker

## Expected title

`visualFix=V12 previewFollowers=100% rootLayout=distributed`

For the default asset after the first rendered frame:

- `totalStrands=100000`
- `drawInstances=100000`
- `drawVertices=25600000`

The exact vertex count assumes 128 points and two ribbon vertices per point.

## Required application procedure

Delete the following before rebuilding:

- `bin`
- `bin-int`
- `.vs`
- the executable-side `asset_cache`

Then rebuild `Release | x64`.
