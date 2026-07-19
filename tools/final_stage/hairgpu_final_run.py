from __future__ import annotations

"""Strict formal-run launcher for the HairSystem Novelty branch.

This file intentionally keeps the public helpers used by hairgpu_runtime_suite.py.
It fixes five formal-measurement blockers without introducing another source file:
lock recovery, proposal coverage, enforced telemetry contracts, portable frozen-asset
bundles, and HairSystem-process RAM/VRAM sampling.
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
from typing import Any, Callable, Iterable, Mapping

EXIT_COMPLETE_VALID = 0
EXIT_QUALITY_GATE_FAILED = 1
EXIT_FORMAL_CONTRACT_INVALID = 2
EXIT_RUNTIME_FAILURE = 3
EXIT_OUTPUT_INTEGRITY_FAILURE = 4
EXIT_CONFIGURATION_FAILURE = 5

VALIDATION_VERSION = "01-safe-v3.6-frozen-asset-source"
FROZEN_ASSET_RUNTIME_VERSION = 5

_FROZEN_ASSET_VIEWER_VALIDATION_CACHE: set[tuple[str, str]] = set()
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
    key: "HAIRGPU_" + re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", key).upper()
    for key in METHOD_FLAG_KEYS
}

REQUIRED_FRAME_COLUMNS = {
    "caseId", "scenario", "seed", "sourceFrameIndex", "frameMilliseconds",
    "gpuHairMilliseconds", "totalStrandCount", "availabilityMask",
    "metricAvailabilityMask", "syntheticMetricCount", "sameFrameReadbackCount",
    "nanStateCount", "infiniteStateCount", "fatalFailure", "fatalFailureCode",
}

AVAILABILITY_BITS = {
    "FrameInterval": 1 << 0,
    "CpuFrameWork": 1 << 1,
    "GpuTiming": 1 << 2,
    "GpuCounters": 1 << 3,
    "Scenario": 1 << 4,
    "ObservedSettings": 1 << 5,
    "CollisionGroundTruth": 1 << 6,
    "WakeGroundTruth": 1 << 7,
    "RepairVerification": 1 << 8,
    "BoundaryComparison": 1 << 9,
    "DeterminismHash": 1 << 10,
    "AdapterIdentity": 1 << 11,
    "FalseStaticGroundTruth": 1 << 12,
    "PerformanceEnvironment": 1 << 13,
    "ResultIntegrity": 1 << 14,
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


def utc_now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


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
        digest.update(relative.encode("utf-8")); digest.update(b"\0")
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        digest.update(b"\0")
    return digest.hexdigest()


def canonical_json_sha256(payload: Any) -> str:
    encoded = json.dumps(
        payload, ensure_ascii=False, sort_keys=True,
        separators=(",", ":"), allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def atomic_write_bytes(path: Path, data: bytes) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(data); handle.flush(); os.fsync(handle.fileno())
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
    except (TypeError, ValueError, OverflowError):
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


def percentile(values: list[float], p: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * p / 100.0
    lower = int(math.floor(position)); upper = int(math.ceil(position))
    fraction = position - lower
    if lower == upper:
        return ordered[lower]
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def linear_slope_per_minute(samples: list[tuple[float, float]]) -> float | None:
    clean = [(float(x), float(y)) for x, y in samples if math.isfinite(x) and math.isfinite(y)]
    if len(clean) < 3:
        return None
    xs = [item[0] for item in clean]; ys = [item[1] for item in clean]
    x_mean = sum(xs) / len(xs); y_mean = sum(ys) / len(ys)
    denominator = sum((x - x_mean) ** 2 for x in xs)
    if denominator <= 0.0:
        return 0.0
    per_second = sum((x - x_mean) * (y - y_mean) for x, y in clean) / denominator
    return per_second * 60.0


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
            ["git", *args], cwd=repo_root, capture_output=True,
            text=True, timeout=20, check=False,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return run.stdout.strip() if run.returncode == 0 else ""


def output_paths(output_dir: Path, run_name: str) -> dict[str, Path]:
    base = Path(output_dir).resolve()
    return {
        "frames": base / f"{run_name}_frames.csv",
        "jsonl": base / f"{run_name}_telemetry.jsonl",
        "settings": base / f"{run_name}_settings.json",
        "summary": base / f"{run_name}_summary.json",
        "environment": base / f"{run_name}_environment.json",
        "process_samples": base / f"{run_name}_process_memory.jsonl",
        "validation": base / f"{run_name}_validation.json",
        "manifest": base / f"{run_name}_artifact_manifest.json",
        "stdout": base / f"{run_name}_stdout.log",
        "stderr": base / f"{run_name}_stderr.log",
        "case": base / f"{run_name}_case.json",
        "running": base / f"{run_name}_RUNNING.json",
        "incomplete": base / f"{run_name}_INCOMPLETE.json",
        "complete": base / f"{run_name}_COMPLETE.json",
        "error": base / f"{run_name}_ERROR.json",
        "lock": base / f"{run_name}.run.lock",
    }


def normalize_case(case: Mapping[str, Any]) -> dict[str, Any]:
    result = dict(case)
    method = normalize_method_code(str(result.get("method", result.get("variant", ""))))
    result["method"] = method
    result.setdefault("variant", method)
    result.setdefault("run_name", result.get("case_id", "formal_case"))
    result.setdefault("case_id", result["run_name"])
    result.setdefault("build_configuration", "Release")
    result.setdefault("platform", "x64")
    result.setdefault("width", 1920); result.setdefault("height", 1080)
    result.setdefault("vsync", False); result.setdefault("present_sync_interval", 0)
    result.setdefault("fixed_timestep", True); result.setdefault("fixed_timestep_seconds", 1.0 / 60.0)
    result.setdefault("warmup_frames", 600); result.setdefault("measurement_frames", 6000)
    result.setdefault("guide_strand_count", 4000); result.setdefault("followers_per_guide", 24)
    result.setdefault("display_strand_count", 100000); result.setdefault("particles_per_guide", 128)
    result.setdefault("solver_iterations", 6); result.setdefault("gpu_timestamp_delay_frames", 6)
    result.setdefault("minimum_average_fps", 60.0)
    result.setdefault("minimum_sixty_fps_ratio", 0.95)
    result.setdefault("maximum_frame_budget_overrun_rate", 0.05)
    result.setdefault("maximum_p95_frame_ms", FRAME_BUDGET_MS)
    result.setdefault("maximum_p99_frame_ms", 20.0)
    result.setdefault("maximum_single_frame_ms", 50.0)
    result.setdefault("maximum_frames_over_100ms", 0)
    result.setdefault("minimum_gpu_timing_coverage", 0.99)
    result.setdefault("minimum_metric_coverage", 0.99)
    result.setdefault("minimum_process_memory_sample_coverage", 0.95)
    result.setdefault("maximum_process_private_memory_mib", 32768.0)
    result.setdefault("target_process_private_memory_mib", 1024.0)
    result.setdefault("maximum_dxgi_local_usage_mib", 6144.0)
    result.setdefault("timeout_seconds", 900)
    result.setdefault("hardware_sample_interval_seconds", 1.0)
    result.setdefault("required_telemetry_fields", sorted(REQUIRED_FRAME_COLUMNS))
    result.setdefault("required_availability", [])
    flags = dict(METHOD_PRESETS.get(method, {}))
    flags.update(dict(result.get("method_settings", {})))
    result["method_settings"] = flags
    if not result.get("settings_hash"):
        timeline_path = resolve_path(result.get("scenario_timeline"))
        asset_manifest_path = resolve_path(result.get("asset_manifest"))
        result["settings_hash"] = canonical_json_sha256({
            "method": method,
            "method_settings": flags,
            "scenario": result.get("scenario"),
            "seed": result.get("seed"),
            "repetition_index": result.get("repetition_index", 0),
            "timelineSha256": sha256_file(timeline_path) if timeline_path and timeline_path.is_file() else str(timeline_path),
            "assetManifestSha256": sha256_file(asset_manifest_path) if asset_manifest_path and asset_manifest_path.is_file() else str(asset_manifest_path),
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
        raise FormalRunError("Repeated timeline source is missing.", EXIT_FORMAL_CONTRACT_INVALID)
    payload = read_json(source)
    base_duration = _to_int(payload.get("durationFrames"), -1)
    events = payload.get("events")
    if base_duration <= 0 or not isinstance(events, list):
        raise FormalRunError("Repeated timeline is invalid.", EXIT_FORMAL_CONTRACT_INVALID)
    required_duration = _to_int(result.get("warmup_frames")) + _to_int(result.get("measurement_frames"))
    expanded: list[dict[str, Any]] = []
    for cycle_index, offset in enumerate(range(0, required_duration, base_duration)):
        for event_index, raw in enumerate(events):
            if not isinstance(raw, dict):
                continue
            frame = _to_int(raw.get("frame"), -1)
            expanded_frame = offset + frame
            if frame < 0 or expanded_frame >= required_duration:
                continue
            event = dict(raw); event["frame"] = expanded_frame
            event["id"] = f"{event.get('id') or event.get('type', 'event') + '_' + str(event_index)}_cycle_{cycle_index:05d}"
            expanded.append(event)
    payload["events"] = expanded; payload["durationFrames"] = required_duration
    payload["materializedFrom"] = str(source); payload["timelineRepeat"] = True
    assertions = dict(payload.get("assertions", {}))
    assertions["exactAppliedEventCount"] = len(expanded)
    assertions["timelineFinalFrameRequired"] = True
    payload["assertions"] = assertions
    output = Path(output_dir) / f"{result['run_name']}_timeline_materialized.json"
    write_json(output, payload)
    result["scenario_timeline"] = str(output); result["settings_hash"] = ""
    return normalize_case(result)


def validate_timeline(path: Path, case: Mapping[str, Any]) -> list[dict[str, Any]]:
    timeline = read_json(path)
    problems: list[dict[str, Any]] = []
    expected_scenario = str(case.get("scenario", ""))
    if str(timeline.get("scenario", "")) != expected_scenario:
        problems.append(issue("E_TIMELINE_SCENARIO", "Timeline scenario mismatch.", expected=expected_scenario, observed=timeline.get("scenario")))
    required_duration = _to_int(case.get("warmup_frames")) + _to_int(case.get("measurement_frames"))
    duration = _to_int(timeline.get("durationFrames"), -1)
    if duration < required_duration:
        problems.append(issue("E_TIMELINE_DURATION", "Timeline is shorter than the formal run.", expected=required_duration, observed=duration))
    observed_dt = _to_float(timeline.get("fixedTimeStepSeconds")); expected_dt = _to_float(case.get("fixed_timestep_seconds"))
    if observed_dt is None or expected_dt is None or abs(observed_dt - expected_dt) > 1.0e-8:
        problems.append(issue("E_TIMELINE_TIMESTEP", "Timeline fixed timestep mismatch.", expected=expected_dt, observed=observed_dt))
    events = timeline.get("events")
    if not isinstance(events, list):
        return problems + [issue("E_TIMELINE_EVENTS", "Timeline events must be an array.")]
    previous = -1; event_ids: set[str] = set()
    for index, event in enumerate(events):
        if not isinstance(event, dict):
            problems.append(issue("E_TIMELINE_EVENT_TYPE", f"Event {index} is not an object.")); continue
        frame = _to_int(event.get("frame"), -1)
        if frame < 0 or frame >= duration or frame < previous:
            problems.append(issue("E_TIMELINE_EVENT_FRAME", f"Invalid event frame at index {index}.", observed=frame))
        previous = max(previous, frame)
        event_id = str(event.get("id", ""))
        if not event_id:
            problems.append(issue("E_TIMELINE_EVENT_ID", f"Event {index} has no stable id."))
        elif event_id in event_ids:
            problems.append(issue("E_TIMELINE_EVENT_ID_DUPLICATE", "Timeline event id is duplicated.", observed=event_id))
        event_ids.add(event_id)
    assertions = timeline.get("assertions")
    if not isinstance(assertions, dict):
        problems.append(issue("E_TIMELINE_ASSERTIONS", "Timeline assertions must be an object."))
    else:
        if _to_int(assertions.get("exactAppliedEventCount"), -1) != len(events):
            problems.append(issue("E_TIMELINE_EVENT_ASSERTION", "exactAppliedEventCount must equal event count.", expected=len(events), observed=assertions.get("exactAppliedEventCount")))
        if assertions.get("timelineFinalFrameRequired") is not True:
            problems.append(issue("E_TIMELINE_FINAL_FRAME_ASSERTION", "timelineFinalFrameRequired must be true."))
    return problems


def _resolve_bundle_member(manifest_path: Path, value: Any) -> tuple[Path | None, str | None]:
    if value in (None, ""):
        return None, None
    raw = Path(str(value))
    if raw.is_absolute():
        return None, "absolute paths are forbidden in frozen bundles"
    root = manifest_path.parent.resolve(); resolved = (root / raw).resolve()
    try:
        resolved.relative_to(root)
    except ValueError:
        return None, "path escapes the frozen bundle"
    if resolved.is_symlink():
        return None, "symbolic links are forbidden in frozen bundles"
    return resolved, None


def validate_asset_manifest(path: Path, repo_root: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    manifest = read_json(path); problems: list[dict[str, Any]] = []
    if str(manifest.get("manifestVersion", "")) != "novelty01-100k-v1.6":
        problems.append(issue("E_ASSET_MANIFEST_VERSION", "Frozen asset manifest version is incompatible.", observed=manifest.get("manifestVersion")))
    if manifest.get("freezeStatus") != "FROZEN":
        problems.append(issue("E_ASSET_NOT_FROZEN", "Formal asset manifest must be FROZEN.", observed=manifest.get("freezeStatus")))
    generation = manifest.get("generation") if isinstance(manifest.get("generation"), dict) else {}
    expected_generation = {
        "displayStrandCount": 100000, "primaryGuideStrandCount": 4000,
        "followersPerGuide": 24, "particlesPerGuide": 128, "solverIterations": 6,
        "groupsPerBlock": 8,
    }
    for key, expected in expected_generation.items():
        if _to_int(generation.get(key), -1) != expected:
            problems.append(issue("E_ASSET_GENERATION_CONTRACT", f"Invalid generation value: {key}", expected=expected, observed=generation.get(key)))
    checks = (
        ("sourceAssetPath", "sourceAssetSha256", "sourceAssetSizeBytes", "E_ASSET"),
        ("generatedTopologyPath", "generatedTopologySha256", None, "E_TOPOLOGY"),
        ("generatorSettingsPath", "generatorSettingsSha256", None, "E_GENERATOR_SETTINGS"),
    )
    resolved_members: dict[str, str] = {}
    for path_key, hash_key, size_key, code in checks:
        member, path_error = _resolve_bundle_member(path, manifest.get(path_key))
        expected_hash = str(manifest.get(hash_key, ""))
        if path_error:
            problems.append(issue(code + "_PATH", path_error, observed=manifest.get(path_key))); continue
        if member is None or not member.is_file():
            problems.append(issue(code + "_MISSING", "Frozen bundle member is missing.", observed=str(member))); continue
        resolved_members[path_key] = str(member)
        if not SHA256_RE.fullmatch(expected_hash):
            problems.append(issue(code + "_HASH_FORMAT", "SHA-256 is invalid.", observed=expected_hash)); continue
        actual_hash = sha256_file(member)
        if actual_hash.lower() != expected_hash.lower():
            problems.append(issue(code + "_HASH", "Frozen bundle member hash mismatch.", expected=expected_hash, observed=actual_hash))
        if size_key and _to_int(manifest.get(size_key), -1) != member.stat().st_size:
            problems.append(issue(code + "_SIZE", "Frozen bundle member size mismatch.", expected=manifest.get(size_key), observed=member.stat().st_size))
    entries = manifest.get("bundleFiles")
    if isinstance(entries, list) and entries:
        canonical_entries: list[dict[str, Any]] = []
        for raw in entries:
            if not isinstance(raw, dict):
                problems.append(issue("E_BUNDLE_ENTRY", "bundleFiles entry must be an object.")); continue
            member, path_error = _resolve_bundle_member(path, raw.get("path"))
            if path_error or member is None or not member.is_file():
                problems.append(issue("E_BUNDLE_ENTRY_PATH", path_error or "bundle member missing", observed=raw.get("path"))); continue
            actual = {"path": Path(str(raw.get("path"))).as_posix(), "sizeBytes": member.stat().st_size, "sha256": sha256_file(member)}
            canonical_entries.append(actual)
            if _to_int(raw.get("sizeBytes"), -1) != actual["sizeBytes"] or str(raw.get("sha256", "")).lower() != actual["sha256"].lower():
                problems.append(issue("E_BUNDLE_ENTRY_HASH", "bundleFiles metadata mismatch.", observed=raw.get("path")))
        expected_bundle_hash = str(manifest.get("bundleSha256", ""))
        actual_bundle_hash = canonical_json_sha256(sorted(canonical_entries, key=lambda item: item["path"]))
        if not SHA256_RE.fullmatch(expected_bundle_hash) or expected_bundle_hash.lower() != actual_bundle_hash.lower():
            problems.append(issue("E_BUNDLE_HASH", "Frozen bundle hash mismatch.", expected=expected_bundle_hash, observed=actual_bundle_hash))
    commit = str(manifest.get("assetSourceCommitSha", manifest.get("sourceCommitSha", "")))
    if not COMMIT_RE.fullmatch(commit):
        problems.append(issue("E_ASSET_SOURCE_COMMIT_FORMAT", "Asset source commit is invalid.", observed=commit))
    if manifest.get("sourceBranch") != "Novelty":
        problems.append(issue("E_ASSET_SOURCE_BRANCH", "Frozen asset must come from Novelty.", observed=manifest.get("sourceBranch")))
    minimum_runtime = _to_int(manifest.get("minimumCompatibleRuntimeVersion", 1), 1)
    if minimum_runtime < 1 or minimum_runtime > FROZEN_ASSET_RUNTIME_VERSION:
        problems.append(issue(
            "E_ASSET_RUNTIME_COMPATIBILITY",
            "Frozen asset requires an unsupported runtime contract version.",
            expected=f"<= {FROZEN_ASSET_RUNTIME_VERSION}",
            observed=minimum_runtime,
        ))
    if str(manifest.get("manifestVersion", "")) == "novelty01-100k-v1.6":
        expected_identity = {
            "generationMode": "viewer-canonical-export",
            "generatorSettingsSchemaVersion": "novelty01-generator-settings-v1",
            "assetFormatVersion": 1,
            "topologyFormatVersion": 1,
            "assetInternalValidation": "PASS",
            "topologyInternalValidation": "PASS",
            "roundTripValidation": "PASS",
        }
        for key, expected_value in expected_identity.items():
            if manifest.get(key) != expected_value:
                problems.append(issue(
                    "E_ASSET_CANONICAL_IDENTITY",
                    f"Frozen asset canonical identity mismatch: {key}",
                    expected=expected_value,
                    observed=manifest.get(key),
                ))
    manifest["_resolvedMembers"] = resolved_members
    manifest["_runtimeCommitSha"] = git_value(repo_root, "rev-parse", "HEAD")
    return manifest, problems


def validate_frozen_asset_with_viewer(
    viewer_exe: Path, manifest_path: Path
) -> list[dict[str, Any]]:
    """Ask the exact Release viewer to parse and round-trip the frozen bundle.

    Hash validation alone cannot detect a structurally incompatible pair of Hair
    Binary and topology files. The CPU-only viewer command validates internal
    counts, topology hash, generator settings and binary round trip without
    creating a D3D device or entering the render loop.
    """
    if not viewer_exe.is_file():
        return [issue(
            "E_VIEWER_MISSING",
            "Viewer executable is missing for frozen-asset validation.",
            observed=str(viewer_exe),
        )]
    try:
        cache_key = (sha256_file(viewer_exe), sha256_file(manifest_path))
    except OSError as exc:
        return [issue(
            "E_FROZEN_ASSET_RUNTIME_VALIDATION_IDENTITY",
            "Frozen-asset validation identity could not be computed.",
            observed=str(exc),
        )]
    if cache_key in _FROZEN_ASSET_VIEWER_VALIDATION_CACHE:
        return []
    try:
        completed = subprocess.run(
            [str(viewer_exe), "--validate-frozen-source", str(manifest_path.parent.resolve())],
            cwd=viewer_exe.parent,
            capture_output=True,
            text=True,
            timeout=600,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return [issue(
            "E_FROZEN_ASSET_RUNTIME_VALIDATION_TIMEOUT",
            "Viewer frozen-asset validation timed out.",
        )]
    except (OSError, subprocess.SubprocessError) as exc:
        return [issue(
            "E_FROZEN_ASSET_RUNTIME_VALIDATION_LAUNCH",
            "Viewer frozen-asset validation could not be launched.",
            observed=str(exc),
        )]
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "<no output>").strip()
        return [issue(
            "E_FROZEN_ASSET_RUNTIME_VALIDATION",
            "Viewer rejected the frozen asset bundle.",
            observed=detail[-8000:],
        )]
    _FROZEN_ASSET_VIEWER_VALIDATION_CACHE.add(cache_key)
    return []


def _process_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if sys.platform == "win32":
        SYNCHRONIZE = 0x00100000; WAIT_TIMEOUT = 0x00000102
        handle = ctypes.windll.kernel32.OpenProcess(SYNCHRONIZE, False, pid)
        if not handle:
            return False
        try:
            return ctypes.windll.kernel32.WaitForSingleObject(handle, 0) == WAIT_TIMEOUT
        finally:
            ctypes.windll.kernel32.CloseHandle(handle)
    try:
        os.kill(pid, 0); return True
    except (ProcessLookupError, PermissionError):
        return False


def recover_stale_run_lock(paths: Mapping[str, Path]) -> None:
    lock_path = paths["lock"]
    if not lock_path.exists():
        return
    try:
        payload = read_json(lock_path)
    except Exception:
        payload = {}
    pid = _to_int(payload.get("pid"), 0)
    if _process_alive(pid):
        raise FormalRunError(
            "Another live process owns this run.", EXIT_RUNTIME_FAILURE,
            [issue("E_RUN_LOCK_ACTIVE", "Run lock is active.", observed=pid)],
        )
    stale = {
        "state": "INCOMPLETE", "reason": "stale_lock_recovered",
        "recoveredUtc": utc_now(), "previousLock": payload,
    }
    if paths["running"].exists():
        try:
            stale["running"] = read_json(paths["running"])
        except Exception:
            stale["running"] = {"unreadable": True}
    write_json(paths["incomplete"], stale)
    paths["running"].unlink(missing_ok=True)
    lock_path.unlink(missing_ok=True)


class RunLease:
    def __init__(self, paths: Mapping[str, Path], run_name: str, fingerprint: str):
        self.paths = paths; self.run_name = run_name; self.fingerprint = fingerprint
        self.fd: int | None = None; self.completed = False

    def __enter__(self) -> "RunLease":
        recover_stale_run_lock(self.paths)
        payload = {
            "pid": os.getpid(), "runName": self.run_name,
            "executionFingerprint": self.fingerprint,
            "startedUtc": utc_now(), "hostName": platform.node(),
        }
        flags = os.O_CREAT | os.O_EXCL | os.O_WRONLY
        try:
            self.fd = os.open(self.paths["lock"], flags, 0o600)
        except FileExistsError as exc:
            raise FormalRunError("Run lock already exists.", EXIT_RUNTIME_FAILURE) from exc
        os.write(self.fd, (json.dumps(payload, ensure_ascii=False) + "\n").encode("utf-8"))
        os.fsync(self.fd)
        write_json(self.paths["running"], payload)
        self.paths["incomplete"].unlink(missing_ok=True)
        return self

    def mark_completed(self) -> None:
        self.completed = True

    def __exit__(self, exc_type, exc, traceback) -> None:
        try:
            if self.fd is not None:
                os.close(self.fd); self.fd = None
        finally:
            self.paths["lock"].unlink(missing_ok=True)
        if self.completed:
            self.paths["running"].unlink(missing_ok=True)
            self.paths["incomplete"].unlink(missing_ok=True)
        else:
            payload = {
                "state": "INCOMPLETE", "runName": self.run_name,
                "executionFingerprint": self.fingerprint,
                "endedUtc": utc_now(),
                "exceptionType": getattr(exc_type, "__name__", "unknown") if exc_type else "unknown",
                "exception": str(exc) if exc else "run did not complete",
            }
            write_json(self.paths["incomplete"], payload)
            self.paths["running"].unlink(missing_ok=True)


class PROCESS_MEMORY_COUNTERS_EX(ctypes.Structure):
    _fields_ = [
        ("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong),
        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t),
        ("PrivateUsage", ctypes.c_size_t),
    ]


def _windows_process_memory(pid: int) -> dict[str, float]:
    if sys.platform != "win32":
        return {}
    PROCESS_QUERY_INFORMATION = 0x0400; PROCESS_VM_READ = 0x0010
    handle = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not handle:
        return {}
    try:
        counters = PROCESS_MEMORY_COUNTERS_EX(); counters.cb = ctypes.sizeof(counters)
        if not ctypes.windll.psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            return {}
        scale = 1024.0 * 1024.0
        return {
            "workingSetMiB": counters.WorkingSetSize / scale,
            "peakWorkingSetMiB": counters.PeakWorkingSetSize / scale,
            "privateBytesMiB": counters.PrivateUsage / scale,
            "commitMiB": counters.PagefileUsage / scale,
        }
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)


def _proc_status_memory(pid: int) -> dict[str, float]:
    path = Path(f"/proc/{pid}/status")
    if not path.is_file():
        return {}
    values: dict[str, float] = {}
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        if ":" not in line:
            continue
        key, raw = line.split(":", 1)
        parts = raw.strip().split()
        if not parts:
            continue
        try:
            mib = float(parts[0]) / 1024.0
        except ValueError:
            continue
        if key == "VmRSS": values["workingSetMiB"] = mib
        elif key == "VmHWM": values["peakWorkingSetMiB"] = mib
        elif key == "VmSize": values["privateBytesMiB"] = mib
    return values


def _powershell_gpu_memory(pid: int) -> dict[str, float]:
    if sys.platform != "win32":
        return {}
    escaped = str(int(pid))
    script = (
        "$ErrorActionPreference='SilentlyContinue';"
        f"$p='{escaped}';"
        "$d=(Get-Counter '\\GPU Process Memory(*)\\Dedicated Usage').CounterSamples|"
        "?{$_.InstanceName -match ('pid_'+$p+'_')}|measure CookedValue -Sum;"
        "$s=(Get-Counter '\\GPU Process Memory(*)\\Shared Usage').CounterSamples|"
        "?{$_.InstanceName -match ('pid_'+$p+'_')}|measure CookedValue -Sum;"
        "[Console]::WriteLine(('{0},{1}' -f ([double]$d.Sum),([double]$s.Sum)))"
    )
    try:
        run = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
            capture_output=True, text=True, timeout=8, check=False,
        )
        if run.returncode == 0 and run.stdout.strip():
            dedicated, shared = (run.stdout.strip().splitlines()[-1].split(",") + ["0", "0"])[:2]
            return {
                "dxgiLocalUsageMiB": float(dedicated) / (1024.0 * 1024.0),
                "dxgiNonLocalUsageMiB": float(shared) / (1024.0 * 1024.0),
                "gpuMemoryProbe": "windows_gpu_process_memory_counter",
            }
    except Exception:
        pass
    return {}


def _nvidia_process_memory(pid: int) -> dict[str, float]:
    try:
        run = subprocess.run(
            ["nvidia-smi", "--query-compute-apps=pid,used_gpu_memory", "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=8, check=False,
        )
        total = 0.0
        for line in run.stdout.splitlines():
            fields = [part.strip() for part in line.split(",")]
            if len(fields) >= 2 and _to_int(fields[0], -1) == pid:
                total += float(fields[1])
        if total > 0.0:
            return {"dxgiLocalUsageMiB": total, "gpuMemoryProbe": "nvidia_smi_compute_apps"}
    except Exception:
        pass
    return {}


class ProcessSampler:
    def __init__(self, interval_seconds: float = 1.0):
        self.interval_seconds = max(0.2, float(interval_seconds))
        self.pid = 0; self.samples: list[dict[str, Any]] = []
        self._stop = threading.Event(); self._thread: threading.Thread | None = None
        self._started_monotonic = 0.0

    def attach_process(self, process: subprocess.Popen[str]) -> None:
        self.pid = int(process.pid); self._started_monotonic = time.monotonic()
        self._thread = threading.Thread(target=self._loop, name="hairgpu-process-sampler", daemon=True)
        self._thread.start()

    def _sample_once(self) -> None:
        memory = _windows_process_memory(self.pid) if sys.platform == "win32" else _proc_status_memory(self.pid)
        gpu = _powershell_gpu_memory(self.pid)
        if not gpu:
            gpu = _nvidia_process_memory(self.pid)
        gpu_health = _nvidia_gpu_health()
        self.samples.append({
            "timestampUtc": utc_now(),
            "elapsedSeconds": max(0.0, time.monotonic() - self._started_monotonic),
            "pid": self.pid, **memory, **gpu, **gpu_health,
        })

    def _loop(self) -> None:
        while not self._stop.is_set():
            self._sample_once()
            self._stop.wait(self.interval_seconds)

    def stop(self) -> None:
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=max(2.0, self.interval_seconds * 2.0))
        if self.pid and (not self.samples or self.samples[-1].get("elapsedSeconds", 0) < time.monotonic() - self._started_monotonic - 0.1):
            self._sample_once()

    def summary(self, expected_duration_seconds: float | None = None) -> dict[str, Any]:
        def values(key: str) -> list[float]:
            return [float(row[key]) for row in self.samples if _to_float(row.get(key)) is not None]
        private = values("privateBytesMiB"); working = values("workingSetMiB")
        local = values("dxgiLocalUsageMiB"); nonlocal_values = values("dxgiNonLocalUsageMiB")
        temperatures = values("gpuTemperatureC"); clocks = values("gpuGraphicsClockMHz")
        private_series = [(float(row.get("elapsedSeconds", 0.0)), float(row["privateBytesMiB"])) for row in self.samples if _to_float(row.get("privateBytesMiB")) is not None]
        local_series = [(float(row.get("elapsedSeconds", 0.0)), float(row["dxgiLocalUsageMiB"])) for row in self.samples if _to_float(row.get("dxgiLocalUsageMiB")) is not None]
        expected_samples = max(1, int((expected_duration_seconds or 0.0) / self.interval_seconds)) if expected_duration_seconds else len(self.samples)
        return {
            "pid": self.pid,
            "sampleCount": len(self.samples),
            "expectedSampleCount": expected_samples,
            "sampleCoverage": min(1.0, len(private_series) / max(1, expected_samples)),
            "gpuSampleCoverage": min(1.0, len(local_series) / max(1, expected_samples)),
            "peakProcessPrivateMiB": max(private) if private else None,
            "peakProcessWorkingSetMiB": max(working) if working else None,
            "processPrivateSlopeMiBPerMin": linear_slope_per_minute(private_series),
            "peakDxgiLocalUsageMiB": max(local) if local else None,
            "dxgiLocalUsageSlopeMiBPerMin": linear_slope_per_minute(local_series),
            "peakDxgiNonLocalUsageMiB": max(nonlocal_values) if nonlocal_values else None,
            "gpuProbe": next((str(row.get("gpuMemoryProbe")) for row in reversed(self.samples) if row.get("gpuMemoryProbe")), "unavailable"),
            "gpuTemperatureMaxC": max(temperatures) if temperatures else None,
            "gpuTemperatureSpreadC": (max(temperatures) - min(temperatures)) if temperatures else None,
            "gpuClockDropRatio": ((max(clocks) - min(clocks)) / max(clocks)) if clocks and max(clocks) > 0.0 else None,
            "gpuHealthSampleCount": min(len(temperatures), len(clocks)),
        }


def _write_jsonl(path: Path, rows: Iterable[Mapping[str, Any]]) -> None:
    payload = "".join(json.dumps(dict(row), ensure_ascii=False, allow_nan=False) + "\n" for row in rows)
    atomic_write_bytes(path, payload.encode("utf-8"))


def collect_hardware_probe(profile_name: str = "", phase: str = "") -> dict[str, Any]:
    result: dict[str, Any] = {
        "profile": profile_name, "phase": phase, "timestampUtc": utc_now(),
        "platform": platform.platform(), "osBuild": platform.version(),
        "machine": platform.machine(), "processor": platform.processor(),
        "pythonVersion": platform.python_version(), "pythonExecutable": sys.executable,
        "logicalCpuCount": os.cpu_count(), "gpu": {}, "power": {}, "systemMemory": {},
    }
    if sys.platform != "win32":
        return result
    host_contract = _powershell_host_contract()
    if host_contract:
        result["physicalCpuCoreCount"] = host_contract.get("physicalCpuCoreCount")
        result["backgroundCaptureDisabled"] = host_contract.get("backgroundCaptureDisabled")
        result["power"]["acPower"] = host_contract.get("acPower")
    try:
        command = [
            "nvidia-smi",
            "--query-gpu=index,name,uuid,driver_version,temperature.gpu,clocks.current.graphics,clocks.current.memory,power.draw,power.limit,memory.total,memory.used",
            "--format=csv,noheader,nounits",
        ]
        run = subprocess.run(command, capture_output=True, text=True, timeout=10, check=False)
        fields = [item.strip() for item in run.stdout.splitlines()[0].split(",")] if run.stdout.strip() else []
        if len(fields) >= 11:
            result["gpu"] = {
                "index": _to_int(fields[0]), "name": fields[1], "uuid": fields[2],
                "driverVersion": fields[3], "temperatureC": _to_float(fields[4]),
                "graphicsClockMHz": _to_float(fields[5]), "memoryClockMHz": _to_float(fields[6]),
                "powerDrawW": _to_float(fields[7]), "powerLimitW": _to_float(fields[8]),
                "memoryTotalMiB": _to_float(fields[9]), "memoryUsedMiB": _to_float(fields[10]),
            }
    except Exception as exc:
        result["gpuProbeError"] = str(exc)
    try:
        run = subprocess.run(["powercfg", "/GETACTIVESCHEME"], capture_output=True, text=True, timeout=10, check=False)
        result["power"]["activeScheme"] = run.stdout.strip()
    except Exception as exc:
        result["powerProbeError"] = str(exc)
    return result


def _build_environment(case: Mapping[str, Any], paths: Mapping[str, Path], repo_root: Path,
                       viewer_exe: Path, asset_manifest: Mapping[str, Any]) -> dict[str, str]:
    env = dict(os.environ)
    timeline_path = resolve_path(case.get("scenario_timeline"), repo_root)
    manifest_path = resolve_path(case.get("asset_manifest"), repo_root)
    shader_root = resolve_path(case.get("shader_root"), repo_root)
    method_settings_hash = canonical_json_sha256(dict(case.get("method_settings", {})))
    timeline_hash = sha256_file(timeline_path) if timeline_path and timeline_path.is_file() else ""
    manifest_hash = sha256_file(manifest_path) if manifest_path and manifest_path.is_file() else ""
    shader_hash = sha256_tree([shader_root], base_path=repo_root) if shader_root and shader_root.exists() else ""
    values = {
        "HAIRGPU_EXPERIMENT_MODE": 1,
        "HAIRGPU_CASE_ID": case["case_id"],
        "HAIRGPU_EXPERIMENT_CASE_ID": case["case_id"],
        "HAIRGPU_RUN_NAME": case["run_name"],
        "HAIRGPU_VARIANT": case["variant"],
        "HAIRGPU_METHOD": case["method"],
        "HAIRGPU_SCENARIO": case.get("scenario", ""),
        "HAIRGPU_SEED": case.get("seed", 0),
        "HAIRGPU_REPEAT_INDEX": case.get("repetition_index", 0),
        "HAIRGPU_MEASUREMENT_MODE": case.get("measurement_mode", "performance"),
        "HAIRGPU_WIDTH": case["width"], "HAIRGPU_HEIGHT": case["height"],
        "HAIRGPU_RENDER_WIDTH": case["width"], "HAIRGPU_RENDER_HEIGHT": case["height"],
        "HAIRGPU_FIXED_TIMESTEP": case["fixed_timestep"],
        "HAIRGPU_FIXED_TIMESTEP_SECONDS": case["fixed_timestep_seconds"],
        "HAIRGPU_VSYNC": case["vsync"], "HAIRGPU_PRESENT_SYNC_INTERVAL": case["present_sync_interval"],
        "HAIRGPU_WARMUP_FRAMES": case["warmup_frames"],
        "HAIRGPU_MEASUREMENT_FRAMES": case["measurement_frames"],
        "HAIRGPU_GUIDE_STRAND_COUNT": case["guide_strand_count"],
        "HAIRGPU_GUIDE_COUNT": case["guide_strand_count"],
        "HAIRGPU_FOLLOWERS_PER_GUIDE": case["followers_per_guide"],
        "HAIRGPU_DISPLAY_STRAND_COUNT": case["display_strand_count"],
        "HAIRGPU_PARTICLES_PER_GUIDE": case["particles_per_guide"],
        "HAIRGPU_SOLVER_ITERATIONS": case["solver_iterations"],
        "HAIRGPU_GPU_TIMESTAMP_DELAY_FRAMES": case["gpu_timestamp_delay_frames"],
        "HAIRGPU_AUTO_EXIT_AFTER_CAPTURE": 1,
        "HAIRGPU_STRICT_VALIDATION": 1,
        "HAIRGPU_REAL_MEASUREMENT": 1,
        "HAIRGPU_OUTPUT_DIR": str(paths["frames"].parent),
        "HAIRGPU_TELEMETRY_CSV": str(paths["frames"]),
        "HAIRGPU_TELEMETRY_JSONL": str(paths["jsonl"]),
        "HAIRGPU_SETTINGS_JSON": str(paths["settings"]),
        "HAIRGPU_SUMMARY_JSON": str(paths["summary"]),
        "HAIRGPU_SCENARIO_TIMELINE": case.get("scenario_timeline", ""),
        "HAIRGPU_ASSET_MANIFEST": case.get("asset_manifest", ""),
        "HAIRGPU_SETTINGS_HASH": case.get("settings_hash", ""),
        "HAIRGPU_COMMIT_SHA": git_value(repo_root, "rev-parse", "HEAD"),
        "HAIRGPU_EXECUTABLE_SHA256": sha256_file(viewer_exe),
        "HAIRGPU_ASSET_FROZEN": 1,
        "HAIRGPU_ASSET_SHA256": asset_manifest.get("sourceAssetSha256", ""),
        "HAIRGPU_BUILD_CONFIGURATION": case.get("build_configuration", "Release"),
        "HAIRGPU_PLATFORM": case.get("platform", "x64"),
        "HAIRGPU_HARDWARE_PROFILE": case.get("hardware_profile", ""),
        "HAIRGPU_SCENARIO_TIMELINE_SHA256": timeline_hash,
        "HAIRGPU_ASSET_MANIFEST_SHA256": manifest_hash,
        "HAIRGPU_SHADER_BUNDLE_SHA256": shader_hash,
        "HAIRGPU_REQUESTED_METHOD_SETTINGS_SHA256": method_settings_hash,
        "HAIRGPU_APPLIED_METHOD_SETTINGS_SHA256": method_settings_hash,
    }
    for key, value in values.items():
        if isinstance(value, bool):
            env[key] = "1" if value else "0"
        elif value not in (None, ""):
            env[key] = str(value)
    for key, value in case.get("method_settings", {}).items():
        env[DIRECT_FLAG_ENV.get(key, "HAIRGPU_" + re.sub(r"[^A-Za-z0-9]+", "_", key).upper())] = (
            "1" if value is True else "0" if value is False else str(value)
        )
    return env


def run_process(command: list[str], cwd: Path, environment: Mapping[str, str],
                timeout_seconds: int,
                on_started: Callable[[subprocess.Popen[str]], None] | None = None) -> tuple[int, str, str, bool]:
    creationflags = subprocess.CREATE_NEW_PROCESS_GROUP if sys.platform == "win32" else 0
    process = subprocess.Popen(
        command, cwd=cwd, env=dict(environment), stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace",
        creationflags=creationflags,
    )
    if on_started:
        on_started(process)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=max(1, int(timeout_seconds)))
    except subprocess.TimeoutExpired:
        timed_out = True
        if sys.platform == "win32":
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True, check=False)
        else:
            try:
                os.killpg(os.getpgid(process.pid), signal.SIGKILL)
            except Exception:
                process.kill()
        stdout, stderr = process.communicate()
    return int(process.returncode if process.returncode is not None else -1), stdout, stderr, timed_out


def _field_coverage(rows: list[Mapping[str, Any]], field: str) -> float:
    if not rows:
        return 0.0
    available = 0
    for row in rows:
        value = row.get(field)
        if isinstance(value, (int, float)):
            available += int(math.isfinite(float(value)))
        elif value not in (None, ""):
            try:
                available += int(math.isfinite(float(value)))
            except (TypeError, ValueError):
                available += 1
    return available / len(rows)


# HAIRGPU_EXISTING_FILE_GATES_V1
def _values_equal(left: Any, right: Any) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return bool(left) == bool(right)
    left_number = _to_float(left)
    right_number = _to_float(right)
    if left_number is not None and right_number is not None:
        return abs(left_number - right_number) <= 1.0e-6
    return str(left or "") == str(right or "")


def _validate_settings_contract(
    case: Mapping[str, Any],
    settings: Mapping[str, Any],
    summary: Mapping[str, Any],
) -> list[dict[str, Any]]:
    problems: list[dict[str, Any]] = []
    requested = settings.get("requested")
    applied = settings.get("applied")
    observed_flags = settings.get("observedFlags")
    observed = settings.get("observed")

    if not isinstance(requested, dict):
        problems.append(issue("E_SETTINGS_REQUESTED_MISSING", "Settings JSON is missing requested flags."))
        requested = {}
    if not isinstance(applied, dict):
        problems.append(issue("E_SETTINGS_APPLIED_MISSING", "Settings JSON is missing applied flags."))
        applied = {}
    if not isinstance(observed_flags, dict):
        problems.append(issue("E_SETTINGS_OBSERVED_FLAGS_MISSING", "Settings JSON is missing observed flags."))
        observed_flags = {}
    if not isinstance(observed, dict):
        problems.append(issue("E_SETTINGS_OBSERVED_MISSING", "Settings JSON is missing observed runtime values."))
        observed = {}

    if requested != applied:
        problems.append(issue(
            "E_SETTINGS_REQUESTED_APPLIED_MISMATCH",
            "Requested and applied method flags differ.",
            expected=requested,
            observed=applied,
        ))
    if applied != observed_flags:
        problems.append(issue(
            "E_SETTINGS_APPLIED_OBSERVED_MISMATCH",
            "Applied and observed method flags differ.",
            expected=applied,
            observed=observed_flags,
        ))

    expected_method = normalize_method_code(str(case.get("method", "")))
    for field in ("requestedMethodCode", "appliedMethodCode"):
        actual = normalize_method_code(str(settings.get(field, "")))
        if actual != expected_method:
            problems.append(issue(
                "E_SETTINGS_METHOD_CODE",
                f"Settings method code mismatch: {field}",
                expected=expected_method,
                observed=actual,
            ))

    checks = {
        "renderWidth": case.get("width"),
        "renderHeight": case.get("height"),
        "displayStrandCount": case.get("display_strand_count"),
        "guideCount": case.get("guide_strand_count"),
        "particlesPerGuide": case.get("particles_per_guide"),
        "solverIterations": case.get("solver_iterations"),
        "vsync": case.get("vsync"),
        "presentSyncInterval": case.get("present_sync_interval"),
    }
    for field, expected in checks.items():
        actual = observed.get(field)
        if not _values_equal(actual, expected):
            problems.append(issue(
                "E_SETTINGS_OBSERVED_VALUE",
                f"Observed runtime setting mismatch: {field}",
                expected=expected,
                observed=actual,
            ))

    if bool(case.get("real_measurement_required", True)):
        adapter = str(observed.get("adapterType", "")).lower()
        if adapter != "hardware":
            problems.append(issue(
                "E_SETTINGS_ADAPTER",
                "Formal measurement requires a hardware D3D11 adapter.",
                expected="hardware",
                observed=adapter,
            ))

    for field in (
        "settingsSha256",
        "commitSha",
        "executableSha256",
        "shaderBundleSha256",
        "assetSha256",
        "assetManifestSha256",
        "scenarioTimelineSha256",
        "requestedMethodSettingsSha256",
        "appliedMethodSettingsSha256",
        "observedMethodSettingsSha256",
    ):
        value = str(settings.get(field, ""))
        expected_length = 40 if field == "commitSha" else 64
        valid = (
            len(value) == expected_length
            and all(ch in "0123456789abcdefABCDEF" for ch in value)
        )
        if not valid:
            problems.append(issue(
                "E_SETTINGS_IDENTITY_HASH",
                f"Missing or invalid identity hash: {field}",
                observed=value,
            ))

    requested_hash = str(settings.get("requestedMethodSettingsSha256", ""))
    applied_hash = str(settings.get("appliedMethodSettingsSha256", ""))
    observed_hash = str(settings.get("observedMethodSettingsSha256", ""))
    if not requested_hash or requested_hash != applied_hash or applied_hash != observed_hash:
        problems.append(issue(
            "E_SETTINGS_METHOD_HASH_MISMATCH",
            "Requested/applied/observed method-setting hashes must be identical.",
            expected=requested_hash,
            observed={"applied": applied_hash, "observed": observed_hash},
        ))

    parity_fields = (
        "requestedMethodCode",
        "appliedMethodCode",
        "settingsSha256",
        "commitSha",
        "executableSha256",
        "shaderBundleSha256",
        "assetSha256",
        "assetManifestSha256",
        "scenarioTimelineSha256",
        "requestedMethodSettingsSha256",
        "appliedMethodSettingsSha256",
        "observedMethodSettingsSha256",
    )
    for field in parity_fields:
        if not _values_equal(summary.get(field), settings.get(field)):
            problems.append(issue(
                "E_SETTINGS_SUMMARY_PARITY",
                f"Settings/Summary mismatch: {field}",
                expected=settings.get(field),
                observed=summary.get(field),
            ))

    for field, expected in (
        ("actualRenderWidth", case.get("width")),
        ("actualRenderHeight", case.get("height")),
        ("actualDisplayStrandCount", case.get("display_strand_count")),
    ):
        if not _values_equal(summary.get(field), expected):
            problems.append(issue(
                "E_SUMMARY_OBSERVED_VALUE",
                f"Summary runtime value mismatch: {field}",
                expected=expected,
                observed=summary.get(field),
            ))

    top_level_checks = (
        ("caseId", case.get("case_id")),
        ("scenario", case.get("scenario")),
        ("seed", case.get("seed")),
        ("repetitionIndex", case.get("repetition_index", 0)),
        ("warmupFrames", case.get("warmup_frames")),
        ("measurementFrames", case.get("measurement_frames")),
        ("fixedTimeStepSeconds", case.get("fixed_timestep_seconds")),
    )
    for field, expected in top_level_checks:
        if not _values_equal(settings.get(field), expected):
            problems.append(issue(
                "E_SETTINGS_TOP_LEVEL_VALUE",
                f"Settings top-level value mismatch: {field}",
                expected=expected,
                observed=settings.get(field),
            ))

    if settings.get("methodFlagsMatched") is not True:
        problems.append(issue(
            "E_SETTINGS_METHOD_FLAGS_NOT_MATCHED",
            "Viewer did not confirm requested/applied/observed method flags.",
            expected=True,
            observed=settings.get("methodFlagsMatched"),
        ))
    if bool(case.get("real_measurement_required", True)) and settings.get("realMeasurement") is not True:
        problems.append(issue(
            "E_SETTINGS_REAL_MEASUREMENT",
            "Settings JSON is not marked as a real measurement.",
            expected=True,
            observed=settings.get("realMeasurement"),
        ))
    if bool(case.get("real_measurement_required", True)):
        if summary.get("realMeasurement") is not True:
            problems.append(issue(
                "E_SUMMARY_REAL_MEASUREMENT",
                "Summary JSON is not marked as a real measurement.",
                expected=True,
                observed=summary.get("realMeasurement"),
            ))
        if str(summary.get("status", "")).lower() != "complete_valid":
            problems.append(issue(
                "E_SUMMARY_STATUS",
                "Summary status must be complete_valid.",
                expected="complete_valid",
                observed=summary.get("status"),
            ))

    if _to_int(summary.get("gpuTimingUnresolvedFrameCount"), -1) != 0:
        problems.append(issue(
            "E_GPU_QUERY_UNRESOLVED",
            "Unresolved GPU timestamp queries must be zero.",
            expected=0,
            observed=summary.get("gpuTimingUnresolvedFrameCount"),
        ))
    return problems


def _powershell_host_contract() -> dict[str, Any]:
    if sys.platform != "win32":
        return {}
    script = r"""
