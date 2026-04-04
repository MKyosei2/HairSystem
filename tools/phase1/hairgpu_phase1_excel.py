from __future__ import annotations

import sqlite3
from pathlib import Path

import openpyxl
from openpyxl.styles import Font

from hairgpu_phase1_db import fetch_comparison_rows


def _write_sheet(workbook: openpyxl.Workbook, name: str, headers, rows) -> None:
    ws = workbook.create_sheet(title=name)
    ws.append(list(headers))
    for cell in ws[1]:
        cell.font = Font(bold=True)
    for row in rows:
        if isinstance(row, sqlite3.Row):
            ws.append([row[h] for h in headers])
        else:
            ws.append(list(row))
    ws.freeze_panes = "A2"
    for col in ws.columns:
        width = max(len(str(c.value)) if c.value is not None else 0 for c in col)
        ws.column_dimensions[col[0].column_letter].width = min(max(width + 2, 12), 40)


def export_workbook(conn: sqlite3.Connection, output_path: Path) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    wb = openpyxl.Workbook()
    default = wb.active
    wb.remove(default)

    for table_name in ["experiments", "scenarios", "methods", "runs", "metrics_summary", "metrics_frame"]:
        rows = conn.execute(f"SELECT * FROM {table_name}").fetchall()
        headers = rows[0].keys() if rows else []
        _write_sheet(wb, table_name, headers, rows)

    comparison_rows = fetch_comparison_rows(conn)
    comparison_headers = comparison_rows[0].keys() if comparison_rows else [
        "experiment_name", "method_code", "method_name", "scenario_code", "scenario_name", "asset_name",
        "runtime_mode", "frame_count", "target_fps", "frame_time_p95_ms", "worst_frame_time_ms", "peak_ram_mb",
        "average_active_cluster_count", "max_active_cluster_count", "penetration_count_per_sec",
        "max_penetration_depth_mm", "silhouette_error", "clump_preservation_score",
        "edit_to_run_latency_ms", "valid", "validation_warning_count", "validation_error_count",
    ]
    _write_sheet(wb, "comparison", comparison_headers, comparison_rows)

    wb.save(output_path)
    return output_path
