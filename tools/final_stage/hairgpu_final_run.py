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

# Existing-source-only formal runtime runner for 01-Safe v3.1.
# This module intentionally keeps the public normalize_method_code() and
# run_viewer() entry points used by hairgpu_runtime_suite.py.

EXIT_COMPLETE_VALID = 0
EXIT_QUALITY_GATE_FAILED = 1
EXIT_FORMAL_CONTRACT_INVALID = 2
EXIT_RUNTIME_FAILURE = 3
EXIT_OUTPUT_INTEGRITY_FAILURE = 4
EXIT_CONFIGURATION_FAILURE = 5

FORMAL_METHOD_CODES = {
    "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "P"
}

METHOD_ALIASES = {
    "b0": "B0", "b0_fulldynamic": "B0", "full_dynamic": "B0", "full-physics": "B0", "full_physics": "B0",
    "b1": "B1", "b1_strandsleep": "B1", "strand_sleep": "B1",
    "b2": "B2", "b2_blocksleep": "B2", "block_sleep": "B2",
    "b3": "B3", "b3_segmentthresholdsleep": "B3", "segment_threshold_sleep": "B3",
    "b4": "B4", "b4_segmenthysteresissleep": "B4", "segment_hysteresis_sleep": "B4",
    "b5": "B5", "b5_segmentcache": "B5", "segment_cache": "B5",
    "b6": "B6", "b6_segmentgpucompaction": "B6", "segment_gpu_compaction": "B6",
    "b7": "B7", "b7_activesetonly": "B7", "active_set_only": "B7", "active-set-runtime": "B7", "active_set_runtime": "B7",
    "b8": "B8", "b8_externalstatearray": "B8", "external_state_array": "B8",
    "p": "P", "p_integratedsegmentgroup": "P", "proposed_integrated_segment_group": "P", "proposal-hybrid": "P", "proposal_hybrid": "P",
    "baseline_currentproject": "B0", "baseline_current_project": "B0",
    "proposed_fullsystem": "P", "proposed_full_system": "P",
    "prediction_only": "B3", "prediction-only": "B3",
}
for _mask in range(32):
    _name = f"Combo_{_mask:05b}"
    METHOD_ALIASES[_name] = _name
    METHOD_ALIASES[_name.lower()] = _name


def normalize_method_code(method_code: str) -> str:
    raw = str(method_code or "").strip()
    if raw in FORMAL_METHOD_CODES:
        return raw
    return METHOD_ALIASES.get(raw, METHOD_ALIASES.get(raw.lower(), raw))


