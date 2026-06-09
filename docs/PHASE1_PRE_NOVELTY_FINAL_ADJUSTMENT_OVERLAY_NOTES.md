# Phase1 Pre-Novelty Final Adjustment overlay

This ZIP is rooted at the HairSystem repository root and is intended for the `Novelty` branch.

## Contents

- Common Phase1 scaffold:
  - `include/hairgpu/HairNoveltyFlags.h`
  - `include/hairgpu/HairNoveltyTelemetry.h`
  - `include/hairgpu/GlobalHairBudget.h`
  - `src/hairgpu_core/HairNoveltyFlags.cpp`
  - `src/hairgpu_core/HairNoveltyTelemetry.cpp`
  - `src/hairgpu_core/GlobalHairBudget.cpp`
- Final5 manifest generator:
  - `tools/final_stage/generate_experiment_final5.py`
- Ready-generated smoke manifest:
  - `results/exp_final5/experiments_final5.jsonl`
  - `results/exp_final5/runtime_suite_final5.json`
- Local patch helper:
  - `tools/final_stage/apply_phase1_patch.py`

## Apply

1. Extract this ZIP at the repository root of `Novelty`.
2. Run:

```bat
python tools\final_stage\apply_phase1_patch.py
```

3. Build `HairSystem.sln` in `Debug|x64`.

The patch helper modifies the large existing `GpuHairViewer.h/.cpp` and `HairSystem.vcxproj` in-place. It is intentionally idempotent.
