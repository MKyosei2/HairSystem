"""Temporary PR-only bootstrap for the requested visual-stability artifact."""
from __future__ import annotations

from pathlib import Path
import runpy

_ROOT = Path(__file__).resolve().parents[2]
_TRIGGER = _ROOT / ".github" / "scripts" / ".visual_fix_trigger"
_GENERATOR = _ROOT / ".github" / "scripts" / "generate_visual_fix_artifact.py"
_MANIFEST = _ROOT / ".artifact_chunks" / "manifest.json"

if _TRIGGER.is_file() and _GENERATOR.is_file() and not _MANIFEST.is_file():
    runpy.run_path(str(_GENERATOR), run_name="__main__")
