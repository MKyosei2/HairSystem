# Hair Baseline Visual Stability V11 — Full Coverage

## Problem

V10 corrected the guide/follower layout and the initial parted flow, but the
interactive renderer still displayed only one third of followers while also
reducing width and alpha. The stochastic subset did not compensate its optical
coverage, so the preview looked bald even though all 100,000 strands existed.

## Baseline-only fix

- Normal interactive preview renders every follower (`RenderFollowerStride=1`).
- Fast edit modes may still use 1/N subsets. Their alpha is compensated with
  `1 - (1 - alpha)^N`, preserving approximate optical coverage.
- Stable Benchmark Wig keeps `WidthScale=0.82`; the later 0.68 override is removed.
- Root width/alpha floors are raised moderately without restoring the old solid
  helmet or curtain.
- Preset changes no longer re-enable the opaque scalp in interactive mode.
- Formal experiment settings remain unchanged.

Novelty01 state classification, certificates, predictive wake, repair, and
static/dynamic switching are not modified.
