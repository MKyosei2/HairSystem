# Viewer Layer Reorder and Toggle Stage

This stage adds two workflow pieces to the DX11 viewer inline edit stack UI:

- Reorder the selected edit layer with **Move Up** and **Move Down**
- Toggle the selected edit layer with **Layer Enabled**

## What changed

- The inline layer list still reflects disabled layers with the `[off]` prefix.
- The quick form now syncs the selected layer enabled state into a checkbox.
- Reordering a layer rewrites the inline CSV text, keeps selection on the moved layer, and reapplies the edit stack.
- Enabling or disabling a layer rewrites the inline CSV text, refreshes the list labels, and reapplies the edit stack.
- The selected layer label now shows `[on]` or `[off]`.

## Result

The viewer now supports:

1. loading an edit stack,
2. selecting any layer,
3. editing its quick form parameters,
4. reordering layers,
5. toggling them on or off,
6. and applying the result back into the runtime path.
