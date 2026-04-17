from __future__ import annotations

from pathlib import Path
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill

from hairgpu_final_db import FinalStageDatabase

HEADER_FILL = PatternFill(fill_type="solid", fgColor="1F4E78")
HEADER_FONT = Font(color="FFFFFF", bold=True)


def _style_header(row):
    for cell in row:
        cell.fill = HEADER_FILL
        cell.font = HEADER_FONT


def _append_table(ws, rows):
    if not rows:
        return
    headers = list(rows[0].keys())
    ws.append(headers)
    _style_header(ws[1])
    for row in rows:
        ws.append([row[h] for h in headers])
    ws.freeze_panes = "A2"


def _autofit(ws):
    for column in ws.columns:
        width = max(len(str(cell.value)) if cell.value is not None else 0 for cell in column) + 2
        ws.column_dimensions[column[0].column_letter].width = min(max(width, 12), 40)


def export_workbook(db_path: Path, output_xlsx: Path) -> Path:
    db = FinalStageDatabase(db_path)
    wb = Workbook()

    ws_summary = wb.active
    ws_summary.title = "Summary"
    summary = db.fetch_experiment_summary()
    if summary:
        ws_summary.append(["metric", "value"])
        _style_header(ws_summary[1])
        for key, value in summary.items():
            ws_summary.append([key, value])
        ws_summary.freeze_panes = "A2"

    ws_leaderboard = wb.create_sheet("Leaderboard")
    _append_table(ws_leaderboard, db.fetch_method_summary_rows())

    ws_scenarios = wb.create_sheet("ScenarioSummary")
    _append_table(ws_scenarios, db.fetch_scenario_summary_rows())

    ws_runs = wb.create_sheet("Runs")
    run_rows = [dict(row) for row in db.fetch_runs()]
    _append_table(ws_runs, run_rows)

    ws_frames = wb.create_sheet("FrameMetrics")
    frame_rows = [dict(row) for row in db.fetch_frame_metrics()]
    _append_table(ws_frames, frame_rows)

    ws_comp = wb.create_sheet("Comparison")
    comp_rows = [dict(row) for row in db.fetch_comparison_rows()]
    _append_table(ws_comp, comp_rows)

    ws_ab = wb.create_sheet("Ablation")
    ab_rows = [dict(row) for row in db.fetch_ablation_rows()]
    _append_table(ws_ab, ab_rows)

    ws_derived = wb.create_sheet("DerivedMetrics")
    _append_table(ws_derived, db.fetch_derived_metric_rows())

    ws_artifacts = wb.create_sheet("Artifacts")
    _append_table(ws_artifacts, db.fetch_artifact_rows())

    ws_tasks = wb.create_sheet("UserTasks")
    _append_table(ws_tasks, db.fetch_user_task_rows())

    ws_task_summary = wb.create_sheet("UserTaskSummary")
    _append_table(ws_task_summary, db.fetch_user_task_summary_rows())

    ws_hyp = wb.create_sheet("HypothesisChecks")
    _append_table(ws_hyp, db.fetch_hypothesis_rows())

    ws_cov = wb.create_sheet("ScenarioCoverage")
    _append_table(ws_cov, db.fetch_scenario_coverage_rows())

    for ws in wb.worksheets:
        _autofit(ws)

    output_xlsx.parent.mkdir(parents=True, exist_ok=True)
    wb.save(output_xlsx)
    db.close()
    return output_xlsx
