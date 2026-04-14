# Phase 7: Viewer Segment / Refinement Overlay Stage

This stage adds viewer-side scalp overlay modes to inspect runtime segment allocation from the adaptive active-set planner.

## Added overlay modes

- Active-Set Wind
- Active-Set Environment
- Active-Set Refinement Level
- Active-Set Selected Segments

## Purpose

The project now supports a submission-time standard profile of 128 segments per strand, while allowing selected local clusters to be promoted to 256 / 512 / 1024 / 2048 depending on runtime risk and budget.

These overlays make it possible to visually verify:

- where wind exposure is high,
- where environment complexity is high,
- which clusters were promoted to higher refinement levels,
- and where larger selected segment counts are currently being assigned.

## Visual meaning

For refinement / selected segment overlays, the scalp tint uses step-like color bands so that coarse vs high-resolution assignments are easier to read than a simple continuous heatmap.

Approximate reading:

- cool cyan/green: near base resolution
- yellow/orange: mid refinement
- magenta/pink: highest refinement tiers

## HUD additions

Active-set runtime status text now includes:

- average selected segments
- maximum selected segments

This helps connect the visual overlay to the runtime planner summary.

## Notes

This stage focuses on visibility and debugging. It does not claim that all render/sim paths have been re-optimized for maximum configuration in every scenario. A full Windows DX11 rebuild is still required for final validation.
