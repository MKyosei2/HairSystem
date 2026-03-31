# Instanced Pass4: stabilized frame + auto render LOD

This step pushes the GPU-first path further without reintroducing CPU mesh rebuilds.

## Added

- automatic follower render decimation based on camera distance
  - near: full
  - mid: every 2nd follower
  - far: every 4th follower
- guide strands are always preserved
- instanced tube frame is stabilized by blending a root-derived frame with a camera-facing frame
- stats now show rendered strand count and render stride

## Why

The previous instanced tube path was already GPU-driven, but it still rendered every follower and used a simple up-vector frame.
This step reduces overdraw at distance and makes the tube orientation less twitchy on bends.

## Limits

- This is render LOD only. Simulation still updates the full strand buffers.
- The next big step is simulation LOD / guide-only update tiers and optional card rendering.
