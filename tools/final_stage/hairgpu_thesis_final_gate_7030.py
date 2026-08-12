from __future__ import annotations

"""70.30 thesis FinalGate.

Reuses the proven 70.29 integrity implementation with the 90-run/no-Smoke
contract. Quality/performance outcomes remain in matrix evidence; FinalGate is
concerned with completeness, pairing, hashes, signatures and uncorrupted runs.
"""

import json
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_thesis_final_gate as legacy  # type: ignore

FORMAL_REVISION = "70.30"
SAMPLING_POLICY = "paired-thesis-90-v1"
FATAL_POLICY = "nan-inf-crash-input-mismatch-corruption-v1"

legacy.REQUIRED_STAGES = {
    "Prepare",
    "Asset",
    "Pilot",
    "Quality",
    "Performance",
    "Determinism",
    "Soak",
    "Report",
    "VerifyBranches",
}
legacy.REQUIRED_MATRIX_PROFILES = {
    "pilot", "formal", "performance", "determinism", "soak"
}
legacy.EXPECTED_MATRIX_RUN_COUNTS = {
    "pilot": 10,
    "formal": 36,
    "performance": 36,
    "determinism": 4,
    "soak": 4,
}
legacy.EXPECTED_TOTAL_RUN_COUNT = 90
legacy.EXPECTED_METHOD_RUN_COUNT = 45
legacy.EXPECTED_SESSION_SEEDS = [1]
legacy.EXPECTED_SESSION_REPETITIONS = 1
legacy.EXPECTED_SAMPLING_POLICY = SAMPLING_POLICY


def _session_from_argv() -> Path | None:
    try:
        index = sys.argv.index("--session")
        return Path(sys.argv[index + 1]).resolve()
    except (ValueError, IndexError):
        return None


def _annotate_gate(session: Path | None) -> None:
    if session is None:
        return
    path = session / "integrity" / "final_gate.json"
    if not path.is_file():
        return
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(payload, dict):
        return
    payload["schema"] = "hairgpu-thesis-final-gate-v5-7030-90run"
    payload["formalRevision"] = FORMAL_REVISION
    payload["samplingPolicy"] = SAMPLING_POLICY
    payload["formalSmokeIncluded"] = False
    payload["expectedTotalRunCount"] = 90
    payload["expectedMethodRunCount"] = 45
    payload["fatalIssuePolicy"] = FATAL_POLICY
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    session = _session_from_argv()
    code = legacy.main()
    _annotate_gate(session)
    raise SystemExit(code)
