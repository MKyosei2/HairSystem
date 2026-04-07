# DX11 Viewer Black Screen Fallback Fix

This patch hardens the viewer so the render area does not stay empty when the hair path becomes invalid or invisible.

## Included changes

- Added a safe fallback state helper that forces:
  - `Particle Billboard` render mode
  - a clamped visible width scale
  - scalp mesh visibility
  - optional camera reframing
- Added visibility diagnostics to the window title and runtime stats:
  - visible / minimum expected visible particles
  - draw path used (`gpu-particles`, `cpu-fallback`, `scalp-only`, `none`)
  - recovery state and recovery count
- Enabled CPU fallback wig rendering not only in full debug mode, but also whenever visibility becomes suspect or recovery is pending.
- Kept scalp mesh visible as a fallback during suspect / recovery states.
- Added safer settings-load behavior:
  - clamps width scale
  - keeps render mode in supported range
  - ensures scalp mesh is visible
  - queues recovery after loading settings
- Toggling render mode with `T` now queues a visibility recovery pass.

## Purpose

The goal is to make the viewer recover into a guaranteed visible state first, instead of letting the render area remain black when the particle path becomes invalid, empty, or off-camera.
