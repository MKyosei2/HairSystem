# Texture/mask driven stage

This stage adds file-driven scalp masks for sample hair generation.

## What changed

- `SampleAsset.cpp` now looks for CSV masks in `docs/sample_assets/`:
  - `scalp_parting_mask.csv` (1 channel)
  - `scalp_region_mask.csv` (3 channels: crown, temple, nape)
- If both files are found, sample hair generation uses them instead of the procedural fallback.
- If they are missing, the old procedural parting / region maps are still used.
- A scalp wig styling pass is applied after rebuilding root positions so roots follow the scalp flow more naturally.

## CSV layout

- Parting mask: one grayscale value per cell
- Region mask: 3 values per cell in repeating RGB-like order for each pixel

## Why CSV

CSV keeps the mask path simple and dependency-free in the current codebase while still making the scalp masks editable outside the code.