# P and B8 deliberately share all behavior settings except state storage.
# This keeps the representation comparison scientifically meaningful.
_BASE_SAFE_FLAGS = {
    "enableNoNoveltyBaseline": False,
    "enableMicrotype": True,
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
        enableNoNoveltyBaseline=True,
        enableMicrotype=False,
        enableSegmentGroupSleep=False,
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False,
        enableDirectionalResponsibility=False,
        enableStaticCertificate=False,
        enablePredictiveWake=False,
        enableVelocityRecovery=False,
        enableRepair=False,
        enableGpuCompaction=False,
        enableActiveSet=False,
        enableSleepHysteresis=False,
        enableStaticCache=False,
        forceAllDynamic=True,
    ),
    "B1": _preset(
        enableMicrotype=False,
        enableSegmentGroupIntegratedState=False,
        enableStrandSleep=True,
        enableStaticCertificate=False,
        enablePredictiveWake=False,
        enableVelocityRecovery=False,
        enableRepair=False,
        enableGpuCompaction=False,
        enableActiveSet=False,
    ),
    "B2": _preset(
        enableMicrotype=False,
        enableSegmentGroupIntegratedState=False,
        enableBlockSleep=True,
        enableStaticCertificate=False,
        enablePredictiveWake=False,
        enableVelocityRecovery=False,
        enableRepair=False,
        enableGpuCompaction=False,
        enableActiveSet=False,
        groupsPerBlock=8,
    ),
    "B3": _preset(
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False,
        enableDirectionalResponsibility=False,
        enableStaticCertificate=False,
        enablePredictiveWake=False,
        enableVelocityRecovery=False,
        enableRepair=False,
        enableGpuCompaction=False,
        enableActiveSet=False,
        enableSleepHysteresis=False,
        enableStaticCache=False,
    ),
    "B4": _preset(
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False,
        enableDirectionalResponsibility=False,
        enableStaticCertificate=False,
        enablePredictiveWake=False,
        enableVelocityRecovery=False,
        enableRepair=False,
        enableGpuCompaction=False,
        enableActiveSet=False,
        enableStaticCache=False,
    ),
    "B5": _preset(
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False,
        enableDirectionalResponsibility=False,
        enableStaticCertificate=False,
        enablePredictiveWake=False,
        enableVelocityRecovery=False,
        enableRepair=False,
        enableGpuCompaction=False,
        enableActiveSet=False,
    ),
    "B6": _preset(
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False,
        enableDirectionalResponsibility=False,
        enableStaticCertificate=False,
        enablePredictiveWake=False,
        enableVelocityRecovery=False,
        enableRepair=False,
        enableActiveSet=False,
    ),
    "B7": _preset(
        enableMicrotype=False,
        enableSegmentGroupSleep=False,
        enableSegmentGroupIntegratedState=False,
        enableSharedPointOwnership=False,
        enableDirectionalResponsibility=False,
        enableStaticCertificate=False,
        enablePredictiveWake=False,
        enableVelocityRecovery=False,
        enableRepair=False,
        enableGpuCompaction=False,
        enableActiveSet=True,
        enableStaticCache=False,
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


class FormalRunError(RuntimeError):
    def __init__(self, message: str, exit_code: int, issues: list[dict[str, Any]] | None = None):
        super().__init__(message)
        self.exit_code = int(exit_code)
        self.issues = list(issues or [])


def _issue(code: str, message: str, *, expected: Any = None, observed: Any = None, severity: str = "error") -> dict[str, Any]:
    return {
        "severity": severity,
        "code": code,
        "message": message,
        "expected": expected,
        "observed": observed,
    }


def wait_for_file(path: Path, timeout_seconds: int) -> None:
    deadline = time.time() + max(timeout_seconds, 1)
    while time.time() < deadline:
        if path.exists() and path.is_file() and path.stat().st_size > 0:
            return
        time.sleep(0.25)
    raise FormalRunError(f"Timed out waiting for output: {path}", EXIT_OUTPUT_INTEGRITY_FAILURE)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_tree(paths: Iterable[Path]) -> str:
    digest = hashlib.sha256()
    files: list[Path] = []
    for root in paths:
        root = Path(root)
        if root.is_file():
            files.append(root)
        elif root.exists():
            files.extend(item for item in root.rglob("*") if item.is_file())
    for path in sorted(files, key=lambda item: item.as_posix().lower()):
        digest.update(path.as_posix().encode("utf-8"))
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
        if temporary.exists():
            temporary.unlink(missing_ok=True)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    encoded = (json.dumps(payload, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
    _atomic_write_bytes(path, encoded)


def _write_text(path: Path, text: str) -> None:
    _atomic_write_bytes(Path(path), text.encode("utf-8"))


def _read_json(path: Path) -> dict[str, Any]:
    value = json.loads(Path(path).read_text(encoding="utf-8-sig"))
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
    return max(len(rows) - 1, 0)


def _metric(row: dict[str, Any], *keys: str, default: Any = None) -> Any:
    for key in keys:
        if key in row and row[key] is not None:
            return row[key]
    return default


def _to_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _to_float(value: Any, default: float = 0.0) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return default
    return result if math.isfinite(result) else default


def _as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def _is_finite_number(value: Any) -> bool:
    if value in (None, "") or isinstance(value, bool):
        return True
    if isinstance(value, (int, float)):
        return math.isfinite(float(value))
    return True


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


def _canonical_summary(summary: dict[str, Any]) -> dict[str, Any]:
    result = dict(summary)
    for key in ("canonicalResultSha256", "validationReasons", "formalAggregateMetrics", "resultManifestSha256"):
        result.pop(key, None)
    return result


def _resolve_case_path(value: Any) -> Path | None:
    if value in (None, ""):
        return None
    path = Path(str(value))
    return path if path.is_absolute() else path.resolve()


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
    result.setdefault("fixed_timestep", True)
    result.setdefault("fixed_timestep_seconds", 1.0 / 60.0)
    result.setdefault("warmup_frames", 600)
    result.setdefault("measurement_frames", 6000)
    result.setdefault("guide_strand_count", 3000)
    result.setdefault("display_strand_count", 100000)
    result.setdefault("particles_per_guide", 24)
    result.setdefault("solver_iterations", 6)
    result.setdefault("minimum_gpu_timing_coverage", 0.99)
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
    return result


def validate_case_contract(case: dict[str, Any], viewer_exe: Path | None = None) -> dict[str, Any]:
    normalized = _normalize_case(case)
    issues: list[dict[str, Any]] = []

    def fail(code: str, message: str, expected: Any = None, observed: Any = None) -> None:
        issues.append(_issue(code, message, expected=expected, observed=observed))

    required_identity = ("case_id", "method", "scenario", "seed", "asset_manifest", "scenario_timeline")
    for key in required_identity:
        if normalized.get(key) in (None, ""):
            fail("E_CONFIG_REQUIRED", f"Required formal case field is missing: {key}")

    method = normalize_method_code(normalized.get("method", ""))
    if method not in FORMAL_METHOD_CODES:
        fail("E_METHOD_UNKNOWN", "Formal method must be one of B0-B8 or P.", sorted(FORMAL_METHOD_CODES), method)
    if str(normalized.get("build_configuration", "")).lower() != "release":
        fail("E_BUILD_CONFIGURATION", "Formal run requires Release build.", "Release", normalized.get("build_configuration"))
    if str(normalized.get("platform", "")).lower() != "x64":
        fail("E_PLATFORM", "Formal run requires x64.", "x64", normalized.get("platform"))
    if [_to_int(normalized.get("width")), _to_int(normalized.get("height"))] != [1920, 1080]:
        fail("E_RESOLUTION", "Formal run requires 1920x1080.", [1920, 1080], [normalized.get("width"), normalized.get("height")])
    if _as_bool(normalized.get("vsync")):
        fail("E_VSYNC", "Formal run requires VSync OFF.", False, normalized.get("vsync"))
    if not _as_bool(normalized.get("fixed_timestep")):
        fail("E_FIXED_TIMESTEP_DISABLED", "Fixed timestep must be enabled.", True, normalized.get("fixed_timestep"))
    if abs(_to_float(normalized.get("fixed_timestep_seconds")) - 1.0 / 60.0) > 1.0e-6:
        fail("E_FIXED_TIMESTEP", "Fixed timestep must be 1/60 second.", 1.0 / 60.0, normalized.get("fixed_timestep_seconds"))
    if _to_int(normalized.get("warmup_frames")) < 600:
        fail("E_WARMUP", "Warm-up must be at least 600 frames.", 600, normalized.get("warmup_frames"))
    if _to_int(normalized.get("measurement_frames")) < 6000:
        fail("E_MEASUREMENT_FRAMES", "Measurement must be at least 6000 frames.", 6000, normalized.get("measurement_frames"))
    guide_count = _to_int(normalized.get("guide_strand_count"))
    if not 2000 <= guide_count <= 5000:
        fail("E_GUIDE_COUNT", "Guide count must be within 2000-5000.", "2000..5000", guide_count)
    if _to_int(normalized.get("display_strand_count")) != 100000:
        fail("E_DISPLAY_STRANDS", "Formal 01 evaluation requires 100000 displayed strands.", 100000, normalized.get("display_strand_count"))
    points = _to_int(normalized.get("particles_per_guide"))
    if not 16 <= points <= 24:
        fail("E_POINTS_PER_GUIDE", "Particles per guide must be within 16-24.", "16..24", points)
    iterations = _to_int(normalized.get("solver_iterations"))
    if not 3 <= iterations <= 8:
        fail("E_SOLVER_ITERATIONS", "Solver iterations must be within 3-8.", "3..8", iterations)

    timeline = _resolve_case_path(normalized.get("scenario_timeline"))
    if timeline is None or not timeline.is_file():
        fail("E_TIMELINE_MISSING", "Scenario timeline file is missing.", "existing file", str(timeline))
    asset_manifest = _resolve_case_path(normalized.get("asset_manifest"))
    if asset_manifest is None or not asset_manifest.is_file():
        fail("E_ASSET_MANIFEST_MISSING", "Frozen asset manifest is missing.", "existing file", str(asset_manifest))
    elif asset_manifest:
        try:
            manifest = _read_json(asset_manifest)
            if str(manifest.get("freezeStatus", "")) != "FROZEN":
                fail("E_ASSET_NOT_FROZEN", "Asset manifest must be FROZEN.", "FROZEN", manifest.get("freezeStatus"))
            if not str(manifest.get("sourceAssetSha256", "")):
                fail("E_ASSET_HASH_MISSING", "Frozen asset manifest lacks sourceAssetSha256.")
        except Exception as exc:
            fail("E_ASSET_MANIFEST_PARSE", "Asset manifest could not be parsed.", observed=str(exc))

    if viewer_exe is not None:
        if not Path(viewer_exe).is_file():
            fail("E_VIEWER_MISSING", "Viewer executable is missing.", "existing executable", str(viewer_exe))
        elif "release" not in str(viewer_exe).lower() and _as_bool(normalized.get("strict_release_path", False)):
            fail("E_RELEASE_PATH", "Viewer path does not identify a Release build.", observed=str(viewer_exe))

    if method in {"B8", "P"}:
        flags = normalized["method_settings"]
        expected_integrated = method == "P"
        expected_external = method == "B8"
        if _as_bool(flags.get("enableSegmentGroupIntegratedState")) != expected_integrated:
            fail("E_STORAGE_MODE", "Integrated state flag does not match method.", expected_integrated, flags.get("enableSegmentGroupIntegratedState"))
        if _as_bool(flags.get("enableExternalStateArray")) != expected_external:
            fail("E_STORAGE_MODE", "External state flag does not match method.", expected_external, flags.get("enableExternalStateArray"))
        common_keys = set(_BASE_SAFE_FLAGS) - {"enableSegmentGroupIntegratedState", "enableExternalStateArray"}
        expected_common = METHOD_PRESETS[method]
        mismatches = {key: flags.get(key) for key in common_keys if flags.get(key) != expected_common.get(key)}
        if mismatches:
            fail("E_B8_P_COMMON_FLAGS", "B8/P safety and solver-facing flags must remain identical.", {k: expected_common.get(k) for k in mismatches}, mismatches)

    result = {"status": "passed" if not issues else "failed", "isValid": not issues, "normalizedCase": normalized, "issues": issues}
    if issues:
        codes = ", ".join(item["code"] for item in issues[:10])
        raise FormalRunError(f"Formal case contract failed: {codes}", EXIT_CONFIGURATION_FAILURE, issues)
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
        "windows": {},
    }
    if sys.platform != "win32":
        return payload
    try:
        line = subprocess.run(
            [
                "nvidia-smi",
                "--query-gpu=name,driver_version,temperature.gpu,clocks.current.graphics,clocks.current.memory,power.draw,power.limit,memory.total,memory.used",
                "--format=csv,noheader,nounits",
            ],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        ).stdout.strip().splitlines()[0]
        fields = [item.strip() for item in line.split(",")]
        if len(fields) >= 9:
            payload["gpu"] = {
                "name": fields[0], "driverVersion": fields[1],
                "temperatureC": _to_float(fields[2]), "graphicsClockMHz": _to_float(fields[3]),
                "memoryClockMHz": _to_float(fields[4]), "powerDrawW": _to_float(fields[5]),
                "powerLimitW": _to_float(fields[6]), "memoryTotalMiB": _to_float(fields[7]),
                "memoryUsedMiB": _to_float(fields[8]),
            }
    except Exception as exc:
        payload["gpuProbeError"] = str(exc)
    try:
        power = subprocess.run(["powercfg", "/GETACTIVESCHEME"], capture_output=True, text=True, timeout=10, check=False)
        payload["power"]["activeScheme"] = power.stdout.strip() or power.stderr.strip()
    except Exception as exc:
        payload["powerProbeError"] = str(exc)
    try:
        ps = (
            "$p=@{};"
            "$p.HAGS=(Get-ItemProperty -Path 'HKLM:\\SYSTEM\\CurrentControlSet\\Control\\GraphicsDrivers' -Name HwSchMode -ErrorAction SilentlyContinue).HwSchMode;"
            "$p.GameMode=(Get-ItemProperty -Path 'HKCU:\\Software\\Microsoft\\GameBar' -Name AutoGameModeEnabled -ErrorAction SilentlyContinue).AutoGameModeEnabled;"
            "$p.RamBytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;"
            "$p.Cpu=(Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name);"
            "$p | ConvertTo-Json -Compress"
        )
        completed = subprocess.run(["powershell", "-NoProfile", "-Command", ps], capture_output=True, text=True, timeout=20, check=False)
        if completed.stdout.strip():
            payload["windows"] = json.loads(completed.stdout.strip())
    except Exception as exc:
        payload["windowsProbeError"] = str(exc)
    return payload


class HardwareSampler:
    def __init__(self, profile_name: str, interval_seconds: float = 1.0):
        self.profile_name = profile_name
        self.interval_seconds = max(float(interval_seconds), 1.0)
        self.samples: list[dict[str, Any]] = []
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        if self._thread is None:
            self._thread = threading.Thread(target=self._run, name="hairgpu-hardware-sampler", daemon=True)
            self._thread.start()

    def stop(self) -> list[dict[str, Any]]:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=max(2.0, self.interval_seconds * 2.0))
        return list(self.samples)

    def _run(self) -> None:
        while not self._stop.is_set():
            sample = collect_hardware_probe(self.profile_name, "sample")
            sample["sampleTimeUnix"] = time.time()
            self.samples.append(sample)
            self._stop.wait(self.interval_seconds)


def write_hardware_timeseries(path: Path, samples: list[dict[str, Any]]) -> None:
    headers = [
        "sampleTimeUnix", "gpuName", "driverVersion", "temperatureC", "graphicsClockMHz",
        "memoryClockMHz", "powerDrawW", "powerLimitW", "memoryTotalMiB", "memoryUsedMiB", "activePowerScheme",
    ]
    rows: list[dict[str, Any]] = []
    for sample in samples:
        gpu = sample.get("gpu", {}) if isinstance(sample.get("gpu"), dict) else {}
        power = sample.get("power", {}) if isinstance(sample.get("power"), dict) else {}
        rows.append({
            "sampleTimeUnix": sample.get("sampleTimeUnix"), "gpuName": gpu.get("name"),
            "driverVersion": gpu.get("driverVersion"), "temperatureC": gpu.get("temperatureC"),
            "graphicsClockMHz": gpu.get("graphicsClockMHz"), "memoryClockMHz": gpu.get("memoryClockMHz"),
            "powerDrawW": gpu.get("powerDrawW"), "powerLimitW": gpu.get("powerLimitW"),
            "memoryTotalMiB": gpu.get("memoryTotalMiB"), "memoryUsedMiB": gpu.get("memoryUsedMiB"),
            "activePowerScheme": power.get("activeScheme"),
        })
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)
        handle.flush()
        os.fsync(handle.fileno())
    os.replace(temporary, path)


