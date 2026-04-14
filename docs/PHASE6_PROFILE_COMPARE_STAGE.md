# Phase 6: Submission/Maximum Profile Compare Stage

This stage adds repeatable CLI-side evaluation for the new segment-budget pipeline.

## New command

```bash
hairgpu_cli hybrid-profile-compare <teacher_dir> <bundle_dir> <output_dir> [profileSet=dev16,standard128,ideal1024,max2048] [--colliders colliders.csv]
```

## Purpose

The command runs the **same full hybrid runtime** across multiple segment-budget profiles and writes:

- `hybrid_profile_compare_summary.csv`
- `hybrid_profile_compare_summary.json`
- `hybrid_profile_compare_summary.md`

## Profiles

- `dev16`: development baseline, fixed around 16 segments/strand
- `standard128`: submission baseline, 128 segments/strand with limited local refinement
- `ideal1024`: submission baseline with local refinement up to 1024 segments/strand
- `max2048`: submission baseline with local refinement up to 2048 segments/strand

## Output metrics

The compare report now tracks runtime quality and segment-budget behavior together:

- `avgNaturalnessGain`
- `avgEfficiency`
- `avgEditCoverage`
- `avgSelectedParticles`
- `maxSelectedParticles`
- `avgPlanMemoryMB`
- `avgEstimatedFrameMs`
- `p95EstimatedFrameMs`
- `hitRate60Fps`
- `avgPenetration`
- `maxPenetration`

## Why this matters

The goal is no longer only “which method is better?”

Now the submission can answer:

> How much quality improvement do we obtain when the standard 128-segment runtime is allowed to refine locally up to 1024 or 2048 segments, while staying under the fixed 60 FPS / 32 GB budget model?

That directly supports the thesis claim around a **hard-budget, segment-adaptive editable hair runtime**.
