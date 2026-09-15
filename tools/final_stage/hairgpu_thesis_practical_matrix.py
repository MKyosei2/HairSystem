from __future__ import annotations

"""Run thesis-practical cases through the existing Novelty01 matrix.

Case definitions live only in benchmarks/thesis_practical/*.json.
The runner intentionally reuses the repository's frozen asset, B0/P runner,
counterbalanced scheduling, and formal measurement profiles.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any


CASE_SCHEMA = "hairgpu-thesis-practical-case-v1"


def load_cases(repo: Path) -> dict[str, dict[str, Any]]:
    root = repo / "benchmarks" / "thesis_practical"
    if not root.is_dir():
        raise FileNotFoundError(root)

    cases: dict[str, dict[str, Any]] = {}
    for path in sorted(root.glob("T*.json")):
        payload = json.loads(path.read_text(encoding="utf-8"))
        if payload.get("schema") != CASE_SCHEMA:
            raise ValueError(f"{path}: unsupported schema")
        case_id = str(payload.get("id", "")).upper()
        if not case_id or case_id in cases:
            raise ValueError(f"{path}: invalid or duplicate case id")
        cases[case_id] = payload

    if not cases:
        raise RuntimeError("no thesis practical cases found")
    return cases


def parse_case_list(text: str, cases: dict[str, dict[str, Any]]) -> list[str]:
    if text.strip().lower() == "all":
        return list(cases)

    selected: list[str] = []
    for raw in text.split(","):
        case_id = raw.strip().upper()
        if not case_id:
            continue
        if case_id not in cases:
            raise ValueError(f"unknown thesis practical case: {case_id}")
        if case_id not in selected:
            selected.append(case_id)
    if not selected:
        raise ValueError("at least one thesis practical case is required")
    return selected


def run_case(
    args: argparse.Namespace,
    case_id: str,
    case: dict[str, Any],
) -> dict[str, Any]:
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
    env.update(
        {
            "HAIRGPU_THESIS_EDIT_EVAL": "1",
            "HAIRGPU_THESIS_CASE": case_id,
            "HAIRGPU_THESIS_EDIT_GUIDE_COUNT": str(
                max(int(case.get("guideCount", 1)), 1)
            ),
            "HAIRGPU_THESIS_EDIT_TIP_MM": str(
                float(case.get("tipOffsetMm", 3.0) or 3.0)
            ),
            "HAIRGPU_THESIS_FORCE_FULL_FALLBACK": (
                "1" if bool(case.get("forceFallback", False)) else "0"
            ),
        }
    )

    command = [
        sys.executable,
        str(matrix_script),
        "--repo", str(args.repo.resolve()),
        "--viewer", str(args.viewer.resolve()),
        "--template", str(args.template.resolve()),
        "--output", str(output),
        "--profile", args.profile,
        "--variants", args.variants,
        "--scenarios", str(case["scenario"]),
        "--seeds", args.seeds,
        "--schedule", args.schedule,
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
        "guideCount": int(case.get("guideCount", 0)),
        "tipMm": float(case.get("tipOffsetMm", 0.0)),
        "forceFallback": bool(case.get("forceFallback", False)),
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

    repo = args.repo.resolve()
    cases = load_cases(repo)
    selected = parse_case_list(args.cases, cases)
    args.output.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, Any]] = []
    for case_id in selected:
        row = run_case(args, case_id, cases[case_id])
        rows.append(row)
        if not row["ok"] and not args.continue_on_error:
            break

    summary = {
        "schema": "hairgpu-thesis-practical-matrix-v2",
        "caseSchema": CASE_SCHEMA,
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
