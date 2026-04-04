# Generated Complete-Version Bundle

This generated bundle integrates the missing research-facing runtime pieces on top of the uploaded GPU hair base:

- Active-set planning is compiled into `hairgpu_core`.
- Sparse corrective physics and hybrid runtime utilities are added via `HybridRuntime.*`.
- `hairgpu_cli` now exposes:
  - `active-set-plan`
  - `hybrid-teacher-dataset`
  - `hybrid-train-bundle`
  - `hybrid-bundle-info`
  - `hybrid-run-bundle`
- The DX11 viewer source is updated to use the active-set plan and sparse corrective pass at runtime.
- The native Visual Studio solution now includes `hairgpu_cli`, and `hairgpu_core` includes the new runtime files.

## Verified in this environment

The following was validated in a Linux CMake build with the DX11 viewer disabled:

```bash
cmake -S . -B build_linux -DHAIRGPU_BUILD_DX11_VIEWER=OFF
cmake --build build_linux -j2
```

That confirms the core library and CLI compile successfully after integration.

## Important limitation

The DX11 viewer and native Visual Studio solution cannot be executed in this container, so the Windows-side viewer path is not runtime-verified here. The source has been integrated, but final DX11 validation still needs to happen on a Windows machine with Visual Studio 2022.


## CLI smoke test results captured in this environment

Using `generated_scaffold/asset_normalized.hairbin`:

- `active-set-plan` completed successfully.
- `hybrid-teacher-dataset` completed successfully for a 12-frame test dataset.
- `hybrid-train-bundle` completed successfully.
- `hybrid-run-bundle` completed successfully.

Observed outputs from the smoke test:

```text
active-set-plan:
clusters=1 selectedClusters=1 selectedGuides=1 selectedStrands=2 highestScore=0.168389 averageSelectedScore=0.168389 lowestSelectedScore=0.168389 budgetScale=1 recommendedSolverIterations=6 tierCounts=2/0/0/0

hybrid-teacher-dataset:
frames=12 avgSelectedClusters=1 avgSelectedGuides=1 avgSelectedStrands=2 avgPenetration=0.0426561 peakEstimatedSimulationMs=2.3602

hybrid-train-bundle:
priorGuides=8 residualGuides=12 residualBlend=1.00000 smoothingAlpha=0.18000 budgetClusters=6 budgetGuides=32 priorRms=0.12748 residualRms=0.00000 avgResidualMag=0.00181

hybrid-run-bundle:
frames=12 avgPriorRms=0.127099 avgHybridRmsBefore=0.000584394 avgHybridRmsAfter=0.0140341 avgSelectedClusters=1 avgSelectedGuides=1 avgPenetration=0.00675456
```
