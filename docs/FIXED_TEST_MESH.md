# Fixed Test Hair Mesh

This patch adds a guaranteed-visible CPU-generated 3D test mesh to the DX11 viewer.

What was added:
- `Show Test Hair Mesh` checkbox in the GUI (default: ON)
- `Y` hotkey to toggle the test mesh
- A bright CPU-generated tube strand placed in front of the scalp
- A short bright marker cylinder near the face to sanity-check the render path

Why:
- If the simulated strands fail to appear, this fixed mesh helps verify that the swap chain,
  viewport, input layout, CPU mesh upload, and triangle draw path are all working.

Expected result:
- Even if the simulation readback is broken, at least one bright 3D tube should appear near the head.
