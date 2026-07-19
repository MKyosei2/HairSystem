from __future__ import annotations

"""Strict, viewer-compatible formal-run launcher for Novelty01.

The runner preserves the environment-variable contract already consumed by the
DirectX 11 viewer while adding stable-60 gates, frozen-evidence checks, resume
fingerprints, failure containment and long-run environment sampling.

Exit codes:
  0: evidence valid and stable-60 target achieved
  1: evidence valid but stable-60 target not achieved
  2: formal contract invalid
  3: viewer/runtime failure
  4: output-integrity failure
  5: configuration failure
"""

import argparse
import csv
import ctypes
import hashlib
import json
import math
import os
import platform
import re
import shutil
import signal
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

VALIDATION_VERSION = "01-safe-v3.4-stable-runtime"
FRAME_BUDGET_MS = 1000.0 / 60.0
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
COMMIT_RE = re.compile(r"^[0-9a-fA-F]{40}$")
FORMAL_METHOD_CODES = {"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "P"}
METHOD_ALIASES = {
    "b0_fulldynamic": "B0", "full_dynamic": "B0", "full-physics": "B0", "full_physics": "B0",
    "b1_strandsleep": "B1", "strand_sleep": "B1",
    "b2_blocksleep": "B2", "block_sleep": "B2",
    "b3_segmentthresholdsleep": "B3", "segment_threshold_sleep": "B3", "prediction_only": "B3",
    "b4_segmenthysteresissleep": "B4", "segment_hysteresis_sleep": "B4",
    "b5_segmentcache": "B5", "segment_cache": "B5",
    "b6_segmentgpucompaction": "B6", "segment_gpu_compaction": "B6",
    "b7_activesetonly": "B7", "active_set_only": "B7", "active-set-runtime": "B7",
    "b8_externalstatearray": "B8", "external_state_array": "B8",
    "p_integratedsegmentgroup": "P", "proposed_integrated_segment_group": "P",
    "proposal-hybrid": "P", "proposal_hybrid": "P", "proposed_full_system": "P",
}

_BASE_FLAGS: dict[str, Any] = {
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
METHOD_FLAG_KEYS = tuple(_BASE_FLAGS)


def _preset(**changes: Any) -> dict[str, Any]:
    result = dict(_BASE_FLAGS)
    result.update(changes)
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

DIRECT_FLAG_ENV = {
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

REQUIRED_FRAME_COLUMNS = {
    "caseId", "scenario", "seed", "sourceFrameIndex", "frameMilliseconds",
    "gpuHairMilliseconds", "totalStrandCount", "availabilityMask",
    "metricAvailabilityMask", "syntheticMetricCount", "sameFrameReadbackCount",
    "nanStateCount", "infiniteStateCount", "fatalFailure", "fatalFailureCode",
}

REQUIRED_METRICS_BY_SCENARIO: dict[str, set[str]] = {
    "S07_ColliderApproach": {"predictiveWakeRecall", "wakeDelayFrameMax", "collisionCandidateCount"},
    "S08_StaticDynamicBoundaryContact": {"staticDynamicBoundaryMaxErrorMm", "sharedPointConflictCount"},
    "S12_FalseStaticTrap": {"falseStaticRate"},
    "S13_RepairStorm": {"repairVerifiedSuccessRate", "repairRecurrenceRate"},
    "S16_StateThrashing": {"stateOscillationRate"},
}


class FormalRunError(RuntimeError):
    def __init__(self, message: str, exit_code: int,
                 issues: list[dict[str, Any]] | None = None):
        super().__init__(message)
        self.exit_code = int(exit_code)
        self.issues = list(issues or [])


def issue(code: str, message: str, *, expected: Any = None,
          observed: Any = None, severity: str = "error") -> dict[str, Any]:
    return {
        "severity": severity,
        "code": code,
        "message": message,
        "expected": expected,
        "observed": observed,
    }


def normalize_method_code(value: str) -> str:
    raw = str(value or "").strip()
    if raw in FORMAL_METHOD_CODES:
        return raw
    return METHOD_ALIASES.get(raw.lower(), raw)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_tree(paths: Iterable[Path], *, base_path: Path | None = None) -> str:
    roots = [Path(item).resolve() for item in paths if item is not None]
    files: list[Path] = []
    for root in roots:
        if root.is_file():
            files.append(root)
        elif root.exists():
            files.extend(item for item in root.rglob("*") if item.is_file())
    if not files:
        return hashlib.sha256(b"").hexdigest()
    if base_path is None:
        base_path = Path(os.path.commonpath([str(item.parent) for item in files]))
    base_path = Path(base_path).resolve()
    digest = hashlib.sha256()
    for path in sorted(files, key=lambda item: item.as_posix().lower()):
        try:
            relative = path.resolve().relative_to(base_path).as_posix()
        except ValueError:
            relative = path.name
        digest.update(relative.encode("utf-8"))
        digest.update(b"\0")
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
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


def atomic_write_bytes(path: Path, data: bytes) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_json(path: Path, payload: Any) -> None:
    text = json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    atomic_write_bytes(Path(path), text.encode("utf-8"))


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    with Path(path).open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with Path(path).open("r", encoding="utf-8-sig") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError(f"JSONL row {line_number} is not an object: {path}")
            rows.append(value)
    return rows


def _to_float(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def _to_int(value: Any, default: int = 0) -> int:
    try:
        return int(float(value))
    except (TypeError, ValueError, OverflowError):
        return default


def _as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def _settings_match(expected: Any, observed: Any) -> bool:
    if isinstance(expected, bool):
        return _as_bool(observed) == expected
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        actual = _to_float(observed)
        tolerance = 1.0e-6 * max(1.0, abs(float(expected)))
        return actual is not None and abs(actual - float(expected)) <= tolerance
    return str(observed) == str(expected)


def percentile(values: list[float], p: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * p / 100.0
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    fraction = position - lower
    if lower == upper:
        return ordered[lower]
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def resolve_path(value: Any, base: Path | None = None) -> Path | None:
    if value in (None, ""):
        return None
    path = Path(str(value))
    if not path.is_absolute() and base is not None:
        path = base / path
    return path.resolve()


def git_value(repo_root: Path, *args: str) -> str:
    try:
        run = subprocess.run(
            ["git", *args],
            cwd=repo_root,
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return run.stdout.strip() if run.returncode == 0 else ""


def _env_token(name: str) -> str:
    step1 = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name)
    return re.sub(r"[^A-Za-z0-9]+", "_", step1).strip("_").upper()


def _env_value(value: Any) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    if isinstance(value, (dict, list)):
        return json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    return str(value)


def _set_if_present(environment: dict[str, str], key: str, value: Any) -> None:
    if value not in (None, ""):
        environment[key] = _env_value(value)


def normalize_case(case: Mapping[str, Any]) -> dict[str, Any]:
    result = dict(case)
    method = normalize_method_code(str(result.get("method", result.get("variant", ""))))
    result["method"] = method
    result.setdefault("variant", method)
    result.setdefault("run_name", result.get("case_id", "formal_case"))
    result.setdefault("case_id", result["run_name"])
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
    result.setdefault("guide_strand_count", 4000)
    result.setdefault("followers_per_guide", 24)
    result.setdefault("display_strand_count", 100000)
    result.setdefault("particles_per_guide", 128)
    result.setdefault("solver_iterations", 6)
    result.setdefault("gpu_timestamp_delay_frames", 6)
    result.setdefault("minimum_average_fps", 60.0)
    result.setdefault("minimum_sixty_fps_ratio", 0.95)
    result.setdefault("maximum_frame_budget_overrun_rate", 0.05)
    result.setdefault("maximum_p95_frame_ms", FRAME_BUDGET_MS)
    result.setdefault("maximum_p99_frame_ms", 20.0)
    result.setdefault("maximum_single_frame_ms", 50.0)
    result.setdefault("maximum_frames_over_100ms", 0)
    result.setdefault("minimum_gpu_timing_coverage", 0.99)
    result.setdefault("timeout_seconds", 900)
    result.setdefault("hardware_sample_interval_seconds", 1.0)
    flags = dict(METHOD_PRESETS.get(method, {}))
    flags.update(dict(result.get("method_settings", {})))
    result["method_settings"] = flags
    if not result.get("settings_hash"):
        timeline_path = resolve_path(result.get("scenario_timeline"))
        asset_manifest_path = resolve_path(result.get("asset_manifest"))
        timeline_identity = (
            sha256_file(timeline_path)
            if timeline_path is not None and timeline_path.is_file()
            else str(result.get("scenario_timeline", ""))
        )
        asset_manifest_identity = (
            sha256_file(asset_manifest_path)
            if asset_manifest_path is not None and asset_manifest_path.is_file()
            else str(result.get("asset_manifest", ""))
        )
        result["settings_hash"] = canonical_json_sha256({
            "method": method,
            "method_settings": flags,
            "scenario": result.get("scenario"),
            "seed": result.get("seed"),
            "repetition_index": result.get("repetition_index", 0),
            "timelineSha256": timeline_identity,
            "assetManifestSha256": asset_manifest_identity,
            "runtime": {
                key: result[key] for key in (
                    "width", "height", "fixed_timestep_seconds", "warmup_frames",
                    "measurement_frames", "guide_strand_count", "followers_per_guide",
                    "display_strand_count", "particles_per_guide", "solver_iterations",
                )
            },
        })
    return result


def materialize_repeated_timeline(case: Mapping[str, Any], output_dir: Path,
                                  repo_root: Path) -> dict[str, Any]:
    result = dict(case)
    if not _as_bool(result.get("timeline_repeat")):
        return result
    source = resolve_path(result.get("scenario_timeline"), repo_root)
    if source is None or not source.is_file():
        raise FormalRunError(
            "Repeated timeline source is missing.",
            EXIT_FORMAL_CONTRACT_INVALID,
            [issue("E_TIMELINE_REPEAT_SOURCE", "Repeated timeline source is missing.", observed=str(source))],
        )
    payload = read_json(source)
    base_duration = _to_int(payload.get("durationFrames"), -1)
    if base_duration <= 0:
        raise FormalRunError("Repeated timeline duration is invalid.", EXIT_FORMAL_CONTRACT_INVALID)
    source_events = payload.get("events")
    if not isinstance(source_events, list):
        raise FormalRunError("Repeated timeline events are invalid.", EXIT_FORMAL_CONTRACT_INVALID)
    required_duration = _to_int(result.get("warmup_frames")) + _to_int(result.get("measurement_frames"))
    expanded: list[dict[str, Any]] = []
    for cycle_index, offset in enumerate(range(0, required_duration, base_duration)):
        for event_index, raw in enumerate(source_events):
            if not isinstance(raw, dict):
                continue
            frame = _to_int(raw.get("frame"), -1)
            if frame < 0:
                continue
            expanded_frame = offset + frame
            if expanded_frame >= required_duration:
                continue
            event = dict(raw)
            event["frame"] = expanded_frame
            base_id = str(event.get("id") or f"{event.get('type', 'event')}_{event_index:03d}")
            event["id"] = f"{base_id}_cycle_{cycle_index:05d}"
            expanded.append(event)
    payload["events"] = expanded
    payload["durationFrames"] = required_duration
    payload["materializedFrom"] = str(source)
    payload["timelineRepeat"] = True
    assertions = payload.get("assertions") if isinstance(payload.get("assertions"), dict) else {}
    assertions = dict(assertions)
    assertions["exactAppliedEventCount"] = len(expanded)
    assertions["timelineFinalFrameRequired"] = True
    payload["assertions"] = assertions
    output = Path(output_dir) / f"{result['run_name']}_timeline_materialized.json"
    write_json(output, payload)
    result["scenario_timeline"] = str(output)
    result["timeline_materialized_from"] = str(source)
    result["settings_hash"] = ""
    return normalize_case(result)


def validate_timeline(path: Path, case: Mapping[str, Any]) -> list[dict[str, Any]]:
    timeline = read_json(path)
    problems: list[dict[str, Any]] = []
    expected_scenario = str(case.get("scenario", ""))
    if str(timeline.get("scenario", "")) != expected_scenario:
        problems.append(issue(
            "E_TIMELINE_SCENARIO",
            "Timeline scenario mismatch.",
            expected=expected_scenario,
            observed=timeline.get("scenario"),
        ))
    required_duration = _to_int(case.get("warmup_frames")) + _to_int(case.get("measurement_frames"))
    duration = _to_int(timeline.get("durationFrames"), -1)
    if duration < required_duration:
        problems.append(issue(
            "E_TIMELINE_DURATION",
            "Timeline is shorter than the formal run.",
            expected=required_duration,
            observed=duration,
        ))
    observed_dt = _to_float(timeline.get("fixedTimeStepSeconds"))
    expected_dt = _to_float(case.get("fixed_timestep_seconds"))
    if observed_dt is None or expected_dt is None or abs(observed_dt - expected_dt) > 1.0e-8:
        problems.append(issue(
            "E_TIMELINE_TIMESTEP",
            "Timeline fixed timestep mismatch.",
            expected=expected_dt,
            observed=observed_dt,
        ))
    events = timeline.get("events")
    if not isinstance(events, list):
        return problems + [issue("E_TIMELINE_EVENTS", "Timeline events must be an array.")]
    previous = -1
    event_ids: set[str] = set()
    for index, event in enumerate(events):
        if not isinstance(event, dict):
            problems.append(issue("E_TIMELINE_EVENT_TYPE", f"Event {index} is not an object."))
            continue
        frame = _to_int(event.get("frame"), -1)
        if frame < 0 or frame >= duration or frame < previous:
            problems.append(issue(
                "E_TIMELINE_EVENT_FRAME",
                f"Invalid event frame at index {index}.",
                observed=frame,
            ))
        previous = max(previous, frame)
        if not str(event.get("type", "")):
            problems.append(issue("E_TIMELINE_EVENT_TYPE_NAME", f"Event {index} has no type."))
        event_id = str(event.get("id", ""))
        if not event_id:
            problems.append(issue("E_TIMELINE_EVENT_ID", f"Event {index} has no stable id."))
        elif event_id in event_ids:
            problems.append(issue("E_TIMELINE_EVENT_ID_DUPLICATE", "Timeline event id is duplicated.", observed=event_id))
        else:
            event_ids.add(event_id)
    assertions = timeline.get("assertions")
    if not isinstance(assertions, dict):
        problems.append(issue("E_TIMELINE_ASSERTIONS", "Timeline assertions must be an object."))
    else:
        if "exactAppliedEventCount" not in assertions:
            problems.append(issue("E_TIMELINE_EVENT_ASSERTION", "exactAppliedEventCount is required."))
        elif _to_int(assertions.get("exactAppliedEventCount"), -1) != len(events):
            problems.append(issue(
                "E_TIMELINE_EVENT_ASSERTION",
                "exactAppliedEventCount must equal the number of materialized events.",
                expected=len(events),
                observed=assertions.get("exactAppliedEventCount"),
            ))
        if assertions.get("timelineFinalFrameRequired") is not True:
            problems.append(issue("E_TIMELINE_FINAL_FRAME_ASSERTION", "timelineFinalFrameRequired must be true."))
    return problems


def validate_asset_manifest(path: Path, repo_root: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    manifest = read_json(path)
    problems: list[dict[str, Any]] = []
    if str(manifest.get("manifestVersion", "")) != "novelty01-100k-v1.4":
        problems.append(issue(
            "E_ASSET_MANIFEST_VERSION",
            "Frozen asset manifest version is incompatible.",
            expected="novelty01-100k-v1.4",
            observed=manifest.get("manifestVersion"),
        ))
    if manifest.get("freezeStatus") != "FROZEN":
        problems.append(issue(
            "E_ASSET_NOT_FROZEN",
            "Formal asset manifest must be FROZEN.",
            expected="FROZEN",
            observed=manifest.get("freezeStatus"),
        ))
    generation = manifest.get("generation") if isinstance(manifest.get("generation"), dict) else {}
    expected = {
        "displayStrandCount": 100000,
        "primaryGuideStrandCount": 4000,
        "followersPerGuide": 24,
        "particlesPerGuide": 128,
        "solverIterations": 6,
    }
    for key, expected_value in expected.items():
        if _to_int(generation.get(key), -1) != expected_value:
            problems.append(issue(
                "E_ASSET_GENERATION_CONTRACT",
                f"Frozen asset generation value is invalid: {key}",
                expected=expected_value,
                observed=generation.get(key),
            ))
    asset_path = resolve_path(manifest.get("sourceAssetPath"), path.parent)
    asset_hash = str(manifest.get("sourceAssetSha256", ""))
    if asset_path is None or not asset_path.is_file():
        problems.append(issue("E_ASSET_FILE_MISSING", "Frozen asset file is missing.", observed=str(asset_path)))
    elif not SHA256_RE.fullmatch(asset_hash):
        problems.append(issue("E_ASSET_HASH_FORMAT", "sourceAssetSha256 is invalid.", observed=asset_hash))
    else:
        actual_hash = sha256_file(asset_path)
        if actual_hash.lower() != asset_hash.lower():
            problems.append(issue(
                "E_ASSET_FILE_HASH",
                "Frozen asset SHA-256 mismatch.",
                expected=asset_hash,
                observed=actual_hash,
            ))
        expected_size = _to_int(manifest.get("sourceAssetSizeBytes"), -1)
        if expected_size < 1 or asset_path.stat().st_size != expected_size:
            problems.append(issue(
                "E_ASSET_FILE_SIZE",
                "Frozen asset size mismatch.",
                expected=expected_size,
                observed=asset_path.stat().st_size,
            ))
    topology_hash = str(manifest.get("generatedTopologySha256", ""))
    if not SHA256_RE.fullmatch(topology_hash):
        problems.append(issue("E_TOPOLOGY_HASH_FORMAT", "Topology SHA-256 is invalid.", observed=topology_hash))
    topology_path = resolve_path(manifest.get("generatedTopologyPath"), path.parent)
    if topology_path is not None:
        if not topology_path.is_file():
            problems.append(issue("E_TOPOLOGY_FILE_MISSING", "Topology file is missing.", observed=str(topology_path)))
        elif SHA256_RE.fullmatch(topology_hash) and sha256_file(topology_path).lower() != topology_hash.lower():
            problems.append(issue("E_TOPOLOGY_HASH", "Topology SHA-256 mismatch."))
    settings_path = resolve_path(manifest.get("generatorSettingsPath"), path.parent)
    settings_hash = str(manifest.get("generatorSettingsSha256", ""))
    if settings_path is None or not settings_path.is_file():
        problems.append(issue("E_GENERATOR_SETTINGS_MISSING", "Generator settings are missing.", observed=str(settings_path)))
    elif not SHA256_RE.fullmatch(settings_hash):
        problems.append(issue("E_GENERATOR_SETTINGS_HASH_FORMAT", "Generator settings SHA-256 is invalid."))
    elif sha256_file(settings_path).lower() != settings_hash.lower():
        problems.append(issue("E_GENERATOR_SETTINGS_HASH", "Generator settings SHA-256 mismatch."))
    commit = str(manifest.get("sourceCommitSha", ""))
    current = git_value(repo_root, "rev-parse", "HEAD")
    if not COMMIT_RE.fullmatch(commit):
        problems.append(issue("E_ASSET_SOURCE_COMMIT_FORMAT", "sourceCommitSha is invalid.", observed=commit))
    elif current and current.lower() != commit.lower():
        problems.append(issue(
            "E_ASSET_SOURCE_COMMIT_MISMATCH",
            "Frozen asset commit differs from current HEAD.",
            expected=commit,
            observed=current,
        ))
    if manifest.get("sourceBranch") != "Novelty":
        problems.append(issue(
            "E_ASSET_SOURCE_BRANCH",
            "Frozen asset must come from Novelty.",
            expected="Novelty",
            observed=manifest.get("sourceBranch"),
        ))
    manifest["_resolvedAssetPath"] = str(asset_path) if asset_path else ""
    return manifest, problems


def _powershell_value(script: str) -> str:
    try:
        completed = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return completed.stdout.strip() if completed.returncode == 0 else ""


_PREFLIGHT_PROBE_CACHE: dict[str, dict[str, Any]] = {}


def collect_hardware_probe(profile_name: str = "", phase: str = "") -> dict[str, Any]:
    if phase == "preflight" and profile_name in _PREFLIGHT_PROBE_CACHE:
        return dict(_PREFLIGHT_PROBE_CACHE[profile_name])
    result: dict[str, Any] = {
        "profile": profile_name,
        "phase": phase,
        "timestampUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "sampleTimeUnix": time.time(),
        "platform": platform.platform(),
        "osBuild": platform.version(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "pythonVersion": platform.python_version(),
        "pythonExecutable": sys.executable,
        "logicalCpuCount": os.cpu_count(),
        "gpu": {},
        "power": {},
        "systemMemory": {},
        "capture": {},
    }
    if sys.platform != "win32":
        return result
    try:
        command = [
            "nvidia-smi",
            "--query-gpu=name,driver_version,temperature.gpu,clocks.current.graphics,clocks.current.memory,power.draw,power.limit,memory.total,memory.used",
            "--format=csv,noheader,nounits",
        ]
        completed = subprocess.run(command, capture_output=True, text=True, timeout=10, check=False)
        fields = [item.strip() for item in completed.stdout.splitlines()[0].split(",")] if completed.stdout.strip() else []
        if len(fields) >= 9:
            result["gpu"] = {
                "name": fields[0],
                "driverVersion": fields[1],
                "temperatureC": _to_float(fields[2]),
                "graphicsClockMHz": _to_float(fields[3]),
                "memoryClockMHz": _to_float(fields[4]),
                "powerDrawW": _to_float(fields[5]),
                "powerLimitW": _to_float(fields[6]),
                "memoryTotalMiB": _to_float(fields[7]),
                "memoryUsedMiB": _to_float(fields[8]),
            }
    except Exception as exc:  # hardware tooling differs by machine
        result["gpuProbeError"] = str(exc)
    try:
        completed = subprocess.run(
            ["powercfg", "/GETACTIVESCHEME"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        result["power"]["activeScheme"] = completed.stdout.strip()
    except Exception as exc:
        result["powerProbeError"] = str(exc)
    try:
        class PowerStatus(ctypes.Structure):
            _fields_ = [
                ("ACLineStatus", ctypes.c_ubyte),
                ("BatteryFlag", ctypes.c_ubyte),
                ("BatteryLifePercent", ctypes.c_ubyte),
                ("SystemStatusFlag", ctypes.c_ubyte),
                ("BatteryLifeTime", ctypes.c_ulong),
                ("BatteryFullLifeTime", ctypes.c_ulong),
            ]
        status = PowerStatus()
        if ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status)):
            result["power"]["acLineStatus"] = int(status.ACLineStatus)
            result["power"]["onAcPower"] = int(status.ACLineStatus) == 1
    except Exception as exc:
        result["acProbeError"] = str(exc)
    try:
        class MemoryStatusEx(ctypes.Structure):
            _fields_ = [
                ("dwLength", ctypes.c_ulong),
                ("dwMemoryLoad", ctypes.c_ulong),
                ("ullTotalPhys", ctypes.c_ulonglong),
                ("ullAvailPhys", ctypes.c_ulonglong),
                ("ullTotalPageFile", ctypes.c_ulonglong),
                ("ullAvailPageFile", ctypes.c_ulonglong),
                ("ullTotalVirtual", ctypes.c_ulonglong),
                ("ullAvailVirtual", ctypes.c_ulonglong),
                ("ullAvailExtendedVirtual", ctypes.c_ulonglong),
            ]
        memory = MemoryStatusEx()
        memory.dwLength = ctypes.sizeof(MemoryStatusEx)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
            result["systemMemory"] = {
                "totalMiB": memory.ullTotalPhys / (1024.0 * 1024.0),
                "usedMiB": (memory.ullTotalPhys - memory.ullAvailPhys) / (1024.0 * 1024.0),
                "loadPercent": int(memory.dwMemoryLoad),
            }
    except Exception as exc:
        result["memoryProbeError"] = str(exc)
    cores = _powershell_value("(Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfCores -Sum).Sum")
    if cores:
        result["physicalCpuCoreCount"] = _to_int(cores, 0)
    try:
        import winreg
        values: list[int] = []
        for key_path, value_name in (
            (r"System\GameConfigStore", "GameDVR_Enabled"),
            (r"Software\Microsoft\Windows\CurrentVersion\GameDVR", "AppCaptureEnabled"),
        ):
            try:
                with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path) as key:
                    values.append(int(winreg.QueryValueEx(key, value_name)[0]))
            except OSError:
                pass
        result["capture"]["gameDvrValues"] = values
        result["capture"]["backgroundCaptureDisabled"] = bool(values) and all(value == 0 for value in values)
    except Exception as exc:
        result["captureProbeError"] = str(exc)
    if phase == "preflight":
        _PREFLIGHT_PROBE_CACHE[profile_name] = dict(result)
    return result


class HardwareSampler:
    def __init__(self, profile_name: str, interval_seconds: float = 1.0):
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
            self._thread.join(timeout=self.interval_seconds + 10.0)
        return list(self.samples)

    def _run(self) -> None:
        while not self._stop.is_set():
            self.samples.append(collect_hardware_probe(self.profile_name, "sample"))
            self._stop.wait(self.interval_seconds)


def write_hardware_timeseries(path: Path, samples: list[dict[str, Any]]) -> None:
    headers = [
        "sampleTimeUnix", "gpuName", "driverVersion", "temperatureC",
        "graphicsClockMHz", "memoryClockMHz", "powerDrawW", "powerLimitW",
        "memoryTotalMiB", "memoryUsedMiB", "systemMemoryTotalMiB",
        "systemMemoryUsedMiB", "systemMemoryLoadPercent", "activePowerScheme",
    ]
    rows: list[dict[str, Any]] = []
    for sample in samples:
        gpu = sample.get("gpu", {}) if isinstance(sample.get("gpu"), dict) else {}
        power = sample.get("power", {}) if isinstance(sample.get("power"), dict) else {}
        memory = sample.get("systemMemory", {}) if isinstance(sample.get("systemMemory"), dict) else {}
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
            "systemMemoryTotalMiB": memory.get("totalMiB"),
            "systemMemoryUsedMiB": memory.get("usedMiB"),
            "systemMemoryLoadPercent": memory.get("loadPercent"),
            "activePowerScheme": power.get("activeScheme"),
        })
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=output.name + ".tmp.", dir=output.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=headers)
            writer.writeheader()
            writer.writerows(rows)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)


def evaluate_hardware_profile(probe: Mapping[str, Any], contract: Mapping[str, Any]) -> list[dict[str, Any]]:
    problems: list[dict[str, Any]] = []
    target = contract.get("target", {}) if isinstance(contract.get("target"), dict) else {}
    execution = contract.get("executionContract", {}) if isinstance(contract.get("executionContract"), dict) else {}
    gpu = probe.get("gpu", {}) if isinstance(probe.get("gpu"), dict) else {}
    power = probe.get("power", {}) if isinstance(probe.get("power"), dict) else {}
    memory = probe.get("systemMemory", {}) if isinstance(probe.get("systemMemory"), dict) else {}
    capture = probe.get("capture", {}) if isinstance(probe.get("capture"), dict) else {}
    gpu_name = str(gpu.get("name", ""))
    gpu_regex = str(target.get("gpuNameRegex", ""))
    if gpu_regex and re.search(gpu_regex, gpu_name, re.IGNORECASE) is None:
        problems.append(issue("E_HARDWARE_GPU_PROFILE", "GPU does not match profile.", expected=gpu_regex, observed=gpu_name))
    vram = _to_float(gpu.get("memoryTotalMiB"))
    minimum_vram = _to_float(target.get("minimumDedicatedVramMiB"))
    maximum_vram = _to_float(target.get("maximumDedicatedVramMiB"))
    if minimum_vram is not None and (vram is None or vram < minimum_vram):
        problems.append(issue("E_HARDWARE_VRAM_MIN", "Dedicated VRAM is below profile minimum.", expected=minimum_vram, observed=vram))
    if maximum_vram is not None and (vram is None or vram > maximum_vram):
        problems.append(issue("E_HARDWARE_VRAM_MAX", "Dedicated VRAM is outside profile maximum.", expected=maximum_vram, observed=vram))
    minimum_cores = _to_int(target.get("minimumCpuPhysicalCoreCount"), 0)
    observed_cores = _to_int(probe.get("physicalCpuCoreCount"), 0)
    if minimum_cores and observed_cores < minimum_cores:
        problems.append(issue("E_HARDWARE_CPU_CORES", "Physical CPU core count is below profile minimum.", expected=minimum_cores, observed=observed_cores))
    maximum_ram = _to_float(target.get("maximumSystemRamMiB"))
    observed_ram = _to_float(memory.get("totalMiB"))
    if maximum_ram is not None and (observed_ram is None or observed_ram > maximum_ram + 256.0):
        problems.append(issue("E_HARDWARE_SYSTEM_RAM", "System RAM is outside the formal profile.", expected=maximum_ram, observed=observed_ram))
    if _as_bool(execution.get("acPowerRequired", False)) and power.get("onAcPower") is not True:
        problems.append(issue("E_HARDWARE_AC_POWER", "Formal run requires AC power.", expected=True, observed=power.get("onAcPower")))
    required_scheme_regex = str(execution.get("requiredPowerSchemeRegex", ""))
    required_scheme_text = str(execution.get("requiredPowerSchemeContains", ""))
    active_scheme = str(power.get("activeScheme", ""))
    scheme_mismatch = False
    expected_scheme: str | None = None
    if required_scheme_regex:
        expected_scheme = required_scheme_regex
        scheme_mismatch = re.search(required_scheme_regex, active_scheme, re.IGNORECASE) is None
    elif required_scheme_text:
        expected_scheme = required_scheme_text
        scheme_mismatch = required_scheme_text.lower() not in active_scheme.lower()
    if scheme_mismatch:
        problems.append(issue("E_HARDWARE_POWER_SCHEME", "Active power scheme does not match profile.", expected=expected_scheme, observed=active_scheme))
    if _as_bool(execution.get("backgroundCaptureDisabled", False)) and capture.get("backgroundCaptureDisabled") is not True:
        problems.append(issue("E_HARDWARE_BACKGROUND_CAPTURE", "Background capture must be disabled.", expected=True, observed=capture))
    return problems


def _linear_slope_per_minute(points: list[tuple[float, float]]) -> float | None:
    if len(points) < 3:
        return None
    origin = points[0][0]
    normalized = [((timestamp - origin) / 60.0, value) for timestamp, value in points]
    mean_x = sum(x for x, _ in normalized) / len(normalized)
    mean_y = sum(y for _, y in normalized) / len(normalized)
    denominator = sum((x - mean_x) ** 2 for x, _ in normalized)
    if denominator <= 0.0:
        return None
    return sum((x - mean_x) * (y - mean_y) for x, y in normalized) / denominator


def environment_metrics(environment: Mapping[str, Any], case: Mapping[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    samples = environment.get("samples", []) if isinstance(environment.get("samples"), list) else []
    vram_points: list[tuple[float, float]] = []
    ram_points: list[tuple[float, float]] = []
    temperatures: list[float] = []
    clocks: list[float] = []
    for sample in samples:
        if not isinstance(sample, dict):
            continue
        timestamp = _to_float(sample.get("sampleTimeUnix"))
        gpu = sample.get("gpu", {}) if isinstance(sample.get("gpu"), dict) else {}
        memory = sample.get("systemMemory", {}) if isinstance(sample.get("systemMemory"), dict) else {}
        vram = _to_float(gpu.get("memoryUsedMiB"))
        ram = _to_float(memory.get("usedMiB"))
        temperature = _to_float(gpu.get("temperatureC"))
        clock = _to_float(gpu.get("graphicsClockMHz"))
        if timestamp is not None and vram is not None:
            vram_points.append((timestamp, vram))
        if timestamp is not None and ram is not None:
            ram_points.append((timestamp, ram))
        if temperature is not None:
            temperatures.append(temperature)
        if clock is not None:
            clocks.append(clock)
    metrics = {
        "hardwareSampleCount": len(samples),
        "peakVramMb": max((value for _, value in vram_points), default=None),
        "peakRamMb": max((value for _, value in ram_points), default=None),
        "vramSlopeMbPerMin": _linear_slope_per_minute(vram_points),
        "ramSlopeMbPerMin": _linear_slope_per_minute(ram_points),
        "maximumGpuTemperatureC": max(temperatures, default=None),
        "gpuTemperatureSpreadC": (max(temperatures) - min(temperatures)) if temperatures else None,
        "gpuClockDropRatio": None,
    }
    if clocks:
        reference = sorted(clocks)[len(clocks) // 2]
        metrics["gpuClockDropRatio"] = (reference - min(clocks)) / reference if reference > 0.0 else 0.0
    problems: list[dict[str, Any]] = []
    contract = case.get("hardware_profile_contract", {}) if isinstance(case.get("hardware_profile_contract"), dict) else {}
    execution = contract.get("executionContract", {}) if isinstance(contract.get("executionContract"), dict) else {}
    minimum_samples = _to_int(execution.get("minimumGpuClockSamples"), 0)
    if minimum_samples and len(clocks) < minimum_samples:
        problems.append(issue("E_THERMAL_SAMPLE_COVERAGE", "Insufficient GPU clock samples.", expected=minimum_samples, observed=len(clocks)))
    maximum_temperature = _to_float(execution.get("maximumGpuTemperatureCelsius"))
    if maximum_temperature is not None and (metrics["maximumGpuTemperatureC"] is None or float(metrics["maximumGpuTemperatureC"]) > maximum_temperature):
        problems.append(issue("E_THERMAL_TEMPERATURE", "GPU temperature exceeded the profile limit.", expected=maximum_temperature, observed=metrics["maximumGpuTemperatureC"]))
    maximum_spread = _to_float(execution.get("maximumGpuTemperatureSpreadCelsius"))
    if maximum_spread is not None and (metrics["gpuTemperatureSpreadC"] is None or float(metrics["gpuTemperatureSpreadC"]) > maximum_spread):
        problems.append(issue("E_THERMAL_SPREAD", "GPU temperature spread exceeded the profile limit.", expected=maximum_spread, observed=metrics["gpuTemperatureSpreadC"]))
    maximum_drop = _to_float(execution.get("maximumGpuClockDropRatio"))
    if maximum_drop is not None and (metrics["gpuClockDropRatio"] is None or float(metrics["gpuClockDropRatio"]) > maximum_drop):
        problems.append(issue("E_THERMAL_CLOCK_DROP", "GPU clock drop exceeded the profile limit.", expected=maximum_drop, observed=metrics["gpuClockDropRatio"]))
    return metrics, problems


def build_execution_fingerprint(case: Mapping[str, Any], viewer_exe: Path,
                                repo_root: Path, manifest: Mapping[str, Any],
                                probe: Mapping[str, Any]) -> tuple[str, dict[str, Any]]:
    timeline = resolve_path(case.get("scenario_timeline"), repo_root)
    shader_root = resolve_path(case.get("shader_root"), repo_root)
    gpu = probe.get("gpu", {}) if isinstance(probe.get("gpu"), dict) else {}
    profile_contract = case.get("hardware_profile_contract", {})
    payload = {
        "commitSha": git_value(repo_root, "rev-parse", "HEAD"),
        "executableSha256": sha256_file(viewer_exe),
        "shaderBundleSha256": sha256_tree([shader_root], base_path=shader_root) if shader_root and shader_root.exists() else "",
        "assetSha256": manifest.get("sourceAssetSha256", ""),
        "topologySha256": manifest.get("generatedTopologySha256", ""),
        "generatorSettingsSha256": manifest.get("generatorSettingsSha256", ""),
        "assetManifestSha256": sha256_file(Path(str(case["asset_manifest"]))),
        "timelineSha256": sha256_file(timeline) if timeline and timeline.is_file() else "",
        "settingsSha256": case.get("settings_hash", ""),
        "hardwareProfile": case.get("hardware_profile", ""),
        "hardwareProfileContractSha256": canonical_json_sha256(profile_contract),
        "gpuName": gpu.get("name", ""),
        "driverVersion": gpu.get("driverVersion", ""),
        "osBuild": probe.get("osBuild", ""),
        "pythonVersion": probe.get("pythonVersion", ""),
    }
    return canonical_json_sha256(payload), payload


def output_paths(output_dir: Path, run_name: str) -> dict[str, Path]:
    base = Path(output_dir)
    return {
        "case": base / f"{run_name}_case.json",
        "frames_csv": base / f"{run_name}_frames.csv",
        "frames_jsonl": base / f"{run_name}_frames.jsonl",
        "summary": base / f"{run_name}_summary.json",
        "settings": base / f"{run_name}_settings.json",
        "environment": base / f"{run_name}_environment.json",
        "hardware": base / f"{run_name}_hardware.json",
        "hardware_timeseries": base / f"{run_name}_hardware_timeseries.csv",
        "validation": base / f"{run_name}_validation.json",
        "manifest": base / f"{run_name}_manifest.json",
        "error": base / f"{run_name}_error.json",
        "stdout": base / f"{run_name}_viewer_stdout.log",
        "stderr": base / f"{run_name}_viewer_stderr.log",
        "running": base / f"{run_name}_RUNNING.json",
        "complete": base / f"{run_name}_COMPLETE.json",
        "lock": base / f"{run_name}.run.lock",
    }


def set_execution_state(active: bool) -> None:
    if sys.platform != "win32":
        return
    flags = (0x80000000 | 0x00000001 | 0x00000002) if active else 0x80000000
    try:
        ctypes.windll.kernel32.SetThreadExecutionState(flags)
    except Exception:
        pass


def run_process(command: list[str], cwd: Path, environment: Mapping[str, str],
                timeout_seconds: int) -> tuple[int, str, str]:
    creation_flags = subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0
    process = subprocess.Popen(
        command,
        cwd=cwd,
        env=dict(environment),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        errors="replace",
        creationflags=creation_flags,
        start_new_session=(sys.platform != "win32"),
    )
    try:
        stdout, stderr = process.communicate(timeout=max(int(timeout_seconds), 1))
    except subprocess.TimeoutExpired as exc:
        if sys.platform == "win32":
            subprocess.run(
                ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                capture_output=True,
                text=True,
                check=False,
            )
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        stdout, stderr = process.communicate(timeout=30)
        raise FormalRunError(
            f"Viewer timed out after {timeout_seconds} seconds.",
            EXIT_RUNTIME_FAILURE,
            [issue("E_VIEWER_TIMEOUT", "Viewer exceeded the formal timeout.", expected=timeout_seconds, observed=timeout_seconds)],
        ) from exc
    return int(process.returncode), (stdout or "")[-1_000_000:], (stderr or "")[-1_000_000:]


def aggregate_frames(rows: list[Mapping[str, Any]], summary: Mapping[str, Any] | None = None) -> dict[str, Any]:
    summary = summary or {}
    frame_ms = [value for row in rows if (value := _to_float(row.get("frameMilliseconds"))) is not None]
    gpu_ms = [value for row in rows if (value := _to_float(row.get("gpuHairMilliseconds"))) is not None and value >= 0.0]
    overrun = sum(value > FRAME_BUDGET_MS for value in frame_ms)
    first_count = max(1, len(frame_ms) // 10) if frame_ms else 0
    first_p99 = percentile(frame_ms[:first_count], 99.0) if first_count else None
    last_p99 = percentile(frame_ms[-first_count:], 99.0) if first_count else None
    p99_degradation = (last_p99 / first_p99) if first_p99 and last_p99 is not None else None

    def max_metric(key: str, default: Any = None) -> Any:
        values = [_to_float(row.get(key)) for row in rows]
        finite = [value for value in values if value is not None]
        return max(finite) if finite else default

    def min_metric(key: str, default: Any = None) -> Any:
        values = [_to_float(row.get(key)) for row in rows]
        finite = [value for value in values if value is not None]
        return min(finite) if finite else default

    def max_int(key: str, default: int = 0) -> int:
        return max((_to_int(row.get(key), default) for row in rows), default=default)

    return {
        "frameCount": len(rows),
        "averageFrameMilliseconds": sum(frame_ms) / len(frame_ms) if frame_ms else None,
        "averageFps": (1000.0 * len(frame_ms) / sum(frame_ms)) if frame_ms and sum(frame_ms) > 0.0 else None,
        "p95FrameMilliseconds": percentile(frame_ms, 95.0),
        "p99FrameMilliseconds": percentile(frame_ms, 99.0),
        "p999FrameMilliseconds": percentile(frame_ms, 99.9),
        "maximumFrameMilliseconds": max(frame_ms) if frame_ms else None,
        "framesOver100Milliseconds": sum(value > 100.0 for value in frame_ms),
        "frameBudgetOverrunRate": overrun / len(frame_ms) if frame_ms else None,
        "sixtyFpsRatio": 1.0 - (overrun / len(frame_ms)) if frame_ms else None,
        "gpuTimingCoverage": len(gpu_ms) / len(rows) if rows else 0.0,
        "maximumGpuHairMilliseconds": max(gpu_ms) if gpu_ms else None,
        "falseStaticRate": max_metric("falseStaticRate"),
        "collisionMissRate": max_metric("collisionMissRate"),
        "repairVerifiedSuccessRate": min_metric("repairVerifiedSuccessRate"),
        "repairRecurrenceRate": max_metric("repairRecurrenceRate"),
        "predictiveWakeRecall": min_metric("predictiveWakeRecall"),
        "wakeDelayFrameMax": max_metric("wakeDelayFrameMax"),
        "sharedPointConflictCount": max_int("sharedPointConflictCount"),
        "stateOscillationRate": max_metric("stateOscillationRate"),
        "staticDynamicBoundaryMaxErrorMm": max_metric("staticDynamicBoundaryMaxErrorMm"),
        "determinismMismatchCount": max_int("determinismMismatchCount"),
        "scenarioAppliedEventCount": max_int("scenarioAppliedEventCount"),
        "collisionCandidateCount": max_int("collisionCandidateCount"),
        "wakeRequestGroupCount": max_int("wakeRequestGroupCount"),
        "repairAttemptCount": max_int("repairAttemptCount"),
        "stateTransitionCount": max_int("stateTransitionCount"),
        "stateOscillationCount": max_int("stateOscillationCount"),
        "stateSequenceHash": max_int("stateSequenceHash"),
        "compactionSequenceHash": max_int("compactionSequenceHash"),
        "finalPositionHash": max_int("finalPositionHash"),
        "p99DegradationRatio": p99_degradation,
        "queryUnresolvedSlope": (
            _to_int(summary.get("gpuTimingUnresolvedFrameCount"), 0)
            / max(len(rows) / 3600.0, 1.0 / 60.0)
        ),
        "availableFrameFields": sorted({key for row in rows for key in row.keys()}),
    }


def validate_scenario_assertions(case: Mapping[str, Any], aggregate: Mapping[str, Any]) -> list[dict[str, Any]]:
    timeline_path = resolve_path(case.get("scenario_timeline"))
    timeline = read_json(timeline_path) if timeline_path else {}
    assertions = timeline.get("assertions", {}) if isinstance(timeline.get("assertions"), dict) else {}
    problems: list[dict[str, Any]] = []
    checks = [
        ("exactAppliedEventCount", "scenarioAppliedEventCount", "equal"),
        ("minimumCollisionCandidates", "collisionCandidateCount", "minimum"),
        ("minimumWakeRequests", "wakeRequestGroupCount", "minimum"),
        ("minimumRepairAttempts", "repairAttemptCount", "minimum"),
        ("minimumStateTransitions", "stateTransitionCount", "minimum"),
        ("maximumStateOscillations", "stateOscillationCount", "maximum"),
    ]
    available_fields = set(aggregate.get("availableFrameFields", []))
    for expected_key, observed_key, mode in checks:
        if expected_key not in assertions:
            continue
        if observed_key not in available_fields:
            problems.append(issue(
                "E_SCENARIO_METRIC_UNAVAILABLE",
                f"Scenario assertion metric is unavailable: {observed_key}",
            ))
            continue
        expected = float(assertions[expected_key])
        observed = float(aggregate.get(observed_key) or 0.0)
        failed = (
            (mode == "equal" and observed != expected)
            or (mode == "minimum" and observed < expected)
            or (mode == "maximum" and observed > expected)
        )
        if failed:
            problems.append(issue(
                "E_SCENARIO_ASSERTION",
                f"Scenario assertion failed: {expected_key}",
                expected=expected,
                observed=observed,
            ))
    return problems


def validate_settings(case: Mapping[str, Any], summary: Mapping[str, Any],
                      settings: Mapping[str, Any]) -> list[dict[str, Any]]:
    problems: list[dict[str, Any]] = []
    expected_case_id = str(case.get("case_id", ""))
    if str(summary.get("caseId") or summary.get("case_id") or "") != expected_case_id:
        problems.append(issue("E_CASE_ID", "Summary case id mismatch.", expected=expected_case_id, observed=summary.get("caseId")))
    if str(summary.get("settingsSha256") or "").lower() != str(case.get("settings_hash") or "").lower():
        problems.append(issue("E_SETTINGS_HASH", "Summary settings hash mismatch.", expected=case.get("settings_hash"), observed=summary.get("settingsSha256")))
    if not _as_bool(summary.get("realMeasurement")):
        problems.append(issue("E_NOT_REAL_MEASUREMENT", "Viewer did not certify a real measurement."))
    requested = settings.get("requested", {}) if isinstance(settings.get("requested"), dict) else {}
    applied = settings.get("applied", {}) if isinstance(settings.get("applied"), dict) else {}
    observed_flags = settings.get("observedFlags", {}) if isinstance(settings.get("observedFlags"), dict) else {}
    expected_flags = case.get("method_settings", {}) if isinstance(case.get("method_settings"), dict) else {}
    for evidence_name, evidence in (("requested", requested), ("applied", applied), ("observed", observed_flags)):
        for key in METHOD_FLAG_KEYS:
            if key not in evidence:
                problems.append(issue(f"E_{evidence_name.upper()}_FLAG_MISSING", f"{evidence_name} flag missing: {key}"))
            elif not _settings_match(expected_flags.get(key), evidence.get(key)):
                problems.append(issue(
                    f"E_{evidence_name.upper()}_SETTING_MISMATCH",
                    f"{evidence_name} flag mismatch: {key}",
                    expected=expected_flags.get(key),
                    observed=evidence.get(key),
                ))
    if not _as_bool(settings.get("methodFlagsMatched")):
        problems.append(issue("E_VIEWER_METHOD_ACK", "Viewer did not acknowledge the complete method flag set."))
    observed = settings.get("observed", {}) if isinstance(settings.get("observed"), dict) else {}
    runtime_expected = {
        "renderWidth": _to_int(case.get("width")),
        "renderHeight": _to_int(case.get("height")),
        "displayStrandCount": _to_int(case.get("display_strand_count")),
        "guideCount": _to_int(case.get("guide_strand_count")),
        "particlesPerGuide": _to_int(case.get("particles_per_guide")),
        "solverIterations": _to_int(case.get("solver_iterations")),
        "vsync": _as_bool(case.get("vsync")),
        "presentSyncInterval": _to_int(case.get("present_sync_interval")),
    }
    for key, expected in runtime_expected.items():
        if key not in observed or not _settings_match(expected, observed.get(key)):
            problems.append(issue("E_OBSERVED_RUNTIME_SETTING", f"Observed runtime setting mismatch: {key}", expected=expected, observed=observed.get(key)))
    expected_method_hash = canonical_json_sha256(dict(expected_flags))
    for key in ("requestedMethodSettingsSha256", "appliedMethodSettingsSha256", "observedMethodSettingsSha256"):
        value = str(settings.get(key) or "")
        if not value or value.lower() != expected_method_hash.lower():
            problems.append(issue("E_METHOD_SETTINGS_HASH", f"Method-settings hash mismatch: {key}", expected=expected_method_hash, observed=value))
    if not _as_bool(settings.get("formalAssetLoaded")):
        problems.append(issue("E_FORMAL_ASSET_NOT_LOADED", "Viewer did not certify loading the frozen formal asset."))
    expected_asset_path = resolve_path(case.get("formalAssetPath"))
    observed_asset_path = resolve_path(settings.get("observedAssetPath"))
    if expected_asset_path and (observed_asset_path is None or os.path.normcase(str(expected_asset_path)) != os.path.normcase(str(observed_asset_path))):
        problems.append(issue("E_FORMAL_ASSET_PATH", "Observed formal asset path mismatch.", expected=str(expected_asset_path), observed=str(observed_asset_path)))
    return problems


def validate_outputs(case: Mapping[str, Any], paths: Mapping[str, Path]) -> dict[str, Any]:
    required = ("frames_csv", "frames_jsonl", "summary", "settings", "environment")
    missing = [str(paths[key]) for key in required if not paths[key].is_file() or paths[key].stat().st_size == 0]
    if missing:
        raise FormalRunError(
            "Required outputs are missing.",
            EXIT_OUTPUT_INTEGRITY_FAILURE,
            [issue("E_OUTPUT_MISSING", "Required outputs are missing.", observed=missing)],
        )
    csv_rows = read_csv_rows(paths["frames_csv"])
    json_rows = read_jsonl(paths["frames_jsonl"])
    summary = read_json(paths["summary"])
    settings = read_json(paths["settings"])
    environment = read_json(paths["environment"])
    problems: list[dict[str, Any]] = []
    expected_frames = _to_int(case.get("measurement_frames"))
    if len(csv_rows) != expected_frames:
        problems.append(issue("E_CSV_FRAME_COUNT", "CSV frame count mismatch.", expected=expected_frames, observed=len(csv_rows)))
    if len(json_rows) != expected_frames:
        problems.append(issue("E_JSONL_FRAME_COUNT", "JSONL frame count mismatch.", expected=expected_frames, observed=len(json_rows)))
    if csv_rows:
        missing_columns = sorted(REQUIRED_FRAME_COLUMNS - set(csv_rows[0]))
        if missing_columns:
            problems.append(issue("E_FRAME_SCHEMA", "Required CSV columns are missing.", observed=missing_columns))
    if json_rows:
        missing_columns = sorted(REQUIRED_FRAME_COLUMNS - set(json_rows[0]))
        if missing_columns:
            problems.append(issue("E_JSONL_SCHEMA", "Required JSONL fields are missing.", observed=missing_columns))
    if len(csv_rows) == len(json_rows):
        for index, (csv_row, json_row) in enumerate(zip(csv_rows, json_rows)):
            for key in ("caseId", "sourceFrameIndex"):
                if str(csv_row.get(key)) != str(json_row.get(key)):
                    problems.append(issue("E_CSV_JSONL_PARITY", f"Frame parity mismatch at row {index}: {key}."))
                    break
            if problems and problems[-1].get("code") == "E_CSV_JSONL_PARITY":
                break
    rows: list[Mapping[str, Any]] = json_rows if json_rows else csv_rows
    source_indices = [_to_int(row.get("sourceFrameIndex"), -1) for row in rows]
    if any(value < 0 for value in source_indices):
        problems.append(issue("E_SOURCE_FRAME_MISSING", "Source frame index is missing."))
    elif any(current <= previous for previous, current in zip(source_indices, source_indices[1:])):
        problems.append(issue("E_SOURCE_FRAME_SEQUENCE", "Source frame indices are not strictly increasing."))
    expected_case_id = str(case.get("case_id", ""))
    for index, row in enumerate(rows):
        if str(row.get("caseId", "")) != expected_case_id:
            problems.append(issue("E_FRAME_CASE_ID", f"Frame {index} case id mismatch.", expected=expected_case_id, observed=row.get("caseId")))
            break
        if _to_int(row.get("fatalFailure")) != 0 or _to_int(row.get("fatalFailureCode")) != 0:
            problems.append(issue("E_FATAL_FRAME", f"Fatal failure reported at frame {index}."))
            break
        if _to_int(row.get("syntheticMetricCount")) != 0:
            problems.append(issue("E_SYNTHETIC_METRIC", f"Synthetic metric reported at frame {index}."))
            break
        if _to_int(row.get("nanStateCount")) or _to_int(row.get("infiniteStateCount")):
            problems.append(issue("E_NONFINITE_STATE", f"NaN/Inf state reported at frame {index}."))
            break
        frame_time = _to_float(row.get("frameMilliseconds"))
        if frame_time is None or frame_time < 0.0:
            problems.append(issue("E_NONFINITE_FRAME_TIME", f"Invalid frame time at frame {index}."))
            break
    problems.extend(validate_settings(case, summary, settings))
    aggregate = aggregate_frames(rows, summary)
    available_fields = set(aggregate.get("availableFrameFields", []))
    required_metrics = (
        REQUIRED_METRICS_BY_SCENARIO.get(str(case.get("scenario", "")), set())
        if normalize_method_code(str(case.get("method", ""))) != "B0"
        else set()
    )
    for required_metric in sorted(required_metrics):
        if required_metric not in available_fields or aggregate.get(required_metric) is None:
            problems.append(issue(
                "E_REQUIRED_SCENARIO_METRIC",
                f"Required scenario metric is unavailable: {required_metric}",
            ))
    problems.extend(validate_scenario_assertions(case, aggregate))
    runtime_metrics, environment_problems = environment_metrics(environment, case)
    aggregate.update(runtime_metrics)
    problems.extend(environment_problems)
    evidence_status = "complete_valid" if not problems else "complete_invalid"
    gates = {
        "averageFps": (aggregate.get("averageFps"), float(case.get("minimum_average_fps")), "minimum"),
        "sixtyFpsRatio": (aggregate.get("sixtyFpsRatio"), float(case.get("minimum_sixty_fps_ratio")), "minimum"),
        "frameBudgetOverrunRate": (aggregate.get("frameBudgetOverrunRate"), float(case.get("maximum_frame_budget_overrun_rate")), "maximum"),
        "p95FrameMilliseconds": (aggregate.get("p95FrameMilliseconds"), float(case.get("maximum_p95_frame_ms")), "maximum"),
        "p99FrameMilliseconds": (aggregate.get("p99FrameMilliseconds"), float(case.get("maximum_p99_frame_ms")), "maximum"),
        "maximumFrameMilliseconds": (aggregate.get("maximumFrameMilliseconds"), float(case.get("maximum_single_frame_ms")), "maximum"),
        "framesOver100Milliseconds": (aggregate.get("framesOver100Milliseconds"), float(case.get("maximum_frames_over_100ms")), "maximum"),
        "gpuTimingCoverage": (aggregate.get("gpuTimingCoverage"), float(case.get("minimum_gpu_timing_coverage")), "minimum"),
    }
    goal_failures: list[dict[str, Any]] = []
    for name, (observed, threshold, direction) in gates.items():
        failed = (
            observed is None
            or (direction == "minimum" and float(observed) < threshold)
            or (direction == "maximum" and float(observed) > threshold)
        )
        if failed:
            goal_failures.append(issue("E_GOAL_GATE", f"Goal gate failed: {name}", expected=threshold, observed=observed))
    stable_status = "achieved" if evidence_status == "complete_valid" and not goal_failures else "not_achieved"
    return {
        "validationVersion": VALIDATION_VERSION,
        "runName": case.get("run_name"),
        "evidenceStatus": evidence_status,
        "stable60FpsStatus": stable_status,
        "hypothesisSupportStatus": "supported" if stable_status == "achieved" else "not_supported",
        "aggregate": aggregate,
        "integrityIssues": problems,
        "goalFailures": goal_failures,
    }


def write_artifact_manifest(paths: Mapping[str, Path], run_name: str,
                            fingerprint: str) -> dict[str, Any]:
    entries: list[dict[str, Any]] = []
    for name, path in paths.items():
        if name in {"manifest", "lock", "running"} or not path.is_file():
            continue
        entries.append({
            "key": name,
            "path": path.name,
            "sizeBytes": path.stat().st_size,
            "sha256": sha256_file(path),
        })
    payload = {
        "manifestVersion": VALIDATION_VERSION,
        "runName": run_name,
        "executionFingerprint": fingerprint,
        "artifacts": sorted(entries, key=lambda item: item["path"]),
    }
    payload["manifestSha256"] = canonical_json_sha256(payload)
    write_json(paths["manifest"], payload)
    return payload


def _case_snapshot(case: Mapping[str, Any], viewer_exe: Path, repo_root: Path,
                   manifest: Mapping[str, Any], fingerprint: str,
                   fingerprint_payload: Mapping[str, Any]) -> dict[str, Any]:
    snapshot = dict(case)
    timeline = resolve_path(snapshot.get("scenario_timeline"), repo_root)
    asset_manifest = resolve_path(snapshot.get("asset_manifest"), repo_root)
    asset_path = resolve_path(manifest.get("_resolvedAssetPath"))
    shader_root = resolve_path(snapshot.get("shader_root"), repo_root)
    snapshot.update({
        "commitSha": git_value(repo_root, "rev-parse", "HEAD"),
        "viewerExecutable": str(viewer_exe),
        "executableSha256": sha256_file(viewer_exe),
        "shaderBundleSha256": sha256_tree([shader_root], base_path=shader_root) if shader_root and shader_root.exists() else "",
        "scenarioTimelinePath": str(timeline) if timeline else "",
        "scenarioTimelineSha256": sha256_file(timeline) if timeline and timeline.is_file() else "",
        "assetManifestPath": str(asset_manifest) if asset_manifest else "",
        "assetManifestSha256": sha256_file(asset_manifest) if asset_manifest and asset_manifest.is_file() else "",
        "assetSha256": str(manifest.get("sourceAssetSha256", "")),
        "formalAssetPath": str(asset_path) if asset_path else "",
        "formalAssetSha256": str(manifest.get("sourceAssetSha256", "")),
        "formalTopologySha256": str(manifest.get("generatedTopologySha256", "")),
        "generatorSettingsSha256": str(manifest.get("generatorSettingsSha256", "")),
        "assetFrozen": str(manifest.get("freezeStatus", "")) == "FROZEN",
        "requestedMethodSettingsSha256": canonical_json_sha256(snapshot.get("method_settings", {})),
        "executionFingerprint": fingerprint,
        "executionFingerprintPayload": dict(fingerprint_payload),
    })
    return snapshot


def execute_case(case: Mapping[str, Any], viewer_exe: Path, output_dir: Path,
                 repo_root: Path, *, dry_run: bool = False) -> dict[str, Any]:
    viewer_exe = Path(viewer_exe).resolve()
    output_dir = Path(output_dir).resolve()
    repo_root = Path(repo_root).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    normalized = normalize_case(case)
    normalized = materialize_repeated_timeline(normalized, output_dir, repo_root)
    paths = output_paths(output_dir, str(normalized["run_name"]))
    contract_issues: list[dict[str, Any]] = []
    if normalized["method"] not in FORMAL_METHOD_CODES:
        contract_issues.append(issue("E_METHOD", "Unknown formal method.", observed=normalized["method"]))
    if not viewer_exe.is_file():
        contract_issues.append(issue("E_VIEWER_MISSING", "Release x64 viewer is missing.", observed=str(viewer_exe)))
    if normalized.get("build_configuration") != "Release" or normalized.get("platform") != "x64":
        contract_issues.append(issue("E_BUILD_CONTRACT", "Formal run requires Release x64."))
    exact = {
        "guide_strand_count": 4000,
        "followers_per_guide": 24,
        "display_strand_count": 100000,
        "particles_per_guide": 128,
        "solver_iterations": 6,
        "width": 1920,
        "height": 1080,
    }
    for key, expected in exact.items():
        if _to_int(normalized.get(key), -1) != expected:
            contract_issues.append(issue("E_FIXED_WORKLOAD", f"Fixed workload mismatch: {key}", expected=expected, observed=normalized.get(key)))
    if _as_bool(normalized.get("vsync")) or _to_int(normalized.get("present_sync_interval")) != 0:
        contract_issues.append(issue("E_VSYNC", "Formal run requires VSync off and present interval 0."))
    timeline = resolve_path(normalized.get("scenario_timeline"), repo_root)
    if timeline is None or not timeline.is_file():
        contract_issues.append(issue("E_TIMELINE_MISSING", "Scenario timeline is missing.", observed=str(timeline)))
    else:
        normalized["scenario_timeline"] = str(timeline)
        contract_issues.extend(validate_timeline(timeline, normalized))
    asset_manifest_path = resolve_path(normalized.get("asset_manifest"), repo_root)
    manifest: dict[str, Any] = {}
    if asset_manifest_path is None or not asset_manifest_path.is_file():
        contract_issues.append(issue("E_ASSET_MANIFEST_MISSING", "Frozen asset manifest is missing.", observed=str(asset_manifest_path)))
    else:
        normalized["asset_manifest"] = str(asset_manifest_path)
        manifest, manifest_issues = validate_asset_manifest(asset_manifest_path, repo_root)
        contract_issues.extend(manifest_issues)
    probe = collect_hardware_probe(str(normalized.get("hardware_profile", "")), "preflight")
    contract_issues.extend(evaluate_hardware_profile(probe, normalized.get("hardware_profile_contract", {})))
    if contract_issues:
        validation = {
            "validationVersion": VALIDATION_VERSION,
            "evidenceStatus": "complete_invalid",
            "stable60FpsStatus": "not_achieved",
            "contractIssues": contract_issues,
        }
        write_json(paths["validation"], validation)
        raise FormalRunError("Formal contract failed.", EXIT_FORMAL_CONTRACT_INVALID, contract_issues)
    fingerprint, fingerprint_payload = build_execution_fingerprint(normalized, viewer_exe, repo_root, manifest, probe)
    normalized["execution_fingerprint"] = fingerprint
    normalized["execution_fingerprint_payload"] = fingerprint_payload
    snapshot = _case_snapshot(normalized, viewer_exe, repo_root, manifest, fingerprint, fingerprint_payload)
    write_json(paths["case"], snapshot)
    write_json(paths["hardware"], probe)
    if dry_run:
        return {
            "runName": normalized["run_name"],
            "dryRun": True,
            "executionFingerprint": fingerprint,
            "evidenceStatus": "preflight_valid",
            "stable60FpsStatus": "not_evaluated",
        }
    for key in ("frames_csv", "frames_jsonl", "summary", "settings", "environment", "validation", "manifest", "error", "stdout", "stderr", "complete"):
        paths[key].unlink(missing_ok=True)
    try:
        lock_fd = os.open(paths["lock"], os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError as exc:
        raise FormalRunError(
            "Run lock already exists.",
            EXIT_RUNTIME_FAILURE,
            [issue("E_RUN_LOCK", "Another run or stale lock exists.", observed=str(paths["lock"]))],
        ) from exc
    os.write(lock_fd, json.dumps({
        "pid": os.getpid(),
        "runName": normalized["run_name"],
        "startedUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }).encode("utf-8"))
    write_json(paths["running"], {
        "runName": normalized["run_name"],
        "executionFingerprint": fingerprint,
        "startedUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    })
    environment = os.environ.copy()
    environment.update({
        "PYTHONHASHSEED": "0",
        "HAIRGPU_AUTO_TELEMETRY": "1",
        "HAIRGPU_TELEMETRY_DIR": str(output_dir),
        "HAIRGPU_TELEMETRY_RUN_NAME": str(normalized["run_name"]),
        "HAIRGPU_CASE_JSON": str(paths["case"]),
        "HAIRGPU_EXPERIMENT_CASE_ID": str(normalized["case_id"]),
        "HAIRGPU_VARIANT": str(normalized["method"]),
        "HAIRGPU_SCENARIO": str(normalized.get("scenario", "")),
        "HAIRGPU_SEED": str(normalized.get("seed", 0)),
        "HAIRGPU_WARMUP_FRAMES": str(normalized["warmup_frames"]),
        "HAIRGPU_MEASUREMENT_FRAMES": str(normalized["measurement_frames"]),
        "HAIRGPU_AUTO_EXIT_AFTER_CAPTURE": "1",
        "HAIRGPU_SETTINGS_HASH": str(normalized["settings_hash"]),
        "HAIRGPU_EXECUTABLE_SHA256": str(snapshot["executableSha256"]),
        "HAIRGPU_COMMIT_SHA": str(snapshot["commitSha"]),
        "HAIRGPU_BUILD_CONFIGURATION": str(normalized["build_configuration"]),
        "HAIRGPU_PLATFORM": str(normalized["platform"]),
        "HAIRGPU_HARDWARE_PROFILE": str(normalized.get("hardware_profile", "")),
        "HAIRGPU_MEASUREMENT_MODE": str(normalized.get("measurement_mode", "performance")),
        "HAIRGPU_REPEAT_INDEX": str(normalized.get("repetition_index", 0)),
        "HAIRGPU_SCENARIO_TIMELINE_SHA256": str(snapshot["scenarioTimelineSha256"]),
        "HAIRGPU_ASSET_MANIFEST_SHA256": str(snapshot["assetManifestSha256"]),
        "HAIRGPU_ASSET_SHA256": str(snapshot["assetSha256"]),
        "HAIRGPU_SHADER_BUNDLE_SHA256": str(snapshot["shaderBundleSha256"]),
        "HAIRGPU_ASSET_FROZEN": "1",
        "HAIRGPU_FORMAL_ASSET_PATH": str(snapshot["formalAssetPath"]),
        "HAIRGPU_FORMAL_ASSET_SHA256": str(snapshot["formalAssetSha256"]),
        "HAIRGPU_FORMAL_TOPOLOGY_SHA256": str(snapshot["formalTopologySha256"]),
        "HAIRGPU_GENERATOR_SETTINGS_SHA256": str(snapshot["generatorSettingsSha256"]),
        "HAIRGPU_REQUESTED_METHOD_SETTINGS_SHA256": str(snapshot["requestedMethodSettingsSha256"]),
        "HAIRGPU_EXECUTION_FINGERPRINT": fingerprint,
    })
    for key, value in normalized["method_settings"].items():
        environment[f"HAIRGPU_SETTING_{_env_token(key)}"] = _env_value(value)
        if key in DIRECT_FLAG_ENV:
            environment[DIRECT_FLAG_ENV[key]] = _env_value(value)
    _set_if_present(environment, "HAIRGPU_SCENARIO_TIMELINE", normalized.get("scenario_timeline"))
    _set_if_present(environment, "HAIRGPU_ASSET_MANIFEST", normalized.get("asset_manifest"))
    _set_if_present(environment, "HAIRGPU_RENDER_WIDTH", normalized.get("width"))
    _set_if_present(environment, "HAIRGPU_RENDER_HEIGHT", normalized.get("height"))
    _set_if_present(environment, "HAIRGPU_VSYNC", normalized.get("vsync"))
    _set_if_present(environment, "HAIRGPU_PRESENT_SYNC_INTERVAL", normalized.get("present_sync_interval"))
    _set_if_present(environment, "HAIRGPU_FIXED_TIMESTEP_ENABLED", normalized.get("fixed_timestep"))
    _set_if_present(environment, "HAIRGPU_FIXED_TIMESTEP_SECONDS", normalized.get("fixed_timestep_seconds"))
    _set_if_present(environment, "HAIRGPU_GUIDE_STRAND_COUNT", normalized.get("guide_strand_count"))
    _set_if_present(environment, "HAIRGPU_GUIDE_COUNT", normalized.get("guide_strand_count"))
    _set_if_present(environment, "HAIRGPU_DISPLAY_STRAND_COUNT", normalized.get("display_strand_count"))
    _set_if_present(environment, "HAIRGPU_PARTICLES_PER_GUIDE", normalized.get("particles_per_guide"))
    _set_if_present(environment, "HAIRGPU_SOLVER_ITERATIONS", normalized.get("solver_iterations"))
    _set_if_present(environment, "HAIRGPU_GPU_TIMESTAMP_DELAY_FRAMES", normalized.get("gpu_timestamp_delay_frames"))
    start_probe = collect_hardware_probe(str(normalized.get("hardware_profile", "")), "start")
    sampler = HardwareSampler(str(normalized.get("hardware_profile", "")), float(normalized.get("hardware_sample_interval_seconds", 1.0)))
    sampler.start()
    return_code: int | None = None
    stdout = ""
    stderr = ""
    set_execution_state(True)
    try:
        return_code, stdout, stderr = run_process(
            [str(viewer_exe)],
            viewer_exe.parent,
            environment,
            _to_int(normalized.get("timeout_seconds"), 900),
        )
        atomic_write_bytes(paths["stdout"], stdout.encode("utf-8"))
        atomic_write_bytes(paths["stderr"], stderr.encode("utf-8"))
        if return_code != 0:
            raise FormalRunError(
                f"Viewer exited with code {return_code}.",
                EXIT_RUNTIME_FAILURE,
                [issue("E_VIEWER_EXIT", "Viewer returned a nonzero exit code.", observed=return_code)],
            )
        asset_path = resolve_path(snapshot.get("formalAssetPath"))
        expected_asset_hash = str(snapshot.get("formalAssetSha256", "")).lower()
        if asset_path is None or not asset_path.is_file() or sha256_file(asset_path).lower() != expected_asset_hash:
            raise FormalRunError(
                "Formal asset bytes changed or disappeared during the run.",
                EXIT_OUTPUT_INTEGRITY_FAILURE,
                [issue("E_ASSET_POST_RUN_HASH", "Formal asset failed post-run hash verification.")],
            )
    except FormalRunError as exc:
        write_json(paths["error"], {
            "runName": normalized["run_name"],
            "exitCode": exc.exit_code,
            "error": str(exc),
            "issues": exc.issues,
            "stdoutTail": stdout,
            "stderrTail": stderr,
        })
        raise
    finally:
        samples = sampler.stop()
        end_probe = collect_hardware_probe(str(normalized.get("hardware_profile", "")), "end")
        write_hardware_timeseries(paths["hardware_timeseries"], samples)
        write_json(paths["environment"], {
            "start": start_probe,
            "end": end_probe,
            "samples": samples,
            "viewerReturnCode": return_code,
            "stdoutTail": stdout,
            "stderrTail": stderr,
            "deviceRemovedDetected": bool(re.search(r"DXGI_ERROR_DEVICE_(REMOVED|RESET)|device removed", stderr, re.IGNORECASE)),
            "executionFingerprint": fingerprint,
        })
        set_execution_state(False)
    try:
        validation = validate_outputs(snapshot, paths)
        validation["executionFingerprint"] = fingerprint
        write_json(paths["validation"], validation)
        if validation["evidenceStatus"] != "complete_valid":
            raise FormalRunError(
                "Run outputs failed evidence validation.",
                EXIT_OUTPUT_INTEGRITY_FAILURE,
                list(validation.get("integrityIssues", [])),
            )
        manifest_payload = write_artifact_manifest(paths, str(normalized["run_name"]), fingerprint)
        write_json(paths["complete"], {
            "runName": normalized["run_name"],
            "executionFingerprint": fingerprint,
            "evidenceStatus": validation["evidenceStatus"],
            "stable60FpsStatus": validation["stable60FpsStatus"],
            "manifestSha256": manifest_payload["manifestSha256"],
        })
        paths["running"].unlink(missing_ok=True)
        return validation
    except FormalRunError as exc:
        write_json(paths["error"], {
            "runName": normalized["run_name"],
            "exitCode": exc.exit_code,
            "error": str(exc),
            "issues": exc.issues,
        })
        raise
    finally:
        try:
            os.close(lock_fd)
        finally:
            paths["lock"].unlink(missing_ok=True)


def _write_test_frame_outputs(paths: Mapping[str, Path], case: Mapping[str, Any],
                              rows: list[dict[str, Any]]) -> None:
    headers = sorted({key for row in rows for key in row})
    with paths["frames_csv"].open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)
    with paths["frames_jsonl"].open("w", encoding="utf-8") as handle:
        for row in rows:
            handle.write(json.dumps(row, ensure_ascii=False, allow_nan=False) + "\n")
    write_json(paths["summary"], {
        "caseId": case["case_id"],
        "settingsSha256": case["settings_hash"],
        "realMeasurement": True,
        "gpuTimingUnresolvedFrameCount": 0,
    })
    expected_flags = case["method_settings"]
    method_hash = canonical_json_sha256(expected_flags)
    write_json(paths["settings"], {
        "requested": {key: expected_flags[key] for key in METHOD_FLAG_KEYS},
        "applied": {key: expected_flags[key] for key in METHOD_FLAG_KEYS},
        "observedFlags": {key: expected_flags[key] for key in METHOD_FLAG_KEYS},
        "methodFlagsMatched": True,
        "requestedMethodSettingsSha256": method_hash,
        "appliedMethodSettingsSha256": method_hash,
        "observedMethodSettingsSha256": method_hash,
        "formalAssetLoaded": True,
        "observedAssetPath": case["formalAssetPath"],
        "observed": {
            "renderWidth": 1920,
            "renderHeight": 1080,
            "displayStrandCount": 100000,
            "guideCount": 4000,
            "particlesPerGuide": 128,
            "solverIterations": 6,
            "vsync": False,
            "presentSyncInterval": 0,
        },
    })
    write_json(paths["environment"], {"start": {}, "end": {}, "samples": []})


def self_test() -> int:
    assert normalize_method_code("B0_FullDynamic") == "B0"
    assert normalize_method_code("P_IntegratedSegmentGroup") == "P"
    assert abs(FRAME_BUDGET_MS - 16.6666666667) < 1.0e-6
    good_rows = [{
        "frameMilliseconds": 16.0,
        "gpuHairMilliseconds": 10.0,
        "falseStaticRate": 0.0,
        "collisionMissRate": 0.0,
        "scenarioAppliedEventCount": 0,
    } for _ in range(100)]
    aggregate = aggregate_frames(good_rows)
    assert aggregate["sixtyFpsRatio"] == 1.0
    bad_rows = list(good_rows)
    bad_rows[0] = {**bad_rows[0], "frameMilliseconds": 30.0}
    assert aggregate_frames(bad_rows)["frameBudgetOverrunRate"] == 0.01
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        atomic = root / "atomic.json"
        write_json(atomic, {"ok": True})
        assert read_json(atomic)["ok"] is True
        original_hash = sha256_file(atomic)
        atomic.write_text('{"ok":false}\n', encoding="utf-8")
        assert sha256_file(atomic) != original_hash

        timeline = root / "timeline.json"
        write_json(timeline, {
            "scenario": "S01_Idle",
            "durationFrames": 3,
            "fixedTimeStepSeconds": 1.0 / 60.0,
            "events": [],
            "assertions": {"exactAppliedEventCount": 0, "timelineFinalFrameRequired": True},
        })
        formal_asset = root / "asset.hairbin"
        formal_asset.write_bytes(b"asset")
        case = normalize_case({
            "method": "P",
            "run_name": "self_test",
            "case_id": "self_test",
            "scenario": "S01_Idle",
            "scenario_timeline": str(timeline),
            "measurement_frames": 3,
            "warmup_frames": 0,
            "hardware_profile_contract": {},
            "formalAssetPath": str(formal_asset),
        })
        paths = output_paths(root, "self_test")
        rows: list[dict[str, Any]] = []
        for index in range(3):
            rows.append({
                "caseId": "self_test",
                "scenario": "S01_Idle",
                "seed": 1,
                "sourceFrameIndex": index,
                "frameMilliseconds": 16.0,
                "gpuHairMilliseconds": 10.0,
                "totalStrandCount": 100000,
                "availabilityMask": 0,
                "metricAvailabilityMask": 0,
                "syntheticMetricCount": 0,
                "sameFrameReadbackCount": 0,
                "nanStateCount": 0,
                "infiniteStateCount": 0,
                "fatalFailure": 0,
                "fatalFailureCode": 0,
                "scenarioAppliedEventCount": 0,
                "stateOscillationCount": 0,
            })
        _write_test_frame_outputs(paths, case, rows)
        result = validate_outputs(case, paths)
        assert result["evidenceStatus"] == "complete_valid", result

        _write_test_frame_outputs(paths, case, rows[:-1])
        result = validate_outputs(case, paths)
        assert result["evidenceStatus"] == "complete_invalid"
        assert any(item["code"] in {"E_CSV_FRAME_COUNT", "E_JSONL_FRAME_COUNT"} for item in result["integrityIssues"])

        _write_test_frame_outputs(paths, case, rows)
        json_rows = read_jsonl(paths["frames_jsonl"])
        json_rows[1]["sourceFrameIndex"] = 999
        with paths["frames_jsonl"].open("w", encoding="utf-8") as handle:
            for row in json_rows:
                handle.write(json.dumps(row, allow_nan=False) + "\n")
        result = validate_outputs(case, paths)
        assert any(item["code"] == "E_CSV_JSONL_PARITY" for item in result["integrityIssues"])

        rows[0]["syntheticMetricCount"] = 1
        _write_test_frame_outputs(paths, case, rows)
        result = validate_outputs(case, paths)
        assert any(item["code"] == "E_SYNTHETIC_METRIC" for item in result["integrityIssues"])
    print("hairgpu_final_run self-test: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Launch one strict HairSystem formal case.")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--case", "--case-json", dest="case_json")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--output-dir", "--telemetry-dir", dest="output_dir")
    parser.add_argument("--run-name")
    parser.add_argument("--repo-root")
    parser.add_argument("--timeout-seconds", type=int)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not all((args.case_json, args.viewer_exe, args.output_dir)):
        parser.error("--case-json, --viewer-exe and --telemetry-dir are required")
    case = read_json(Path(args.case_json))
    if args.run_name:
        case["run_name"] = args.run_name
        case["case_id"] = args.run_name
    if args.timeout_seconds is not None:
        case["timeout_seconds"] = args.timeout_seconds
    repo_root = Path(args.repo_root).resolve() if args.repo_root else Path(__file__).resolve().parents[2]
    try:
        result = execute_case(
            case,
            Path(args.viewer_exe),
            Path(args.output_dir),
            repo_root,
            dry_run=args.dry_run,
        )
    except FormalRunError as exc:
        print(str(exc), file=sys.stderr)
        for item in exc.issues:
            print(f"[{item.get('code')}] {item.get('message')}", file=sys.stderr)
        return exc.exit_code
    if args.dry_run:
        return 0
    if result.get("evidenceStatus") != "complete_valid":
        return EXIT_OUTPUT_INTEGRITY_FAILURE
    return EXIT_COMPLETE_VALID if result.get("stable60FpsStatus") == "achieved" else EXIT_QUALITY_GATE_FAILED


if __name__ == "__main__":
    raise SystemExit(main())
