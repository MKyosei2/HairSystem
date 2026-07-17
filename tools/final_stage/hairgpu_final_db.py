from __future__ import annotations

import csv
import hashlib
import json
import math
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

# Keep these values aligned with include/hairgpu/HairNoveltyTelemetry.h.
AVAILABILITY_GPU_TIMING = 1 << 2
AVAILABILITY_COLLISION_GROUND_TRUTH = 1 << 6
AVAILABILITY_WAKE_GROUND_TRUTH = 1 << 7
AVAILABILITY_REPAIR_VERIFICATION = 1 << 8
AVAILABILITY_BOUNDARY_COMPARISON = 1 << 9
AVAILABILITY_FALSE_STATIC_GROUND_TRUTH = 1 << 12
AVAILABILITY_PERFORMANCE_ENVIRONMENT = 1 << 13
AVAILABILITY_RESULT_INTEGRITY = 1 << 14


@dataclass(frozen=True)
class RuntimeRunMetadata:
    experiment_name: str
    method_code: str
    scenario_code: str
    asset_name: str
    run_name: str
    runtime_source: str = "dx11_viewer"
    operator_name: str = ""
    notes: str = ""


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


class FinalStageDatabase:
    """SQLite storage for validated HairSystem formal runs.

    Invalid, synthetic, incomplete, hash-mismatched, or identity-mismatched runs
    are rejected before any run row is committed. Missing metrics remain SQL
    NULL and are never converted to a favourable zero.
    """

    _RUN_MIGRATION_COLUMNS: dict[str, str] = {
        "manifest_json_path": "TEXT",
        "observed_shader_bytecode_sha256": "TEXT",
        "result_manifest_sha256": "TEXT",
        "summary_sha256": "TEXT",
        "frame_csv_sha256": "TEXT",
        "frame_jsonl_sha256": "TEXT",
        "settings_json_sha256": "TEXT",
        "environment_json_sha256": "TEXT",
        "validation_json_sha256": "TEXT",
        "state_sequence_sha256": "TEXT",
        "compaction_sequence_sha256": "TEXT",
        "final_position_sha256": "TEXT",
        "thermal_valid": "INTEGER",
        "environment_valid": "INTEGER",
        "required_metric_coverage_valid": "INTEGER",
        "requested_guide_count": "INTEGER",
        "actual_guide_count": "INTEGER",
        "requested_particles_per_guide": "INTEGER",
        "actual_particles_per_guide": "INTEGER",
        "requested_solver_iterations": "INTEGER",
        "actual_solver_iterations": "INTEGER",
        "p50_frame_ms": "REAL",
        "p99_frame_ms": "REAL",
        "avg_hair_gpu_ms": "REAL",
        "p95_hair_gpu_ms": "REAL",
        "p99_hair_gpu_ms": "REAL",
        "false_static_rate": "REAL",
        "predictive_wake_precision": "REAL",
        "predictive_wake_recall": "REAL",
        "repair_execution_success_rate": "REAL",
        "repair_verified_success_rate": "REAL",
        "repair_recurrence_rate": "REAL",
        "collision_miss_rate": "REAL",
        "state_oscillation_rate": "REAL",
        "max_boundary_error_mm": "REAL",
        "thermal_evidence_available": "INTEGER",
        "ground_truth_valid": "INTEGER",
        "determinism_valid": "INTEGER",
        "long_run_valid": "INTEGER",
        "peak_vram_mb": "REAL",
        "vram_slope_mb_per_min": "REAL",
        "ram_slope_mb_per_min": "REAL",
        "p99_degradation_ratio": "REAL",
        "query_unresolved_slope": "REAL",
    }

    _FRAME_MIGRATION_COLUMNS: dict[str, str] = {
        "availability_mask": "INTEGER",
        "total_segment_group_count": "INTEGER",
        "static_cached_group_count": "INTEGER",
        "static_transformed_group_count": "INTEGER",
        "dynamic_group_count": "INTEGER",
        "deferred_group_count": "INTEGER",
        "repair_group_count": "INTEGER",
        "fallback_group_count": "INTEGER",
        "predictive_wake_precision": "REAL",
        "predictive_wake_recall": "REAL",
        "repair_execution_success_count": "INTEGER",
        "repair_verified_count": "INTEGER",
        "repair_unverified_count": "INTEGER",
        "repair_execution_success_rate": "REAL",
        "repair_verified_success_rate": "REAL",
        "repair_recurrence_rate": "REAL",
        "state_oscillation_rate": "REAL",
        "shared_point_conflict_count": "INTEGER",
        "cache_hit_count": "INTEGER",
        "cache_miss_count": "INTEGER",
        "cache_invalidation_count": "INTEGER",
        "dynamic_compaction_input_count": "INTEGER",
        "dynamic_compaction_output_count": "INTEGER",
        "repair_compaction_output_count": "INTEGER",
        "wake_propagation_gpu_ms": "REAL",
        "total_hair_gpu_ms": "REAL",
        "penetration_event_count": "INTEGER",
        "mean_penetration_mm": "REAL",
        "max_penetration_mm": "REAL",
        "determinism_mismatch_count": "INTEGER",
        "state_sequence_hash": "TEXT",
        "compaction_sequence_hash": "TEXT",
        "final_position_hash": "TEXT",
    }

    def __init__(self, db_path: Path):
        self.db_path = Path(db_path)
        self.db_path.parent.mkdir(parents=True, exist_ok=True)
        self.connection = sqlite3.connect(str(self.db_path))
        self.connection.row_factory = sqlite3.Row
        self.connection.execute("PRAGMA foreign_keys = ON")

    def close(self) -> None:
        self.connection.close()

    def __enter__(self) -> "FinalStageDatabase":
        return self

    def __exit__(self, exc_type, exc, traceback) -> None:
        self.close()

    def initialize(self, schema_path: Path) -> None:
        sql = Path(schema_path).read_text(encoding="utf-8")
        with self.connection:
            self.connection.executescript(sql)
            self._ensure_columns("runtime_runs", self._RUN_MIGRATION_COLUMNS)
            self._ensure_columns("runtime_frame_metrics", self._FRAME_MIGRATION_COLUMNS)

    def _ensure_columns(self, table: str, definitions: dict[str, str]) -> None:
        existing = {
            str(row["name"])
            for row in self.connection.execute(f"PRAGMA table_info({table})").fetchall()
        }
        for name, sql_type in definitions.items():
            if name not in existing:
                self.connection.execute(f"ALTER TABLE {table} ADD COLUMN {name} {sql_type}")

    @staticmethod
    def file_sha256(path: Path) -> str:
        digest = hashlib.sha256()
        with Path(path).open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()

    @staticmethod
    def _read_json(path: Path) -> dict[str, Any]:
        payload = json.loads(Path(path).read_text(encoding="utf-8-sig"))
        if not isinstance(payload, dict):
            raise RuntimeError(f"Expected a JSON object: {path}")
        return payload

    @staticmethod
    def _nullable_float(value: Any) -> float | None:
        if value in (None, "", "null") or isinstance(value, bool):
            return None
        try:
            parsed = float(value)
        except (TypeError, ValueError):
            return None
        return parsed if math.isfinite(parsed) else None

    @staticmethod
    def _nullable_int(value: Any) -> int | None:
        parsed = FinalStageDatabase._nullable_float(value)
        return int(parsed) if parsed is not None else None

    @staticmethod
    def _pick(payload: dict[str, Any], *keys: str, default: Any = None) -> Any:
        for key in keys:
            if key in payload and payload[key] is not None:
                return payload[key]
        return default

    @staticmethod
    def _nested(payload: dict[str, Any], dotted_key: str, default: Any = None) -> Any:
        current: Any = payload
        for part in dotted_key.split("."):
            if not isinstance(current, dict) or part not in current:
                return default
            current = current[part]
        return current

    @staticmethod
    def _csv_value(row: dict[str, Any], *keys: str) -> Any:
        for key in keys:
            value = row.get(key)
            if value not in (None, ""):
                return value
        return None

    @staticmethod
    def _as_bool(value: Any) -> bool:
        if isinstance(value, bool):
            return value
        return str(value).strip().lower() in {"1", "true", "yes", "on"}

    def upsert_experiment(self, experiment_name: str, operator_name: str, notes: str) -> int:
        row = self.connection.execute(
            "SELECT experiment_id FROM experiments WHERE experiment_name = ? "
            "ORDER BY experiment_id DESC LIMIT 1",
            (experiment_name,),
        ).fetchone()
        if row:
            return int(row["experiment_id"])
        cursor = self.connection.execute(
            "INSERT INTO experiments "
            "(experiment_name, created_at, operator_name, notes) VALUES (?, ?, ?, ?)",
            (experiment_name, _utc_now(), operator_name, notes),
        )
        return int(cursor.lastrowid)

    def _verify_manifest(
        self,
        manifest_path: Path,
        required_paths: Iterable[Path],
        expected_manifest_sha256: str | None,
    ) -> dict[str, Any]:
        manifest_path = Path(manifest_path)
        if not manifest_path.is_file():
            raise RuntimeError(f"Artifact manifest is missing: {manifest_path}")
        manifest = self._read_json(manifest_path)
        if expected_manifest_sha256:
            observed_manifest_sha = self.file_sha256(manifest_path)
            # resultManifestSha256 may be either the physical file hash or the
            # canonical manifest hash. Accept only an explicit match to one.
            canonical_hash = str(manifest.get("manifestSha256") or "")
            if expected_manifest_sha256.lower() not in {
                observed_manifest_sha.lower(), canonical_hash.lower()
            }:
                raise RuntimeError("Artifact manifest hash does not match the summary")

        entries = manifest.get("artifacts")
        if not isinstance(entries, list):
            raise RuntimeError("Artifact manifest has no artifacts array")
        by_name = {
            str(entry.get("path")): entry
            for entry in entries
            if isinstance(entry, dict) and entry.get("path")
        }
        for path in required_paths:
            path = Path(path)
            if not path.is_file():
                raise RuntimeError(f"Required artifact is missing: {path}")
            entry = by_name.get(path.name)
            if not entry:
                raise RuntimeError(f"Required artifact is absent from manifest: {path.name}")
            expected = str(entry.get("sha256") or "").lower()
            observed = self.file_sha256(path).lower()
            if not expected or expected != observed:
                raise RuntimeError(f"Artifact hash mismatch: {path.name}")
            expected_size = self._nullable_int(entry.get("sizeBytes"))
            if expected_size is None or expected_size != path.stat().st_size:
                raise RuntimeError(f"Artifact size mismatch: {path.name}")
        return manifest

    def add_artifact(
        self,
        run_id: int,
        category: str,
        label: str,
        artifact_path: Path,
        *,
        commit: bool = True,
    ) -> None:
        artifact_path = Path(artifact_path)
        sha256 = self.file_sha256(artifact_path) if artifact_path.is_file() else None
        size_bytes = artifact_path.stat().st_size if artifact_path.is_file() else None
        self.connection.execute(
            "INSERT INTO run_artifacts "
            "(run_id, category, label, artifact_path, sha256, size_bytes, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            (
                run_id,
                category,
                label,
                str(artifact_path),
                sha256,
                size_bytes,
                _utc_now(),
            ),
        )
        if commit:
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
            "INSERT INTO derived_metrics "
            "(run_id, scope_type, scope_key, metric_name, metric_value, units, notes, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            (
                run_id,
                scope_type,
                scope_key,
                metric_name,
                float(metric_value) if metric_value is not None else None,
                units,
                notes,
                _utc_now(),
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
        with self.connection:
            experiment_id = self.upsert_experiment(experiment_name, "", notes)
            self.connection.execute(
                "INSERT INTO user_task_results "
                "(experiment_id, task_id, title, method_code, scenario_code, asset_name, "
                "participant_id, completion_seconds, redo_count, unintended_change_score, "
                "satisfaction_score, notes, created_at) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    experiment_id,
                    task_id,
                    title,
                    method_code,
                    scenario_code,
                    asset_name,
                    participant_id,
                    completion_seconds,
                    redo_count,
                    unintended_change_score,
                    satisfaction_score,
                    notes,
                    _utc_now(),
                ),
            )

    def clear_suite_rows(self, suite_name: str) -> None:
        with self.connection:
            self.connection.execute(
                "DELETE FROM hypothesis_checks WHERE suite_name = ?", (suite_name,)
            )
            self.connection.execute(
                "DELETE FROM scenario_coverage WHERE suite_name = ?", (suite_name,)
            )

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
            "INSERT INTO scenario_coverage "
            "(suite_name, scenario_code, category, required_method_code, is_present, notes, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?)",
            (
                suite_name,
                scenario_code,
                category,
                required_method_code,
                int(is_present),
                notes,
                _utc_now(),
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
            "INSERT INTO hypothesis_checks "
            "(suite_name, hypothesis_id, title, source_type, scenario_code, task_id, "
            "method_code, baseline_method_code, metric_name, actual_value, threshold_value, "
            "comparator, passed, notes, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
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
                actual_value,
                threshold_value,
                comparator,
                int(passed),
                notes,
                _utc_now(),
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
        manifest_json_path: Path | None = None,
    ) -> int:
        summary_json_path = Path(summary_json_path)
        frame_csv_path = Path(frame_csv_path)
        frame_jsonl_path = Path(frame_jsonl_path) if frame_jsonl_path else None
        settings_json_path = Path(settings_json_path) if settings_json_path else None
        environment_json_path = Path(environment_json_path) if environment_json_path else None
        validation_json_path = Path(validation_json_path) if validation_json_path else None

        required_files = [summary_json_path, frame_csv_path]
        required_files.extend(
            path
            for path in (frame_jsonl_path, settings_json_path, environment_json_path, validation_json_path)
            if path is not None
        )
        for path in required_files:
            if not path.is_file():
                raise RuntimeError(f"Required formal artifact is missing: {path}")

        summary = self._read_json(summary_json_path)
        settings = self._read_json(settings_json_path) if settings_json_path else {}
        validation = self._read_json(validation_json_path) if validation_json_path else {}
        environment = self._read_json(environment_json_path) if environment_json_path else {}

        status = str(
            summary.get("validationStatus")
            or summary.get("status")
            or validation.get("status")
            or "incomplete"
        ).strip().lower()
        if status != "complete_valid" or not self._as_bool(summary.get("realMeasurement")):
            reasons = summary.get("validationReasons") or validation.get("issues") or []
            raise RuntimeError(
                f"Run '{metadata.run_name}' is not importable: status={status}, reasons={reasons}"
            )
        if validation and not self._as_bool(validation.get("isValid")):
            raise RuntimeError(f"Run '{metadata.run_name}' has an invalid validation artifact")

        requested_code = str(summary.get("requestedMethodCode") or "").strip()
        applied_code = str(summary.get("appliedMethodCode") or "").strip()
        observed_code = str(summary.get("observedMethodCode") or "").strip()
        if not requested_code or not applied_code or not observed_code:
            raise RuntimeError("Requested/applied/observed method identity must all be present")
        if len({requested_code, applied_code, observed_code, metadata.method_code}) != 1:
            raise RuntimeError(
                "Method identity mismatch: "
                f"requested={requested_code}, applied={applied_code}, "
                f"observed={observed_code}, metadata={metadata.method_code}"
            )

        manifest_json_path = Path(manifest_json_path) if manifest_json_path else None
        if manifest_json_path is None:
            manifest_name = str(summary.get("resultManifestPath") or f"{metadata.run_name}_manifest.json")
            manifest_json_path = summary_json_path.parent / manifest_name
        self._verify_manifest(
            manifest_json_path,
            required_files,
            str(summary.get("resultManifestSha256") or "") or None,
        )
        manifest_physical_sha256 = self.file_sha256(manifest_json_path)

        requested = settings.get("requested") if isinstance(settings.get("requested"), dict) else {}
        applied = settings.get("applied") if isinstance(settings.get("applied"), dict) else {}
        observed = settings.get("observed") if isinstance(settings.get("observed"), dict) else {}
        if not requested or not applied or not observed:
            raise RuntimeError("Requested/applied/observed settings are required for a formal run")

        fatal_count = self._nullable_int(summary.get("fatalRuntimeFailureCount")) or 0
        synthetic_count = self._nullable_int(summary.get("syntheticMetricCount")) or 0
        if fatal_count or synthetic_count:
            raise RuntimeError("Fatal or synthetic metrics cannot enter the formal database")

        sv = lambda *keys, default=None: self._pick(summary, *keys, default=default)
        env_start = environment.get("start", {}) if isinstance(environment.get("start"), dict) else {}
        env_end = environment.get("end", {}) if isinstance(environment.get("end"), dict) else {}
        gpu = env_start.get("gpu", {}) if isinstance(env_start.get("gpu"), dict) else {}
        if not gpu:
            gpu = env_end.get("gpu", {}) if isinstance(env_end.get("gpu"), dict) else {}
        viewer_adapter = environment.get("viewerAdapter", {}) if isinstance(environment.get("viewerAdapter"), dict) else {}
        power = env_start.get("power", {}) if isinstance(env_start.get("power"), dict) else {}

        run_values: dict[str, Any] = {
            "experiment_id": None,
            "run_name": metadata.run_name,
            "method_code": metadata.method_code,
            "scenario_code": metadata.scenario_code,
            "asset_name": metadata.asset_name,
            "runtime_source": metadata.runtime_source,
            "summary_json_path": str(summary_json_path),
            "frame_csv_path": str(frame_csv_path),
            "frame_jsonl_path": str(frame_jsonl_path) if frame_jsonl_path else None,
            "settings_json_path": str(settings_json_path) if settings_json_path else None,
            "environment_json_path": str(environment_json_path) if environment_json_path else None,
            "validation_json_path": str(validation_json_path) if validation_json_path else None,
            "manifest_json_path": str(manifest_json_path),
            "validation_status": status,
            "is_valid": 1,
            "invalid_reason_json": json.dumps([], ensure_ascii=False),
            "real_measurement": 1,
            "requested_method_code": requested_code,
            "applied_method_code": applied_code,
            "observed_method_code": observed_code,
            "measurement_mode": sv("measurementMode", "measurement_mode"),
            "seed": self._nullable_int(sv("seed")),
            "repetition_index": self._nullable_int(sv("repetitionIndex", "repeat_index")),
            "commit_sha": sv("commitSha", "commit_sha"),
            "executable_sha256": sv("executableSha256"),
            "shader_bundle_sha256": sv("shaderBundleSha256"),
            "observed_shader_bytecode_sha256": sv("observedShaderBytecodeSha256"),
            "asset_sha256": sv("assetSha256"),
            "asset_manifest_sha256": sv("assetManifestSha256"),
            "settings_sha256": sv("settingsSha256", "settings_hash"),
            "scenario_timeline_sha256": sv("scenarioTimelineSha256"),
            "method_settings_sha256": sv("appliedMethodSettingsSha256", "methodSettingsSha256"),
            "canonical_result_sha256": sv("canonicalResultSha256"),
            "result_manifest_sha256": sv("resultManifestSha256") or manifest_physical_sha256,
            "summary_sha256": self.file_sha256(summary_json_path),
            "frame_csv_sha256": self.file_sha256(frame_csv_path),
            "frame_jsonl_sha256": self.file_sha256(frame_jsonl_path) if frame_jsonl_path else None,
            "settings_json_sha256": self.file_sha256(settings_json_path) if settings_json_path else None,
            "environment_json_sha256": self.file_sha256(environment_json_path) if environment_json_path else None,
            "validation_json_sha256": self.file_sha256(validation_json_path) if validation_json_path else None,
            "decision_sequence_sha256": sv("decisionSequenceSha256"),
            "state_sequence_sha256": str(sv("stateSequenceHash", "stateSequenceSha256") or ""),
            "compaction_sequence_sha256": str(sv("compactionSequenceHash", "compactionSequenceSha256") or ""),
            "final_position_sha256": str(sv("finalPositionHash", "finalPositionSha256") or ""),
            "hardware_profile": sv("hardwareProfile", "hardware_profile"),
            "gpu_name": sv("gpuName", default=gpu.get("name") or viewer_adapter.get("name")),
            "gpu_driver": sv("gpuDriver", default=gpu.get("driverVersion") or viewer_adapter.get("driverVersion")),
            "gpu_vendor_id": self._nullable_int(sv("gpuVendorId", default=viewer_adapter.get("vendorId"))),
            "gpu_device_id": self._nullable_int(sv("gpuDeviceId", default=viewer_adapter.get("deviceId"))),
            "dedicated_vram_mb": self._nullable_float(sv("dedicatedVramMb", default=gpu.get("memoryTotalMiB") or viewer_adapter.get("dedicatedVideoMemoryMiB"))),
            "cpu_name": sv("cpuName", default=env_start.get("processor")),
            "power_scheme": sv("powerScheme", default=power.get("activeScheme")),
            "thermal_valid": int(self._as_bool(validation.get("thermalValid", True))),
            "environment_valid": int(self._as_bool(validation.get("environmentValid", True))),
            "required_metric_coverage_valid": int(self._as_bool(validation.get("requiredMetricCoverageValid", True))),
            "thermal_evidence_available": int(not any(
                str(item.get("code", "")).startswith("E_THERMAL_EVIDENCE")
                for item in validation.get("issues", []) if isinstance(item, dict)
            )),
            "ground_truth_valid": int(
                str(sv("measurementMode", "measurement_mode", default="")).lower() != "quality"
                or all(sv(key) is not None for key in ("falseStaticRate", "collisionMissRate"))
            ),
            "determinism_valid": int(
                str(sv("measurementMode", "measurement_mode", default="")).lower() != "determinism"
                or (self._nullable_int(sv("determinismMismatchCount")) in (None, 0)
                    and bool(sv("stateSequenceHash")) and bool(sv("compactionSequenceHash"))
                    and bool(sv("finalPositionHash")))
            ),
            "long_run_valid": int(self._as_bool(sv("longRunValid", default=True))),
            "peak_vram_mb": self._nullable_float(sv("peakVramMb", "peak_vram_mb")),
            "vram_slope_mb_per_min": self._nullable_float(sv("vramSlopeMbPerMin", "vram_slope_mb_per_min")),
            "ram_slope_mb_per_min": self._nullable_float(sv("ramSlopeMbPerMin", "ram_slope_mb_per_min")),
            "p99_degradation_ratio": self._nullable_float(sv("p99DegradationRatio", "p99_degradation_ratio")),
            "query_unresolved_slope": self._nullable_float(sv("queryUnresolvedSlope", "query_unresolved_slope")),
            "requested_render_width": self._nullable_int(sv("requestedRenderWidth", default=requested.get("renderWidth", requested.get("width")))),
            "requested_render_height": self._nullable_int(sv("requestedRenderHeight", default=requested.get("renderHeight", requested.get("height")))),
            "actual_render_width": self._nullable_int(sv("actualRenderWidth", default=observed.get("renderWidth"))),
            "actual_render_height": self._nullable_int(sv("actualRenderHeight", default=observed.get("renderHeight"))),
            "requested_guide_count": self._nullable_int(sv("requestedGuideCount", default=requested.get("guideCount"))),
            "actual_guide_count": self._nullable_int(sv("actualGuideCount", default=observed.get("guideCount"))),
            "requested_display_strand_count": self._nullable_int(sv("requestedDisplayStrandCount", default=requested.get("displayStrandCount"))),
            "actual_display_strand_count": self._nullable_int(sv("actualDisplayStrandCount", "totalStrandCount", default=observed.get("displayStrandCount"))),
            "requested_particles_per_guide": self._nullable_int(sv("requestedParticlesPerGuide", default=requested.get("particlesPerGuide"))),
            "actual_particles_per_guide": self._nullable_int(sv("actualParticlesPerGuide", default=observed.get("particlesPerGuide"))),
            "requested_solver_iterations": self._nullable_int(sv("requestedSolverIterations", default=requested.get("solverIterations"))),
            "actual_solver_iterations": self._nullable_int(sv("actualSolverIterations", default=observed.get("solverIterations"))),
            "sample_count": self._nullable_int(sv("sample_count", "sampleCount")),
            "avg_frame_ms": self._nullable_float(sv("avg_frame_ms", "avgFrameMs", "averageFrameMilliseconds")),
            "p50_frame_ms": self._nullable_float(sv("p50_frame_ms", "p50FrameMs")),
            "p95_frame_ms": self._nullable_float(sv("p95_frame_ms", "p95FrameMs")),
            "p99_frame_ms": self._nullable_float(sv("p99_frame_ms", "p99FrameMs")),
            "p999_frame_ms": self._nullable_float(sv("p999_frame_ms", "p999FrameMs")),
            "worst_frame_ms": self._nullable_float(sv("worst_frame_ms", "worstFrameMs")),
            "avg_simulation_ms": self._nullable_float(sv("avg_simulation_ms", "avgSimulationMs")),
            "avg_hair_gpu_ms": self._nullable_float(sv("avg_hair_gpu_ms", "avgHairGpuMs")),
            "p95_hair_gpu_ms": self._nullable_float(sv("p95_hair_gpu_ms", "p95HairGpuMs")),
            "p99_hair_gpu_ms": self._nullable_float(sv("p99_hair_gpu_ms", "p99HairGpuMs")),
            "avg_fps": self._nullable_float(sv("avg_fps", "avgFps")),
            "sixty_fps_ratio": self._nullable_float(sv("sixty_fps_ratio", "sixtyFpsRatio")),
            "frame_overrun_rate": self._nullable_float(sv("frameOverrunRate", "frame_overrun_rate")),
            "guide_count": self._nullable_int(sv("guide_count", "guideCount")),
            "follower_count": self._nullable_int(sv("follower_count", "followerCount")),
            "total_strand_count": self._nullable_int(sv("total_strand_count", "totalStrandCount")),
            "total_particle_count": self._nullable_int(sv("total_particle_count", "totalParticleCount")),
            "false_static_rate": self._nullable_float(sv("falseStaticRate")),
            "predictive_wake_precision": self._nullable_float(sv("predictiveWakePrecision")),
            "predictive_wake_recall": self._nullable_float(sv("predictiveWakeRecall")),
            "repair_execution_success_rate": self._nullable_float(sv("repairExecutionSuccessRate")),
            "repair_verified_success_rate": self._nullable_float(sv("repairVerifiedSuccessRate", "repairSuccessRate")),
            "repair_recurrence_rate": self._nullable_float(sv("repairRecurrenceRate")),
            "collision_miss_rate": self._nullable_float(sv("collisionMissRate")),
            "state_oscillation_rate": self._nullable_float(sv("stateOscillationRate")),
            "max_boundary_error_mm": self._nullable_float(sv("staticDynamicBoundaryMaxErrorMm")),
            "avg_silhouette_error": self._nullable_float(sv("avg_silhouette_error", "averageSilhouetteError")),
            "avg_clump_preservation": self._nullable_float(sv("avg_clump_preservation", "averageClumpPreservation")),
            "avg_edit_locality": self._nullable_float(sv("avg_edit_locality", "averageEditLocality")),
            "avg_boundary_naturalness": self._nullable_float(sv("avg_boundary_naturalness", "averageBoundaryNaturalness")),
            "avg_edit_to_run_latency_ms": self._nullable_float(sv("avg_edit_to_run_latency_ms", "averageEditToRunLatencyMs")),
            "avg_penetration_per_sec": self._nullable_float(sv("avg_penetration_per_sec", "avgPenetrationPerSec")),
            "peak_ram_mb": self._nullable_float(sv("peak_ram_mb", "peakRamMb")),
            "gpu_timing_coverage": self._nullable_float(sv("gpuTimingCoverage")),
            "counter_coverage": self._nullable_float(sv("counterCoverage")),
            "fatal_runtime_failure_count": fatal_count,
            "nan_count": self._nullable_int(sv("nanCount", "nanStateCount")),
            "inf_count": self._nullable_int(sv("infCount", "infiniteStateCount")),
            "full_cpu_upload_count": self._nullable_int(sv("fullCpuUploadCount", "fullCpuGroupUploadCount")),
            "same_frame_readback_count": self._nullable_int(sv("sameFrameReadbackCount")),
            "synthetic_metric_count": synthetic_count,
            "unavailable_metric_count": self._nullable_int(sv("unavailableMetricCount")),
            "created_at": _utc_now(),
        }

        frame_rows: list[dict[str, Any]] = []
        with frame_csv_path.open("r", encoding="utf-8-sig", newline="") as handle:
            reader = csv.DictReader(handle)
            if not reader.fieldnames:
                raise RuntimeError("Frame CSV has no header")
            for row in reader:
                frame_rows.append(self._frame_record(row))
        if not frame_rows:
            raise RuntimeError("Frame CSV has no data rows")

        expected_count = run_values.get("sample_count")
        if expected_count is not None and expected_count != len(frame_rows):
            raise RuntimeError(
                f"Frame count mismatch: summary={expected_count}, csv={len(frame_rows)}"
            )

        with self.connection:
            experiment_id = self.upsert_experiment(
                metadata.experiment_name, metadata.operator_name, metadata.notes
            )
            run_values["experiment_id"] = experiment_id
            columns = list(run_values)
            cursor = self.connection.execute(
                f"INSERT INTO runtime_runs ({','.join(columns)}) "
                f"VALUES ({','.join('?' for _ in columns)})",
                [run_values[column] for column in columns],
            )
            run_id = int(cursor.lastrowid)

            for frame in frame_rows:
                frame["run_id"] = run_id
                frame_columns = list(frame)
                self.connection.execute(
                    f"INSERT INTO runtime_frame_metrics ({','.join(frame_columns)}) "
                    f"VALUES ({','.join('?' for _ in frame_columns)})",
                    [frame[column] for column in frame_columns],
                )

            for category, label, artifact in (
                ("telemetry", "summary_json", summary_json_path),
                ("telemetry", "frame_csv", frame_csv_path),
                ("telemetry", "frame_jsonl", frame_jsonl_path),
                ("telemetry", "settings_json", settings_json_path),
                ("telemetry", "environment_json", environment_json_path),
                ("validation", "validation_json", validation_json_path),
                ("integrity", "manifest_json", manifest_json_path),
            ):
                if artifact and Path(artifact).is_file():
                    self.add_artifact(run_id, category, label, Path(artifact), commit=False)
        return run_id

    def _frame_record(self, row: dict[str, Any]) -> dict[str, Any]:
        cv = lambda *keys: self._csv_value(row, *keys)
        availability = self._nullable_int(
            cv("availabilityMask", "availability_mask")
        ) or 0
        metric_availability = self._nullable_int(
            cv("metricAvailabilityMask", "metric_availability_mask")
        ) or availability
        collision_available = bool(
            metric_availability & AVAILABILITY_COLLISION_GROUND_TRUTH
        )
        wake_available = bool(metric_availability & AVAILABILITY_WAKE_GROUND_TRUTH)
        repair_available = bool(
            metric_availability & AVAILABILITY_REPAIR_VERIFICATION
        )
        boundary_available = bool(
            metric_availability & AVAILABILITY_BOUNDARY_COMPARISON
        )
        false_static_available = bool(
            metric_availability & AVAILABILITY_FALSE_STATIC_GROUND_TRUTH
        )

        optional_int = self._nullable_int
        optional_float = self._nullable_float
        result: dict[str, Any] = {
            "run_id": None,
            "sample_index": optional_int(cv("sampleIndex", "sample_index")),
            "source_frame_index": optional_int(cv("sourceFrameIndex", "source_frame_index")),
            "elapsed_seconds": optional_float(cv("elapsedSeconds", "elapsed_seconds")),
            "frame_ms": optional_float(cv("frameMilliseconds", "frame_ms")),
            "frame_interval_ms": optional_float(cv("frameIntervalMilliseconds", "frame_interval_ms")),
            "cpu_frame_work_ms": optional_float(cv("cpuFrameWorkMilliseconds", "cpu_frame_work_ms")),
            "simulation_ms": optional_float(cv("simulationMilliseconds", "simulation_ms")),
            "fps": optional_float(cv("fps")),
            "guide_count": optional_int(cv("guideCount", "guide_count")),
            "follower_count": optional_int(cv("followerCount", "follower_count")),
            "total_strand_count": optional_int(cv("totalStrandCount", "total_strand_count")),
            "total_particle_count": optional_int(cv("totalParticleCount", "total_particle_count")),
            "availability_mask": availability,
            "metric_availability_mask": metric_availability,
            "unavailable_metric_count": optional_int(cv("unavailableMetricCount", "unavailable_metric_count")),
            "synthetic_metric_count": optional_int(cv("syntheticMetricCount", "synthetic_metric_count")),
            "fatal_failure": optional_int(cv("fatalFailure", "fatal_failure")),
            "fatal_failure_code": optional_int(cv("fatalFailureCode", "fatal_failure_code")),
            "total_segment_group_count": optional_int(cv("totalSegmentGroupCount", "total_segment_group_count")),
            "static_cached_group_count": optional_int(cv("staticCachedGroupCount", "static_cached_group_count")),
            "static_transformed_group_count": optional_int(cv("staticTransformedGroupCount", "static_transformed_group_count")),
            "dynamic_group_count": optional_int(cv("dynamicGroupCount", "dynamic_group_count")),
            "deferred_group_count": optional_int(cv("deferredCorrectionGroupCount", "deferred_group_count")),
            "repair_group_count": optional_int(cv("repairRequiredGroupCount", "repair_group_count")),
            "fallback_group_count": optional_int(cv("fallbackGroupCount", "fallback_group_count")),
            "collision_candidate_count": optional_int(cv("collisionCandidateCount", "collision_candidate_count")),
            "collision_miss_count": optional_int(cv("collisionMissCount", "collision_miss_count")) if collision_available else None,
            "collision_miss_rate": optional_float(cv("collisionMissRate", "collision_miss_rate")) if collision_available else None,
            "penetration_event_count": optional_int(cv("penetrationEventCount", "penetration_event_count")),
            "mean_penetration_mm": optional_float(cv("meanPenetrationMm", "mean_penetration_mm")),
            "max_penetration_mm": optional_float(cv("maxPenetrationMm", "max_penetration_mm")),
            "predictive_wake_tp": optional_int(cv("predictiveWakeTruePositiveCount", "predictive_wake_tp")) if wake_available else None,
            "predictive_wake_fp": optional_int(cv("predictiveWakeFalsePositiveCount", "predictive_wake_fp")) if wake_available else None,
            "predictive_wake_fn": optional_int(cv("predictiveWakeFalseNegativeCount", "predictive_wake_fn")) if wake_available else None,
            "predictive_wake_precision": optional_float(cv("predictiveWakePrecision", "predictive_wake_precision")) if wake_available else None,
            "predictive_wake_recall": optional_float(cv("predictiveWakeRecall", "predictive_wake_recall")) if wake_available else None,
            "wake_delay_frame_max": optional_int(cv("wakeDelayFrameMax", "wake_delay_frame_max")) if wake_available else None,
            "repair_attempt_count": optional_int(cv("repairAttemptCount", "repair_attempt_count")),
            "repair_execution_success_count": optional_int(cv("repairSuccessCount", "repairExecutionSuccessCount", "repair_execution_success_count")) if repair_available else None,
            "repair_verified_count": optional_int(cv("repairVerifiedCount", "repair_verified_count")) if repair_available else None,
            "repair_unverified_count": optional_int(cv("repairUnverifiedCount", "repair_unverified_count")) if repair_available else None,
            "repair_failure_count": optional_int(cv("repairFailureCount", "repair_failure_count")) if repair_available else None,
            "repair_recurrence_count": optional_int(cv("repairRecurrenceCount", "repair_recurrence_count")) if repair_available else None,
            "repair_execution_success_rate": optional_float(cv("repairExecutionSuccessRate", "repair_execution_success_rate")) if repair_available else None,
            "repair_verified_success_rate": optional_float(cv("repairVerifiedSuccessRate", "repair_verified_success_rate")) if repair_available else None,
            "repair_recurrence_rate": optional_float(cv("repairRecurrenceRate", "repair_recurrence_rate")) if repair_available else None,
            "state_transition_count": optional_int(cv("stateTransitionCount", "state_transition_count")),
            "state_oscillation_count": optional_int(cv("stateOscillationCount", "state_oscillation_count")),
            "state_oscillation_rate": optional_float(cv("stateOscillationRate", "state_oscillation_rate")),
            "false_static_count": optional_int(cv("falseStaticCount", "false_static_count")) if false_static_available else None,
            "false_static_rate": optional_float(cv("falseStaticRate", "false_static_rate")) if false_static_available else None,
            "static_dynamic_boundary_mean_error_mm": optional_float(cv("staticDynamicBoundaryMeanErrorMm", "static_dynamic_boundary_mean_error_mm")) if boundary_available else None,
            "static_dynamic_boundary_max_error_mm": optional_float(cv("staticDynamicBoundaryMaxErrorMm", "static_dynamic_boundary_max_error_mm")) if boundary_available else None,
            "shared_point_conflict_count": optional_int(cv("sharedPointConflictCount", "shared_point_conflict_count")) if boundary_available else None,
            "cache_hit_count": optional_int(cv("cacheHitCount", "cache_hit_count")),
            "cache_miss_count": optional_int(cv("cacheMissCount", "cache_miss_count")),
            "cache_invalidation_count": optional_int(cv("cacheInvalidationCount", "cache_invalidation_count")),
            "dynamic_compaction_input_count": optional_int(cv("dynamicCompactionInputCount", "dynamic_compaction_input_count")),
            "dynamic_compaction_output_count": optional_int(cv("dynamicCompactionOutputCount", "dynamic_compaction_output_count")),
            "repair_compaction_output_count": optional_int(cv("repairCompactionOutputCount", "repair_compaction_output_count")),
            "full_cpu_group_upload_count": optional_int(cv("fullCpuGroupUploadCount", "full_cpu_group_upload_count")),
            "same_frame_readback_count": optional_int(cv("sameFrameReadbackCount", "same_frame_readback_count")),
            "nan_state_count": optional_int(cv("nanStateCount", "nan_state_count")),
            "infinite_state_count": optional_int(cv("infiniteStateCount", "infinite_state_count")),
            "block_classification_gpu_ms": optional_float(cv("blockClassificationGpuMilliseconds", "block_classification_gpu_ms")),
            "group_classification_gpu_ms": optional_float(cv("groupClassificationGpuMilliseconds", "group_classification_gpu_ms")),
            "predictive_wake_gpu_ms": optional_float(cv("predictiveWakeGpuMilliseconds", "predictive_wake_gpu_ms")),
            "wake_propagation_gpu_ms": optional_float(cv("wakePropagationGpuMilliseconds", "wake_propagation_gpu_ms")),
            "compaction_gpu_ms": optional_float(cv("compactionGpuMilliseconds", "compaction_gpu_ms")),
            "static_transform_gpu_ms": optional_float(cv("staticTransformGpuMilliseconds", "static_transform_gpu_ms")),
            "dynamic_solve_gpu_ms": optional_float(cv("dynamicSolveGpuMilliseconds", "dynamic_solve_gpu_ms")),
            "velocity_recovery_gpu_ms": optional_float(cv("velocityRecoveryGpuMilliseconds", "velocity_recovery_gpu_ms")),
            "repair_solve_gpu_ms": optional_float(cv("repairSolveGpuMilliseconds", "repair_solve_gpu_ms")),
            "boundary_repair_gpu_ms": optional_float(cv("boundaryRepairGpuMilliseconds", "boundary_repair_gpu_ms")),
            "cache_update_gpu_ms": optional_float(cv("cacheUpdateGpuMilliseconds", "cache_update_gpu_ms")),
            "watchdog_gpu_ms": optional_float(cv("watchdogGpuMilliseconds", "watchdog_gpu_ms")),
            "follower_reconstruction_gpu_ms": optional_float(cv("followerReconstructionGpuMilliseconds", "follower_reconstruction_gpu_ms")),
            "render_preparation_gpu_ms": optional_float(cv("renderPreparationGpuMilliseconds", "render_preparation_gpu_ms")),
            "hair_draw_gpu_ms": optional_float(cv("hairDrawGpuMilliseconds", "hair_draw_gpu_ms")),
            "telemetry_reduction_gpu_ms": optional_float(cv("telemetryReductionGpuMilliseconds", "telemetry_reduction_gpu_ms")),
            "total_hair_gpu_ms": optional_float(cv("totalHairGpuMilliseconds", "gpuHairMilliseconds", "total_hair_gpu_ms")),
            "determinism_mismatch_count": optional_int(cv("determinismMismatchCount", "determinism_mismatch_count")),
            "state_sequence_hash": str(cv("stateSequenceHash", "state_sequence_hash") or "") or None,
            "compaction_sequence_hash": str(cv("compactionSequenceHash", "compaction_sequence_hash") or "") or None,
            "final_position_hash": str(cv("finalPositionHash", "final_position_hash") or "") or None,
        }
        if result["sample_index"] is None:
            raise RuntimeError("Frame CSV contains a row without sampleIndex")
        return result

    @staticmethod
    def _rows_to_dicts(rows: Iterable[sqlite3.Row]) -> list[dict[str, Any]]:
        return [dict(row) for row in rows]

    def fetch_runs(self):
        return self.connection.execute(
            "SELECT * FROM runtime_runs ORDER BY created_at, run_id"
        ).fetchall()

    def fetch_frame_metrics(self):
        return self.connection.execute(
            "SELECT * FROM runtime_frame_metrics ORDER BY run_id, sample_index"
        ).fetchall()

    def fetch_artifact_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(
            self.connection.execute(
                "SELECT * FROM run_artifacts ORDER BY run_id, artifact_id"
            ).fetchall()
        )

    def fetch_derived_metric_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(
            self.connection.execute(
                "SELECT * FROM derived_metrics ORDER BY scope_type, scope_key, metric_name"
            ).fetchall()
        )

    def fetch_user_task_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(
            self.connection.execute(
                "SELECT utr.*, e.experiment_name FROM user_task_results utr "
                "JOIN experiments e ON e.experiment_id = utr.experiment_id "
                "ORDER BY utr.created_at, utr.task_result_id"
            ).fetchall()
        )

    def fetch_user_task_summary_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(
            self.connection.execute(
                "SELECT e.experiment_name, utr.task_id, utr.title, utr.method_code, "
                "utr.scenario_code, utr.asset_name, COUNT(*) AS sample_count, "
                "AVG(utr.completion_seconds) AS avg_completion_seconds, "
                "AVG(utr.redo_count) AS avg_redo_count, "
                "AVG(utr.unintended_change_score) AS avg_unintended_change_score, "
                "AVG(utr.satisfaction_score) AS avg_satisfaction_score "
                "FROM user_task_results utr "
                "JOIN experiments e ON e.experiment_id = utr.experiment_id "
                "GROUP BY e.experiment_name, utr.task_id, utr.title, utr.method_code, "
                "utr.scenario_code, utr.asset_name "
                "ORDER BY utr.task_id, utr.method_code"
            ).fetchall()
        )

    def fetch_scenario_coverage_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(
            self.connection.execute(
                "SELECT * FROM scenario_coverage "
                "ORDER BY suite_name, scenario_code, required_method_code"
            ).fetchall()
        )

    def fetch_hypothesis_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(
            self.connection.execute(
                "SELECT * FROM hypothesis_checks "
                "ORDER BY suite_name, hypothesis_id, check_id"
            ).fetchall()
        )

    def fetch_comparison_rows(self):
        return self.connection.execute(
            "SELECT method_code, scenario_code, COUNT(*) AS run_count, "
            "AVG(avg_frame_ms) AS avg_frame_ms, "
            "AVG(p95_frame_ms) AS avg_p95_frame_ms, "
            "AVG(p99_frame_ms) AS avg_p99_frame_ms, "
            "AVG(worst_frame_ms) AS avg_worst_frame_ms, "
            "AVG(avg_hair_gpu_ms) AS avg_hair_gpu_ms, "
            "AVG(avg_fps) AS avg_fps, "
            "AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio, "
            "AVG(false_static_rate) AS avg_false_static_rate, "
            "AVG(predictive_wake_recall) AS avg_predictive_wake_recall, "
            "AVG(repair_verified_success_rate) AS avg_repair_verified_success_rate, "
            "AVG(collision_miss_rate) AS avg_collision_miss_rate, "
            "AVG(state_oscillation_rate) AS avg_state_oscillation_rate, "
            "AVG(max_boundary_error_mm) AS avg_max_boundary_error_mm, "
            "AVG(avg_edit_locality) AS avg_edit_locality, "
            "AVG(avg_boundary_naturalness) AS avg_boundary_naturalness, "
            "AVG(avg_edit_to_run_latency_ms) AS avg_edit_to_run_latency_ms, "
            "AVG(avg_penetration_per_sec) AS avg_penetration_per_sec "
            "FROM runtime_runs WHERE is_valid = 1 "
            "GROUP BY method_code, scenario_code "
            "ORDER BY method_code, scenario_code"
        ).fetchall()

    def fetch_ablation_rows(self):
        return self.connection.execute(
            "SELECT method_code, COUNT(*) AS run_count, "
            "AVG(avg_frame_ms) AS avg_frame_ms, "
            "AVG(p95_frame_ms) AS avg_p95_frame_ms, "
            "AVG(p99_frame_ms) AS avg_p99_frame_ms, "
            "AVG(avg_hair_gpu_ms) AS avg_hair_gpu_ms, "
            "AVG(avg_fps) AS avg_fps, "
            "AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio, "
            "AVG(false_static_rate) AS avg_false_static_rate, "
            "AVG(predictive_wake_recall) AS avg_predictive_wake_recall, "
            "AVG(repair_verified_success_rate) AS avg_repair_verified_success_rate, "
            "AVG(collision_miss_rate) AS avg_collision_miss_rate "
            "FROM runtime_runs WHERE is_valid = 1 "
            "GROUP BY method_code ORDER BY method_code"
        ).fetchall()

    def fetch_method_summary_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(self.fetch_ablation_rows())

    def fetch_scenario_summary_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(
            self.connection.execute(
                "SELECT scenario_code, COUNT(*) AS run_count, "
                "AVG(avg_frame_ms) AS avg_frame_ms, "
                "AVG(p95_frame_ms) AS avg_p95_frame_ms, "
                "AVG(p99_frame_ms) AS avg_p99_frame_ms, "
                "AVG(worst_frame_ms) AS avg_worst_frame_ms, "
                "AVG(avg_hair_gpu_ms) AS avg_hair_gpu_ms, "
                "AVG(avg_fps) AS avg_fps, "
                "AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio, "
                "AVG(false_static_rate) AS avg_false_static_rate, "
                "AVG(predictive_wake_recall) AS avg_predictive_wake_recall, "
                "AVG(repair_verified_success_rate) AS avg_repair_verified_success_rate, "
                "AVG(collision_miss_rate) AS avg_collision_miss_rate, "
                "AVG(state_oscillation_rate) AS avg_state_oscillation_rate, "
                "AVG(max_boundary_error_mm) AS avg_max_boundary_error_mm "
                "FROM runtime_runs WHERE is_valid = 1 "
                "GROUP BY scenario_code ORDER BY scenario_code"
            ).fetchall()
        )

    def fetch_experiment_summary(self) -> dict[str, Any]:
        row = self.connection.execute(
            "SELECT COUNT(*) AS run_count, "
            "COUNT(DISTINCT method_code) AS method_count, "
            "COUNT(DISTINCT scenario_code) AS scenario_count, "
            "MIN(created_at) AS first_created_at, "
            "MAX(created_at) AS last_created_at, "
            "AVG(avg_frame_ms) AS avg_frame_ms, "
            "AVG(p95_frame_ms) AS avg_p95_frame_ms, "
            "AVG(p99_frame_ms) AS avg_p99_frame_ms, "
            "AVG(avg_hair_gpu_ms) AS avg_hair_gpu_ms, "
            "AVG(avg_fps) AS avg_fps, "
            "AVG(sixty_fps_ratio) AS avg_sixty_fps_ratio, "
            "AVG(false_static_rate) AS avg_false_static_rate, "
            "AVG(predictive_wake_recall) AS avg_predictive_wake_recall, "
            "AVG(repair_verified_success_rate) AS avg_repair_verified_success_rate, "
            "AVG(collision_miss_rate) AS avg_collision_miss_rate "
            "FROM runtime_runs WHERE is_valid = 1"
        ).fetchone()
        return dict(row) if row else {}
