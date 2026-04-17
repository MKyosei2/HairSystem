# Phase 3–4 strand rendering and presentation patch

This patch advances the viewer from the phase 1–2 wig-authoring/runtime state to the phase 3–4 presentation state.

## What is added

- GPU strand render modes in the DX11 viewer:
  - Particle Point Sprite
  - Particle Billboard
  - Strand Line
  - Strand Ribbon
  - Strand Tube
- Active-set tier coloring for presentation/debugging.
- Strand-tier SRV upload so the live renderer can visualize full/reduced/frozen/cached recommendations.
- Render-mode persistence in settings save/load.
- Render-mode cycling with the `T` key.

## Intent

Phase 3 and 4 were defined as:

1. make the hair itself visible in the live path, not only the wig proxy
2. preserve the runtime/LOD story already built in phase 1–2
3. add presentation-quality strand views without changing the research core

This patch keeps the runtime foundation intact and layers presentation on top.

## Notes

- `Strand Line` is the lightest strand mode and is the best first validation mode.
- `Strand Ribbon` is the recommended default for demos.
- `Strand Tube` is the heaviest mode and intentionally thins follower rendering under tighter budget scales.
- Active-set tier color mode is useful for showing that the bounded-latency planner is still driving the visual output.

## Verification checklist

1. Open the render mode combo and confirm all 5 modes appear.
2. Switch to `Strand Line` and verify live guide/follower motion is visible.
3. Switch debug color to `Active Set Tier` and verify color changes when motion/contact increases.
4. Switch to `Strand Ribbon` and `Strand Tube` and confirm they render without using the CPU debug fallback path.
5. Save settings, restart, and confirm the selected render mode restores.
