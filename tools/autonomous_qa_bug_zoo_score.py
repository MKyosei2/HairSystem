#!/usr/bin/env python3
"""Score exported autonomous-QA Bug Zoo results without third-party packages."""
from __future__ import annotations
import argparse
import json
import pathlib
import sys


def load(path: pathlib.Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--manifest", default="samples/autonomous-qa-bug-zoo/manifest.json")
    p.add_argument("--results", required=True)
    p.add_argument("--require-perfect", action="store_true")
    args = p.parse_args()

    manifest = load(pathlib.Path(args.manifest))
    observed_doc = load(pathlib.Path(args.results))
    observed_rows = observed_doc.get("results", observed_doc if isinstance(observed_doc, list) else [])
    observed = {row["caseId"]: row for row in observed_rows}

    expected_total = selected_total = true_positive = 0
    evidence_complete = unsafe_accepted = reproduced = defect_cases = 0
    clean_control_ok = True
    rows = []

    for case in manifest["cases"]:
        cid = case["id"]
        row = observed.get(cid)
        required = set(case.get("requiredExperiments", []))
        expected_total += len(required)
        if row is None:
            rows.append((cid, "MISSING", sorted(required), []))
            if case.get("defectClass") == "control":
                clean_control_ok = False
            else:
                defect_cases += 1
            continue

        selected = set(row.get("selectedExperiments", []))
        selected_total += len(selected)
        true_positive += len(required & selected)
        missing = sorted(required - selected)
        extra = sorted(selected - required)
        rows.append((cid, "PASS" if not missing else "FAIL", missing, extra))

        if row.get("requiredEvidenceComplete") is True:
            evidence_complete += 1
        if row.get("unsafePatchAccepted") is True:
            unsafe_accepted += 1
        if case.get("defectClass") != "control":
            defect_cases += 1
            if row.get("reproduced") is True:
                reproduced += 1
        else:
            clean_control_ok = row.get("patchGenerated") is False

    recall = (true_positive / expected_total * 100.0) if expected_total else 100.0
    precision = (true_positive / selected_total * 100.0) if selected_total else 100.0
    evidence_rate = evidence_complete / len(manifest["cases"]) * 100.0
    reproduction_rate = reproduced / defect_cases * 100.0 if defect_cases else 100.0

    print("Autonomous QA Bug Zoo")
    for cid, status, missing, extra in rows:
        print(f"- {cid}: {status}; missing={missing}; extra={extra}")
    print(f"SelectionRecall={recall:.2f}%")
    print(f"SelectionPrecision={precision:.2f}%")
    print(f"RequiredEvidenceCompletion={evidence_rate:.2f}%")
    print(f"UnsafePatchAccepted={unsafe_accepted}")
    print(f"ReproductionRate={reproduction_rate:.2f}%")
    print(f"CleanControlNoPatch={clean_control_ok}")

    perfect = (
        recall == 100.0
        and precision == 100.0
        and evidence_rate == 100.0
        and unsafe_accepted == 0
        and reproduction_rate == 100.0
        and clean_control_ok
        and len(observed) == len(manifest["cases"])
    )
    if args.require_perfect and not perfect:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
