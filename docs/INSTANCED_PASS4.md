# Instanced Pass4

This update adds a fixed-topology GPU rendering path for hair strands.

## What changed

- Added `RenderMode::InstancedTube`
- Added `HairStrandInstanced.hlsl`
- Added immutable template vertex/index buffers for a per-strand tube mesh
- Render path now uses `DrawIndexedInstanced(...)` for one template tube per strand
- Instanced mode is now the default render mode
- CPU readback/test mesh remains optional behind `Show Test Mesh`
- Ribbon / TubePreview GS paths remain available as fallback modes

## Design

Each strand is rendered from the same fixed-topology template:

- longitudinal samples: `particlesPerStrand`
- radial segments: 6
- template vertices store `(u, angle)` only
- `SV_InstanceID` selects the strand
- the vertex shader samples the simulated strand buffer and reconstructs a tube vertex in world space

## Limits

- The local frame currently uses a simple up-vector fallback and is not yet parallel-transport stabilized
- This is still a rendering-side step; guide/follower simulation logic is unchanged
- Final validation still needs to be done in Visual Studio on Windows
