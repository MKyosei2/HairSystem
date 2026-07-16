from __future__ import annotations

import csv
import hashlib
import json
import math
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
    def _resolve_summary_value(summary: dict, *keys: str, default=None):
        for key in keys:
            if key in summary and summary[key] is not None:
                return summary[key]
        return default

    @staticmethod
    def _pick_csv_value(row: dict, *keys: str, default=None):
        for key in keys:
            if key in row and row[key] not in (None, ""):
                return row[key]
        return default

    @staticmethod
    def _nullable_float(value):
        if value in (None, "", "null"):
            return None
        try:
            parsed = float(value)
        except (TypeError, ValueError):
            return None
        return parsed if math.isfinite(parsed) else None

    @staticmethod
    def _nullable_int(value):
        if value in (None, "", "null"):
            return None
        try:
            parsed = float(value)
        except (TypeError, ValueError):
            return None
        return int(parsed) if math.isfinite(parsed) else None

    @staticmethod
    def _nested_value(payload: dict, dotted_key: str, default=None):
        current = payload
        for part in dotted_key.split("."):
            if not isinstance(current, dict) or part not in current:
                return default
            current = current[part]
        return current

    @staticmethod
    def file_sha256(path: Path) -> str:
        digest = hashlib.sha256()
        with Path(path).open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    def add_artifact(self, run_id: int, category: str, label: str, artifact_path: Path) -> None:
        artifact_path = Path(artifact_path)
        sha256 = self.file_sha256(artifact_path) if artifact_path.exists() and artifact_path.is_file() else None
        size_bytes = artifact_path.stat().st_size if artifact_path.exists() and artifact_path.is_file() else None
        self.connection.execute(
            """
            INSERT INTO run_artifacts (run_id, category, label, artifact_path, sha256, size_bytes, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                category,
                label,
                str(artifact_path),
                sha256,
                int(size_bytes) if size_bytes is not None else None,
                datetime.utcnow().isoformat(timespec="seconds"),
            ),
        )
        self.connection.commit()

    def add_derived_metric(
        self,
        scope_type: str,
        scope_key: str,
        metric_name: str,
        metric_value: float | int | None,
        *,
        run_id: int | None = None,
        units: str = "",
        notes: str = "",
    ) -> None:
        self.connection.execute(
            """
            INSERT INTO derived_metrics (run_id, scope_type, scope_key, metric_name, metric_value, units, notes, created_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                run_id,
                scope_type,
                scope_key,
                metric_name,
                float(metric_value) if metric_value is not None else None,
                units,
                notes,
                datetime.utcnow().isoformat(timespec="seconds"),
            ),
        )
        self.connection.commit()

    def add_user_task_result(
        self,
        experiment_name: str,
        task_id: str,
        title: str,
        method_code: str,
        scenario_code: str = "",
        asset_name: str = "",
        participant_id: str = "",
        completion_seconds: float | None = None,
        redo_count: int | None = None,
        unintended_change_score: float | None = None,
        satisfaction_score: float | None = None,
        notes: str = "",
    ) -> None:
        experiment_id = self.upsert_experiment(experiment_name, "", notes)
        self.connection.execute(
            """
            INSERT INTO user_task_results (
                experiment_id, task_id, title, method_code, scenario_code, asset_name,
                participant_id, completion_seconds, redo_count, unintended_change_score,
                satisfaction_score, notes, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                experiment_id,
                task_id,
                title,
                method_code,
                scenario_code,
                asset_name,
                participant_id,
                float(completion_seconds) if completion_seconds is not None else None,
                int(redo_count) if redo_count is not None else None,
                float(unintended_change_score) if unintended_change_score is not None else None,
                float(satisfaction_score) if satisfaction_score is not None else None,
                notes,
                datetime.utcnow().isoformat(timespec="seconds"),
            ),
        )
        self.connection.commit()

    def clear_suite_rows(self, suite_name: str) -> None:
        self.connection.execute("DELETE FROM hypothesis_checks WHERE suite_name = ?", (suite_name,))
        self.connection.execute("DELETE FROM scenario_coverage WHERE suite_name = ?", (suite_name,))
        self.connection.commit()

    def add_scenario_coverage(
        self,
        suite_name: str,
        scenario_code: str,
        required_method_code: str,
        is_present: bool,
        *,
        category: str = "",
        notes: str = "",
    ) -> None:
        self.connection.execute(
            """
            INSERT INTO scenario_coverage (
                suite_name, scenario_code, category, required_method_code, is_present, notes, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?)
            """,
            (
                suite_name,
                scenario_code,
                category,
                required_method_code,
                1 if is_present else 0,
                notes,
                datetime.utcnow().isoformat(timespec="seconds"),
            ),
        )
        self.connection.commit()

    def add_hypothesis_check(
        self,
        suite_name: str,
        hypothesis_id: str,
        title: str,
        source_type: str,
        metric_name: str,
        actual_value: float | None,
        threshold_value: float | None,
        comparator: str,
        passed: bool,
        *,
        scenario_code: str = "",
        task_id: str = "",
        method_code: str = "",
        baseline_method_code: str = "",
        notes: str = "",
    ) -> None:
        self.connection.execute(
            """
            INSERT INTO hypothesis_checks (
                suite_name, hypothesis_id, title, source_type, scenario_code, task_id,
                method_code, baseline_method_code, metric_name, actual_value,
                threshold_value, comparator, passed, notes, created_at
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                suite_name,
                hypothesis_id,
                title,
                source_type,
                scenario_code,
                task_id,
                method_code,
                baseline_method_code,
                metric_name,
                float(actual_value) if actual_value is not None else None,
                float(threshold_value) if threshold_value is not None else None,
                comparator,
                1 if passed else 0,
                notes,
                datetime.utcnow().isoformat(timespec="seconds"),
            ),
        )
        self.connection.commit()

    def import_runtime_run(
        self,
        metadata: RuntimeRunMetadata,
        summary_json_path: Path,
        frame_csv_path: Path,
        *,
        frame_jsonl_path: Path | None = None,
        settings_json_path: Path | None = None,
        environment_json_path: Path | None = None,
        validation_json_path: Path | None = None,
    ) -> int:
        """Import one validated formal run.
    
        Missing telemetry remains SQL NULL. Invalid, synthetic, or incomplete
        runs are rejected before any row is committed.
        """
        summary_json_path = Path(summary_json_path)
        frame_csv_path = Path(frame_csv_path)
        summary = self._read_summary(summary_json_path)
        settings = {}
        if settings_json_path and Path(settings_json_path).exists():
            settings = json.loads(Path(settings_json_path).read_text(encoding="utf-8-sig"))
    
        raw_status = str(
            summary.get("validationStatus")
            or summary.get("status")
            or summary.get("measurementStatus")
            or "incomplete"
        ).strip().lower()
        real_measurement = bool(summary.get("realMeasurement", False))
        fatal_count = self._nullable_int(summary.get("fatalRuntimeFailureCount")) or 0
        synthetic_count = self._nullable_int(summary.get("syntheticMetricCount")) or 0
        timing_coverage = self._nullable_float(summary.get("gpuTimingCoverage"))
    
        # Backward-compatible acceptance is deliberately strict: old
        # "complete" files are accepted only when they independently satisfy
        # the formal validity contract.
        if raw_status == "complete":
            legacy_valid = (
                real_measurement
                and fatal_count == 0
                and synthetic_count == 0
                and timing_coverage is not None
                and timing_coverage >= 0.99
            )
            raw_status = "complete_valid" if legacy_valid else "complete_invalid"
        is_valid = raw_status == "complete_valid"
        if not is_valid:
            reasons = summary.get("validationReasons") or summary.get("invalidReasons") or []
            raise RuntimeError(
                f"Run '{metadata.run_name}' is not importable: status={raw_status}, reasons={reasons}"
            )
    
        observed = settings.get("observed", {}) if isinstance(settings.get("observed"), dict) else {}
        requested = settings.get("requested", {}) if isinstance(settings.get("requested"), dict) else {}
        invalid_reasons = summary.get("validationReasons") or summary.get("invalidReasons") or []
        experiment_id = self.upsert_experiment(metadata.experiment_name, metadata.operator_name, metadata.notes)
    
        def sv(*keys, default=None):
            return self._resolve_summary_value(summary, *keys, default=default)
    
        def nv(payload, *keys, default=None):
            for key in keys:
                value = self._nested_value(payload, key, None)
                if value is not None:
                    return value
            return default
    
        columns = [
            "experiment_id", "run_name", "method_code", "scenario_code", "asset_name", "runtime_source",
            "summary_json_path", "frame_csv_path", "frame_jsonl_path", "settings_json_path",
            "environment_json_path", "validation_json_path", "validation_status", "is_valid",
            "invalid_reason_json", "real_measurement", "requested_method_code", "applied_method_code",
            "observed_method_code", "measurement_mode", "seed", "repetition_index", "commit_sha",
            "executable_sha256", "shader_bundle_sha256", "asset_sha256", "asset_manifest_sha256",
            "settings_sha256", "scenario_timeline_sha256", "method_settings_sha256",
            "canonical_result_sha256", "decision_sequence_sha256", "hardware_profile", "gpu_name",
            "gpu_driver", "gpu_vendor_id", "gpu_device_id", "dedicated_vram_mb", "cpu_name",
            "power_scheme", "requested_render_width", "requested_render_height", "actual_render_width",
            "actual_render_height", "requested_display_strand_count", "actual_display_strand_count",
            "sample_count", "avg_frame_ms", "p95_frame_ms", "p999_frame_ms", "worst_frame_ms",
            "avg_simulation_ms", "avg_fps", "sixty_fps_ratio", "frame_overrun_rate", "guide_count",
            "follower_count", "total_strand_count", "total_particle_count", "avg_silhouette_error",
            "avg_clump_preservation", "avg_edit_locality", "avg_boundary_naturalness",
            "avg_edit_to_run_latency_ms", "avg_penetration_per_sec", "peak_ram_mb", "gpu_timing_coverage",
            "counter_coverage", "fatal_runtime_failure_count", "nan_count", "inf_count",
            "full_cpu_upload_count", "same_frame_readback_count", "synthetic_metric_count",
            "unavailable_metric_count", "created_at"
        ]
        values = [
            experiment_id, metadata.run_name, metadata.method_code, metadata.scenario_code,
            metadata.asset_name, metadata.runtime_source, str(summary_json_path), str(frame_csv_path),
            str(frame_jsonl_path) if frame_jsonl_path else None,
            str(settings_json_path) if settings_json_path else None,
            str(environment_json_path) if environment_json_path else None,
            str(validation_json_path) if validation_json_path else None,
            raw_status, 1, json.dumps(invalid_reasons, ensure_ascii=False), 1,
            sv("requestedMethodCode", "requested_method_code", default=metadata.method_code),
            sv("appliedMethodCode", "applied_method_code", default=metadata.method_code),
            sv("observedMethodCode", "observed_method_code", default=metadata.method_code),
            sv("measurementMode", "measurement_mode"), self._nullable_int(sv("seed")),
            self._nullable_int(sv("repetitionIndex", "repeat_index")), sv("commitSha", "commit_sha"),
            sv("executableSha256"), sv("shaderBundleSha256"), sv("assetSha256"),
            sv("assetManifestSha256"), sv("settingsSha256", "settings_hash"),
            sv("scenarioTimelineSha256"), sv("appliedMethodSettingsSha256", "methodSettingsSha256"),
            sv("canonicalResultSha256"), sv("decisionSequenceSha256"),
            sv("hardwareProfile", "hardware_profile"), sv("gpuName", default=nv(summary, "gpu.name")),
            sv("gpuDriver", default=nv(summary, "gpu.driverVersion")),
            self._nullable_int(sv("gpuVendorId")), self._nullable_int(sv("gpuDeviceId")),
            self._nullable_float(sv("dedicatedVramMb")), sv("cpuName", default=nv(summary, "cpu.name")),
            sv("powerScheme", default=nv(summary, "power.activeScheme")),
            self._nullable_int(sv("requestedRenderWidth", default=requested.get("width"))),
            self._nullable_int(sv("requestedRenderHeight", default=requested.get("height"))),
            self._nullable_int(sv("actualRenderWidth", default=observed.get("width"))),
            self._nullable_int(sv("actualRenderHeight", default=observed.get("height"))),
            self._nullable_int(sv("requestedDisplayStrandCount", default=requested.get("displayStrandCount"))),
            self._nullable_int(sv("actualDisplayStrandCount", "totalStrandCount", default=observed.get("displayStrandCount"))),
            self._nullable_int(sv("sample_count", "sampleCount")), self._nullable_float(sv("avg_frame_ms", "avgFrameMs")),
            self._nullable_float(sv("p95_frame_ms", "p95FrameMs")), self._nullable_float(sv("p999_frame_ms", "p999FrameMs")),
            self._nullable_float(sv("worst_frame_ms", "worstFrameMs")), self._nullable_float(sv("avg_simulation_ms", "avgSimulationMs")),
            self._nullable_float(sv("avg_fps", "avgFps")), self._nullable_float(sv("sixty_fps_ratio", "sixtyFpsRatio")),
            self._nullable_float(sv("frameOverrunRate", "frame_overrun_rate")), self._nullable_int(sv("guide_count", "guideCount")),
            self._nullable_int(sv("follower_count", "followerCount")), self._nullable_int(sv("total_strand_count", "totalStrandCount")),
            self._nullable_int(sv("total_particle_count", "totalParticleCount")),
            self._nullable_float(sv("avg_silhouette_error", "averageSilhouetteError")),
            self._nullable_float(sv("avg_clump_preservation", "averageClumpPreservation")),
            self._nullable_float(sv("avg_edit_locality", "averageEditLocality")),
            self._nullable_float(sv("avg_boundary_naturalness", "averageBoundaryNaturalness")),
            self._nullable_float(sv("avg_edit_to_run_latency_ms", "averageEditToRunLatencyMs")),
            self._nullable_float(sv("avg_penetration_per_sec", "avgPenetrationPerSec")),
            self._nullable_float(sv("peak_ram_mb", "peakRamMb")), timing_coverage,
            self._nullable_float(sv("counterCoverage")), fatal_count, self._nullable_int(sv("nanCount")),
            self._nullable_int(sv("infCount")), self._nullable_int(sv("fullCpuUploadCount")),
            self._nullable_int(sv("sameFrameReadbackCount")), synthetic_count,
            self._nullable_int(sv("unavailableMetricCount")), datetime.utcnow().isoformat(timespec="seconds")
        ]
    
        with self.connection:
            placeholders = ",".join("?" for _ in columns)
            cursor = self.connection.execute(
                f"INSERT INTO runtime_runs ({','.join(columns)}) VALUES ({placeholders})",
                values,
            )
            run_id = int(cursor.lastrowid)
    
            with frame_csv_path.open("r", encoding="utf-8-sig", newline="") as handle:
                reader = csv.DictReader(handle)
                for row in reader:
                    def cv(*keys):
                        return self._pick_csv_value(row, *keys, default=None)
                    availability_mask = self._nullable_int(
                        cv("metricAvailabilityMask", "availabilityMask", "availability_mask")
                    ) or 0
                    collision_available = bool(availability_mask & (1 << 6))
                    wake_available = bool(availability_mask & (1 << 7))
                    repair_available = bool(availability_mask & (1 << 8))
                    boundary_available = bool(availability_mask & (1 << 9))
    
                    frame_columns = [
                        "run_id", "sample_index", "source_frame_index", "elapsed_seconds", "frame_ms",
                        "frame_interval_ms", "cpu_frame_work_ms", "simulation_ms", "fps", "guide_count",
                        "follower_count", "total_strand_count", "total_particle_count", "metric_availability_mask",
                        "unavailable_metric_count", "synthetic_metric_count", "fatal_failure", "fatal_failure_code",
                        "collision_candidate_count", "collision_miss_count", "collision_miss_rate",
                        "predictive_wake_tp", "predictive_wake_fp", "predictive_wake_fn", "wake_delay_frame_max",
                        "repair_attempt_count", "repair_success_count", "repair_failure_count", "repair_recurrence_count",
                        "state_transition_count", "state_oscillation_count", "false_static_count", "false_static_rate",
                        "static_dynamic_boundary_mean_error_mm", "static_dynamic_boundary_max_error_mm",
                        "full_cpu_group_upload_count", "same_frame_readback_count", "nan_state_count", "infinite_state_count",
                        "block_classification_gpu_ms", "group_classification_gpu_ms", "predictive_wake_gpu_ms",
                        "compaction_gpu_ms", "static_transform_gpu_ms", "dynamic_solve_gpu_ms",
                        "velocity_recovery_gpu_ms", "repair_solve_gpu_ms", "boundary_repair_gpu_ms",
                        "cache_update_gpu_ms", "watchdog_gpu_ms", "follower_reconstruction_gpu_ms",
                        "render_preparation_gpu_ms", "hair_draw_gpu_ms", "telemetry_reduction_gpu_ms"
                    ]
                    frame_values = [
                        run_id, self._nullable_int(cv("sampleIndex", "sample_index")),
                        self._nullable_int(cv("sourceFrameIndex", "source_frame_index")),
                        self._nullable_float(cv("elapsedSeconds", "elapsed_seconds")),
                        self._nullable_float(cv("frameMilliseconds", "frame_ms")),
                        self._nullable_float(cv("frameIntervalMilliseconds", "frame_interval_ms")),
                        self._nullable_float(cv("cpuFrameWorkMilliseconds", "cpu_frame_work_ms")),
                        self._nullable_float(cv("simulationMilliseconds", "simulation_ms")), self._nullable_float(cv("fps")),
                        self._nullable_int(cv("guideCount", "guide_count")), self._nullable_int(cv("followerCount", "follower_count")),
                        self._nullable_int(cv("totalStrandCount", "total_strand_count")), self._nullable_int(cv("totalParticleCount", "total_particle_count")),
                        availability_mask,
                        self._nullable_int(cv("unavailableMetricCount", "unavailable_metric_count")),
                        self._nullable_int(cv("syntheticMetricCount", "synthetic_metric_count")),
                        self._nullable_int(cv("fatalFailure", "fatal_failure")), self._nullable_int(cv("fatalFailureCode", "fatal_failure_code")),
                        self._nullable_int(cv("collisionCandidateCount", "collision_candidate_count")),
                        self._nullable_int(cv("collisionMissCount", "collision_miss_count")) if collision_available else None,
                        self._nullable_float(cv("collisionMissRate", "collision_miss_rate")) if collision_available else None,
                        self._nullable_int(cv("predictiveWakeTruePositiveCount", "predictive_wake_tp")) if wake_available else None,
                        self._nullable_int(cv("predictiveWakeFalsePositiveCount", "predictive_wake_fp")) if wake_available else None,
                        self._nullable_int(cv("predictiveWakeFalseNegativeCount", "predictive_wake_fn")) if wake_available else None,
                        self._nullable_int(cv("wakeDelayFrameMax", "wake_delay_frame_max")) if wake_available else None,
                        self._nullable_int(cv("repairAttemptCount", "repair_attempt_count")),
                        self._nullable_int(cv("repairSuccessCount", "repair_success_count")) if repair_available else None,
                        self._nullable_int(cv("repairFailureCount", "repair_failure_count")) if repair_available else None,
                        self._nullable_int(cv("repairRecurrenceCount", "repair_recurrence_count")) if repair_available else None,
                        self._nullable_int(cv("stateTransitionCount", "state_transition_count")), self._nullable_int(cv("stateOscillationCount", "state_oscillation_count")),
                        self._nullable_int(cv("falseStaticCount", "false_static_count")) if boundary_available else None,
                        self._nullable_float(cv("falseStaticRate", "false_static_rate")) if boundary_available else None,
                        self._nullable_float(cv("staticDynamicBoundaryMeanErrorMm", "static_dynamic_boundary_mean_error_mm")) if boundary_available else None,
                        self._nullable_float(cv("staticDynamicBoundaryMaxErrorMm", "static_dynamic_boundary_max_error_mm")) if boundary_available else None,
                        self._nullable_int(cv("fullCpuGroupUploadCount", "full_cpu_group_upload_count")),
                        self._nullable_int(cv("sameFrameReadbackCount", "same_frame_readback_count")),
                        self._nullable_int(cv("nanStateCount", "nan_state_count")), self._nullable_int(cv("infiniteStateCount", "infinite_state_count")),
                        self._nullable_float(cv("blockClassificationGpuMilliseconds", "block_classification_gpu_ms")),
                        self._nullable_float(cv("groupClassificationGpuMilliseconds", "group_classification_gpu_ms")),
                        self._nullable_float(cv("predictiveWakeGpuMilliseconds", "wakePropagationGpuMilliseconds", "predictive_wake_gpu_ms")),
                        self._nullable_float(cv("compactionGpuMilliseconds", "compaction_gpu_ms")),
                        self._nullable_float(cv("staticTransformGpuMilliseconds", "static_transform_gpu_ms")),
                        self._nullable_float(cv("dynamicSolveGpuMilliseconds", "dynamic_solve_gpu_ms")),
                        self._nullable_float(cv("velocityRecoveryGpuMilliseconds", "velocity_recovery_gpu_ms")),
                        self._nullable_float(cv("repairSolveGpuMilliseconds", "repair_solve_gpu_ms")),
                        self._nullable_float(cv("boundaryRepairGpuMilliseconds", "boundary_repair_gpu_ms")),
                        self._nullable_float(cv("cacheUpdateGpuMilliseconds", "cache_update_gpu_ms")),
                        self._nullable_float(cv("watchdogGpuMilliseconds", "watchdog_gpu_ms")),
                        self._nullable_float(cv("followerReconstructionGpuMilliseconds", "follower_reconstruction_gpu_ms")),
                        self._nullable_float(cv("renderPreparationGpuMilliseconds", "render_preparation_gpu_ms")),
                        self._nullable_float(cv("hairDrawGpuMilliseconds", "hair_draw_gpu_ms")),
                        self._nullable_float(cv("telemetryReductionGpuMilliseconds", "telemetry_reduction_gpu_ms")),
                    ]
                    self.connection.execute(
                        f"INSERT INTO runtime_frame_metrics ({','.join(frame_columns)}) VALUES ({','.join('?' for _ in frame_columns)})",
                        frame_values,
                    )
    
        for category, label, artifact in (
            ("telemetry", "summary_json", summary_json_path),
            ("telemetry", "frame_csv", frame_csv_path),
            ("telemetry", "frame_jsonl", frame_jsonl_path),
            ("telemetry", "settings_json", settings_json_path),
            ("telemetry", "environment_json", environment_json_path),
            ("validation", "validation_json", validation_json_path),
        ):
            if artifact and Path(artifact).exists():
                self.add_artifact(run_id, category, label, Path(artifact))
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

    def fetch_artifact_rows(self):
        rows = self.connection.execute(
            "SELECT * FROM run_artifacts ORDER BY run_id, artifact_id"
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_derived_metric_rows(self):
        rows = self.connection.execute(
            "SELECT * FROM derived_metrics ORDER BY scope_type, scope_key, metric_name"
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_user_task_rows(self):
        rows = self.connection.execute(
            """
            SELECT
                utr.*, e.experiment_name
            FROM user_task_results utr
            JOIN experiments e ON e.experiment_id = utr.experiment_id
            ORDER BY utr.created_at, utr.task_result_id
            """
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_user_task_summary_rows(self):
        rows = self.connection.execute(
            """
            SELECT
                e.experiment_name,
                utr.task_id,
                utr.title,
                utr.method_code,
                utr.scenario_code,
                utr.asset_name,
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
        rows = self.connection.execute(
            "SELECT * FROM scenario_coverage ORDER BY suite_name, scenario_code, required_method_code"
        ).fetchall()
        return self._rows_to_dicts(rows)

    def fetch_hypothesis_rows(self):
        rows = self.connection.execute(
            "SELECT * FROM hypothesis_checks ORDER BY suite_name, hypothesis_id, check_id"
        ).fetchall()
        return self._rows_to_dicts(rows)

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
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio,
                AVG(avg_edit_locality) AS avg_edit_locality,
                AVG(avg_boundary_naturalness) AS avg_boundary_naturalness,
                AVG(avg_edit_to_run_latency_ms) AS avg_edit_to_run_latency_ms,
                AVG(avg_penetration_per_sec) AS avg_penetration_per_sec
            FROM runtime_runs
            WHERE is_valid = 1
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
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio,
                AVG(avg_edit_locality) AS avg_edit_locality,
                AVG(avg_boundary_naturalness) AS avg_boundary_naturalness,
                AVG(avg_edit_to_run_latency_ms) AS avg_edit_to_run_latency_ms,
                AVG(avg_penetration_per_sec) AS avg_penetration_per_sec
            FROM runtime_runs
            WHERE is_valid = 1
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
                AVG(avg_simulation_ms) AS avg_simulation_ms,
                AVG(avg_edit_locality) AS avg_edit_locality,
                AVG(avg_boundary_naturalness) AS avg_boundary_naturalness,
                AVG(avg_edit_to_run_latency_ms) AS avg_edit_to_run_latency_ms,
                AVG(avg_penetration_per_sec) AS avg_penetration_per_sec
            FROM runtime_runs
            WHERE is_valid = 1
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
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio,
                AVG(avg_edit_locality) AS avg_edit_locality,
                AVG(avg_boundary_naturalness) AS avg_boundary_naturalness,
                AVG(avg_edit_to_run_latency_ms) AS avg_edit_to_run_latency_ms,
                AVG(avg_penetration_per_sec) AS avg_penetration_per_sec
            FROM runtime_runs
            WHERE is_valid = 1
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
                AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio,
                AVG(avg_edit_locality) AS avg_edit_locality,
                AVG(avg_boundary_naturalness) AS avg_boundary_naturalness,
                AVG(avg_edit_to_run_latency_ms) AS avg_edit_to_run_latency_ms,
                AVG(avg_penetration_per_sec) AS avg_penetration_per_sec
            FROM runtime_runs
            WHERE is_valid = 1
            """
        ).fetchone()
        return dict(row) if row else {}
