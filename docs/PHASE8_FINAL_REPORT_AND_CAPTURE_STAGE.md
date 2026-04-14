# Phase 8 Final Report and Capture Stage

This stage adds the remaining submission-facing tooling:

- viewer runtime report bundle now also writes a BMP screenshot, snapshot JSON, and snapshot markdown
- F9 in the DX11 viewer writes an auto-numbered report bundle to `report_captures/` next to the executable
- `hybrid-profile-compare` now also writes:
  - `hybrid_profile_compare_report.html`
  - `hybrid_profile_compare_table.tex`
  - `hybrid_profile_compare_manifest.json`
- `hybrid-final-report <profile_compare_dir>` can rebuild the report artifacts after additional viewer screenshots are copied into each profile directory

Recommended submission flow:

1. Run `hybrid-profile-compare` for `standard128,ideal1024,max2048`
2. Open the viewer with the same preset and export F9 captures for key overlays
3. Copy those BMP screenshots into the matching profile subdirectories
4. Run `hybrid-final-report` on the compare output directory
5. Use the HTML report and LaTeX table directly in the thesis / presentation
