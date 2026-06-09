# Clean source package

This package is generated from the supplied ZIP as a source-focused complete tree.

Removed from the clean package:

- compiled binaries and debug symbols (`bin/`, `bin-int/`)
- CMake/Visual Studio build output (`build/`)
- runtime profile output (`profile_results/`)
- backup/intermediate files such as `.bak`, `.modified`, `.obj`, `.pdb`, `.exe`, `.pyc`
- patch-only helper scripts that are no longer needed after integration

Integrated/normalized:

- root `CMakeLists.txt` was restored so the tree configures from the repository root
- `src/hairgpu_core/CMakeLists.txt` now includes `GlobalHairBudget.cpp`, `HairNoveltyFlags.cpp`, and `HairNoveltyTelemetry.cpp`
- `apps/dx11_viewer/CMakeLists.txt` has a complete ordered shader list including `ScalpCoverage.hlsl`
- Phase1 remaining novelty flags, no-op hooks, and telemetry placeholders are connected directly in the source tree
- final-stage DB/schema/runner code is updated for the final5 Phase1 columns

Build notes:

```bash
cmake -S . -B build -DHAIRGPU_BUILD_DX11_VIEWER=OFF
cmake --build build
```

On Windows, the DirectX 11 viewer can be enabled with `-DHAIRGPU_BUILD_DX11_VIEWER=ON`.
