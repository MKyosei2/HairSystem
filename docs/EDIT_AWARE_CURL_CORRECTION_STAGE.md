# Edit-Aware Curl and Boundary Corrective Stage

This stage adds a broader corrective pass on top of the existing sparse active-set runtime.

## Added corrective components

### 1. Ghost rest target correction
Selected strands are softly pulled toward an anchored rest pose translated by the current root.
This acts as an edit-aware target stabilizer and is useful as a lightweight alternative to a full AMS-style ghost solver.

New settings:
- `ghostRestBlend`
- `ghostAngularBlend`
- `enableGhostRestShape`

### 2. Curl preservation
Selected strands preserve local rest-space second-order curvature using a transported rest target.
This is a lightweight curl target proxy for wavy / curly hairstyles.

New settings:
- `curlPreservationBlend`
- `enableCurlPreservation`

### 3. Adhesion / clump preservation
Nearby strands in the same group softly attract each other at corresponding local points.
This preserves clumps and supports coarse curly-hair bundle behavior.

New settings:
- `adhesionBlend`
- `clumpRootRadius`
- `enableAdhesion`

### 4. Boundary smoothing
Selected strands near non-selected strands are blended toward nearby non-selected neighbors, scaled by the active-set boundary score.
This targets edit boundary continuity.

New settings:
- `boundaryBlend`
- `boundaryRootDistance`
- `enableBoundarySmoothing`

### 5. Rest preservation
A small blend back toward the original frame pose is used to avoid over-correction drift.
This is a lightweight proxy for non-edit preservation.

New settings:
- `preserveRestBlend`
- `enableRestPreservation`

## Intended usage

The implementation is designed as a best-effort integrated prototype stage:
- It is runtime-cheap compared to a full rod solve.
- It integrates directly into `ApplySparseCorrectivePhysics`.
- It is most useful for active-set selected clusters where edit boundaries, clump coherence, or curl retention matter.

## Important note

This is an integrated prototype stage, not a final SIGGRAPH-ready solver.
It provides implementation hooks and measurable behavior for:
- edit-aware correction,
- boundary naturality,
- non-edit preservation proxy,
- curl retention,
- clump retention.

These hooks are intended for further ablation and evaluation.
