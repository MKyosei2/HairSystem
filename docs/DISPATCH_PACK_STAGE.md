# Dispatch Pack Stage

This stage pushes the active-set runtime one step closer to a real GPU indirect-dispatch path.

## What changed

- Added a binary `dispatchbin` format for compacted active-set dispatch payloads.
- Added `ActiveSetDispatchPack` with:
  - compacted guide records
  - compacted strand records for full / corrective / reduced / frozen / cached tiers
  - per-tier total point counts
  - precomputed indirect dispatch args for strand-driven and point-driven kernels
- Added save/load helpers:
  - `SaveActiveSetDispatchPackToFile(...)`
  - `LoadActiveSetDispatchPackFromFile(...)`
- Added CLI commands:
  - `hairgpu_cli active-set-dispatch-pack ...`
  - `hairgpu_cli dispatchpack-info ...`
- `hybrid-run-bundle ... exportPerFrameHairBins=1` now also exports:
  - `full_hybrid_dispatch_pack_XXXX.dispatchbin`
  - `full_hybrid_dispatch_pack_XXXX.json`

## Why this matters

The previous JSON export was useful for inspection, but it was not a practical runtime artifact for a GPU path.
This new binary pack is much closer to what a renderer / simulation runtime actually wants:

- compacted worklists
- precomputed dispatch counts
- per-tier records that can be copied almost directly into GPU buffers

That means the next DX11 / GPU step no longer starts from a loose JSON description.
It starts from a stable binary intermediate that already matches indirect-dispatch style submission.

## Current scope

This is **not yet full GPU-generated indirect dispatch**.
The CPU still decides the active set and writes the compacted pack.

But the new pack already contains the exact intermediate data that the GPU path needs next:

- strand counts
- point counts
- compacted strand index records
- cluster-to-guide / cluster-to-strand ranges
- dispatch args for 64-thread groups

## New commands

```bash
hairgpu_cli active-set-dispatch-pack input.hairbin output.dispatchbin \
  [guidesPerCluster] [maxGuideRootDistance] [maxActiveClusters] \
  [maxActiveGuides] [promoteThreshold] [dumpJson=1]

hairgpu_cli dispatchpack-info output.dispatchbin
```

## Verified outputs

- `out_runtime_test3/test_dispatch_pack.dispatchbin`
- `out_runtime_test3/test_dispatch_pack.json`
- `out_runtime_test3/full_hybrid_dispatch_pack_0000.dispatchbin`
- `out_runtime_test3/full_hybrid_dispatch_pack_0000.json`

## Remaining next step

The best next implementation target is:

1. upload `dispatchbin` contents to structured/raw GPU buffers
2. consume those buffers from DX11 compute passes
3. switch the corrective / tiered simulation passes to indirect execution directly from the compacted tier payloads
