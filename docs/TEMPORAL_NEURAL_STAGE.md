# Temporal Neural Stage

This stage adds a lightweight learned temporal layer on top of the existing reduced-model hybrid runtime.

## What was added

- `TemporalGuideNetwork` in `include/hairgpu/TemporalModel.h` and `src/hairgpu_core/TemporalModel.cpp`
- Binary save/load:
  - `prior.temporalbin`
  - `residual.temporalbin`
- Hybrid bundle integration:
  - `HybridModelBundle::priorTemporalNetwork`
  - `HybridModelBundle::residualTemporalNetwork`
  - `priorTemporalBlend`
  - `residualTemporalBlend`
- Hybrid training report integration:
  - `priorTemporalEvaluation`
  - `residualTemporalEvaluation`
- Runtime integration:
  - prior temporal correction is blended into prior guide prediction
  - residual temporal correction is blended into residual guide input

## Model shape

The temporal layer is a shared point-wise MLP over guide points.

Input features per guide point:
- current displacement from rest (xyz)
- velocity (xyz)
- acceleration (xyz)
- normalized point position along strand
- normalized strand index
- rest-space distance from strand root
- displacement magnitude

Output:
- predicted/rest-relative guide-point displacement (xyz)

Root points are locked by default.

## Training wiring

### Prior temporal network
- input sequence: raw selected guide points from teacher frames
- target sequence: selected guide points from the smoothed prior sequence

### Residual temporal network
- input sequence: base residual guide input
  - `rest + (teacherGuide - priorGuide) * residualGuideGain`
- target sequence: selected teacher guide points for the residual model layout

## Runtime wiring

### Prior path
The existing velocity/acceleration extrapolation remains.
Then the temporal prior network predicts corrected guide positions and blends them by `priorTemporalBlend`.

### Residual path
The existing residual guide input remains.
Then the temporal residual network predicts corrected guide positions and blends them by `residualTemporalBlend`.

## Verification

Verified on Linux build:
- `hybrid-teacher-dataset`
- `hybrid-train-bundle`
- `hybrid-run-bundle`
- `hybrid-bundle-info`

Generated bundle files include:
- `prior.temporalbin`
- `residual.temporalbin`

Observed stage-6 bundle summary:
- `priorTemporalRms = 0.000898`
- `residualTemporalRms = 0.001727`
- `priorTemporalBlend = 0.457180`
- `residualTemporalBlend = 0.517271`

## Remaining limitations

This is still not a full production neural hair stack.
What remains outside this stage:
- real Alembic SDK integration
- DX11 consumption of dispatch/gpu buffers
- true GPU-generated indirect dispatch
- a larger sequence model / offline trainer beyond this lightweight in-engine MLP layer