def _aggregate_frame_metrics(rows: list[dict[str, Any]]) -> dict[str, Any]:
    frame_ms = [_to_float(_metric(row, "frameMilliseconds", "frame_ms")) for row in rows]
    frame_ms = [value for value in frame_ms if value > 0.0 and math.isfinite(value)]
    static_count = sum(_to_int(_metric(row, "staticCachedGroupCount")) + _to_int(_metric(row, "staticTransformedGroupCount")) for row in rows)
    false_static = sum(_to_int(_metric(row, "falseStaticCount")) for row in rows)
    repair_attempts = sum(_to_int(_metric(row, "repairAttemptCount")) for row in rows)
    repair_success = sum(_to_int(_metric(row, "repairSuccessCount")) for row in rows)
    repair_verified = sum(_to_int(_metric(row, "repairVerifiedCount", default=_metric(row, "repairSuccessCount", default=0))) for row in rows)
    repair_recurrence = sum(_to_int(_metric(row, "repairRecurrenceCount")) for row in rows)
    wake_tp = sum(_to_int(_metric(row, "predictiveWakeTruePositiveCount")) for row in rows)
    wake_fp = sum(_to_int(_metric(row, "predictiveWakeFalsePositiveCount")) for row in rows)
    wake_fn = sum(_to_int(_metric(row, "predictiveWakeFalseNegativeCount")) for row in rows)
    collision_candidates = sum(_to_int(_metric(row, "collisionCandidateCount")) for row in rows)
    collision_misses = sum(_to_int(_metric(row, "collisionMissCount")) for row in rows)
    transitions = sum(_to_int(_metric(row, "stateTransitionCount")) for row in rows)
    oscillations = sum(_to_int(_metric(row, "stateOscillationCount")) for row in rows)
    boundary_values = [_to_float(_metric(row, "staticDynamicBoundaryMaxErrorMm")) for row in rows]
    boundary_values = [value for value in boundary_values if math.isfinite(value)]
    return {
        "sampleCount": len(rows),
        "averageFrameMilliseconds": sum(frame_ms) / len(frame_ms) if frame_ms else None,
        "p50FrameMilliseconds": _percentile(frame_ms, 50.0),
        "p95FrameMilliseconds": _percentile(frame_ms, 95.0),
        "p99FrameMilliseconds": _percentile(frame_ms, 99.0),
        "p999FrameMilliseconds": _percentile(frame_ms, 99.9),
        "maximumFrameMilliseconds": max(frame_ms) if frame_ms else None,
        "frameBudgetOverrunRate": _safe_rate(sum(1 for value in frame_ms if value > 16.67), len(frame_ms)),
        "falseStaticRate": _safe_rate(false_static, static_count),
        "repairExecutionSuccessRate": _safe_rate(repair_success, repair_attempts),
        "repairVerifiedSuccessRate": _safe_rate(repair_verified, repair_attempts),
        "repairRecurrenceRate": _safe_rate(repair_recurrence, repair_attempts),
        "predictiveWakePrecision": _safe_rate(wake_tp, wake_tp + wake_fp),
        "predictiveWakeRecall": _safe_rate(wake_tp, wake_tp + wake_fn),
        "collisionMissRate": _safe_rate(collision_misses, collision_candidates),
        "stateOscillationRate": _safe_rate(oscillations, transitions),
        "staticDynamicBoundaryMaxErrorMm": max(boundary_values) if boundary_values else None,
        "counts": {
            "falseStatic": false_static, "staticGroups": static_count,
            "repairAttempts": repair_attempts, "repairSuccess": repair_success,
            "repairVerified": repair_verified, "repairRecurrence": repair_recurrence,
            "wakeTruePositive": wake_tp, "wakeFalsePositive": wake_fp, "wakeFalseNegative": wake_fn,
            "collisionCandidates": collision_candidates, "collisionMisses": collision_misses,
            "stateTransitions": transitions, "stateOscillations": oscillations,
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
    issues: list[dict[str, Any]] = []
    maximum_keys = {"falseStaticRate", "repairRecurrenceRate", "collisionMissRate", "stateOscillationRate", "staticDynamicBoundaryMaxErrorMm"}
    minimum_keys = {"repairVerifiedSuccessRate", "predictiveWakeRecall"}
    for key, threshold in thresholds.items():
        observed = aggregate.get(key)
        if observed is None:
            # A metric may be inapplicable to a scenario; required metrics can be named explicitly.
            if key in set(case.get("required_quality_metrics", [])):
                issues.append(_issue("E_QUALITY_METRIC_UNAVAILABLE", f"Required quality metric is unavailable: {key}", threshold, None))
            continue
        if key in maximum_keys and float(observed) > float(threshold):
            issues.append(_issue("E_QUALITY_GATE", f"Quality metric exceeds maximum: {key}", threshold, observed))
        elif key in minimum_keys and float(observed) < float(threshold):
            issues.append(_issue("E_QUALITY_GATE", f"Quality metric is below minimum: {key}", threshold, observed))
    return issues


def _write_artifact_manifest(directory: Path, run_name: str, artifact_paths: list[Path], canonical_result_sha256: str) -> tuple[Path, str]:
    entries = []
    for path in artifact_paths:
        if path.exists() and path.is_file():
            entries.append({
                "path": path.name,
                "sizeBytes": path.stat().st_size,
                "sha256": sha256_file(path),
            })
    payload = {
        "manifestVersion": "01-safe-v3.1-existing-source-only",
        "runName": run_name,
        "canonicalResultSha256": canonical_result_sha256,
        "artifacts": sorted(entries, key=lambda item: item["path"]),
    }
    payload["manifestSha256"] = canonical_json_sha256(payload)
    manifest_path = directory / f"{run_name}_manifest.json"
    write_json(manifest_path, payload)
    return manifest_path, payload["manifestSha256"]


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
        wait_for_file(path, 10)

    issues: list[dict[str, Any]] = []
    integrity_issues: list[dict[str, Any]] = []
    summary = _read_json(Path(summary_json))
    settings = _read_json(Path(settings_json))
    rows = _read_jsonl(Path(frame_jsonl))
    try:
        csv_rows = _count_csv_rows(Path(frame_csv))
    except FormalRunError as exc:
        integrity_issues.append(_issue("E_CSV_STRUCTURE", str(exc)))
        csv_rows = -1

    expected_frames = _to_int(normalized.get("measurement_frames"), _to_int(summary.get("measurement_frames") or summary.get("measurementFrames")))
    if len(rows) != expected_frames:
        integrity_issues.append(_issue("E_JSONL_FRAME_COUNT", "JSONL row count is not exact.", expected_frames, len(rows)))
    if csv_rows != expected_frames:
        integrity_issues.append(_issue("E_CSV_FRAME_COUNT", "CSV row count is not exact.", expected_frames, csv_rows))

    status = str(summary.get("validationStatus") or summary.get("status") or "").lower()
    if status not in {"complete_valid", "complete"}:
        issues.append(_issue("E_RUNTIME_STATUS", "Viewer status is not complete_valid.", "complete_valid", status))
    if not _as_bool(summary.get("realMeasurement")):
        issues.append(_issue("E_REAL_MEASUREMENT", "Run is not marked as a real hardware measurement.", True, summary.get("realMeasurement")))

    zero_summary_counters = (
        "fatalRuntimeFailureCount", "syntheticMetricCount", "sameFrameReadbackCount",
        "fullCpuGroupUploadCount", "nanStateCount", "infiniteStateCount", "determinismMismatchCount",
    )
    for key in zero_summary_counters:
        observed = _to_int(summary.get(key))
        if observed != 0:
            issues.append(_issue("E_RUNTIME_COUNTER", f"Formal run requires {key}=0.", 0, observed))

    observed_settings = settings.get("observed")
    if not isinstance(observed_settings, dict):
        issues.append(_issue("E_OBSERVED_SETTINGS", "Observed settings object is missing."))
        observed_settings = {}
    expected_observed = {
        "renderWidth": _to_int(normalized.get("width")),
        "renderHeight": _to_int(normalized.get("height")),
        "displayStrandCount": _to_int(normalized.get("display_strand_count")),
        "guideCount": _to_int(normalized.get("guide_strand_count")),
        "particlesPerGuide": _to_int(normalized.get("particles_per_guide")),
        "solverIterations": _to_int(normalized.get("solver_iterations")),
    }
    for key, expected in expected_observed.items():
        observed = _to_int(observed_settings.get(key), -1)
        if observed != expected:
            issues.append(_issue("E_OBSERVED_SETTING_MISMATCH", f"Observed setting mismatch: {key}", expected, observed))
    if str(observed_settings.get("adapterType", summary.get("actualAdapterType", ""))).lower() != "hardware":
        issues.append(_issue("E_HARDWARE_ADAPTER", "Formal run requires a hardware adapter.", "hardware", observed_settings.get("adapterType")))

    expected_case_id = str(normalized.get("case_id", ""))
    observed_case_id = str(summary.get("caseId") or summary.get("case_id") or "")
    if expected_case_id and observed_case_id != expected_case_id:
        issues.append(_issue("E_CASE_IDENTITY", "Case identity mismatch.", expected_case_id, observed_case_id))
    expected_method = normalize_method_code(normalized.get("method", ""))
    observed_method = normalize_method_code(summary.get("appliedMethodCode") or summary.get("variant") or "")
    if expected_method and observed_method != expected_method:
        issues.append(_issue("E_METHOD_IDENTITY", "Applied method mismatch.", expected_method, observed_method))

    identity_keys = (
        "settingsSha256", "commitSha", "executableSha256", "shaderBundleSha256",
        "assetSha256", "assetManifestSha256", "scenarioTimelineSha256", "methodSettingsSha256",
    )
    for key in identity_keys:
        observed = str(summary.get(key) or settings.get(key) or "")
        if not observed:
            issues.append(_issue("E_HASH_MISSING", f"Formal identity hash is missing: {key}"))

    expected_settings_hash = str(normalized.get("settings_hash", ""))
    observed_settings_hash = str(summary.get("settingsSha256") or settings.get("settingsSha256") or "")
    if expected_settings_hash and observed_settings_hash.lower() != expected_settings_hash.lower():
        issues.append(_issue("E_SETTINGS_HASH", "Settings hash mismatch.", expected_settings_hash, observed_settings_hash))

    requested_flags = settings.get("requested", {})
    applied_flags = settings.get("applied", {})
    if not isinstance(requested_flags, dict) or not isinstance(applied_flags, dict):
        issues.append(_issue("E_METHOD_FLAGS_MISSING", "Requested/applied method flags are missing."))
    else:
        for key, expected in normalized.get("method_settings", {}).items():
            if key.startswith("enable") and key in applied_flags and _as_bool(applied_flags[key]) != _as_bool(expected):
                issues.append(_issue("E_METHOD_FLAG_MISMATCH", f"Applied method flag mismatch: {key}", expected, applied_flags[key]))

    source_indices: list[int] = []
    timing_valid = 0
    for index, row in enumerate(rows):
        source = _metric(row, "sourceFrameIndex", "source_frame_index")
        if source is None:
            integrity_issues.append(_issue("E_SOURCE_FRAME_MISSING", f"Frame {index} lacks sourceFrameIndex."))
        else:
            source_indices.append(_to_int(source))
        availability = _to_int(_metric(row, "availabilityMask", "availability_mask"))
        resolved = _as_bool(_metric(row, "gpuTimingResolved", "gpu_timing_resolved")) or (availability & (1 << 2)) != 0
        timing_valid += 1 if resolved else 0
        for key, value in row.items():
            if not _is_finite_number(value):
                integrity_issues.append(_issue("E_NON_FINITE_METRIC", f"Frame {index} contains non-finite metric: {key}", observed=value))
                break
        for key in ("syntheticMetricCount", "fatalFailure", "sameFrameReadbackCount", "fullCpuGroupUploadCount", "nanStateCount", "infiniteStateCount", "determinismMismatchCount"):
            if _to_int(_metric(row, key, default=0)) != 0:
                issues.append(_issue("E_FRAME_COUNTER", f"Frame {index} requires {key}=0.", 0, _metric(row, key)))

    if source_indices:
        expected_sequence = list(range(source_indices[0], source_indices[0] + len(source_indices)))
        if source_indices != expected_sequence:
            integrity_issues.append(_issue("E_FRAME_SEQUENCE", "Source frame indices are missing, duplicated, or reordered."))
    timing_coverage = timing_valid / len(rows) if rows else 0.0
    minimum_timing = _to_float(normalized.get("minimum_gpu_timing_coverage"), 0.99)
    if timing_coverage < minimum_timing:
        issues.append(_issue("E_TIMING_COVERAGE", "GPU timing coverage is below threshold.", minimum_timing, timing_coverage))

    environment: dict[str, Any] = {}
    if environment_json and Path(environment_json).exists():
        environment = _read_json(Path(environment_json))
    else:
        issues.append(_issue("E_ENVIRONMENT_MISSING", "Hardware environment evidence is missing."))

    aggregate = _aggregate_frame_metrics(rows)
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

    all_contract_issues = issues + integrity_issues
    if integrity_issues:
        validation_status = "complete_invalid"
        exit_code = EXIT_OUTPUT_INTEGRITY_FAILURE
    elif issues:
        validation_status = "complete_invalid"
        exit_code = EXIT_FORMAL_CONTRACT_INVALID
    elif quality_issues:
        validation_status = "quality_gate_failed"
        exit_code = EXIT_QUALITY_GATE_FAILED
    else:
        validation_status = "complete_valid"
        exit_code = EXIT_COMPLETE_VALID

    validation = {
        "validationVersion": "01-safe-v3.1-existing-source-only",
        "runName": run_name,
        "status": validation_status,
        "isValid": exit_code == EXIT_COMPLETE_VALID,
        "formalContractValid": not all_contract_issues,
        "qualityGatePassed": not quality_issues,
        "exitCode": exit_code,
        "realMeasurement": _as_bool(summary.get("realMeasurement")),
        "frameCount": len(rows),
        "csvFrameCount": csv_rows,
        "expectedFrameCount": expected_frames,
        "gpuTimingResolvedFrameCount": timing_valid,
        "gpuTimingCoverage": timing_coverage,
        "canonicalResultSha256": canonical_result_sha256,
        "aggregateMetrics": aggregate,
        "issues": all_contract_issues,
        "qualityGateIssues": quality_issues,
    }
    validation_path = Path(summary_json).parent / f"{run_name}_validation.json"
    write_json(validation_path, validation)

    summary["validationStatus"] = validation_status
    summary["status"] = validation_status
    summary["formalExitCode"] = exit_code
    summary["gpuTimingCoverage"] = timing_coverage
    summary["canonicalResultSha256"] = canonical_result_sha256
    summary["formalAggregateMetrics"] = aggregate
    summary["validationReasons"] = all_contract_issues + quality_issues
    write_json(Path(summary_json), summary)

    artifact_paths = [Path(summary_json), Path(frame_csv), Path(frame_jsonl), Path(settings_json), validation_path]
    if environment_json and Path(environment_json).exists():
        artifact_paths.append(Path(environment_json))
    hardware_csv = Path(summary_json).parent / f"{run_name}_hardware_timeseries.csv"
    if hardware_csv.exists():
        artifact_paths.append(hardware_csv)
    manifest_path, manifest_sha256 = _write_artifact_manifest(Path(summary_json).parent, run_name, artifact_paths, canonical_result_sha256)
    summary["resultManifestSha256"] = manifest_sha256
    summary["resultManifestPath"] = manifest_path.name
    write_json(Path(summary_json), summary)

    if exit_code != EXIT_COMPLETE_VALID:
        reasons = all_contract_issues + quality_issues
        codes = ", ".join(item["code"] for item in reasons[:10])
        raise FormalRunError(f"Run '{run_name}' failed formal validation: {codes}", exit_code, reasons)
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


def _write_case_snapshot(telemetry_dir: Path, run_name: str, case: dict[str, Any], viewer_exe: Path) -> Path:
    snapshot = _normalize_case(case)
    repo_root = Path(__file__).resolve().parents[2]
    snapshot["run_name"] = run_name
    snapshot["commitSha"] = str(snapshot.get("commitSha") or snapshot.get("commit_sha") or _git_commit_sha(repo_root))
    snapshot["viewerExecutable"] = str(viewer_exe)
    snapshot["executableSha256"] = sha256_file(viewer_exe)
    snapshot["shaderBundleSha256"] = sha256_tree([repo_root / "shaders", repo_root / "apps" / "dx11_viewer" / "shaders"])
    snapshot["appliedMethodSettingsSha256"] = canonical_json_sha256(snapshot.get("method_settings", {}))

    timeline = _resolve_case_path(snapshot.get("scenario_timeline"))
    if timeline and timeline.exists():
        snapshot["scenarioTimelinePath"] = str(timeline)
        snapshot["scenarioTimelineSha256"] = sha256_file(timeline)
    asset_manifest = _resolve_case_path(snapshot.get("asset_manifest"))
    if asset_manifest and asset_manifest.exists():
        snapshot["assetManifestPath"] = str(asset_manifest)
        snapshot["assetManifestSha256"] = sha256_file(asset_manifest)
        manifest = _read_json(asset_manifest)
        snapshot["assetSha256"] = str(manifest.get("sourceAssetSha256", ""))
        snapshot["assetFrozen"] = str(manifest.get("freezeStatus", "")) == "FROZEN"

    path = telemetry_dir / f"{run_name}_case.json"
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
        "HAIRGPU_APPLIED_METHOD_SETTINGS_SHA256": str(snapshot.get("appliedMethodSettingsSha256", "")),
    })

    assertion_env = {
        "scenarioAppliedEventCount": "HAIRGPU_ASSERT_MIN_SCENARIO_EVENTS",
        "collisionCandidateCount": "HAIRGPU_ASSERT_MIN_COLLISION_CANDIDATES",
        "wakeRequestGroupCount": "HAIRGPU_ASSERT_MIN_WAKE_REQUESTS",
        "repairAttemptCount": "HAIRGPU_ASSERT_MIN_REPAIRS",
        "stateTransitionCount": "HAIRGPU_ASSERT_MIN_TRANSITIONS",
    }
    for metric, env_name in assertion_env.items():
        rule = dict(snapshot.get("scenario_assertions", {})).get(metric)
        if isinstance(rule, dict) and rule.get("min") is not None:
            env[env_name] = str(rule["min"])

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
    settings = dict(snapshot.get("method_settings", {}))
    for key, value in settings.items():
        env[f"HAIRGPU_SETTING_{_env_token(key)}"] = _env_value(value)
        if key in direct_flags:
            env[direct_flags[key]] = _env_value(value)

    _set_if_present(env, "HAIRGPU_SCENARIO_TIMELINE", snapshot.get("scenario_timeline"))
    _set_if_present(env, "HAIRGPU_ASSET_MANIFEST", snapshot.get("asset_manifest"))
    _set_if_present(env, "HAIRGPU_RENDER_WIDTH", snapshot.get("width"))
    _set_if_present(env, "HAIRGPU_RENDER_HEIGHT", snapshot.get("height"))
    _set_if_present(env, "HAIRGPU_VSYNC", snapshot.get("vsync"))
    _set_if_present(env, "HAIRGPU_FIXED_TIMESTEP_ENABLED", snapshot.get("fixed_timestep"))
    _set_if_present(env, "HAIRGPU_FIXED_TIMESTEP_SECONDS", snapshot.get("fixed_timestep_seconds"))
    _set_if_present(env, "HAIRGPU_GUIDE_STRAND_COUNT", snapshot.get("guide_strand_count"))
    _set_if_present(env, "HAIRGPU_DISPLAY_STRAND_COUNT", snapshot.get("display_strand_count"))
    _set_if_present(env, "HAIRGPU_PARTICLES_PER_GUIDE", snapshot.get("particles_per_guide"))
    _set_if_present(env, "HAIRGPU_SOLVER_ITERATIONS", snapshot.get("solver_iterations"))
    _set_if_present(env, "HAIRGPU_GPU_TIMESTAMP_DELAY_FRAMES", snapshot.get("gpu_timestamp_delay_frames", 4))

    environment_json = telemetry_dir / f"{run_name}_environment.json"
    environment_payload = {"start": collect_hardware_probe(str(snapshot.get("hardware_profile", "")), "start")}
    write_json(environment_json, environment_payload)
    sampler = HardwareSampler(str(snapshot.get("hardware_profile", "")), 1.0)
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
        raise FormalRunError(f"Viewer timed out for run '{run_name}'", EXIT_RUNTIME_FAILURE) from exc
    samples = sampler.stop()
    environment_payload["samples"] = samples
    environment_payload["end"] = collect_hardware_probe(str(snapshot.get("hardware_profile", "")), "end")
    write_json(environment_json, environment_payload)
    write_hardware_timeseries(telemetry_dir / f"{run_name}_hardware_timeseries.csv", samples)

    if process.returncode not in (0, None):
        raise FormalRunError(f"Viewer exited with code {process.returncode} for run '{run_name}'", EXIT_RUNTIME_FAILURE)

    summary_json = telemetry_dir / f"{run_name}_summary.json"
    frame_csv = telemetry_dir / f"{run_name}_frames.csv"
    frame_jsonl = telemetry_dir / f"{run_name}_frames.jsonl"
    settings_json = telemetry_dir / f"{run_name}_settings.json"
    validate_required_outputs(
        summary_json, frame_csv, frame_jsonl, settings_json, run_name,
        case=snapshot, environment_json=environment_json,
    )
    return summary_json, frame_csv, frame_jsonl, settings_json, environment_json


