# Clip-Space Emergency Wig Fallback

This patch adds a final guaranteed-visibility fallback for the DX11 viewer.

## What it changes

- Adds `DrawHairMeshVertices()` helper for CPU mesh style drawing.
- Adds `BuildEmergencyClipSpaceWigMesh()` which generates a small blond wig-like triangle mesh directly in clip space.
- Renders that emergency wig if:
  - no CPU fallback wig was drawn,
  - no scalp mesh was drawn,
  - and the frame still looks visually suspect (`0 visible`, previous GPU pipe dead, visibility suspect, or hair path missing).
- Adds `clipFallback=yes/no` to the title/stats.

## Why

If the world-space hair path is broken because of camera/bounds/state issues, the old code can still clear the render target and present a blank scene. This fallback bypasses world transforms and should still be visible if the patched binary is actually running.

## Expected outcome

After applying this patch, a completely black render area should instead show a simple emergency wig silhouette in the center of the render viewport when the normal hair/scalp path fails.

If the viewer is **still fully blank after this patch**, the most likely causes are:

1. the rebuilt executable is not the one being launched,
2. the patched source was not compiled into the running binary,
3. the viewer is loading a different source tree than expected.
