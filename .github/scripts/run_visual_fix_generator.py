#!/usr/bin/env python3
"""Execute the temporary visual fix generator without the legacy brace counter.

Exact one-match replacements and required-token validation remain active. The
removed check counted braces in comments and was not a C++ parser.
"""
from __future__ import annotations

from pathlib import Path
import runpy

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / ".github" / "scripts" / "generate_visual_fix_artifact.py"

namespace = runpy.run_path(str(GENERATOR), run_name="hairgpu_visual_fix_generator")
apply_fixes = namespace["apply_fixes"]
apply_fixes.__globals__["validate_cpp_balance"] = lambda _path: None
apply_fixes()
namespace["build_archive"]()
