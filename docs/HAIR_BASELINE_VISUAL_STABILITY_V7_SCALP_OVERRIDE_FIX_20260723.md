# Hair baseline visual stability V7: scalp override fix

## Symptom

V6 follower density and alpha changes produced almost no visible difference. The
preview still looked severely bald and a large dark spherical surface covered the
center of the hairstyle.

## Root cause

Interactive initialization first set `m_showScalpMesh = false`, but a later
unconditional assignment set it back to `true`. The opaque mannequin scalp was
therefore rendered before the hair and depth-tested against every ribbon. Most
root and near-root coverage was visually hidden, so increasing follower density
could not materially change the screenshot.

## Fix

- Interactive preview: `m_showScalpMesh = false`
- Formal experiment: `m_showScalpMesh = true`
- Window title exposes `scalp=on/off` and `visualFix=V7`

No Novelty 01 state classification, certificate, wake, repair, or cache logic was
changed.
