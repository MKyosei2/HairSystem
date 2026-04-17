# Viewer Layer List Stage

This stage adds a layer list UI on top of the inline edit stack flow.

## Added
- Layer list box that reflects the current inline edit stack
- Selection sync from list -> quick edit form
- `Replace Selected` instead of only replacing the first layer
- `Delete Layer` button
- Selected layer index saved/restored in viewer settings

## Flow
1. Load or type an edit stack CSV
2. Pick any layer from the layer list
3. Adjust mode / feather / length scale in Quick Edit Form
4. Replace the selected layer or append a new one
5. Apply inline edit stack and rebuild as before

## Notes
- This keeps the CSV text editor path intact
- Windows / DX11 runtime verification is still pending
- Core / CLI code paths are unchanged by this stage
