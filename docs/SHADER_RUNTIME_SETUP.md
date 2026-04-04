# DX11 Viewer Shader Runtime Setup

The DX11 viewer compiles HLSL files at runtime, so the executable must be able to read the source `.hlsl` files.

This complete version fixes startup in two ways:

1. every viewer shader is copied after build to both runtime locations:
   - `bin/<Config>/hairgpu_dx11_viewer/`
   - `bin/<Config>/hairgpu_dx11_viewer/shaders/`
2. the runtime shader loader searches additional source-tree locations and now reports whether each candidate path exists.

## Quick check

If startup still fails, verify these files exist:

- `bin/Debug/hairgpu_dx11_viewer/HairCpuMesh.hlsl`
- `bin/Debug/hairgpu_dx11_viewer/shaders/HairCpuMesh.hlsl`

Also confirm:

- startup project: `hairgpu_dx11_viewer`
- configuration: `Debug` or `Release`
- platform: `x64`
