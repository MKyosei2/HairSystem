from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from typing import Any, Dict, Iterable

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


def _json_or_none(value: Any) -> Any:
    if value is None:
        return None
    if isinstance(value, (dict, list, tuple)):
        return json.dumps(value, ensure_ascii=False, sort_keys=True)
    return value


def upsert_metrics_summary(conn: sqlite3.Connection, run_id: int, payload: Dict[str, Any]) -> None:
    normalized = {k: _json_or_none(v) for k, v in payload.items()}
    columns = ["run_id"] + sorted(normalized.keys())
    values = [run_id] + [normalized[key] for key in sorted(normalized.keys())]
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


def insert_teacher_dataset(conn: sqlite3.Connection, payload: Dict[str, Any]) -> int:
    cur = conn.execute(
        """
        INSERT INTO teacher_datasets (
            run_id, dataset_name, scenario_code, frame_count, fps, guide_count, cluster_count,
            csv_path, summary_json_path, created_at, notes
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            payload["run_id"],
            payload["dataset_name"],
            payload["scenario_code"],
            payload.get("frame_count"),
            payload.get("fps"),
            payload.get("guide_count"),
            payload.get("cluster_count"),
            payload.get("csv_path"),
            payload.get("summary_json_path"),
            payload.get("created_at"),
            payload.get("notes"),
        ),
    )
    return int(cur.lastrowid)


def insert_teacher_frame_summary(conn: sqlite3.Connection, teacher_dataset_id: int, rows: Iterable[Dict[str, Any]]) -> None:
    conn.executemany(
        """
        INSERT INTO teacher_frame_summary (
            teacher_dataset_id, frame_index, average_velocity, average_displacement, average_uncertainty,
            average_contact_risk, average_visibility, active_label_ratio, hand_contact_count, chair_contact_count,
            accessory_contact_count, object_contact_count
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        [
            (
                teacher_dataset_id,
                row.get("frame_index"),
                row.get("average_velocity"),
                row.get("average_displacement"),
                row.get("average_uncertainty"),
                row.get("average_contact_risk"),
                row.get("average_visibility"),
                row.get("active_label_ratio"),
                row.get("hand_contact_count"),
                row.get("chair_contact_count"),
                row.get("accessory_contact_count"),
                row.get("object_contact_count"),
            )
            for row in rows
        ],
    )


def insert_learned_model(conn: sqlite3.Connection, payload: Dict[str, Any]) -> int:
    cur = conn.execute(
        """
        INSERT INTO learned_models (
            run_id, model_kind, model_name, feature_schema_json, target_schema_json,
            model_json_path, metrics_json_path, rmse, mae, created_at, notes
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            payload["run_id"],
            payload["model_kind"],
            payload["model_name"],
            _json_or_none(payload.get("feature_schema_json")),
            _json_or_none(payload.get("target_schema_json")),
            payload.get("model_json_path"),
            payload.get("metrics_json_path"),
            payload.get("rmse"),
            payload.get("mae"),
            payload.get("created_at"),
            payload.get("notes"),
        ),
    )
    return int(cur.lastrowid)


def insert_active_set_evaluation(conn: sqlite3.Connection, payload: Dict[str, Any]) -> int:
    cur = conn.execute(
        """
        INSERT INTO active_set_evaluations (
            run_id, teacher_dataset_id, plan_csv_path, summary_json_path, cluster_budget,
            average_selected_cluster_count, average_selected_guide_count, average_selected_score,
            average_budget_utilization, average_contact_risk, average_uncertainty, created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            payload["run_id"],
            payload.get("teacher_dataset_id"),
            payload.get("plan_csv_path"),
            payload.get("summary_json_path"),
            payload.get("cluster_budget"),
            payload.get("average_selected_cluster_count"),
            payload.get("average_selected_guide_count"),
            payload.get("average_selected_score"),
            payload.get("average_budget_utilization"),
            payload.get("average_contact_risk"),
            payload.get("average_uncertainty"),
            payload.get("created_at"),
        ),
    )
    return int(cur.lastrowid)


def insert_corrective_physics_evaluation(conn: sqlite3.Connection, payload: Dict[str, Any]) -> int:
    cur = conn.execute(
        """
        INSERT INTO corrective_physics_evaluations (
            run_id, active_set_eval_id, mode_name, summary_json_path, average_corrected_strand_count,
            penetration_reduction_ratio, silhouette_recovery_ratio, correction_cost_ms, created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            payload["run_id"],
            payload.get("active_set_eval_id"),
            payload["mode_name"],
            payload.get("summary_json_path"),
            payload.get("average_corrected_strand_count"),
            payload.get("penetration_reduction_ratio"),
            payload.get("silhouette_recovery_ratio"),
            payload.get("correction_cost_ms"),
            payload.get("created_at"),
        ),
    )
    return int(cur.lastrowid)


def insert_external_contact_evaluation(conn: sqlite3.Connection, payload: Dict[str, Any]) -> int:
    cur = conn.execute(
        """
        INSERT INTO external_contact_evaluations (
            run_id, active_set_eval_id, summary_json_path, hand_contact_count, chair_contact_count,
            accessory_contact_count, object_contact_count, total_contact_event_count,
            contact_penetration_reduction_ratio, created_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        (
            payload["run_id"],
            payload.get("active_set_eval_id"),
            payload.get("summary_json_path"),
            payload.get("hand_contact_count"),
            payload.get("chair_contact_count"),
            payload.get("accessory_contact_count"),
            payload.get("object_contact_count"),
            payload.get("total_contact_event_count"),
            payload.get("contact_penetration_reduction_ratio"),
            payload.get("created_at"),
        ),
    )
    return int(cur.lastrowid)


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
            ms.penetration_count_per_sec,
            ms.max_penetration_depth_mm,
            ms.silhouette_error,
            ms.clump_preservation_score,
            ms.edit_to_run_latency_ms,
            ms.prior_rmse,
            ms.residual_rmse,
            ms.full_model_rmse,
            ms.uncertainty_mae,
            ase.average_selected_cluster_count,
            ase.average_selected_guide_count,
            ase.average_budget_utilization,
            cpe.penetration_reduction_ratio,
            cpe.silhouette_recovery_ratio,
            cpe.correction_cost_ms,
            ece.total_contact_event_count,
            ece.contact_penetration_reduction_ratio,
            ms.valid,
            ms.validation_warning_count,
            ms.validation_error_count
        FROM runs r
        JOIN experiments e ON e.experiment_id = r.experiment_id
        JOIN methods m ON m.method_id = r.method_id
        JOIN scenarios s ON s.scenario_id = r.scenario_id
        LEFT JOIN metrics_summary ms ON ms.run_id = r.run_id
        LEFT JOIN active_set_evaluations ase ON ase.active_set_eval_id = ms.active_set_eval_id
        LEFT JOIN corrective_physics_evaluations cpe ON cpe.corrective_eval_id = ms.corrective_eval_id
        LEFT JOIN external_contact_evaluations ece ON ece.external_contact_eval_id = ms.external_contact_eval_id
        ORDER BY e.experiment_id DESC, r.run_id DESC
        """
    ).fetchall()


def fetch_ablation_rows(conn: sqlite3.Connection):
    rows = conn.execute(
        """
        SELECT
            e.experiment_name,
            s.scenario_code,
            r.asset_name,
            m.method_code,
            ms.frame_time_p95_ms,
            ms.max_penetration_depth_mm,
            ms.silhouette_error,
            ms.clump_preservation_score,
            ms.prior_rmse,
            ms.residual_rmse,
            cpe.penetration_reduction_ratio,
            ase.average_selected_cluster_count
        FROM runs r
        JOIN experiments e ON e.experiment_id = r.experiment_id
        JOIN methods m ON m.method_id = r.method_id
        JOIN scenarios s ON s.scenario_id = r.scenario_id
        LEFT JOIN metrics_summary ms ON ms.run_id = r.run_id
        LEFT JOIN active_set_evaluations ase ON ase.active_set_eval_id = ms.active_set_eval_id
        LEFT JOIN corrective_physics_evaluations cpe ON cpe.corrective_eval_id = ms.corrective_eval_id
        ORDER BY e.experiment_id, s.scenario_code, r.asset_name, m.method_code
        """
    ).fetchall()
    grouped: dict[tuple[str, str, str], dict[str, sqlite3.Row]] = {}
    for row in rows:
        key = (row["experiment_name"], row["scenario_code"], row["asset_name"])
        grouped.setdefault(key, {})[row["method_code"]] = row

    ablations = []
    for (experiment_name, scenario_code, asset_name), bucket in grouped.items():
        baseline = bucket.get("A")
        for method_code in ["B", "C", "D"]:
            row = bucket.get(method_code)
            if row is None:
                continue
            ablations.append(
                {
                    "experiment_name": experiment_name,
                    "scenario_code": scenario_code,
                    "asset_name": asset_name,
                    "method_code": method_code,
                    "delta_p95_ms_vs_A": (row["frame_time_p95_ms"] - baseline["frame_time_p95_ms"]) if baseline and row["frame_time_p95_ms"] is not None and baseline["frame_time_p95_ms"] is not None else None,
                    "delta_penetration_mm_vs_A": (row["max_penetration_depth_mm"] - baseline["max_penetration_depth_mm"]) if baseline and row["max_penetration_depth_mm"] is not None and baseline["max_penetration_depth_mm"] is not None else None,
                    "delta_silhouette_error_vs_A": (row["silhouette_error"] - baseline["silhouette_error"]) if baseline and row["silhouette_error"] is not None and baseline["silhouette_error"] is not None else None,
                    "delta_clump_score_vs_A": (row["clump_preservation_score"] - baseline["clump_preservation_score"]) if baseline and row["clump_preservation_score"] is not None and baseline["clump_preservation_score"] is not None else None,
                    "prior_rmse": row["prior_rmse"],
                    "residual_rmse": row["residual_rmse"],
                    "penetration_reduction_ratio": row["penetration_reduction_ratio"],
                    "average_selected_cluster_count": row["average_selected_cluster_count"],
                }
            )
    return ablations
