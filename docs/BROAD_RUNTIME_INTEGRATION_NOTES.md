# Broad Runtime Integration Notes

This prototype integrates several shallow, runtime-friendly approximations of literature ideas:

- ghost target pull-back for edited target shapes
- curl preservation via rest-space second-difference matching
- adhesion / clump preservation across nearby strands in the same group
- boundary smoothing against nearby non-active strands
- rest-preservation and root-alignment terms
- sag-free style pre-relaxation proxy for post-edit initialization
- strand-level shape matching proxy

These additions are intentionally lightweight and are meant as implementation hooks and ablation baselines rather than final publication-grade reproductions of the cited methods.
