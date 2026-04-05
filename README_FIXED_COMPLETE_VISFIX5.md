HairSystem VISFIX5 complete source package

What changed:
- GpuHairViewer now recomputes asset bounds and auto-frames the camera after startup, reset, rebuild, and import.
- Render path estimates visible particle count every frame and triggers recovery when visibility looks broken.
- Window title now includes a build marker: "Hair GPU Viewer VISFIX5".
- Particle shader has a larger minimum radius and stronger visibility.

How to verify you are running the new build:
1. Clean the solution.
2. Delete bin, bin-int, and .vs if needed.
3. Rebuild on Windows.
4. Launch the rebuilt dx11 viewer EXE directly from the build output folder.
5. Confirm the title bar starts with: Hair GPU Viewer VISFIX5

Files changed:
- apps/dx11_viewer/src/GpuHairViewer.h
- apps/dx11_viewer/src/GpuHairViewer.cpp
- apps/dx11_viewer/shaders/HairParticle.hlsl
- bin/Debug/hairgpu_dx11_viewer/HairParticle.hlsl
- bin/Debug/hairgpu_dx11_viewer/shaders/HairParticle.hlsl