$ErrorActionPreference='SilentlyContinue'
$cores=(Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfCores -Sum).Sum
$batteries=@(Get-CimInstance Win32_Battery)
if($batteries.Count -eq 0){$ac=$true}else{
  $ac=(@($batteries | Where-Object {$_.BatteryStatus -in @(2,6,7,8,9,11)}).Count -gt 0)
}
$dvr=(Get-ItemProperty 'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\GameDVR' -ErrorAction SilentlyContinue).AppCaptureEnabled
$game=(Get-ItemProperty 'HKCU:\System\GameConfigStore' -ErrorAction SilentlyContinue).GameDVR_Enabled
$captureDisabled=(($null -eq $dvr -or [int]$dvr -eq 0) -and ($null -eq $game -or [int]$game -eq 0))
[pscustomobject]@{
  physicalCpuCoreCount=[int]$cores
  acPower=[bool]$ac
  backgroundCaptureDisabled=[bool]$captureDisabled
} | ConvertTo-Json -Compress
"""
    try:
        run = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-Command", script],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        if run.returncode == 0 and run.stdout.strip():
            payload = json.loads(run.stdout.strip().splitlines()[-1])
            return payload if isinstance(payload, dict) else {}
    except Exception:
        pass
    return {}


def _nvidia_gpu_health() -> dict[str, float]:
    try:
        run = subprocess.run(
            [
                "nvidia-smi",
                "--query-gpu=temperature.gpu,clocks.current.graphics",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=8,
            check=False,
        )
        if run.returncode == 0 and run.stdout.strip():
            fields = [item.strip() for item in run.stdout.splitlines()[0].split(",")]
            if len(fields) >= 2:
                result: dict[str, float] = {}
                temperature = _to_float(fields[0])
                clock = _to_float(fields[1])
                if temperature is not None:
                    result["gpuTemperatureC"] = temperature
                if clock is not None:
                    result["gpuGraphicsClockMHz"] = clock
                return result
    except Exception:
        pass
    return {}


def _validate_hardware_contract(
    case: Mapping[str, Any],
    environment: Mapping[str, Any],
    process_summary: Mapping[str, Any],
) -> list[dict[str, Any]]:
    if not bool(case.get("hardware_profile_strict", False)):
        return []

    profile = case.get("hardware_profile_contract")
    if not isinstance(profile, dict):
        return [issue(
            "E_HARDWARE_PROFILE_MISSING",
            "Strict hardware profile contract is unavailable.",
        )]

    problems: list[dict[str, Any]] = []
    target = profile.get("target", {}) if isinstance(profile.get("target"), dict) else {}
    contract = (
        profile.get("executionContract", {})
        if isinstance(profile.get("executionContract"), dict)
        else {}
    )
    pre = environment.get("pre", {}) if isinstance(environment.get("pre"), dict) else {}
    post = environment.get("post", {}) if isinstance(environment.get("post"), dict) else {}
    pre_gpu = pre.get("gpu", {}) if isinstance(pre.get("gpu"), dict) else {}
    post_gpu = post.get("gpu", {}) if isinstance(post.get("gpu"), dict) else {}
    pre_power = pre.get("power", {}) if isinstance(pre.get("power"), dict) else {}

    name_regex = str(target.get("gpuNameRegex", ""))
    gpu_name = str(pre_gpu.get("name", ""))
    if name_regex and re.search(name_regex, gpu_name, re.IGNORECASE) is None:
        problems.append(issue(
            "E_HARDWARE_GPU_NAME",
            "GPU does not match the formal profile.",
            expected=name_regex,
            observed=gpu_name,
        ))

    vram = _to_float(pre_gpu.get("memoryTotalMiB"))
    minimum_vram = _to_float(target.get("minimumDedicatedVramMiB"))
    maximum_vram = _to_float(target.get("maximumDedicatedVramMiB"))
    if minimum_vram is not None and (vram is None or vram < minimum_vram):
        problems.append(issue("E_HARDWARE_VRAM_MIN", "Dedicated VRAM is below the profile minimum.", expected=minimum_vram, observed=vram))
    if maximum_vram is not None and (vram is None or vram > maximum_vram):
        problems.append(issue("E_HARDWARE_VRAM_MAX", "Dedicated VRAM exceeds the profile maximum.", expected=maximum_vram, observed=vram))

    cores = _to_int(pre.get("physicalCpuCoreCount"), -1)
    minimum_cores = _to_int(target.get("minimumCpuPhysicalCoreCount"), 0)
    if minimum_cores > 0 and cores < minimum_cores:
        problems.append(issue("E_HARDWARE_CPU_CORES", "Physical CPU core count is below the profile minimum.", expected=minimum_cores, observed=cores))

    if bool(contract.get("acPowerRequired", False)) and pre_power.get("acPower") is not True:
        problems.append(issue("E_HARDWARE_AC_POWER", "AC power is required.", expected=True, observed=pre_power.get("acPower")))

    if bool(contract.get("backgroundCaptureDisabled", False)) and pre.get("backgroundCaptureDisabled") is not True:
        problems.append(issue("E_HARDWARE_CAPTURE", "Windows background capture must be disabled.", expected=True, observed=pre.get("backgroundCaptureDisabled")))

    scheme_regex = str(contract.get("requiredPowerSchemeRegex", ""))
    scheme = str(pre_power.get("activeScheme", ""))
    if scheme_regex and re.search(scheme_regex, scheme, re.IGNORECASE) is None:
        problems.append(issue("E_HARDWARE_POWER_SCHEME", "Power scheme does not match the formal profile.", expected=scheme_regex, observed=scheme))

    temperature_limit = _to_float(contract.get("maximumGpuTemperatureCelsius"))
    temperature_max = _to_float(process_summary.get("gpuTemperatureMaxC"))
    if temperature_limit is not None and (temperature_max is None or temperature_max > temperature_limit):
        problems.append(issue("E_HARDWARE_GPU_TEMPERATURE", "GPU temperature exceeded the limit or was unavailable.", expected=temperature_limit, observed=temperature_max))

    spread_limit = _to_float(contract.get("maximumGpuTemperatureSpreadCelsius"))
    spread = _to_float(process_summary.get("gpuTemperatureSpreadC"))
    if spread_limit is not None and (spread is None or spread > spread_limit):
        problems.append(issue("E_HARDWARE_GPU_TEMPERATURE_SPREAD", "GPU temperature spread exceeded the limit or was unavailable.", expected=spread_limit, observed=spread))

    clock_drop_limit = _to_float(contract.get("maximumGpuClockDropRatio"))
    clock_drop = _to_float(process_summary.get("gpuClockDropRatio"))
    if clock_drop_limit is not None and (clock_drop is None or clock_drop > clock_drop_limit):
        problems.append(issue("E_HARDWARE_GPU_CLOCK_DROP", "GPU clock drop exceeded the limit or was unavailable.", expected=clock_drop_limit, observed=clock_drop))

    minimum_samples = _to_int(contract.get("minimumGpuClockSamples"), 0)
    observed_samples = _to_int(process_summary.get("gpuHealthSampleCount"), 0)
    if observed_samples < minimum_samples:
        problems.append(issue("E_HARDWARE_GPU_CLOCK_SAMPLES", "Too few GPU health samples.", expected=minimum_samples, observed=observed_samples))

    for phase, gpu in (("pre", pre_gpu), ("post", post_gpu)):
        endpoint = _to_float(gpu.get("temperatureC"))
        if temperature_limit is not None and (endpoint is None or endpoint > temperature_limit):
            problems.append(issue(
                "E_HARDWARE_GPU_TEMPERATURE_ENDPOINT",
                f"GPU {phase}-run temperature is invalid.",
                expected=temperature_limit,
                observed=endpoint,
            ))
    return problems


def validate_outputs(case: Mapping[str, Any], paths: Mapping[str, Path],
                     process_summary: Mapping[str, Any],
                     environment_summary: Mapping[str, Any] | None = None) -> dict[str, Any]:
    problems: list[dict[str, Any]] = []
    for key in ("frames", "jsonl", "settings", "summary"):
        if not paths[key].is_file():
            problems.append(issue("E_OUTPUT_MISSING", f"Required output is missing: {key}", observed=str(paths[key])))
    if problems:
        return {"validationVersion": VALIDATION_VERSION, "valid": False, "qualityGatePassed": False, "issues": problems, "aggregate": {}, "processMemory": dict(process_summary)}
    try:
        csv_rows = read_csv_rows(paths["frames"]); json_rows = read_jsonl(paths["jsonl"])
    except Exception as exc:
        problems.append(issue("E_OUTPUT_PARSE", "Failed to parse telemetry output.", observed=str(exc)))
        return {"validationVersion": VALIDATION_VERSION, "valid": False, "qualityGatePassed": False, "issues": problems, "aggregate": {}, "processMemory": dict(process_summary)}
    try:
        settings_payload = read_json(paths["settings"])
        summary_payload = read_json(paths["summary"])
    except Exception as exc:
        problems.append(issue(
            "E_SETTINGS_PARSE",
            "Failed to parse Settings/Summary JSON.",
            observed=str(exc),
        ))
        settings_payload = {}
        summary_payload = {}
    problems.extend(_validate_settings_contract(case, settings_payload, summary_payload))
    problems.extend(_validate_hardware_contract(
        case,
        environment_summary or {},
        process_summary,
    ))
    expected_frames = _to_int(case.get("measurement_frames"), 0)
    if len(csv_rows) != expected_frames or len(json_rows) != expected_frames:
        problems.append(issue("E_FRAME_COUNT", "Telemetry row count mismatch.", expected=expected_frames, observed={"csv": len(csv_rows), "jsonl": len(json_rows)}))
    required_fields = set(REQUIRED_FRAME_COLUMNS) | {str(item) for item in case.get("required_telemetry_fields", [])}
    minimum_coverage = float(case.get("minimum_metric_coverage", 0.99))
    for field in sorted(required_fields):
        csv_coverage = _field_coverage(csv_rows, field); json_coverage = _field_coverage(json_rows, field)
        if csv_coverage < minimum_coverage or json_coverage < minimum_coverage:
            problems.append(issue("E_TELEMETRY_FIELD_COVERAGE", f"Required telemetry field coverage is too low: {field}", expected=minimum_coverage, observed={"csv": csv_coverage, "jsonl": json_coverage}))
    source_indices = [_to_int(row.get("sourceFrameIndex"), -1) for row in json_rows]
    if source_indices:
        expected_indices = list(range(source_indices[0], source_indices[0] + len(source_indices)))
        if source_indices != expected_indices:
            problems.append(issue("E_SOURCE_FRAME_GAP", "sourceFrameIndex must be contiguous.", observed=source_indices[:20]))
    parity_fields = sorted(required_fields | {"sourceFrameIndex"})
    if len(csv_rows) == len(json_rows):
        for index, (csv_row, json_row) in enumerate(zip(csv_rows, json_rows)):
            for field in parity_fields:
                left = str(csv_row.get(field, "")); right = str(json_row.get(field, ""))
                left_num = _to_float(left); right_num = _to_float(right)
                equal = abs(left_num - right_num) <= 1.0e-6 if left_num is not None and right_num is not None else left == right
                if not equal:
                    problems.append(issue("E_CSV_JSONL_PARITY", f"CSV/JSONL mismatch at row {index}, field {field}.", expected=left, observed=right)); break
            if problems and problems[-1].get("code") == "E_CSV_JSONL_PARITY":
                break
    required_mask = 0
    for name in case.get("required_availability", []):
        if name not in AVAILABILITY_BITS:
            problems.append(issue("E_AVAILABILITY_NAME", "Unknown availability name.", observed=name))
        else:
            required_mask |= AVAILABILITY_BITS[name]
    frame_ms: list[float] = []; gpu_ms: list[float] = []
    for index, row in enumerate(json_rows):
        if _to_int(row.get("totalStrandCount"), -1) != 100000:
            problems.append(issue("E_FRAME_WORKLOAD_STRANDS", f"Frame {index} did not contain 100,000 strands.", observed=row.get("totalStrandCount"))); break
        for field, code in (
            ("syntheticMetricCount", "E_SYNTHETIC_METRIC"),
            ("sameFrameReadbackCount", "E_SAME_FRAME_READBACK"),
            ("nanStateCount", "E_NAN_STATE"), ("infiniteStateCount", "E_INFINITE_STATE"),
            ("fatalFailure", "E_FATAL_RUNTIME"),
        ):
            if _to_int(row.get(field), 0) != 0:
                problems.append(issue(code, f"{field} must be zero at frame {index}.", observed=row.get(field))); break
        if required_mask:
            observed_mask = _to_int(row.get("availabilityMask"), 0) | _to_int(row.get("metricAvailabilityMask"), 0)
            if observed_mask & required_mask != required_mask:
                problems.append(issue("E_AVAILABILITY_MASK", f"Required evidence unavailable at frame {index}.", expected=required_mask, observed=observed_mask)); break
        value = _to_float(row.get("frameMilliseconds"))
        if value is not None: frame_ms.append(value)
        value = _to_float(row.get("gpuHairMilliseconds"))
        if value is not None: gpu_ms.append(value)
    average_frame = sum(frame_ms) / len(frame_ms) if frame_ms else None
    average_fps = 1000.0 / average_frame if average_frame and average_frame > 0 else None
    sixty_ratio = sum(value <= FRAME_BUDGET_MS for value in frame_ms) / len(frame_ms) if frame_ms else 0.0
    overrun_rate = sum(value > FRAME_BUDGET_MS for value in frame_ms) / len(frame_ms) if frame_ms else 1.0
    gpu_coverage = len(gpu_ms) / len(json_rows) if json_rows else 0.0
    aggregate = {
        "frameCount": len(frame_ms), "averageFrameMilliseconds": average_frame,
        "averageFps": average_fps, "sixtyFpsRatio": sixty_ratio,
        "frameBudgetOverrunRate": overrun_rate,
        "p95FrameMilliseconds": percentile(frame_ms, 95.0),
        "p99FrameMilliseconds": percentile(frame_ms, 99.0),
        "maximumFrameMilliseconds": max(frame_ms) if frame_ms else None,
        "framesOver100Milliseconds": sum(value > 100.0 for value in frame_ms),
        "gpuTimingCoverage": gpu_coverage,
    }
    quality_issues: list[dict[str, Any]] = []
    gates = (
        ("averageFps", ">=", float(case.get("minimum_average_fps", 60.0))),
        ("sixtyFpsRatio", ">=", float(case.get("minimum_sixty_fps_ratio", 0.95))),
        ("frameBudgetOverrunRate", "<=", float(case.get("maximum_frame_budget_overrun_rate", 0.05))),
        ("p95FrameMilliseconds", "<=", float(case.get("maximum_p95_frame_ms", FRAME_BUDGET_MS))),
        ("p99FrameMilliseconds", "<=", float(case.get("maximum_p99_frame_ms", 20.0))),
        ("maximumFrameMilliseconds", "<=", float(case.get("maximum_single_frame_ms", 50.0))),
        ("framesOver100Milliseconds", "<=", float(case.get("maximum_frames_over_100ms", 0))),
        ("gpuTimingCoverage", ">=", float(case.get("minimum_gpu_timing_coverage", 0.99))),
    )
    for field, operator, threshold in gates:
        observed = aggregate.get(field)
        passed = observed is not None and (float(observed) >= threshold if operator == ">=" else float(observed) <= threshold)
        if not passed:
            quality_issues.append(issue("Q_" + field.upper(), f"Quality gate failed: {field} {operator} {threshold}", expected=threshold, observed=observed, severity="quality"))
    sample_coverage = _to_float(process_summary.get("sampleCoverage"))
    if sample_coverage is None or sample_coverage < float(case.get("minimum_process_memory_sample_coverage", 0.95)):
        problems.append(issue("E_PROCESS_MEMORY_COVERAGE", "Process memory sample coverage is too low.", expected=case.get("minimum_process_memory_sample_coverage"), observed=sample_coverage))
    peak_private = _to_float(process_summary.get("peakProcessPrivateMiB"))
    if peak_private is None or peak_private > float(case.get("maximum_process_private_memory_mib", 32768.0)):
        problems.append(issue("E_PROCESS_PRIVATE_MEMORY", "HairSystem process private memory exceeded the limit or was unavailable.", expected=case.get("maximum_process_private_memory_mib"), observed=peak_private))
    peak_local = _to_float(process_summary.get("peakDxgiLocalUsageMiB"))
    if peak_local is None:
        problems.append(issue("E_PROCESS_GPU_MEMORY_UNAVAILABLE", "HairSystem process GPU-memory measurement is unavailable.", observed=process_summary.get("gpuProbe")))
    elif peak_local > float(case.get("maximum_dxgi_local_usage_mib", 6144.0)):
        problems.append(issue("E_PROCESS_GPU_MEMORY", "HairSystem process local GPU memory exceeded the limit.", expected=case.get("maximum_dxgi_local_usage_mib"), observed=peak_local))
    all_issues = problems + quality_issues
    valid = not problems
    return {
        "validationVersion": VALIDATION_VERSION,
        "valid": valid,
        "qualityGatePassed": valid and not quality_issues,
        "issues": all_issues,
        "aggregate": aggregate,
        "processMemory": dict(process_summary),
    }


def _artifact_manifest(paths: Mapping[str, Path], case: Mapping[str, Any], fingerprint: str) -> dict[str, Any]:
    artifacts: list[dict[str, Any]] = []
    for key in ("frames", "jsonl", "settings", "summary", "environment", "process_samples", "validation", "stdout", "stderr", "case"):
        path = paths[key]
        if path.is_file():
            artifacts.append({"kind": key, "path": path.name, "sizeBytes": path.stat().st_size, "sha256": sha256_file(path)})
    payload = {
        "manifestVersion": "hairgpu-artifacts-v3.6",
        "runName": case["run_name"], "executionFingerprint": fingerprint,
        "createdUtc": utc_now(), "artifacts": artifacts,
    }
    payload["manifestSha256"] = canonical_json_sha256(payload)
    return payload


def execute_case(case: Mapping[str, Any], viewer_exe: Path, output_dir: Path,
                 repo_root: Path, dry_run: bool = False) -> dict[str, Any]:
    output_dir = Path(output_dir).resolve(); output_dir.mkdir(parents=True, exist_ok=True)
    viewer_exe = Path(viewer_exe).resolve(); repo_root = Path(repo_root).resolve()
    normalized = materialize_repeated_timeline(normalize_case(case), output_dir, repo_root)
    paths = output_paths(output_dir, str(normalized["run_name"]))
    timeline_path = resolve_path(normalized.get("scenario_timeline"), repo_root)
    asset_manifest_path = resolve_path(os.environ.get("HAIRGPU_FROZEN_ASSET_MANIFEST") or normalized.get("asset_manifest"), repo_root)
    problems: list[dict[str, Any]] = []
    if not viewer_exe.is_file():
        problems.append(issue("E_VIEWER_MISSING", "Viewer executable is missing.", observed=str(viewer_exe)))
    if timeline_path is None or not timeline_path.is_file():
        problems.append(issue("E_TIMELINE_MISSING", "Scenario timeline is missing.", observed=str(timeline_path)))
    else:
        problems.extend(validate_timeline(timeline_path, normalized))
    asset_manifest: dict[str, Any] = {}
    if asset_manifest_path is None or not asset_manifest_path.is_file():
        problems.append(issue("E_ASSET_MANIFEST_MISSING", "Frozen asset manifest is missing.", observed=str(asset_manifest_path)))
    else:
        asset_manifest, asset_problems = validate_asset_manifest(asset_manifest_path, repo_root)
        problems.extend(asset_problems)
        if not asset_problems:
            # Dry-run also validates binary compatibility, but the cache ensures
            # a full suite process performs this expensive check only once.
            problems.extend(validate_frozen_asset_with_viewer(viewer_exe, asset_manifest_path))
    if normalized.get("method") not in FORMAL_METHOD_CODES:
        problems.append(issue("E_METHOD", "Unknown formal method.", observed=normalized.get("method")))
    if problems:
        raise FormalRunError("Formal preflight failed.", EXIT_FORMAL_CONTRACT_INVALID, problems)
    normalized["scenario_timeline"] = str(timeline_path)
    normalized["asset_manifest"] = str(asset_manifest_path)
    executable_hash = sha256_file(viewer_exe)
    shader_root = resolve_path(normalized.get("shader_root"), repo_root)
    fingerprint = canonical_json_sha256({
        "validationVersion": VALIDATION_VERSION,
        "case": normalized,
        "viewerSha256": executable_hash,
        "timelineSha256": sha256_file(timeline_path),
        "assetManifestSha256": sha256_file(asset_manifest_path),
        "assetSha256": asset_manifest.get("sourceAssetSha256"),
        "shaderBundleSha256": sha256_tree([shader_root], base_path=repo_root) if shader_root and shader_root.exists() else "",
        "runtimeCommitSha": git_value(repo_root, "rev-parse", "HEAD"),
    })
    preflight_payload = {
        "executionFingerprint": fingerprint, "case": normalized,
        "asset": asset_manifest, "viewerSha256": executable_hash,
    }
    if dry_run:
        return preflight_payload
    write_json(paths["case"], normalized)
    pre_asset_hashes = {
        key: sha256_file(Path(value)) for key, value in asset_manifest.get("_resolvedMembers", {}).items()
        if Path(value).is_file()
    }
    environment = _build_environment(normalized, paths, repo_root, viewer_exe, asset_manifest)
    sampler = ProcessSampler(float(normalized.get("hardware_sample_interval_seconds", 1.0)))
    with RunLease(paths, normalized["run_name"], fingerprint) as lease:
        try:
            pre_hardware = collect_hardware_probe(str(normalized.get("hardware_profile", "")), "pre")
            return_code, stdout, stderr, timed_out = run_process(
                [str(viewer_exe), "--formal-run"], cwd=viewer_exe.parent,
                environment=environment, timeout_seconds=_to_int(normalized.get("timeout_seconds"), 900),
                on_started=sampler.attach_process,
            )
            sampler.stop()
            atomic_write_bytes(paths["stdout"], stdout.encode("utf-8", errors="replace"))
            atomic_write_bytes(paths["stderr"], stderr.encode("utf-8", errors="replace"))
            _write_jsonl(paths["process_samples"], sampler.samples)
            expected_duration = (_to_int(normalized.get("warmup_frames")) + _to_int(normalized.get("measurement_frames"))) * float(normalized.get("fixed_timestep_seconds", 1.0 / 60.0))
            process_summary = sampler.summary(expected_duration)
            post_hardware = collect_hardware_probe(str(normalized.get("hardware_profile", "")), "post")
            environment_payload = {
                "pre": pre_hardware, "post": post_hardware,
                "process": process_summary, "runtimeReturnCode": return_code,
                "timedOut": timed_out, "runtimeCommitSha": git_value(repo_root, "rev-parse", "HEAD"),
                "assetSourceCommitSha": asset_manifest.get("assetSourceCommitSha", asset_manifest.get("sourceCommitSha")),
            }
            write_json(paths["environment"], environment_payload)
            if timed_out:
                raise FormalRunError("Viewer timed out.", EXIT_RUNTIME_FAILURE, [issue("E_VIEWER_TIMEOUT", "Viewer timed out.")])
            if return_code != 0:
                raise FormalRunError("Viewer returned a non-zero exit code.", EXIT_RUNTIME_FAILURE, [issue("E_VIEWER_EXIT", "Viewer returned a non-zero exit code.", observed=return_code)])
            for key, before in pre_asset_hashes.items():
                value = asset_manifest.get("_resolvedMembers", {}).get(key)
                if value and sha256_file(Path(value)) != before:
                    raise FormalRunError("Frozen asset bundle changed during execution.", EXIT_OUTPUT_INTEGRITY_FAILURE, [issue("E_ASSET_POST_RUN_HASH", "Frozen asset changed during execution.", observed=key)])
            validation = validate_outputs(normalized, paths, process_summary, environment_payload)
            write_json(paths["validation"], validation)
            if not validation["valid"]:
                raise FormalRunError("Output integrity validation failed.", EXIT_OUTPUT_INTEGRITY_FAILURE, validation.get("issues", []))
            write_json(paths["manifest"], _artifact_manifest(paths, normalized, fingerprint))
            complete = {
                "state": "COMPLETE", "runName": normalized["run_name"],
                "executionFingerprint": fingerprint, "completedUtc": utc_now(),
                "valid": True, "qualityGatePassed": validation["qualityGatePassed"],
            }
            write_json(paths["complete"], complete)
            paths["error"].unlink(missing_ok=True)
            lease.mark_completed()
            return {**validation, "executionFingerprint": fingerprint}
        except BaseException as exc:
            sampler.stop()
            error_payload = {
                "state": "ERROR", "runName": normalized["run_name"], "endedUtc": utc_now(),
                "executionFingerprint": fingerprint, "exceptionType": type(exc).__name__,
                "message": str(exc), "exitCode": getattr(exc, "exit_code", EXIT_RUNTIME_FAILURE),
                "issues": getattr(exc, "issues", []),
            }
            write_json(paths["error"], error_payload)
            raise


def _self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="hairgpu_final_run_selftest_") as temporary:
        root = Path(temporary); paths = output_paths(root, "lock_test")
        try:
            with RunLease(paths, "lock_test", "f" * 64):
                raise RuntimeError("injected")
        except RuntimeError:
            pass
        if paths["lock"].exists() or paths["running"].exists() or not paths["incomplete"].is_file():
            print("self-test failed: lock cleanup")
            return 1
        write_json(paths["lock"], {"pid": 99999999, "runName": "lock_test"})
        write_json(paths["running"], {"pid": 99999999})
        recover_stale_run_lock(paths)
        if paths["lock"].exists() or not paths["incomplete"].is_file():
            print("self-test failed: stale lock recovery")
            return 1

        rows = [{"sourceFrameIndex": 10}, {"sourceFrameIndex": 11}, {"sourceFrameIndex": 12}]
        if _field_coverage(rows, "sourceFrameIndex") != 1.0:
            print("self-test failed: telemetry coverage")
            return 1

        digest = "a" * 64
        commit = "b" * 40
        flags = {"enableGpuResident": True, "stateStorage": "IntegratedRecord"}
        case = {
            "case_id": "settings_test",
            "method": "P",
            "scenario": "S01_Idle",
            "seed": 1103,
            "repetition_index": 0,
            "warmup_frames": 600,
            "measurement_frames": 6000,
            "fixed_timestep_seconds": 1.0 / 60.0,
            "width": 1920,
            "height": 1080,
            "display_strand_count": 100000,
            "guide_strand_count": 4000,
            "particles_per_guide": 128,
            "solver_iterations": 6,
            "vsync": False,
            "present_sync_interval": 0,
            "real_measurement_required": True,
        }
        settings = {
            "caseId": "settings_test",
            "requestedMethodCode": "P",
            "appliedMethodCode": "P",
            "scenario": "S01_Idle",
            "seed": 1103,
            "repetitionIndex": 0,
            "warmupFrames": 600,
            "measurementFrames": 6000,
            "fixedTimeStepSeconds": 1.0 / 60.0,
            "settingsSha256": digest,
            "commitSha": commit,
            "executableSha256": digest,
            "shaderBundleSha256": digest,
            "assetSha256": digest,
            "assetManifestSha256": digest,
            "scenarioTimelineSha256": digest,
            "requestedMethodSettingsSha256": digest,
            "appliedMethodSettingsSha256": digest,
            "observedMethodSettingsSha256": digest,
            "methodFlagsMatched": True,
            "realMeasurement": True,
            "requested": flags,
            "applied": flags,
            "observedFlags": flags,
            "observed": {
                "adapterType": "hardware",
                "renderWidth": 1920,
                "renderHeight": 1080,
                "displayStrandCount": 100000,
                "guideCount": 4000,
                "particlesPerGuide": 128,
                "solverIterations": 6,
                "vsync": False,
                "presentSyncInterval": 0,
            },
        }
        summary = {
            key: settings[key]
            for key in (
                "requestedMethodCode",
                "appliedMethodCode",
                "settingsSha256",
                "commitSha",
                "executableSha256",
                "shaderBundleSha256",
                "assetSha256",
                "assetManifestSha256",
                "scenarioTimelineSha256",
                "requestedMethodSettingsSha256",
                "appliedMethodSettingsSha256",
                "observedMethodSettingsSha256",
            )
        }
        summary.update({
            "status": "complete_valid",
            "realMeasurement": True,
            "actualRenderWidth": 1920,
            "actualRenderHeight": 1080,
            "actualDisplayStrandCount": 100000,
            "gpuTimingUnresolvedFrameCount": 0,
        })
        if _validate_settings_contract(case, settings, summary):
            print("self-test failed: valid Settings/Summary contract")
            return 1
        bad_summary = dict(summary)
        bad_summary["gpuTimingUnresolvedFrameCount"] = 1
        if not any(
            item.get("code") == "E_GPU_QUERY_UNRESOLVED"
            for item in _validate_settings_contract(case, settings, bad_summary)
        ):
            print("self-test failed: unresolved GPU query gate")
            return 1

    print("hairgpu_final_run self-test: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Run one strict HairSystem formal case.")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--case")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--output")
    parser.add_argument("--repo-root")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return _self_test()
    if not args.case or not args.viewer_exe or not args.output:
        parser.error("--case, --viewer-exe and --output are required")
    repo_root = Path(args.repo_root).resolve() if args.repo_root else Path(__file__).resolve().parents[2]
    try:
        result = execute_case(read_json(Path(args.case)), Path(args.viewer_exe), Path(args.output), repo_root, dry_run=args.dry_run)
        print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))
        if args.dry_run:
            return EXIT_COMPLETE_VALID
        return EXIT_COMPLETE_VALID if result.get("qualityGatePassed") else EXIT_QUALITY_GATE_FAILED
    except FormalRunError as exc:
        print(json.dumps({"error": str(exc), "exitCode": exc.exit_code, "issues": exc.issues}, ensure_ascii=False, indent=2), file=sys.stderr)
        return exc.exit_code
    except Exception as exc:
        print(json.dumps({"error": str(exc), "exitCode": EXIT_CONFIGURATION_FAILURE}, ensure_ascii=False, indent=2), file=sys.stderr)
        return EXIT_CONFIGURATION_FAILURE


if __name__ == "__main__":
    raise SystemExit(main())
