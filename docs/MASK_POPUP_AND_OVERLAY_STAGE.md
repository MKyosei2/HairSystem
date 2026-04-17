# Mask popup and overlay stage

This stage adds two debugging features for scalp mask work:

- Clickable mask previews that open a larger popup window
- A scalp overlay mode that colors the scalp mesh from the active parting / region mask

## Overlay modes

- None
- Parting
- Region

The overlay uses the loaded external mask when available. If no external file is active, it falls back to a procedural approximation so the debug mode still shows useful structure.
