from __future__ import annotations

import argparse
import csv
import hashlib
import json
import sys
import zipfile
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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def resolve_path(base_dir: Path, value: str | None) -> Path | None:
    if not value:
        return None
    path = Path(value)
    if path.is_absolute():
        return path
    return (base_dir / path).resolve()


def load_metric_definitions(base_dir: Path, config: dict[str, Any]) -> dict[str, Any] | None:
    metric_path = resolve_path(base_dir, config.get("metric_definitions"))
    if metric_path and metric_path.exists():
        return read_json(metric_path)
    return None


def write_markdown(
    path: Path,
    summary: dict[str, Any],
    method_rows: list[dict[str, Any]],
    scenario_rows: list[dict[str, Any]],
    pairwise_rows: list[dict[str, Any]],
    goal_rows: list[dict[str, Any]],
    metric_definitions: dict[str, Any] | None,
    scenario_coverage_rows: list[dict[str, Any]],
    hypothesis_rows: list[dict[str, Any]],
    task_summary_rows: list[dict[str, Any]],
) -> None:
    lines = [
        "# HairSystem Final Runtime Suite Summary",
        "",
        "## Overall",
        "",
        f"- Runs: {summary.get('run_count', 0)}",
        f"- Methods: {summary.get('method_count', 0)}",
        f"- Scenarios: {summary.get('scenario_count', 0)}",
        f"- Avg frame ms: {float(summary.get('avg_frame_ms') or 0.0):.3f}",
        f"- Avg p95 frame ms: {float(summary.get('avg_p95_frame_ms') or 0.0):.3f}",
        f"- Avg 60 FPS ratio: {float(summary.get('avg_sixty_fps_ratio') or 0.0):.3f}",
        "",
        "## Method leaderboard",
        "",
        "| Method | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in method_rows:
        lines.append(
            f"| {row['method_code']} | {row['run_count']} | {float(row['avg_frame_ms'] or 0.0):.3f} | {float(row['avg_p95_frame_ms'] or 0.0):.3f} | {float(row['avg_worst_frame_ms'] or 0.0):.3f} | {float(row['avg_fps'] or 0.0):.3f} | {float(row['avg_sixty_fps_ratio'] or 0.0):.3f} |"
        )
    lines += ["", "## Scenario summary", "", "| Scenario | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio |", "|---|---:|---:|---:|---:|---:|---:|"]
    for row in scenario_rows:
        lines.append(
            f"| {row['scenario_code']} | {row['run_count']} | {float(row['avg_frame_ms'] or 0.0):.3f} | {float(row['avg_p95_frame_ms'] or 0.0):.3f} | {float(row['avg_worst_frame_ms'] or 0.0):.3f} | {float(row['avg_fps'] or 0.0):.3f} | {float(row['avg_sixty_fps_ratio'] or 0.0):.3f} |"
        )
    if pairwise_rows:
        lines += ["", "## Pairwise deltas vs baseline", "", "| Scenario | Method | Baseline | Δ avg frame ms | Δ p95 ms | Δ 60 FPS ratio | Better overall |", "|---|---|---|---:|---:|---:|---|"]
        for row in pairwise_rows:
            lines.append(
                f"| {row['scenario_code']} | {row['method_code']} | {row['baseline_method']} | {float(row['delta_avg_frame_ms']):.3f} | {float(row['delta_p95_frame_ms']):.3f} | {float(row['delta_sixty_fps_ratio']):.3f} | {row['better_overall']} |"
            )
    if goal_rows:
        lines += ["", "## Goal checks", "", "| Goal | Actual | Threshold | Passed |", "|---|---:|---:|---|"]
        for row in goal_rows:
            lines.append(f"| {row['goal']} | {float(row['actual']):.3f} | {float(row['threshold']):.3f} | {row['passed']} |")
    if scenario_coverage_rows:
        lines += ["", "## Scenario coverage", "", "| Scenario | Category | Required method | Present | Notes |", "|---|---|---|---|---|"]
        for row in scenario_coverage_rows:
            lines.append(
                f"| {row['scenario_code']} | {row.get('category','')} | {row['required_method_code']} | {row['present']} | {row.get('notes','')} |"
            )
    if hypothesis_rows:
        lines += ["", "## Hypothesis checks", "", "| ID | Source | Metric | Actual | Threshold | Comparator | Passed |", "|---|---|---|---:|---:|---|---|"]
        for row in hypothesis_rows:
            lines.append(
                f"| {row['hypothesis_id']} | {row['source_type']} | {row['metric_name']} | {float(row['actual_value']):.3f} | {float(row['threshold_value']):.3f} | {row['comparator']} | {row['passed']} |"
            )
    if task_summary_rows:
        lines += ["", "## User task summary", "", "| Task | Method | Samples | Avg completion s | Avg redo | Avg unintended change | Avg satisfaction |", "|---|---|---:|---:|---:|---:|---:|"]
        for row in task_summary_rows:
            lines.append(
                f"| {row['task_id']} | {row['method_code']} | {row['sample_count']} | {float(row['avg_completion_seconds'] or 0.0):.3f} | {float(row['avg_redo_count'] or 0.0):.3f} | {float(row['avg_unintended_change_score'] or 0.0):.3f} | {float(row['avg_satisfaction_score'] or 0.0):.3f} |"
            )
    if metric_definitions:
        runtime_metrics = metric_definitions.get("runtimeMetrics", [])
        if runtime_metrics:
            lines += ["", "## Runtime metric definitions", ""]
            for metric in runtime_metrics:
                lines.append(f"- **{metric.get('name', 'metric')}**: {metric.get('interpretation', '')}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def emit_charts(
    charts_dir: Path,
    method_rows: list[dict[str, Any]],
    pairwise_rows: list[dict[str, Any]],
    coverage_rows: list[dict[str, Any]],
    task_summary_rows: list[dict[str, Any]],
) -> list[str]:
    if plt is None or not method_rows:
        return []
    charts_dir.mkdir(parents=True, exist_ok=True)
    created: list[str] = []
    ordered = sorted(method_rows, key=lambda row: DEFAULT_METHOD_ORDER.index(row['method_code']) if row['method_code'] in DEFAULT_METHOD_ORDER else len(DEFAULT_METHOD_ORDER))
    labels = [row['method_code'] for row in ordered]
    p95 = [float(row['avg_p95_frame_ms'] or 0.0) for row in ordered]
    ratio = [float(row['avg_sixty_fps_ratio'] or 0.0) for row in ordered]

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

    if pairwise_rows:
        delta_labels = [f"{row['scenario_code']}:{row['method_code']}" for row in pairwise_rows]
        deltas = [float(row['delta_p95_frame_ms']) for row in pairwise_rows]
        plt.figure(figsize=(10, 5))
        plt.bar(delta_labels, deltas)
        plt.ylabel('Δ p95 frame ms vs baseline')
        plt.title('Pairwise p95 deltas')
        plt.xticks(rotation=35, ha='right')
        plt.tight_layout()
        delta_path = charts_dir / 'pairwise_delta_p95_frame_ms.png'
        plt.savefig(delta_path, dpi=160)
        plt.close()
        created.append(str(delta_path))

    if coverage_rows:
        missing_by_scenario: dict[str, int] = {}
        for row in coverage_rows:
            if row['present'] != 'yes':
                missing_by_scenario[row['scenario_code']] = missing_by_scenario.get(row['scenario_code'], 0) + 1
        if missing_by_scenario:
            plt.figure(figsize=(8, 4.5))
            plt.bar(list(missing_by_scenario.keys()), list(missing_by_scenario.values()))
            plt.ylabel('Missing required methods')
            plt.title('Scenario coverage gaps')
            plt.xticks(rotation=20, ha='right')
            plt.tight_layout()
            gap_path = charts_dir / 'scenario_coverage_gaps.png'
            plt.savefig(gap_path, dpi=160)
            plt.close()
            created.append(str(gap_path))

    if task_summary_rows:
        completion_by_method: dict[str, list[float]] = {}
        for row in task_summary_rows:
            value = row.get('avg_completion_seconds')
            if value is None:
                continue
            completion_by_method.setdefault(row['method_code'], []).append(float(value))
        if completion_by_method:
            labels = list(completion_by_method.keys())
            values = [sum(vals) / len(vals) for vals in completion_by_method.values()]
            plt.figure(figsize=(8, 4.5))
            plt.bar(labels, values)
            plt.ylabel('Avg task completion seconds')
            plt.title('User task time by method')
            plt.xticks(rotation=20, ha='right')
            plt.tight_layout()
            task_path = charts_dir / 'user_task_completion_seconds.png'
            plt.savefig(task_path, dpi=160)
            plt.close()
            created.append(str(task_path))
    return created


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
    run_id = db.import_runtime_run(metadata, summary_json, frame_csv)
    for artifact in run.get('artifacts', []):
        category = artifact.get('category', 'artifact')
        label = artifact.get('label', Path(artifact['path']).name)
        artifact_path = resolve_path(base_dir, artifact['path'])
        if artifact_path and artifact_path.exists():
            db.add_artifact(run_id, category, label, artifact_path)
    return run_id


def build_pairwise_rows(comparison_rows: list[dict[str, Any]], baseline_method: str) -> list[dict[str, Any]]:
    by_scenario_method = {(row['scenario_code'], row['method_code']): row for row in comparison_rows}
    scenarios = sorted({row['scenario_code'] for row in comparison_rows})
    methods = sorted({row['method_code'] for row in comparison_rows if row['method_code'] != baseline_method})
    pairwise: list[dict[str, Any]] = []
    for scenario in scenarios:
        baseline = by_scenario_method.get((scenario, baseline_method))
        if not baseline:
            continue
        for method in methods:
            row = by_scenario_method.get((scenario, method))
            if not row:
                continue
            delta_avg = float(row['avg_frame_ms'] or 0.0) - float(baseline['avg_frame_ms'] or 0.0)
            delta_p95 = float(row['avg_p95_frame_ms'] or 0.0) - float(baseline['avg_p95_frame_ms'] or 0.0)
            delta_ratio = float(row['avg_sixty_fps_ratio'] or 0.0) - float(baseline['avg_sixty_fps_ratio'] or 0.0)
            better = 'yes' if delta_p95 <= 0 and delta_ratio >= 0 else 'no'
            pairwise.append({
                'scenario_code': scenario,
                'method_code': method,
                'baseline_method': baseline_method,
                'delta_avg_frame_ms': delta_avg,
                'delta_p95_frame_ms': delta_p95,
                'delta_sixty_fps_ratio': delta_ratio,
                'better_overall': better,
            })
    return pairwise


def build_goal_rows(summary: dict[str, Any], goals: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not goals:
        return rows
    specs = [
        ('avg_p95_frame_ms', 'max_avg_p95_frame_ms', 'le'),
        ('avg_frame_ms', 'max_avg_frame_ms', 'le'),
        ('avg_sixty_fps_ratio', 'min_avg_sixty_fps_ratio', 'ge'),
        ('avg_fps', 'min_avg_fps', 'ge'),
    ]
    for actual_key, threshold_key, mode in specs:
        if threshold_key not in goals:
            continue
        actual = float(summary.get(actual_key) or 0.0)
        threshold = float(goals[threshold_key])
        passed = actual <= threshold if mode == 'le' else actual >= threshold
        rows.append({
            'goal': threshold_key,
            'actual': actual,
            'threshold': threshold,
            'passed': 'yes' if passed else 'no',
        })
    return rows


def build_user_task_rows(config: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for task in config.get('user_tasks', []):
        methods = task.get('methods') or [task.get('method', '')]
        for method in methods:
            rows.append({
                'task_id': task.get('task_id', ''),
                'title': task.get('title', ''),
                'method_code': normalize_method_code(method),
                'scenario_code': task.get('scenario', ''),
                'asset_name': task.get('asset', ''),
                'participant_id': '',
                'completion_seconds': '',
                'redo_count': '',
                'unintended_change_score': '',
                'satisfaction_score': '',
                'notes': task.get('notes', ''),
            })
    return rows


def import_user_task_results(db: FinalStageDatabase, experiment_name: str, task_results_csv: Path) -> list[dict[str, Any]]:
    imported: list[dict[str, Any]] = []
    if not task_results_csv.exists():
        return imported
    with task_results_csv.open('r', encoding='utf-8', newline='') as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            payload = {
                'task_id': row.get('task_id', ''),
                'title': row.get('title', row.get('task_title', '')),
                'method_code': normalize_method_code(row.get('method_code', row.get('method', ''))),
                'scenario_code': row.get('scenario_code', row.get('scenario', '')),
                'asset_name': row.get('asset_name', row.get('asset', '')),
                'participant_id': row.get('participant_id', ''),
                'completion_seconds': float(row['completion_seconds']) if row.get('completion_seconds') not in (None, '') else None,
                'redo_count': int(float(row['redo_count'])) if row.get('redo_count') not in (None, '') else None,
                'unintended_change_score': float(row['unintended_change_score']) if row.get('unintended_change_score') not in (None, '') else None,
                'satisfaction_score': float(row['satisfaction_score']) if row.get('satisfaction_score') not in (None, '') else None,
                'notes': row.get('notes', ''),
            }
            db.add_user_task_result(experiment_name=experiment_name, **payload)
            imported.append(payload)
    return imported


def build_scenario_coverage_rows(config: dict[str, Any], comparison_rows: list[dict[str, Any]], baseline_method: str) -> list[dict[str, Any]]:
    present = {(row['scenario_code'], row['method_code']) for row in comparison_rows}
    rows: list[dict[str, Any]] = []
    matrix = config.get('scenario_matrix', [])
    if not matrix:
        all_scenarios = sorted({row['scenario_code'] for row in comparison_rows})
        all_methods = sorted({row['method_code'] for row in comparison_rows} | {baseline_method})
        matrix = [{'scenario': scenario, 'required_methods': all_methods, 'category': ''} for scenario in all_scenarios]
    for item in matrix:
        scenario_code = item.get('scenario', '')
        required_methods = [normalize_method_code(method) for method in item.get('required_methods', [])]
        if not required_methods:
            required_methods = [baseline_method]
        for method_code in required_methods:
            rows.append({
                'scenario_code': scenario_code,
                'category': item.get('category', ''),
                'required_method_code': method_code,
                'present': 'yes' if (scenario_code, method_code) in present else 'no',
                'notes': item.get('notes', ''),
            })
    return rows


def normalize_comparator(value: str | None) -> str:
    mapping = {
        '<=': 'le',
        'le': 'le',
        '<': 'lt',
        'lt': 'lt',
        '>=': 'ge',
        'ge': 'ge',
        '>': 'gt',
        'gt': 'gt',
        '==': 'eq',
        'eq': 'eq',
    }
    return mapping.get((value or 'le').strip().lower(), 'le')


def evaluate_comparator(actual: float, comparator: str, threshold: float) -> bool:
    if comparator == 'le':
        return actual <= threshold
    if comparator == 'lt':
        return actual < threshold
    if comparator == 'ge':
        return actual >= threshold
    if comparator == 'gt':
        return actual > threshold
    return abs(actual - threshold) < 1e-9


def build_hypothesis_rows(
    config: dict[str, Any],
    comparison_rows: list[dict[str, Any]],
    task_summary_rows: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    runtime_lookup = {(row['scenario_code'], row['method_code']): row for row in comparison_rows}
    task_lookup = {(row['task_id'], row['method_code']): row for row in task_summary_rows}
    rows: list[dict[str, Any]] = []
    for item in config.get('hypotheses', []):
        source_type = item.get('source', 'runtime')
        comparator = normalize_comparator(item.get('comparator'))
        method_code = normalize_method_code(item.get('method', '')) if item.get('method') else ''
        baseline_code = normalize_method_code(item.get('baseline', '')) if item.get('baseline') else ''
        metric_name = item.get('metric', '')
        actual_value = None
        threshold_value = float(item.get('threshold', 0.0))
        notes = ''
        scenario_code = item.get('scenario', '')
        task_id = item.get('task_id', '')
        if source_type == 'runtime':
            method_row = runtime_lookup.get((scenario_code, method_code))
            baseline_row = runtime_lookup.get((scenario_code, baseline_code)) if baseline_code else None
            if method_row:
                method_metric = float(method_row.get(metric_name, 0.0) or 0.0)
                if baseline_row:
                    baseline_metric = float(baseline_row.get(metric_name, 0.0) or 0.0)
                    actual_value = method_metric - baseline_metric
                    threshold_value = float(item.get('threshold_delta', threshold_value))
                    notes = f"method={method_metric:.3f}, baseline={baseline_metric:.3f}"
                else:
                    actual_value = method_metric
                    notes = f"method={method_metric:.3f}"
        elif source_type == 'user_task':
            method_row = task_lookup.get((task_id, method_code))
            baseline_row = task_lookup.get((task_id, baseline_code)) if baseline_code else None
            if method_row:
                method_metric = float(method_row.get(metric_name, 0.0) or 0.0)
                if baseline_row:
                    baseline_metric = float(baseline_row.get(metric_name, 0.0) or 0.0)
                    actual_value = method_metric - baseline_metric
                    threshold_value = float(item.get('threshold_delta', threshold_value))
                    notes = f"method={method_metric:.3f}, baseline={baseline_metric:.3f}"
                else:
                    actual_value = method_metric
                    notes = f"method={method_metric:.3f}"
        if actual_value is None:
            notes = (notes + '; ' if notes else '') + 'missing_data'
            passed = False
            actual_value = 0.0
        else:
            passed = evaluate_comparator(actual_value, comparator, threshold_value)
        rows.append({
            'hypothesis_id': item.get('hypothesis_id', ''),
            'title': item.get('title', ''),
            'source_type': source_type,
            'scenario_code': scenario_code,
            'task_id': task_id,
            'method_code': method_code,
            'baseline_method_code': baseline_code,
            'metric_name': metric_name,
            'actual_value': actual_value,
            'threshold_value': threshold_value,
            'comparator': comparator,
            'passed': 'yes' if passed else 'no',
            'notes': notes,
        })
    return rows


def write_repro_manifest(path: Path, config_path: Path, db_path: Path, excel_path: Path, outputs_dir: Path, generated_files: list[Path]) -> dict[str, Any]:
    manifest = {
        'configPath': str(config_path),
        'configSha256': sha256_file(config_path),
        'databasePath': str(db_path),
        'excelPath': str(excel_path),
        'outputsDir': str(outputs_dir),
        'generatedFiles': [
            {
                'path': str(item),
                'sha256': sha256_file(item),
                'sizeBytes': item.stat().st_size,
            }
            for item in generated_files if item.exists() and item.is_file()
        ],
    }
    write_json(path, manifest)
    return manifest


def maybe_package_zip(outputs_dir: Path, zip_path: Path) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for file_path in sorted(outputs_dir.rglob('*')):
            if file_path.is_file():
                archive.write(file_path, arcname=file_path.relative_to(outputs_dir))


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
    baseline_method = normalize_method_code(config.get('baseline_method', 'prediction_only'))
    metric_definitions = load_metric_definitions(base_dir, config)
    suite_name = config.get('suite_name', config_path.stem)
    experiment_name = config.get('experiment_name') or config.get('runs', [{}])[0].get('experiment', 'runtime_suite')

    db = FinalStageDatabase(db_path)
    db.initialize(schema_path)
    db.clear_suite_rows(suite_name)

    imported_run_ids = []
    for run in config.get('runs', []):
        imported_run_ids.append(import_run(db, base_dir, run, telemetry_dir))

    task_results_csv = resolve_path(base_dir, config.get('user_task_results_csv'))
    imported_task_rows = import_user_task_results(db, experiment_name, task_results_csv) if task_results_csv else []

    method_rows = db.fetch_method_summary_rows()
    scenario_rows = db.fetch_scenario_summary_rows()
    summary = db.fetch_experiment_summary()
    comparison_rows = [dict(row) for row in db.fetch_comparison_rows()]
    ablation_rows = [dict(row) for row in db.fetch_ablation_rows()]
    pairwise_rows = build_pairwise_rows(comparison_rows, baseline_method)
    goal_rows = build_goal_rows(summary, config.get('goals', {}))
    user_task_rows = build_user_task_rows(config)
    task_summary_rows = db.fetch_user_task_summary_rows()
    scenario_coverage_rows = build_scenario_coverage_rows(config, comparison_rows, baseline_method)
    for row in scenario_coverage_rows:
        db.add_scenario_coverage(
            suite_name=suite_name,
            scenario_code=row['scenario_code'],
            required_method_code=row['required_method_code'],
            is_present=row['present'] == 'yes',
            category=row.get('category', ''),
            notes=row.get('notes', ''),
        )
    hypothesis_rows = build_hypothesis_rows(config, comparison_rows, task_summary_rows)
    for row in hypothesis_rows:
        db.add_hypothesis_check(
            suite_name=suite_name,
            hypothesis_id=row['hypothesis_id'],
            title=row['title'],
            source_type=row['source_type'],
            metric_name=row['metric_name'],
            actual_value=row['actual_value'],
            threshold_value=row['threshold_value'],
            comparator=row['comparator'],
            passed=row['passed'] == 'yes',
            scenario_code=row.get('scenario_code', ''),
            task_id=row.get('task_id', ''),
            method_code=row.get('method_code', ''),
            baseline_method_code=row.get('baseline_method_code', ''),
            notes=row.get('notes', ''),
        )
    db.close()

    export_workbook(db_path, excel_path)

    outputs_dir.mkdir(parents=True, exist_ok=True)
    summary_json_path = outputs_dir / 'runtime_suite_summary.json'
    summary_csv_path = outputs_dir / 'runtime_suite_method_summary.csv'
    scenario_csv_path = outputs_dir / 'runtime_suite_scenario_summary.csv'
    ablation_csv_path = outputs_dir / 'runtime_suite_ablation_summary.csv'
    pairwise_csv_path = outputs_dir / 'runtime_suite_pairwise_vs_baseline.csv'
    goal_csv_path = outputs_dir / 'runtime_suite_goal_check.csv'
    scenario_coverage_csv_path = outputs_dir / 'runtime_suite_scenario_coverage.csv'
    hypothesis_csv_path = outputs_dir / 'runtime_suite_hypothesis_checks.csv'
    user_tasks_csv_path = outputs_dir / 'runtime_suite_user_task_checklist.csv'
    user_task_results_csv_path = outputs_dir / 'runtime_suite_user_task_results_imported.csv'
    user_task_summary_csv_path = outputs_dir / 'runtime_suite_user_task_summary.csv'
    markdown_path = outputs_dir / 'runtime_suite_summary.md'
    repro_manifest_path = outputs_dir / 'runtime_suite_repro_manifest.json'
    charts_dir = outputs_dir / 'charts'

    chart_paths = emit_charts(charts_dir, method_rows, pairwise_rows, scenario_coverage_rows, task_summary_rows)
    payload = {
        'suiteName': suite_name,
        'experimentName': experiment_name,
        'importedRunIds': imported_run_ids,
        'summary': summary,
        'methodSummary': method_rows,
        'scenarioSummary': scenario_rows,
        'ablationSummary': ablation_rows,
        'pairwiseVsBaseline': pairwise_rows,
        'goalChecks': goal_rows,
        'scenarioCoverage': scenario_coverage_rows,
        'hypothesisChecks': hypothesis_rows,
        'userTaskChecklist': user_task_rows,
        'userTaskSummary': task_summary_rows,
        'excelPath': str(excel_path),
        'databasePath': str(db_path),
        'baselineMethod': baseline_method,
    }
    if imported_task_rows:
        payload['importedUserTaskResults'] = imported_task_rows
    if metric_definitions:
        payload['metricDefinitions'] = metric_definitions
    if chart_paths:
        payload['charts'] = chart_paths

    write_json(summary_json_path, payload)
    write_csv(summary_csv_path, method_rows)
    write_csv(scenario_csv_path, scenario_rows)
    write_csv(ablation_csv_path, ablation_rows)
    write_csv(pairwise_csv_path, pairwise_rows)
    write_csv(goal_csv_path, goal_rows)
    write_csv(scenario_coverage_csv_path, scenario_coverage_rows)
    write_csv(hypothesis_csv_path, hypothesis_rows)
    write_csv(user_tasks_csv_path, user_task_rows)
    write_csv(user_task_results_csv_path, imported_task_rows)
    write_csv(user_task_summary_csv_path, task_summary_rows)
    write_markdown(markdown_path, summary, method_rows, scenario_rows, pairwise_rows, goal_rows, metric_definitions, scenario_coverage_rows, hypothesis_rows, task_summary_rows)

    generated_files = [
        summary_json_path,
        summary_csv_path,
        scenario_csv_path,
        ablation_csv_path,
        pairwise_csv_path,
        goal_csv_path,
        scenario_coverage_csv_path,
        hypothesis_csv_path,
        user_tasks_csv_path,
        user_task_results_csv_path,
        user_task_summary_csv_path,
        markdown_path,
        excel_path,
        db_path,
    ]
    generated_files += [Path(p) for p in chart_paths]
    repro_manifest = write_repro_manifest(repro_manifest_path, config_path, db_path, excel_path, outputs_dir, generated_files)
    payload['reproManifest'] = repro_manifest
    write_json(summary_json_path, payload)

    package_zip = resolve_path(base_dir, config.get('package_zip'))
    if package_zip:
        maybe_package_zip(outputs_dir, package_zip)

    print(f'Imported {len(imported_run_ids)} runs')
    print(f'Imported user task rows: {len(imported_task_rows)}')
    print(f'Database: {db_path}')
    print(f'Excel: {excel_path}')
    print(f'Summary JSON: {summary_json_path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
