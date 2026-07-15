# Novelty 01 Safe Evaluation Protocol

## 1. Purpose

This protocol validates the Segment Group runtime without changing its core representation, ownership, state, wake, repair, or GPU-dispatch semantics. It separates three questions:

1. **Evidence integrity** — was the exact source, asset, scenario, method, machine, and measured frame stream identified and validated?
2. **Correctness and safety** — did False Static, wake delay, boundary error, collision miss, penetration, repair, determinism, and fatal-failure gates pass?
3. **Scientific support** — does the integrated Segment Group representation outperform the same decision logic stored in external arrays?

A hypothesis may be unsupported while the evidence remains valid. The suite must never rewrite an unfavorable result as an invalid measurement.

## 2. Files introduced or strengthened

- `tools/final_stage/hairgpu_safe_runtime_suite.py`
  - resolves the clean Git HEAD instead of trusting a stale hard-coded commit;
  - expands and reproducibly randomizes the method/scenario/seed matrix;
  - hashes timelines, method settings, full run settings, and the asset manifest;
  - rejects missing matched baselines instead of silently skipping them;
  - validates fixed timestep, real measurement, source commit, 100k strand count, exact SHA formats, runtime feature acknowledgement, fatal failures, and frame samples;
  - validates deterministic replay using canonical result hashes;
  - reports evidence integrity separately from hypothesis support.
- `tools/final_stage/freeze_novelty01_asset.py`
  - freezes the real asset, topology, generator settings, and source commit using local bytes.
- `include/hairgpu/Validation.h`
  - provides the same evidence rules for C++ callers without changing Novelty 01 core behavior.
- `tests/final_stage/test_hairgpu_safe_runtime_suite.py`
  - protects the evaluator itself against regression.

## 3. Mandatory preparation

### 3.1 Apply and commit the patch

The final suite uses `source.commit=AUTO` with `commit_mode=git_head_clean`. The worktree must be clean. Do not collect paper evidence from uncommitted source.

```bat
git status
git add include tools tests docs benchmarks
git commit -m "61.55 strengthen novelty01 evidence gates"
```

### 3.2 Run evaluator regression tests

```bat
tools\final_stage\Run_Safe_Runtime_Suite_Tests.bat
```

Expected result: all tests pass. A failed evaluator test blocks all runtime evidence.

### 3.3 Build Release x64

Use the repository's existing Windows Release build path. Required conditions:

- `Release`
- `x64`
- debug draw and validation visualization disabled during measurement
- VSync disabled
- frame cap disabled
- fixed timestep `1/60 s`
- delayed GPU timestamp readback; no same-frame blocking readback

### 3.4 Freeze the 100k asset

Do not manually type hashes. Use the freeze tool after the patch is committed.

```bat
python tools\final_stage\freeze_novelty01_asset.py ^
  --manifest docs\sample_assets\novelty01_100k_asset_manifest.json ^
  --asset D:\HairBench\novelty01_100k.asset ^
  --generator-settings D:\HairBench\generator_settings.json ^
  --topology-file D:\HairBench\generated_topology.bin ^
  --operator <name>
```

The tool requires a clean Git worktree and writes:

- source asset path, size, and SHA-256;
- generated topology SHA-256;
- generator settings SHA-256;
- clean source commit SHA;
- UTC freeze time and operator;
- `freezeStatus=FROZEN`.

After freezing, commit the manifest. If the asset, topology, generator settings, or source changes, invalidate the old evidence and freeze again.

## 4. Static preflight

Run without launching the viewer:

```bat
tools\final_stage\Run_Final_Runtime_Suite.bat ^
  docs\sample_assets\final_runtime_suite_template.json ^
  --prepare-only
```

The preflight rejects:

- dirty or unresolved source state;
- unfrozen or missing asset;
- malformed or mismatched hashes;
- missing scenario timeline;
- scenario identity mismatch;
- too-short timeline;
- wrong fixed timestep;
- fewer than five independent seeds;
- missing B0–B8 or proposal method;
- duplicate case IDs;
- wrong Release/x64/1080p/VSync/100k settings;
- unknown hardware profile;
- incomplete determinism replay contract.

`--allow-unfrozen-asset` is development-only. Results produced with this escape hatch are not paper evidence.

## 5. Final run matrix

Primary matrix:

- 18 deterministic scenarios;
- 10 methods: B0–B8 plus proposal;
- 5 independent seeds;
- 900 primary runs.

Deterministic replay adds 8 runs:

- proposal: walk, False Static trap, repair storm;
- external-state baseline: walk;
- two identical repetitions per replay group.

