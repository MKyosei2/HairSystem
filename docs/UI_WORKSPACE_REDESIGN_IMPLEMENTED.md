# UI Workspace Redesign Implemented

This stage continues the DX11 viewer redesign and connects the new workspace/tool UI to viewport-driven grooming interactions.

## What changed

### 1. Workspace split
The viewer exposes four top-level workspaces:
- Groom
- Sim
- Review
- Report

These workspaces drive layout and interaction intent instead of relying on a single long right-side form.

### 2. Groom tool rail
A left-side tool rail is available for:
- Select
- Comb
- Grab
- Smooth
- Length
- Volume
- Part
- Freeze
- Strand Fix

Each tool remaps the inspector wording and, for brush tools, now drives viewport interaction directly.

### 3. Contextual inspector
The inspector no longer shows every slider at once.
Instead, Groom tools reveal only the relevant controls.
Examples:
- Comb shows flow / tip / yaw style controls.
- Grab and Strand Fix show root / tip move controls.
- Smooth focuses on falloff and boundary relax.
- Length focuses on length scaling.
- Volume focuses on width / clump response.
- Part exposes angle and spread controls.

Sim / Review / Report each get their own inspector content as well.

### 4. Bottom tray
A bottom tray switches between:
- Layers
- History
- Metrics

The implementation reuses existing widgets:
- Layers -> inline layer list
- History -> viewport stroke history and notes
- Metrics -> runtime stats label

### 5. Session chrome
The top section is organized around:
- workspace selection
- asset/session actions
- playback / rebuild actions
- status text for current stack and overlay mode

### 6. Undo / Redo buttons and richer snapshots
Undo / Redo now preserve more than raw rest points.
Snapshots now also include:
- inline edit stack text
- selected layer index
- locked strand state

This makes viewport brush strokes reversible as real session edits instead of only geometric offsets.

### 7. Viewport brush interaction
The Groom workspace now has direct viewport-driven brush editing for:
- Comb
- Grab
- Smooth
- Length
- Volume
- Part
- Freeze

Current interaction model:
- `LMB` = paint / stroke for brush tools
- `Alt + LMB` = orbit camera
- mouse wheel over viewport = change brush radius for brush tools
- `Esc` = cancel current drag / stroke
- `Ctrl+Z / Ctrl+Y` = undo / redo

The brush picks the nearest visible guide root, converts that to scalp/root UV, builds an internal `EditLayer`, and previews / commits by applying an `EditStack` against the base asset.

### 8. Non-destructive brush-to-layer conversion
Viewport grooming does not bypass the existing edit system.
Each committed stroke is converted into a layer and written back into the inline edit stack.
That means:
- the Layers tray updates
- overlay previews still work
- the stack can still be exported
- runtime edit state remains compatible with existing tooling

### 9. Freeze / lock state
The Freeze tool now marks selected guide families as locked in viewer state.
Locked strands:
- are shown distinctly in editability coloring
- are listed in the history/notes summary
- are skipped by later brush strokes
- are saved inside `.strandedit` sessions

### 10. Strand-edit session persistence expanded
Saved strand edit sessions now include:
- selected strand
- selected layer index
- locked strand indices
- inline edit stack text
- rest point positions

So a saved session can restore both the geometric state and the current non-destructive layer session more faithfully.

## Important implementation note
This stage still preserves the existing backend:
- `EditStack`
- `EditLayer`
- `RuntimeEditState`
- inline CSV stack flow
- selected strand editing / gizmo path

The new viewport brush path is layered on top of that runtime instead of replacing it.

## Files changed
- `apps/dx11_viewer/src/GpuHairViewer.h`
- `apps/dx11_viewer/src/GpuHairViewer.cpp`
- `docs/UI_WORKSPACE_REDESIGN_IMPLEMENTED.md`

## Scope of this stage
Implemented now:
- workspace buttons
- tool rail buttons
- contextual inspector layout
- bottom tray tabs
- session/workspace state persistence
- tool-specific label wording
- direct Undo / Redo buttons
- viewport brush strokes mapped to edit layers
- Alt-orbit fallback while in Groom tools
- brush radius wheel adjustment
- lock/freeze state
- richer history text
- expanded strand session save/load

Still not fully replaced:
- scalp triangle raycast brush cursor rendering
- pressure/tablet support
- full clump-aware smooth solve
- dedicated brush cursor geometry in viewport
- ImGui migration

Those can now be added on top of the new workspace/tool state model rather than on top of the previous monolithic quick-edit form.
