from __future__ import annotations

"""70.32 thesis report wrapper.

Statistical aggregation and the paired 90-run policy are unchanged. 70.32
requires exactly 90 fixed-client-window-guard-v2 records and records the actual
formal revision as 70.32.
"""

import json
import sys
from pathlib import Path
from typing import Any, Mapping

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_thesis_report_7030 as revision7030  # type: ignore

FORMAL_REVISION = "70.32"
SAMPLING_POLICY = "paired-thesis-90-v1"
FATAL_POLICY = "nan-inf-crash-input-mismatch-corruption-v1"
WINDOW_GUARD_REVISION = "fixed-client-window-guard-v2"
WINDOW_GUARD_EVIDENCE_NAME = "formal_window_guard_7032.json"
EXPECTED_WINDOW_GUARD_RECORDS = 90

revision7030.FORMAL_REVISION = FORMAL_REVISION


def _session_from_argv() -> Path | None:
    try:
        index = sys.argv.index("--session")
        return Path(sys.argv[index + 1]).resolve()
    except (ValueError, IndexError):
        return None


def collect_window_guard_evidence(session: Path) -> dict[str, Any]:
    records: list[dict[str, Any]] = []
    parse_failures: list[dict[str, str]] = []
    violations: list[dict[str, Any]] = []

    for path in sorted(session.rglob(WINDOW_GUARD_EVIDENCE_NAME)):
        try:
            value = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError, json.JSONDecodeError) as exc:
            parse_failures.append({"path": str(path), "error": str(exc)})
            continue
        if not isinstance(value, Mapping):
            parse_failures.append({"path": str(path), "error": "not a JSON object"})
            continue
        record = {"path": str(path), **dict(value)}
        records.append(record)
        for violation in value.get("violations", []) or []:
            if isinstance(violation, Mapping):
                violations.append({"path": str(path), **dict(violation)})

    pass_count = sum(1 for item in records if item.get("status") == "PASS")
    revision_count = sum(
        1
        for item in records
        if item.get("guardRevision") == WINDOW_GUARD_REVISION
        and item.get("formalRevision") == FORMAL_REVISION
    )
    all_failures = [
        {
            "path": str(item.get("path", "")),
            "status": str(item.get("status", "")),
            "failureReason": str(item.get("failureReason", "")),
        }
        for item in records
        if item.get("status") != "PASS"
    ]

    status = "PASS"
    if len(records) != EXPECTED_WINDOW_GUARD_RECORDS:
        status = "FAIL"
    if pass_count != len(records) or revision_count != len(records):
        status = "FAIL"
    if parse_failures or violations:
        status = "FAIL"

    return {
        "schema": "hairgpu-formal-window-guard-audit-v2-7032",
        "formalRevision": FORMAL_REVISION,
        "guardRevision": WINDOW_GUARD_REVISION,
        "status": status,
        "expectedRecordCount": EXPECTED_WINDOW_GUARD_RECORDS,
        "recordCount": len(records),
        "passCount": pass_count,
        "revisionMatchCount": revision_count,
        "violationCount": len(violations),
        "parseFailureCount": len(parse_failures),
        "viewerExit92RemainsFatal": True,
        "normalShutdownWindowGraceSeconds": 2.0,
        "failures": all_failures,
        "violations": violations,
        "parseFailures": parse_failures,
    }


def _annotate_report(session: Path, guard_audit: Mapping[str, Any]) -> None:
    path = session / "report" / "thesis_statistical_report.json"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(payload, dict):
        raise RuntimeError(f"Expected report JSON object: {path}")

    payload["schema"] = "hairgpu-thesis-statistical-report-v5-7032-90run"
    payload["formalRevision"] = FORMAL_REVISION
    payload["samplingPolicy"] = SAMPLING_POLICY
    payload["formalSmokeIncluded"] = False
    payload["expectedTotalRunCount"] = 90
    payload["expectedMethodRunCount"] = 45
    payload["fatalIssuePolicy"] = FATAL_POLICY
    payload["formalWindowGuard"] = dict(guard_audit)
    payload["outcomePolicy"] = (
        "Performance/quality/hardware/determinism misses are reported outcomes; "
        "NaN/Inf, abnormal Viewer termination, input/evidence corruption, actual "
        "client/display-size change, or missing/failed 70.32 guard-v2 evidence "
        "remain fail-closed. Normal HWND destruction immediately before process "
        "exit is not an integrity violation."
    )
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    session = _session_from_argv()
    if session is None:
        print("70.32 report requires --session", file=sys.stderr)
        return 2

    code = int(revision7030.legacy.main())
    if code != 0:
        return code

    revision7030._annotate_report(session)
    guard_audit = collect_window_guard_evidence(session)
    _annotate_report(session, guard_audit)
    if guard_audit.get("status") != "PASS":
        print(
            json.dumps(
                {"error": "70.32 formal window-guard-v2 evidence audit failed", **guard_audit},
                ensure_ascii=False,
                indent=2,
            ),
            file=sys.stderr,
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
