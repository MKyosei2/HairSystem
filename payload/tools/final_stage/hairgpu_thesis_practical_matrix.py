from __future__ import annotations

"""Run practical thesis edit workloads through the existing Novelty01 matrix.

This wrapper deliberately reuses the repository's frozen asset, B0/P runner,
counterbalanced ordering, and measurement profiles. It adds only edit scale and
fallback-control environment variables consumed by the thesis overlay.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

CASES = {
    "T00": {"scenario": "S01", "guide_count": 0, "tip_mm": 0.0, "fallback": False},
    "T01": {"scenario": "S11", "guide_count": 1, "tip_mm": 3.0, "fallback": False},
    "T02": {"scenario": "S11", "guide_count": 4, "tip_mm": 3.0, "fallback": False},
    "T03": {"scenario": "S11", "guide_count": 16, "tip_mm": 3.0, "fallback": False},
    "T04": {"scenario": "S11", "guide_count": 64, "tip_mm": 3.0, "fallback": False},
    "T05": {"scenario": "S11", "guide_count": 256, "tip_mm": 3.0, "fallback": False},
    "T06": {"scenario": "S11", "guide_count": 1000, "tip_mm": 3.0, "fallback": False},
    "T07": {"scenario": "S11", "guide_count": 64, "tip_mm": 3.0, "fallback": True},
}


def parse_case_list(text: str) -> list[str]:
    if text.strip().lower() == "all":
        return list(CASES)
    result: list[str] = []
    for raw in text.split(","):
        item = raw.strip().upper()
        if not item:
            continue
        if item not in CASES:
            raise ValueError(f"unknown thesis practical case: {item}")
        if item not in result:
            result.append(item)
    if not result:
        raise ValueError("at least one thesis practical case is required")
    return result


def run_case(args: argparse.Namespace, case_id: str) -> dict:
    case = CASES[case_id]
    output = args.output.resolve() / case_id
    output.mkdir(parents=True, exist_ok=True)

    matrix_script = (
        args.repo.resolve()
        / "tools"
        / "final_stage"
        / "hairgpu_novelty01_matrix.py"
    )
    if not matrix_script.is_file():
        raise FileNotFoundError(matrix_script)

    env = os.environ.copy()
    env["HAIRGPU_THESIS_EDIT_EVAL"] = "1"
    env["HAIRGPU_THESIS_CASE"] = case_id
    env["HAIRGPU_THESIS_EDIT_GUIDE_COUNT"] = str(max(int(case["guide_count"]), 1))
    env["HAIRGPU_THESIS_EDIT_TIP_MM"] = str(float(case["tip_mm"] or 3.0))
    env["HAIRGPU_THESIS_FORCE_FULL_FALLBACK"] = "1" if case["fallback"] else "0"

    command = [
        sys.executable,
        str(matrix_script),
        "--repo",
        str(args.repo.resolve()),
        "--viewer",
        str(args.viewer.resolve()),
        "--template",
        str(args.template.resolve()),
        "--output",
        str(output),
        "--profile",
        args.profile,
        "--variants",
        args.variants,
        "--scenarios",
        str(case["scenario"]),
        "--seeds",
        args.seeds,
        "--schedule",
        args.schedule,
    ]
    if args.repetitions is not None:
        command += ["--repetitions", str(args.repetitions)]
    if args.dry_run:
        command.append("--dry-run")
    if args.continue_on_error:
        command.append("--continue-on-error")
    if args.require_fxc:
        command.append("--require-fxc")

    completed = subprocess.run(
        command,
        env=env,
        cwd=args.repo.resolve(),
        text=True,
        capture_output=True,
        check=False,
    )

    (output / "thesis_wrapper_stdout.txt").write_text(
        completed.stdout, encoding="utf-8"
    )
    (output / "thesis_wrapper_stderr.txt").write_text(
        completed.stderr, encoding="utf-8"
    )
    return {
        "case": case_id,
        "scenario": case["scenario"],
        "guideCount": case["guide_count"],
        "tipMm": case["tip_mm"],
        "forceFallback": bool(case["fallback"]),
        "returnCode": completed.returncode,
        "ok": completed.returncode == 0,
        "output": str(output),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--viewer", required=True, type=Path)
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--profile", default="pilot")
    parser.add_argument("--variants", default="B0,P")
    parser.add_argument("--seeds", default="1,2")
    parser.add_argument("--repetitions", type=int)
    parser.add_argument("--schedule", default="counterbalanced")
    parser.add_argument("--cases", default="T01,T02,T03,T04,T05,T06,T07")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument("--require-fxc", action="store_true")
    args = parser.parse_args()

    selected = parse_case_list(args.cases)
    args.output.mkdir(parents=True, exist_ok=True)

    rows = []
    for case_id in selected:
        row = run_case(args, case_id)
        rows.append(row)
        if not row["ok"] and not args.continue_on_error:
            break

    summary = {
        "schema": "hairgpu-thesis-practical-matrix-v1",
        "baseCases": CASES,
        "selectedCases": selected,
        "passed": bool(rows) and all(row["ok"] for row in rows),
        "results": rows,
    }
    (args.output / "thesis_practical_matrix_summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
