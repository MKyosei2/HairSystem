from __future__ import annotations

import argparse
import csv
import math
import threading
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

from hairgpu_final_db import FinalStageDatabase, RuntimeRunMetadata
from hairgpu_final_excel import export_workbook

METHOD_ALIASES = {
    "prediction_only": "prediction_only",
    "prediction-only": "prediction_only",
    "active_set_runtime": "active_set_runtime",
    "active-set-runtime": "active_set_runtime",
    "proposal_hybrid": "proposal_hybrid",
    "proposal-hybrid": "proposal_hybrid",
    "full_physics": "full_physics",
    "full-physics": "full_physics",
    "Baseline_CurrentProject": "Baseline_CurrentProject",
    "baseline_current_project": "Baseline_CurrentProject",
    "Proposed_FullSystem": "Proposed_FullSystem",
    "proposed_full_system": "Proposed_FullSystem",
}
for _mask in range(32):
    _name = f"Combo_{_mask:05b}"
    METHOD_ALIASES[_name] = _name
    METHOD_ALIASES[_name.lower()] = _name


def normalize_method_code(method_code: str) -> str:
    return METHOD_ALIASES.get(method_code, method_code)


def wait_for_file(path: Path, timeout_seconds: int) -> None:
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        if path.exists() and path.stat().st_size > 0:
            return
        time.sleep(0.5)
    raise TimeoutError(f"Timed out waiting for {path}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_sha256(payload: Any) -> str:
    encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def collect_hardware_probe(profile_name: str = "", phase: str = "") -> dict[str, Any]:
    """Collect hardware/thermal/power evidence without an extra source module."""
    import platform
    payload: dict[str, Any] = {
        "profile": profile_name,
        "phase": phase,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor(),
        "python": platform.python_version(),
        "timestampUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "gpu": {},
        "power": {},
    }
    if sys.platform == "win32":
        try:
            command = [
                "nvidia-smi",
                "--query-gpu=name,driver_version,temperature.gpu,clocks.current.graphics,clocks.current.memory,power.draw,power.limit",
                "--format=csv,noheader,nounits",
            ]
            line = subprocess.run(command, capture_output=True, text=True, timeout=15, check=False).stdout.strip().splitlines()[0]
            fields = [item.strip() for item in line.split(",")]
            if len(fields) >= 7:
                payload["gpu"] = {
                    "name": fields[0],
                    "driverVersion": fields[1],
                    "temperatureC": float(fields[2]),
                    "graphicsClockMHz": float(fields[3]),
                    "memoryClockMHz": float(fields[4]),
                    "powerDrawW": float(fields[5]),
                    "powerLimitW": float(fields[6]),
                }
        except Exception as exc:
            payload["gpuProbeError"] = str(exc)
        try:
            power = subprocess.run(["powercfg", "/GETACTIVESCHEME"], capture_output=True, text=True, timeout=10, check=False)
            payload["power"]["activeScheme"] = power.stdout.strip() or power.stderr.strip()
        except Exception as exc:
            payload["powerProbeError"] = str(exc)
    return payload



def sha256_tree(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    files: list[Path] = []
    for root in paths:
        root = Path(root)
        if root.is_file():
            files.append(root)
        elif root.exists():
            files.extend(path for path in root.rglob("*") if path.is_file())
    for path in sorted(files, key=lambda item: item.as_posix().lower()):
        digest.update(path.as_posix().encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def _read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with Path(path).open("r", encoding="utf-8-sig") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise RuntimeError(f"Invalid JSONL at {path}:{line_number}: {exc}") from exc
            if not isinstance(value, dict):
                raise RuntimeError(f"JSONL row {line_number} is not an object")
            rows.append(value)
    return rows


def _metric(row: dict[str, Any], *keys: str, default=None):
    for key in keys:
        if key in row and row[key] is not None:
            return row[key]
    return default


def _is_finite_number(value: Any) -> bool:
    if value in (None, ""):
        return True
    if isinstance(value, bool):
        return True
    if isinstance(value, (int, float)):
        return math.isfinite(float(value))
    try:
        return math.isfinite(float(value))
    except (TypeError, ValueError):
        return True


class HardwareSampler:
    """Collect one hardware sample per second without blocking the viewer."""

    def __init__(self, profile_name: str, interval_seconds: float = 1.0):
        self.profile_name = profile_name
        self.interval_seconds = max(float(interval_seconds), 1.0)
        self.samples: list[dict[str, Any]] = []
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(target=self._run, name="hairgpu-hardware-sampler", daemon=True)
        self._thread.start()

    def stop(self) -> list[dict[str, Any]]:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=max(self.interval_seconds * 2.0, 2.0))
        return list(self.samples)

    def _run(self) -> None:
        while not self._stop.is_set():
            sample = collect_hardware_probe(self.profile_name, "sample")
            sample["sampleTimeUnix"] = time.time()
            self.samples.append(sample)
            self._stop.wait(self.interval_seconds)


def write_hardware_timeseries(path: Path, samples: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    headers = [
        "sampleTimeUnix", "gpuName", "driverVersion", "temperatureC",
        "graphicsClockMHz", "memoryClockMHz", "powerDrawW", "powerLimitW",
        "activePowerScheme",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        for sample in samples:
            gpu = sample.get("gpu", {}) if isinstance(sample.get("gpu"), dict) else {}
            power = sample.get("power", {}) if isinstance(sample.get("power"), dict) else {}
            writer.writerow({
                "sampleTimeUnix": sample.get("sampleTimeUnix"),
                "gpuName": gpu.get("name"),
                "driverVersion": gpu.get("driverVersion"),
                "temperatureC": gpu.get("temperatureC"),
                "graphicsClockMHz": gpu.get("graphicsClockMHz"),
                "memoryClockMHz": gpu.get("memoryClockMHz"),
                "powerDrawW": gpu.get("powerDrawW"),
                "powerLimitW": gpu.get("powerLimitW"),
                "activePowerScheme": power.get("activeScheme"),
            })

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
    """Validate one run and emit a machine-readable validity decision."""
    for path in (summary_json, frame_csv, frame_jsonl, settings_json):
        wait_for_file(path, 10)

    summary = json.loads(summary_json.read_text(encoding="utf-8-sig"))
    settings = json.loads(settings_json.read_text(encoding="utf-8-sig"))
    rows = _read_jsonl(frame_jsonl)
    issues: list[dict[str, Any]] = []

    def issue(code: str, message: str, *, expected=None, observed=None) -> None:
        issues.append({
            "severity": "error",
            "code": code,
            "message": message,
            "expected": expected,
            "observed": observed,
        })

    status = str(summary.get("validationStatus") or summary.get("status") or "").lower()
    if status not in {"complete_valid", "complete"}:
        issue("status", "Run status is not complete_valid.", expected="complete_valid", observed=status)
    if not bool(summary.get("realMeasurement", False)):
        issue("real_measurement", "Run is not marked as a real hardware measurement.", expected=True, observed=summary.get("realMeasurement"))
    if int(summary.get("fatalRuntimeFailureCount") or 0) != 0:
        issue("fatal_runtime_failure", "Fatal runtime failures were reported.", expected=0, observed=summary.get("fatalRuntimeFailureCount"))
    if int(summary.get("syntheticMetricCount") or 0) != 0:
        issue("synthetic_metrics", "Synthetic metrics are forbidden in formal evidence.", expected=0, observed=summary.get("syntheticMetricCount"))

    observed_settings = settings.get("observed")
    if not isinstance(observed_settings, dict) or not observed_settings:
        issue("observed_settings", "Observed settings are missing from settings JSON.")

    expected_frames = int(summary.get("measurement_frames") or summary.get("measurementFrames") or (case or {}).get("measurement_frames") or 0)
    if expected_frames and len(rows) != expected_frames:
        issue("frame_count", "Measured JSONL row count is not exact.", expected=expected_frames, observed=len(rows))

    source_indices: list[int] = []
    resolved_count = 0
    for index, row in enumerate(rows):
        source = _metric(row, "sourceFrameIndex", "source_frame_index")
        if source is None:
            issue("source_frame_missing", f"Frame row {index} has no source-frame identity.")
        else:
            source_indices.append(int(source))
        resolved = bool(_metric(row, "gpuTimingResolved", "gpu_timing_resolved", default=False))
        availability = int(_metric(row, "availabilityMask", "availability_mask", default=0) or 0)
        if resolved or (availability & (1 << 2)) != 0:
            resolved_count += 1
        for key, value in row.items():
            if not _is_finite_number(value):
                issue("non_finite_metric", f"Frame row {index} contains a non-finite value for {key}.", observed=value)
                break
        if int(_metric(row, "syntheticMetricCount", "synthetic_metric_count", default=0) or 0) != 0:
            issue("frame_synthetic_metric", f"Frame row {index} contains synthetic metrics.")
        if int(_metric(row, "fatalFailure", "fatal_failure", default=0) or 0) != 0:
            issue("frame_fatal_failure", f"Frame row {index} reports a fatal runtime failure.")

    if source_indices:
        expected_sequence = list(range(source_indices[0], source_indices[0] + len(source_indices)))
        if source_indices != expected_sequence:
            issue("frame_sequence", "Source frame indices are missing, duplicated, or reordered.")
        if len(set(source_indices)) != len(source_indices):
            issue("duplicate_source_frame", "Duplicate source frame indices were emitted.")

    coverage = resolved_count / len(rows) if rows else 0.0
    minimum_coverage = float((case or {}).get("minimum_gpu_timing_coverage", 0.99))
    if coverage < minimum_coverage:
        issue("gpu_timing_coverage", "GPU timing coverage is below the formal threshold.", expected=minimum_coverage, observed=coverage)

    if case:
        expected_case = str(case.get("case_id", ""))
        observed_case = str(summary.get("caseId") or summary.get("case_id") or "")
        if expected_case and expected_case != observed_case:
            issue("case_identity", "Case identity does not match.", expected=expected_case, observed=observed_case)
        expected_method = str(case.get("method", ""))
        observed_method = str(summary.get("appliedMethodCode") or summary.get("variant") or "")
        if expected_method and observed_method and expected_method != observed_method:
            issue("method_identity", "Applied method does not match requested method.", expected=expected_method, observed=observed_method)
        expected_hash = str(case.get("settings_hash") or canonical_json_sha256(case))
        observed_hash = str(summary.get("settingsSha256") or summary.get("settings_hash") or "")
        if observed_hash and observed_hash.lower() != expected_hash.lower():
            issue("settings_hash", "Settings hash does not match the case snapshot.", expected=expected_hash, observed=observed_hash)

    environment = {}
    if environment_json and Path(environment_json).exists():
        environment = json.loads(Path(environment_json).read_text(encoding="utf-8-sig"))
    canonical_payload = {
        "summary": summary,
        "settings": settings,
        "frames": rows,
        "environment": environment,
    }
    canonical_result_sha256 = canonical_json_sha256(canonical_payload)
    validation_status = "complete_valid" if not issues else "complete_invalid"
    validation = {
        "runName": run_name,
        "status": validation_status,
        "isValid": not issues,
        "realMeasurement": bool(summary.get("realMeasurement", False)),
        "frameCount": len(rows),
        "expectedFrameCount": expected_frames,
        "gpuTimingResolvedFrameCount": resolved_count,
        "gpuTimingCoverage": coverage,
        "canonicalResultSha256": canonical_result_sha256,
        "issues": issues,
    }
    validation_path = summary_json.parent / f"{run_name}_validation.json"
    write_json(validation_path, validation)

    summary["validationStatus"] = validation_status
    summary["status"] = validation_status
    summary["gpuTimingCoverage"] = coverage
    summary["canonicalResultSha256"] = canonical_result_sha256
    summary["validationReasons"] = issues
    write_json(summary_json, summary)

    if issues:
        codes = ", ".join(item["code"] for item in issues[:8])
        raise RuntimeError(f"Run '{run_name}' failed formal validation: {codes}")
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


def _resolve_case_path(value: Any) -> Path | None:
    if value in (None, ""):
        return None
    path = Path(str(value))
    return path.resolve() if not path.is_absolute() else path


def _set_if_present(env: dict[str, str], key: str, value: Any) -> None:
    if value not in (None, ""):
        env[key] = _env_value(value)


def _write_case_snapshot(telemetry_dir: Path, run_name: str, case: dict[str, Any], viewer_exe: Path) -> Path:
    snapshot = dict(case)
    snapshot.setdefault("run_name", run_name)
    snapshot.setdefault("settings_hash", canonical_json_sha256(case))
    method_settings = dict(case.get("method_settings", {}))
    snapshot["appliedMethodSettingsSha256"] = canonical_json_sha256(method_settings)
    snapshot["method_settings_sha256"] = snapshot["appliedMethodSettingsSha256"]
    commit_sha = str(case.get("commitSha") or case.get("commit_sha") or "")
    if not commit_sha:
        try:
            repo_root = Path(__file__).resolve().parents[2]
            commit_sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo_root, capture_output=True, text=True, timeout=10, check=False).stdout.strip()
        except Exception:
            commit_sha = ""
    snapshot["commitSha"] = commit_sha
    snapshot["viewerExecutable"] = str(viewer_exe)
    snapshot["executableSha256"] = sha256_file(viewer_exe)
    repo_root = Path(__file__).resolve().parents[2]
    shader_roots = [repo_root / "shaders", repo_root / "apps" / "dx11_viewer" / "shaders"]
    snapshot["shaderBundleSha256"] = sha256_tree(shader_roots)

    timeline = _resolve_case_path(case.get("scenario_timeline"))
    if timeline and timeline.exists():
        snapshot["scenarioTimelinePath"] = str(timeline)
        snapshot["scenarioTimelineSha256"] = sha256_file(timeline)

    asset_manifest = _resolve_case_path(case.get("asset_manifest"))
    if asset_manifest and asset_manifest.exists():
        snapshot["assetManifestPath"] = str(asset_manifest)
        snapshot["assetManifestSha256"] = sha256_file(asset_manifest)
        try:
            manifest = json.loads(asset_manifest.read_text(encoding="utf-8-sig"))
            snapshot.setdefault("assetSha256", manifest.get("sourceAssetSha256", ""))
            snapshot["assetFrozen"] = str(manifest.get("freezeStatus", "")) == "FROZEN"
        except Exception:
            pass

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
    if not viewer_exe.exists():
        raise FileNotFoundError(f"Viewer executable does not exist: {viewer_exe}")

    telemetry_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["HAIRGPU_AUTO_TELEMETRY"] = "1"
    env["HAIRGPU_TELEMETRY_DIR"] = str(telemetry_dir)
    env["HAIRGPU_TELEMETRY_RUN_NAME"] = run_name

    if case:
        case_snapshot_path = _write_case_snapshot(telemetry_dir, run_name, case, viewer_exe)
        env["HAIRGPU_CASE_JSON"] = str(case_snapshot_path)
        env["HAIRGPU_EXPERIMENT_CASE_ID"] = str(case.get("case_id", ""))
        env["HAIRGPU_VARIANT"] = str(case.get("method", case.get("variant", "")))
        env["HAIRGPU_SCENARIO"] = str(case.get("scenario", ""))
        env["HAIRGPU_SEED"] = str(case.get("seed", 0))
        env["HAIRGPU_WARMUP_FRAMES"] = str(case.get("warmup_frames", 0))
        env["HAIRGPU_MEASUREMENT_FRAMES"] = str(case.get("measurement_frames", 0))
        env["HAIRGPU_AUTO_EXIT_AFTER_CAPTURE"] = "1"
        env["HAIRGPU_SETTINGS_HASH"] = str(case.get("settings_hash") or canonical_json_sha256(case))
        env["HAIRGPU_EXECUTABLE_SHA256"] = sha256_file(viewer_exe)
        env["HAIRGPU_COMMIT_SHA"] = str(case_snapshot_path and json.loads(case_snapshot_path.read_text(encoding="utf-8-sig")).get("commitSha", ""))
        env["HAIRGPU_BUILD_CONFIGURATION"] = str(case.get("build_configuration", "Release"))
        env["HAIRGPU_PLATFORM"] = str(case.get("platform", "x64"))
        env["HAIRGPU_HARDWARE_PROFILE"] = str(case.get("hardware_profile", ""))
        env["HAIRGPU_MEASUREMENT_MODE"] = str(case.get("measurement_mode", "performance"))
        env["HAIRGPU_REPEAT_INDEX"] = str(case.get("repeat_index", 0))
        snapshot_payload = json.loads(case_snapshot_path.read_text(encoding="utf-8-sig"))
        env["HAIRGPU_SCENARIO_TIMELINE_SHA256"] = str(snapshot_payload.get("scenarioTimelineSha256", ""))
        env["HAIRGPU_ASSET_MANIFEST_SHA256"] = str(snapshot_payload.get("assetManifestSha256", ""))
        env["HAIRGPU_ASSET_SHA256"] = str(snapshot_payload.get("assetSha256", ""))
        env["HAIRGPU_SHADER_BUNDLE_SHA256"] = str(snapshot_payload.get("shaderBundleSha256", ""))
        env["HAIRGPU_ASSET_FROZEN"] = "1" if snapshot_payload.get("assetFrozen") else "0"
        env["HAIRGPU_APPLIED_METHOD_SETTINGS_SHA256"] = str(snapshot_payload.get("appliedMethodSettingsSha256", ""))
        assertions = dict(case.get("scenario_assertions", {}))
        assertion_env = {
            "scenarioAppliedEventCount": "HAIRGPU_ASSERT_MIN_SCENARIO_EVENTS",
            "collisionCandidateCount": "HAIRGPU_ASSERT_MIN_COLLISION_CANDIDATES",
            "wakeRequestGroupCount": "HAIRGPU_ASSERT_MIN_WAKE_REQUESTS",
            "repairAttemptCount": "HAIRGPU_ASSERT_MIN_REPAIRS",
            "stateTransitionCount": "HAIRGPU_ASSERT_MIN_TRANSITIONS",
        }
        for metric, env_name in assertion_env.items():
            rule = assertions.get(metric)
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
        settings = dict(case.get("method_settings", {}))
        for key, value in case.items():
            if key.startswith("enable") or key in ("groupsPerBlock", "solver_iterations", "guide_strand_count", "display_strand_count"):
                settings.setdefault(key, value)
        for key, value in settings.items():
            env[f"HAIRGPU_SETTING_{_env_token(key)}"] = _env_value(value)
            if key in direct_flags:
                env[direct_flags[key]] = _env_value(value)

        _set_if_present(env, "HAIRGPU_SCENARIO_TIMELINE", case.get("scenario_timeline"))
        _set_if_present(env, "HAIRGPU_ASSET_MANIFEST", case.get("asset_manifest"))
        _set_if_present(env, "HAIRGPU_BUILD_CONFIGURATION", case.get("build_configuration"))
        _set_if_present(env, "HAIRGPU_PLATFORM", case.get("platform"))
        _set_if_present(env, "HAIRGPU_RENDER_WIDTH", case.get("width"))
        _set_if_present(env, "HAIRGPU_RENDER_HEIGHT", case.get("height"))
        _set_if_present(env, "HAIRGPU_VSYNC", case.get("vsync"))
        _set_if_present(env, "HAIRGPU_FIXED_TIMESTEP_ENABLED", case.get("fixed_timestep"))
        _set_if_present(env, "HAIRGPU_FIXED_TIMESTEP_SECONDS", case.get("fixed_timestep_seconds"))
        _set_if_present(env, "HAIRGPU_GUIDE_STRAND_COUNT", case.get("guide_strand_count"))
        _set_if_present(env, "HAIRGPU_DISPLAY_STRAND_COUNT", case.get("display_strand_count"))
        _set_if_present(env, "HAIRGPU_PARTICLES_PER_GUIDE", case.get("particles_per_guide"))
        _set_if_present(env, "HAIRGPU_SOLVER_ITERATIONS", case.get("solver_iterations"))
        _set_if_present(env, "HAIRGPU_GPU_TIMESTAMP_DELAY_FRAMES", case.get("gpu_timestamp_delay_frames"))

    environment_json = telemetry_dir / f"{run_name}_environment.json"
    environment_payload = {"start": collect_hardware_probe(str((case or {}).get("hardware_profile", "")), "start")}
    write_json(environment_json, environment_payload)
    hardware_sampler = HardwareSampler(str((case or {}).get("hardware_profile", "")), 1.0)
    hardware_sampler.start()
    process = subprocess.Popen([str(viewer_exe)], env=env)
    try:
        process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as exc:
        hardware_sampler.stop()
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
        raise TimeoutError(f"Timed out waiting for viewer process for run '{run_name}'") from exc
    if process.returncode not in (0, None):
        raise RuntimeError(f"Viewer exited with code {process.returncode} for run '{run_name}'")

    summary_json = telemetry_dir / f"{run_name}_summary.json"
    frame_csv = telemetry_dir / f"{run_name}_frames.csv"
    frame_jsonl = telemetry_dir / f"{run_name}_frames.jsonl"
    settings_json = telemetry_dir / f"{run_name}_settings.json"
    hardware_samples = hardware_sampler.stop()
    environment_payload["samples"] = hardware_samples
    environment_payload["end"] = collect_hardware_probe(str((case or {}).get("hardware_profile", "")), "end")
    write_json(environment_json, environment_payload)
    write_hardware_timeseries(telemetry_dir / f"{run_name}_hardware_timeseries.csv", hardware_samples)
    validate_required_outputs(
        summary_json, frame_csv, frame_jsonl, settings_json, run_name,
        case=case, environment_json=environment_json,
    )
    return summary_json, frame_csv, frame_jsonl, settings_json, environment_json


def load_config(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def copy_artifact(path: Path, destination_dir: Path) -> dict[str, Any]:
    destination_dir.mkdir(parents=True, exist_ok=True)
    dst = destination_dir / path.name
    shutil.copy2(path, dst)
    return {"source": str(path), "copiedTo": str(dst), "sizeBytes": dst.stat().st_size, "sha256": sha256_file(dst)}


def harvest_artifacts(values: list[str], output_dir: Path) -> list[dict[str, Any]]:
    harvested: list[dict[str, Any]] = []
    for index, value in enumerate(values):
        label, raw_path = value.split("=", 1) if "=" in value else (f"artifact_{index}", value)
        artifact_path = Path(raw_path).resolve()
        if artifact_path.exists():
            record = copy_artifact(artifact_path, output_dir / "artifacts")
            record["label"] = label
            harvested.append(record)
    return harvested


def main() -> int:
    parser = argparse.ArgumentParser(description="Run final stage DX11 telemetry -> SQLite -> Excel pipeline")
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

    config: dict[str, Any] = load_config(Path(args.config_json)) if args.config_json else {}
    db_path = Path(args.db or config.get("db", "runtime.db"))
    schema_path = Path(args.schema or config.get("schema", "tools/final_stage/schema.sql"))
    excel_path = Path(args.excel or config.get("excel", db_path.with_suffix(".xlsx")))
    experiment_name = args.experiment or config.get("experiment")
    method_code = normalize_method_code(args.method or config.get("method", "proposal_hybrid"))
    scenario_code = args.scenario or config.get("scenario", "default")
    asset_name = args.asset or config.get("asset", "")
    run_name = args.run_name or config.get("run_name")
    operator_name = args.operator or config.get("operator", "")
    notes = args.notes or config.get("notes", "")
    runtime_source = args.runtime_source or config.get("runtime_source", "dx11_viewer")
    timeout_seconds = int(args.timeout_seconds if args.timeout_seconds is not None else config.get("timeout_seconds", 900))
    if not experiment_name or not run_name:
        raise SystemExit("--experiment and --run-name are required unless provided in --config-json")

    telemetry_dir = Path(args.telemetry_dir or config.get("telemetry_dir", "runtime_telemetry"))
    telemetry_dir.mkdir(parents=True, exist_ok=True)
    db = FinalStageDatabase(db_path)
    db.initialize(schema_path)

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
    else:
        viewer_exe_value = args.viewer_exe or config.get("viewer_exe")
        if not viewer_exe_value:
            raise SystemExit("Provide either --viewer-exe or both --summary-json and --frame-csv")
        if sys.platform != "win32":
            print("Viewer launch is Windows-only. Use existing telemetry on this platform.", file=sys.stderr)
            return 2
        case_config = config.get("case") if isinstance(config.get("case"), dict) else config
        try:
            summary_json, frame_csv, frame_jsonl, settings_json, environment_json = run_viewer(Path(viewer_exe_value), telemetry_dir, run_name, timeout_seconds, case_config)
        except Exception as exc:
            error_path = telemetry_dir / f"{run_name}_error.json"
            write_json(error_path, {"runName": run_name, "status": "failed", "errorType": type(exc).__name__, "message": str(exc)})
            raise

    environment_json = locals().get("environment_json")
    validation_json = telemetry_dir / f"{run_name}_validation.json"
    if frame_jsonl and settings_json:
        validate_required_outputs(
            Path(summary_json), Path(frame_csv), Path(frame_jsonl), Path(settings_json), run_name,
            case=config.get("case") if isinstance(config.get("case"), dict) else config,
            environment_json=Path(environment_json) if environment_json else None,
        )

    metadata = RuntimeRunMetadata(
        experiment_name=experiment_name,
        method_code=method_code,
        scenario_code=scenario_code,
        asset_name=asset_name,
        run_name=run_name,
        runtime_source=runtime_source,
        operator_name=operator_name,
        notes=notes,
    )
    run_id = db.import_runtime_run(
        metadata, summary_json, frame_csv,
        frame_jsonl_path=frame_jsonl, settings_json_path=settings_json,
        environment_json_path=environment_json, validation_json_path=validation_json,
    )

    output_dir_value = args.output_dir or config.get("output_dir")
    if output_dir_value:
        output_dir = Path(output_dir_value)
        harvested = []
        for source, label in ((summary_json, "summary_json"), (frame_csv, "frame_csv"), (frame_jsonl, "frame_jsonl"), (settings_json, "settings_json"), (environment_json, "environment_json")):
            if source and Path(source).exists():
                record = copy_artifact(Path(source), output_dir / run_name)
                record["label"] = label
                harvested.append(record)
        harvested.extend(harvest_artifacts(list(config.get("artifacts", [])) + list(args.artifact), output_dir / run_name))
        manifest = {
            "runId": run_id,
            "experiment": experiment_name,
            "method": method_code,
            "scenario": scenario_code,
            "asset": asset_name,
            "runtimeSource": runtime_source,
            "operator": operator_name,
            "notes": notes,
            "telemetry": {
                "summaryJson": str(summary_json),
                "frameCsv": str(frame_csv),
                "frameJsonl": str(frame_jsonl) if frame_jsonl else None,
                "settingsJson": str(settings_json) if settings_json else None,
                "summarySha256": sha256_file(summary_json),
                "frameSha256": sha256_file(frame_csv),
                "frameJsonlSha256": sha256_file(frame_jsonl) if frame_jsonl and Path(frame_jsonl).exists() else None,
                "settingsSha256": sha256_file(settings_json) if settings_json and Path(settings_json).exists() else None,
                "environmentJson": str(environment_json) if environment_json else None,
                "environmentSha256": sha256_file(environment_json) if environment_json and Path(environment_json).exists() else None,
            },
            "artifacts": harvested,
            "configSnapshot": config or None,
        }
        write_json(output_dir / run_name / "run_manifest.json", manifest)
        for artifact in harvested:
            if artifact.get("copiedTo"):
                db.add_artifact(run_id, "harvested", artifact.get("label", "artifact"), Path(artifact["copiedTo"]))

    db.close()
    export_workbook(db_path, excel_path)
    print(f"Imported runtime run {run_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
