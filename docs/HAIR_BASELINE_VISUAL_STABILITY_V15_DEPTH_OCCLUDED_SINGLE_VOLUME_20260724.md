# Hair baseline visual stability V15

The persistent inner shell was caused by two independent baseline rendering/asset issues:

1. Back-side transparent hair remained visible because interactive preview hid the scalp and hair did not write depth.
2. Procedural mid-length volume expanded the strand radius by several centimeters.

V15 adds an invisible depth-only scalp occluder and reduces the artificial radial volume. Novelty 01 logic is unchanged.
