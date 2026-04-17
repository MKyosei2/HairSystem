# Viewer Active-Set Runtime Tuning and Report Stage

This stage completes the remaining viewer-side integration for the proposal-oriented demo path.

## Added capabilities

- **Live Active-Set tuning UI** in the DX11 viewer
  - Adaptive budget toggle
  - Hard clamp toggle
  - Max active clusters
  - Max active guides
  - Promote threshold
  - Target frame milliseconds
  - Estimated fixed cost milliseconds
  - Estimated cluster cost milliseconds
  - Motion / contact / uncertainty / edit score weights

- **Live runtime feedback**
  - Stats panel now includes the current active-set tuning summary
  - Stats panel now includes active-set score weights
  - Active-set scalp overlays refresh after tuning changes

- **Export buttons**
  - `Export Active-Set JSON`
  - `Export Runtime Report`

## Runtime report bundle

`Export Runtime Report` writes a summary text file and also attempts to emit:

- `*_performance.csv`
- `*_activeset_plan.json`
- `*_viewer_settings.ini`

This makes it easier to capture a single experiment snapshot for the graduation-project evaluation flow.

## Settings persistence

The following viewer-side runtime tuning values are now saved and restored through the viewer settings file:

- adaptive budget on/off
- hard clamp on/off
- max clusters
- max guides
- promote threshold
- target frame ms
- fixed cost ms
- cluster cost ms
- motion/contact/uncertainty/edit score weights

## Verification note

- **core / CLI**: rebuilt on Linux after this stage
- **DX11 viewer**: implementation updated, but Windows / DX11 runtime execution was **not** verified in this environment
