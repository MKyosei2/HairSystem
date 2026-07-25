# Hair baseline visual stability V10

This baseline-only fix does not modify Novelty 01 state classification, certificates, predictive wake, repair, or static/dynamic transitions.

## Fixed regressions

- Removed implicit viewport selection when an edit layer is loaded into the Quick Edit form.
- Default interactive startup enters Sim/View with no selected strand. Set `HAIRGPU_START_IN_GROOM=1` to opt into Groom/Edit startup.
- Selected-strand points and transform gizmo are drawn only in explicit Groom/Edit workspace.
- Replaced circular crown-whorl flow with a deterministic side-part flow field.
- Shortened the scalp-shell phase from 36% to 22% of strand length.
- Added a bounded crown lift and side-part separation before gravity dominates.
- Bumped procedural cache generation to V10.

## Expected visual result

The large yellow comb/gizmo must not appear at startup. The crown should split toward both sides of the part instead of forming a spherical cap and one central curtain.
