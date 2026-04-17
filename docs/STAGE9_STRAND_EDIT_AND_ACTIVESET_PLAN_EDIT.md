# Stage 9: Strand Edit + Active-Set Edit Plan

This stage adds the minimum implementation needed to make single-strand editing a first-class part of the research pipeline.

## Added

- Explicit strand selection support in `EditLayer`
  - `strandSelection`
  - `strandSelectionMode = none|union|replace|guide_family`
  - `strandSelectionWeight`
- Shared parsing helpers:
  - `ParseStrandSelectionSpec(...)`
  - `FormatStrandSelectionSpec(...)`
- Edit stack CSV extension with three new columns:
  - `strandSelection`
  - `strandSelectionMode`
  - `strandSelectionWeight`
- New CLI command:
  - `hairgpu_cli edit-strand <input> <output.hairbin> <strandSpec> [mode] ...`
- New CLI command for evaluation/debug:
  - `hairgpu_cli active-set-plan-edit <input> <stack.csv> <output.json> ...`

## Behavior

- `replace` applies edits only to the explicit strands.
- `union` combines explicit strands with the region brush selection.
- `guide_family` expands a selected guide to its followers, or promotes a selected follower back to its parent guide.
- Runtime edit influence now promotes follower-only edits back to the parent guide so active-set scoring can react to single-strand edits.

## Intended use

This is the research-grade minimum path needed to support:

1. broad region edit
2. local refinement
3. explicit single-strand correction
4. active-set planning driven by edit influence

The DX11 viewer source was also updated so inline edit-stack CSV text can preserve the new columns, even though Windows-side runtime validation was not performed in this environment.
