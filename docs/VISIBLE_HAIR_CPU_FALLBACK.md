# Visible Hair CPU Fallback

This build replaces the previous geometry-shader-only hair preview path with a safer CPU-generated polygon mesh path.

## What changed
- Hair is now rebuilt every frame from the current simulated particle positions.
- `Ribbon` mode renders each segment as a lit quad mesh.
- `Tube Preview` mode renders each segment as crossed lit quads for a fuller 3D look.
- The same GUI sliders still control wind, gravity, damping, solver iterations, self collision, width scale, and related simulation parameters.

## Why
The previous preview path depended on geometry shader expansion from structured buffers. In some environments the viewer initialized correctly but showed only the background. This build uses a CPU fallback mesh path so the simulated hair is visible as normal triangle geometry.
