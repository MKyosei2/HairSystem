# GPU Buffer and USDA Sequence Stage

## Added in this stage

- `dispatchpack-gpubuffers <input.dispatchbin> <output_dir>`
  - Loads an existing active-set `dispatchbin`
  - Writes GPU-ready raw binary buffers plus `dispatch_gpu_manifest.json`
  - Output files:
    - `full_simulation_clusters.bin`
    - `full_simulation_guides.bin`
    - `full_simulation_strands.bin`
    - `corrective_strands.bin`
    - `reduced_simulation_strands.bin`
    - `frozen_strands.bin`
    - `cached_strands.bin`
    - `dispatch_indirect_args.bin`
    - `dispatch_gpu_manifest.json`

- `haircache-export-usda-sequence <input.haircache> <referenceAsset> <output_dir> [everyNth=1] [includeScalp=1] [includeCards=0]`
  - Exports a full strand animation cache as a per-frame USDA sequence
  - Emits `sequence_manifest.json`

- `guidecache-export-usda-sequence <input.guidecache> <referenceAsset> <output_dir> [everyNth=1] [includeScalp=1]`
  - Exports a guide-only animation cache as a per-frame USDA sequence
  - Emits `sequence_manifest.json`

## Why this matters

- The dispatch side now has a concrete bridge from `dispatchbin` to GPU-consumable raw buffers.
- The interchange side now has a practical Alembic-free export path for animated results using USDA frame sequences.
- This makes it easier to connect the runtime to DX11/Vulkan/compute buffer upload code while still being able to inspect and archive animated outputs without Alembic.
