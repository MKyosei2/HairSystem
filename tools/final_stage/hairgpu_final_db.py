from __future__ import annotations

"""SQLite persistence for validated HairSystem formal runtime evidence.

Evidence integrity and result quality are intentionally separate. A real,
hash-consistent run may be imported even when its quality/performance gate is
not met; the distinction is retained in ``validation_status`` and
``quality_gate_passed``.
"""

import csv
import hashlib
import json
import math
import sqlite3
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable, Mapping

# Keep these values aligned with include/hairgpu/HairNoveltyTelemetry.h.
AVAILABILITY_GPU_TIMING = 1 << 2
AVAILABILITY_COLLISION_GROUND_TRUTH = 1 << 6
AVAILABILITY_WAKE_GROUND_TRUTH = 1 << 7
AVAILABILITY_REPAIR_VERIFICATION = 1 << 8
AVAILABILITY_BOUNDARY_COMPARISON = 1 << 9
AVAILABILITY_FALSE_STATIC_GROUND_TRUTH = 1 << 12
AVAILABILITY_PERFORMANCE_ENVIRONMENT = 1 << 13
AVAILABILITY_RESULT_INTEGRITY = 1 << 14

IMPORTABLE_STATUSES = {"complete_valid", "quality_gate_failed"}


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
    """Store only complete, real, identity-matched measurement evidence."""

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
        "evidence_integrity_passed": "INTEGER",
        "quality_gate_passed": "INTEGER",
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
        "formal_asset_path": "TEXT",
        "formal_asset_sha256": "TEXT",
        "formal_topology_sha256": "TEXT",
        "generator_settings_sha256": "TEXT",
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

    def _table_exists(self, table: str) -> bool:
        row = self.connection.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", (table,)
        ).fetchone()
        return row is not None

    def _table_columns(self, table: str) -> set[str]:
        if not self._table_exists(table):
            return set()
        return {
            str(row["name"])
            for row in self.connection.execute(f"PRAGMA table_info({table})").fetchall()
        }

    def _ensure_columns(self, table: str, definitions: Mapping[str, str]) -> None:
        existing = self._table_columns(table)
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
    def _read_json(path: Path | None) -> dict[str, Any]:
        if path is None:
            return {}
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
    def _pick(payload: Mapping[str, Any], *keys: str, default: Any = None) -> Any:
        for key in keys:
            if key in payload and payload[key] is not None:
                return payload[key]
        return default

    @staticmethod
    def _csv_value(row: Mapping[str, Any], *keys: str) -> Any:
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
            "SELECT experiment_id FROM experiments WHERE experiment_name=? "
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
            physical = self.file_sha256(manifest_path)
            canonical = str(manifest.get("manifestSha256") or "")
            if expected_manifest_sha256.lower() not in {physical.lower(), canonical.lower()}:
                raise RuntimeError("Artifact manifest hash does not match the summary")
        entries = manifest.get("artifacts")
        if not isinstance(entries, list):
            raise RuntimeError("Artifact manifest has no artifacts array")
        by_name = {
            str(item.get("path")): item
            for item in entries
            if isinstance(item, dict) and item.get("path")
        }
        for path in required_paths:
            path = Path(path)
            if not path.is_file():
                raise RuntimeError(f"Required artifact is missing: {path}")
            entry = by_name.get(path.name)
            if not entry:
                raise RuntimeError(f"Required artifact is absent from manifest: {path.name}")
            expected = str(entry.get("sha256") or "").lower()
            if expected != self.file_sha256(path).lower():
                raise RuntimeError(f"Artifact hash mismatch: {path.name}")
            size = self._nullable_int(entry.get("sizeBytes"))
            if size is None or size != path.stat().st_size:
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
        if not self._table_exists("run_artifacts"):
            return
        artifact_path = Path(artifact_path)
        values = {
            "run_id": run_id,
            "category": category,
            "label": label,
            "artifact_path": str(artifact_path),
            "sha256": self.file_sha256(artifact_path) if artifact_path.is_file() else None,
            "size_bytes": artifact_path.stat().st_size if artifact_path.is_file() else None,
            "created_at": _utc_now(),
        }
        self._insert_dynamic("run_artifacts", values)
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
        if not self._table_exists("derived_metrics"):
            return
        self._insert_dynamic("derived_metrics", {
            "run_id": run_id,
            "scope_type": scope_type,
            "scope_key": scope_key,
            "metric_name": metric_name,
            "metric_value": float(metric_value) if metric_value is not None else None,
            "units": units,
            "notes": notes,
            "created_at": _utc_now(),
        })
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
        if not self._table_exists("user_task_results"):
            return
        with self.connection:
            experiment_id = self.upsert_experiment(experiment_name, "", notes)
            self._insert_dynamic("user_task_results", {
                "experiment_id": experiment_id,
                "task_id": task_id,
                "title": title,
                "method_code": method_code,
                "scenario_code": scenario_code,
                "asset_name": asset_name,
                "participant_id": participant_id,
                "completion_seconds": completion_seconds,
                "redo_count": redo_count,
                "unintended_change_score": unintended_change_score,
                "satisfaction_score": satisfaction_score,
                "notes": notes,
                "created_at": _utc_now(),
            })

    def clear_suite_rows(self, suite_name: str) -> None:
        with self.connection:
            if self._table_exists("hypothesis_checks"):
                self.connection.execute(
                    "DELETE FROM hypothesis_checks WHERE suite_name=?", (suite_name,)
                )
            if self._table_exists("scenario_coverage"):
                self.connection.execute(
                    "DELETE FROM scenario_coverage WHERE suite_name=?", (suite_name,)
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
        if not self._table_exists("scenario_coverage"):
            return
        self._insert_dynamic("scenario_coverage", {
            "suite_name": suite_name,
            "scenario_code": scenario_code,
            "category": category,
            "required_method_code": required_method_code,
            "is_present": int(is_present),
            "notes": notes,
            "created_at": _utc_now(),
        })
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
        if not self._table_exists("hypothesis_checks"):
            return
        self._insert_dynamic("hypothesis_checks", {
            "suite_name": suite_name,
            "hypothesis_id": hypothesis_id,
            "title": title,
            "source_type": source_type,
            "scenario_code": scenario_code,
            "task_id": task_id,
            "method_code": method_code,
            "baseline_method_code": baseline_method_code,
            "metric_name": metric_name,
            "actual_value": actual_value,
            "threshold_value": threshold_value,
            "comparator": comparator,
            "passed": int(passed),
            "notes": notes,
            "created_at": _utc_now(),
        })
        self.connection.commit()

    def _insert_dynamic(self, table: str, values: Mapping[str, Any]) -> sqlite3.Cursor:
        columns = self._table_columns(table)
        filtered = {key: value for key, value in values.items() if key in columns}
        if not filtered:
            raise RuntimeError(f"No compatible columns found for table {table}")
        names = list(filtered)
        return self.connection.execute(
            f"INSERT INTO {table} ({','.join(names)}) VALUES ({','.join('?' for _ in names)})",
            [filtered[name] for name in names],
        )

    def _delete_existing_run(self, run_name: str) -> None:
        if not self._table_exists("runtime_runs"):
            return
        rows = self.connection.execute(
            "SELECT run_id FROM runtime_runs WHERE run_name=?", (run_name,)
        ).fetchall()
        for row in rows:
            run_id = int(row["run_id"])
            for table in ("run_artifacts", "derived_metrics", "runtime_frame_metrics"):
                if self._table_exists(table) and "run_id" in self._table_columns(table):
                    self.connection.execute(f"DELETE FROM {table} WHERE run_id=?", (run_id,))
            self.connection.execute("DELETE FROM runtime_runs WHERE run_id=?", (run_id,))

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
            path for path in (
                frame_jsonl_path,
                settings_json_path,
                environment_json_path,
                validation_json_path,
            ) if path is not None
        )
        for path in required_files:
            if not path.is_file():
                raise RuntimeError(f"Required formal artifact is missing: {path}")

        summary = self._read_json(summary_json_path)
        settings = self._read_json(settings_json_path)
        validation = self._read_json(validation_json_path)
        environment = self._read_json(environment_json_path)
        status = str(
            summary.get("validationStatus")
            or summary.get("status")
            or validation.get("status")
            or "incomplete"
        ).strip().lower()
        if status not in IMPORTABLE_STATUSES or not self._as_bool(summary.get("realMeasurement")):
            reasons = summary.get("validationReasons") or validation.get("issues") or []
            raise RuntimeError(
                f"Run '{metadata.run_name}' is not importable: status={status}, reasons={reasons}"
            )
        evidence_valid = self._as_bool(
            validation.get(
                "evidenceIntegrityPassed",
                validation.get("formalContractValid", validation.get("isValid", False)),
            )
        )
        if not evidence_valid:
            raise RuntimeError(f"Run '{metadata.run_name}' has invalid measurement evidence")

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
            manifest_json_path = summary_json_path.parent / str(
                summary.get("resultManifestPath") or f"{metadata.run_name}_manifest.json"
            )
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

        aggregate = summary.get("formalAggregateMetrics") if isinstance(summary.get("formalAggregateMetrics"), dict) else {}
        def metric(*keys: str, default: Any = None) -> Any:
            value = self._pick(summary, *keys, default=None)
            return self._pick(aggregate, *keys, default=default) if value is None else value

        quality_gate_passed = self._as_bool(validation.get("qualityGatePassed", status == "complete_valid"))
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
            "evidence_integrity_passed": 1,
            "quality_gate_passed": int(quality_gate_passed),
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
            "formal_asset_path": settings.get("observedAssetPath") or settings.get("formalAssetPath"),
            "formal_asset_sha256": settings.get("formalAssetSha256") or sv("assetSha256"),
            "formal_topology_sha256": settings.get("formalTopologySha256"),
            "generator_settings_sha256": settings.get("generatorSettingsSha256"),
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
                or all(metric(key) is not None for key in ("falseStaticRate", "collisionMissRate"))
            ),
            "determinism_valid": int(
                str(sv("measurementMode", "measurement_mode", default="")).lower() != "determinism"
                or (
                    self._nullable_int(metric("determinismMismatchCount")) in (None, 0)
                    and bool(metric("stateSequenceHash"))
                    and bool(metric("compactionSequenceHash"))
                    and bool(metric("finalPositionHash"))
                )
            ),
            "long_run_valid": int(self._as_bool(sv("longRunValid", default=True))),
            "peak_vram_mb": self._nullable_float(metric("peakVramMb", "peak_vram_mb")),
            "peak_ram_mb": self._nullable_float(metric("peakRamMb", "peak_ram_mb")),
            "vram_slope_mb_per_min": self._nullable_float(metric("vramSlopeMbPerMin", "vram_slope_mb_per_min")),
            "ram_slope_mb_per_min": self._nullable_float(metric("ramSlopeMbPerMin", "ram_slope_mb_per_min")),
            "p99_degradation_ratio": self._nullable_float(metric("p99DegradationRatio", "p99_degradation_ratio")),
            "query_unresolved_slope": self._nullable_float(metric("queryUnresolvedSlope", "query_unresolved_slope")),
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
            "sample_count": self._nullable_int(metric("sample_count", "sampleCount")),
            "avg_frame_ms": self._nullable_float(metric("avg_frame_ms", "avgFrameMs", "averageFrameMilliseconds")),
            "p50_frame_ms": self._nullable_float(metric("p50_frame_ms", "p50FrameMs", "p50FrameMilliseconds")),
            "p95_frame_ms": self._nullable_float(metric("p95_frame_ms", "p95FrameMs", "p95FrameMilliseconds")),
            "p99_frame_ms": self._nullable_float(metric("p99_frame_ms", "p99FrameMs", "p99FrameMilliseconds")),
            "p999_frame_ms": self._nullable_float(metric("p999_frame_ms", "p999FrameMs", "p999FrameMilliseconds")),
            "worst_frame_ms": self._nullable_float(metric("worst_frame_ms", "worstFrameMs", "maximumFrameMilliseconds")),
            "avg_simulation_ms": self._nullable_float(metric("avg_simulation_ms", "avgSimulationMs")),
            "avg_hair_gpu_ms": self._nullable_float(metric("avg_hair_gpu_ms", "avgHairGpuMs", "averageHairGpuMilliseconds")),
            "p95_hair_gpu_ms": self._nullable_float(metric("p95_hair_gpu_ms", "p95HairGpuMs", "p95HairGpuMilliseconds")),
            "p99_hair_gpu_ms": self._nullable_float(metric("p99_hair_gpu_ms", "p99HairGpuMs", "p99HairGpuMilliseconds")),
            "avg_fps": self._nullable_float(metric("avg_fps", "avgFps")),
            "sixty_fps_ratio": self._nullable_float(metric("sixty_fps_ratio", "sixtyFpsRatio")),
            "frame_overrun_rate": self._nullable_float(metric("frameOverrunRate", "frame_overrun_rate", "frameBudgetOverrunRate")),
            "guide_count": self._nullable_int(metric("guide_count", "guideCount")),
            "follower_count": self._nullable_int(metric("follower_count", "followerCount")),
            "total_strand_count": self._nullable_int(metric("total_strand_count", "totalStrandCount", "displayStrandCount")),
            "total_particle_count": self._nullable_int(metric("total_particle_count", "totalParticleCount")),
            "false_static_rate": self._nullable_float(metric("falseStaticRate")),
            "predictive_wake_precision": self._nullable_float(metric("predictiveWakePrecision")),
            "predictive_wake_recall": self._nullable_float(metric("predictiveWakeRecall")),
            "repair_execution_success_rate": self._nullable_float(metric("repairExecutionSuccessRate")),
            "repair_verified_success_rate": self._nullable_float(metric("repairVerifiedSuccessRate", "repairSuccessRate")),
            "repair_recurrence_rate": self._nullable_float(metric("repairRecurrenceRate")),
            "collision_miss_rate": self._nullable_float(metric("collisionMissRate")),
            "state_oscillation_rate": self._nullable_float(metric("stateOscillationRate")),
            "max_boundary_error_mm": self._nullable_float(metric("staticDynamicBoundaryMaxErrorMm")),
            "avg_silhouette_error": self._nullable_float(metric("avg_silhouette_error", "averageSilhouetteError")),
            "avg_clump_preservation": self._nullable_float(metric("avg_clump_preservation", "averageClumpPreservation")),
            "avg_edit_locality": self._nullable_float(metric("avg_edit_locality", "averageEditLocality")),
            "avg_boundary_naturalness": self._nullable_float(metric("avg_boundary_naturalness", "averageBoundaryNaturalness")),
            "avg_edit_to_run_latency_ms": self._nullable_float(metric("avg_edit_to_run_latency_ms", "averageEditToRunLatencyMs")),
            "avg_penetration_per_sec": self._nullable_float(metric("avg_penetration_per_sec", "avgPenetrationPerSec")),
            "gpu_timing_coverage": self._nullable_float(metric("gpuTimingCoverage")),
            "counter_coverage": self._nullable_float(metric("counterCoverage")),
            "fatal_runtime_failure_count": fatal_count,
            "nan_count": self._nullable_int(metric("nanCount", "nanStateCount")),
            "inf_count": self._nullable_int(metric("infCount", "infiniteStateCount")),
            "full_cpu_upload_count": self._nullable_int(metric("fullCpuUploadCount", "fullCpuGroupUploadCount")),
            "same_frame_readback_count": self._nullable_int(metric("sameFrameReadbackCount")),
            "synthetic_metric_count": synthetic_count,
            "unavailable_metric_count": self._nullable_int(metric("unavailableMetricCount")),
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
            self._delete_existing_run(metadata.run_name)
            cursor = self._insert_dynamic("runtime_runs", run_values)
            run_id = int(cursor.lastrowid)
            for frame in frame_rows:
                frame["run_id"] = run_id
                self._insert_dynamic("runtime_frame_metrics", frame)
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

    def _frame_record(self, row: Mapping[str, Any]) -> dict[str, Any]:
        cv = lambda *keys: self._csv_value(row, *keys)
        optional_int = self._nullable_int
        optional_float = self._nullable_float
        availability = optional_int(cv("availabilityMask", "availability_mask")) or 0
        metric_availability = optional_int(cv("metricAvailabilityMask", "metric_availability_mask")) or availability
        collision_available = bool(metric_availability & AVAILABILITY_COLLISION_GROUND_TRUTH)
        wake_available = bool(metric_availability & AVAILABILITY_WAKE_GROUND_TRUTH)
        repair_available = bool(metric_availability & AVAILABILITY_REPAIR_VERIFICATION)
        boundary_available = bool(metric_availability & AVAILABILITY_BOUNDARY_COMPARISON)
        false_static_available = bool(metric_availability & AVAILABILITY_FALSE_STATIC_GROUND_TRUTH)
        result: dict[str, Any] = {
            "run_id": None,
            "sample_index": optional_int(cv("sampleIndex", "sample_index", "measuredFrameIndex")),
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
            raise RuntimeError("Frame CSV contains a row without sampleIndex/measuredFrameIndex")
        return result

    @staticmethod
    def _rows_to_dicts(rows: Iterable[sqlite3.Row]) -> list[dict[str, Any]]:
        return [dict(row) for row in rows]

    def _select_all(self, table: str, order_by: str = "") -> list[dict[str, Any]]:
        if not self._table_exists(table):
            return []
        suffix = f" ORDER BY {order_by}" if order_by else ""
        return self._rows_to_dicts(self.connection.execute(f"SELECT * FROM {table}{suffix}").fetchall())

    def fetch_runs(self):
        if not self._table_exists("runtime_runs"):
            return []
        return self.connection.execute("SELECT * FROM runtime_runs ORDER BY run_id").fetchall()

    def fetch_frame_metrics(self):
        if not self._table_exists("runtime_frame_metrics"):
            return []
        return self.connection.execute(
            "SELECT * FROM runtime_frame_metrics ORDER BY run_id, sample_index"
        ).fetchall()

    def fetch_artifact_rows(self) -> list[dict[str, Any]]:
        return self._select_all("run_artifacts", "run_id, artifact_id")

    def fetch_derived_metric_rows(self) -> list[dict[str, Any]]:
        return self._select_all("derived_metrics", "run_id, metric_id")

    def fetch_scenario_coverage_rows(self) -> list[dict[str, Any]]:
        return self._select_all("scenario_coverage", "suite_name, scenario_code, required_method_code")

    def fetch_hypothesis_rows(self) -> list[dict[str, Any]]:
        return self._select_all("hypothesis_checks", "suite_name, hypothesis_id, check_id")

    def fetch_user_task_rows(self) -> list[dict[str, Any]]:
        return self._select_all("user_task_results", "task_result_id")

    def fetch_user_task_summary_rows(self) -> list[dict[str, Any]]:
        if not self._table_exists("user_task_results"):
            return []
        columns = self._table_columns("user_task_results")
        required = {"method_code", "completion_seconds", "redo_count", "unintended_change_score", "satisfaction_score"}
        if not required.issubset(columns):
            return []
        return self._rows_to_dicts(self.connection.execute(
            "SELECT method_code, COUNT(*) AS task_count, "
            "AVG(completion_seconds) AS avg_completion_seconds, "
            "AVG(redo_count) AS avg_redo_count, "
            "AVG(unintended_change_score) AS avg_unintended_change_score, "
            "AVG(satisfaction_score) AS avg_satisfaction_score "
            "FROM user_task_results GROUP BY method_code ORDER BY method_code"
        ).fetchall())

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
            "FROM runtime_runs WHERE is_valid=1 "
            "GROUP BY method_code, scenario_code ORDER BY method_code, scenario_code"
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
            "FROM runtime_runs WHERE is_valid=1 "
            "GROUP BY method_code ORDER BY method_code"
        ).fetchall()

    def fetch_method_summary_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(self.fetch_ablation_rows())

    def fetch_scenario_summary_rows(self) -> list[dict[str, Any]]:
        return self._rows_to_dicts(self.connection.execute(
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
            "FROM runtime_runs WHERE is_valid=1 "
            "GROUP BY scenario_code ORDER BY scenario_code"
        ).fetchall())

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
            "FROM runtime_runs WHERE is_valid=1"
        ).fetchone()
        return dict(row) if row else {}
