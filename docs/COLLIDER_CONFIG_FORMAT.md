# Collider Config Format

Hybrid runtime CLI commands now accept `--colliders <file>`.

Supported rows in the collider CSV:

```csv
# comments are allowed
sphere,cx,cy,cz,radius
capsule,ax,ay,az,radiusA,bx,by,bz,radiusB
plane,px,py,pz,nx,ny,nz
aabb,minx,miny,minz,maxx,maxy,maxz
sdf,relative_or_absolute_sdf_grid_csv_path
```

## SDF grid CSV format

```csv
resolution,32,32,32
bounds_min,-0.15,-0.10,-0.15
bounds_max, 0.15, 0.20, 0.15
data,v0,v1,v2,...
```

`data` must contain exactly `resolutionX * resolutionY * resolutionZ` floating-point samples.
The values are interpreted as a signed distance field in row-major order `(z,y,x)`.

## Commands that accept `--colliders`

- `hybrid-teacher-dataset`
- `hybrid-train-bundle`
- `hybrid-run-bundle`

When omitted, the runtime falls back to the built-in teacher colliders.