The primary execution order is shuffled with a fixed order seed. Case IDs remain stable, and the expanded order is recorded. Randomization reduces thermal and temporal order bias while preserving reproducibility.

## 6. Comparison hierarchy

| Code | Method | Purpose |
|---|---|---|
| B0 | Full Dynamic | correctness and absolute-cost reference |
| B1 | Strand Sleep | coarse sleeping reference |
| B2 | Fixed Block Sleep | block granularity reference |
| B3 | Segment threshold sleep | simple local sleep |
| B4 | Segment sleep + hysteresis | state-thrash control |
| B5 | Segment sleep + cache | cache contribution |
| B6 | Segment sleep + GPU compaction | strong execution baseline |
| B7 | Active Set only | budget scheduler baseline |
| B8 | External state arrays with the same decisions | isolates representation integration |
| P | Integrated Segment Group representation | proposal |

The primary novelty comparison is **P versus B8**, not P versus Full Dynamic alone. B8 must use the same Certificate, Wake, velocity recovery, Repair, compaction, and Active-Set decisions while storing state outside the Segment Group record.

## 7. Runtime telemetry contract

Every run must identify:

- case ID, method, scenario, seed, hardware profile;
- clean source commit;
- executable, shader, frozen asset, settings, timeline, manifest, and method-settings hashes;
- Release/x64, real measurement, VSync, fixed timestep, resolution, displayed strand count;
- CPU, GPU, driver, OS;
- canonical deterministic result hash;
- fatal runtime failure count;
- all required Segment Group state, wake, repair, collision, and pass-level GPU fields.

Every requested method setting must appear in telemetry and equal the requested value. The canonical `appliedMethodSettingsSha256` must equal the suite-generated method-settings hash.

## 8. Frame measurement

Per run:

- warm-up: at least 600 frames;
- accepted measurement: at least 6000 positive finite frame samples;
- invalid frame samples: zero;
- resolution: 1920 × 1080;
- fixed timestep: `0.016666666666666666 s` within `1e-5 s`;
- VSync: disabled.

Report:

- average, p50, p95, p99, p99.9, worst frame time;
- 60 FPS ratio;
- 16.67 ms overrun rate;
- each hair GPU pass;
- total hair GPU time and total frame time;
- dynamic/static/repair group ratios;
- quality and safety metrics.

Do not use `estimatedFrameMilliseconds` as measured FPS evidence.

## 9. Matched statistical analysis

Each non-baseline run is paired with B8 using identical:

- scenario;
- seed;
- frozen asset;
- hardware profile.

A missing pair is an error. The evaluator produces:

- per-pair deltas;
- scenario-level mean deltas;
- 95% bootstrap confidence intervals;
- overall method deltas across all scenarios;
- pair coverage rate.

The integrity gate requires all expected pairs and at least five matched seeds per scenario/method comparison.

## 10. Determinism

Each replay group must provide an exact 64-character `canonicalResultSha256`. Identical source, asset, settings, timeline, method, scenario, and seed must produce the same canonical result hash.

Any mismatch blocks evidence integrity. A canonical result should exclude timestamps, wall-clock values, paths, and unordered map iteration artifacts.

## 11. Safety gates

Recommended final thresholds:

| Metric | Gate |
|---|---:|
| False Static rate | ≤ 2%, hard ceiling 5% |
| Repair success rate | ≥ 95% |
| Wake delay | 0–1 frame target |
| Collision miss rate | ≤ 1% |
| State oscillation rate | ≤ 2% |
| Static/Dynamic boundary maximum error | ≤ 1.0 mm or ≤ 1% group length, whichever is stricter |
| Frame-budget overrun rate | ≤ 5% |
| Determinism mismatches | 0 |
| Fatal runtime failures | 0 |
| Invalid frame samples | 0 |

The proposal must not be considered safe merely because average FPS improves. Tail latency, False Static, collision, boundary, wake, repair, and determinism gates are jointly required.

## 12. Output interpretation

`safe_evidence_report.json` contains two independent conclusions:

- `evidenceIntegrityPassed`: whether the evidence is admissible;
- `allHypothesesSupported`: whether all declared scientific hypotheses were supported.

Valid but unfavorable measurements are retained. A result is publishable only after the integrity gate passes; unsupported hypotheses must be reported as such.

## 13. Unavoidable manual work

This patch cannot manufacture real Windows/DX11 measurements or a real 100k asset hash. The following remain machine-dependent:

1. commit the applied patch;
2. freeze the actual asset/topology/generator settings;
3. build and run Release x64 on the declared hardware;
4. make the viewer emit the required telemetry and canonical result hash;
5. execute the full matrix and archive outputs.

Until those steps are completed, performance values are pending and must not be described as measured results.
