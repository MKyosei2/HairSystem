# Asymmetric voxel + UI stage

This stage focuses on the issues visible in the screenshots:

- hair roots looked too symmetric
- the scalp flow was not being restyled strongly enough
- the UI appeared non-functional because the visible render path still leaned on the old CPU fallback path
- there was no voxel/block style render mode

## Implemented changes

- Procedural sample assets are now restyled with `StyleAssetToScalpWig(...)`
- Imported assets are also lightly restyled after import
- Guide root distribution was changed from a square grid to an asymmetric golden-sequence scalp distribution
- Default sample parameters now use an off-center part and less symmetric silhouette values
- Follower root offsets were tightened and made less circular / less symmetric
- Default viewer debug overlays are mostly disabled
- GPU segment rendering is now the main visible path; CPU test mesh only renders when explicitly enabled
- Added a new `Voxel Blocks` render mode using `HairVoxel.hlsl`

## Known limits

- This was edited without Windows-side runtime validation in this environment
- The voxel mode is a brick-per-segment preview, not a sparse voxel volume system
- UI rebuild-sensitive settings still require `Apply Asset`
