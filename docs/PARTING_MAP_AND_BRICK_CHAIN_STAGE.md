# Parting map and brick-chain stage

This stage adds both requested directions at once:

- procedural parting-map + scalp-region-map driven sample generation
- fixed-topology instanced brick-chain rendering as the main path

## Generation changes

Guide roots are no longer only biased by ad-hoc offsets. The sample generator now evaluates:

- a procedural parting map near the crown
- a procedural scalp region map with crown / temple / nape masks

These masks are used to push roots away from the part line and vary density / placement by region.

## Rendering changes

A new shader `HairBrickInstanced.hlsl` renders each strand segment as an instanced cuboid.
This is intended to be the main "hair strand = 3D voxel/brick" path.

The viewer now defaults to `BrickInstanced` and still keeps Ribbon / Tube / Voxel preview as fallbacks.
