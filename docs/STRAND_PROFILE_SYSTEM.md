# Strand Profile System

This build adds per-strand authoring for total length and effective segment count.

## Workflow

1. In the DX11 viewer, click **Export Strand Profile...** to write a CSV template for the currently loaded asset.
2. Edit the CSV. Important columns are:
   - `strandIndex`
   - `targetLengthMeters`
   - `targetSegmentCount`
   - `targetLengthScale`
3. Click **Load Strand Profile...** to apply it.
4. After that, every procedural rebuild re-applies the loaded strand profile automatically.

## Notes

- `targetLengthMeters` is absolute strand length in meters.
- `targetLengthScale` is only used when `targetLengthMeters` is left empty or negative.
- `targetSegmentCount` is the visible/effective segment count for that strand. The runtime keeps a common GPU particle layout and collapses any extra trailing segments to an almost zero-length tail so the solver can still run.
