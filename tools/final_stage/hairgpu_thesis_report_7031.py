from __future__ import annotations

"""70.31 thesis report wrapper.

Statistical aggregation and 70.30 outcome policy remain unchanged. 70.31 adds a
fail-closed audit of per-case fixed-client window-guard evidence so a thesis
report cannot be produced from runs whose 1920x1080 Viewer client area was not
successfully locked and monitored.
"""

import json
import sys
from pathlib import Path
from typing import Any, Mapping

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_thesis_report_7030 as revision7030  # type: ignore

FORMAL_REVISION = "70.31"
SAMPLING_POLICY = "paired-thesis-90-v1"
FATAL_POLICY = "nan-inf-crash-input-mismatch-corruption-v1"
WINDOW_GUARD_REVISION = "fixed-client-window-guard-v1"
WINDOW_GUARD_EVIDENCE_NAME = "formal_window_guard_7031.json"
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
        1 for item in records if item.get("guardRevision") == WINDOW_GUARD_REVISION
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
        "schema": "hairgpu-formal-window-guard-audit-v1-7031",
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
        "failures": all_failures,
        "violations": violations,
        "parseFailures": parse_failures,
    }


def _annotate_report(session: Path, guard_audit: Mapping[str, Any]) -> None:
    path = session / "report" / "thesis_statistical_report.json"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(payload, dict):
        raise RuntimeError(f"Expected report JSON object: {path}")

    payload["schema"] = "hairgpu-thesis-statistical-report-v4-7031-90run"
    payload["formalRevision"] = FORMAL_REVISION
    payload["samplingPolicy"] = SAMPLING_POLICY
    payload["formalSmokeIncluded"] = False
    payload["expectedTotalRunCount"] = 90
    payload["expectedMethodRunCount"] = 45
    payload["fatalIssuePolicy"] = FATAL_POLICY
    payload["formalWindowGuard"] = dict(guard_audit)
    payload["outcomePolicy"] = (
        "Performance/quality/hardware/determinism misses are reported outcomes; "
        "NaN/Inf, abnormal Viewer termination, input/evidence corruption, or "
        "missing/failed 1920x1080 window-guard evidence remain fail-closed."
    )
    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    session = _session_from_argv()
    if session is None:
        print("70.31 report requires --session", file=sys.stderr)
        return 2

    code = int(revision7030.legacy.main())
    if code != 0:
        return code

    # Reuse the 70.30 non-fatal-outcome collector, with the revision marker
    # rebound above, then add the new guard audit.
    revision7030._annotate_report(session)
    guard_audit = collect_window_guard_evidence(session)
    _annotate_report(session, guard_audit)
    if guard_audit.get("status") != "PASS":
        print(
            json.dumps(
                {"error": "70.31 formal window-guard evidence audit failed", **guard_audit},
                ensure_ascii=False,
                indent=2,
            ),
            file=sys.stderr,
        )
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
