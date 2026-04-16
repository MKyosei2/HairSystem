# Heavy Features Now Prototyped

This branch broadens the previous prototype by adding lightweight, runtime-friendly approximations for the previously missing items:

- Curly-Cue inspired coil transport, phase-lock, period skipping, and switchback offsets
- Stable-Cosserat-inspired local bend/twist proxy correction
- stronger sag-free pre-relaxation passes
- Kajiya-Kay / Marschner / dual-scattering inspired preview shading in `HairRibbon.hlsl`

These are not faithful reproductions of the original papers. They are integration-oriented approximations intended for ablation, UI testing, and rapid iteration before deeper research versions are implemented.
