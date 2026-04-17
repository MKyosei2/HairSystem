# Fast-forward note

This package was advanced from the older GPU ribbon / tube-preview path to include an instanced Pass4 path.

## Highlights
- Added `RenderMode::InstancedTube`
- Added `HairStrandInstanced.hlsl`
- Added fixed-topology indexed instanced tube rendering with `SV_InstanceID`
- Keeps CPU test-mesh path as optional debug-only fallback

## Intent
This moves the project closer to a GPU-first architecture:
- guide simulation on GPU
- follower reconstruction on GPU
- rendering through GPU instancing rather than CPU mesh rebuilds
