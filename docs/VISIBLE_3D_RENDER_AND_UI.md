# Visible 3D Render + UI update

This update focuses on two issues:

1. The viewer looked like only a flat background.
2. Hair needed to be controllable from GUI and rendered as actual 3D-ish geometry instead of plain debug lines.

## What changed

- Hair ribbon shader now emits camera-facing quad geometry per segment.
- Hair tube shader now emits a 6-sided tube preview per segment.
- Tube Preview is now the default render mode.
- Width scale range was widened so hair is easier to see immediately.
- A solid scalp mesh is rendered before the hair.
- A `Show Scalp Mesh` checkbox was added.
- The main window now uses `WS_CLIPCHILDREN | WS_CLIPSIBLINGS` to reduce child UI overdraw issues.
- Existing sliders already update simulation values live:
  - Width Scale
  - Gravity
  - Damping
  - Wind Strength
  - Solver Iterations
  - Follow Stiffness
  - Self Collision Radius
  - Self Collision Stiffness
  - Grid Cell Size
  - Stretch Compliance
  - Bend Compliance

## New files

- `apps/dx11_viewer/shaders/ScalpMesh.hlsl`

## Quick test

1. Build and run.
2. Leave `Render = Tube Preview`.
3. Keep `Show Scalp Mesh` enabled.
4. Press `Reset` once.
5. Increase `Width Scale` if you want thicker preview geometry.
6. Move `Wind Strength` to confirm live physics response.

## Notes

The hair is still a preview mesh generated from strands, not a final production tube mesh asset export.
That is intentional so the simulation remains interactive.
