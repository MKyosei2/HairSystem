# UI DCC Toolbar Stage

This stage reworks the DX11 viewer UI toward a DCC-style toolbar workflow.

## Goals
- Make the left rail the primary tool switcher.
- Move common actions into compact top toolbars.
- Hide the bottom tray by default.
- Show only minimal per-tool options unless `Advanced` is enabled.
- Add a fast grooming viewport mode for lighter interactive hair rendering.

## What changed
- Added top toolbar rows for workspace, file/session actions, and panel toggles.
- Added collapsible controls:
  - Inspector On/Off
  - Advanced On/Off
  - Panels On/Off
  - Fast View On/Off
- Reduced Groom inspector to core controls first, with advanced controls behind a toggle.
- Added keyboard-friendly tool labels and shortcuts:
  - Q Select
  - W Comb
  - E Grab
  - A Smooth
  - S Length
  - D Volume
  - P Part
  - X Freeze
  - V Strand Fix
- Added `Tab` to toggle the bottom tray.
- Saved the new UI state in session settings.
- Fast grooming viewport now reduces follower rendering during grooming and active brush/gizmo interaction.

## Modified files
- `apps/dx11_viewer/src/GpuHairViewer.h`
- `apps/dx11_viewer/src/GpuHairViewer.cpp`
