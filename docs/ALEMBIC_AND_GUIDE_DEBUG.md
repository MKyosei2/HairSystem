# Alembic import hook + guide/follower debug overlay

## Added now
- Hair asset import button in the DX11 viewer
- Real CSV curves import path (`.csv`)
- Alembic import entry point (`.abc`) with a clear SDK-required error path
- Guide/follower debug overlay with CPU readback from the current GPU simulation state
- Root markers and follower-to-guide link visualization

## What works immediately
Use **Import Hair...** and load a CSV with columns like:

- `strandIndex`
- `pointIndex`
- `x,y,z`
- optional: `width`
- optional: `isGuide`
- optional: `parentGuide`
- optional: `followWeight`
- optional: `clumpWeight`
- optional: `noiseWeight`

A template file is included at:

- `docs/sample_assets/import_curves_template.csv`

The current frame CSV export was also expanded so it can be re-imported as a starting point for an external edit / round-trip.

## Alembic status
The viewer now recognizes `.abc` in the import UI and routes it through `hairgpu::LoadHairAssetFromAlembic(...)`.

This package still does **not** bundle the Alembic SDK itself. That means the actual binary Alembic reader is not complete in this archive.

To finish real Alembic import on your machine:
1. Install/link your local Alembic SDK
2. Build with `HAIRGPU_WITH_ALEMBIC=ON`
3. Replace the stub implementation in `src/hairgpu_core/AssetIO.cpp`

## Guide/follower debug overlay
New overlay toggles:
- Show Guide Skeletons
- Show Follower Skeletons
- Show Root Markers
- Show Follow Links

New shortcuts:
- `J`: toggle guide skeletons
- `K`: toggle follower skeletons
- `L`: toggle follow links
- `U`: toggle root markers

Follower overlays are sampled when there are many follower strands so the screen does not become unreadable.
