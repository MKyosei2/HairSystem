# Stable Test / Runtime Environment Completion

Target branch: `Novelty`

This document defines the minimum completed environment required before Novelty 01 implementation and formal paper measurements.

## Added now

### `tools/final_stage/hairgpu_completion_gates.py`

Final hard-gate validator added after the existing runtime suite. It validates:

- portable frozen bundle status, member count, sizes and SHA-256 hashes;
- requested / applied / observed settings parity;
- GPU model and VRAM profile;
- power plan, GPU temperature spread and graphics-clock drop;
- per-process RAM and GPU-memory availability;
- deterministic replay agreement for state, compaction and final-position hashes;
- long-run RAM/VRAM slopes and p99 frame-time degradation;
- required suite artifacts and complete/valid run counts.

Outputs:

- `TEST_ENVIRONMENT_COMPLETION.json`
- `TEST_ENVIRONMENT_COMPLETION.md`

A non-zero exit code means the environment is not eligible for formal evidence.

### `tools/final_stage/Run_Completed_Runtime_Environment.bat`

New single entry point. It runs the existing suite and then applies completion gates.

Examples:

```bat
set HAIRGPU_FREEZE_OPERATOR=Your_Name
tools\final_stage\Run_Completed_Runtime_Environment.bat docs\sample_assets\final_runtime_suite_template.json prepare-asset

tools\final_stage\Run_Completed_Runtime_Environment.bat docs\sample_assets\final_runtime_suite_template.json proposal-smoke out\proposal_smoke

tools\final_stage\Run_Completed_Runtime_Environment.bat docs\sample_assets\final_runtime_suite_template.json critical out\critical

tools\final_stage\Run_Completed_Runtime_Environment.bat docs\sample_assets\final_runtime_suite_template.json determinism out\determinism

tools\final_stage\Run_Completed_Runtime_Environment.bat docs\sample_assets\final_runtime_suite_template.json all out\formal_all
```

### `.github/workflows/windows-build-test.yml`

Adds two validation tiers:

1. GitHub-hosted Windows build and static/self-tests;
2. optional self-hosted GTX 1660 formal GPU smoke.

The self-hosted runner requires labels:

```text
self-hosted
Windows
X64
GTX1660
HairSystem
```

## Existing files that still require direct integration

The completion-gate layer was added without rewriting the large runtime implementation. The following existing-file changes remain required for a fully self-contained implementation.

### 1. `apps/dx11_viewer/src/GpuHairViewer.h`

Add the following two declarations to the public section of `GpuHairViewer`:

```cpp
int ExportCanonicalFrozenAssetSource(
    const std::filesystem::path& outputDirectory);

int ValidateCanonicalFrozenAssetSource(
    const std::filesystem::path& sourceDirectory);
```

Reason: `AppMain.cpp` calls these functions for `--export-frozen-source` and `--validate-frozen-source`. They must be public and declared in the class definition.

### 2. `tools/final_stage/Run_Final_Runtime_Suite.bat`

Add `hairgpu_completion_gates.py` to the Python syntax and self-test stage:

```bat
python -m py_compile ... "%~dp0hairgpu_completion_gates.py"
if errorlevel 1 exit /b %errorlevel%

python "%~dp0hairgpu_completion_gates.py" --self-test
if errorlevel 1 exit /b %errorlevel%
```

The new completed runner already performs the final gate, so existing users should switch to `Run_Completed_Runtime_Environment.bat` immediately.

### 3. `tools/final_stage/hairgpu_final_run.py`

The viewer settings output must contain three independent sections:

```json
{
  "requested": {},
  "applied": {},
  "observed": {}
}
```

Required values include:

- width / height;
- fixed timestep and timestep seconds;
- VSync and present interval;
- guide / follower / display strand counts;
- particles per guide;
- solver iterations;
- method code and every method flag;
- state storage mode;
- commit SHA;
- executable SHA-256;
- shader bundle SHA-256;
- asset manifest and asset SHA-256;
- GPU vendor, device and driver identity.

`hairgpu_completion_gates.py` rejects a run when any requested/applied/observed value is missing or differs.

### 4. `apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl`

Ensure the settings JSON exporter writes the three sections above rather than a single mixed settings object.

Also write these long-run fields into validation or summary output:

```text
unresolvedQueryCount
unresolvedQuerySlope
```

The unresolved query count should come from delayed GPU timing/counter query slots that remain unresolved beyond the allowed age.

### 5. `apps/dx11_viewer/src/GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl`

Add a monotonically sampled unresolved-query counter:

- increment when a timestamp/counter query remains unresolved beyond the allowed delay;
- never block the CPU waiting for the query;
- export source frame and query age;
- reset only at a new formal run;
- expose total and slope input to telemetry.

Formal long-run acceptance requires the unresolved-query slope to be zero.

### 6. `tools/final_stage/hairgpu_runtime_suite.py`

After every suite mode completes, invoke the final completion gate or use the new wrapper.

For native integration, add:

```python
completion_exit = subprocess.run([
    sys.executable,
    str(repo_root / "tools/final_stage/hairgpu_completion_gates.py"),
    "--config", str(config_path),
    "--output", str(output_dir),
    "--mode", run_kind,
], check=False).returncode
return max(worst_exit, completion_exit)
```

Do not execute the completion gate for `--dry-run`.

### 7. `benchmarks/novelty01/frozen_asset_bundle/`

Generate the canonical bundle once from a clean `Novelty` commit:

```bat
set HAIRGPU_FREEZE_OPERATOR=Your_Name
tools\final_stage\Run_Completed_Runtime_Environment.bat docs\sample_assets\final_runtime_suite_template.json prepare-asset
```

The generated bundle contains:

```text
frozen_asset_manifest.json
asset/novelty01_100k.hairbin
topology/novelty01_100k_topology.bin
settings/generator_settings.json
```

Do not regenerate it between B0-B8/P comparisons. All methods and seeds must use the same bundle SHA-256.

For distribution, store the binary bundle in Git LFS or a versioned release artifact and retain a small lock document in the repository containing the expected bundle SHA-256 and source commit.

### 8. `.gitignore`

Keep generated output ignored, but explicitly allow the frozen bundle lock document:

```gitignore
!benchmarks/novelty01/frozen_asset_bundle/frozen_asset_lock.json
```

Do not commit normal runtime output directories.

## Required execution order

1. Apply the public API declarations in `GpuHairViewer.h`.
2. Build Release x64.
3. Run native, shader and Python self-tests.
4. Generate and verify the frozen bundle from a clean commit.
5. Run proposal smoke.
6. Run comparison smoke.
7. Run critical scenarios.
8. Run determinism replays.
9. Run 30-minute soak.
10. Run 2-hour soak.
11. Run 8-hour soak.
12. Run completion gates with `--mode all`.

Formal evidence is accepted only when `TEST_ENVIRONMENT_COMPLETION.json` contains:

```json
{
  "status": "PASS",
  "issues": []
}
```
