from __future__ import annotations

import csv
import json
import sqlite3
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable


@dataclass
class RuntimeRunMetadata:
    experiment_name: str
    method_code: str
    scenario_code: str
    asset_name: str
    run_name: str
    runtime_source: str = "dx11_viewer"
    operator_name: str = ""
    notes: str = ""


class FinalStageDatabase:
    def __init__(self, db_path: Path):
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(str(self.db_path))
        self.connection.row_factory = sqlite3.Row

    def close(self) -> None:
        self.connection.close()

    def initialize(self, schema_path: Path) -> None:
        sql = Path(schema_path).read_text(encoding="utf-8")
        self.connection.executescript(sql)
        self.connection.commit()

    def upsert_experiment(self, experiment_name: str, operator_name: str, notes: str) -> int:
        row = self.connection.execute(
            "SELECT experiment_id FROM experiments WHERE experiment_name = ? ORDER BY experiment_id DESC LIMIT 1",
            (experiment_name,),
        ).fetchone()
        if row:
            return int(row["experiment_id"])
        cursor = self.connection.execute(
            "INSERT INTO experiments (experiment_name, created_at, operator_name, notes) VALUES (?, ?, ?, ?)",
            (experiment_name, datetime.utcnow().isoformat(timespec="seconds"), operator_name, notes),
        )
        self.connection.commit()
        return int(cursor.lastrowid)

    @staticmethod
    def _read_summary(summary_json_path: Path) -> dict:
        return json.loads(Path(summary_json_path).read_text(encoding="utf-8"))

    @staticmethod
    def _resolve_summary_value(summary: dict, *keys: str, default=0.0):
        for key in keys:
            if key in summary and summary[key] is not None:
                return summary[key]
        return default

    @staticmethod
    def _pick_csv_value(row: dict, *keys: str, default: str = "0") -> str:
        for key in keys:
            if key in row and row[key] not in (None, ""):
                return row[key]
        return default

    def import_runtime_run(
        self,
        metadata: RuntimeRunMetadata,
        summary_json_path: Path,
        frame_csv_path: Path,
    ) -> int:
        summary = self._read_summary(summary_json_path)
        experiment_id = self.upsert_experiment(metadata.experiment_name, metadata.operator_name, metadata.notes)
        cursor = self.connection.execute(
            """
            INSERT INTO runtime_runs (
                experiment_id, run_name, method_code, scenario_code, asset_name, runtime_source,
                summary_json_path, frame_csv_path, sample_count, avg_frame_ms, p95_frame_ms,
                worst_frame_ms, avg_simulation_ms, avg_fps, sixty_fps_ratio, guide_count,
                follower_count, total_strand_count, total_particle_count, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                experiment_id,
                metadata.run_name,
                metadata.method_code,
                metadata.scenario_code,
                metadata.asset_name,
                metadata.runtime_source,
                str(summary_json_path),
                str(frame_csv_path),
                int(self._resolve_summary_value(summary, "sample_count", "sampleCount", default=0)),
                float(self._resolve_summary_value(summary, "avg_frame_ms", "avgFrameMs", default=0.0)),
                float(self._resolve_summary_value(summary, "p95_frame_ms", "p95FrameMs", default=0.0)),
                float(self._resolve_summary_value(summary, "worst_frame_ms", "worstFrameMs", default=0.0)),
                float(self._resolve_summary_value(summary, "avg_simulation_ms", "avgSimulationMs", default=0.0)),
                float(self._resolve_summary_value(summary, "avg_fps", "avgFps", default=0.0)),
                float(self._resolve_summary_value(summary, "sixty_fps_ratio", "sixtyFpsRatio", default=0.0)),
                int(self._resolve_summary_value(summary, "guide_count", "guideCount", default=0)),
                int(self._resolve_summary_value(summary, "follower_count", "followerCount", default=0)),
                int(self._resolve_summary_value(summary, "total_strand_count", "totalStrandCount", default=0)),
                int(self._resolve_summary_value(summary, "total_particle_count", "totalParticleCount", default=0)),
                datetime.utcnow().isoformat(timespec="seconds"),
            ),
        )
        run_id = int(cursor.lastrowid)

        with Path(frame_csv_path).open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            rows = [
                (
                    run_id,
                    int(self._pick_csv_value(row, "sampleIndex", "sample_index", default="0")),
                    float(self._pick_csv_value(row, "elapsedSeconds", "elapsed_seconds", default="0")),
                    float(self._pick_csv_value(row, "frameMilliseconds", "frame_ms", default="0")),
                    float(self._pick_csv_value(row, "simulationMilliseconds", "simulation_ms", default="0")),
                    float(self._pick_csv_value(row, "fps", default="0")),
                    int(self._pick_csv_value(row, "guideCount", "guide_count", default="0")),
                    int(self._pick_csv_value(row, "followerCount", "follower_count", default="0")),
                    int(self._pick_csv_value(row, "totalStrandCount", "total_strand_count", default="0")),
                    int(self._pick_csv_value(row, "totalParticleCount", "total_particle_count", default="0")),
                )
                for row in reader
            ]
        self.connection.executemany(
            """
            INSERT INTO runtime_frame_metrics (
                run_id, sample_index, elapsed_seconds, frame_ms, simulation_ms, fps,
                guide_count, follower_count, total_strand_count, total_particle_count
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            rows,
        )
        self.connection.commit()
        return run_id

    @staticmethod
    def _rows_to_dicts(rows: Iterable[sqlite3.Row]):
        return [dict(row) for row in rows]

    def fetch_runs(self):
        return self.connection.execute(
            "SELECT * FROM runtime_runs ORDER BY created_at, run_id"
        ).fetchall()

    def fetch_frame_metrics(self):
        return self.connection.execute(
            "SELECT * FROM runtime_frame_metrics ORDER BY run_id, sample_index"
        ).fetchall()

    def fetch_comparison_rows(self):
        return self.connection.execute(
            """
            SELECT
                method_code,
                scenario_code,
                COUNT(*) AS run_count,
                AVG(avg_frame_ms) AS avg_frame_ms,
                AVG(p95_frame_ms) AS avg_p95_frame_ms,
                AVG(worst_frame_ms) AS avg_worst_frame_ms,
                AVG(avg_fps) AS avg_fps,
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio
            FROM runtime_runs
            GROUP BY method_code, scenario_code
            ORDER BY method_code, scenario_code
            """
        ).fetchall()

    def fetch_ablation_rows(self):
        return self.connection.execute(
            """
            SELECT
                method_code,
                COUNT(*) AS run_count,
                AVG(avg_frame_ms) AS avg_frame_ms,
                AVG(p95_frame_ms) AS avg_p95_frame_ms,
                AVG(avg_fps) AS avg_fps,
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio
            FROM runtime_runs
            GROUP BY method_code
            ORDER BY method_code
            """
        ).fetchall()

    def fetch_method_summary_rows(self):
        rows = self.connection.execute(
            """
            SELECT
                method_code,
                COUNT(*) AS run_count,
                AVG(avg_frame_ms) AS avg_frame_ms,
                AVG(p95_frame_ms) AS avg_p95_frame_ms,
                AVG(worst_frame_ms) AS avg_worst_frame_ms,
                AVG(avg_fps) AS avg_fps,
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio,
                AVG(avg_simulation_ms) AS avg_simulation_ms
            FROM runtime_runs
            GROUP BY method_code
            ORDER BY avg_sixty_fps_ratio DESC, avg_p95_frame_ms ASC, method_code ASC
            """
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_scenario_summary_rows(self):
        rows = self.connection.execute(
            """
            SELECT
                scenario_code,
                COUNT(*) AS run_count,
                AVG(avg_frame_ms) AS avg_frame_ms,
                AVG(p95_frame_ms) AS avg_p95_frame_ms,
                AVG(worst_frame_ms) AS avg_worst_frame_ms,
                AVG(avg_fps) AS avg_fps,
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio
            FROM runtime_runs
            GROUP BY scenario_code
            ORDER BY scenario_code ASC
            """
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_experiment_summary(self) -> dict:
        row = self.connection.execute(
            """
            SELECT
                COUNT(*) AS run_count,
                COUNT(DISTINCT method_code) AS method_count,
                COUNT(DISTINCT scenario_code) AS scenario_count,
                MIN(created_at) AS first_created_at,
                MAX(created_at) AS last_created_at,
                AVG(avg_frame_ms) AS avg_frame_ms,
                AVG(p95_frame_ms) AS avg_p95_frame_ms,
                AVG(worst_frame_ms) AS avg_worst_frame_ms,
                AVG(avg_fps) AS avg_fps,
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio
            FROM runtime_runs
            """
        ).fetchone()
        return dict(row) if row else {}
