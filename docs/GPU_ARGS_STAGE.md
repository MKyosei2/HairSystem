# GPU Args Stage

This stage moves indirect submission one step further toward GPU ownership.

## What changed

- Added `HairIndirectArgs.hlsl`
- Added `BuildFollowDispatchArgsCS`
- Added `BuildDrawArgsCS`
- Indirect args buffers are now created with UAV access and raw views
- CPU-side `UpdateSubresource(...)` for follow dispatch args was removed
- CPU-side `UpdateSubresource(...)` for instanced draw args was removed
- Follow pass now uses:
  1. GPU compute writes dispatch args
  2. `DispatchIndirect(...)`
- Instanced tube render now uses:
  1. GPU compute writes draw args
  2. `DrawIndexedInstancedIndirect(...)`

## Scope

This is still not full GPU culling/compaction. The CPU still decides the LOD constants and writes constant buffers.
But the actual indirect argument payload is now generated on the GPU.

## Next step

- GPU compacted visible follower lists
- Near/mid/far tier counters written on GPU
- Draw/dispatch args sourced from those counters
