from __future__ import annotations

import json
import sqlite3
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, Optional

SCENARIOS = [
    ("S1", "Normal Motion", "Baseline motion without severe contact"),
    ("S2", "High Acceleration", "Abrupt motion stressing temporal stability"),
    ("S3", "Contact", "Body/object contact and penetration checks"),
    ("S4", "Editing", "Interactive edits and edit-to-run latency"),
]

METHODS = [
    ("A", "Pure Physics"),
    ("B", "Prior + Residual Only"),
    ("C", "Active-Set Physics Only"),
    ("D", "Full Hybrid"),
]


def connect(db_path: Path) -> sqlite3.Connection:
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA foreign_keys = ON")
    return conn


def initialize_database(db_path: Path) -> None:
    db_path.parent.mkdir(parents=True, exist_ok=True)
    schema_path = Path(__file__).with_name("schema.sql")
    conn = connect(db_path)
    try:
        conn.executescript(schema_path.read_text(encoding="utf-8"))
        conn.executemany(
            "INSERT OR IGNORE INTO scenarios (scenario_code, scenario_name, description) VALUES (?, ?, ?)",
            SCENARIOS,
        )
        conn.executemany(
            "INSERT OR IGNORE INTO methods (method_code, method_name) VALUES (?, ?)",
            METHODS,
        )
        conn.commit()
    finally:
        conn.close()


def _lookup_id(conn: sqlite3.Connection, table: str, code_field: str, code: str) -> int:
    row = conn.execute(f"SELECT {table[:-1]}_id AS id FROM {table} WHERE {code_field} = ?", (code,)).fetchone()
    if row is None:
        raise KeyError(f"Unknown {table} code: {code}")
    return int(row["id"])


def insert_experiment(conn: sqlite3.Connection, payload: Dict[str, Any]) -> int:
    cur = conn.execute(
        """
        INSERT INTO experiments (experiment_name, created_at, operator_name, git_commit, build_type, notes)
        VALUES (?, ?, ?, ?, ?, ?)
        """,
        (
            payload["experiment_name"],
            payload["created_at"],
            payload.get("operator_name"),
            payload.get("git_commit"),
            payload.get("build_type"),
            payload.get("notes"),
        ),
    )
    return int(cur.lastrowid)


def insert_run(conn: sqlite3.Connection, payload: Dict[str, Any]) -> int:
    scenario_id = _lookup_id(conn, "scenarios", "scenario_code", payload["scenario_code"])
    method_id = _lookup_id(conn, "methods", "method_code", payload["method_code"])
    cur = conn.execute(
        """
        INSERT INTO runs (
            experiment_id, scenario_id, method_id, asset_name, asset_path, camera_setup, hardware_profile,
            status, started_at, ended_at, config_json, cli_stdout, cli_stderr, notes
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            payload["experiment_id"],
            scenario_id,
            method_id,
            payload.get("asset_name"),
            payload.get("asset_path"),
            payload.get("camera_setup"),
            payload.get("hardware_profile"),
            payload.get("status"),
            payload.get("started_at"),
            payload.get("ended_at"),
            json.dumps(payload.get("config", {}), ensure_ascii=False, sort_keys=True),
            payload.get("cli_stdout"),
            payload.get("cli_stderr"),
            payload.get("notes"),
        ),
    )
    return int(cur.lastrowid)


def upsert_metrics_summary(conn: sqlite3.Connection, run_id: int, payload: Dict[str, Any]) -> None:
    columns = ["run_id"] + sorted(payload.keys())
    values = [run_id] + [payload[key] for key in sorted(payload.keys())]
    placeholders = ", ".join(["?"] * len(columns))
    updates = ", ".join([f"{col}=excluded.{col}" for col in columns if col != "run_id"])
    conn.execute(
        f"INSERT INTO metrics_summary ({', '.join(columns)}) VALUES ({placeholders}) ON CONFLICT(run_id) DO UPDATE SET {updates}",
        values,
    )


def insert_frame_metrics(conn: sqlite3.Connection, run_id: int, rows: Iterable[Dict[str, Any]]) -> None:
    prepared = [
        (
            run_id,
            row.get("frame_index"),
            row.get("frame_time_ms"),
            row.get("active_cluster_count"),
            row.get("penetration_count"),
            row.get("max_penetration_depth_mm"),
            row.get("visible_hair_ratio"),
            row.get("gpu_memory_mb"),
            row.get("cpu_memory_mb"),
        )
        for row in rows
    ]
    conn.executemany(
        """
        INSERT INTO metrics_frame (
            run_id, frame_index, frame_time_ms, active_cluster_count, penetration_count,
            max_penetration_depth_mm, visible_hair_ratio, gpu_memory_mb, cpu_memory_mb
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        prepared,
    )


def fetch_table(conn: sqlite3.Connection, table_name: str):
    return conn.execute(f"SELECT * FROM {table_name}").fetchall()


def fetch_comparison_rows(conn: sqlite3.Connection):
    return conn.execute(
        """
        SELECT
            e.experiment_name,
            m.method_code,
            m.method_name,
            s.scenario_code,
            s.scenario_name,
            r.asset_name,
            ms.runtime_mode,
            ms.frame_count,
            ms.target_fps,
            ms.frame_time_p95_ms,
            ms.worst_frame_time_ms,
            ms.peak_ram_mb,
            ms.average_active_cluster_count,
            ms.max_active_cluster_count,
            ms.penetration_count_per_sec,
            ms.max_penetration_depth_mm,
            ms.silhouette_error,
            ms.clump_preservation_score,
            ms.edit_to_run_latency_ms,
            ms.valid,
            ms.validation_warning_count,
            ms.validation_error_count
        FROM runs r
        JOIN experiments e ON e.experiment_id = r.experiment_id
        JOIN methods m ON m.method_id = r.method_id
        JOIN scenarios s ON s.scenario_id = r.scenario_id
        LEFT JOIN metrics_summary ms ON ms.run_id = r.run_id
        ORDER BY e.experiment_id DESC, r.run_id DESC
        """
    ).fetchall()
