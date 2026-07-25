# Follower collision, secondary physics, and runtime strand rendering fix

## Scope

This patch changes only baseline collision, follower secondary dynamics, startup configuration, and the existing DX11 strand renderer. It does not modify Novelty 01 segment-group state, certificates, static/dynamic switching, repair policy, or experiment method definitions.

## Root causes fixed

- The interactive stable profile was overwritten later in startup by a harsher physics block.
- Startup switched the viewer back to `ParticleBillboard`, so the previously edited `HairRibbon.hlsl` was not the shader used by the real `StrandRibbon` path.
- The actual runtime shader, `HairStrandInstanced.hlsl`, forced a 1.8 mm minimum radius and returned alpha 0.98 for every strand.
- `CollisionCS` processed guide particles only; followers reconstructed afterward could pass through the head without an external-collider solve.
- Follower secondary force was world-space and too uniform, making each 24-follower family move like one sheet.

## Changes

- Preserve the selected interactive/formal physics profile instead of overwriting gravity, damping, compliance and follow values after asset creation.
- Use the instanced `StrandRibbon` path for the real preview and experiment renderer.
- Rewrite `HairStrandInstanced.hlsl` as a thin camera-facing ribbon with per-strand width/color variation, tip taper, tier/stride filtering and actual alpha output.
- Add follower sphere, capsule, plane, AABB, floor, SDF and near-root scalp-shell collision.
- Resolve follower collision once with exact analytic projections, remove inward normal velocity and preserve damped tangential sliding. The single pass avoids tripling collider work over 12.288 million follower points.
- Drive follower secondary motion in a guide-local radial/binormal frame so neighboring followers keep family coherence without moving as one wall.

## Deliberate limitation

Follower physics remains a bounded lightweight secondary solve rather than a six-iteration full XPBD solve for all 96,000 followers. This is a baseline runtime correction and does not implement or depend on Novelty 01.

## 2026-07-23 visibility regression correction

The instanced ribbon renderer no longer treats a zero or sub-pixel authored width as an instruction to discard the strand. A conservative guide/follower fallback width is used only when the width buffer is invalid or zero-initialized. Pixel clipping is now driven exclusively by the explicit LOD visibility flag, not by alpha. This restores hair rendering without returning to the former thick opaque tube shell.

## 2026-07-23 guaranteed visibility and stale-shader correction

The previous correction could still appear unchanged for two independent reasons:

1. Release/F5 launches used the solution directory as the working directory, and `CompileShaderFromFile` could resolve an older copied `HairStrandInstanced.hlsl` before the newly edited source.
2. The new fallback width was still sub-pixel at the normal preview camera distance, so a technically valid ribbon could contribute almost no visible coverage.

This revision adds the marker `HAIRGPU_STRAND_RIBBON_VISIBILITY_V4`. Runtime shader resolution rejects older copies that do not contain the marker and continues searching for the current source. `HairSystem.vcxproj` also treats every HLSL file as an up-to-date input and copies shaders to both runtime shader locations after a build.

The instanced ribbon now uses a camera-distance-scaled width floor, a guaranteed nonzero material alpha, and no pixel-shader `clip`/`discard`. Interactive preview renders every second follower to preserve readable strand separation; formal experiment mode continues to render every follower.

