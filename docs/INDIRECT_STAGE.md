# Indirect Stage

This stage pushes the viewer further away from CPU-driven submission.

## Added

- `DispatchIndirect` for the follower reconstruction pass
- `DrawIndexedInstancedIndirect` for the instanced tube render path
- Render-phase rotation for follower decimation so mid/far LOD does not always drop the same follower subset

## Notes

The indirect argument buffers are still populated by CPU-side `UpdateSubresource`, so this is not yet a fully GPU-generated argument path.
The next logical step is GPU-written args via append/count or reduction passes.
