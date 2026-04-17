# Mask mode and preview stage

This stage adds two UI features:

- Mask Source combo box with:
  - Auto
  - External Files
  - Procedural
- Small preview thumbnails for:
  - parting mask
  - region mask

Behavior:

- Auto: uses external override paths when selected, otherwise tries the default sample asset masks under `docs/sample_assets/`
- External Files: only uses the explicitly chosen files
- Procedural: bypasses external files and forces procedural generation

The viewer saves and restores:

- `maskSourceMode`
- `partingMaskPath`
- `regionMaskPath`
