# Long-run hair stability correction

## Symptom

After a long Visual Studio/F5 capture, some guide families became horizontal slabs,
root-local explosions, or dense rectangular masses. The length guard prevented
unbounded segment extension, but it could not restore a guide whose direction had
already drifted.

## Root causes

1. Distance and bend XPBD multipliers were preserved across simulation time steps.
   XPBD warm starting is valid inside one time step; carrying the same multipliers
   across thousands of frames accumulated a directional bias.
2. The previous bend constraint controlled only the magnitude of the midpoint
   offset. It did not preserve a rotationally invariant rest-shape relation, so a
   guide could fold/rotate while every segment still satisfied its length.
3. Static runs rebound each root to the raw scalp barycentric position. The authored
   wig root contains a small scalp-normal lift, so this injected root-segment stress
   every frame.
4. One damaged guide was copied to 24 followers, amplifying a single failure into a
   dense visible slab.

## Correction

- Clear distance and bend multipliers once at the start of every simulation step.
- Replace the direction-free bend rule with an authored rest-chord constraint.
- Preserve the authored root while scalp animation is disabled.
- Run a finite/envelope/segment validation pass after the final collision and before
  follower reconstruction.
- In interactive/F5 runs, catastrophically corrupted guides recover to translated
  authored rest shape. Formal experiment runs disable silent recovery so failures
  remain visible to validators.
- Validate follower parent indices and reject non-finite or implausibly large targets.

## Evidence boundary

This correction is shared by B0-B8 and P. It is infrastructure stability, not a
Novelty 01 advantage. Formal comparisons must use the same correction for every
method. Visual Studio debugger output remains diagnostic evidence; paper values must
still be collected by the formal Release x64 runtime suite.

## Expected output

The package keeps the fixed workload: 4,000 guides, 24 followers per guide, 100,000
visible strands, 128 particles per strand, and 6 solver iterations. It does not lower
strand count, particle count, or authored hair length to hide the failure.
