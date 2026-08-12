from __future__ import annotations

"""70.30 thesis report wrapper.

The statistical aggregation implementation is unchanged from 70.29; this
wrapper annotates the report with the 90-run/no-Smoke design and preserves
non-fatal quality/hardware/determinism outcomes explicitly for thesis writing.
"""

import json
import sys
from pathlib import Path
from typing import Any, Mapping

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_thesis_report as legacy  # type: ignore

FORMAL_REVISION = "70.30"
SAMPLING_POLICY = "paired-thesis-90-v1"
FATAL_POLICY = "nan-inf-crash-input-mismatch-corruption-v1"


def _session_from_argv() -> Path | None:
    try:
        index = sys.argv.index("--session")
        return Path(sys.argv[index + 1]).resolve()
    except (ValueError, IndexError):
        return None


def _collect_outcomes(session: Path) -> dict[str, Any]:
    quality_issues: list[dict[str, Any]] = []
    determinism_outcomes: list[dict[str, Any]] = []
    fatal_issues: list[dict[str, Any]] = []

    for summary_path in sorted(session.rglob("novelty01_matrix_summary.json")):
        try:
            summary = json.loads(summary_path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError, UnicodeError):
            continue
        if not isinstance(summary, Mapping):
            continue

        for item in summary.get("determinismOutcomeIssues", []) or []:
            if isinstance(item, Mapping):
                determinism_outcomes.append({
                    "profile": str(summary.get("profile", "")),
                    "summaryPath": str(summary_path),
                    **dict(item),
                })

        for row in summary.get("results", []) or []:
            if not isinstance(row, Mapping):
                continue
            result = row.get("result")
            if not isinstance(result, Mapping):
                continue
            for issue in result.get("issues", []) or []:
                if not isinstance(issue, Mapping):
                    continue
                enriched = {
                    "caseId": str(row.get("caseId", "")),
                    "profile": str(summary.get("profile", "")),
                    "variant": str(row.get("variant", "")),
                    "scenario": str(row.get("scenario", "")),
                    **dict(issue),
                }
                severity = str(issue.get("severity", ""))
                if severity == "quality":
                    quality_issues.append(enriched)
                elif severity == "error":
                    fatal_issues.append(enriched)

    return {
        "qualityOutcomeIssueCount": len(quality_issues),
        "fatalIntegrityIssueCount": len(fatal_issues),
        "determinismOutcomeIssueCount": len(determinism_outcomes),
        "qualityOutcomeIssues": quality_issues,
        "fatalIntegrityIssues": fatal_issues,
        "determinismOutcomeIssues": determinism_outcomes,
    }


def _annotate_report(session: Path | None) -> None:
    if session is None:
        return
    path = session / "report" / "thesis_statistical_report.json"
    if not path.is_file():
        return
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(payload, dict):
        return

    payload["schema"] = "hairgpu-thesis-statistical-report-v3-7030-90run"
    payload["formalRevision"] = FORMAL_REVISION
    payload["samplingPolicy"] = SAMPLING_POLICY
    payload["formalSmokeIncluded"] = False
    payload["expectedTotalRunCount"] = 90
    payload["expectedMethodRunCount"] = 45
    payload["fatalIssuePolicy"] = FATAL_POLICY
    payload["outcomePolicy"] = (
        "Performance/quality/hardware/determinism misses are reported outcomes; "
        "only invalid/corrupted execution evidence is fail-closed."
    )
    payload["outcomes"] = _collect_outcomes(session)

    path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    session = _session_from_argv()
    code = legacy.main()
    if code == 0:
        _annotate_report(session)
    raise SystemExit(code)
