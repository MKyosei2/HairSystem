from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.write_text(text.replace("\r\n", "\n"), encoding="utf-8", newline="\n")


modified: list[str] = []

# PR #7 restored these functions in their canonical ordered INL files. Keeping
# another copy in 02a creates C2084 duplicate-body errors, so 02a is now only an
# ordering placeholder.
path = "apps/dx11_viewer/src/GpuHairViewer_parts/02a_required_runtime_foundation.inl"
write(
    path,
    "// GpuHairViewer.cpp ordered part 02a: required_runtime_foundation\n"
    "// The formerly missing definitions now live in their canonical ordered INL\n"
    "// files (01, 06, and 08). This placeholder intentionally contains no\n"
    "// function bodies so each GpuHairViewer member has exactly one definition.\n"
    "// It is included by ../GpuHairViewer.cpp; do not compile it directly.\n",
)
modified.append(path)

# Restore actual function calls that were accidentally converted into member
# function references during source splitting.
for path in (
    "apps/dx11_viewer/src/GpuHairViewer_parts/07_draw_path_and_gpu_timing.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/12_strand_pick_gizmo_and_brush_edit.inl",
    "apps/dx11_viewer/src/GpuHairViewer_parts/14_scalp_overlay_and_window_messages.inl",
):
    text = read(path)
    fixed, count = re.subn(r"GetRenderViewportRect(?!\s*\()", "GetRenderViewportRect()", text)
    if count == 0 and "GetRenderViewportRect()" not in text:
        raise RuntimeError(f"No GetRenderViewportRect call found in {path}")
    write(path, fixed)
    modified.append(path)

path = "apps/dx11_viewer/src/GpuHairViewer_parts/09_settings_load_save_and_export.inl"
text = read(path)
fixed, count = re.subn(r"GetWorkspaceDisplayName(?!\s*\()", "GetWorkspaceDisplayName()", text)
if count == 0 and "GetWorkspaceDisplayName()" not in text:
    raise RuntimeError("No GetWorkspaceDisplayName call found")
write(path, fixed)
modified.append(path)

manifest = ROOT / "phase13_modified_files.txt"
existing = []
if manifest.is_file():
    existing = [line.strip() for line in manifest.read_text(encoding="utf-8").splitlines() if line.strip()]
for item in modified:
    if item not in existing:
        existing.append(item)
manifest.write_text("\n".join(existing) + "\n", encoding="utf-8", newline="\n")
print(f"Added {len(modified)} build-fix files to phase13 package")
