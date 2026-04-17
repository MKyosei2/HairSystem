# Phase 1-2 wig-only runtime integration

This patch set adds the missing glue needed to drive the wig-proxy path closer to the intended research pipeline before visible strand rendering is finalized.

## Added
- Edit stack support in `Authoring` with layered presets
- Sector / ellipse / rectangle region selection with feathering
- Runtime edit-state extraction for guide edit proximity scoring
- Boundary smoothing pass for edited guide regions and follower reattachment
- Active-set scoring support for edit proximity
- Hard budget clamp fields for `T = C + dK`-style runtime control
- DX11 viewer authoring preset controls:
  - Front Bangs
  - Left Sweep
  - Right Sweep
  - Crown Lift
  - Side Volume
  - Back Tuck
- Clear-edits flow that restores the unedited wig proxy baseline

## Notes
- This package focuses on the wig-proxy / no-final-strand-render path.
- It does not claim to complete the fully interactive scalp-raycast brush UI.
- Core files were syntax-checked in this environment; DX11 viewer validation still needs Windows / Visual Studio.
