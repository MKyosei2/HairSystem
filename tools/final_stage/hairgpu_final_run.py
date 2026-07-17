from __future__ import annotations

"""Authoritative formal-run launcher and validator for HairSystem.

This module intentionally treats missing evidence as unavailable.  It never
replaces a missing quality metric with a proxy.  ``hairgpu_runtime_suite.py``
imports the public helpers defined here.
"""

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import re
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Iterable, Mapping

EXIT_COMPLETE_VALID = 0
EXIT_QUALITY_GATE_FAILED = 1
EXIT_FORMAL_CONTRACT_INVALID = 2
EXIT_RUNTIME_FAILURE = 3
EXIT_OUTPUT_INTEGRITY_FAILURE = 4
EXIT_CONFIGURATION_FAILURE = 5

VALIDATION_VERSION = "01-safe-v3.1-existing-source-only"
FORMAL_METHOD_CODES = {"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "P"}
METHOD_ALIASES = {
    "b0": "B0", "b0_fulldynamic": "B0", "full_dynamic": "B0",
    "full-physics": "B0", "full_physics": "B0", "baseline_currentproject": "B0",
    "b1": "B1", "b1_strandsleep": "B1", "strand_sleep": "B1",
    "b2": "B2", "b2_blocksleep": "B2", "block_sleep": "B2",
    "b3": "B3", "b3_segmentthresholdsleep": "B3", "segment_threshold_sleep": "B3",
    "prediction_only": "B3", "prediction-only": "B3",
    "b4": "B4", "b4_segmenthysteresissleep": "B4", "segment_hysteresis_sleep": "B4",
    "b5": "B5", "b5_segmentcache": "B5", "segment_cache": "B5",
    "b6": "B6", "b6_segmentgpucompaction": "B6", "segment_gpu_compaction": "B6",
    "b7": "B7", "b7_activesetonly": "B7", "active_set_only": "B7",
    "active-set-runtime": "B7", "active_set_runtime": "B7",
    "b8": "B8", "b8_externalstatearray": "B8", "external_state_array": "B8",
    "p": "P", "p_integratedsegmentgroup": "P",
    "proposed_integrated_segment_group": "P", "proposal-hybrid": "P",
    "proposal_hybrid": "P", "proposed_fullsystem": "P", "proposed_full_system": "P",
}

METHOD_FLAG_KEYS = (
    "enableNoNoveltyBaseline", "enableMicrotype", "enableBudgetedPhysics",
    "enableCertificateRender", "enablePerStrandEdit", "enableCBMA",
    "enablePhysicsIslands", "enableRodRepair", "enableGpuResident",
    "enableSegmentGroupSleep", "enableSegmentGroupIntegratedState",
    "enableExternalStateArray", "enableSharedPointOwnership",
    "enableDirectionalResponsibility", "enableStaticCertificate",
    "enablePredictiveWake", "enableVelocityRecovery", "enableRepair",
    "enableGpuCompaction", "enableActiveSet", "enableStrandSleep",
    "enableBlockSleep", "enableSleepHysteresis", "enableStaticCache",
)
RUNTIME_SETTING_KEYS = (
    "renderWidth", "renderHeight", "displayStrandCount", "guideCount",
    "particlesPerGuide", "solverIterations", "vsync", "presentSyncInterval",
)
# Formal runs require the complete requested/applied/observed flag set.
# A viewer acknowledgement is supplemental evidence, never a substitute.
APPLIED_METHOD_EVIDENCE_KEYS = METHOD_FLAG_KEYS

REQUIRED_FRAME_COLUMNS = (
    "caseId", "variant", "scenario", "seed", "sourceFrameIndex",
    "globalFrameIndex", "measuredFrameIndex", "frameMilliseconds",
    "gpuHairMilliseconds", "totalStrandCount", "availabilityMask",
    "metricAvailabilityMask", "unavailableMetricCount", "syntheticMetricCount",
    "sameFrameReadbackCount", "nanStateCount", "infiniteStateCount",
    "fatalFailure", "fatalFailureCode",
)

_BASE_SAFE_FLAGS: dict[str, Any] = {
    "enableNoNoveltyBaseline": False,
    "enableMicrotype": True,
    "enableBudgetedPhysics": False,
    "enableCertificateRender": False,
    "enablePerStrandEdit": False,
    "enableCBMA": False,
    "enablePhysicsIslands": False,
    "enableRodRepair": False,
    "enableGpuResident": True,
    "enableSegmentGroupSleep": True,
    "enableSegmentGroupIntegratedState": True,
    "enableExternalStateArray": False,
    "enableSharedPointOwnership": True,
    "enableDirectionalResponsibility": True,
    "enableStaticCertificate": True,
    "enablePredictiveWake": True,
    "enableVelocityRecovery": True,
    "enableRepair": True,
    "enableGpuCompaction": True,
    "enableActiveSet": True,
    "enableStrandSleep": False,
    "enableBlockSleep": False,
    "enableSleepHysteresis": True,
    "enableStaticCache": True,
}


def _preset(**overrides: Any) -> dict[str, Any]:
    result = dict(_BASE_SAFE_FLAGS)
    result.update(overrides)
    return result


METHOD_PRESETS: dict[str, dict[str, Any]] = {
    "B0": _preset(
        enableNoNoveltyBaseline=True, enableMicrotype=False,
        enableSegmentGroupSleep=False, enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False, enableDirectionalResponsibility=False,
        enableStaticCertificate=False, enablePredictiveWake=False,
        enableVelocityRecovery=False, enableRepair=False,
        enableGpuCompaction=False, enableActiveSet=False,
        enableGpuResident=False, enableSleepHysteresis=False,
        enableStaticCache=False, forceAllDynamic=True,
        stateStorage="IntegratedRecord",
    ),
    "B1": _preset(
        enableMicrotype=False, enableSegmentGroupIntegratedState=False,
        enableStrandSleep=True, enableStaticCertificate=False,
        enablePredictiveWake=False, enableVelocityRecovery=False,
        enableRepair=False, enableGpuCompaction=False,
        enableActiveSet=False, enableGpuResident=False,
        stateStorage="ExternalArray",
    ),
    "B2": _preset(
        enableMicrotype=False, enableSegmentGroupIntegratedState=False,
        enableBlockSleep=True, enableStaticCertificate=False,
        enablePredictiveWake=False, enableVelocityRecovery=False,
        enableRepair=False, enableGpuCompaction=False,
        enableActiveSet=False, enableGpuResident=False,
        groupsPerBlock=8, stateStorage="ExternalArray",
    ),
    "B3": _preset(
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False, enableDirectionalResponsibility=False,
        enableStaticCertificate=False, enablePredictiveWake=False,
        enableVelocityRecovery=False, enableRepair=False,
        enableGpuCompaction=False, enableActiveSet=False,
        enableGpuResident=False, enableSleepHysteresis=False,
        enableStaticCache=False, stateStorage="ExternalArray",
    ),
    "B4": _preset(
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False, enableDirectionalResponsibility=False,
        enableStaticCertificate=False, enablePredictiveWake=False,
        enableVelocityRecovery=False, enableRepair=False,
        enableGpuCompaction=False, enableActiveSet=False,
        enableGpuResident=False, enableStaticCache=False,
        stateStorage="ExternalArray",
    ),
    "B5": _preset(
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False, enableDirectionalResponsibility=False,
        enableStaticCertificate=False, enablePredictiveWake=False,
        enableVelocityRecovery=False, enableRepair=False,
        enableGpuCompaction=False, enableActiveSet=False,
        enableGpuResident=False, stateStorage="ExternalArray",
    ),
    "B6": _preset(
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False, enableDirectionalResponsibility=False,
        enableStaticCertificate=False, enablePredictiveWake=False,
        enableVelocityRecovery=False, enableRepair=False,
        enableActiveSet=False, enableGpuResident=True,
        stateStorage="ExternalArray",
    ),
    "B7": _preset(
        enableMicrotype=False, enableSegmentGroupSleep=False,
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False, enableDirectionalResponsibility=False,
        enableStaticCertificate=False, enablePredictiveWake=False,
        enableVelocityRecovery=False, enableRepair=False,
        enableGpuCompaction=True, enableActiveSet=True,
        enableStaticCache=False, stateStorage="ExternalArray",
    ),
    "B8": _preset(
        enableSegmentGroupIntegratedState=False,
        enableExternalStateArray=True,
        stateStorage="ExternalArray",
    ),
    "P": _preset(
        enableSegmentGroupIntegratedState=True,
        enableExternalStateArray=False,
        stateStorage="IntegratedRecord",
    ),
}

REQUIRED_METRICS_BY_SCENARIO: dict[str, set[str]] = {
    "S07_ColliderApproach": {"predictiveWakeRecall", "wakeDelayFrameMax", "collisionCandidateCount"},
    "S08_StaticDynamicBoundaryContact": {"staticDynamicBoundaryMaxErrorMm", "sharedPointConflictCount"},
    "S12_FalseStaticTrap": {"falseStaticRate"},
    "S13_RepairStorm": {"repairVerifiedSuccessRate", "repairRecurrenceRate"},
    "S16_StateThrashing": {"stateOscillationRate"},
    "S17_CloseCamera100k": {"gpuTimingCoverage", "displayStrandCount"},
    "S18_FarCamera100k": {"gpuTimingCoverage", "displayStrandCount"},
}

AVAILABILITY_BITS = {
    "gpuTiming": 1 << 2,
    "collision": 1 << 6,
    "wake": 1 << 7,
    "repair": 1 << 8,
    "boundary": 1 << 9,
    "determinism": 1 << 10,
    "adapterIdentity": 1 << 11,
    "falseStatic": 1 << 12,
    "performanceEnvironment": 1 << 13,
    "resultIntegrity": 1 << 14,
}

METRIC_AVAILABILITY = {
    "collisionMissRate": "collision",
    "collisionCandidateCount": "collision",
    "predictiveWakePrecision": "wake",
    "predictiveWakeRecall": "wake",
    "wakeDelayFrameMax": "wake",
    "repairExecutionSuccessRate": "repair",
    "repairVerifiedSuccessRate": "repair",
    "repairRecurrenceRate": "repair",
    "staticDynamicBoundaryMaxErrorMm": "boundary",
    "sharedPointConflictCount": "boundary",
    "falseStaticRate": "falseStatic",
}


class FormalRunError(RuntimeError):
    def __init__(self, message: str, exit_code: int, issues: list[dict[str, Any]] | None = None):
        super().__init__(message)
        self.exit_code = int(exit_code)
        self.issues = list(issues or [])


def _issue(code: str, message: str, *, expected: Any = None, observed: Any = None, severity: str = "error") -> dict[str, Any]:
    return {"severity": severity, "code": code, "message": message, "expected": expected, "observed": observed}


def normalize_method_code(method_code: str) -> str:
    raw = str(method_code or "").strip()
    if raw in FORMAL_METHOD_CODES:
        return raw
    return METHOD_ALIASES.get(raw, METHOD_ALIASES.get(raw.lower(), raw))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_tree(paths: Iterable[Path], *, base_path: Path | None = None) -> str:
    roots = [Path(path).resolve() for path in paths]
    files: list[Path] = []
    for root in roots:
        if root.is_file():
            files.append(root)
        elif root.exists():
            files.extend(path for path in root.rglob("*") if path.is_file())
    if base_path is None:
        base_path = Path(os.path.commonpath([str(path.parent) for path in files])) if files else Path.cwd()
    base_path = Path(base_path).resolve()
    digest = hashlib.sha256()
    for path in sorted(files, key=lambda item: item.as_posix().lower()):
        try:
            relative = path.resolve().relative_to(base_path).as_posix()
        except ValueError:
            relative = path.name
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def canonical_json_sha256(payload: Any) -> str:
    encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _atomic_write_bytes(path: Path, data: bytes) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".tmp.{os.getpid()}.{threading.get_ident()}")
    try:
        with temporary.open("wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    data = (json.dumps(dict(payload), ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
    _atomic_write_bytes(Path(path), data)


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise FormalRunError(f"Invalid JSON: {path}: {exc}", EXIT_OUTPUT_INTEGRITY_FAILURE) from exc
    if not isinstance(value, dict):
        raise FormalRunError(f"Expected JSON object: {path}", EXIT_OUTPUT_INTEGRITY_FAILURE)
    return value


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with Path(path).open("r", encoding="utf-8-sig") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise FormalRunError(f"Invalid JSONL at {path}:{line_number}: {exc}", EXIT_OUTPUT_INTEGRITY_FAILURE) from exc
            if not isinstance(value, dict):
                raise FormalRunError(f"JSONL row {line_number} is not an object", EXIT_OUTPUT_INTEGRITY_FAILURE)
            rows.append(value)
    return rows


def _parse_csv_scalar(value: str) -> Any:
    text = value.strip()
    if text == "":
        return None
    lower = text.lower()
    if lower in {"true", "false"}:
        return lower == "true"
    try:
        if re.fullmatch(r"[-+]?\d+", text):
            return int(text)
        number = float(text)
        return number if math.isfinite(number) else None
    except ValueError:
        return text


def _read_csv_rows(path: Path) -> list[dict[str, Any]]:
    with Path(path).open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        if not reader.fieldnames:
            return []
        return [{key: _parse_csv_scalar(value or "") for key, value in row.items()} for row in reader]


def _frame_values_equal(left: Any, right: Any) -> bool:
    left_number = _optional_float(left)
    right_number = _optional_float(right)
    if left_number is not None and right_number is not None:
        return math.isclose(left_number, right_number, rel_tol=1.0e-6, abs_tol=1.0e-6)
    if isinstance(left, bool) or isinstance(right, bool):
        return _as_bool(left) == _as_bool(right)
    return str(left) == str(right)


def _validate_frame_schema_and_parity(
    jsonl_rows: list[dict[str, Any]], csv_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    required = set(REQUIRED_FRAME_COLUMNS)
    json_keys = set(jsonl_rows[0]) if jsonl_rows else set()
    csv_keys = set(csv_rows[0]) if csv_rows else set()
    missing_json = sorted(required - json_keys)
    missing_csv = sorted(required - csv_keys)
    if missing_json:
        issues.append(_issue(
            "E_JSONL_SCHEMA_MISSING", "JSONL frame schema is incomplete.",
            expected=sorted(required), observed=sorted(json_keys),
        ))
    if missing_csv:
        issues.append(_issue(
            "E_CSV_SCHEMA_MISSING", "CSV frame schema is incomplete.",
            expected=sorted(required), observed=sorted(csv_keys),
        ))
    if len(jsonl_rows) != len(csv_rows) or missing_json or missing_csv:
        return issues
    for index, (json_row, csv_row) in enumerate(zip(jsonl_rows, csv_rows)):
        for key in REQUIRED_FRAME_COLUMNS:
            if not _frame_values_equal(json_row.get(key), csv_row.get(key)):
                issues.append(_issue(
                    "E_FRAME_FORMAT_MISMATCH",
                    f"CSV/JSONL mismatch at frame {index}: {key}",
                    expected=json_row.get(key), observed=csv_row.get(key),
                ))
                if len(issues) >= 32:
                    return issues
    return issues


def _merge_frame_rows(jsonl_rows: list[dict[str, Any]], csv_rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    if len(jsonl_rows) != len(csv_rows):
        return list(jsonl_rows)
    merged: list[dict[str, Any]] = []
    for json_row, csv_row in zip(jsonl_rows, csv_rows):
        row = dict(csv_row)
        for key, value in json_row.items():
            if value is not None or key not in row:
                row[key] = value
        merged.append(row)
    return merged


def _merge_formal_evidence(
    rows: list[dict[str, Any]], evidence_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    """Merge dedicated repair/false-static evidence by source frame index."""
    if not evidence_rows:
        return list(rows)
    by_source: dict[int, dict[str, Any]] = {}
    for evidence in evidence_rows:
        source = _optional_int(evidence.get("sourceFrameIndex"))
        if source is not None:
            by_source[source] = evidence
    merged: list[dict[str, Any]] = []
    evidence_keys = (
        "availabilityMask", "metricAvailabilityMask", "falseStaticCount",
        "staticCachedGroupCount", "staticTransformedGroupCount",
        "repairAttemptCount", "repairSuccessCount", "repairVerifiedCount",
        "repairRecurrenceCount",
    )
    for row in rows:
        output = dict(row)
        source = _optional_int(_metric(row, "sourceFrameIndex", "globalFrameIndex"))
        evidence = by_source.get(source) if source is not None else None
        if evidence:
            for key in evidence_keys:
                if key in evidence and evidence[key] is not None:
                    output[key] = evidence[key]
            output["formalEvidenceMerged"] = True
        merged.append(output)
    return merged


def _optional_float(value: Any) -> float | None:
    if value in (None, "", "null") or isinstance(value, bool):
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None
    return parsed if math.isfinite(parsed) else None


def _optional_int(value: Any) -> int | None:
    value_float = _optional_float(value)
    return int(value_float) if value_float is not None else None


def _required_int(value: Any, default: int = 0) -> int:
    parsed = _optional_int(value)
    return parsed if parsed is not None else default


def _required_float(value: Any, default: float = 0.0) -> float:
    parsed = _optional_float(value)
    return parsed if parsed is not None else default


def _as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def _metric(row: Mapping[str, Any], *keys: str, default: Any = None) -> Any:
    for key in keys:
        if key in row and row[key] is not None:
            return row[key]
    return default


def _percentile(values: list[float], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * percentile / 100.0
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    fraction = position - lower
    return ordered[lower] if lower == upper else ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def _safe_rate(numerator: int | float, denominator: int | float) -> float | None:
    return float(numerator) / float(denominator) if denominator else None


def _resolve_case_path(value: Any, base_dir: Path | None = None) -> Path | None:
    if value in (None, ""):
        return None
    path = Path(str(value))
    if not path.is_absolute() and base_dir is not None:
        path = base_dir / path
    return path.resolve()


def _normalize_case(case: Mapping[str, Any]) -> dict[str, Any]:
    result = dict(case)
    method = normalize_method_code(str(result.get("method", result.get("variant", ""))))
    result["method"] = method
    result.setdefault("variant", method)
    result.setdefault("case_id", result.get("run_name", ""))
    result.setdefault("run_name", result.get("case_id", ""))
    result.setdefault("build_configuration", "Release")
    result.setdefault("platform", "x64")
    result.setdefault("width", 1920)
    result.setdefault("height", 1080)
    result.setdefault("vsync", False)
    result.setdefault("present_sync_interval", 0)
    result.setdefault("fixed_timestep", True)
    result.setdefault("fixed_timestep_seconds", 1.0 / 60.0)
    result.setdefault("warmup_frames", 600)
    result.setdefault("measurement_frames", 6000)
    result.setdefault("guide_strand_count", 3000)
    result.setdefault("display_strand_count", 100000)
    result.setdefault("particles_per_guide", 24)
    result.setdefault("solver_iterations", 6)
    result.setdefault("minimum_gpu_timing_coverage", 0.99)
    result.setdefault("minimum_metric_coverage", 0.99)
    result.setdefault("hardware_adapter_required", True)
    result.setdefault("formal", True)
    requested = dict(METHOD_PRESETS.get(method, {}))
    requested.update(dict(result.get("method_settings", {})))
    result["method_settings"] = requested
    result.setdefault("settings_hash", canonical_json_sha256({
        "method": method,
        "method_settings": requested,
        "scenario": result.get("scenario", ""),
        "seed": result.get("seed", 0),
        "repeat_index": result.get("repeat_index", 0),
        "timeline": result.get("scenario_timeline", ""),
        "asset_manifest": result.get("asset_manifest", ""),
        "runtime": {
            "width": result["width"], "height": result["height"],
            "fixed_timestep_seconds": result["fixed_timestep_seconds"],
            "warmup_frames": result["warmup_frames"],
            "measurement_frames": result["measurement_frames"],
            "guide_strand_count": result["guide_strand_count"],
            "display_strand_count": result["display_strand_count"],
            "particles_per_guide": result["particles_per_guide"],
            "solver_iterations": result["solver_iterations"],
        },
    }))
    required = set(result.get("required_quality_metrics", []))
    required.update(REQUIRED_METRICS_BY_SCENARIO.get(str(result.get("scenario", "")), set()))
    result["required_quality_metrics"] = sorted(required)
    return result


def _validate_timeline(path: Path, case: Mapping[str, Any]) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    timeline = _read_json(path)
    expected_scenario = str(case.get("scenario") or "")
    scenario = str(timeline.get("scenario") or timeline.get("scenarioId") or "")
    if scenario and scenario != expected_scenario:
        issues.append(_issue("E_TIMELINE_SCENARIO", "Timeline scenario mismatch.", expected=expected_scenario, observed=scenario))
    duration = _required_int(timeline.get("durationFrames", timeline.get("duration_frames")))
    required_duration = _required_int(case.get("warmup_frames")) + _required_int(case.get("measurement_frames"))
    if duration < required_duration:
        issues.append(_issue("E_TIMELINE_DURATION", "Timeline is shorter than the formal run.", expected=required_duration, observed=duration))
    fixed_dt = _required_float(timeline.get("fixedTimeStepSeconds", timeline.get("fixed_timestep_seconds")), -1.0)
    expected_dt = _required_float(case.get("fixed_timestep_seconds"))
    if abs(fixed_dt - expected_dt) > 1.0e-6:
        issues.append(_issue("E_TIMELINE_TIMESTEP", "Timeline fixed timestep mismatch.", expected=expected_dt, observed=fixed_dt))
    events = timeline.get("events")
    if not isinstance(events, list) or not events:
        issues.append(_issue("E_TIMELINE_EVENTS", "Formal timeline must contain events."))
        return issues
    previous = -1
    measurement_events = 0
    for index, event in enumerate(events):
        if not isinstance(event, dict):
            issues.append(_issue("E_TIMELINE_EVENT_TYPE", f"Timeline event {index} is not an object."))
            continue
        frame = _optional_int(event.get("frame"))
        if frame is None or frame < 0 or frame >= duration:
            issues.append(_issue("E_TIMELINE_EVENT_FRAME", f"Timeline event {index} has an invalid frame.", observed=frame))
        elif frame < previous:
            issues.append(_issue("E_TIMELINE_ORDER", "Timeline events are not ordered."))
        else:
            previous = frame
            if frame >= _required_int(case.get("warmup_frames")):
                measurement_events += 1
        if not str(event.get("type") or ""):
            issues.append(_issue("E_TIMELINE_EVENT_NAME", f"Timeline event {index} has no type."))
    if measurement_events == 0:
        issues.append(_issue("E_TIMELINE_MEASUREMENT_EVENT", "Timeline has no event in the measured interval."))
    return issues


def _validate_asset_manifest(path: Path) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    manifest = _read_json(path)
    if str(manifest.get("freezeStatus") or "") != "FROZEN":
        issues.append(_issue("E_ASSET_NOT_FROZEN", "Asset manifest must be FROZEN.", expected="FROZEN", observed=manifest.get("freezeStatus")))
    expected_hash = str(manifest.get("sourceAssetSha256") or "")
    if not expected_hash:
        issues.append(_issue("E_ASSET_HASH_MISSING", "Asset manifest lacks sourceAssetSha256."))
    asset_value = manifest.get("sourceAssetPath") or manifest.get("assetPath") or manifest.get("sourceAsset")
    if asset_value:
        asset_path = _resolve_case_path(asset_value, path.parent)
        if asset_path is None or not asset_path.is_file():
            issues.append(_issue("E_ASSET_FILE_MISSING", "Frozen asset file is missing.", observed=str(asset_path)))
        elif expected_hash and sha256_file(asset_path).lower() != expected_hash.lower():
            issues.append(_issue("E_ASSET_FILE_HASH", "Frozen asset bytes do not match sourceAssetSha256.", expected=expected_hash, observed=sha256_file(asset_path)))
    return issues


def _formal_shader_paths(viewer_exe: Path) -> tuple[Path, Path]:
    return (
        viewer_exe.parent / "HairSegmentGroupRuntime.hlsl",
        viewer_exe.parent / "shaders" / "HairSegmentGroupRuntime.hlsl",
    )


def validate_case_contract(case: Mapping[str, Any], viewer_exe: Path | None = None) -> dict[str, Any]:
    normalized = _normalize_case(case)
    issues: list[dict[str, Any]] = []
    for key in ("case_id", "method", "scenario", "seed", "asset_manifest", "scenario_timeline"):
        if normalized.get(key) in (None, ""):
            issues.append(_issue("E_CONFIG_REQUIRED", f"Required formal case field is missing: {key}"))
    assertions = normalized.get("assertions", normalized.get("scenario_assertions"))
    if not isinstance(assertions, dict) or not assertions:
        issues.append(_issue("E_SCENARIO_ASSERTIONS", "Formal scenario assertions are required."))
    else:
        minimum_events = _required_int(assertions.get("min_applied_events", assertions.get("minAppliedEvents")))
        if minimum_events < 1:
            issues.append(_issue(
                "E_SCENARIO_ASSERTION_EVENTS",
                "Formal scenario assertions must require at least one applied event.",
                expected=1, observed=minimum_events,
            ))
    method = normalize_method_code(str(normalized.get("method", "")))
    if method not in FORMAL_METHOD_CODES:
        issues.append(_issue("E_METHOD_UNKNOWN", "Formal method must be B0-B8 or P.", expected=sorted(FORMAL_METHOD_CODES), observed=method))
    if str(normalized.get("build_configuration", "")).lower() != "release":
        issues.append(_issue("E_BUILD_CONFIGURATION", "Formal run requires Release.", expected="Release", observed=normalized.get("build_configuration")))
    if str(normalized.get("platform", "")).lower() != "x64":
        issues.append(_issue("E_PLATFORM", "Formal run requires x64.", expected="x64", observed=normalized.get("platform")))
    if [_required_int(normalized.get("width")), _required_int(normalized.get("height"))] != [1920, 1080]:
        issues.append(_issue("E_RESOLUTION", "Formal run requires 1920x1080."))
    if _as_bool(normalized.get("vsync")) or _required_int(normalized.get("present_sync_interval")) != 0:
        issues.append(_issue("E_VSYNC", "Formal run requires VSync OFF and sync interval 0."))
    if not _as_bool(normalized.get("fixed_timestep")) or abs(_required_float(normalized.get("fixed_timestep_seconds")) - 1.0 / 60.0) > 1.0e-6:
        issues.append(_issue("E_FIXED_TIMESTEP", "Formal run requires a fixed 1/60 second timestep."))
    if _required_int(normalized.get("warmup_frames")) < 600:
        issues.append(_issue("E_WARMUP", "Warm-up must be at least 600 frames."))
    if _required_int(normalized.get("measurement_frames")) < 6000:
        issues.append(_issue("E_MEASUREMENT_FRAMES", "Measurement must be at least 6000 frames."))
    guide_count = _required_int(normalized.get("guide_strand_count"))
    if not 2000 <= guide_count <= 5000:
        issues.append(_issue("E_GUIDE_COUNT", "Guide count must be 2000-5000.", observed=guide_count))
    if _required_int(normalized.get("display_strand_count")) != 100000:
        issues.append(_issue("E_DISPLAY_STRANDS", "Formal evaluation requires exactly 100000 displayed strands."))
    particles = _required_int(normalized.get("particles_per_guide"))
    if not 16 <= particles <= 24:
        issues.append(_issue("E_POINTS_PER_GUIDE", "Particles per guide must be 16-24.", observed=particles))
    iterations = _required_int(normalized.get("solver_iterations"))
    if not 3 <= iterations <= 8:
        issues.append(_issue("E_SOLVER_ITERATIONS", "Solver iterations must be 3-8.", observed=iterations))

    timeline = _resolve_case_path(normalized.get("scenario_timeline"))
    if timeline is None or not timeline.is_file():
        issues.append(_issue("E_TIMELINE_MISSING", "Scenario timeline file is missing.", observed=str(timeline)))
    else:
        issues.extend(_validate_timeline(timeline, normalized))
    asset_manifest = _resolve_case_path(normalized.get("asset_manifest"))
    if asset_manifest is None or not asset_manifest.is_file():
        issues.append(_issue("E_ASSET_MANIFEST_MISSING", "Frozen asset manifest is missing.", observed=str(asset_manifest)))
    else:
        issues.extend(_validate_asset_manifest(asset_manifest))

    if viewer_exe is not None:
        viewer_exe = Path(viewer_exe).resolve()
        if not viewer_exe.is_file():
            issues.append(_issue("E_VIEWER_MISSING", "Viewer executable is missing.", observed=str(viewer_exe)))
        else:
            root_shader, bundled_shader = _formal_shader_paths(viewer_exe)
            if not root_shader.is_file() or not bundled_shader.is_file():
                issues.append(_issue(
                    "E_FORMAL_SHADER_MISSING",
                    "HairSegmentGroupRuntime.hlsl must exist beside the executable and in the shaders directory.",
                    observed={"root": str(root_shader), "bundle": str(bundled_shader)},
                ))
            elif sha256_file(root_shader) != sha256_file(bundled_shader):
                issues.append(_issue("E_FORMAL_SHADER_DIVERGED", "The two formal shader copies differ."))

    if method in {"B8", "P"}:
        flags = normalized["method_settings"]
        if _as_bool(flags.get("enableSegmentGroupIntegratedState")) != (method == "P"):
            issues.append(_issue("E_STORAGE_MODE", "Integrated state flag does not match method."))
        if _as_bool(flags.get("enableExternalStateArray")) != (method == "B8"):
            issues.append(_issue("E_STORAGE_MODE", "External state flag does not match method."))
        common = set(_BASE_SAFE_FLAGS) - {"enableSegmentGroupIntegratedState", "enableExternalStateArray"}
        mismatches = {key: flags.get(key) for key in common if flags.get(key) != METHOD_PRESETS[method].get(key)}
        if mismatches:
            issues.append(_issue("E_B8_P_COMMON_FLAGS", "B8/P must differ only in storage representation.", observed=mismatches))

    result = {"status": "passed" if not issues else "failed", "isValid": not issues, "normalizedCase": normalized, "issues": issues}
    if issues:
        raise FormalRunError("Formal case contract failed: " + ", ".join(item["code"] for item in issues[:10]), EXIT_CONFIGURATION_FAILURE, issues)
    return result


def collect_hardware_probe(profile_name: str = "", phase: str = "") -> dict[str, Any]:
    payload: dict[str, Any] = {
        "profile": profile_name,
        "phase": phase,
        "timestampUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "platform": platform.platform(),
        "osBuild": platform.version(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "python": platform.python_version(),
        "logicalCpuCount": os.cpu_count(),
        "gpu": {},
        "power": {},
    }
    if sys.platform != "win32":
        return payload
    try:
        result = subprocess.run([
            "nvidia-smi",
            "--query-gpu=name,driver_version,temperature.gpu,clocks.current.graphics,clocks.current.memory,power.draw,power.limit,memory.total,memory.used",
            "--format=csv,noheader,nounits",
        ], capture_output=True, text=True, timeout=10, check=False)
        fields = [item.strip() for item in result.stdout.strip().splitlines()[0].split(",")] if result.stdout.strip() else []
        if len(fields) >= 9:
            payload["gpu"] = {
                "name": fields[0], "driverVersion": fields[1],
                "temperatureC": _optional_float(fields[2]),
                "graphicsClockMHz": _optional_float(fields[3]),
                "memoryClockMHz": _optional_float(fields[4]),
                "powerDrawW": _optional_float(fields[5]),
                "powerLimitW": _optional_float(fields[6]),
                "memoryTotalMiB": _optional_float(fields[7]),
                "memoryUsedMiB": _optional_float(fields[8]),
            }
    except Exception as exc:
        payload["gpuProbeError"] = str(exc)
    try:
        result = subprocess.run(["powercfg", "/GETACTIVESCHEME"], capture_output=True, text=True, timeout=10, check=False)
        payload["power"] = {"activeScheme": result.stdout.strip()}
    except Exception as exc:
        payload["powerProbeError"] = str(exc)
    return payload


class HardwareSampler:
    def __init__(self, profile_name: str, interval_seconds: float = 5.0):
        self.profile_name = profile_name
        self.interval_seconds = max(float(interval_seconds), 1.0)
        self.samples: list[dict[str, Any]] = []
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self) -> list[dict[str, Any]]:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=self.interval_seconds + 5.0)
        return list(self.samples)

    def _run(self) -> None:
        while not self._stop.is_set():
            sample = collect_hardware_probe(self.profile_name, "sample")
            sample["sampleTimeUnix"] = time.time()
            self.samples.append(sample)
            self._stop.wait(self.interval_seconds)


def write_hardware_timeseries(path: Path, samples: list[dict[str, Any]]) -> None:
    headers = [
        "sampleTimeUnix", "gpuName", "driverVersion", "temperatureC",
        "graphicsClockMHz", "memoryClockMHz", "powerDrawW", "powerLimitW",
        "memoryTotalMiB", "memoryUsedMiB", "activePowerScheme",
    ]
    rows: list[dict[str, Any]] = []
    for sample in samples:
        gpu = sample.get("gpu", {}) if isinstance(sample.get("gpu"), dict) else {}
        power = sample.get("power", {}) if isinstance(sample.get("power"), dict) else {}
        rows.append({
            "sampleTimeUnix": sample.get("sampleTimeUnix"),
            "gpuName": gpu.get("name"), "driverVersion": gpu.get("driverVersion"),
            "temperatureC": gpu.get("temperatureC"), "graphicsClockMHz": gpu.get("graphicsClockMHz"),
            "memoryClockMHz": gpu.get("memoryClockMHz"), "powerDrawW": gpu.get("powerDrawW"),
            "powerLimitW": gpu.get("powerLimitW"), "memoryTotalMiB": gpu.get("memoryTotalMiB"),
            "memoryUsedMiB": gpu.get("memoryUsedMiB"), "activePowerScheme": power.get("activeScheme"),
        })
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + f".tmp.{os.getpid()}")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=headers)
            writer.writeheader()
            writer.writerows(rows)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def _settings_match(expected: Any, observed: Any) -> bool:
    if isinstance(expected, bool):
        return _as_bool(observed) == expected
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        number = _optional_float(observed)
        tolerance = 1.0e-6 * max(1.0, abs(float(expected)))
        return number is not None and abs(number - float(expected)) <= tolerance
    return str(observed) == str(expected)


def derive_method_code(flags: Mapping[str, Any]) -> str:
    candidates: list[str] = []
    present = {key for key in METHOD_FLAG_KEYS if key in flags}
    if not present:
        return "UNKNOWN"
    for code, preset in METHOD_PRESETS.items():
        if all(_settings_match(preset.get(key), flags.get(key)) for key in present):
            candidates.append(code)
    return candidates[0] if len(candidates) == 1 else "UNKNOWN"


def _expected_runtime_settings(case: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "renderWidth": _required_int(case.get("width")),
        "renderHeight": _required_int(case.get("height")),
        "displayStrandCount": _required_int(case.get("display_strand_count")),
        "guideCount": _required_int(case.get("guide_strand_count")),
        "particlesPerGuide": _required_int(case.get("particles_per_guide")),
        "solverIterations": _required_int(case.get("solver_iterations")),
        "vsync": _as_bool(case.get("vsync")),
        "presentSyncInterval": _required_int(case.get("present_sync_interval")),
    }


def _compare_settings(settings: dict[str, Any], case: Mapping[str, Any]) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    requested = dict(settings.get("requested", {})) if isinstance(settings.get("requested"), dict) else {}
    applied = dict(settings.get("applied", {})) if isinstance(settings.get("applied"), dict) else {}
    observed_flags = dict(settings.get("observedFlags", {})) if isinstance(settings.get("observedFlags"), dict) else {}
    observed = dict(settings.get("observed", {})) if isinstance(settings.get("observed"), dict) else {}

    expected_settings = dict(case.get("method_settings", {}))
    expected_flags = {key: expected_settings.get(key) for key in METHOD_FLAG_KEYS}
    for evidence_name, evidence in (
        ("requested", requested), ("applied", applied), ("observed", observed_flags)
    ):
        for key in METHOD_FLAG_KEYS:
            if key not in expected_settings:
                issues.append(_issue(
                    "E_EXPECTED_FLAG_MISSING",
                    f"Formal method preset lacks required flag: {key}",
                ))
                continue
            if key not in evidence:
                issues.append(_issue(
                    f"E_{evidence_name.upper()}_FLAG_MISSING",
                    f"{evidence_name.capitalize()} method flag is missing: {key}",
                ))
                continue
            if not _settings_match(expected_flags[key], evidence[key]):
                issues.append(_issue(
                    f"E_{evidence_name.upper()}_SETTING_MISMATCH",
                    f"{evidence_name.capitalize()} feature flag mismatch: {key}",
                    expected=expected_flags[key], observed=evidence[key],
                ))

    runtime_expected = _expected_runtime_settings(case)
    for key, expected in runtime_expected.items():
        if key not in observed:
            issues.append(_issue(
                "E_OBSERVED_SETTING_MISSING",
                f"Observed runtime setting is missing: {key}",
            ))
        elif not _settings_match(expected, observed[key]):
            issues.append(_issue(
                "E_OBSERVED_SETTING_MISMATCH",
                f"Observed runtime setting mismatch: {key}",
                expected=expected, observed=observed[key],
            ))

    expected_hash = canonical_json_sha256(expected_settings)
    hash_fields = {
        "requested": str(settings.get("requestedMethodSettingsSha256") or ""),
        "applied": str(settings.get("appliedMethodSettingsSha256") or settings.get("methodSettingsSha256") or ""),
        "observed": str(settings.get("observedMethodSettingsSha256") or ""),
    }
    for name, value in hash_fields.items():
        if not value:
            issues.append(_issue(
                f"E_{name.upper()}_METHOD_HASH_MISSING",
                f"{name.capitalize()} method-settings hash is missing.",
            ))
        elif value.lower() != expected_hash.lower():
            issues.append(_issue(
                f"E_{name.upper()}_METHOD_HASH_MISMATCH",
                f"{name.capitalize()} method-settings hash mismatch.",
                expected=expected_hash, observed=value,
            ))

    requested_method = normalize_method_code(str(case.get("method", "")))
    applied_method = derive_method_code(applied)
    observed_method = derive_method_code(observed_flags)
    if applied_method != requested_method:
        issues.append(_issue(
            "E_APPLIED_METHOD_MISMATCH",
            "Applied method differs from requested method.",
            expected=requested_method, observed=applied_method,
        ))
    if observed_method != requested_method:
        issues.append(_issue(
            "E_OBSERVED_METHOD_MISMATCH",
            "Observed method differs from requested method.",
            expected=requested_method, observed=observed_method,
        ))
    if not _as_bool(settings.get("methodFlagsMatched")):
        issues.append(_issue(
            "E_VIEWER_METHOD_ACKNOWLEDGEMENT",
            "Viewer did not acknowledge the complete method flag set.",
        ))

    normalized = dict(settings)
    normalized.update({
        "requested": requested,
        "applied": applied,
        "observedFlags": observed_flags,
        "observed": observed,
        "requestedMethodCode": requested_method,
        "appliedMethodCode": applied_method,
        "observedMethodCode": observed_method,
        "methodFlagsMatched": not any(
            item["code"].startswith(("E_REQUESTED", "E_APPLIED", "E_OBSERVED", "E_VIEWER"))
            for item in issues
        ),
        "methodCodeEvidence": "full_requested_applied_observed_flags",
        "unexportedAppliedFlags": [],
        "evidenceWarnings": [],
    })
    return issues, normalized


def _availability_coverage(rows: list[dict[str, Any]], bit: int) -> float:
    if not rows:
        return 0.0
    available = 0
    for row in rows:
        mask = _required_int(_metric(row, "metricAvailabilityMask", "availabilityMask", default=0))
        available += 1 if mask & bit else 0
    return available / len(rows)


def _available_values(rows: list[dict[str, Any]], keys: tuple[str, ...], bit: int | None = None) -> list[float]:
    values: list[float] = []
    for row in rows:
        if bit is not None:
            mask = _required_int(_metric(row, "metricAvailabilityMask", "availabilityMask", default=0))
            if not mask & bit:
                continue
        value = _optional_float(_metric(row, *keys))
        if value is not None:
            values.append(value)
    return values


def _sum_available(rows: list[dict[str, Any]], keys: tuple[str, ...], bit: int | None = None) -> tuple[int, bool]:
    total = 0
    found = False
    for row in rows:
        if bit is not None:
            mask = _required_int(_metric(row, "metricAvailabilityMask", "availabilityMask", default=0))
            if not mask & bit:
                continue
        value = _optional_int(_metric(row, *keys))
        if value is not None:
            total += value
            found = True
    return total, found


def _aggregate_frame_metrics(rows: list[dict[str, Any]], summary: Mapping[str, Any]) -> dict[str, Any]:
    frame_ms = [value for value in _available_values(rows, ("frameMilliseconds", "frame_ms")) if value > 0.0]
    gpu_ms = _available_values(rows, ("totalHairGpuMilliseconds", "gpuHairMilliseconds", "total_hair_gpu_ms"), AVAILABILITY_BITS["gpuTiming"])
    static_cached, _ = _sum_available(rows, ("staticCachedGroupCount",))
    static_transformed, _ = _sum_available(rows, ("staticTransformedGroupCount",))
    static_count = static_cached + static_transformed
    false_static, false_static_found = _sum_available(rows, ("falseStaticCount",), AVAILABILITY_BITS["falseStatic"])
    repair_attempts, repair_attempts_found = _sum_available(rows, ("repairAttemptCount",))
    repair_success, repair_success_found = _sum_available(rows, ("repairSuccessCount", "repairExecutionSuccessCount"), AVAILABILITY_BITS["repair"])
    repair_verified, repair_verified_found = _sum_available(rows, ("repairVerifiedCount",), AVAILABILITY_BITS["repair"])
    repair_recurrence, repair_recurrence_found = _sum_available(rows, ("repairRecurrenceCount",), AVAILABILITY_BITS["repair"])

    repair_source = "frames"
    if not repair_verified_found and _optional_int(summary.get("repairVerifiedCount")) is not None:
        repair_verified = _required_int(summary.get("repairVerifiedCount"))
        repair_verified_found = True
        repair_source = "summary_final_counter"
    if not repair_recurrence_found and _optional_int(summary.get("repairRecurrenceCount")) is not None:
        repair_recurrence = _required_int(summary.get("repairRecurrenceCount"))
        repair_recurrence_found = True
        repair_source = "summary_final_counter"
    if repair_source == "summary_final_counter" and _optional_int(summary.get("repairAttemptCount")) is not None:
        repair_attempts = _required_int(summary.get("repairAttemptCount"))
        repair_attempts_found = True
    if repair_source == "summary_final_counter" and _optional_int(summary.get("repairSuccessCount")) is not None:
        repair_success = _required_int(summary.get("repairSuccessCount"))
        repair_success_found = True

    wake_tp, _ = _sum_available(rows, ("predictiveWakeTruePositiveCount",), AVAILABILITY_BITS["wake"])
    wake_fp, _ = _sum_available(rows, ("predictiveWakeFalsePositiveCount",), AVAILABILITY_BITS["wake"])
    wake_fn, _ = _sum_available(rows, ("predictiveWakeFalseNegativeCount",), AVAILABILITY_BITS["wake"])
    collision_candidates, _ = _sum_available(rows, ("collisionCandidateCount",))
    collision_misses, _ = _sum_available(rows, ("collisionMissCount",), AVAILABILITY_BITS["collision"])
    transitions, _ = _sum_available(rows, ("stateTransitionCount",))
    oscillations, _ = _sum_available(rows, ("stateOscillationCount",))
    boundary_values = _available_values(rows, ("staticDynamicBoundaryMaxErrorMm",), AVAILABILITY_BITS["boundary"])
    wake_delays = _available_values(rows, ("wakeDelayFrameMax",), AVAILABILITY_BITS["wake"])
    shared_conflicts, shared_found = _sum_available(rows, ("sharedPointConflictCount",), AVAILABILITY_BITS["boundary"])
    display_counts = _available_values(rows, ("totalStrandCount", "displayStrandCount"))

    repair_available = _availability_coverage(rows, AVAILABILITY_BITS["repair"]) > 0.0
    false_static_available = _availability_coverage(rows, AVAILABILITY_BITS["falseStatic"]) > 0.0
    return {
        "sampleCount": len(rows),
        "averageFrameMilliseconds": sum(frame_ms) / len(frame_ms) if frame_ms else None,
        "p50FrameMilliseconds": _percentile(frame_ms, 50.0),
        "p95FrameMilliseconds": _percentile(frame_ms, 95.0),
        "p99FrameMilliseconds": _percentile(frame_ms, 99.0),
        "p999FrameMilliseconds": _percentile(frame_ms, 99.9),
        "maximumFrameMilliseconds": max(frame_ms) if frame_ms else None,
        "frameBudgetOverrunRate": _safe_rate(sum(value > 16.67 for value in frame_ms), len(frame_ms)),
        "averageHairGpuMilliseconds": sum(gpu_ms) / len(gpu_ms) if gpu_ms else None,
        "p95HairGpuMilliseconds": _percentile(gpu_ms, 95.0),
        "p99HairGpuMilliseconds": _percentile(gpu_ms, 99.0),
        "falseStaticRate": _safe_rate(false_static, static_count) if false_static_available and false_static_found else None,
        "repairExecutionSuccessRate": _safe_rate(repair_success, repair_attempts) if repair_available and repair_success_found and repair_attempts_found else None,
        "repairVerifiedSuccessRate": _safe_rate(repair_verified, repair_attempts) if repair_available and repair_verified_found and repair_attempts_found else None,
        "repairRecurrenceRate": _safe_rate(repair_recurrence, repair_attempts) if repair_available and repair_recurrence_found and repair_attempts_found else None,
        "predictiveWakePrecision": _safe_rate(wake_tp, wake_tp + wake_fp) if _availability_coverage(rows, AVAILABILITY_BITS["wake"]) > 0 else None,
        "predictiveWakeRecall": _safe_rate(wake_tp, wake_tp + wake_fn) if _availability_coverage(rows, AVAILABILITY_BITS["wake"]) > 0 else None,
        "wakeDelayFrameMax": max(wake_delays) if wake_delays else None,
        "collisionCandidateCount": collision_candidates if _availability_coverage(rows, AVAILABILITY_BITS["collision"]) > 0 else None,
        "collisionMissRate": _safe_rate(collision_misses, collision_candidates) if _availability_coverage(rows, AVAILABILITY_BITS["collision"]) > 0 else None,
        "stateOscillationRate": _safe_rate(oscillations, transitions),
        "staticDynamicBoundaryMaxErrorMm": max(boundary_values) if boundary_values else None,
        "sharedPointConflictCount": shared_conflicts if shared_found else None,
        "displayStrandCount": int(round(max(display_counts))) if display_counts else None,
        "gpuTimingCoverage": _availability_coverage(rows, AVAILABILITY_BITS["gpuTiming"]),
        "availabilityCoverage": {name: _availability_coverage(rows, bit) for name, bit in AVAILABILITY_BITS.items()},
        "evidenceProvenance": {
            "falseStatic": "dedicated_bit12_only",
            "repair": repair_source if repair_verified_found or repair_recurrence_found else "unavailable",
            "csvJsonlMerge": True,
            "formalEvidenceSidecarMerged": any(_as_bool(row.get("formalEvidenceMerged")) for row in rows),
            "proxyMetricsUsed": False,
        },
        "counts": {
            "falseStatic": false_static, "staticGroups": static_count,
            "repairAttempts": repair_attempts, "repairSuccess": repair_success,
            "repairVerified": repair_verified if repair_verified_found else None,
            "repairRecurrence": repair_recurrence if repair_recurrence_found else None,
            "wakeTruePositive": wake_tp, "wakeFalsePositive": wake_fp, "wakeFalseNegative": wake_fn,
            "collisionCandidates": collision_candidates, "collisionMisses": collision_misses,
            "stateTransitions": transitions, "stateOscillations": oscillations,
        },
    }


def _evaluate_quality_gates(aggregate: Mapping[str, Any], case: Mapping[str, Any]) -> list[dict[str, Any]]:
    thresholds: dict[str, float] = {
        "falseStaticRate": 0.05,
        "repairVerifiedSuccessRate": 0.95,
        "repairRecurrenceRate": 0.05,
        "predictiveWakeRecall": 0.95,
        "collisionMissRate": 0.01,
        "stateOscillationRate": 0.02,
        "staticDynamicBoundaryMaxErrorMm": 1.0,
    }
    thresholds.update({key: float(value) for key, value in dict(case.get("quality_gates", {})).items()})
    required = set(case.get("required_quality_metrics", []))
    issues: list[dict[str, Any]] = []
    maximum = {"falseStaticRate", "repairRecurrenceRate", "collisionMissRate", "stateOscillationRate", "staticDynamicBoundaryMaxErrorMm"}
    minimum = {"repairVerifiedSuccessRate", "predictiveWakeRecall"}
    for key in sorted(set(thresholds) | required):
        observed = aggregate.get(key)
        if observed is None:
            if key in required:
                issues.append(_issue("E_QUALITY_METRIC_UNAVAILABLE", f"Required quality metric is unavailable: {key}"))
            continue
        threshold = thresholds.get(key)
        if threshold is None:
            continue
        if key in maximum and float(observed) > threshold:
            issues.append(_issue("E_QUALITY_GATE", f"Quality metric exceeds maximum: {key}", expected=threshold, observed=observed))
        elif key in minimum and float(observed) < threshold:
            issues.append(_issue("E_QUALITY_GATE", f"Quality metric is below minimum: {key}", expected=threshold, observed=observed))
    return issues


def _evaluate_environment(environment: Mapping[str, Any], case: Mapping[str, Any]) -> tuple[bool, bool, list[dict[str, Any]]]:
    issues: list[dict[str, Any]] = []
    if not environment:
        return False, False, [_issue("E_ENVIRONMENT_MISSING", "Hardware environment evidence is missing.")]
    start = environment.get("start", {}) if isinstance(environment.get("start"), dict) else {}
    end = environment.get("end", {}) if isinstance(environment.get("end"), dict) else {}
    samples = environment.get("samples", []) if isinstance(environment.get("samples"), list) else []
    gpu = start.get("gpu", {}) if isinstance(start.get("gpu"), dict) else {}
    if not gpu and isinstance(end.get("gpu"), dict):
        gpu = end.get("gpu", {})
    environment_valid = bool(gpu.get("name") and gpu.get("driverVersion") and gpu.get("memoryTotalMiB"))
    if sys.platform == "win32" and not environment_valid:
        issues.append(_issue("E_ENVIRONMENT_FIELDS", "GPU name, driver, and VRAM are required."))
    temperatures: list[float] = []
    clocks: list[float] = []
    for sample in samples:
        sample_gpu = sample.get("gpu", {}) if isinstance(sample, dict) and isinstance(sample.get("gpu"), dict) else {}
        temperature = _optional_float(sample_gpu.get("temperatureC"))
        clock = _optional_float(sample_gpu.get("graphicsClockMHz"))
        if temperature is not None:
            temperatures.append(temperature)
        if clock is not None:
            clocks.append(clock)
    thermal_valid = True
    maximum_temperature = _required_float(case.get("maximum_gpu_temperature_c"), 88.0)
    if temperatures and max(temperatures) > maximum_temperature:
        thermal_valid = False
        issues.append(_issue("E_THERMAL_TEMPERATURE", "GPU temperature exceeded the formal limit.", expected=maximum_temperature, observed=max(temperatures)))
    maximum_drop = _required_float(case.get("maximum_gpu_clock_drop_ratio"), 0.15)
    if len(clocks) >= 3:
        reference = sorted(clocks)[len(clocks) // 2]
        drop = (reference - min(clocks)) / reference if reference > 0 else 0.0
        if drop > maximum_drop:
            thermal_valid = False
            issues.append(_issue("E_THERMAL_CLOCK_DROP", "GPU clock drop exceeded the formal limit.", expected=maximum_drop, observed=drop))
    return environment_valid, thermal_valid, issues


def _canonical_summary(summary: Mapping[str, Any]) -> dict[str, Any]:
    result = dict(summary)
    for key in ("canonicalResultSha256", "validationReasons", "formalAggregateMetrics", "resultManifestSha256", "resultManifestPath", "formalExitCode", "validationStatus", "status"):
        result.pop(key, None)
    return result


def _write_artifact_manifest(directory: Path, run_name: str, artifact_paths: list[Path], canonical_result_sha256: str) -> Path:
    entries = []
    for path in artifact_paths:
        path = Path(path)
        if path.is_file():
            entries.append({"path": path.name, "sizeBytes": path.stat().st_size, "sha256": sha256_file(path)})
    payload: dict[str, Any] = {
        "manifestVersion": VALIDATION_VERSION,
        "runName": run_name,
        "canonicalResultSha256": canonical_result_sha256,
        "artifacts": sorted(entries, key=lambda item: item["path"]),
    }
    payload["manifestSha256"] = canonical_json_sha256(payload)
    path = Path(directory) / f"{run_name}_manifest.json"
    write_json(path, payload)
    return path


def validate_required_outputs(
    summary_json: Path,
    frame_csv: Path,
    frame_jsonl: Path,
    settings_json: Path,
    run_name: str,
    *,
    case: Mapping[str, Any] | None = None,
    environment_json: Path | None = None,
) -> dict[str, Any]:
    normalized = _normalize_case(case or {})
    required_paths = [Path(summary_json), Path(frame_csv), Path(frame_jsonl), Path(settings_json)]
    for path in required_paths:
        if not path.is_file() or path.stat().st_size <= 0:
            raise FormalRunError(f"Required output is missing or empty: {path}", EXIT_OUTPUT_INTEGRITY_FAILURE)

    summary = _read_json(Path(summary_json))
    settings = _read_json(Path(settings_json))
    json_rows = _read_jsonl(Path(frame_jsonl))
    csv_rows = _read_csv_rows(Path(frame_csv))
    evidence_path = Path(summary_json).parent / f"{run_name}_formal_evidence.jsonl"
    evidence_rows = _read_jsonl(evidence_path) if evidence_path.is_file() else []
    issues: list[dict[str, Any]] = []
    integrity_issues: list[dict[str, Any]] = _validate_frame_schema_and_parity(json_rows, csv_rows)
    rows = _merge_formal_evidence(_merge_frame_rows(json_rows, csv_rows), evidence_rows)

    expected_frames = _required_int(normalized.get("measurement_frames"), _required_int(summary.get("measurement_frames", summary.get("measurementFrames"))))
    if len(json_rows) != expected_frames:
        integrity_issues.append(_issue("E_JSONL_FRAME_COUNT", "JSONL row count is not exact.", expected=expected_frames, observed=len(json_rows)))
    if len(csv_rows) != expected_frames:
        integrity_issues.append(_issue("E_CSV_FRAME_COUNT", "CSV row count is not exact.", expected=expected_frames, observed=len(csv_rows)))

    expected_case_id = str(normalized.get("case_id", ""))
    observed_case_id = str(summary.get("caseId") or summary.get("case_id") or "")
    if observed_case_id != expected_case_id:
        issues.append(_issue("E_CASE_ID", "Summary case id mismatch.", expected=expected_case_id, observed=observed_case_id))
    if str(summary.get("settingsSha256") or "").lower() != str(normalized.get("settings_hash") or "").lower():
        issues.append(_issue("E_SETTINGS_HASH", "Summary settings hash mismatch.", expected=normalized.get("settings_hash"), observed=summary.get("settingsSha256")))
    if not _as_bool(summary.get("realMeasurement")):
        issues.append(_issue("E_NOT_REAL_MEASUREMENT", "Viewer did not certify this as a real measurement."))

    settings_issues, normalized_settings = _compare_settings(settings, normalized)
    issues.extend(settings_issues)
    write_json(Path(settings_json), normalized_settings)

    source_indices = [_optional_int(_metric(row, "sourceFrameIndex", "globalFrameIndex")) for row in rows]
    if any(value is None for value in source_indices):
        integrity_issues.append(_issue("E_SOURCE_FRAME_MISSING", "One or more source frame indices are missing."))
    elif any(current <= previous for previous, current in zip(source_indices, source_indices[1:])):
        integrity_issues.append(_issue("E_SOURCE_FRAME_SEQUENCE", "Source frame indices are not strictly increasing."))

    for index, row in enumerate(rows):
        if _required_int(row.get("fatalFailure")) != 0 or _required_int(row.get("fatalFailureCode")) != 0:
            integrity_issues.append(_issue("E_FATAL_FRAME", f"Frame {index} reports a fatal failure."))
            break
        if _required_int(row.get("syntheticMetricCount")) != 0:
            integrity_issues.append(_issue("E_SYNTHETIC_METRIC", f"Frame {index} contains synthetic metrics."))
            break

    aggregate = _aggregate_frame_metrics(rows, summary)
    minimum_gpu_coverage = _required_float(normalized.get("minimum_gpu_timing_coverage"), 0.99)
    if _required_float(aggregate.get("gpuTimingCoverage")) < minimum_gpu_coverage:
        issues.append(_issue("E_GPU_TIMING_COVERAGE", "GPU timing coverage is below the formal minimum.", expected=minimum_gpu_coverage, observed=aggregate.get("gpuTimingCoverage")))

    minimum_metric_coverage = _required_float(normalized.get("minimum_metric_coverage"), 0.99)
    for metric in normalized.get("required_quality_metrics", []):
        availability_name = METRIC_AVAILABILITY.get(metric)
        if availability_name:
            coverage = aggregate.get("availabilityCoverage", {}).get(availability_name, 0.0)
            if float(coverage) < minimum_metric_coverage:
                issues.append(_issue("E_REQUIRED_METRIC_COVERAGE", f"Required evidence coverage is too low: {metric}", expected=minimum_metric_coverage, observed=coverage))

    environment: dict[str, Any] = {}
    if environment_json and Path(environment_json).is_file():
        environment = _read_json(Path(environment_json))
    environment_valid, thermal_valid, environment_issues = _evaluate_environment(environment, normalized)
    issues.extend(environment_issues)
    quality_issues = _evaluate_quality_gates(aggregate, normalized)

    canonical_payload = {
        "summary": _canonical_summary(summary),
        "settings": normalized_settings,
        "frameCsvSha256": sha256_file(Path(frame_csv)),
        "frameJsonlSha256": sha256_file(Path(frame_jsonl)),
        "environmentSha256": sha256_file(Path(environment_json)) if environment_json and Path(environment_json).is_file() else "",
        "formalEvidenceSha256": sha256_file(evidence_path) if evidence_path.is_file() else "",
        "aggregate": aggregate,
    }
    try:
        canonical_result_sha256 = canonical_json_sha256(canonical_payload)
    except (TypeError, ValueError) as exc:
        integrity_issues.append(_issue("E_CANONICAL_JSON", "Canonical result could not be serialized.", observed=str(exc)))
        canonical_result_sha256 = ""

    if integrity_issues:
        status, exit_code = "complete_invalid", EXIT_OUTPUT_INTEGRITY_FAILURE
    elif issues:
        status, exit_code = "complete_invalid", EXIT_FORMAL_CONTRACT_INVALID
    elif quality_issues:
        status, exit_code = "quality_gate_failed", EXIT_QUALITY_GATE_FAILED
    else:
        status, exit_code = "complete_valid", EXIT_COMPLETE_VALID

    summary.update({
        "validationStatus": status, "status": status, "formalExitCode": exit_code,
        "canonicalResultSha256": canonical_result_sha256,
        "formalAggregateMetrics": aggregate,
        "validationReasons": issues + integrity_issues + quality_issues,
        "requestedMethodCode": normalized_settings.get("requestedMethodCode"),
        "appliedMethodCode": normalized_settings.get("appliedMethodCode"),
        "observedMethodCode": normalized_settings.get("observedMethodCode"),
        "gpuTimingCoverage": aggregate.get("gpuTimingCoverage"),
        "avg_frame_ms": aggregate.get("averageFrameMilliseconds"),
        "p50_frame_ms": aggregate.get("p50FrameMilliseconds"),
        "p95_frame_ms": aggregate.get("p95FrameMilliseconds"),
        "p99_frame_ms": aggregate.get("p99FrameMilliseconds"),
        "p999_frame_ms": aggregate.get("p999FrameMilliseconds"),
        "worst_frame_ms": aggregate.get("maximumFrameMilliseconds"),
        "frameOverrunRate": aggregate.get("frameBudgetOverrunRate"),
        "avg_hair_gpu_ms": aggregate.get("averageHairGpuMilliseconds"),
        "p95_hair_gpu_ms": aggregate.get("p95HairGpuMilliseconds"),
        "p99_hair_gpu_ms": aggregate.get("p99HairGpuMilliseconds"),
        "falseStaticRate": aggregate.get("falseStaticRate"),
        "predictiveWakePrecision": aggregate.get("predictiveWakePrecision"),
        "predictiveWakeRecall": aggregate.get("predictiveWakeRecall"),
        "repairExecutionSuccessRate": aggregate.get("repairExecutionSuccessRate"),
        "repairVerifiedSuccessRate": aggregate.get("repairVerifiedSuccessRate"),
        "repairRecurrenceRate": aggregate.get("repairRecurrenceRate"),
        "collisionMissRate": aggregate.get("collisionMissRate"),
        "stateOscillationRate": aggregate.get("stateOscillationRate"),
        "staticDynamicBoundaryMaxErrorMm": aggregate.get("staticDynamicBoundaryMaxErrorMm"),
    })
    write_json(Path(summary_json), summary)

    validation_path = Path(summary_json).parent / f"{run_name}_validation.json"
    validation: dict[str, Any] = {
        "validationVersion": VALIDATION_VERSION,
        "runName": run_name,
        "status": status,
        "isValid": exit_code == EXIT_COMPLETE_VALID,
        "formalContractValid": not (issues or integrity_issues),
        "qualityGatePassed": not quality_issues,
        "thermalValid": thermal_valid,
        "environmentValid": environment_valid,
        "requiredMetricCoverageValid": not any(item["code"] == "E_REQUIRED_METRIC_COVERAGE" for item in issues),
        "exitCode": exit_code,
        "realMeasurement": _as_bool(summary.get("realMeasurement")),
        "frameCount": len(json_rows), "csvFrameCount": len(csv_rows), "expectedFrameCount": expected_frames,
        "gpuTimingCoverage": aggregate.get("gpuTimingCoverage"),
        "canonicalResultSha256": canonical_result_sha256,
        "aggregateMetrics": aggregate,
        "issues": issues + integrity_issues,
        "qualityGateIssues": quality_issues,
        "resultManifestPath": f"{run_name}_manifest.json",
    }
    write_json(validation_path, validation)

    artifacts = [Path(summary_json), Path(frame_csv), Path(frame_jsonl), Path(settings_json), validation_path]
    if environment_json and Path(environment_json).is_file():
        artifacts.append(Path(environment_json))
    if evidence_path.is_file():
        artifacts.append(evidence_path)
    hardware_csv = Path(summary_json).parent / f"{run_name}_hardware_timeseries.csv"
    if hardware_csv.is_file():
        artifacts.append(hardware_csv)
    _write_artifact_manifest(Path(summary_json).parent, run_name, artifacts, canonical_result_sha256)

    if exit_code != EXIT_COMPLETE_VALID:
        reasons = issues + integrity_issues + quality_issues
        raise FormalRunError(f"Run '{run_name}' failed formal validation: " + ", ".join(item["code"] for item in reasons[:10]), exit_code, reasons)
    return validation


def _env_token(name: str) -> str:
    step1 = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    return re.sub(r"[^A-Za-z0-9]+", "_", step1).strip("_").upper()


def _env_value(value: Any) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    return str(value)


def _set_if_present(env: dict[str, str], key: str, value: Any) -> None:
    if value not in (None, ""):
        env[key] = _env_value(value)


def _git_commit_sha(repo_root: Path) -> str:
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo_root, capture_output=True, text=True, timeout=10, check=False).stdout.strip()
    except Exception:
        return ""


def _write_case_snapshot(telemetry_dir: Path, run_name: str, case: Mapping[str, Any], viewer_exe: Path) -> Path:
    snapshot = _normalize_case(case)
    repo_root = Path(__file__).resolve().parents[2]
    snapshot["run_name"] = run_name
    snapshot["commitSha"] = str(snapshot.get("commitSha") or snapshot.get("commit_sha") or _git_commit_sha(repo_root))
    snapshot["viewerExecutable"] = str(viewer_exe)
    snapshot["executableSha256"] = sha256_file(viewer_exe)
    root_shader, bundled_shader = _formal_shader_paths(viewer_exe)
    snapshot["segmentGroupShaderPath"] = str(root_shader)
    snapshot["segmentGroupShaderSha256"] = sha256_file(root_shader)
    snapshot["shaderBundleSha256"] = sha256_tree([root_shader, viewer_exe.parent / "shaders"], base_path=viewer_exe.parent)
    snapshot["requestedMethodSettingsSha256"] = canonical_json_sha256(snapshot.get("method_settings", {}))
    timeline = _resolve_case_path(snapshot.get("scenario_timeline"))
    if timeline and timeline.is_file():
        snapshot["scenario_timeline"] = str(timeline)
        snapshot["scenarioTimelinePath"] = str(timeline)
        snapshot["scenarioTimelineSha256"] = sha256_file(timeline)
    asset_manifest = _resolve_case_path(snapshot.get("asset_manifest"))
    if asset_manifest and asset_manifest.is_file():
        snapshot["asset_manifest"] = str(asset_manifest)
        snapshot["assetManifestPath"] = str(asset_manifest)
        snapshot["assetManifestSha256"] = sha256_file(asset_manifest)
        manifest = _read_json(asset_manifest)
        snapshot["assetSha256"] = str(manifest.get("sourceAssetSha256") or "")
        snapshot["assetFrozen"] = str(manifest.get("freezeStatus") or "") == "FROZEN"
    path = Path(telemetry_dir) / f"{run_name}_case.json"
    write_json(path, snapshot)
    return path


def run_viewer(viewer_exe: Path, telemetry_dir: Path, run_name: str, timeout_seconds: int, case: Mapping[str, Any] | None = None) -> tuple[Path, Path, Path, Path, Path]:
    viewer_exe = Path(viewer_exe).resolve()
    normalized_case = _normalize_case(case or {})
    preflight = validate_case_contract(normalized_case, viewer_exe)
    if sys.platform != "win32":
        raise FormalRunError("The DirectX 11 formal viewer can only be launched on Windows.", EXIT_RUNTIME_FAILURE)

    telemetry_dir = Path(telemetry_dir).resolve()
    telemetry_dir.mkdir(parents=True, exist_ok=True)
    paths = {
        "summary": telemetry_dir / f"{run_name}_summary.json",
        "csv": telemetry_dir / f"{run_name}_frames.csv",
        "jsonl": telemetry_dir / f"{run_name}_frames.jsonl",
        "settings": telemetry_dir / f"{run_name}_settings.json",
        "environment": telemetry_dir / f"{run_name}_environment.json",
        "validation": telemetry_dir / f"{run_name}_validation.json",
        "manifest": telemetry_dir / f"{run_name}_manifest.json",
        "formal_evidence": telemetry_dir / f"{run_name}_formal_evidence.jsonl",
        "error": telemetry_dir / f"{run_name}_error.json",
    }
    for key in ("summary", "csv", "jsonl", "settings", "environment", "validation", "manifest", "formal_evidence", "error"):
        paths[key].unlink(missing_ok=True)
    write_json(telemetry_dir / f"{run_name}_preflight.json", preflight)
    case_snapshot_path = _write_case_snapshot(telemetry_dir, run_name, normalized_case, viewer_exe)
    snapshot = _read_json(case_snapshot_path)

    env = os.environ.copy()
    env.update({
        "HAIRGPU_AUTO_TELEMETRY": "1",
        "HAIRGPU_TELEMETRY_DIR": str(telemetry_dir),
        "HAIRGPU_TELEMETRY_RUN_NAME": run_name,
        "HAIRGPU_CASE_JSON": str(case_snapshot_path),
        "HAIRGPU_EXPERIMENT_CASE_ID": str(snapshot.get("case_id", "")),
        "HAIRGPU_VARIANT": str(snapshot.get("method", "")),
        "HAIRGPU_SCENARIO": str(snapshot.get("scenario", "")),
        "HAIRGPU_SEED": str(snapshot.get("seed", 0)),
        "HAIRGPU_WARMUP_FRAMES": str(snapshot.get("warmup_frames", 600)),
        "HAIRGPU_MEASUREMENT_FRAMES": str(snapshot.get("measurement_frames", 6000)),
        "HAIRGPU_AUTO_EXIT_AFTER_CAPTURE": "1",
        "HAIRGPU_SETTINGS_HASH": str(snapshot.get("settings_hash", "")),
        "HAIRGPU_EXECUTABLE_SHA256": str(snapshot.get("executableSha256", "")),
        "HAIRGPU_COMMIT_SHA": str(snapshot.get("commitSha", "")),
        "HAIRGPU_BUILD_CONFIGURATION": str(snapshot.get("build_configuration", "Release")),
        "HAIRGPU_PLATFORM": str(snapshot.get("platform", "x64")),
        "HAIRGPU_HARDWARE_PROFILE": str(snapshot.get("hardware_profile", "")),
        "HAIRGPU_MEASUREMENT_MODE": str(snapshot.get("measurement_mode", "performance")),
        "HAIRGPU_REPEAT_INDEX": str(snapshot.get("repeat_index", 0)),
        "HAIRGPU_SCENARIO_TIMELINE_SHA256": str(snapshot.get("scenarioTimelineSha256", "")),
        "HAIRGPU_ASSET_MANIFEST_SHA256": str(snapshot.get("assetManifestSha256", "")),
        "HAIRGPU_ASSET_SHA256": str(snapshot.get("assetSha256", "")),
        "HAIRGPU_SHADER_BUNDLE_SHA256": str(snapshot.get("shaderBundleSha256", "")),
        "HAIRGPU_ASSET_FROZEN": "1" if snapshot.get("assetFrozen") else "0",
        "HAIRGPU_REQUESTED_METHOD_SETTINGS_SHA256": str(snapshot.get("requestedMethodSettingsSha256", "")),
    })
    direct_flags = {
        "enableNoNoveltyBaseline": "HAIRGPU_ENABLE_NO_NOVELTY_BASELINE",
        "enableMicrotype": "HAIRGPU_ENABLE_MICROTYPE",
        "enableBudgetedPhysics": "HAIRGPU_ENABLE_BUDGETED_PHYSICS",
        "enableCertificateRender": "HAIRGPU_ENABLE_CERTIFICATE_RENDER",
        "enablePerStrandEdit": "HAIRGPU_ENABLE_PER_STRAND_EDIT",
        "enableCBMA": "HAIRGPU_ENABLE_CBMA",
        "enablePhysicsIslands": "HAIRGPU_ENABLE_PHYSICS_ISLANDS",
        "enableRodRepair": "HAIRGPU_ENABLE_ROD_REPAIR",
        "enableGpuResident": "HAIRGPU_ENABLE_GPU_RESIDENT",
        "enableSegmentGroupSleep": "HAIRGPU_ENABLE_SEGMENT_GROUP_SLEEP",
        "enableSegmentGroupIntegratedState": "HAIRGPU_ENABLE_SEGMENT_GROUP_INTEGRATED_STATE",
        "enableExternalStateArray": "HAIRGPU_ENABLE_EXTERNAL_STATE_ARRAY",
        "enableSharedPointOwnership": "HAIRGPU_ENABLE_SHARED_POINT_OWNERSHIP",
        "enableDirectionalResponsibility": "HAIRGPU_ENABLE_DIRECTIONAL_RESPONSIBILITY",
        "enableStaticCertificate": "HAIRGPU_ENABLE_STATIC_CERTIFICATE",
        "enablePredictiveWake": "HAIRGPU_ENABLE_PREDICTIVE_WAKE",
        "enableVelocityRecovery": "HAIRGPU_ENABLE_VELOCITY_RECOVERY",
        "enableRepair": "HAIRGPU_ENABLE_REPAIR",
        "enableGpuCompaction": "HAIRGPU_ENABLE_GPU_COMPACTION",
        "enableActiveSet": "HAIRGPU_ENABLE_ACTIVE_SET",
        "enableStrandSleep": "HAIRGPU_ENABLE_STRAND_SLEEP",
        "enableBlockSleep": "HAIRGPU_ENABLE_BLOCK_SLEEP",
        "enableSleepHysteresis": "HAIRGPU_ENABLE_SLEEP_HYSTERESIS",
        "enableStaticCache": "HAIRGPU_ENABLE_STATIC_CACHE",
    }
    for key, value in dict(snapshot.get("method_settings", {})).items():
        env[f"HAIRGPU_SETTING_{_env_token(key)}"] = _env_value(value)
        if key in direct_flags:
            env[direct_flags[key]] = _env_value(value)
    _set_if_present(env, "HAIRGPU_SCENARIO_TIMELINE", snapshot.get("scenario_timeline"))
    _set_if_present(env, "HAIRGPU_ASSET_MANIFEST", snapshot.get("asset_manifest"))
    _set_if_present(env, "HAIRGPU_RENDER_WIDTH", snapshot.get("width"))
    _set_if_present(env, "HAIRGPU_RENDER_HEIGHT", snapshot.get("height"))
    _set_if_present(env, "HAIRGPU_VSYNC", snapshot.get("vsync"))
    _set_if_present(env, "HAIRGPU_PRESENT_SYNC_INTERVAL", snapshot.get("present_sync_interval", 0))
    _set_if_present(env, "HAIRGPU_FIXED_TIMESTEP_ENABLED", snapshot.get("fixed_timestep"))
    _set_if_present(env, "HAIRGPU_FIXED_TIMESTEP_SECONDS", snapshot.get("fixed_timestep_seconds"))
    _set_if_present(env, "HAIRGPU_GUIDE_STRAND_COUNT", snapshot.get("guide_strand_count"))
    _set_if_present(env, "HAIRGPU_GUIDE_COUNT", snapshot.get("guide_strand_count"))
    _set_if_present(env, "HAIRGPU_DISPLAY_STRAND_COUNT", snapshot.get("display_strand_count"))
    _set_if_present(env, "HAIRGPU_PARTICLES_PER_GUIDE", snapshot.get("particles_per_guide"))
    _set_if_present(env, "HAIRGPU_SOLVER_ITERATIONS", snapshot.get("solver_iterations"))
    _set_if_present(env, "HAIRGPU_GPU_TIMESTAMP_DELAY_FRAMES", snapshot.get("gpu_timestamp_delay_frames", 6))

    environment_payload: dict[str, Any] = {"start": collect_hardware_probe(str(snapshot.get("hardware_profile", "")), "start")}
    write_json(paths["environment"], environment_payload)
    sampler = HardwareSampler(str(snapshot.get("hardware_profile", "")), _required_float(snapshot.get("hardware_sample_interval_seconds"), 5.0))
    sampler.start()
    process_return_code: int | None = None
    try:
        completed = subprocess.run([str(viewer_exe)], cwd=viewer_exe.parent, env=env, timeout=max(int(timeout_seconds), 1), check=False)
        process_return_code = int(completed.returncode)
    except subprocess.TimeoutExpired as exc:
        raise FormalRunError(f"Viewer timed out after {timeout_seconds} seconds.", EXIT_RUNTIME_FAILURE) from exc
    finally:
        samples = sampler.stop()
        environment_payload["end"] = collect_hardware_probe(str(snapshot.get("hardware_profile", "")), "end")
        environment_payload["samples"] = samples
        environment_payload["viewerReturnCode"] = process_return_code
        write_json(paths["environment"], environment_payload)
        write_hardware_timeseries(telemetry_dir / f"{run_name}_hardware_timeseries.csv", samples)

    if process_return_code not in (0, None):
        error = FormalRunError(f"Viewer exited with code {process_return_code}.", EXIT_RUNTIME_FAILURE)
        write_json(paths["error"], {"runName": run_name, "exitCode": error.exit_code, "error": str(error), "issues": error.issues})
        raise error
    try:
        validate_required_outputs(paths["summary"], paths["csv"], paths["jsonl"], paths["settings"], run_name, case=snapshot, environment_json=paths["environment"])
    except FormalRunError as exc:
        write_json(paths["error"], {"runName": run_name, "exitCode": exc.exit_code, "error": str(exc), "issues": exc.issues})
        raise
    return paths["summary"], paths["csv"], paths["jsonl"], paths["settings"], paths["validation"]


def _self_test() -> None:
    assert derive_method_code({key: METHOD_PRESETS["P"][key] for key in METHOD_FLAG_KEYS}) == "P"
    assert derive_method_code({key: METHOD_PRESETS["B8"][key] for key in METHOD_FLAG_KEYS}) == "B8"
    case = _normalize_case({"method": "P", "width": 1920, "height": 1080, "display_strand_count": 100000, "guide_strand_count": 3000, "particles_per_guide": 24, "solver_iterations": 6, "vsync": False, "present_sync_interval": 0})
    requested = dict(METHOD_PRESETS["P"])
    applied = {key: requested[key] for key in METHOD_FLAG_KEYS}
    observed = dict(applied)
    observed.update(_expected_runtime_settings(case))
    issues, evidence = _compare_settings({"requested": requested, "applied": applied, "observed": observed}, case)
    assert not issues, issues
    assert evidence["observedMethodCode"] == "P"
    rows = [{
        "frameMilliseconds": 10.0,
        "gpuHairMilliseconds": 3.0,
        "metricAvailabilityMask": AVAILABILITY_BITS["gpuTiming"] | AVAILABILITY_BITS["falseStatic"] | AVAILABILITY_BITS["repair"],
        "staticCachedGroupCount": 10,
        "staticTransformedGroupCount": 0,
        "falseStaticCount": 1,
        "repairAttemptCount": 4,
        "repairSuccessCount": 4,
        "totalStrandCount": 100000,
    }]
    aggregate = _aggregate_frame_metrics(rows, {"repairAttemptCount": 4, "repairSuccessCount": 4, "repairVerifiedCount": 4, "repairRecurrenceCount": 0})
    assert abs(float(aggregate["falseStaticRate"]) - 0.1) < 1.0e-9
    assert aggregate["repairVerifiedSuccessRate"] == 1.0
    assert aggregate["repairRecurrenceRate"] == 0.0
    assert aggregate["evidenceProvenance"]["proxyMetricsUsed"] is False
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "test.json"
        write_json(path, {"ok": True})
        assert _read_json(path) == {"ok": True}
    print("hairgpu_final_run self-test: PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Launch or validate a formal HairSystem runtime case.")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--telemetry-dir")
    parser.add_argument("--run-name")
    parser.add_argument("--case-json")
    parser.add_argument("--timeout-seconds", type=int, default=900)
    args = parser.parse_args()
    if args.self_test:
        _self_test()
        return 0
    if not all((args.viewer_exe, args.telemetry_dir, args.run_name, args.case_json)):
        parser.error("--viewer-exe, --telemetry-dir, --run-name and --case-json are required unless --self-test is used")
    case = _read_json(Path(args.case_json))
    try:
        run_viewer(Path(args.viewer_exe), Path(args.telemetry_dir), args.run_name, args.timeout_seconds, case)
        return EXIT_COMPLETE_VALID
    except FormalRunError as exc:
        print(str(exc), file=sys.stderr)
        return exc.exit_code


if __name__ == "__main__":
    raise SystemExit(main())
