# Thesis Practical Evaluation Overlay

## Purpose

This overlay makes the local-edit claim measurable without changing the existing
Novelty01 64-counter ABI.

It adds:

- real Guide edit input through `ApplyEditStackToGuideFamiliesSparse`
- edit episodes written to `*_thesis_edits.csv`
- direct Segment Group / Block counts for each edit
- proposal-only local Block invalidation
- B0 remains fail-closed Full Dynamic
- P falls back to full invalidation if the local invalidation pass cannot run
- T00-T07 practical edit-scale cases
- report generation that joins edit episodes with the existing frame and
  Novelty01 telemetry

## Important evaluation interpretation

`B0` is still the baseline implementation in this repository. This patch does
not claim that B0 is a reproduction of a specific paper.

For P, the practical local-edit path does **not** bump the global edit version
when local Block invalidation is successfully queued. Instead, only Blocks
containing directly edited Guide segments receive `Dirty_Edit`. The existing
SG-APT classification then treats those Blocks as dynamic and the ordinary
Follower/Render dirty-range propagation continues downstream.

If any prerequisite is unavailable, the patch increments the normal edit
version and requests the existing full-dynamic path. This is intentionally
fail-closed.

## Output

With `HAIRGPU_THESIS_EDIT_EVAL=1`, each run can create:

`<run_name>_thesis_edits.csv`

The existing runner still owns:

- `<run_name>_frames.csv`
- `<run_name>_novelty01.csv`
- quality/determinism/soak evidence

Use `hairgpu_thesis_practical_report.py` to join the edit episode with those
existing measurements.

## Recommended order

1. Apply overlay
2. Run contract test
3. Build Release x64
4. Run the existing Novelty01 smoke/preflight
5. Run T01 first with `--profile smoke`
6. Run B0/P pilot for T01-T07
7. Generate the thesis practical report
8. Only after the pilot is clean, run formal/performance profiles

Do not use results from a failed evidence-completeness report in the thesis.
