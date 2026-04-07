from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def run_command(args: list[str]) -> None:
    print("[run]", " ".join(args))
    completed = subprocess.run(args, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="One-shot core research pipeline: teacher dataset -> hybrid bundle -> 4-way compare"
    )
    parser.add_argument("--cli", required=True, help="Path to hairgpu_cli executable")
    parser.add_argument("--input", required=True, help="Input hair asset (.hairbin/.csv/.usda/.abc)")
    parser.add_argument("--output-dir", required=True, help="Directory for all generated artifacts")
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--fps", type=float, default=60.0)
    parser.add_argument("--sway-amplitude", type=float, default=0.18)
    parser.add_argument("--max-active-clusters", type=int, default=6)
    parser.add_argument("--max-active-guides", type=int, default=64)
    parser.add_argument("--target-frame-ms", type=float, default=16.67)
    parser.add_argument("--max-solver-iterations", type=int, default=8)
    parser.add_argument("--prior-guides", type=int, default=24)
    parser.add_argument("--residual-guides", type=int, default=16)
    parser.add_argument("--max-influences", type=int, default=4)
    parser.add_argument("--regularization", type=float, default=1.0e-4)
    parser.add_argument("--validation-stride", type=int, default=6)
    parser.add_argument("--smoothing-alpha", type=float, default=0.35)
    parser.add_argument("--residual-guide-gain", type=float, default=1.0)
    parser.add_argument("--residual-blend", type=float, default=1.0)
    parser.add_argument("--colliders", default="")
    args = parser.parse_args()

    cli = str(Path(args.cli))
    input_path = str(Path(args.input))
    output_dir = Path(args.output_dir)
    teacher_dir = output_dir / "teacher_dataset"
    bundle_dir = output_dir / "hybrid_bundle"
    compare_dir = output_dir / "hybrid_compare"
    output_dir.mkdir(parents=True, exist_ok=True)

    colliders_args = ["--colliders", args.colliders] if args.colliders else []

    run_command(
        [
            cli,
            "hybrid-teacher-dataset",
            input_path,
            str(teacher_dir),
            str(args.frames),
            str(args.fps),
            str(args.sway_amplitude),
            str(args.max_active_clusters),
            str(args.max_active_guides),
            str(args.target_frame_ms),
            str(args.max_solver_iterations),
            *colliders_args,
        ]
    )

    run_command(
        [
            cli,
            "hybrid-train-bundle",
            str(teacher_dir),
            str(bundle_dir),
            str(args.prior_guides),
            str(args.residual_guides),
            str(args.max_influences),
            str(args.regularization),
            str(args.validation_stride),
            str(args.smoothing_alpha),
            str(args.residual_guide_gain),
            str(args.residual_blend),
            *colliders_args,
        ]
    )

    run_command(
        [
            cli,
            "hybrid-compare",
            str(teacher_dir),
            str(bundle_dir),
            str(compare_dir),
            str(args.max_active_clusters),
            str(args.max_active_guides),
            str(args.target_frame_ms),
            *colliders_args,
        ]
    )

    manifest = {
        "input": input_path,
        "teacher_dir": str(teacher_dir),
        "bundle_dir": str(bundle_dir),
        "compare_dir": str(compare_dir),
        "compare_csv": str(compare_dir / "hybrid_compare_summary.csv"),
        "compare_json": str(compare_dir / "hybrid_compare_summary.json"),
        "frames": args.frames,
        "fps": args.fps,
        "swayAmplitude": args.sway_amplitude,
        "maxActiveClusters": args.max_active_clusters,
        "maxActiveGuides": args.max_active_guides,
        "targetFrameMs": args.target_frame_ms,
        "maxSolverIterations": args.max_solver_iterations,
        "colliders": args.colliders,
    }
    manifest_path = output_dir / "core_eval_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"[done] manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
