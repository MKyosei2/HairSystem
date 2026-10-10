#!/usr/bin/env python3
"""Static contract validation for the Autonomous QA Bug Zoo manifest/fixtures."""
from __future__ import annotations
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "samples" / "autonomous-qa-bug-zoo" / "manifest.json"
MIN_CASES = 15
CORE = {
    "compile-error": {"Compile", "RegressionTest"},
    "native-buffer-overflow": {"MemorySafety", "RegressionTest"},
    "memory-growth": {"Soak", "MemorySafety", "RegressionTest"},
    "race-condition": {"RaceDetection", "Stress", "RegressionTest"},
    "deadlock": {"DeadlockDetection", "Stress", "RegressionTest"},
    "visual-regression": {"ShaderValidation", "RuntimeReplay", "VisualRegression", "Benchmark", "RegressionTest"},
    "network-protocol": {"Fuzz", "LoadTest", "RegressionTest"},
    "missing-texture": {"AssetValidation", "VisualRegression", "RegressionTest"},
    "audio-regression": {"AudioRegression", "RegressionTest"},
    "clean-control": {"Compile", "StaticAnalysis", "UnitTest", "RegressionTest"},
}

def main() -> int:
    doc = json.loads(MANIFEST.read_text(encoding="utf-8"))
    rows = doc.get("cases", [])
    cases = {row.get("id"): row for row in rows if row.get("id")}
    errors: list[str] = []
    if len(rows) < MIN_CASES:
        errors.append(f"expected >= {MIN_CASES} cases, got {len(rows)}")
    for case_id, required in CORE.items():
        row = cases.get(case_id)
        if row is None:
            errors.append(f"missing case: {case_id}")
            continue
        actual = set(row.get("requiredExperiments", []))
        missing = required - actual
        if missing:
            errors.append(f"{case_id}: missing required experiments {sorted(missing)}")
        fixture = MANIFEST.parent / case_id
        if not fixture.is_dir() or not any(fixture.iterdir()):
            errors.append(f"{case_id}: fixture directory missing/empty")
    if errors:
        print("AUTONOMOUS_QA_BUG_ZOO=FAIL")
        for error in errors:
            print("-", error)
        return 1
    print(f"AUTONOMOUS_QA_BUG_ZOO=PASS cases={len(rows)}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
