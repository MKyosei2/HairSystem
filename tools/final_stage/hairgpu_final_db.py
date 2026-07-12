from __future__ import annotations

import csv
import hashlib
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
    case_id: str = ""
    variant_code: str = ""
    seed: int = 0
    warmup_frames: int = 0
    measurement_frames: int = 0
    enable_no_novelty_baseline: bool = False
    enable_microtype: bool = False
    enable_budgeted_physics: bool = False
    enable_certificate_render: bool = False
    enable_per_strand_edit: bool = False
    top_level_novelty_mask: int = 0
    phase: str = "phase1_pre_novelty_final_adjustment"
    novelty_mapping_version: str = "final5_v1"
    enable_cbma: bool = False
    enable_physics_islands: bool = False
    enable_rod_repair: bool = False
    enable_gpu_resident: bool = False
    frame_jsonl_path: str = ""
    settings_json_path: str = ""
    error_json_path: str = ""


class FinalStageDatabase:
    RUNTIME_RUN_EXTRA_COLUMNS = {
        "frame_jsonl_path": "TEXT", "settings_json_path": "TEXT", "error_json_path": "TEXT",
        "case_id": "TEXT", "variant_code": "TEXT", "seed": "INTEGER DEFAULT 0",
        "settings_hash": "TEXT", "warmup_frames": "INTEGER DEFAULT 0", "measurement_frames": "INTEGER DEFAULT 0",
        "enable_no_novelty_baseline": "INTEGER DEFAULT 0", "enable_microtype": "INTEGER DEFAULT 0",
        "enable_budgeted_physics": "INTEGER DEFAULT 0", "enable_certificate_render": "INTEGER DEFAULT 0",
        "enable_per_strand_edit": "INTEGER DEFAULT 0", "top_level_novelty_mask": "INTEGER DEFAULT 0",
        "phase": "TEXT", "novelty_mapping_version": "TEXT", "enable_cbma": "INTEGER DEFAULT 0",
        "enable_physics_islands": "INTEGER DEFAULT 0", "enable_rod_repair": "INTEGER DEFAULT 0",
        "enable_gpu_resident": "INTEGER DEFAULT 0", "p50_frame_ms": "REAL", "p99_frame_ms": "REAL",
        "p50_simulation_ms": "REAL", "p95_simulation_ms": "REAL", "p99_simulation_ms": "REAL",
        "avg_gpu_hair_ms": "REAL", "p95_gpu_hair_ms": "REAL", "p99_gpu_hair_ms": "REAL",
        "vram_mb": "REAL", "bandwidth_mb_frame": "REAL", "certificate_failure_rate": "REAL",
        "fallback_rate": "REAL", "repair_success_rate": "REAL", "cpu_readback_count": "INTEGER DEFAULT 0",
        "false_static_count": "INTEGER DEFAULT 0", "false_static_rate": "REAL DEFAULT 0",
        "group_static_reuse_rate": "REAL DEFAULT 0", "group_dynamic_rate": "REAL DEFAULT 0",
        "deferred_correction_rate": "REAL DEFAULT 0", "avg_static_dynamic_boundary_error_mm": "REAL DEFAULT 0",
        "max_static_dynamic_boundary_error_mm": "REAL DEFAULT 0", "cache_hit_count": "INTEGER DEFAULT 0",
        "cache_miss_count": "INTEGER DEFAULT 0", "cache_invalidation_count": "INTEGER DEFAULT 0",
        "watchdog_check_count": "INTEGER DEFAULT 0", "watchdog_repair_count": "INTEGER DEFAULT 0",
        "direction_cache_memory_mb": "REAL DEFAULT 0", "full_position_memory_mb": "REAL DEFAULT 0",
        "direction_cache_memory_saving_rate": "REAL DEFAULT 0",
    }

    RUNTIME_FRAME_EXTRA_COLUMNS = {
        "global_frame_index": "INTEGER", "measured_frame_index": "INTEGER",
        "top_level_novelty_mask": "INTEGER DEFAULT 0", "overflow_count": "INTEGER DEFAULT 0",
        "budget_drop_count": "INTEGER DEFAULT 0", "defer_count": "INTEGER DEFAULT 0",
        "microtype_decoded_count": "INTEGER DEFAULT 0", "safe_decode_count": "INTEGER DEFAULT 0",
        "false_safe_count": "INTEGER DEFAULT 0", "typed_repair_request_count": "INTEGER DEFAULT 0",
        "float_fallback_request_count": "INTEGER DEFAULT 0", "reject_payload_count": "INTEGER DEFAULT 0",
        "risky_strand_count": "INTEGER DEFAULT 0", "repair_commit_count": "INTEGER DEFAULT 0",
        "repair_reject_count": "INTEGER DEFAULT 0", "repair_defer_count": "INTEGER DEFAULT 0",
        "island_candidate_count": "INTEGER DEFAULT 0", "physics_fallback_count": "INTEGER DEFAULT 0",
        "render_certificate_count": "INTEGER DEFAULT 0", "render_skip_count": "INTEGER DEFAULT 0",
        "render_coarse_opacity_count": "INTEGER DEFAULT 0", "render_strand_raster_count": "INTEGER DEFAULT 0",
        "render_high_quality_shadow_count": "INTEGER DEFAULT 0", "render_high_quality_specular_count": "INTEGER DEFAULT 0",
        "render_full_quality_count": "INTEGER DEFAULT 0", "render_dropped_full_quality_count": "INTEGER DEFAULT 0",
        "p95_shadow_error_bound": "REAL DEFAULT 0", "p95_specular_error_bound": "REAL DEFAULT 0",
        "temporal_flicker_score": "REAL DEFAULT 0", "edit_operation_count": "INTEGER DEFAULT 0",
        "edit_repair_task_count": "INTEGER DEFAULT 0", "edit_island_escalation_count": "INTEGER DEFAULT 0",
        "edit_commit_count": "INTEGER DEFAULT 0", "edit_reject_count": "INTEGER DEFAULT 0",
        "edit_preview_full_quality_count": "INTEGER DEFAULT 0", "edit_latency_ms": "REAL DEFAULT 0",
        "gpu_resident_task_count": "INTEGER DEFAULT 0", "risky_compaction_count": "INTEGER DEFAULT 0",
        "dispatch_indirect_count": "INTEGER DEFAULT 0", "draw_indirect_count": "INTEGER DEFAULT 0",
        "delayed_telemetry_read_count": "INTEGER DEFAULT 0", "certificate_failure_count": "INTEGER DEFAULT 0",
        "certificate_failure_rate": "REAL DEFAULT 0", "fallback_count": "INTEGER DEFAULT 0",
        "mode_skip_count": "INTEGER DEFAULT 0", "mode_qs_count": "INTEGER DEFAULT 0",
        "mode_dynamic_count": "INTEGER DEFAULT 0", "mode_island_count": "INTEGER DEFAULT 0",
        "mode_repair_count": "INTEGER DEFAULT 0", "mode_float_fallback_count": "INTEGER DEFAULT 0",
        "island_count": "INTEGER DEFAULT 0", "island_member_count": "INTEGER DEFAULT 0",
        "island_commit_count": "INTEGER DEFAULT 0", "island_reject_count": "INTEGER DEFAULT 0",
        "repair_task_count": "INTEGER DEFAULT 0", "repair_committed_count": "INTEGER DEFAULT 0",
        "repair_rejected_count": "INTEGER DEFAULT 0", "repair_deferred_count": "INTEGER DEFAULT 0",
        "repair_success_rate": "REAL DEFAULT 0", "cpu_readback_count": "INTEGER DEFAULT 0",
        "gpu_hair_ms": "REAL DEFAULT 0", "vram_mb": "REAL DEFAULT 0",
        "bandwidth_mb_frame": "REAL DEFAULT 0", "mean_decode_error_mm": "REAL DEFAULT 0",
        "max_penetration_mm": "REAL DEFAULT 0",
        "total_segment_group_count": "INTEGER DEFAULT 0", "static_cached_group_count": "INTEGER DEFAULT 0",
        "static_transformed_group_count": "INTEGER DEFAULT 0", "dynamic_group_count": "INTEGER DEFAULT 0",
        "deferred_correction_group_count": "INTEGER DEFAULT 0", "repair_required_group_count": "INTEGER DEFAULT 0",
        "fallback_group_count": "INTEGER DEFAULT 0", "false_static_count": "INTEGER DEFAULT 0",
        "false_static_rate": "REAL DEFAULT 0", "cache_hit_count": "INTEGER DEFAULT 0",
        "cache_miss_count": "INTEGER DEFAULT 0", "cache_invalidation_count": "INTEGER DEFAULT 0",
        "watchdog_check_count": "INTEGER DEFAULT 0", "watchdog_repair_count": "INTEGER DEFAULT 0",
        "static_dynamic_boundary_mean_error_mm": "REAL DEFAULT 0",
        "static_dynamic_boundary_max_error_mm": "REAL DEFAULT 0",
        "direction_cache_memory_mb": "REAL DEFAULT 0", "full_position_memory_mb": "REAL DEFAULT 0",
        "direction_cache_memory_saving_rate": "REAL DEFAULT 0",
    }

    def __init__(self, db_path: Path):
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(str(self.db_path))
        self.connection.row_factory = sqlite3.Row

    def close(self) -> None:
        self.connection.close()

    def initialize(self, schema_path: Path) -> None:
        self.connection.executescript(Path(schema_path).read_text(encoding="utf-8"))
        self._ensure_runtime_schema_columns()
        self.connection.commit()

    def _ensure_columns(self, table: str, columns: dict[str, str]) -> None:
        existing = {row["name"] for row in self.connection.execute(f"PRAGMA table_info({table})").fetchall()}
        for name, ddl in columns.items():
            if name not in existing:
                self.connection.execute(f"ALTER TABLE {table} ADD COLUMN {name} {ddl}")

    def _ensure_runtime_schema_columns(self) -> None:
        self._ensure_columns("runtime_runs", self.RUNTIME_RUN_EXTRA_COLUMNS)
        self._ensure_columns("runtime_frame_metrics", self.RUNTIME_FRAME_EXTRA_COLUMNS)

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

    @classmethod
    def _csv_int(cls, row: dict, *keys: str, default: str = "0") -> int:
        return int(float(cls._pick_csv_value(row, *keys, default=default) or 0))

    @classmethod
    def _csv_float(cls, row: dict, *keys: str, default: str = "0") -> float:
        return float(cls._pick_csv_value(row, *keys, default=default) or 0.0)

    @staticmethod
    def file_sha256(path: Path) -> str:
        digest = hashlib.sha256()
        with Path(path).open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

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

    def add_artifact(self, run_id: int, category: str, label: str, artifact_path: Path) -> None:
        artifact_path = Path(artifact_path)
        sha256 = self.file_sha256(artifact_path) if artifact_path.exists() and artifact_path.is_file() else None
        size_bytes = artifact_path.stat().st_size if artifact_path.exists() and artifact_path.is_file() else None
        self.connection.execute(
            """
            INSERT INTO run_artifacts (run_id, category, label, artifact_path, sha256, size_bytes, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (run_id, category, label, str(artifact_path), sha256, int(size_bytes) if size_bytes is not None else None, datetime.utcnow().isoformat(timespec="seconds")),
        )
        self.connection.commit()

    def add_derived_metric(self, scope_type: str, scope_key: str, metric_name: str, metric_value: float | int | None, *, run_id: int | None = None, units: str = "", notes: str = "") -> None:
        self.connection.execute(
            """
            INSERT INTO derived_metrics (run_id, scope_type, scope_key, metric_name, metric_value, units, notes, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (run_id, scope_type, scope_key, metric_name, float(metric_value) if metric_value is not None else None, units, notes, datetime.utcnow().isoformat(timespec="seconds")),
        )
        self.connection.commit()

    def add_user_task_result(self, experiment_name: str, task_id: str, title: str, method_code: str, scenario_code: str = "", asset_name: str = "", participant_id: str = "", completion_seconds: float | None = None, redo_count: int | None = None, unintended_change_score: float | None = None, satisfaction_score: float | None = None, notes: str = "") -> None:
        experiment_id = self.upsert_experiment(experiment_name, "", notes)
        self.connection.execute(
            """
            INSERT INTO user_task_results (
                experiment_id, task_id, title, method_code, scenario_code, asset_name,
                participant_id, completion_seconds, redo_count, unintended_change_score,
                satisfaction_score, notes, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (experiment_id, task_id, title, method_code, scenario_code, asset_name, participant_id, float(completion_seconds) if completion_seconds is not None else None, int(redo_count) if redo_count is not None else None, float(unintended_change_score) if unintended_change_score is not None else None, float(satisfaction_score) if satisfaction_score is not None else None, notes, datetime.utcnow().isoformat(timespec="seconds")),
        )
        self.connection.commit()

    def clear_suite_rows(self, suite_name: str) -> None:
        self.connection.execute("DELETE FROM hypothesis_checks WHERE suite_name = ?", (suite_name,))
        self.connection.execute("DELETE FROM scenario_coverage WHERE suite_name = ?", (suite_name,))
        self.connection.commit()

    def add_scenario_coverage(self, suite_name: str, scenario_code: str, required_method_code: str, is_present: bool, *, category: str = "", notes: str = "") -> None:
        self.connection.execute(
            """
            INSERT INTO scenario_coverage (suite_name, scenario_code, category, required_method_code, is_present, notes, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (suite_name, scenario_code, category, required_method_code, 1 if is_present else 0, notes, datetime.utcnow().isoformat(timespec="seconds")),
        )
        self.connection.commit()

    def add_hypothesis_check(self, suite_name: str, hypothesis_id: str, title: str, source_type: str, metric_name: str, actual_value: float | None, threshold_value: float | None, comparator: str, passed: bool, *, scenario_code: str = "", task_id: str = "", method_code: str = "", baseline_method_code: str = "", notes: str = "") -> None:
        self.connection.execute(
            """
            INSERT INTO hypothesis_checks (
                suite_name, hypothesis_id, title, source_type, scenario_code, task_id,
                method_code, baseline_method_code, metric_name, actual_value,
                threshold_value, comparator, passed, notes, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (suite_name, hypothesis_id, title, source_type, scenario_code, task_id, method_code, baseline_method_code, metric_name, float(actual_value) if actual_value is not None else None, float(threshold_value) if threshold_value is not None else None, comparator, 1 if passed else 0, notes, datetime.utcnow().isoformat(timespec="seconds")),
        )
        self.connection.commit()

    def runtime_run_exists(self, run_name: str) -> bool:
        row = self.connection.execute("SELECT 1 FROM runtime_runs WHERE run_name = ? LIMIT 1", (run_name,)).fetchone()
        return row is not None

    @staticmethod
    def _insert_dict(connection: sqlite3.Connection, table: str, values: dict) -> int:
        columns = list(values.keys())
        sql = f"INSERT INTO {table} ({', '.join(columns)}) VALUES ({', '.join(':' + c for c in columns)})"
        cursor = connection.execute(sql, values)
        return int(cursor.lastrowid)

    @staticmethod
    def _executemany_insert_dicts(connection: sqlite3.Connection, table: str, rows: list[dict]) -> None:
        if not rows:
            return
        columns = list(rows[0].keys())
        sql = f"INSERT INTO {table} ({', '.join(columns)}) VALUES ({', '.join(':' + c for c in columns)})"
        connection.executemany(sql, rows)

    def import_runtime_run(self, metadata: RuntimeRunMetadata, summary_json_path: Path, frame_csv_path: Path, *, frame_jsonl_path: Path | None = None, settings_json_path: Path | None = None, error_json_path: Path | None = None) -> int:
        summary = self._read_summary(summary_json_path)
        flags = summary.get("feature_flags", {}) if isinstance(summary.get("feature_flags", {}), dict) else {}
        frame_jsonl_final = Path(frame_jsonl_path) if frame_jsonl_path else (Path(summary.get("frame_jsonl")) if summary.get("frame_jsonl") else None)
        settings_json_final = Path(settings_json_path) if settings_json_path else (Path(summary.get("settings_json")) if summary.get("settings_json") else None)
        error_json_final = Path(error_json_path) if error_json_path else None
        case_id = str(summary.get("case_id") or metadata.case_id or "")
        variant_code = str(summary.get("variant") or metadata.variant_code or metadata.method_code)
        phase = str(summary.get("phase", metadata.phase or "phase1_pre_novelty_final_adjustment"))
        novelty_mapping_version = str(summary.get("novelty_mapping_version", metadata.novelty_mapping_version or "final5_v1"))
        top_level_novelty_mask = int(summary.get("top_level_novelty_mask", summary.get("topLevelNoveltyMask", metadata.top_level_novelty_mask or 0)) or 0)

        experiment_id = self.upsert_experiment(metadata.experiment_name, metadata.operator_name, metadata.notes)
        run_values = {
            "experiment_id": experiment_id, "run_name": metadata.run_name, "method_code": metadata.method_code,
            "scenario_code": metadata.scenario_code, "asset_name": metadata.asset_name, "runtime_source": metadata.runtime_source,
            "summary_json_path": str(summary_json_path), "frame_csv_path": str(frame_csv_path),
            "frame_jsonl_path": str(frame_jsonl_final or metadata.frame_jsonl_path or ""),
            "settings_json_path": str(settings_json_final or metadata.settings_json_path or ""),
            "error_json_path": str(error_json_final or metadata.error_json_path or ""),
            "case_id": case_id, "variant_code": variant_code,
            "seed": int(summary.get("seed", metadata.seed or 0) or 0),
            "settings_hash": str(summary.get("settings_hash", "")),
            "warmup_frames": int(summary.get("warmup_frames", metadata.warmup_frames or 0) or 0),
            "measurement_frames": int(summary.get("measurement_frames", metadata.measurement_frames or 0) or 0),
            "enable_no_novelty_baseline": 1 if flags.get("enableNoNoveltyBaseline", metadata.enable_no_novelty_baseline) else 0,
            "enable_microtype": 1 if flags.get("enableMicrotype", metadata.enable_microtype) else 0,
            "enable_cbma": 1 if flags.get("enableCBMA", metadata.enable_cbma) else 0,
            "enable_physics_islands": 1 if flags.get("enablePhysicsIslands", metadata.enable_physics_islands) else 0,
            "enable_rod_repair": 1 if flags.get("enableRodRepair", metadata.enable_rod_repair) else 0,
            "enable_gpu_resident": 1 if flags.get("enableGpuResident", metadata.enable_gpu_resident) else 0,
            "enable_budgeted_physics": 1 if flags.get("enableBudgetedPhysics", metadata.enable_budgeted_physics) else 0,
            "enable_certificate_render": 1 if flags.get("enableCertificateRender", metadata.enable_certificate_render) else 0,
            "enable_per_strand_edit": 1 if flags.get("enablePerStrandEdit", metadata.enable_per_strand_edit) else 0,
            "top_level_novelty_mask": top_level_novelty_mask,
            "phase": phase,
            "novelty_mapping_version": novelty_mapping_version,
            "created_at": datetime.utcnow().isoformat(timespec="seconds"),
        }
        summary_map = {
            "sample_count": (int, "sample_count", "sampleCount"),
            "avg_frame_ms": (float, "avg_frame_ms", "avgFrameMs"),
            "p50_frame_ms": (float, "p50_frame_ms", "p50FrameMs"),
            "p95_frame_ms": (float, "p95_frame_ms", "p95FrameMs"),
            "p99_frame_ms": (float, "p99_frame_ms", "p99FrameMs"),
            "worst_frame_ms": (float, "worst_frame_ms", "worstFrameMs"),
            "avg_simulation_ms": (float, "avg_simulation_ms", "avgSimulationMs"),
            "p50_simulation_ms": (float, "p50_simulation_ms", "p50SimulationMs"),
            "p95_simulation_ms": (float, "p95_simulation_ms", "p95SimulationMs"),
            "p99_simulation_ms": (float, "p99_simulation_ms", "p99SimulationMs"),
            "avg_gpu_hair_ms": (float, "avg_gpu_hair_ms", "avgGpuHairMs"),
            "p95_gpu_hair_ms": (float, "p95_gpu_hair_ms", "p95GpuHairMs"),
            "p99_gpu_hair_ms": (float, "p99_gpu_hair_ms", "p99GpuHairMs"),
            "avg_fps": (float, "avg_fps", "avgFps"),
            "sixty_fps_ratio": (float, "sixty_fps_ratio", "sixtyFpsRatio"),
            "guide_count": (int, "guide_count", "guideCount"),
            "follower_count": (int, "follower_count", "followerCount"),
            "total_strand_count": (int, "total_strand_count", "totalStrandCount"),
            "total_particle_count": (int, "total_particle_count", "totalParticleCount"),
            "avg_silhouette_error": (float, "avg_silhouette_error", "averageSilhouetteError"),
            "avg_clump_preservation": (float, "avg_clump_preservation", "averageClumpPreservation"),
            "avg_edit_locality": (float, "avg_edit_locality", "averageEditLocality"),
            "avg_boundary_naturalness": (float, "avg_boundary_naturalness", "averageBoundaryNaturalness"),
            "avg_edit_to_run_latency_ms": (float, "avg_edit_to_run_latency_ms", "averageEditToRunLatencyMs"),
            "avg_penetration_per_sec": (float, "avg_penetration_per_sec", "avgPenetrationPerSec"),
            "peak_ram_mb": (float, "peak_ram_mb", "peakRamMb"),
            "vram_mb": (float, "vram_mb", "vramMb"),
            "bandwidth_mb_frame": (float, "bandwidth_mb_frame", "bandwidthMbFrame"),
            "certificate_failure_rate": (float, "certificate_failure_rate", "certificateFailureRate"),
            "fallback_rate": (float, "fallback_rate", "fallbackRate"),
            "repair_success_rate": (float, "repair_success_rate", "repairSuccessRate"),
            "cpu_readback_count": (int, "cpu_readback_count", "cpuReadbackCount"),
            "false_static_count": (int, "false_static_count", "falseStaticCount"),
            "false_static_rate": (float, "false_static_rate", "falseStaticRate"),
            "group_static_reuse_rate": (float, "group_static_reuse_rate", "groupStaticReuseRate"),
            "group_dynamic_rate": (float, "group_dynamic_rate", "groupDynamicRate"),
            "deferred_correction_rate": (float, "deferred_correction_rate", "deferredCorrectionRate"),
            "avg_static_dynamic_boundary_error_mm": (float, "avg_static_dynamic_boundary_error_mm", "avgStaticDynamicBoundaryErrorMm"),
            "max_static_dynamic_boundary_error_mm": (float, "max_static_dynamic_boundary_error_mm", "maxStaticDynamicBoundaryErrorMm"),
            "cache_hit_count": (int, "cache_hit_count", "cacheHitCount"),
            "cache_miss_count": (int, "cache_miss_count", "cacheMissCount"),
            "cache_invalidation_count": (int, "cache_invalidation_count", "cacheInvalidationCount"),
            "watchdog_check_count": (int, "watchdog_check_count", "watchdogCheckCount"),
            "watchdog_repair_count": (int, "watchdog_repair_count", "watchdogRepairCount"),
            "direction_cache_memory_mb": (float, "direction_cache_memory_mb", "directionCacheMemoryMb"),
            "full_position_memory_mb": (float, "full_position_memory_mb", "fullPositionMemoryMb"),
            "direction_cache_memory_saving_rate": (float, "direction_cache_memory_saving_rate", "directionCacheMemorySavingRate"),
        }
        for column, (caster, *keys) in summary_map.items():
            run_values[column] = caster(self._resolve_summary_value(summary, *keys, default=0))
        run_id = self._insert_dict(self.connection, "runtime_runs", run_values)

        with Path(frame_csv_path).open("r", encoding="utf-8", newline="") as handle:
            base_rows = []
            for row in csv.DictReader(handle):
                base_rows.append({
                    "run_id": run_id,
                    "sample_index": self._csv_int(row, "sampleIndex", "sample_index"),
                    "elapsed_seconds": self._csv_float(row, "elapsedSeconds", "elapsed_seconds"),
                    "frame_ms": self._csv_float(row, "frameMilliseconds", "frame_ms"),
                    "simulation_ms": self._csv_float(row, "simulationMilliseconds", "simulation_ms"),
                    "fps": self._csv_float(row, "fps"),
                    "guide_count": self._csv_int(row, "guideCount", "guide_count"),
                    "follower_count": self._csv_int(row, "followerCount", "follower_count"),
                    "total_strand_count": self._csv_int(row, "totalStrandCount", "total_strand_count"),
                    "total_particle_count": self._csv_int(row, "totalParticleCount", "total_particle_count"),
                    "global_frame_index": self._csv_int(row, "globalFrameIndex", "global_frame_index"),
                    "measured_frame_index": self._csv_int(row, "measuredFrameIndex", "measured_frame_index"),
                    "certificate_failure_count": self._csv_int(row, "certificateFailureCount", "certificate_failure_count"),
                    "certificate_failure_rate": self._csv_float(row, "certificateFailureRate", "certificate_failure_rate"),
                    "fallback_count": self._csv_int(row, "fallbackCount", "fallback_count"),
                    "mode_skip_count": self._csv_int(row, "modeSkipCount", "mode_skip_count"),
                    "mode_qs_count": self._csv_int(row, "modeQSOnlyCount", "modeQsCount", "mode_qs_count"),
                    "mode_dynamic_count": self._csv_int(row, "modeDynamicCount", "mode_dynamic_count"),
                    "mode_island_count": self._csv_int(row, "modeIslandCount", "mode_island_count"),
                    "mode_repair_count": self._csv_int(row, "modeRepairCount", "mode_repair_count"),
                    "mode_float_fallback_count": self._csv_int(row, "modeFloatFallbackCount", "mode_float_fallback_count"),
                    "island_count": self._csv_int(row, "islandCount", "island_count"),
                    "island_member_count": self._csv_int(row, "islandMemberCount", "island_member_count"),
                    "island_commit_count": self._csv_int(row, "islandCommitCount", "island_commit_count"),
                    "island_reject_count": self._csv_int(row, "islandRejectCount", "island_reject_count"),
                    "repair_task_count": self._csv_int(row, "repairTaskCount", "repair_task_count"),
                    "repair_committed_count": self._csv_int(row, "repairCommittedCount", "repair_committed_count"),
                    "repair_rejected_count": self._csv_int(row, "repairRejectedCount", "repair_rejected_count"),
                    "repair_deferred_count": self._csv_int(row, "repairDeferredCount", "repair_deferred_count"),
                    "repair_success_rate": self._csv_float(row, "repairSuccessRate", "repair_success_rate"),
                    "cpu_readback_count": self._csv_int(row, "cpuReadbackCount", "cpu_readback_count"),
                    "gpu_hair_ms": self._csv_float(row, "gpuHairMilliseconds", "gpuHairMs", "gpu_hair_ms"),
                    "vram_mb": self._csv_float(row, "vramMegabytes", "vramMb", "vram_mb"),
                    "bandwidth_mb_frame": self._csv_float(row, "bandwidthMegabytesPerFrame", "bandwidthMbFrame", "bandwidth_mb_frame"),
                    "mean_decode_error_mm": self._csv_float(row, "meanDecodeErrorMm", "mean_decode_error_mm"),
                    "max_penetration_mm": self._csv_float(row, "maxPenetrationMm", "max_penetration_mm"),
                })
        self._executemany_insert_dicts(self.connection, "runtime_frame_metrics", base_rows)

        phase1_columns = [
            "top_level_novelty_mask", "overflow_count", "budget_drop_count", "defer_count",
            "microtype_decoded_count", "safe_decode_count", "false_safe_count",
            "total_segment_group_count", "static_cached_group_count", "static_transformed_group_count",
            "dynamic_group_count", "deferred_correction_group_count", "repair_required_group_count",
            "fallback_group_count", "false_static_count", "false_static_rate", "cache_hit_count",
            "cache_miss_count", "cache_invalidation_count", "watchdog_check_count", "watchdog_repair_count",
            "static_dynamic_boundary_mean_error_mm", "static_dynamic_boundary_max_error_mm",
            "direction_cache_memory_mb", "full_position_memory_mb", "direction_cache_memory_saving_rate",
            "typed_repair_request_count", "float_fallback_request_count", "reject_payload_count",
            "risky_strand_count", "repair_commit_count", "repair_reject_count", "repair_defer_count",
            "island_candidate_count", "physics_fallback_count", "render_certificate_count", "render_skip_count",
            "render_coarse_opacity_count", "render_strand_raster_count", "render_high_quality_shadow_count",
            "render_high_quality_specular_count", "render_full_quality_count", "render_dropped_full_quality_count",
            "p95_shadow_error_bound", "p95_specular_error_bound", "temporal_flicker_score",
            "edit_operation_count", "edit_repair_task_count", "edit_island_escalation_count", "edit_commit_count",
            "edit_reject_count", "edit_preview_full_quality_count", "edit_latency_ms",
            "gpu_resident_task_count", "risky_compaction_count", "dispatch_indirect_count",
            "draw_indirect_count", "delayed_telemetry_read_count",
        ]
        csv_mapping = {
            "top_level_novelty_mask": (int, "topLevelNoveltyMask", "top_level_novelty_mask"),
            "overflow_count": (int, "overflowCount", "overflow_count"),
            "budget_drop_count": (int, "budgetDropCount", "budget_drop_count"),
            "defer_count": (int, "deferCount", "defer_count"),
            "microtype_decoded_count": (int, "microtypeDecodedCount", "microtype_decoded_count"),
            "safe_decode_count": (int, "safeDecodeCount", "safe_decode_count"),
            "false_safe_count": (int, "falseSafeCount", "false_safe_count"),
            "total_segment_group_count": (int, "totalSegmentGroupCount", "total_segment_group_count"),
            "static_cached_group_count": (int, "staticCachedGroupCount", "static_cached_group_count"),
            "static_transformed_group_count": (int, "staticTransformedGroupCount", "static_transformed_group_count"),
            "dynamic_group_count": (int, "dynamicGroupCount", "dynamic_group_count"),
            "deferred_correction_group_count": (int, "deferredCorrectionGroupCount", "deferred_correction_group_count"),
            "repair_required_group_count": (int, "repairRequiredGroupCount", "repair_required_group_count"),
            "fallback_group_count": (int, "fallbackGroupCount", "fallback_group_count"),
            "false_static_count": (int, "falseStaticCount", "false_static_count"),
            "false_static_rate": (float, "falseStaticRate", "false_static_rate"),
            "cache_hit_count": (int, "cacheHitCount", "cache_hit_count"),
            "cache_miss_count": (int, "cacheMissCount", "cache_miss_count"),
            "cache_invalidation_count": (int, "cacheInvalidationCount", "cache_invalidation_count"),
            "watchdog_check_count": (int, "watchdogCheckCount", "watchdog_check_count"),
            "watchdog_repair_count": (int, "watchdogRepairCount", "watchdog_repair_count"),
            "static_dynamic_boundary_mean_error_mm": (float, "staticDynamicBoundaryMeanErrorMm", "static_dynamic_boundary_mean_error_mm"),
            "static_dynamic_boundary_max_error_mm": (float, "staticDynamicBoundaryMaxErrorMm", "static_dynamic_boundary_max_error_mm"),
            "direction_cache_memory_mb": (float, "directionCacheMemoryMb", "direction_cache_memory_mb"),
            "full_position_memory_mb": (float, "fullPositionMemoryMb", "full_position_memory_mb"),
            "direction_cache_memory_saving_rate": (float, "directionCacheMemorySavingRate", "direction_cache_memory_saving_rate"),
            "typed_repair_request_count": (int, "typedRepairRequestCount", "typed_repair_request_count"),
            "float_fallback_request_count": (int, "floatFallbackRequestCount", "float_fallback_request_count"),
            "reject_payload_count": (int, "rejectPayloadCount", "reject_payload_count"),
            "risky_strand_count": (int, "riskyStrandCount", "risky_strand_count"),
            "repair_commit_count": (int, "repairCommitCount", "repair_commit_count"),
            "repair_reject_count": (int, "repairRejectCount", "repair_reject_count"),
            "repair_defer_count": (int, "repairDeferCount", "repair_defer_count"),
            "island_candidate_count": (int, "islandCandidateCount", "island_candidate_count"),
            "physics_fallback_count": (int, "physicsFallbackCount", "physics_fallback_count"),
            "render_certificate_count": (int, "renderCertificateCount", "render_certificate_count"),
            "render_skip_count": (int, "renderSkipCount", "render_skip_count"),
            "render_coarse_opacity_count": (int, "renderCoarseOpacityCount", "render_coarse_opacity_count"),
            "render_strand_raster_count": (int, "renderStrandRasterCount", "render_strand_raster_count"),
            "render_high_quality_shadow_count": (int, "renderHighQualityShadowCount", "render_high_quality_shadow_count"),
            "render_high_quality_specular_count": (int, "renderHighQualitySpecularCount", "render_high_quality_specular_count"),
            "render_full_quality_count": (int, "renderFullQualityCount", "render_full_quality_count"),
            "render_dropped_full_quality_count": (int, "renderDroppedFullQualityCount", "render_dropped_full_quality_count"),
            "p95_shadow_error_bound": (float, "p95ShadowErrorBound", "p95_shadow_error_bound"),
            "p95_specular_error_bound": (float, "p95SpecularErrorBound", "p95_specular_error_bound"),
            "temporal_flicker_score": (float, "temporalFlickerScore", "temporal_flicker_score"),
            "edit_operation_count": (int, "editOperationCount", "edit_operation_count"),
            "edit_repair_task_count": (int, "editRepairTaskCount", "edit_repair_task_count"),
            "edit_island_escalation_count": (int, "editIslandEscalationCount", "edit_island_escalation_count"),
            "edit_commit_count": (int, "editCommitCount", "edit_commit_count"),
            "edit_reject_count": (int, "editRejectCount", "edit_reject_count"),
            "edit_preview_full_quality_count": (int, "editPreviewFullQualityCount", "edit_preview_full_quality_count"),
            "edit_latency_ms": (float, "editLatencyMs", "edit_latency_ms"),
            "gpu_resident_task_count": (int, "gpuResidentTaskCount", "gpu_resident_task_count"),
            "risky_compaction_count": (int, "riskyCompactionCount", "risky_compaction_count"),
            "dispatch_indirect_count": (int, "dispatchIndirectCount", "dispatch_indirect_count"),
            "draw_indirect_count": (int, "drawIndirectCount", "draw_indirect_count"),
            "delayed_telemetry_read_count": (int, "delayedTelemetryReadCount", "delayed_telemetry_read_count"),
        }
        with Path(frame_csv_path).open("r", encoding="utf-8", newline="") as handle:
            phase1_rows = []
            for row in csv.DictReader(handle):
                out = {"run_id": run_id, "sample_index": self._csv_int(row, "sampleIndex", "sample_index")}
                for column, spec in csv_mapping.items():
                    caster, *keys = spec
                    out[column] = self._csv_int(row, *keys) if caster is int else self._csv_float(row, *keys)
                phase1_rows.append(out)
        if phase1_rows:
            sql = "UPDATE runtime_frame_metrics SET " + ", ".join(f"{c} = :{c}" for c in phase1_columns) + " WHERE run_id = :run_id AND sample_index = :sample_index"
            self.connection.executemany(sql, phase1_rows)

        self.connection.execute(
            """
            UPDATE runtime_runs
            SET false_static_count = COALESCE((SELECT SUM(false_static_count) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                false_static_rate = COALESCE((SELECT 1.0 * SUM(false_static_count) / NULLIF(SUM(static_cached_group_count + static_transformed_group_count), 0) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                group_static_reuse_rate = COALESCE((SELECT 1.0 * SUM(static_cached_group_count + static_transformed_group_count) / NULLIF(SUM(total_segment_group_count), 0) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                group_dynamic_rate = COALESCE((SELECT 1.0 * SUM(dynamic_group_count) / NULLIF(SUM(total_segment_group_count), 0) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                deferred_correction_rate = COALESCE((SELECT 1.0 * SUM(deferred_correction_group_count) / NULLIF(SUM(total_segment_group_count), 0) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                avg_static_dynamic_boundary_error_mm = COALESCE((SELECT AVG(static_dynamic_boundary_mean_error_mm) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                max_static_dynamic_boundary_error_mm = COALESCE((SELECT MAX(static_dynamic_boundary_max_error_mm) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                cache_hit_count = COALESCE((SELECT SUM(cache_hit_count) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                cache_miss_count = COALESCE((SELECT SUM(cache_miss_count) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                cache_invalidation_count = COALESCE((SELECT SUM(cache_invalidation_count) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                watchdog_check_count = COALESCE((SELECT SUM(watchdog_check_count) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                watchdog_repair_count = COALESCE((SELECT SUM(watchdog_repair_count) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                direction_cache_memory_mb = COALESCE((SELECT AVG(direction_cache_memory_mb) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                full_position_memory_mb = COALESCE((SELECT AVG(full_position_memory_mb) FROM runtime_frame_metrics WHERE run_id = :run_id), 0),
                direction_cache_memory_saving_rate = COALESCE((SELECT 1.0 - SUM(direction_cache_memory_mb) / NULLIF(SUM(full_position_memory_mb), 0) FROM runtime_frame_metrics WHERE run_id = :run_id), 0)
            WHERE run_id = :run_id
            """,
            {"run_id": run_id},
        )
        self.connection.commit()

        avg_frame_ms = float(self._resolve_summary_value(summary, "avg_frame_ms", "avgFrameMs", default=0.0))
        p95_frame_ms = float(self._resolve_summary_value(summary, "p95_frame_ms", "p95FrameMs", default=0.0))
        avg_simulation_ms = float(self._resolve_summary_value(summary, "avg_simulation_ms", "avgSimulationMs", default=0.0))
        sixty_fps_ratio = float(self._resolve_summary_value(summary, "sixty_fps_ratio", "sixtyFpsRatio", default=0.0))
        guide_count = int(self._resolve_summary_value(summary, "guide_count", "guideCount", default=0))
        particle_count = int(self._resolve_summary_value(summary, "total_particle_count", "totalParticleCount", default=0))
        self.add_derived_metric("run", metadata.run_name, "budget_headroom_ms", 16.67 - p95_frame_ms, run_id=run_id, units="ms")
        self.add_derived_metric("run", metadata.run_name, "fps_success_percent", sixty_fps_ratio * 100.0, run_id=run_id, units="percent")
        self.add_derived_metric("run", metadata.run_name, "simulation_share_ratio", (avg_simulation_ms / avg_frame_ms) if avg_frame_ms > 0.0 else 0.0, run_id=run_id, units="ratio")
        self.add_derived_metric("run", metadata.run_name, "particles_per_guide", (particle_count / guide_count) if guide_count > 0 else 0.0, run_id=run_id, units="count")
        self.add_artifact(run_id, "telemetry", "summary_json", summary_json_path)
        self.add_artifact(run_id, "telemetry", "frame_csv", frame_csv_path)
        if frame_jsonl_final and Path(frame_jsonl_final).exists():
            self.add_artifact(run_id, "telemetry", "frame_jsonl", Path(frame_jsonl_final))
        if settings_json_final and Path(settings_json_final).exists():
            self.add_artifact(run_id, "telemetry", "settings_json", Path(settings_json_final))
        return run_id

    @staticmethod
    def _rows_to_dicts(rows: Iterable[sqlite3.Row]):
        return [dict(row) for row in rows]

    def fetch_runs(self):
        return self.connection.execute("SELECT * FROM runtime_runs ORDER BY created_at, run_id").fetchall()

    def fetch_frame_metrics(self):
        return self.connection.execute("SELECT * FROM runtime_frame_metrics ORDER BY run_id, sample_index").fetchall()

    def fetch_artifact_rows(self):
        rows = self.connection.execute("SELECT * FROM run_artifacts ORDER BY run_id, artifact_id").fetchall()
        return self._rows_to_dicts(rows)

    def fetch_derived_metric_rows(self):
        rows = self.connection.execute("SELECT * FROM derived_metrics ORDER BY scope_type, scope_key, metric_name").fetchall()
        return self._rows_to_dicts(rows)

    def fetch_user_task_rows(self):
        rows = self.connection.execute(
            """
            SELECT utr.*, e.experiment_name
            FROM user_task_results utr
            JOIN experiments e ON e.experiment_id = utr.experiment_id
            ORDER BY utr.created_at, utr.task_result_id
            """
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_user_task_summary_rows(self):
        rows = self.connection.execute(
            """
            SELECT e.experiment_name, utr.task_id, utr.title, utr.method_code, utr.scenario_code, utr.asset_name,
                   COUNT(*) AS sample_count,
                   AVG(utr.completion_seconds) AS avg_completion_seconds,
                   AVG(utr.redo_count) AS avg_redo_count,
                   AVG(utr.unintended_change_score) AS avg_unintended_change_score,
                   AVG(utr.satisfaction_score) AS avg_satisfaction_score
            FROM user_task_results utr
            JOIN experiments e ON e.experiment_id = utr.experiment_id
            GROUP BY e.experiment_name, utr.task_id, utr.title, utr.method_code, utr.scenario_code, utr.asset_name
            ORDER BY utr.task_id, utr.method_code
            """
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_scenario_coverage_rows(self):
        rows = self.connection.execute("SELECT * FROM scenario_coverage ORDER BY suite_name, scenario_code, required_method_code").fetchall()
        return self._rows_to_dicts(rows)

    def fetch_hypothesis_rows(self):
        rows = self.connection.execute("SELECT * FROM hypothesis_checks ORDER BY suite_name, hypothesis_id, check_id").fetchall()
        return self._rows_to_dicts(rows)

    def fetch_comparison_rows(self):
        return self.connection.execute(self._comparison_sql("method_code, scenario_code", "method_code, scenario_code")).fetchall()

    def fetch_ablation_rows(self):
        return self.connection.execute(self._comparison_sql("method_code", "method_code")).fetchall()

    def fetch_method_summary_rows(self):
        rows = self.connection.execute(
            self._comparison_sql("method_code", "avg_sixty_fps_ratio DESC, avg_p95_frame_ms ASC, method_code ASC", include_simulation=True)
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_scenario_summary_rows(self):
        rows = self.connection.execute(self._comparison_sql("scenario_code", "scenario_code ASC")).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_experiment_summary(self) -> dict:
        row = self.connection.execute(
            """
            SELECT COUNT(*) AS run_count,
                   COUNT(DISTINCT method_code) AS method_count,
                   COUNT(DISTINCT scenario_code) AS scenario_count,
                   MIN(created_at) AS first_created_at,
                   MAX(created_at) AS last_created_at,
                   AVG(avg_frame_ms) AS avg_frame_ms,
                   AVG(p95_frame_ms) AS avg_p95_frame_ms,
                   AVG(p99_frame_ms) AS avg_p99_frame_ms,
                   AVG(avg_gpu_hair_ms) AS avg_gpu_hair_ms,
                   AVG(vram_mb) AS avg_vram_mb,
                   AVG(certificate_failure_rate) AS avg_certificate_failure_rate,
                   AVG(fallback_rate) AS avg_fallback_rate,
                   AVG(repair_success_rate) AS avg_repair_success_rate,
                   AVG(worst_frame_ms) AS avg_worst_frame_ms,
                   AVG(avg_fps) AS avg_fps,
                   AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio,
                   AVG(avg_edit_locality) AS avg_edit_locality,
                   AVG(avg_boundary_naturalness) AS avg_boundary_naturalness,
                   AVG(avg_edit_to_run_latency_ms) AS avg_edit_to_run_latency_ms,
                   AVG(avg_penetration_per_sec) AS avg_penetration_per_sec
            FROM runtime_runs
            """
        ).fetchone()
        return dict(row) if row else {}

    @staticmethod
    def _comparison_sql(group_by: str, order_by: str, include_simulation: bool = False) -> str:
        simulation_column = ", AVG(avg_simulation_ms) AS avg_simulation_ms" if include_simulation else ""
        return f"""
            SELECT {group_by},
                   COUNT(*) AS run_count,
                   AVG(avg_frame_ms) AS avg_frame_ms,
                   AVG(p95_frame_ms) AS avg_p95_frame_ms,
                   AVG(p99_frame_ms) AS avg_p99_frame_ms,
                   AVG(avg_gpu_hair_ms) AS avg_gpu_hair_ms,
                   AVG(vram_mb) AS avg_vram_mb,
                   AVG(certificate_failure_rate) AS avg_certificate_failure_rate,
                   AVG(fallback_rate) AS avg_fallback_rate,
                   AVG(repair_success_rate) AS avg_repair_success_rate,
                   AVG(worst_frame_ms) AS avg_worst_frame_ms,
                   AVG(avg_fps) AS avg_fps,
                   AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio{simulation_column},
                   AVG(avg_edit_locality) AS avg_edit_locality,
                   AVG(avg_boundary_naturalness) AS avg_boundary_naturalness,
                   AVG(avg_edit_to_run_latency_ms) AS avg_edit_to_run_latency_ms,
                   AVG(avg_penetration_per_sec) AS avg_penetration_per_sec
            FROM runtime_runs
            GROUP BY {group_by}
            ORDER BY {order_by}
        """
