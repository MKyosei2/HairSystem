# Hair baseline visual stability V6 — coverage balance

This patch corrects the over-thinning introduced by V5 without restoring the former cap/curtain artifact.

## Scope

- Baseline renderer and preview constants only.
- No Novelty 01 state classification, certificate, wake, repair, or cache-decision logic is changed.

## Changes

- Normal preview: deterministic stochastic 1/3 follower subset instead of the same modulo slots at every guide.
- Fast groom preview: 1/4 followers; heavy interaction: 1/8.
- Preview material alpha raised moderately while keeping hair depth-write disabled.
- Root width opening and root alpha restored to cover the scalp.
- Interactive width scale raised from 0.58 to 0.68.
- Formal experiment still requests stride 1 and preserves the 100,000-strand draw contract.

The stochastic selection is deterministic per strand, so screenshots and repeated runs remain stable.
