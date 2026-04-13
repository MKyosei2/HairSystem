from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from hairgpu_final_db import FinalStageDatabase, RuntimeRunMetadata
from hairgpu_final_excel import export_workbook
from hairgpu_final_run import normalize_method_code, run_viewer

try:
    import matplotlib.pyplot as plt
except Exception:
    plt = None

DEFAULT_METHOD_ORDER = [
    "prediction_only",
    "active_set_runtime",
    "proposal_hybrid",
    "full_physics",
]


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    headers = list(rows[0].keys())
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)


def write_markdown(path: Path, summary: dict[str, Any], method_rows: list[dict[str, Any]], scenario_rows: list[dict[str, Any]]) -> None:
    lines = [
        "# HairSystem Final Runtime Suite Summary",
        "",
        "## Overall",
        "",
        f"- Runs: {summary.get('run_count', 0)}",
        f"- Methods: {summary.get('method_count', 0)}",
        f"- Scenarios: {summary.get('scenario_count', 0)}",
        f"- Avg frame ms: {summary.get('avg_frame_ms', 0):.3f}",
        f"- Avg p95 frame ms: {summary.get('avg_p95_frame_ms', 0):.3f}",
        f"- Avg 60 FPS ratio: {summary.get('avg_sixty_fps_ratio', 0):.3f}",
        "",
        "## Method leaderboard",
        "",
        "| Method | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in method_rows:
        lines.append(
            f"| {row['method_code']} | {row['run_count']} | {row['avg_frame_ms']:.3f} | {row['avg_p95_frame_ms']:.3f} | {row['avg_worst_frame_ms']:.3f} | {row['avg_fps']:.3f} | {row['avg_sixty_fps_ratio']:.3f} |"
        )
    lines += ["", "## Scenario summary", "", "| Scenario | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio |", "|---|---:|---:|---:|---:|---:|---:|"]
    for row in scenario_rows:
        lines.append(
            f"| {row['scenario_code']} | {row['run_count']} | {row['avg_frame_ms']:.3f} | {row['avg_p95_frame_ms']:.3f} | {row['avg_worst_frame_ms']:.3f} | {row['avg_fps']:.3f} | {row['avg_sixty_fps_ratio']:.3f} |"
        )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit_charts(charts_dir: Path, method_rows: list[dict[str, Any]]) -> list[str]:
    if plt is None or not method_rows:
        return []
    charts_dir.mkdir(parents=True, exist_ok=True)
    created: list[str] = []
    ordered = sorted(method_rows, key=lambda row: DEFAULT_METHOD_ORDER.index(row['method_code']) if row['method_code'] in DEFAULT_METHOD_ORDER else len(DEFAULT_METHOD_ORDER))
    labels = [row['method_code'] for row in ordered]
    p95 = [row['avg_p95_frame_ms'] for row in ordered]
    ratio = [row['avg_sixty_fps_ratio'] for row in ordered]

    plt.figure(figsize=(8, 4.5))
    plt.bar(labels, p95)
    plt.ylabel('Avg p95 frame ms')
    plt.title('P95 frame time by method')
    plt.xticks(rotation=20, ha='right')
    plt.tight_layout()
    p95_path = charts_dir / 'method_p95_frame_ms.png'
    plt.savefig(p95_path, dpi=160)
    plt.close()
    created.append(str(p95_path))

    plt.figure(figsize=(8, 4.5))
    plt.bar(labels, ratio)
    plt.ylabel('Avg 60 FPS ratio')
    plt.title('60 FPS ratio by method')
    plt.xticks(rotation=20, ha='right')
    plt.tight_layout()
    ratio_path = charts_dir / 'method_sixty_fps_ratio.png'
    plt.savefig(ratio_path, dpi=160)
    plt.close()
    created.append(str(ratio_path))
    return created


def resolve_path(base_dir: Path, value: str | None) -> Path | None:
    if not value:
        return None
    path = Path(value)
    if path.is_absolute():
        return path
    return (base_dir / path).resolve()


def import_run(db: FinalStageDatabase, base_dir: Path, run: dict[str, Any], default_telemetry_dir: Path) -> int:
    run_name = run['run_name']
    summary_json = resolve_path(base_dir, run.get('summary_json'))
    frame_csv = resolve_path(base_dir, run.get('frame_csv'))
    viewer_exe = resolve_path(base_dir, run.get('viewer_exe'))
    telemetry_dir = resolve_path(base_dir, run.get('telemetry_dir')) or default_telemetry_dir
    timeout_seconds = int(run.get('timeout_seconds', 120))

    if summary_json is None or frame_csv is None:
        if viewer_exe is None:
            raise ValueError(f"Run '{run_name}' must provide summary/frame telemetry or a viewer_exe")
        if sys.platform != 'win32':
            raise RuntimeError(f"Run '{run_name}' requires viewer launch, which is only supported on Windows")
        summary_json, frame_csv = run_viewer(viewer_exe, telemetry_dir, run_name, timeout_seconds)

    metadata = RuntimeRunMetadata(
        experiment_name=run['experiment'],
        method_code=normalize_method_code(run['method']),
        scenario_code=run['scenario'],
        asset_name=run.get('asset', ''),
        run_name=run_name,
        runtime_source=run.get('runtime_source', 'dx11_viewer'),
        operator_name=run.get('operator', ''),
        notes=run.get('notes', ''),
    )
    return db.import_runtime_run(metadata, summary_json, frame_csv)


def main() -> int:
    parser = argparse.ArgumentParser(description='Run/import a final runtime suite and emit summary artifacts.')
    parser.add_argument('--config', required=True, help='Path to a runtime suite manifest JSON file.')
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    config = read_json(config_path)
    base_dir = config_path.parent

    db_path = resolve_path(base_dir, config.get('db')) or (base_dir / 'runtime_suite.sqlite')
    excel_path = resolve_path(base_dir, config.get('excel')) or db_path.with_suffix('.xlsx')
    telemetry_dir = resolve_path(base_dir, config.get('telemetry_dir')) or (base_dir / 'runtime_telemetry')
    schema_path = resolve_path(base_dir, config.get('schema')) or (THIS_DIR / 'schema.sql')
    outputs_dir = resolve_path(base_dir, config.get('outputs_dir')) or (base_dir / 'runtime_suite_outputs')

    db = FinalStageDatabase(db_path)
    db.initialize(schema_path)

    imported_run_ids = []
    for run in config.get('runs', []):
        imported_run_ids.append(import_run(db, base_dir, run, telemetry_dir))

    method_rows = db.fetch_method_summary_rows()
    scenario_rows = db.fetch_scenario_summary_rows()
    summary = db.fetch_experiment_summary()
    db.close()

    export_workbook(db_path, excel_path)

    outputs_dir.mkdir(parents=True, exist_ok=True)
    summary_json_path = outputs_dir / 'runtime_suite_summary.json'
    summary_csv_path = outputs_dir / 'runtime_suite_method_summary.csv'
    scenario_csv_path = outputs_dir / 'runtime_suite_scenario_summary.csv'
    markdown_path = outputs_dir / 'runtime_suite_summary.md'
    charts_dir = outputs_dir / 'charts'

    payload = {
        'importedRunIds': imported_run_ids,
        'summary': summary,
        'methodSummary': method_rows,
        'scenarioSummary': scenario_rows,
        'excelPath': str(excel_path),
        'databasePath': str(db_path),
    }
    chart_paths = emit_charts(charts_dir, method_rows)
    if chart_paths:
        payload['charts'] = chart_paths
    write_json(summary_json_path, payload)
    write_csv(summary_csv_path, method_rows)
    write_csv(scenario_csv_path, scenario_rows)
    write_markdown(markdown_path, summary, method_rows, scenario_rows)

    print(f'Imported {len(imported_run_ids)} runs')
    print(f'Database: {db_path}')
    print(f'Excel: {excel_path}')
    print(f'Summary JSON: {summary_json_path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
