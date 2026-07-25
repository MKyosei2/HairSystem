from __future__ import annotations

"""Compatibility entry point for the single authoritative runtime suite.

The former independent implementation diverged from the research runner.
Keeping this existing path as a thin forwarder preserves old commands while
ensuring validation, statistics and result semantics come from one module.
"""

import sys
from pathlib import Path

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from hairgpu_runtime_suite import main


if __name__ == "__main__":
    raise SystemExit(main())
