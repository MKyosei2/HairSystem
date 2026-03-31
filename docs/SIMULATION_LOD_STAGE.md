# Simulation LOD Stage

This stage adds simulation-side LOD on top of the existing render-side LOD.

## What changed

- Guide solver iterations now scale down with camera distance.
- Follower reconstruction now updates on a phase/stride basis:
  - near: every frame
  - mid: every 2nd frame
  - far: every 4th frame
- Far distance disables guide self-collision to reduce GPU cost.
- Render LOD remains active separately.

## Why it helps

Render-only LOD reduces overdraw, but the compute cost still remained close to full. This stage lowers the actual GPU simulation work while keeping guide silhouettes stable.

## Current limitation

The follower pass still dispatches over the same thread space and early-outs for skipped followers, so this is a meaningful optimization but not yet the final compacted/indirect simulation path.