def load_config(path: Path) -> dict[str, Any]:
    return _read_json(path)


def copy_artifact(path: Path, destination_dir: Path) -> dict[str, Any]:
    destination_dir.mkdir(parents=True, exist_ok=True)
    destination = destination_dir / path.name
    temporary = destination.with_name(destination.name + ".tmp")
    shutil.copy2(path, temporary)
    os.replace(temporary, destination)
    return {"source": str(path), "copiedTo": str(destination), "sizeBytes": destination.stat().st_size, "sha256": sha256_file(destination)}


def harvest_artifacts(values: list[str], output_dir: Path) -> list[dict[str, Any]]:
    harvested: list[dict[str, Any]] = []
    for index, value in enumerate(values):
        label, raw_path = value.split("=", 1) if "=" in value else (f"artifact_{index}", value)
        artifact_path = Path(raw_path).resolve()
        if artifact_path.exists() and artifact_path.is_file():
            record = copy_artifact(artifact_path, output_dir / "artifacts")
            record["label"] = label
            harvested.append(record)
    return harvested


def _main_impl(args: argparse.Namespace) -> int:
    config: dict[str, Any] = load_config(Path(args.config_json)) if args.config_json else {}
    case_config = config.get("case") if isinstance(config.get("case"), dict) else config
    db_path = Path(args.db or config.get("db", "runtime.db"))
    schema_path = Path(args.schema or config.get("schema", "tools/final_stage/schema.sql"))
    excel_path = Path(args.excel or config.get("excel", db_path.with_suffix(".xlsx")))
    experiment_name = args.experiment or config.get("experiment")
    method_code = normalize_method_code(args.method or config.get("method", case_config.get("method", "P")))
    scenario_code = args.scenario or config.get("scenario", case_config.get("scenario", "default"))
    asset_name = args.asset or config.get("asset", "")
    run_name = args.run_name or config.get("run_name") or case_config.get("run_name") or case_config.get("case_id")
    operator_name = args.operator or config.get("operator", "")
    notes = args.notes or config.get("notes", "")
    runtime_source = args.runtime_source or config.get("runtime_source", "dx11_viewer")
    timeout_seconds = int(args.timeout_seconds if args.timeout_seconds is not None else config.get("timeout_seconds", 900))
    if not experiment_name or not run_name:
        raise FormalRunError("--experiment and --run-name are required unless provided in --config-json", EXIT_CONFIGURATION_FAILURE)

    telemetry_dir = Path(args.telemetry_dir or config.get("telemetry_dir", "runtime_telemetry"))
    telemetry_dir.mkdir(parents=True, exist_ok=True)
    environment_json: Path | None = None
    already_validated = False

    if args.summary_json and args.frame_csv:
        summary_json = Path(args.summary_json)
        frame_csv = Path(args.frame_csv)
        frame_jsonl = Path(args.frame_jsonl) if args.frame_jsonl else None
        settings_json = Path(args.settings_json) if args.settings_json else None
    elif config.get("summary_json") and config.get("frame_csv"):
        summary_json = Path(config["summary_json"])
        frame_csv = Path(config["frame_csv"])
        frame_jsonl = Path(config["frame_jsonl"]) if config.get("frame_jsonl") else None
        settings_json = Path(config["settings_json"]) if config.get("settings_json") else None
        environment_json = Path(config["environment_json"]) if config.get("environment_json") else None
    else:
        viewer_value = args.viewer_exe or config.get("viewer_exe") or case_config.get("viewer_exe")
        if not viewer_value:
            raise FormalRunError("Provide --viewer-exe or existing telemetry files.", EXIT_CONFIGURATION_FAILURE)
        if sys.platform != "win32":
            raise FormalRunError("Viewer launch is Windows-only; use existing telemetry on this platform.", EXIT_FORMAL_CONTRACT_INVALID)
        summary_json, frame_csv, frame_jsonl, settings_json, environment_json = run_viewer(
            Path(viewer_value), telemetry_dir, str(run_name), timeout_seconds, case_config,
        )
        already_validated = True

    if frame_jsonl is None or settings_json is None:
        raise FormalRunError("Formal import requires frame JSONL and settings JSON.", EXIT_OUTPUT_INTEGRITY_FAILURE)
    if not already_validated:
        validate_case_contract(case_config, None)
        validate_required_outputs(
            Path(summary_json), Path(frame_csv), Path(frame_jsonl), Path(settings_json), str(run_name),
            case=case_config, environment_json=environment_json,
        )

    db = FinalStageDatabase(db_path)
    db.initialize(schema_path)
    validation_json = telemetry_dir / f"{run_name}_validation.json"
    metadata = RuntimeRunMetadata(
        experiment_name=str(experiment_name), method_code=method_code, scenario_code=str(scenario_code),
        asset_name=str(asset_name), run_name=str(run_name), runtime_source=str(runtime_source),
        operator_name=str(operator_name), notes=str(notes),
    )
    run_id = db.import_runtime_run(
        metadata, Path(summary_json), Path(frame_csv), frame_jsonl_path=Path(frame_jsonl),
        settings_json_path=Path(settings_json), environment_json_path=environment_json,
        validation_json_path=validation_json,
    )

    output_dir_value = args.output_dir or config.get("output_dir")
    if output_dir_value:
        output_dir = Path(output_dir_value)
        harvested: list[dict[str, Any]] = []
        sources = (
            (summary_json, "summary_json"), (frame_csv, "frame_csv"), (frame_jsonl, "frame_jsonl"),
            (settings_json, "settings_json"), (environment_json, "environment_json"),
            (validation_json, "validation_json"),
        )
        for source, label in sources:
            if source and Path(source).exists():
                record = copy_artifact(Path(source), output_dir / str(run_name))
                record["label"] = label
                harvested.append(record)
        harvested.extend(harvest_artifacts(list(config.get("artifacts", [])) + list(args.artifact), output_dir / str(run_name)))
        manifest = {
            "runId": run_id, "experiment": experiment_name, "method": method_code, "scenario": scenario_code,
            "asset": asset_name, "runtimeSource": runtime_source, "operator": operator_name, "notes": notes,
            "artifacts": harvested, "configSnapshot": config or None,
        }
        manifest["canonicalManifestSha256"] = canonical_json_sha256(manifest)
        write_json(output_dir / str(run_name) / "run_manifest.json", manifest)
        for artifact in harvested:
            if artifact.get("copiedTo"):
                db.add_artifact(run_id, "harvested", artifact.get("label", "artifact"), Path(artifact["copiedTo"]))

    db.close()
    export_workbook(db_path, excel_path)
    print(f"Imported runtime run {run_id}")
    return EXIT_COMPLETE_VALID


def main() -> int:
    parser = argparse.ArgumentParser(description="Run/import formal HairSystem 01 telemetry using existing source files only.")
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
    parser.add_argument("--timeout-seconds", type=int, default=None)
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
            write_json(telemetry_dir / f"{run_name}_error.json", {
                "runName": run_name, "status": "failed", "exitCode": exc.exit_code,
                "errorType": type(exc).__name__, "message": str(exc), "issues": exc.issues,
            })
        except Exception:
            pass
        print(str(exc), file=sys.stderr)
        return exc.exit_code
    except Exception as exc:
        print(f"Unhandled runtime failure: {exc}", file=sys.stderr)
        return EXIT_RUNTIME_FAILURE


if __name__ == "__main__":
    raise SystemExit(main())
