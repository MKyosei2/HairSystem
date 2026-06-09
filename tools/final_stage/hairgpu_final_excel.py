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
        getter = row.get if hasattr(row, "get") else row.__getitem__
        ws.append([getter(h) for h in headers])
    ws.freeze_panes = "A2"


def _append_check_rows(ws, rows):
    ws.append(["check", "status", "notes"])
    _style_header(ws[1])
    for check, status, notes in rows:
        ws.append([check, status, notes])
    ws.freeze_panes = "A2"


def _autofit(ws):
    for column in ws.columns:
        width = max(len(str(cell.value)) if cell.value is not None else 0 for cell in column) + 2
        ws.column_dimensions[column[0].column_letter].width = min(max(width, 12), 48)


def _row_dicts(rows):
    return [dict(row) for row in rows]


def _columns(rows):
    return set(rows[0].keys()) if rows else set()


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

    method_rows = db.fetch_method_summary_rows()
    scenario_rows = db.fetch_scenario_summary_rows()
    run_rows = _row_dicts(db.fetch_runs())
    frame_rows = _row_dicts(db.fetch_frame_metrics())

    ws_leaderboard = wb.create_sheet("Leaderboard")
    _append_table(ws_leaderboard, method_rows)

    ws_scenarios = wb.create_sheet("ScenarioSummary")
    _append_table(ws_scenarios, scenario_rows)

    ws_runs = wb.create_sheet("Runs")
    _append_table(ws_runs, run_rows)

    ws_frames = wb.create_sheet("FrameMetrics")
    _append_table(ws_frames, frame_rows)

    ws_comp = wb.create_sheet("Comparison")
    _append_table(ws_comp, _row_dicts(db.fetch_comparison_rows()))

    ws_ab = wb.create_sheet("Ablation")
    _append_table(ws_ab, _row_dicts(db.fetch_ablation_rows()))

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

    run_cols = _columns(run_rows)
    frame_cols = _columns(frame_rows)

    ws_baseline = wb.create_sheet("Baseline_Check")
    _append_check_rows(ws_baseline, [
        ("Baseline_CurrentProject preserved", "PASS" if any(r.get("method_code") == "Baseline_CurrentProject" for r in run_rows) else "INFO", "PASS after importing a baseline run."),
        ("Combo_00000 present", "PASS" if any(r.get("method_code") == "Combo_00000" for r in run_rows) else "INFO", "Required Phase1 smoke case."),
        ("Combo_11111 present", "PASS" if any(r.get("method_code") == "Combo_11111" for r in run_rows) else "INFO", "Required Phase1 smoke case."),
        ("Legacy exp_650 flags preserved", "PASS" if {"enable_cbma", "enable_physics_islands", "enable_rod_repair"}.issubset(run_cols) else "WARN", "Existing exp_650 columns should remain available."),
    ])

    ws_flags = wb.create_sheet("FeatureFlag_Check")
    expected_run_flags = [
        "enable_microtype",
        "enable_budgeted_physics",
        "enable_certificate_render",
        "enable_per_strand_edit",
        "enable_gpu_resident",
        "top_level_novelty_mask",
        "phase",
        "novelty_mapping_version",
    ]
    _append_table(ws_flags, [
        {"column": name, "present": name in run_cols, "scope": "runtime_runs"}
        for name in expected_run_flags
    ])

    ws_telemetry = wb.create_sheet("TelemetrySchema_Check")
    expected_frame_cols = [
        "top_level_novelty_mask", "overflow_count", "budget_drop_count", "defer_count",
        "microtype_decoded_count", "safe_decode_count", "false_safe_count",
        "typed_repair_request_count", "float_fallback_request_count", "reject_payload_count",
        "risky_strand_count", "island_candidate_count", "physics_fallback_count",
        "render_certificate_count", "render_skip_count", "render_coarse_opacity_count",
        "render_strand_raster_count", "render_high_quality_shadow_count",
        "render_high_quality_specular_count", "render_full_quality_count",
        "render_dropped_full_quality_count", "p95_shadow_error_bound",
        "p95_specular_error_bound", "temporal_flicker_score",
        "edit_operation_count", "edit_repair_task_count", "edit_island_escalation_count",
        "edit_commit_count", "edit_reject_count", "edit_preview_full_quality_count",
        "edit_latency_ms", "gpu_resident_task_count", "risky_compaction_count",
        "dispatch_indirect_count", "draw_indirect_count", "delayed_telemetry_read_count",
    ]
    _append_table(ws_telemetry, [
        {"column": name, "present": name in frame_cols, "scope": "runtime_frame_metrics"}
        for name in expected_frame_cols
    ])

    ws_final5 = wb.create_sheet("Final5_Readiness")
    _append_check_rows(ws_final5, [
        ("01 Microtype flag", "PASS" if "enable_microtype" in run_cols else "WARN", "Required final5 axis 01."),
        ("02 Budgeted physics flag", "PASS" if "enable_budgeted_physics" in run_cols else "WARN", "Required final5 axis 02."),
        ("03 Certificate render flag", "PASS" if "enable_certificate_render" in run_cols else "WARN", "Required final5 axis 03."),
        ("04 Per-strand edit flag", "PASS" if "enable_per_strand_edit" in run_cols else "WARN", "Required final5 axis 04."),
        ("05 GPU resident flag", "PASS" if "enable_gpu_resident" in run_cols else "WARN", "Required final5 axis 05."),
        ("Top-level mask", "PASS" if "top_level_novelty_mask" in run_cols else "WARN", "Required for final5 comparisons."),
        ("Telemetry placeholders", "PASS" if all(name in frame_cols for name in expected_frame_cols) else "WARN", "All placeholder columns should exist for Phase2-6."),
    ])

    for ws in wb.worksheets:
        _autofit(ws)

    output_xlsx.parent.mkdir(parents=True, exist_ok=True)
    wb.save(output_xlsx)
    db.close()
    return output_xlsx
