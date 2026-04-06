from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

from hairgpu_final_db import FinalStageDatabase, RuntimeRunMetadata
from hairgpu_final_excel import export_workbook


def wait_for_file(path: Path, timeout_seconds: int) -> None:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if path.exists() and path.stat().st_size > 0:
            return
        time.sleep(0.5)
    raise TimeoutError(f"Timed out waiting for {path}")


def run_viewer(viewer_exe: Path, telemetry_dir: Path, run_name: str, timeout_seconds: int) -> tuple[Path, Path]:
    env = os.environ.copy()
    env["HAIRGPU_AUTO_TELEMETRY"] = "1"
    env["HAIRGPU_TELEMETRY_DIR"] = str(telemetry_dir)
    env["HAIRGPU_TELEMETRY_RUN_NAME"] = run_name

    process = subprocess.Popen([str(viewer_exe)], env=env)
    try:
        process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired:
        process.terminate()
        process.wait(timeout=10)

    summary_json = telemetry_dir / f"{run_name}_summary.json"
    frame_csv = telemetry_dir / f"{run_name}_frames.csv"
    wait_for_file(summary_json, 10)
    wait_for_file(frame_csv, 10)
    return summary_json, frame_csv


def main() -> int:
    parser = argparse.ArgumentParser(description="Run final stage DX11 telemetry -> SQLite -> Excel pipeline")
    parser.add_argument("--db", required=True)
    parser.add_argument("--schema", default="tools/final_stage/schema.sql")
    parser.add_argument("--excel", required=True)
    parser.add_argument("--experiment", required=True)
    parser.add_argument("--method", required=True)
    parser.add_argument("--scenario", required=True)
    parser.add_argument("--asset", default="")
    parser.add_argument("--run-name", required=True)
    parser.add_argument("--operator", default="")
    parser.add_argument("--notes", default="")
    parser.add_argument("--runtime-source", default="dx11_viewer")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--telemetry-dir", required=True)
    parser.add_argument("--summary-json")
    parser.add_argument("--frame-csv")
    parser.add_argument("--timeout-seconds", type=int, default=120)
    args = parser.parse_args()

    db = FinalStageDatabase(Path(args.db))
    db.initialize(Path(args.schema))

    telemetry_dir = Path(args.telemetry_dir)
    telemetry_dir.mkdir(parents=True, exist_ok=True)

    if args.summary_json and args.frame_csv:
        summary_json = Path(args.summary_json)
        frame_csv = Path(args.frame_csv)
    elif args.viewer_exe:
        if sys.platform != "win32":
            print("Viewer launch is Windows-only. Use --summary-json/--frame-csv to ingest existing telemetry on this platform.", file=sys.stderr)
            return 2
        summary_json, frame_csv = run_viewer(Path(args.viewer_exe), telemetry_dir, args.run_name, args.timeout_seconds)
    else:
        raise SystemExit("Provide either --viewer-exe or both --summary-json and --frame-csv")

    metadata = RuntimeRunMetadata(
        experiment_name=args.experiment,
        method_code=args.method,
        scenario_code=args.scenario,
        asset_name=args.asset,
        run_name=args.run_name,
        runtime_source=args.runtime_source,
        operator_name=args.operator,
        notes=args.notes,
    )
    run_id = db.import_runtime_run(metadata, summary_json, frame_csv)
    db.close()
    export_workbook(Path(args.db), Path(args.excel))
    print(f"Imported runtime run {run_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
