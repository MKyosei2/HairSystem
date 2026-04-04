from __future__ import annotations

import csv
import json
import sqlite3
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Optional


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

    def import_runtime_run(
        self,
        metadata: RuntimeRunMetadata,
        summary_json_path: Path,
        frame_csv_path: Path,
    ) -> int:
        summary = json.loads(Path(summary_json_path).read_text(encoding="utf-8"))
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
                int(summary.get("sample_count", 0)),
                float(summary.get("avg_frame_ms", 0.0)),
                float(summary.get("p95_frame_ms", 0.0)),
                float(summary.get("worst_frame_ms", 0.0)),
                float(summary.get("avg_simulation_ms", 0.0)),
                float(summary.get("avg_fps", 0.0)),
                float(summary.get("sixty_fps_ratio", 0.0)),
                int(summary.get("guide_count", 0)),
                int(summary.get("follower_count", 0)),
                int(summary.get("total_strand_count", 0)),
                int(summary.get("total_particle_count", 0)),
                datetime.utcnow().isoformat(timespec="seconds"),
            ),
        )
        run_id = int(cursor.lastrowid)

        with Path(frame_csv_path).open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            rows = [
                (
                    run_id,
                    int(row["sampleIndex"]),
                    float(row["elapsedSeconds"]),
                    float(row["frameMilliseconds"]),
                    float(row["simulationMilliseconds"]),
                    float(row["fps"]),
                    int(row["guideCount"]),
                    int(row["followerCount"]),
                    int(row["totalStrandCount"]),
                    int(row["totalParticleCount"]),
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
