from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import platform
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Iterable

from hairgpu_final_db import FinalStageDatabase, RuntimeRunMetadata
from hairgpu_final_excel import export_workbook

EXIT_COMPLETE_VALID = 0
EXIT_QUALITY_GATE_FAILED = 1
EXIT_FORMAL_CONTRACT_INVALID = 2
EXIT_RUNTIME_FAILURE = 3
EXIT_OUTPUT_INTEGRITY_FAILURE = 4
EXIT_CONFIGURATION_FAILURE = 5

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
for _mask in range(32):
    _name = f"Combo_{_mask:05b}"
    METHOD_ALIASES[_name] = _name
    METHOD_ALIASES[_name.lower()] = _name

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
    value = dict(_BASE_SAFE_FLAGS)
    value.update(overrides)
    return value


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

# Metrics that must exist for a scenario to be accepted. Additional required
# metrics can still be supplied by the case JSON.
REQUIRED_METRICS_BY_SCENARIO: dict[str, set[str]] = {
    "S07_ColliderApproach": {
        "predictiveWakeRecall", "wakeDelayFrameMax", "collisionCandidateCount"
    },
    "S08_StaticDynamicBoundaryContact": {
        "staticDynamicBoundaryMaxErrorMm", "sharedPointConflictCount"
    },
    "S12_FalseStaticTrap": {"falseStaticRate"},
    "S13_RepairStorm": {
        "repairVerifiedSuccessRate", "repairRecurrenceRate"
    },
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
    def __init__(
        self,
        message: str,
        exit_code: int,
        issues: list[dict[str, Any]] | None = None,
    ):
        super().__init__(message)
        self.exit_code = int(exit_code)
        self.issues = list(issues or [])


def normalize_method_code(method_code: str) -> str:
    raw = str(method_code or "").strip()
    if raw in FORMAL_METHOD_CODES:
        return raw
    return METHOD_ALIASES.get(raw, METHOD_ALIASES.get(raw.lower(), raw))


def _issue(
    code: str,
    message: str,
    *,
    expected: Any = None,
    observed: Any = None,
    severity: str = "error",
) -> dict[str, Any]:
    return {
        "severity": severity,
        "code": code,
        "message": message,
        "expected": expected,
        "observed": observed,
    }


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_tree(paths: Iterable[Path], *, base_path: Path | None = None) -> str:
    """Hash files by repository-relative path and bytes.

    Absolute machine paths are deliberately excluded so identical source trees
    produce identical hashes on different machines.
    """
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
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _atomic_write_bytes(path: Path, data: bytes) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(
        path.name + f".tmp.{os.getpid()}.{threading.get_ident()}"
    )
    try:
        with temporary.open("wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    encoded = (
        json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    ).encode("utf-8")
    _atomic_write_bytes(Path(path), encoded)


def _read_json(path: Path) -> dict[str, Any]:
    value = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise FormalRunError(
            f"Expected JSON object: {path}", EXIT_OUTPUT_INTEGRITY_FAILURE
        )
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
                raise FormalRunError(
                    f"Invalid JSONL at {path}:{line_number}: {exc}",
                    EXIT_OUTPUT_INTEGRITY_FAILURE,
                ) from exc
            if not isinstance(value, dict):
                raise FormalRunError(
                    f"JSONL row {line_number} is not an object",
                    EXIT_OUTPUT_INTEGRITY_FAILURE,
                )
            rows.append(value)
    return rows


def _count_csv_rows(path: Path) -> int:
    with Path(path).open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.reader(handle)
        rows = list(reader)
    if not rows:
        return 0
    width = len(rows[0])
    if width == 0:
        return 0
    for index, row in enumerate(rows[1:], 2):
        if len(row) != width:
            raise FormalRunError(
                f"CSV column mismatch at row {index}: expected {width}, observed {len(row)}",
                EXIT_OUTPUT_INTEGRITY_FAILURE,
            )
    return len(rows) - 1


def _metric(row: dict[str, Any], *keys: str, default: Any = None) -> Any:
    for key in keys:
        if key in row and row[key] is not None:
            return row[key]
    return default


def _optional_float(value: Any) -> float | None:
    if value in (None, "", "null") or isinstance(value, bool):
        return None
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        return None
    return parsed if math.isfinite(parsed) else None


def _optional_int(value: Any) -> int | None:
    parsed = _optional_float(value)
    return int(parsed) if parsed is not None else None


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


def _percentile(values: list[float], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * percentile / 100.0
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def _safe_rate(numerator: int | float, denominator: int | float) -> float | None:
    return float(numerator) / float(denominator) if denominator else None


def _resolve_case_path(value: Any, base_dir: Path | None = None) -> Path | None:
    if value in (None, ""):
        return None
    path = Path(str(value))
    if not path.is_absolute() and base_dir is not None:
        path = base_dir / path
    return path.resolve()


def _normalize_case(case: dict[str, Any]) -> dict[str, Any]:
    result = dict(case)
    method = normalize_method_code(result.get("method", result.get("variant", "")))
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
    result.setdefault(
        "settings_hash",
        canonical_json_sha256(
            {
                "method": method,
                "method_settings": requested,
                "scenario": result.get("scenario", ""),
                "seed": result.get("seed", 0),
                "repeat_index": result.get("repeat_index", 0),
                "timeline": result.get("scenario_timeline", ""),
                "asset_manifest": result.get("asset_manifest", ""),
                "runtime": {
                    "width": result["width"],
                    "height": result["height"],
                    "fixed_timestep_seconds": result["fixed_timestep_seconds"],
                    "warmup_frames": result["warmup_frames"],
                    "measurement_frames": result["measurement_frames"],
                    "guide_strand_count": result["guide_strand_count"],
                    "display_strand_count": result["display_strand_count"],
                    "particles_per_guide": result["particles_per_guide"],
                    "solver_iterations": result["solver_iterations"],
                },
            }
        ),
    )
    required = set(result.get("required_quality_metrics", []))
    required.update(REQUIRED_METRICS_BY_SCENARIO.get(str(result.get("scenario", "")), set()))
    result["required_quality_metrics"] = sorted(required)
    return result


def _validate_timeline(path: Path, case: dict[str, Any]) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    try:
        timeline = _read_json(path)
    except Exception as exc:
        return [_issue("E_TIMELINE_PARSE", "Timeline JSON could not be parsed.", observed=str(exc))]
    scenario = str(timeline.get("scenario") or timeline.get("scenarioId") or "")
    expected_scenario = str(case.get("scenario") or "")
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
    measurement_event_count = 0
    for index, event in enumerate(events):
        if not isinstance(event, dict):
            issues.append(_issue("E_TIMELINE_EVENT_TYPE", f"Timeline event {index} is not an object."))
            continue
        frame = _optional_int(event.get("frame"))
        event_type = str(event.get("type") or "")
        if frame is None or frame < 0 or frame >= duration:
            issues.append(_issue("E_TIMELINE_EVENT_FRAME", f"Timeline event {index} has an invalid frame.", observed=frame))
        elif frame < previous:
            issues.append(_issue("E_TIMELINE_ORDER", "Timeline events are not ordered."))
        else:
            previous = frame
            if frame >= _required_int(case.get("warmup_frames")):
                measurement_event_count += 1
        if not event_type:
            issues.append(_issue("E_TIMELINE_EVENT_NAME", f"Timeline event {index} has no type."))
        values = event.get("value", event.get("translation", event.get("rotation")))
        if values is not None:
            values_iter = values if isinstance(values, list) else [values]
            if any(_optional_float(item) is None for item in values_iter):
                issues.append(_issue("E_TIMELINE_NONFINITE", f"Timeline event {index} contains a non-finite value."))
    if measurement_event_count == 0:
        issues.append(_issue("E_TIMELINE_MEASUREMENT_EVENT", "Timeline has no event in the measured interval."))
    return issues


def _validate_asset_manifest(path: Path) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    try:
        manifest = _read_json(path)
    except Exception as exc:
        return [_issue("E_ASSET_MANIFEST_PARSE", "Asset manifest could not be parsed.", observed=str(exc))]
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


def validate_case_contract(case: dict[str, Any], viewer_exe: Path | None = None) -> dict[str, Any]:
    normalized = _normalize_case(case)
    issues: list[dict[str, Any]] = []
    required_identity = ("case_id", "method", "scenario", "seed", "asset_manifest", "scenario_timeline")
    for key in required_identity:
        if normalized.get(key) in (None, ""):
            issues.append(_issue("E_CONFIG_REQUIRED", f"Required formal case field is missing: {key}"))
    method = normalize_method_code(normalized.get("method", ""))
    if method not in FORMAL_METHOD_CODES:
        issues.append(_issue("E_METHOD_UNKNOWN", "Formal method must be B0-B8 or P.", expected=sorted(FORMAL_METHOD_CODES), observed=method))
    if str(normalized.get("build_configuration", "")).lower() != "release":
        issues.append(_issue("E_BUILD_CONFIGURATION", "Formal run requires Release.", expected="Release", observed=normalized.get("build_configuration")))
    if str(normalized.get("platform", "")).lower() != "x64":
        issues.append(_issue("E_PLATFORM", "Formal run requires x64.", expected="x64", observed=normalized.get("platform")))
    if [_required_int(normalized.get("width")), _required_int(normalized.get("height"))] != [1920, 1080]:
        issues.append(_issue("E_RESOLUTION", "Formal run requires 1920x1080.", expected=[1920, 1080], observed=[normalized.get("width"), normalized.get("height")]))
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
    points = _required_int(normalized.get("particles_per_guide"))
    if not 16 <= points <= 24:
        issues.append(_issue("E_POINTS_PER_GUIDE", "Particles per guide must be 16-24.", observed=points))
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
        viewer_exe = Path(viewer_exe)
        if not viewer_exe.is_file():
            issues.append(_issue("E_VIEWER_MISSING", "Viewer executable is missing.", observed=str(viewer_exe)))
        elif _as_bool(normalized.get("strict_release_path", False)) and "release" not in str(viewer_exe).lower():
            issues.append(_issue("E_RELEASE_PATH", "Viewer path does not identify Release output.", observed=str(viewer_exe)))

    if method in {"B8", "P"}:
        flags = normalized["method_settings"]
        expected_integrated = method == "P"
        expected_external = method == "B8"
        if _as_bool(flags.get("enableSegmentGroupIntegratedState")) != expected_integrated:
            issues.append(_issue("E_STORAGE_MODE", "Integrated state flag does not match method."))
        if _as_bool(flags.get("enableExternalStateArray")) != expected_external:
            issues.append(_issue("E_STORAGE_MODE", "External state flag does not match method."))
        common_keys = set(_BASE_SAFE_FLAGS) - {"enableSegmentGroupIntegratedState", "enableExternalStateArray"}
        mismatches = {
            key: flags.get(key)
            for key in common_keys
            if flags.get(key) != METHOD_PRESETS[method].get(key)
        }
        if mismatches:
            issues.append(_issue("E_B8_P_COMMON_FLAGS", "B8/P must differ only in storage representation.", observed=mismatches))

    result = {
        "status": "passed" if not issues else "failed",
        "isValid": not issues,
        "normalizedCase": normalized,
        "issues": issues,
    }
    if issues:
        codes = ", ".join(item["code"] for item in issues[:10])
        raise FormalRunError(
            f"Formal case contract failed: {codes}",
            EXIT_CONFIGURATION_FAILURE,
            issues,
        )
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
        result = subprocess.run(
            [
                "nvidia-smi",
                "--query-gpu=name,driver_version,temperature.gpu,clocks.current.graphics,"
                "clocks.current.memory,power.draw,power.limit,memory.total,memory.used",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        lines = result.stdout.strip().splitlines()
        if lines:
            fields = [item.strip() for item in lines[0].split(",")]
            if len(fields) >= 9:
                payload["gpu"] = {
                    "name": fields[0],
                    "driverVersion": fields[1],
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
        result = subprocess.run(
            ["powercfg", "/GETACTIVESCHEME"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
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
            "gpuName": gpu.get("name"),
            "driverVersion": gpu.get("driverVersion"),
            "temperatureC": gpu.get("temperatureC"),
            "graphicsClockMHz": gpu.get("graphicsClockMHz"),
            "memoryClockMHz": gpu.get("memoryClockMHz"),
            "powerDrawW": gpu.get("powerDrawW"),
            "powerLimitW": gpu.get("powerLimitW"),
            "memoryTotalMiB": gpu.get("memoryTotalMiB"),
            "memoryUsedMiB": gpu.get("memoryUsedMiB"),
            "activePowerScheme": power.get("activeScheme"),
        })
    temporary = Path(path).with_name(Path(path).name + ".tmp")
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    with temporary.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def _availability_coverage(rows: list[dict[str, Any]], bit: int) -> float:
    if not rows:
        return 0.0
    count = 0
    for row in rows:
        mask = _required_int(_metric(row, "metricAvailabilityMask", "availabilityMask", default=0))
        if mask & bit:
            count += 1
    return count / len(rows)


def _available_values(
    rows: list[dict[str, Any]],
    keys: tuple[str, ...],
    availability_bit: int | None = None,
) -> list[float]:
    values: list[float] = []
    for row in rows:
        if availability_bit is not None:
            mask = _required_int(_metric(row, "metricAvailabilityMask", "availabilityMask", default=0))
            if not (mask & availability_bit):
                continue
        value = _optional_float(_metric(row, *keys))
        if value is not None:
            values.append(value)
    return values


def _sum_available(
    rows: list[dict[str, Any]],
    keys: tuple[str, ...],
    availability_bit: int | None = None,
) -> int:
    total = 0
    for row in rows:
        if availability_bit is not None:
            mask = _required_int(_metric(row, "metricAvailabilityMask", "availabilityMask", default=0))
            if not (mask & availability_bit):
                continue
        value = _optional_int(_metric(row, *keys))
        if value is not None:
            total += value
    return total


def _aggregate_frame_metrics(rows: list[dict[str, Any]]) -> dict[str, Any]:
    frame_ms = _available_values(rows, ("frameMilliseconds", "frame_ms"))
    frame_ms = [value for value in frame_ms if value > 0.0]
    gpu_ms = _available_values(
        rows,
        ("totalHairGpuMilliseconds", "gpuHairMilliseconds", "total_hair_gpu_ms"),
        AVAILABILITY_BITS["gpuTiming"],
    )
    static_count = _sum_available(rows, ("staticCachedGroupCount",)) + _sum_available(rows, ("staticTransformedGroupCount",))
    false_static = _sum_available(rows, ("falseStaticCount",), AVAILABILITY_BITS["falseStatic"])
    repair_attempts = _sum_available(rows, ("repairAttemptCount",))
    repair_success = _sum_available(rows, ("repairSuccessCount", "repairExecutionSuccessCount"), AVAILABILITY_BITS["repair"])
    repair_verified = _sum_available(rows, ("repairVerifiedCount",), AVAILABILITY_BITS["repair"])
    repair_recurrence = _sum_available(rows, ("repairRecurrenceCount",), AVAILABILITY_BITS["repair"])
    wake_tp = _sum_available(rows, ("predictiveWakeTruePositiveCount",), AVAILABILITY_BITS["wake"])
    wake_fp = _sum_available(rows, ("predictiveWakeFalsePositiveCount",), AVAILABILITY_BITS["wake"])
    wake_fn = _sum_available(rows, ("predictiveWakeFalseNegativeCount",), AVAILABILITY_BITS["wake"])
    collision_candidates = _sum_available(rows, ("collisionCandidateCount",))
    collision_misses = _sum_available(rows, ("collisionMissCount",), AVAILABILITY_BITS["collision"])
    transitions = _sum_available(rows, ("stateTransitionCount",))
    oscillations = _sum_available(rows, ("stateOscillationCount",))
    boundary_values = _available_values(rows, ("staticDynamicBoundaryMaxErrorMm",), AVAILABILITY_BITS["boundary"])
    wake_delays = _available_values(rows, ("wakeDelayFrameMax",), AVAILABILITY_BITS["wake"])
    shared_conflicts = _sum_available(rows, ("sharedPointConflictCount",), AVAILABILITY_BITS["boundary"])
    display_counts = _available_values(rows, ("totalStrandCount", "displayStrandCount"))
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
        "falseStaticRate": _safe_rate(false_static, static_count) if _availability_coverage(rows, AVAILABILITY_BITS["falseStatic"]) > 0 else None,
        "repairExecutionSuccessRate": _safe_rate(repair_success, repair_attempts) if _availability_coverage(rows, AVAILABILITY_BITS["repair"]) > 0 else None,
        "repairVerifiedSuccessRate": _safe_rate(repair_verified, repair_attempts) if _availability_coverage(rows, AVAILABILITY_BITS["repair"]) > 0 else None,
        "repairRecurrenceRate": _safe_rate(repair_recurrence, repair_attempts) if _availability_coverage(rows, AVAILABILITY_BITS["repair"]) > 0 else None,
        "predictiveWakePrecision": _safe_rate(wake_tp, wake_tp + wake_fp) if _availability_coverage(rows, AVAILABILITY_BITS["wake"]) > 0 else None,
        "predictiveWakeRecall": _safe_rate(wake_tp, wake_tp + wake_fn) if _availability_coverage(rows, AVAILABILITY_BITS["wake"]) > 0 else None,
        "wakeDelayFrameMax": max(wake_delays) if wake_delays else None,
        "collisionMissRate": _safe_rate(collision_misses, collision_candidates) if _availability_coverage(rows, AVAILABILITY_BITS["collision"]) > 0 else None,
        "stateOscillationRate": _safe_rate(oscillations, transitions),
        "staticDynamicBoundaryMaxErrorMm": max(boundary_values) if boundary_values else None,
        "sharedPointConflictCount": shared_conflicts if _availability_coverage(rows, AVAILABILITY_BITS["boundary"]) > 0 else None,
        "displayStrandCount": int(round(max(display_counts))) if display_counts else None,
        "gpuTimingCoverage": _availability_coverage(rows, AVAILABILITY_BITS["gpuTiming"]),
        "availabilityCoverage": {
            name: _availability_coverage(rows, bit)
            for name, bit in AVAILABILITY_BITS.items()
        },
        "counts": {
            "falseStatic": false_static,
            "staticGroups": static_count,
            "repairAttempts": repair_attempts,
            "repairSuccess": repair_success,
            "repairVerified": repair_verified,
            "repairRecurrence": repair_recurrence,
            "wakeTruePositive": wake_tp,
            "wakeFalsePositive": wake_fp,
            "wakeFalseNegative": wake_fn,
            "collisionCandidates": collision_candidates,
            "collisionMisses": collision_misses,
            "stateTransitions": transitions,
            "stateOscillations": oscillations,
        },
    }


def _evaluate_quality_gates(aggregate: dict[str, Any], case: dict[str, Any]) -> list[dict[str, Any]]:
    thresholds = {
        "falseStaticRate": 0.05,
        "repairVerifiedSuccessRate": 0.95,
        "repairRecurrenceRate": 0.05,
        "predictiveWakeRecall": 0.95,
        "collisionMissRate": 0.01,
        "stateOscillationRate": 0.02,
        "staticDynamicBoundaryMaxErrorMm": 1.0,
    }
    thresholds.update(dict(case.get("quality_gates", {})))
    required = set(case.get("required_quality_metrics", []))
    issues: list[dict[str, Any]] = []
    maximum = {
        "falseStaticRate", "repairRecurrenceRate", "collisionMissRate",
        "stateOscillationRate", "staticDynamicBoundaryMaxErrorMm",
    }
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
        if key in maximum and float(observed) > float(threshold):
            issues.append(_issue("E_QUALITY_GATE", f"Quality metric exceeds maximum: {key}", expected=threshold, observed=observed))
        elif key in minimum and float(observed) < float(threshold):
            issues.append(_issue("E_QUALITY_GATE", f"Quality metric is below minimum: {key}", expected=threshold, observed=observed))
    return issues


def _evaluate_environment(environment: dict[str, Any], case: dict[str, Any]) -> tuple[bool, bool, list[dict[str, Any]]]:
    issues: list[dict[str, Any]] = []
    if not environment:
        return False, False, [_issue("E_ENVIRONMENT_MISSING", "Hardware environment evidence is missing.")]
    start = environment.get("start", {}) if isinstance(environment.get("start"), dict) else {}
    end = environment.get("end", {}) if isinstance(environment.get("end"), dict) else {}
    samples = environment.get("samples", []) if isinstance(environment.get("samples"), list) else []
    gpu = start.get("gpu", {}) if isinstance(start.get("gpu"), dict) else {}
    if not gpu:
        gpu = end.get("gpu", {}) if isinstance(end.get("gpu"), dict) else {}
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
    max_temperature = _required_float(case.get("maximum_gpu_temperature_c"), 88.0)
    maximum_drop = _required_float(case.get("maximum_gpu_clock_drop_ratio"), 0.15)
    thermal_valid = True
    if temperatures and max(temperatures) > max_temperature:
        thermal_valid = False
        issues.append(_issue("E_THERMAL_TEMPERATURE", "GPU temperature exceeded the formal limit.", expected=max_temperature, observed=max(temperatures)))
    if len(clocks) >= 3:
        reference = sorted(clocks)[len(clocks) // 2]
        minimum = min(clocks)
        drop = (reference - minimum) / reference if reference > 0 else 0.0
        if drop > maximum_drop:
            thermal_valid = False
            issues.append(_issue("E_THERMAL_CLOCK_DROP", "GPU clock drop exceeded the formal limit.", expected=maximum_drop, observed=drop))
    return environment_valid, thermal_valid, issues


def _canonical_summary(summary: dict[str, Any]) -> dict[str, Any]:
    result = dict(summary)
    for key in (
        "canonicalResultSha256", "validationReasons", "formalAggregateMetrics",
        "resultManifestSha256", "resultManifestPath", "formalExitCode",
        "validationStatus", "status",
    ):
        result.pop(key, None)
    return result


def _write_artifact_manifest(
    directory: Path,
    run_name: str,
    artifact_paths: list[Path],
    canonical_result_sha256: str,
) -> Path:
    entries = []
    for path in artifact_paths:
        path = Path(path)
        if path.is_file():
            entries.append({
                "path": path.name,
                "sizeBytes": path.stat().st_size,
                "sha256": sha256_file(path),
            })
    payload = {
        "manifestVersion": "01-safe-v3.2-existing-source-only",
        "runName": run_name,
        "canonicalResultSha256": canonical_result_sha256,
        "artifacts": sorted(entries, key=lambda item: item["path"]),
    }
    payload["manifestSha256"] = canonical_json_sha256(payload)
    manifest_path = Path(directory) / f"{run_name}_manifest.json"
    write_json(manifest_path, payload)
    return manifest_path


def _compare_settings(
    requested: dict[str, Any],
    applied: dict[str, Any],
    observed: dict[str, Any],
) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    for key, expected in requested.items():
        if key not in applied:
            issues.append(_issue("E_APPLIED_SETTING_MISSING", f"Applied setting is missing: {key}"))
            continue
        if key not in observed:
            issues.append(_issue("E_OBSERVED_SETTING_MISSING", f"Observed setting is missing: {key}"))
            continue
        applied_value = applied[key]
        observed_value = observed[key]
        if isinstance(expected, bool):
            if _as_bool(applied_value) != expected:
                issues.append(_issue("E_APPLIED_SETTING_MISMATCH", f"Applied setting mismatch: {key}", expected=expected, observed=applied_value))
            if _as_bool(observed_value) != expected:
                issues.append(_issue("E_OBSERVED_SETTING_MISMATCH", f"Observed setting mismatch: {key}", expected=expected, observed=observed_value))
        elif isinstance(expected, (int, float)):
            applied_number = _optional_float(applied_value)
            observed_number = _optional_float(observed_value)
            tolerance = 1.0e-6 * max(1.0, abs(float(expected)))
            if applied_number is None or abs(applied_number - float(expected)) > tolerance:
                issues.append(_issue("E_APPLIED_SETTING_MISMATCH", f"Applied setting mismatch: {key}", expected=expected, observed=applied_value))
            if observed_number is None or abs(observed_number - float(expected)) > tolerance:
                issues.append(_issue("E_OBSERVED_SETTING_MISMATCH", f"Observed setting mismatch: {key}", expected=expected, observed=observed_value))
        elif str(applied_value) != str(expected) or str(observed_value) != str(expected):
            issues.append(_issue("E_SETTING_MISMATCH", f"Requested/applied/observed setting mismatch: {key}", expected=expected, observed={"applied": applied_value, "observed": observed_value}))
    return issues


def validate_required_outputs(
    summary_json: Path,
    frame_csv: Path,
    frame_jsonl: Path,
    settings_json: Path,
    run_name: str,
    *,
    case: dict[str, Any] | None = None,
    environment_json: Path | None = None,
) -> dict[str, Any]:
    normalized = _normalize_case(case or {})
    required_paths = [Path(summary_json), Path(frame_csv), Path(frame_jsonl), Path(settings_json)]
    for path in required_paths:
        if not path.is_file() or path.stat().st_size <= 0:
            raise FormalRunError(f"Required output is missing or empty: {path}", EXIT_OUTPUT_INTEGRITY_FAILURE)

    summary = _read_json(summary_json)
    settings = _read_json(settings_json)
    rows = _read_jsonl(frame_jsonl)
    csv_rows = _count_csv_rows(frame_csv)
    issues: list[dict[str, Any]] = []
    integrity_issues: list[dict[str, Any]] = []

    expected_frames = _required_int(
        normalized.get("measurement_frames"),
        _required_int(summary.get("measurement_frames", summary.get("measurementFrames"))),
    )
    if len(rows) != expected_frames:
        integrity_issues.append(_issue("E_JSONL_FRAME_COUNT", "JSONL row count is not exact.", expected=expected_frames, observed=len(rows)))
    if csv_rows != expected_frames:
        integrity_issues.append(_issue("E_CSV_FRAME_COUNT", "CSV row count is not exact.", expected=expected_frames, observed=csv_rows))

    if not _as_bool(summary.get("realMeasurement")):
        issues.append(_issue("E_REAL_MEASUREMENT", "Run is not marked as a real measurement."))
    for key in (
        "fatalRuntimeFailureCount", "syntheticMetricCount", "sameFrameReadbackCount",
        "fullCpuGroupUploadCount", "nanStateCount", "infiniteStateCount",
        "determinismMismatchCount",
    ):
        if _required_int(summary.get(key)) != 0:
            issues.append(_issue("E_RUNTIME_COUNTER", f"Formal run requires {key}=0.", expected=0, observed=summary.get(key)))

    requested = settings.get("requested") if isinstance(settings.get("requested"), dict) else {}
    applied = settings.get("applied") if isinstance(settings.get("applied"), dict) else {}
    observed = settings.get("observed") if isinstance(settings.get("observed"), dict) else {}
    if not requested or not applied or not observed:
        issues.append(_issue("E_SETTINGS_MISSING", "Requested, applied, and observed settings must all exist."))
    else:
        normalized_requested = dict(normalized.get("method_settings", {}))
        normalized_requested.update({
            "renderWidth": _required_int(normalized.get("width")),
            "renderHeight": _required_int(normalized.get("height")),
            "displayStrandCount": _required_int(normalized.get("display_strand_count")),
            "guideCount": _required_int(normalized.get("guide_strand_count")),
            "particlesPerGuide": _required_int(normalized.get("particles_per_guide")),
            "solverIterations": _required_int(normalized.get("solver_iterations")),
            "vsyncEnabled": False,
            "presentSyncInterval": 0,
        })
        # The viewer may split runtime settings between requested and method flags.
        requested_combined = dict(requested)
        for key, value in normalized_requested.items():
            requested_combined.setdefault(key, value)
        issues.extend(_compare_settings(requested_combined, applied, observed))

    expected_method = normalize_method_code(normalized.get("method", ""))
    method_codes = {
        "requested": normalize_method_code(summary.get("requestedMethodCode", "")),
        "applied": normalize_method_code(summary.get("appliedMethodCode", "")),
        "observed": normalize_method_code(summary.get("observedMethodCode", "")),
    }
    for label, value in method_codes.items():
        if not value:
            issues.append(_issue("E_METHOD_IDENTITY_MISSING", f"{label} method code is missing."))
        elif value != expected_method:
            issues.append(_issue("E_METHOD_IDENTITY", f"{label} method code mismatch.", expected=expected_method, observed=value))

    expected_case_id = str(normalized.get("case_id") or "")
    observed_case_id = str(summary.get("caseId") or summary.get("case_id") or "")
    if expected_case_id and observed_case_id != expected_case_id:
        issues.append(_issue("E_CASE_IDENTITY", "Case identity mismatch.", expected=expected_case_id, observed=observed_case_id))

    for key in (
        "settingsSha256", "commitSha", "executableSha256", "shaderBundleSha256",
        "assetSha256", "assetManifestSha256", "scenarioTimelineSha256",
        "methodSettingsSha256",
    ):
        if not str(summary.get(key) or settings.get(key) or ""):
            issues.append(_issue("E_HASH_MISSING", f"Formal identity hash is missing: {key}"))
    expected_settings_hash = str(normalized.get("settings_hash") or "")
    observed_settings_hash = str(summary.get("settingsSha256") or settings.get("settingsSha256") or "")
    if expected_settings_hash and expected_settings_hash.lower() != observed_settings_hash.lower():
        issues.append(_issue("E_SETTINGS_HASH", "Settings hash mismatch.", expected=expected_settings_hash, observed=observed_settings_hash))

    source_indices: list[int] = []
    for index, row in enumerate(rows):
        source = _optional_int(_metric(row, "sourceFrameIndex", "source_frame_index"))
        if source is None:
            integrity_issues.append(_issue("E_SOURCE_FRAME_MISSING", f"Frame {index} lacks sourceFrameIndex."))
        else:
            source_indices.append(source)
        for key in (
            "syntheticMetricCount", "fatalFailure", "sameFrameReadbackCount",
            "fullCpuGroupUploadCount", "nanStateCount", "infiniteStateCount",
            "determinismMismatchCount",
        ):
            if _required_int(_metric(row, key, default=0)) != 0:
                issues.append(_issue("E_FRAME_COUNTER", f"Frame {index} requires {key}=0.", expected=0, observed=_metric(row, key)))
    if source_indices:
        expected_sequence = list(range(source_indices[0], source_indices[0] + len(source_indices)))
        if source_indices != expected_sequence:
            integrity_issues.append(_issue("E_FRAME_SEQUENCE", "Source frame indices are missing, duplicated, or reordered."))

    aggregate = _aggregate_frame_metrics(rows)
    minimum_timing = _required_float(normalized.get("minimum_gpu_timing_coverage"), 0.99)
    if _required_float(aggregate.get("gpuTimingCoverage")) < minimum_timing:
        issues.append(_issue("E_TIMING_COVERAGE", "GPU timing coverage is below threshold.", expected=minimum_timing, observed=aggregate.get("gpuTimingCoverage")))

    minimum_metric_coverage = _required_float(normalized.get("minimum_metric_coverage"), 0.99)
    required_metrics = set(normalized.get("required_quality_metrics", []))
    required_metric_coverage_valid = True
    for metric_name in sorted(required_metrics):
        availability_name = METRIC_AVAILABILITY.get(metric_name)
        if availability_name:
            coverage = aggregate["availabilityCoverage"].get(availability_name, 0.0)
            if coverage < minimum_metric_coverage:
                required_metric_coverage_valid = False
                issues.append(_issue("E_REQUIRED_METRIC_COVERAGE", f"Required metric coverage is too low: {metric_name}", expected=minimum_metric_coverage, observed=coverage))
        if aggregate.get(metric_name) is None:
            required_metric_coverage_valid = False
            issues.append(_issue("E_QUALITY_METRIC_UNAVAILABLE", f"Required quality metric is unavailable: {metric_name}"))

    environment = _read_json(environment_json) if environment_json and Path(environment_json).is_file() else {}
    environment_valid, thermal_valid, environment_issues = _evaluate_environment(environment, normalized)
    issues.extend(environment_issues)
    quality_issues = _evaluate_quality_gates(aggregate, normalized)

    canonical_payload = {
        "summary": _canonical_summary(summary),
        "settings": settings,
        "frames": rows,
        "environment": environment,
    }
    try:
        canonical_result_sha256 = canonical_json_sha256(canonical_payload)
    except (TypeError, ValueError) as exc:
        integrity_issues.append(_issue("E_CANONICAL_JSON", "Canonical result could not be serialized.", observed=str(exc)))
        canonical_result_sha256 = ""

    if integrity_issues:
        status = "complete_invalid"
        exit_code = EXIT_OUTPUT_INTEGRITY_FAILURE
    elif issues:
        status = "complete_invalid"
        exit_code = EXIT_FORMAL_CONTRACT_INVALID
    elif quality_issues:
        status = "quality_gate_failed"
        exit_code = EXIT_QUALITY_GATE_FAILED
    else:
        status = "complete_valid"
        exit_code = EXIT_COMPLETE_VALID

    all_contract_issues = issues + integrity_issues
    summary.update({
        "validationStatus": status,
        "status": status,
        "formalExitCode": exit_code,
        "canonicalResultSha256": canonical_result_sha256,
        "formalAggregateMetrics": aggregate,
        "validationReasons": all_contract_issues + quality_issues,
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
    # Finalize the summary exactly once before hashing artifacts. It is never
    # rewritten after the manifest is created, avoiding the old circular hash.
    write_json(summary_json, summary)

    validation = {
        "validationVersion": "01-safe-v3.2-existing-source-only",
        "runName": run_name,
        "status": status,
        "isValid": exit_code == EXIT_COMPLETE_VALID,
        "formalContractValid": not all_contract_issues,
        "qualityGatePassed": not quality_issues,
        "thermalValid": thermal_valid,
        "environmentValid": environment_valid,
        "requiredMetricCoverageValid": required_metric_coverage_valid,
        "exitCode": exit_code,
        "realMeasurement": _as_bool(summary.get("realMeasurement")),
        "frameCount": len(rows),
        "csvFrameCount": csv_rows,
        "expectedFrameCount": expected_frames,
        "gpuTimingCoverage": aggregate.get("gpuTimingCoverage"),
        "canonicalResultSha256": canonical_result_sha256,
        "aggregateMetrics": aggregate,
        "issues": all_contract_issues,
        "qualityGateIssues": quality_issues,
    }
    validation_path = Path(summary_json).parent / f"{run_name}_validation.json"
    write_json(validation_path, validation)

    artifacts = [Path(summary_json), Path(frame_csv), Path(frame_jsonl), Path(settings_json), validation_path]
    if environment_json and Path(environment_json).is_file():
        artifacts.append(Path(environment_json))
    hardware_csv = Path(summary_json).parent / f"{run_name}_hardware_timeseries.csv"
    if hardware_csv.is_file():
        artifacts.append(hardware_csv)
    manifest_path = _write_artifact_manifest(Path(summary_json).parent, run_name, artifacts, canonical_result_sha256)
    validation["resultManifestPath"] = manifest_path.name
    validation["resultManifestPhysicalSha256"] = sha256_file(manifest_path)
    # Do not rewrite validation here; doing so would invalidate the manifest.

    if exit_code != EXIT_COMPLETE_VALID:
        reasons = all_contract_issues + quality_issues
        codes = ", ".join(item["code"] for item in reasons[:10])
        raise FormalRunError(
            f"Run '{run_name}' failed formal validation: {codes}",
            exit_code,
            reasons,
        )
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
        return subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=repo_root,
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        ).stdout.strip()
    except Exception:
        return ""


def _write_case_snapshot(
    telemetry_dir: Path,
    run_name: str,
    case: dict[str, Any],
    viewer_exe: Path,
) -> Path:
    snapshot = _normalize_case(case)
    repo_root = Path(__file__).resolve().parents[2]
    snapshot["run_name"] = run_name
    snapshot["commitSha"] = str(
        snapshot.get("commitSha")
        or snapshot.get("commit_sha")
        or _git_commit_sha(repo_root)
    )
    snapshot["viewerExecutable"] = str(viewer_exe)
    snapshot["executableSha256"] = sha256_file(viewer_exe)
    snapshot["shaderBundleSha256"] = sha256_tree(
        [repo_root / "shaders", repo_root / "apps" / "dx11_viewer" / "shaders"],
        base_path=repo_root,
    )
    snapshot["requestedMethodSettingsSha256"] = canonical_json_sha256(
        snapshot.get("method_settings", {})
    )

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


def run_viewer(
    viewer_exe: Path,
    telemetry_dir: Path,
    run_name: str,
    timeout_seconds: int,
    case: dict[str, Any] | None = None,
) -> tuple[Path, Path, Path, Path, Path]:
    viewer_exe = Path(viewer_exe).resolve()
    normalized_case = _normalize_case(case or {})
    preflight = validate_case_contract(normalized_case, viewer_exe)

    telemetry_dir = Path(telemetry_dir).resolve()
    telemetry_dir.mkdir(parents=True, exist_ok=True)
    write_json(telemetry_dir / f"{run_name}_preflight.json", preflight)
    case_snapshot_path = _write_case_snapshot(
        telemetry_dir, run_name, normalized_case, viewer_exe
    )
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
    # Set both names for compatibility with the current viewer and corrected viewer.
    _set_if_present(env, "HAIRGPU_GUIDE_STRAND_COUNT", snapshot.get("guide_strand_count"))
    _set_if_present(env, "HAIRGPU_GUIDE_COUNT", snapshot.get("guide_strand_count"))
    _set_if_present(env, "HAIRGPU_DISPLAY_STRAND_COUNT", snapshot.get("display_strand_count"))
    _set_if_present(env, "HAIRGPU_PARTICLES_PER_GUIDE", snapshot.get("particles_per_guide"))
    _set_if_present(env, "HAIRGPU_SOLVER_ITERATIONS", snapshot.get("solver_iterations"))
    _set_if_present(env, "HAIRGPU_GPU_TIMESTAMP_DELAY_FRAMES", snapshot.get("gpu_timestamp_delay_frames", 6))

    environment_json = telemetry_dir / f"{run_name}_environment.json"
    environment_payload: dict[str, Any] = {
        "start": collect_hardware_probe(str(snapshot.get("hardware_profile", "")), "start")
    }
    write_json(environment_json, environment_payload)
    sampler = HardwareSampler(
        str(snapshot.get("hardware_profile", "")),
        _required_float(snapshot.get("hardware_sample_interval_seconds"), 5.0),
    )
    sampler.start()
    process = subprocess.Popen([str(viewer_exe)], env=env)
    try:
        process.wait(timeout=max(int(timeout_seconds), 1))
    except subprocess.TimeoutExpired as exc:
        sampler.stop()
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
        raise FormalRunError(
            f"Viewer timed out for run '{run_name}'", EXIT_RUNTIME_FAILURE
        ) from exc
    samples = sampler.stop()
    environment_payload["samples"] = samples
    environment_payload["end"] = collect_hardware_probe(
        str(snapshot.get("hardware_profile", "")), "end"
    )
    write_json(environment_json, environment_payload)
    write_hardware_timeseries(
        telemetry_dir / f"{run_name}_hardware_timeseries.csv", samples
    )
    if process.returncode not in (0, None):
        raise FormalRunError(
            f"Viewer exited with code {process.returncode} for run '{run_name}'",
            EXIT_RUNTIME_FAILURE,
        )

    summary_json = telemetry_dir / f"{run_name}_summary.json"
    frame_csv = telemetry_dir / f"{run_name}_frames.csv"
    frame_jsonl = telemetry_dir / f"{run_name}_frames.jsonl"
    settings_json = telemetry_dir / f"{run_name}_settings.json"
    validate_required_outputs(
        summary_json,
        frame_csv,
        frame_jsonl,
        settings_json,
        run_name,
        case=snapshot,
        environment_json=environment_json,
    )
    return summary_json, frame_csv, frame_jsonl, settings_json, environment_json


def copy_artifact(path: Path, destination_dir: Path) -> dict[str, Any]:
    destination_dir.mkdir(parents=True, exist_ok=True)
    destination = destination_dir / Path(path).name
    temporary = destination.with_name(destination.name + ".tmp")
    shutil.copy2(path, temporary)
    os.replace(temporary, destination)
    return {
        "source": str(path),
        "copiedTo": str(destination),
        "sizeBytes": destination.stat().st_size,
        "sha256": sha256_file(destination),
    }


def _main_impl(args: argparse.Namespace) -> int:
    config = _read_json(Path(args.config_json)) if args.config_json else {}
    case = config.get("case") if isinstance(config.get("case"), dict) else config
    db_path = Path(args.db or config.get("db", "runtime.db"))
    schema_path = Path(args.schema or config.get("schema", "tools/final_stage/schema.sql"))
    excel_path = Path(args.excel or config.get("excel", db_path.with_suffix(".xlsx")))
    experiment = args.experiment or config.get("experiment")
    method = normalize_method_code(args.method or config.get("method", case.get("method", "P")))
    scenario = args.scenario or config.get("scenario", case.get("scenario", "default"))
    asset = args.asset or config.get("asset", "")
    run_name = args.run_name or config.get("run_name") or case.get("run_name") or case.get("case_id")
    if not experiment or not run_name:
        raise FormalRunError(
            "--experiment and --run-name are required unless provided in --config-json",
            EXIT_CONFIGURATION_FAILURE,
        )
    telemetry_dir = Path(args.telemetry_dir or config.get("telemetry_dir", "runtime_telemetry"))
    telemetry_dir.mkdir(parents=True, exist_ok=True)

    environment_json: Path | None = None
    already_validated = False
    if args.summary_json and args.frame_csv:
        summary_json = Path(args.summary_json)
        frame_csv = Path(args.frame_csv)
        frame_jsonl = Path(args.frame_jsonl) if args.frame_jsonl else None
        settings_json = Path(args.settings_json) if args.settings_json else None
        environment_json = Path(args.environment_json) if args.environment_json else None
    elif config.get("summary_json") and config.get("frame_csv"):
        summary_json = Path(config["summary_json"])
        frame_csv = Path(config["frame_csv"])
        frame_jsonl = Path(config["frame_jsonl"]) if config.get("frame_jsonl") else None
        settings_json = Path(config["settings_json"]) if config.get("settings_json") else None
        environment_json = Path(config["environment_json"]) if config.get("environment_json") else None
    else:
        viewer_value = args.viewer_exe or config.get("viewer_exe") or case.get("viewer_exe")
        if not viewer_value:
            raise FormalRunError("Provide --viewer-exe or existing telemetry files.", EXIT_CONFIGURATION_FAILURE)
        if sys.platform != "win32":
            raise FormalRunError("Viewer launch is Windows-only.", EXIT_FORMAL_CONTRACT_INVALID)
        summary_json, frame_csv, frame_jsonl, settings_json, environment_json = run_viewer(
            Path(viewer_value), telemetry_dir, str(run_name),
            int(args.timeout_seconds or config.get("timeout_seconds", 900)), case,
        )
        already_validated = True

    if frame_jsonl is None or settings_json is None:
        raise FormalRunError("Formal import requires frame JSONL and settings JSON.", EXIT_OUTPUT_INTEGRITY_FAILURE)
    if not already_validated:
        validate_case_contract(case, None)
        validate_required_outputs(
            summary_json, frame_csv, frame_jsonl, settings_json, str(run_name),
            case=case, environment_json=environment_json,
        )

    validation_json = Path(summary_json).parent / f"{run_name}_validation.json"
    manifest_json = Path(summary_json).parent / f"{run_name}_manifest.json"
    db = FinalStageDatabase(db_path)
    db.initialize(schema_path)
    metadata = RuntimeRunMetadata(
        experiment_name=str(experiment),
        method_code=method,
        scenario_code=str(scenario),
        asset_name=str(asset),
        run_name=str(run_name),
        runtime_source=str(args.runtime_source or config.get("runtime_source", "dx11_viewer")),
        operator_name=str(args.operator or config.get("operator", "")),
        notes=str(args.notes or config.get("notes", "")),
    )
    run_id = db.import_runtime_run(
        metadata,
        Path(summary_json),
        Path(frame_csv),
        frame_jsonl_path=Path(frame_jsonl),
        settings_json_path=Path(settings_json),
        environment_json_path=environment_json,
        validation_json_path=validation_json,
        manifest_json_path=manifest_json,
    )
    db.close()
    export_workbook(db_path, excel_path)
    print(f"Imported runtime run {run_id}")
    return EXIT_COMPLETE_VALID


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run/import formal HairSystem 01 telemetry using existing source files only."
    )
    parser.add_argument("--db")
    parser.add_argument("--schema", default="tools/final_stage/schema.sql")
    parser.add_argument("--excel")
    parser.add_argument("--experiment")
    parser.add_argument("--method")
    parser.add_argument("--scenario")
    parser.add_argument("--asset", default="")
    parser.add_argument("--run-name")
    parser.add_argument("--operator", default="")
    parser.add_argument("--notes", default="")
    parser.add_argument("--runtime-source", default="dx11_viewer")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--telemetry-dir")
    parser.add_argument("--summary-json")
    parser.add_argument("--frame-csv")
    parser.add_argument("--frame-jsonl")
    parser.add_argument("--settings-json")
    parser.add_argument("--environment-json")
    parser.add_argument("--timeout-seconds", type=int)
    parser.add_argument("--config-json")
    parser.add_argument("--output-dir")
    parser.add_argument("--artifact", action="append", default=[])
    args = parser.parse_args()
    try:
        return _main_impl(args)
    except FormalRunError as exc:
        run_name = args.run_name or "formal_run"
        telemetry_dir = Path(args.telemetry_dir or "runtime_telemetry")
        try:
            write_json(
                telemetry_dir / f"{run_name}_error.json",
                {
                    "runName": run_name,
                    "status": "failed",
                    "exitCode": exc.exit_code,
                    "errorType": type(exc).__name__,
                    "message": str(exc),
                    "issues": exc.issues,
                },
            )
        except Exception:
            pass
        print(str(exc), file=sys.stderr)
        return exc.exit_code
    except Exception as exc:
        print(f"Unhandled runtime failure: {exc}", file=sys.stderr)
        return EXIT_RUNTIME_FAILURE


if __name__ == "__main__":
    raise SystemExit(main())
