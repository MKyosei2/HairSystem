from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, List

from hairgpu_phase1_db import (
    connect,
    initialize_database,
    insert_experiment,
    insert_frame_metrics,
    insert_run,
    upsert_metrics_summary,
)
from hairgpu_phase1_excel import export_workbook


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def try_git_commit(repo_root: Path) -> str:
    try:
        result = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo_root, check=True, capture_output=True, text=True)
        return result.stdout.strip()
    except Exception:
        return "unknown"


def parse_validation_text(text: str) -> Dict[str, Any]:
    line = text.strip().splitlines()[0] if text.strip() else ""
    payload: Dict[str, Any] = {
        "valid": 1 if line.startswith("valid") else 0,
    }
    for token in line.replace("|", " ").split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        try:
            if "." in value:
                payload[key] = float(value)
            else:
                payload[key] = int(value)
        except ValueError:
            payload[key] = value
    return {
        "valid": payload.get("valid", 0),
        "validation_info_count": payload.get("info", 0),
        "validation_warning_count": payload.get("warnings", 0),
        "validation_error_count": payload.get("errors", 0),
        "average_segment_length": payload.get("avgSeg"),
        "max_segment_length": payload.get("maxSeg"),
    }


def run_subprocess(args, cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True)


def read_frame_csv(frame_csv_path: Path) -> List[Dict[str, Any]]:
    rows: List[Dict[str, Any]] = []
    with frame_csv_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(
                {
                    "frame_index": int(row["frame_index"]),
                    "frame_time_ms": float(row["frame_time_ms"]),
                    "active_cluster_count": int(row["active_cluster_count"]),
                    "penetration_count": int(row["penetration_count"]),
                    "max_penetration_depth_mm": float(row["max_penetration_depth_mm"]),
                    "visible_hair_ratio": float(row["visible_hair_ratio"]),
                    "gpu_memory_mb": float(row["gpu_memory_mb"]),
                    "cpu_memory_mb": float(row["cpu_memory_mb"]),
                }
            )
    return rows


def choose_value(auto_value: Any, override_value: Any) -> Any:
    return override_value if override_value is not None else auto_value


