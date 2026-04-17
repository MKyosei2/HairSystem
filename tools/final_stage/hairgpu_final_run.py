from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from hairgpu_final_db import FinalStageDatabase, RuntimeRunMetadata
from hairgpu_final_excel import export_workbook

METHOD_ALIASES = {
    "prediction_only": "prediction_only",
    "prediction-only": "prediction_only",
    "active_set_runtime": "active_set_runtime",
    "active-set-runtime": "active_set_runtime",
    "proposal_hybrid": "proposal_hybrid",
    "proposal-hybrid": "proposal_hybrid",
    "full_physics": "full_physics",
    "full-physics": "full_physics",
}


def normalize_method_code(method_code: str) -> str:
    return METHOD_ALIASES.get(method_code, method_code)


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


def load_config(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def copy_artifact(path: Path, destination_dir: Path) -> dict[str, Any]:
    destination_dir.mkdir(parents=True, exist_ok=True)
    dst = destination_dir / path.name
    shutil.copy2(path, dst)
    return {
        "source": str(path),
        "copiedTo": str(dst),
        "sizeBytes": dst.stat().st_size,
        "sha256": sha256_file(dst),
    }


def harvest_artifacts(values: list[str], output_dir: Path) -> list[dict[str, Any]]:
    harvested: list[dict[str, Any]] = []
    for index, value in enumerate(values):
        if "=" in value:
            label, raw_path = value.split("=", 1)
        else:
            label, raw_path = f"artifact_{index}", value
        artifact_path = Path(raw_path).resolve()
        if not artifact_path.exists():
            continue
        record = copy_artifact(artifact_path, output_dir / "artifacts")
        record["label"] = label
        harvested.append(record)
    return harvested


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run final stage DX11 telemetry -> SQLite -> Excel pipeline")
    parser.add_argument("--db")
    parser.add_argument("--schema", default="tools/final_stage/schema.sql")
    parser.add_argument("--excel")
    parser.add_argument("--experiment")
    parser.add_argument("--method")
    parser.add_argument("--scenario")
    parser.add_argument("--asset", default="")
    parser.add_argument("--run-name")
    parser.add_argument("--operator", default="")
    parser.add_argument("--notes", default="")
    parser.add_argument("--runtime-source", default="dx11_viewer")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--telemetry-dir")
    parser.add_argument("--summary-json")
    parser.add_argument("--frame-csv")
    parser.add_argument("--timeout-seconds", type=int, default=120)
    parser.add_argument("--config-json", help="Optional JSON config with the same fields as the CLI arguments.")
    parser.add_argument("--output-dir", help="Optional directory to copy telemetry and harvested artifacts into.")
    parser.add_argument(
        "--artifact",
        action="append",
        default=[],
        help="Optional artifact to attach. Use label=/absolute/or/relative/path or just a path.",
    )
    args = parser.parse_args()

    config: dict[str, Any] = {}
    if args.config_json:
        config = load_config(Path(args.config_json))

    db_path = Path(args.db or config.get("db", "runtime.db"))
    schema_path = Path(args.schema or config.get("schema", "tools/final_stage/schema.sql"))
    excel_path = Path(args.excel or config.get("excel", db_path.with_suffix(".xlsx")))
    experiment_name = args.experiment or config.get("experiment")
    method_code = normalize_method_code(args.method or config.get("method", "proposal_hybrid"))
    scenario_code = args.scenario or config.get("scenario", "default")
    asset_name = args.asset or config.get("asset", "")
    run_name = args.run_name or config.get("run_name")
    operator_name = args.operator or config.get("operator", "")
    notes = args.notes or config.get("notes", "")
    runtime_source = args.runtime_source or config.get("runtime_source", "dx11_viewer")
    timeout_seconds = int(args.timeout_seconds or config.get("timeout_seconds", 120))

    if not experiment_name or not run_name:
        raise SystemExit("--experiment and --run-name are required unless provided in --config-json")

    telemetry_dir = Path(args.telemetry_dir or config.get("telemetry_dir", "runtime_telemetry"))
    telemetry_dir.mkdir(parents=True, exist_ok=True)

    db = FinalStageDatabase(db_path)
    db.initialize(schema_path)

    if args.summary_json and args.frame_csv:
        summary_json = Path(args.summary_json)
        frame_csv = Path(args.frame_csv)
    elif config.get("summary_json") and config.get("frame_csv"):
        summary_json = Path(config["summary_json"])
        frame_csv = Path(config["frame_csv"])
    else:
        viewer_exe_value = args.viewer_exe or config.get("viewer_exe")
        if not viewer_exe_value:
            raise SystemExit("Provide either --viewer-exe or both --summary-json and --frame-csv")
        if sys.platform != "win32":
            print("Viewer launch is Windows-only. Use --summary-json/--frame-csv to ingest existing telemetry on this platform.", file=sys.stderr)
            return 2
        summary_json, frame_csv = run_viewer(Path(viewer_exe_value), telemetry_dir, run_name, timeout_seconds)

    metadata = RuntimeRunMetadata(
        experiment_name=experiment_name,
        method_code=method_code,
        scenario_code=scenario_code,
        asset_name=asset_name,
        run_name=run_name,
        runtime_source=runtime_source,
        operator_name=operator_name,
        notes=notes,
    )
    run_id = db.import_runtime_run(metadata, summary_json, frame_csv)

    output_dir_value = args.output_dir or config.get("output_dir")
    harvested: list[dict[str, Any]] = []
    if output_dir_value:
        output_dir = Path(output_dir_value)
        copied_summary = copy_artifact(summary_json, output_dir / run_name)
        copied_summary["label"] = "summary_json"
        copied_frame = copy_artifact(frame_csv, output_dir / run_name)
        copied_frame["label"] = "frame_csv"
        harvested.extend([copied_summary, copied_frame])
        for artifact in harvest_artifacts(list(config.get("artifacts", [])) + list(args.artifact), output_dir / run_name):
            harvested.append(artifact)
        manifest = {
            "runId": run_id,
            "experiment": experiment_name,
            "method": method_code,
            "scenario": scenario_code,
            "asset": asset_name,
            "runtimeSource": runtime_source,
            "operator": operator_name,
            "notes": notes,
            "telemetry": {
                "summaryJson": str(summary_json),
                "frameCsv": str(frame_csv),
                "summarySha256": sha256_file(summary_json),
                "frameSha256": sha256_file(frame_csv),
            },
            "artifacts": harvested,
            "configSnapshot": config if config else None,
        }
        write_json(output_dir / run_name / "run_manifest.json", manifest)
        for artifact in harvested:
            label = artifact.get("label", "artifact")
            copied_to = artifact.get("copiedTo")
            if copied_to:
                db.add_artifact(run_id, "harvested", label, Path(copied_to))

    db.close()
    export_workbook(db_path, excel_path)
    print(f"Imported runtime run {run_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
