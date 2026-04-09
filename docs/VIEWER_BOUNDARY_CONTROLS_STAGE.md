# Viewer Boundary Controls Stage

This stage adds stack-level boundary controls to the DX11 viewer quick edit form.

Added:
- Boundary Smoothing slider
- Boundary Radius UV slider
- Inline CSV stack header sync (`stack,boundarySmoothing,boundaryRadiusUv`)
- Quick form preview summary now shows smoothing / radius
- Replace / Append now preserve edited stack boundary settings

Behavior:
- Loading an inline or external edit stack now syncs the viewer form from the stack header values.
- Editing the boundary sliders updates the stack values that are written back into the inline CSV.
- Applying inline edits keeps the edited stack-level boundary smoothing settings instead of silently reverting to defaults.

Notes:
- Linux build was re-run for core / CLI targets.
- DX11 viewer implementation was updated, but Windows runtime verification was not performed in this environment.
