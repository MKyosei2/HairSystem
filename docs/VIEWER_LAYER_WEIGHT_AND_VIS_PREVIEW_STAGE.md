# Viewer layer weight + selected layer visualization stage

This stage adds two proposal-aligned authoring features to the DX11 viewer.

## Added

- Quick Edit Form now exposes **Layer Weight** as a slider.
- Selected layer -> quick form sync now includes `layerWeight`.
- `Replace Selected` / `Append Layer` now preserve the chosen `layerWeight`.
- Layer list rows now show both **weight** and **length scale**.
- Scalp overlay entry was renamed to **Edit Influence / Selected Layer**.
- When that overlay is enabled, the viewer now prefers the **currently selected inline layer** for preview.
- Boundary zones between selected / non-selected guides receive an extra tint so the edit border reads more clearly.
- Quick Edit Form shows a short preview summary with selected-guide count and max influence.

## Notes

- Preview uses the selected inline layer against the current base asset.
- If no inline layer is selected, overlay falls back to the existing combined runtime edit influence.
- Windows / DX11 runtime verification is still pending; Linux build verification was run for core / CLI.
