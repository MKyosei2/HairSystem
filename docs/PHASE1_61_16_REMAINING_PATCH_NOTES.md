# Phase1 remaining patch for Novelty 61.16

Apply from the repository root:

```bat
python tools\final_stage\apply_phase1_remaining_patch.py
```

This package contains only the changed/new patch files. The patcher updates these existing files in-place:

- `HairSystem.vcxproj`
- `HairSystem.vcxproj.filters`
- `apps/dx11_viewer/src/GpuHairViewer.h`
- `apps/dx11_viewer/src/GpuHairViewer.cpp`
- `tools/final_stage/hairgpu_final_run.py`
- `tools/final_stage/schema.sql`
- `tools/final_stage/hairgpu_final_db.py`

It also writes:

- `tools/final_stage/schema_phase1_remaining_additive.sql`
- `docs/PHASE1_61_16_REMAINING_PATCH_NOTES.md`

The patch fills the remaining Phase1 scaffold gaps: VS project registration, formal 01-05 flags/envs, no-op hooks, telemetry placeholder output, DB schema/importer final5 columns, and runner env propagation.

After applying, build `HairSystem.sln` / `HairSystem.vcxproj` with `Debug|x64` or `Release|x64`, then run the final5 smoke cases (`Combo_00000`, `Combo_11111`).
