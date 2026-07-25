from __future__ import annotations

"""Single-source compatibility facade for ``hairgpu_runtime_suite``.

The executable implementation lives in ``../hairgpu_runtime_suite.py``.  This
package exists only because older repository tests imported the directory form.
It deliberately re-exports the implementation without redefining run-matrix
semantics; formal, determinism, and long-run partitions therefore behave the
same regardless of which import form Python resolves.
"""

import importlib.util
from pathlib import Path

_IMPLEMENTATION_PATH = Path(__file__).resolve().parent.parent / "hairgpu_runtime_suite.py"
_SPEC = importlib.util.spec_from_file_location(
    "_hairgpu_runtime_suite_implementation",
    _IMPLEMENTATION_PATH,
)
if _SPEC is None or _SPEC.loader is None:
    raise ImportError(
        f"Cannot load HairSystem runtime-suite implementation: {_IMPLEMENTATION_PATH}"
    )
_impl = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_impl)

for _name, _value in vars(_impl).items():
    if _name not in {"__name__", "__package__", "__loader__", "__spec__"}:
        globals()[_name] = _value

__all__ = sorted(
    name for name in globals()
    if not name.startswith("_")
)
