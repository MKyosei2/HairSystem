from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import subprocess
import sys
from pathlib import Path


def run_command(args: list[str]) -> None:
    print("[run]", " ".join(args))
    completed = subprocess.run(args, check=False)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = max(0.0, min(1.0, q)) * (len(ordered) - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return ordered[lo]
    t = pos - lo
    return ordered[lo] * (1.0 - t) + ordered[hi] * t


def summarize_viewer_perf_csv(csv_path: Path) -> dict[str, float | int | str]:
    rows: list[dict[str, str]] = []
    with csv_path.open('r', encoding='utf-8', newline='') as f:
        reader = csv.DictReader(f)
        rows.extend(reader)
    if not rows:
        return {"path": str(csv_path), "samples": 0}

    def floats(key: str) -> list[float]:
        out: list[float] = []
        for row in rows:
            value = row.get(key, '')
            if value == '':
                continue
            try:
                out.append(float(value))
            except ValueError:
                continue
        return out

    frame_ms = floats('frameMilliseconds')
    fps = floats('fps')
    gpu_mem = floats('gpuMemoryMegabytes')
    cpu_mem = floats('cpuMemoryMegabytes')
    edit_latency = floats('editApplyLatencyMilliseconds')
    boundary = floats('editBoundaryTransitionScore')
    locality = floats('editLocalityRatio')
    coverage = floats('editCoverageRatio')
    penetration = floats('correctiveMaxPenetration')

    summary = {
        'path': str(csv_path),
        'samples': len(rows),
        'avgFrameMs': statistics.fmean(frame_ms) if frame_ms else 0.0,
        'p95FrameMs': percentile(frame_ms, 0.95) if frame_ms else 0.0,
        'maxFrameMs': max(frame_ms) if frame_ms else 0.0,
        'avgFps': statistics.fmean(fps) if fps else 0.0,
        'sixtyFpsHitRate': (sum(1 for value in frame_ms if value <= 16.67) / len(frame_ms)) if frame_ms else 0.0,
        'peakGpuMemoryMb': max(gpu_mem) if gpu_mem else 0.0,
        'peakCpuMemoryMb': max(cpu_mem) if cpu_mem else 0.0,
        'avgEditLatencyMs': statistics.fmean(edit_latency) if edit_latency else 0.0,
        'avgBoundaryTransitionScore': statistics.fmean(boundary) if boundary else 0.0,
        'avgEditLocalityRatio': statistics.fmean(locality) if locality else 0.0,
        'avgEditCoverageRatio': statistics.fmean(coverage) if coverage else 0.0,
        'maxCorrectivePenetration': max(penetration) if penetration else 0.0,
    }
    return summary


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
    parser.add_argument("--viewer-perf-csv", default="", help="Optional DX11 viewer Export Perf CSV for summary aggregation")
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

    viewer_perf_summary = None
    if args.viewer_perf_csv:
        viewer_perf_path = Path(args.viewer_perf_csv)
        viewer_perf_summary = summarize_viewer_perf_csv(viewer_perf_path)
        (output_dir / "viewer_perf_summary.json").write_text(json.dumps(viewer_perf_summary, indent=2), encoding="utf-8")
        with (output_dir / "viewer_perf_summary.csv").open("w", encoding="utf-8", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["metric", "value"])
            for key, value in viewer_perf_summary.items():
                writer.writerow([key, value])

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
        "viewerPerfCsv": args.viewer_perf_csv,
        "viewerPerfSummaryJson": str(output_dir / "viewer_perf_summary.json") if viewer_perf_summary else "",
    }
    manifest_path = output_dir / "core_eval_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"[done] manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
