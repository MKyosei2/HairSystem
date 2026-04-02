# CPU collision + angular bend stage

This stage fills in a practical offline path for a few previously missing items that are expensive to finish end-to-end in the DX11 runtime first.

Implemented now:
- closest-surface scalp collision against the imported scalp mesh
- sampled scalp SDF collision using a generated volume grid
- angular bend relaxation for imported / noisy strand data
- SDF CSV export for debugging the generated scalar field

CLI examples:

```
hairgpu_cli relax-angular-bend asset.hairbin relaxed.hairbin 1.0 4 0.65 1 1
hairgpu_cli collide-scalp-mesh asset.hairbin collide_mesh.hairbin 0.0025 1.0 1 0 1
hairgpu_cli collide-scalp-sdf asset.hairbin collide_sdf.hairbin 28 0.0025 1.0 2 0 1 0.04
hairgpu_cli export-scalp-sdf asset.hairbin scalp_sdf.csv 20 0.04
```

Notes:
- These paths are CPU / authoring-side utilities. They do not replace the existing DX11 runtime collision path.
- The SDF sign is inferred from scalp triangle orientation and/or normals, so it works best with a reasonably coherent scalp mesh.
- Angular bend relaxation is designed as a cleanup pass, not as a physically exact runtime XPBD replacement.
