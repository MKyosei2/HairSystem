# Continued fast-track pass

Added in this pass:

- Step-through simulation button for paused debugging
- Debug color modes in the hair shaders
  - Natural
  - Guide / Follower
  - Width Heatmap
- Floor guide wireframe overlay
- Self-collision grid bounds overlay
- Performance-history sampling in memory
- CSV export of performance history for research plots and graduation-project reporting

## Why this helps

These additions are aimed at making the viewer more useful as a research/demo tool under time pressure:

- You can pause and inspect unstable frames one step at a time.
- You can visually confirm guide/follower distribution immediately.
- You can export frame timing history for graphs and comparisons between presets / assets.
- You can verify the broadphase volume and floor contact region from the GUI without adding code.
