from __future__ import annotations

"""Run all Novelty 01 formal evidence profiles and the fail-closed merge gate.

This orchestrator intentionally keeps quality, performance, determinism, soak,
and GTX 1660 evidence in separate runs.  It does not substitute dry-run or
source checks for the required D3D11 measurements.
"""

import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable


def run(command: Iterable[str], *, cwd: Path) -> None:
    rendered = [str(item) for item in command]
    print("+ " + " ".join(rendered), flush=True)
    result = subprocess.run(rendered, cwd=cwd)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed with exit code {result.returncode}: " +
            " ".join(rendered))


def matrix_command(
    python: str,
    script: Path,
    *,
    repo: Path,
    viewer: Path,
    template: Path,
    output: Path,
    profile: str,
    variants: str,
    scenarios: str,
) -> list[str]:
    return [
        python, str(script),
        "--repo", str(repo),
        "--viewer", str(viewer),
        "--template", str(template),
        "--output", str(output),
        "--profile", profile,
        "--variants", variants,
        "--scenarios", scenarios,
        "--require-fxc",
    ]


def copy_summary(source_dir: Path, destination: Path) -> None:
    source = source_dir / "novelty01_matrix_summary.json"
    if not source.is_file():
        raise FileNotFoundError(f"Matrix summary is missing: {source}")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--viewer", required=True, type=Path)
    parser.add_argument("--template", required=True, type=Path)
    parser.add_argument("--evidence-root", required=True, type=Path)
    parser.add_argument(
        "--skip-gtx1660", action="store_true",
        help="Development-only: omit the hardware profile and produce a gate that cannot support a GTX1660 claim.")
    parser.add_argument("--bootstrap-samples", type=int, default=10000)
    args = parser.parse_args()

    repo = args.repo.resolve()
    viewer = args.viewer.resolve()
    template = args.template.resolve()
    evidence = args.evidence_root.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    if not viewer.is_file():
        raise FileNotFoundError(f"Release viewer is missing: {viewer}")
    if not template.is_file():
        raise FileNotFoundError(f"Frozen case template is missing: {template}")

    script_root = repo / "tools/final_stage"
    matrix = script_root / "hairgpu_novelty01_matrix.py"
    report = script_root / "hairgpu_novelty01_report.py"
    gate = script_root / "hairgpu_novelty01_gate.py"

    profiles = [
        ("formal", "all", "all"),
        ("performance", "B0,B1,B8,P", "all"),
        ("determinism", "P", "S01,S02,S03,S06,S12,S16,S18"),
        ("soak", "P", "S15"),
    ]
    if not args.skip_gtx1660:
        profiles.append(("gtx1660", "B1,B8,P", "all"))

    outputs: dict[str, Path] = {}
    for profile, variants, scenarios in profiles:
        output = evidence / profile
        outputs[profile] = output
        run(matrix_command(
            sys.executable, matrix, repo=repo, viewer=viewer,
            template=template, output=output, profile=profile,
            variants=variants, scenarios=scenarios), cwd=repo)

    copy_summary(
        outputs["formal"], evidence / "novelty01_matrix_summary.json")
    copy_summary(
        outputs["performance"],
        evidence / "novelty01_performance_matrix_summary.json")
    copy_summary(
        outputs["determinism"],
        evidence / "novelty01_determinism_summary.json")
    copy_summary(
        outputs["soak"], evidence / "novelty01_soak_summary.json")
    if "gtx1660" in outputs:
        copy_summary(
            outputs["gtx1660"],
            evidence / "novelty01_gtx1660_summary.json")

    run([
        sys.executable, str(report),
        "--matrix-summary", str(evidence / "novelty01_matrix_summary.json"),
        "--performance-matrix-summary",
        str(evidence / "novelty01_performance_matrix_summary.json"),
        "--output", str(evidence),
        "--bootstrap-samples", str(args.bootstrap_samples),
        "--confidence", "0.95",
        "--strict-gates",
    ], cwd=repo)

    gate_command = [
        sys.executable, str(gate),
        "--evidence-root", str(evidence),
        "--output", str(evidence / "novelty01_merge_gate.json"),
    ]
    if args.skip_gtx1660:
        gate_command.append("--allow-missing-gtx1660")
    run(gate_command, cwd=repo)

    payload = json.loads(
        (evidence / "novelty01_merge_gate.json").read_text(encoding="utf-8"))
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0 if payload.get("passed") is True else 2


if __name__ == "__main__":
    raise SystemExit(main())
