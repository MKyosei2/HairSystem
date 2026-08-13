from __future__ import annotations

"""70.32 thesis FinalGate.

Reuses the proven 70.30 90-run integrity gate and requires exactly 90 PASS
fixed-client-window-guard-v2 records. Native Viewer ExitCode 92 remains fatal.
"""

import json
import sys
from pathlib import Path
from typing import Any, Mapping

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_thesis_final_gate_7030 as revision7030  # type: ignore
import hairgpu_thesis_report_7032 as report7032  # type: ignore

FORMAL_REVISION = "70.32"
SAMPLING_POLICY = "paired-thesis-90-v1"
FATAL_POLICY = "nan-inf-crash-input-mismatch-corruption-v1"

revision7030.FORMAL_REVISION = FORMAL_REVISION


def _session_from_argv() -> Path | None:
    try:
        index = sys.argv.index("--session")
        return Path(sys.argv[index + 1]).resolve()
    except (ValueError, IndexError):
        return None


def _write_failed_guard_gate(session: Path, audit: Mapping[str, Any]) -> None:
    path = session / "integrity" / "final_gate.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": "hairgpu-thesis-final-gate-v7-7032-90run",
        "formalRevision": FORMAL_REVISION,
        "samplingPolicy": SAMPLING_POLICY,
        "status": "FAIL",
        "formalSmokeIncluded": False,
        "expectedTotalRunCount": 90,
        "expectedMethodRunCount": 45,
        "fatalIssuePolicy": FATAL_POLICY,
        "formalWindowGuard": dict(audit),
        "errors": ["70.32 formal window-guard-v2 evidence audit failed"],
    }
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def _annotate_gate(session: Path, audit: Mapping[str, Any]) -> None:
    path = session / "integrity" / "final_gate.json"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(payload, dict):
        raise RuntimeError(f"Expected FinalGate JSON object: {path}")
    payload["schema"] = "hairgpu-thesis-final-gate-v7-7032-90run"
    payload["formalRevision"] = FORMAL_REVISION
    payload["samplingPolicy"] = SAMPLING_POLICY
    payload["formalSmokeIncluded"] = False
    payload["expectedTotalRunCount"] = 90
    payload["expectedMethodRunCount"] = 45
    payload["fatalIssuePolicy"] = FATAL_POLICY
    payload["formalWindowGuard"] = dict(audit)
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    session = _session_from_argv()
    if session is None:
        print("70.32 FinalGate requires --session", file=sys.stderr)
        return 2

    audit = report7032.collect_window_guard_evidence(session)
    if audit.get("status") != "PASS":
        _write_failed_guard_gate(session, audit)
        print(
            json.dumps(
                {"error": "70.32 formal window-guard-v2 evidence audit failed", **audit},
                ensure_ascii=False,
                indent=2,
            ),
            file=sys.stderr,
        )
        return 2

    code = int(revision7030.legacy.main())
    revision7030._annotate_gate(session)
    _annotate_gate(session, audit)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
