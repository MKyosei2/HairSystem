# Viewer Boundary Before/After Stage

This stage adds a new scalp overlay mode to the DX11 viewer:

- **Boundary Before / After**

It is intended to make the boundary controls easier to tune while editing a selected layer.

## What it shows

When the selected layer preview is available:

- **Left side** of the scalp preview shows the raw selected-layer influence.
- **Right side** shows the boundary-tinted result used by the edit influence preview.
- A bright divider line is drawn near the middle so the split is easy to read.

This makes it easier to compare:

- selection alone
- selection plus boundary emphasis

while adjusting:

- `boundarySmoothing`
- `boundaryRadiusUv`

## Notes

- The mode reuses the same selected-layer preview path as the existing `Edit Influence / Selected Layer` overlay.
- If a selected-layer preview is not available, the overlay falls back to the runtime edit influence data.
- This stage only changes the viewer-side visualization; it does not change the edit-stack file format.
