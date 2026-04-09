# Proposal-aligned edit stack pipeline

This stage adds a CLI workflow that maps directly to the graduation proposal's two-stage editing story:

1. broad scalp-area selection
2. fine local refinement inside that area
3. runtime influence export for active-set coupling
4. edit-locality / boundary-naturalness evaluation

## New CLI commands

```text
hairgpu_cli edit-stack-template <output.csv>
hairgpu_cli edit-stack-apply <input> <stack.csv> <output.hairbin> [runtime.json]
hairgpu_cli edit-stack-eval <input> <stack.csv> <output_dir> [samplesPerStrand=16]
hairgpu_cli edit-compare <reference> <edited> <output.json> [samplesPerStrand=16]
```

## Stack CSV format

```text
stack,boundarySmoothing,boundaryRadiusUv
layer,name,enabled,layerWeight,mode,cx,cy,rx,ry,innerRadius,angleCenterRadians,angleHalfExtentRadians,feather,groupId,guidesOnly,lengthScale,widthScale,rootOffsetX,rootOffsetY,rootOffsetZ,tipOffsetX,tipOffsetY,tipOffsetZ,sideOffset,yawRadians,followerInfluence
```

- `stack` row configures boundary smoothing applied after layered edits.
- each `layer` row combines a selection region and an edit payload.
- supported modes: `all`, `ellipse`, `rectangle`, `sector`.
- `guidesOnly=1` keeps the edit decision on guide strands while followers inherit via follow metadata.
- leave `groupId` empty for all groups.

## edit-stack-eval outputs

`edit-stack-eval` writes:

- `edited.hairbin`
- `edit_runtime.json`
- `edit_metrics.json`

The runtime json exposes guide/strand influence arrays and active-guide counts.
The metrics json reports:

- RMS position error
- silhouette error
- clump preservation score
- root locality score
- edit locality score
- boundary naturalness score
- edited strand ratio
- measured edit latency in milliseconds

## Example

```text
hairgpu_cli edit-stack-template tmp/edit_stack.csv
hairgpu_cli edit-stack-eval examples/sample_assets/import_curves_template.csv tmp/edit_stack.csv tmp/edit_eval 16
```

This is intended as the fast path for the proposal demo and for comparing local-edit behavior before wiring the same edit influence into the DX11 runtime UI.
