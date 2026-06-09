# Clean reordered complete source package

This package is intended as a full replacement for the source-focused contents supplied in `apps(2).zip`.

Recommended replacement workflow:

1. Back up the current project.
2. Delete old generated/output directories such as `bin/`, `bin-int/`, `build/`, and `profile_results/`.
3. Extract this ZIP at the repository root.
4. Configure/build from the root `CMakeLists.txt`.

What was cleaned:

- Removed binary/build/profile output from the submitted ZIP.
- Removed backup/intermediate files such as `.bak.modified`, `.obj`, `.pdb`, `.exe`, `.tlog`, and transient logs.
- Integrated the Phase1 final5 wiring directly in source instead of keeping patch-only helper scripts.
- Reordered the largest monolithic source files into numbered include parts while keeping one compiled `.cpp` wrapper per original translation unit.

Large-file layout:

- `apps/dx11_viewer/src/GpuHairViewer.cpp` now includes ordered parts under `GpuHairViewer_parts/`.
- `apps/hairgpu_cli/src/main.cpp` now includes ordered parts under `main_parts/`.
- `src/hairgpu_core/HybridRuntime.cpp` now includes ordered parts under `HybridRuntime_parts/`.

Build notes:

```bash
cmake -S . -B build -DHAIRGPU_BUILD_DX11_VIEWER=OFF
cmake --build build
```

On Windows, set `-DHAIRGPU_BUILD_DX11_VIEWER=ON` to build the DirectX 11 viewer.
