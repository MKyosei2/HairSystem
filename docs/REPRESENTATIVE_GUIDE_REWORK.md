# Representative-guide rework

This package was updated to move the heavy path away from per-frame CPU hair-mesh rebuilding and toward a guide/follower system.

## Main changes

- CPU readback + `BuildHairMeshVertices(...)` is now skipped unless **Show Test Mesh** is enabled.
- Default render mode is now **Ribbon** instead of **TubePreview**.
- Default solver iterations were reduced to **3** for interactive work.
- Follower strands now support:
  - up to 3 guide references
  - guide blend weights
  - per-follower radial / side / lift offsets
  - per-follower length scaling
  - per-follower phase offsets for controlled secondary variation
- The GPU `FollowCS` pass reconstructs follower strands from blended guides instead of copying a single parent guide.
- CSV import/export was extended so the extra representative-guide parameters can round-trip.

## Files changed

- `include/hairgpu/Types.h`
- `src/hairgpu_core/SampleAsset.cpp`
- `src/hairgpu_core/AssetIO.cpp`
- `apps/dx11_viewer/src/GpuHairViewer.h`
- `apps/dx11_viewer/src/GpuHairViewer.cpp`
- `apps/dx11_viewer/shaders/HairSim.hlsl`

## Resulting behavior

The new path is designed for a "representative hairs + procedural followers" workflow:

1. Simulate only guide strands.
2. Reconstruct followers from the guides on the GPU.
3. Keep the render topology stable and avoid rebuilding CPU tube geometry every frame.

This keeps the system much closer to a scalable long-hair architecture for future Unreal / Maya / Blender integration.
