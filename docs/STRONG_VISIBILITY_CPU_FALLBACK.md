# Strong Visibility CPU Fallback

This patch forces the DX11 viewer to draw a CPU clip-space fallback mesh whenever the GPU hair path is missing, visibility is suspect, or the previous GPU pipeline statistics show no real draw work.

Behavior changes:
- uses CPU fallback not only in FullParticlesDebug
- falls back when the particle pipeline is unavailable
- falls back when visibility recovery is still pending
- falls back when the previous GPU draw had zero VS/GS/PS activity
- injects a tiny emergency triangle only as a last-resort so the frame cannot remain fully black
