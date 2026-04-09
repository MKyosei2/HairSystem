# Viewer Layer Name And Duplicate Stage

This stage extends the DX11 viewer inline edit-stack workflow with two authoring conveniences:

- edit the selected layer name directly in the **Quick Edit Form**
- duplicate the selected layer with **Duplicate Layer**

## Added UI

- `Layer Name` label + single-line edit field
- `Duplicate Layer` button next to `Delete Layer`

## Behavior

- Selecting a layer in the list now also loads its current name into the form.
- `Replace Selected` writes the edited name back into the selected layer together with mode / feather / length scale.
- `Append Layer` uses the current form name for the new layer.
- `Duplicate Layer` inserts a copy right after the selected layer and appends `_copy` to the duplicated name.
- Names are sanitized before CSV serialization so commas and line breaks do not break the shared CLI/viewer CSV format.

## Workflow

1. Select any inline layer from the list.
2. Change `Layer Name`.
3. Press `Replace Selected` to rename that layer in-place.
4. Press `Duplicate Layer` when you want a nearby variation to branch from the current settings.
5. Apply / rebuild as before.

This keeps the proposal-side “stack of editable regional layers” usable inside the viewer without forcing direct CSV edits for simple naming and branching tasks.