def run_phase1(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    db_path = Path(args.db_path).resolve()
    output_dir = Path(args.output_dir).resolve()
    asset_path = Path(args.asset_path).resolve()
    cli_path = Path(args.cli_path).resolve()

    initialize_database(db_path)
    output_dir.mkdir(parents=True, exist_ok=True)

    created_at = utc_now()
    experiment_payload = {
        "experiment_name": args.experiment_name,
        "created_at": created_at,
        "operator_name": args.operator_name,
        "git_commit": try_git_commit(repo_root),
        "build_type": args.build_type,
        "notes": args.notes,
    }

    metrics_path = output_dir / f"{asset_path.stem}_metrics.json"
    validation_path = output_dir / f"{asset_path.stem}_validation.txt"
    runtime_summary_path = output_dir / f"{asset_path.stem}_phase1_runtime_summary.json"
    frame_csv_path = output_dir / f"{asset_path.stem}_frame_metrics.csv"

    metrics_proc = run_subprocess([str(cli_path), "metrics-json", str(asset_path), str(metrics_path)], repo_root)
    validation_proc = run_subprocess([str(cli_path), "validate", str(asset_path), str(validation_path)], repo_root)
    runtime_proc = run_subprocess(
        [
            str(cli_path),
            "phase1-proxy-run",
            str(asset_path),
            str(runtime_summary_path),
            str(frame_csv_path),
            args.scenario_code,
            args.method_code,
            str(args.frame_count),
            str(args.target_fps),
        ],
        repo_root,
    )

    if metrics_proc.returncode != 0:
        sys.stderr.write(metrics_proc.stderr)
        return metrics_proc.returncode
    if validation_proc.returncode not in (0, 4):
        sys.stderr.write(validation_proc.stderr)
        return validation_proc.returncode
    if runtime_proc.returncode not in (0, 4):
        sys.stderr.write(runtime_proc.stderr)
        return runtime_proc.returncode

    metrics_payload = json.loads(metrics_path.read_text(encoding="utf-8"))
    runtime_payload = json.loads(runtime_summary_path.read_text(encoding="utf-8"))
    validation_summary = parse_validation_text(validation_path.read_text(encoding="utf-8"))
    frame_rows = read_frame_csv(frame_csv_path)

    summary_payload = {
        "runtime_mode": runtime_payload.get("runtimeMode", "proxy"),
        "frame_count": runtime_payload.get("frameCount"),
        "target_fps": runtime_payload.get("targetFps"),
        "strand_count": metrics_payload.get("strandCount"),
        "guide_count": metrics_payload.get("guideCount"),
        "follower_count": metrics_payload.get("followerCount"),
        "point_count": metrics_payload.get("pointCount"),
        "particles_per_strand": metrics_payload.get("particlesPerStrand"),
        "total_length": metrics_payload.get("totalLength"),
        "average_length": metrics_payload.get("averageLength"),
        "max_length": metrics_payload.get("maxLength"),
        "average_width": metrics_payload.get("averageWidth"),
        "average_follower_per_guide": metrics_payload.get("averageFollowerPerGuide"),
        "fps_60_ratio": choose_value(runtime_payload.get("fps60Ratio"), args.fps_60_ratio),
        "frame_time_p95_ms": choose_value(runtime_payload.get("frameTimeP95Ms"), args.frame_time_p95_ms),
        "worst_frame_time_ms": choose_value(runtime_payload.get("worstFrameTimeMs"), args.worst_frame_time_ms),
        "peak_ram_mb": choose_value(runtime_payload.get("peakRamMb"), args.peak_ram_mb),
        "average_active_cluster_count": runtime_payload.get("averageActiveClusterCount"),
        "max_active_cluster_count": runtime_payload.get("maxActiveClusterCount"),
        "penetration_count_per_sec": choose_value(runtime_payload.get("penetrationCountPerSec"), args.penetration_count_per_sec),
        "max_penetration_depth_mm": choose_value(runtime_payload.get("maxPenetrationDepthMm"), args.max_penetration_depth_mm),
        "silhouette_error": choose_value(runtime_payload.get("silhouetteError"), args.silhouette_error),
        "clump_preservation_score": choose_value(runtime_payload.get("clumpPreservationScore"), args.clump_preservation_score),
        "edit_to_run_latency_ms": choose_value(runtime_payload.get("editToRunLatencyMs"), args.edit_to_run_latency_ms),
        "metrics_json_path": str(metrics_path),
        "validation_json_path": str(validation_path),
        "frame_metrics_csv_path": str(frame_csv_path),
        **validation_summary,
    }

    conn = connect(db_path)
    try:
        experiment_id = insert_experiment(conn, experiment_payload)
        run_id = insert_run(
            conn,
            {
                "experiment_id": experiment_id,
                "scenario_code": args.scenario_code,
                "method_code": args.method_code,
                "asset_name": asset_path.name,
                "asset_path": str(asset_path),
                "camera_setup": args.camera_setup,
                "hardware_profile": args.hardware_profile,
                "status": "completed" if validation_proc.returncode == 0 else "completed_with_validation_errors",
                "started_at": created_at,
                "ended_at": utc_now(),
                "config": {
                    "phase": "phase1",
                    "runtime_mode": runtime_payload.get("runtimeMode", "proxy"),
                    "frame_count": args.frame_count,
                    "target_fps": args.target_fps,
                    "notes": args.notes,
                },
                "cli_stdout": metrics_proc.stdout + "\n" + validation_proc.stdout + "\n" + runtime_proc.stdout,
                "cli_stderr": metrics_proc.stderr + "\n" + validation_proc.stderr + "\n" + runtime_proc.stderr,
                "notes": args.notes,
            },
        )
        upsert_metrics_summary(conn, run_id, summary_payload)
        insert_frame_metrics(conn, run_id, frame_rows)
        conn.commit()
        workbook_path = export_workbook(conn, output_dir / "phase1_experiment_log.xlsx")
    finally:
        conn.close()

    print(
        json.dumps(
            {
                "db_path": str(db_path),
                "excel_path": str(workbook_path),
                "metrics_path": str(metrics_path),
                "validation_path": str(validation_path),
                "runtime_summary_path": str(runtime_summary_path),
                "frame_metrics_csv_path": str(frame_csv_path),
                "frame_rows": len(frame_rows),
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="HairGPU Phase 1 pipeline runner")
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--db-path", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--cli-path", required=True)
    parser.add_argument("--asset-path", required=True)
    parser.add_argument("--experiment-name", required=True)
    parser.add_argument("--operator-name", default="")
    parser.add_argument("--build-type", default="Debug")
    parser.add_argument("--scenario-code", choices=["S1", "S2", "S3", "S4"], required=True)
    parser.add_argument("--method-code", choices=["A", "B", "C", "D"], required=True)
    parser.add_argument("--camera-setup", default="default")
    parser.add_argument("--hardware-profile", default="unknown")
    parser.add_argument("--notes", default="")
    parser.add_argument("--frame-count", type=int, default=240)
    parser.add_argument("--target-fps", type=float, default=60.0)
    parser.add_argument("--fps-60-ratio", type=float, default=None)
    parser.add_argument("--frame-time-p95-ms", type=float, default=None)
    parser.add_argument("--worst-frame-time-ms", type=float, default=None)
    parser.add_argument("--peak-ram-mb", type=float, default=None)
    parser.add_argument("--penetration-count-per-sec", type=float, default=None)
    parser.add_argument("--max-penetration-depth-mm", type=float, default=None)
    parser.add_argument("--silhouette-error", type=float, default=None)
    parser.add_argument("--clump-preservation-score", type=float, default=None)
    parser.add_argument("--edit-to-run-latency-ms", type=float, default=None)
    return parser


if __name__ == "__main__":
    raise SystemExit(run_phase1(build_parser().parse_args()))
