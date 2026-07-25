from __future__ import annotations

"""Strict final gate for the HairSystem formal runtime environment.

The existing runtime suite remains responsible for launching cases and validating
per-frame telemetry.  This module adds the missing experiment-level guarantees:
exact workload enforcement, host-profile validation, native environment aliases,
requested/applied/observed parity, determinism comparison, long-run degradation
checks, and a single final PASS marker.
"""

import argparse
import ctypes
import hashlib
import json
import math
import os
import platform
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

import hairgpu_final_run as final_run
import hairgpu_runtime_suite as runtime_suite

GATE_VERSION = "4.1-native-evidence-aligned"
EXIT_PASS = 0
EXIT_QUALITY = 1
EXIT_CONTRACT = 2
EXIT_RUNTIME = 3
EXIT_INTEGRITY = 4

HASH40 = re.compile(r"^[0-9a-fA-F]{40}$")
HASH64 = re.compile(r"^[0-9a-fA-F]{64}$")
REQUIRED_METHODS = {"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "P"}
REQUIRED_SCENARIOS = {f"S{index:02d}" for index in range(1, 19)}


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8-sig") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            value = json.loads(line)
            if not isinstance(value, dict):
                raise ValueError(f"JSONL row {line_number} is not an object: {path}")
            rows.append(value)
    return rows


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    encoded = json.dumps(dict(payload), ensure_ascii=False, indent=2, allow_nan=False) + "\n"
    descriptor, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(encoded)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_hash(payload: Any) -> str:
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def issue(code: str, message: str, *, expected: Any = None, observed: Any = None) -> dict[str, Any]:
    return {
        "code": code,
        "message": message,
        "expected": expected,
        "observed": observed,
    }


def resolve_path(base: Path, value: Any) -> Path:
    path = Path(str(value))
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def run_text(command: Sequence[str], cwd: Path, timeout: int = 30) -> tuple[int, str, str]:
    try:
        completed = subprocess.run(
            list(command),
            cwd=cwd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except (FileNotFoundError, subprocess.SubprocessError) as exc:
        return 127, "", str(exc)
    return completed.returncode, completed.stdout.strip(), completed.stderr.strip()


def output_directory(config_path: Path, override: Path | None) -> Path:
    if override is not None:
        return override.resolve()
    config = read_json(config_path)
    return resolve_path(config_path.parent, config["outputs_dir"])


def _scenario_family(value: str) -> str:
    match = re.match(r"^(S\d{2})", str(value))
    return match.group(1) if match else str(value)


def _as_bool(value: Any) -> bool | None:
    if isinstance(value, bool):
        return value
    if value is None:
        return None
    normalized = str(value).strip().lower()
    if normalized in {"1", "true", "yes", "on"}:
        return True
    if normalized in {"0", "false", "no", "off"}:
        return False
    return None


def _equal(expected: Any, observed: Any) -> bool:
    if isinstance(expected, bool):
        return _as_bool(observed) is expected
    if isinstance(expected, float):
        try:
            return math.isclose(float(expected), float(observed), rel_tol=0.0, abs_tol=1.0e-8)
        except (TypeError, ValueError):
            return False
    if isinstance(expected, int):
        try:
            return int(float(observed)) == expected
        except (TypeError, ValueError):
            return False
    return str(expected) == str(observed)


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


def validate_static_contract(config_path: Path, repo_root: Path) -> list[dict[str, Any]]:
    config = read_json(config_path)
    defaults = config.get("run_defaults") if isinstance(config.get("run_defaults"), dict) else {}
    problems: list[dict[str, Any]] = []

    exact = {
        "build_configuration": "Release",
        "platform": "x64",
        "width": 1920,
        "height": 1080,
        "fixed_timestep": True,
        "fixed_timestep_seconds": 1.0 / 60.0,
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "guide_strand_count": 4000,
        "followers_per_guide": 24,
        "display_strand_count": 100000,
        "particles_per_guide": 128,
        "solver_iterations": 6,
        "vsync": False,
        "present_sync_interval": 0,
    }
    for key, expected in exact.items():
        observed = defaults.get(key)
        if not _equal(expected, observed):
            problems.append(issue(
                "fixed_contract_mismatch",
                f"Invalid formal fixed value: {key}",
                expected=expected,
                observed=observed,
            ))

    seeds = config.get("seeds") if isinstance(config.get("seeds"), list) else []
    if len({int(value) for value in seeds}) < 5:
        problems.append(issue(
            "insufficient_seed_count",
            "At least five unique formal seeds are required.",
            expected=5,
            observed=seeds,
        ))

    method_codes = {
        final_run.normalize_method_code(str(entry.get("code", "")))
        for entry in config.get("methods", [])
        if isinstance(entry, dict)
    }
    if not REQUIRED_METHODS.issubset(method_codes):
        problems.append(issue(
            "method_matrix_incomplete",
            "The formal method matrix must contain B0-B8 and P.",
            expected=sorted(REQUIRED_METHODS),
            observed=sorted(method_codes),
        ))

    scenario_codes = {
        _scenario_family(str(entry.get("code", "")))
        for entry in config.get("scenarios", [])
        if isinstance(entry, dict)
    }
    if not REQUIRED_SCENARIOS.issubset(scenario_codes):
        problems.append(issue(
            "scenario_matrix_incomplete",
            "The formal scenario matrix must contain S01-S18.",
            expected=sorted(REQUIRED_SCENARIOS),
            observed=sorted(scenario_codes),
        ))

    source = config.get("source") if isinstance(config.get("source"), dict) else {}
    required_branch = str(source.get("required_branch", "Novelty"))
    branch_code, branch, _ = run_text(["git", "branch", "--show-current"], repo_root)
    allow_review = os.environ.get("HAIRGPU_ALLOW_REVIEW_BRANCH", "").strip().lower() in {
        "1", "true", "yes", "on"
    }
    if branch_code != 0 or not branch:
        problems.append(issue(
            "detached_or_unknown_branch",
            "Formal evidence requires a named Git branch.",
            expected=required_branch,
            observed=branch,
        ))
    elif branch != required_branch and not (allow_review and branch.startswith("agent/")):
        problems.append(issue(
            "branch_mismatch",
            "Formal branch does not match the configured branch.",
            expected=required_branch,
            observed=branch,
        ))

    status_code, dirty, _ = run_text(
        ["git", "status", "--porcelain", "--untracked-files=normal"],
        repo_root,
    )
    if status_code != 0:
        problems.append(issue("git_status_failed", "Could not inspect the Git worktree."))
    elif dirty:
        problems.append(issue(
            "dirty_worktree",
            "Formal evidence requires a clean worktree, including untracked files.",
            expected="clean",
            observed=dirty.splitlines()[:20],
        ))

    manifest_value = source.get("asset_manifest")
    if not manifest_value:
        problems.append(issue("asset_manifest_not_configured", "source.asset_manifest is required."))
    else:
        manifest_path = resolve_path(config_path.parent, manifest_value)
        if not manifest_path.is_file():
            problems.append(issue(
                "frozen_asset_manifest_missing",
                "Run prepare-asset before formal measurement.",
                observed=str(manifest_path),
            ))
    return problems


class SYSTEM_POWER_STATUS(ctypes.Structure):
    _fields_ = [
        ("ACLineStatus", ctypes.c_ubyte),
        ("BatteryFlag", ctypes.c_ubyte),
        ("BatteryLifePercent", ctypes.c_ubyte),
        ("SystemStatusFlag", ctypes.c_ubyte),
        ("BatteryLifeTime", ctypes.c_ulong),
        ("BatteryFullLifeTime", ctypes.c_ulong),
    ]


def collect_host_evidence(repo_root: Path) -> dict[str, Any]:
    evidence: dict[str, Any] = {
        "gateVersion": GATE_VERSION,
        "timestampUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "platform": platform.platform(),
        "python": platform.python_version(),
        "machine": platform.machine(),
        "logicalCpuCount": os.cpu_count(),
        "physicalCpuCoreCount": None,
        "acPower": None,
        "powerScheme": "",
        "gpu": {},
    }
    if sys.platform != "win32":
        return evidence

    status = SYSTEM_POWER_STATUS()
    try:
        if ctypes.windll.kernel32.GetSystemPowerStatus(ctypes.byref(status)):
            evidence["acPower"] = status.ACLineStatus == 1
    except Exception as exc:  # pragma: no cover - Windows-only failure path
        evidence["powerProbeError"] = str(exc)

    code, stdout, stderr = run_text(["powercfg", "/GETACTIVESCHEME"], repo_root)
    evidence["powerScheme"] = stdout if code == 0 else ""
    if code != 0:
        evidence["powerProbeError"] = stderr or stdout

    cpu_script = (
        "$v=(Get-CimInstance Win32_Processor | "
        "Measure-Object -Property NumberOfCores -Sum).Sum;"
        "[Console]::WriteLine([int]$v)"
    )
    code, stdout, stderr = run_text(
        ["powershell", "-NoProfile", "-NonInteractive", "-Command", cpu_script],
        repo_root,
    )
    if code == 0:
        try:
            evidence["physicalCpuCoreCount"] = int(stdout.splitlines()[-1])
        except (ValueError, IndexError):
            evidence["cpuProbeError"] = stdout
    else:
        evidence["cpuProbeError"] = stderr or stdout

    query = (
        "index,name,uuid,driver_version,temperature.gpu,clocks.current.graphics,"
        "clocks.max.graphics,memory.total,memory.used"
    )
    code, stdout, stderr = run_text(
        ["nvidia-smi", f"--query-gpu={query}", "--format=csv,noheader,nounits"],
        repo_root,
    )
    if code == 0 and stdout:
        fields = [part.strip() for part in stdout.splitlines()[0].split(",")]
        if len(fields) >= 9:
            def number(index: int) -> float | None:
                try:
                    return float(fields[index])
                except (ValueError, IndexError):
                    return None

            evidence["gpu"] = {
                "index": int(number(0) or 0),
                "name": fields[1],
                "uuid": fields[2],
                "driverVersion": fields[3],
                "temperatureC": number(4),
                "graphicsClockMHz": number(5),
                "maximumGraphicsClockMHz": number(6),
                "memoryTotalMiB": number(7),
                "memoryUsedMiB": number(8),
            }
    else:
        evidence["gpuProbeError"] = stderr or stdout
    return evidence


def _profile_from_config(config_path: Path) -> tuple[str, dict[str, Any]]:
    config = read_json(config_path)
    defaults = config.get("run_defaults") if isinstance(config.get("run_defaults"), dict) else {}
    profile_name = str(defaults.get("hardware_profile", ""))
    profiles_path = resolve_path(config_path.parent, config["hardware_profiles"])
    profiles_payload = read_json(profiles_path)
    profiles = profiles_payload.get("profiles") if isinstance(profiles_payload.get("profiles"), dict) else {}
    profile = profiles.get(profile_name) if isinstance(profiles, dict) else None
    return profile_name, dict(profile) if isinstance(profile, dict) else {}


def validate_host_contract(config_path: Path, evidence: Mapping[str, Any], skip: bool) -> list[dict[str, Any]]:
    if skip:
        return []
    if sys.platform != "win32":
        return [issue(
            "formal_windows_required",
            "Formal DirectX/GPU evidence must run on Windows.",
            expected="win32",
            observed=sys.platform,
        )]

    profile_name, profile = _profile_from_config(config_path)
    if not profile:
        return [issue(
            "hardware_profile_missing",
            "Configured hardware profile was not found.",
            observed=profile_name,
        )]

    target = profile.get("target") if isinstance(profile.get("target"), dict) else {}
    contract = profile.get("executionContract") if isinstance(profile.get("executionContract"), dict) else {}
    gpu = evidence.get("gpu") if isinstance(evidence.get("gpu"), dict) else {}
    problems: list[dict[str, Any]] = []

    gpu_name = str(gpu.get("name", ""))
    gpu_regex = str(target.get("gpuNameRegex", ".*"))
    if not gpu_name or re.search(gpu_regex, gpu_name, re.IGNORECASE) is None:
        problems.append(issue(
            "gpu_profile_mismatch",
            "GPU does not match the formal profile.",
            expected=gpu_regex,
            observed=gpu_name,
        ))
    if "warp" in gpu_name.lower() or "microsoft basic" in gpu_name.lower():
        problems.append(issue(
            "software_adapter_rejected",
            "WARP and software adapters are forbidden.",
            observed=gpu_name,
        ))

    memory = gpu.get("memoryTotalMiB")
    minimum_memory = target.get("minimumDedicatedVramMiB")
    maximum_memory = target.get("maximumDedicatedVramMiB")
    try:
        memory_valid = (
            memory is not None
            and (minimum_memory is None or float(memory) >= float(minimum_memory))
            and (maximum_memory is None or float(memory) <= float(maximum_memory))
        )
    except (TypeError, ValueError):
        memory_valid = False
    if not memory_valid:
        problems.append(issue(
            "vram_profile_mismatch",
            "Dedicated VRAM is outside the formal profile.",
            expected={"minimum": minimum_memory, "maximum": maximum_memory},
            observed=memory,
        ))

    physical = evidence.get("physicalCpuCoreCount")
    minimum_cores = target.get("minimumCpuPhysicalCoreCount")
    if physical is None or (minimum_cores is not None and int(physical) < int(minimum_cores)):
        problems.append(issue(
            "cpu_core_profile_mismatch",
            "Physical CPU core count is below the formal profile.",
            expected=minimum_cores,
            observed=physical,
        ))

    if bool(contract.get("acPowerRequired", False)) and evidence.get("acPower") is not True:
        problems.append(issue(
            "ac_power_required",
            "Connect AC power before formal measurement.",
            expected=True,
            observed=evidence.get("acPower"),
        ))

    scheme_pattern = str(contract.get("requiredPowerSchemeRegex", ""))
    observed_scheme = str(evidence.get("powerScheme", ""))
    if scheme_pattern and re.search(scheme_pattern, observed_scheme, re.IGNORECASE) is None:
        problems.append(issue(
            "power_scheme_mismatch",
            "Select High performance or Ultimate Performance.",
            expected=scheme_pattern,
            observed=observed_scheme,
        ))

    temperature = gpu.get("temperatureC")
    maximum_temperature = contract.get("maximumGpuTemperatureCelsius")
    if temperature is None or (
        maximum_temperature is not None and float(temperature) > float(maximum_temperature)
    ):
        problems.append(issue(
            "gpu_temperature_invalid",
            "GPU temperature is unavailable or above the formal limit.",
            expected=maximum_temperature,
            observed=temperature,
        ))
    return problems


def _patched_environment_builder(original):
    def build(
        case: Mapping[str, Any],
        paths: Mapping[str, Path],
        repo_root: Path,
        viewer_exe: Path,
        asset_manifest: Mapping[str, Any],
    ) -> dict[str, str]:
        env = original(case, paths, repo_root, viewer_exe, asset_manifest)
        method_hash = canonical_hash(case.get("method_settings", {}))
        aliases: dict[str, Any] = {
            "HAIRGPU_EXPERIMENT_CASE_ID": case.get("case_id", ""),
            "HAIRGPU_RENDER_WIDTH": case.get("width", 1920),
            "HAIRGPU_RENDER_HEIGHT": case.get("height", 1080),
            "HAIRGPU_GUIDE_COUNT": case.get("guide_strand_count", 4000),
            "HAIRGPU_REPEAT_INDEX": case.get("repetition_index", 0),
            "HAIRGPU_MEASUREMENT_MODE": case.get("measurement_mode", "performance"),
            "HAIRGPU_BUILD_CONFIGURATION": case.get("build_configuration", "Release"),
            "HAIRGPU_PLATFORM": case.get("platform", "x64"),
            "HAIRGPU_HARDWARE_PROFILE": case.get("hardware_profile", ""),
            "HAIRGPU_REQUESTED_METHOD_SETTINGS_SHA256": method_hash,
            "HAIRGPU_APPLIED_METHOD_SETTINGS_SHA256": method_hash,
            "HAIRGPU_CASE_JSON": str(paths.get("case", "")),
            "HAIRGPU_EXPERIMENT_CASE": str(paths.get("case", "")),
        }
        timeline = Path(str(case.get("scenario_timeline", "")))
        manifest = Path(str(case.get("asset_manifest", "")))
        shader_root = Path(str(case.get("shader_root", "")))
        if timeline.is_file():
            aliases["HAIRGPU_SCENARIO_TIMELINE_SHA256"] = sha256_file(timeline)
        if manifest.is_file():
            aliases["HAIRGPU_ASSET_MANIFEST_SHA256"] = sha256_file(manifest)
        if shader_root.exists():
            aliases["HAIRGPU_SHADER_BUNDLE_SHA256"] = final_run.sha256_tree(
                [shader_root],
                base_path=repo_root,
            )
        for key, value in aliases.items():
            if value not in (None, ""):
                env[key] = str(value)
        return env

    return build


def install_runtime_patches() -> None:
    if getattr(final_run._build_environment, "_hairgpu_gate_patched", False):
        return
    patched = _patched_environment_builder(final_run._build_environment)
    setattr(patched, "_hairgpu_gate_patched", True)
    final_run._build_environment = patched
    runtime_suite.execute_case = final_run.execute_case


def _recursive_values(value: Any, aliases: set[str]) -> list[Any]:
    found: list[Any] = []
    if isinstance(value, dict):
        for key, child in value.items():
            if str(key).lower() in aliases:
                found.append(child)
            found.extend(_recursive_values(child, aliases))
    elif isinstance(value, list):
        for child in value:
            found.extend(_recursive_values(child, aliases))
    return found


def _first_value(payload: Mapping[str, Any], *aliases: str) -> Any:
    values = _recursive_values(payload, {alias.lower() for alias in aliases})
    return values[0] if values else None


def validate_environment_evidence(
    config: Mapping[str, Any],
    environment: Mapping[str, Any],
    run_name: str,
) -> list[dict[str, Any]]:
    problems: list[dict[str, Any]] = []
    defaults = config.get("run_defaults") if isinstance(config.get("run_defaults"), dict) else {}
    pre = environment.get("pre") if isinstance(environment.get("pre"), dict) else {}
    post = environment.get("post") if isinstance(environment.get("post"), dict) else {}
    pre_gpu = pre.get("gpu") if isinstance(pre.get("gpu"), dict) else {}
    post_gpu = post.get("gpu") if isinstance(post.get("gpu"), dict) else {}

    maximum_temperature = float(defaults.get("maximum_gpu_temperature_c", 88.0))
    temperatures = [
        float(value)
        for value in (pre_gpu.get("temperatureC"), post_gpu.get("temperatureC"))
        if value is not None
    ]
    if len(temperatures) != 2 or max(temperatures) > maximum_temperature:
        problems.append(issue(
            "run_gpu_temperature_invalid",
            "Pre/post GPU temperature is missing or above the run limit.",
            expected=maximum_temperature,
            observed={"run": run_name, "temperatures": temperatures},
        ))
    else:
        allowed_spread = float(defaults.get("maximum_gpu_temperature_spread_c", 12.0))
        spread = max(temperatures) - min(temperatures)
        if spread > allowed_spread:
            problems.append(issue(
                "run_gpu_temperature_spread",
                "GPU temperature changed too much during the run.",
                expected=allowed_spread,
                observed={"run": run_name, "spread": spread},
            ))

    pre_clock = pre_gpu.get("graphicsClockMHz")
    post_clock = post_gpu.get("graphicsClockMHz")
    try:
        drop_ratio = max(0.0, float(pre_clock) - float(post_clock)) / max(float(pre_clock), 1.0)
    except (TypeError, ValueError):
        drop_ratio = None
    maximum_drop = float(defaults.get("maximum_gpu_clock_drop_ratio", 0.15))
    if drop_ratio is None or drop_ratio > maximum_drop:
        problems.append(issue(
            "run_gpu_clock_drop",
            "GPU clock evidence is missing or the drop ratio is too large.",
            expected=maximum_drop,
            observed={"run": run_name, "dropRatio": drop_ratio},
        ))
    return problems


def validate_case_evidence(
    config: Mapping[str, Any],
    output_dir: Path,
    run_name: str,
) -> list[dict[str, Any]]:
    paths = final_run.output_paths(output_dir, run_name)
    problems: list[dict[str, Any]] = []
    for key in ("case", "settings", "environment", "jsonl", "validation", "manifest", "complete"):
        if not paths[key].is_file():
            problems.append(issue(
                "run_artifact_missing",
                f"Required run artifact is missing: {key}",
                observed=str(paths[key]),
            ))
    if problems:
        return problems

    case = read_json(paths["case"])
    settings = read_json(paths["settings"])
    environment = read_json(paths["environment"])
    telemetry = read_jsonl(paths["jsonl"])
    if not telemetry:
        return [issue("telemetry_empty", "Run telemetry is empty.", observed=run_name)]

    scalar_settings = {
        "warmup_frames": ("warmupFrames",),
        "measurement_frames": ("measurementFrames",),
        "fixed_timestep_seconds": ("fixedTimeStepSeconds",),
        "width": ("renderWidth",),
        "height": ("renderHeight",),
        "display_strand_count": ("displayStrandCount",),
        "guide_strand_count": ("guideCount",),
        "particles_per_guide": ("particlesPerGuide",),
        "solver_iterations": ("solverIterations",),
        "vsync": ("vsync",),
        "present_sync_interval": ("presentSyncInterval",),
    }
    for case_key, aliases in scalar_settings.items():
        expected = case.get(case_key)
        observed = _first_value(settings, *aliases)
        if observed is None or not _equal(expected, observed):
            problems.append(issue(
                "observed_setting_mismatch",
                f"Requested/applied/observed mismatch: {case_key}",
                expected=expected,
                observed=observed,
            ))

    if _as_bool(settings.get("methodFlagsMatched")) is not True:
        problems.append(issue(
            "method_flags_not_matched",
            "Native requested/applied/observed method flags did not match.",
            expected=True,
            observed=settings.get("methodFlagsMatched"),
        ))
    if _as_bool(settings.get("realMeasurement")) is not True:
        problems.append(issue(
            "native_measurement_invalid",
            "The Viewer did not classify the run as complete valid real evidence.",
            expected=True,
            observed=settings.get("realMeasurement"),
        ))

    requested_flags = settings.get("requested")
    applied_flags = settings.get("applied")
    observed_flags = settings.get("observedFlags")
    if not (
        isinstance(requested_flags, dict)
        and requested_flags == applied_flags
        and applied_flags == observed_flags
    ):
        problems.append(issue(
            "method_flag_parity_failed",
            "Requested, applied and observed method flags must be identical.",
            expected=requested_flags,
            observed={"applied": applied_flags, "observed": observed_flags},
        ))

    method_hash = canonical_hash(case.get("method_settings", {}))
    method_hashes = [
        settings.get("requestedMethodSettingsSha256"),
        settings.get("appliedMethodSettingsSha256"),
        settings.get("observedMethodSettingsSha256"),
    ]
    if any(not isinstance(value, str) or HASH64.fullmatch(value) is None for value in method_hashes):
        problems.append(issue(
            "method_settings_hash_missing",
            "Requested/applied/observed method-setting hashes must all be valid SHA-256.",
            expected=method_hash,
            observed=method_hashes,
        ))
    elif any(str(value).lower() != method_hash.lower() for value in method_hashes):
        problems.append(issue(
            "method_settings_hash_mismatch",
            "Requested/applied/observed method-setting hashes differ.",
            expected=method_hash,
            observed=method_hashes,
        ))

    identity = {
        "commitSha": HASH40,
        "executableSha256": HASH64,
        "shaderBundleSha256": HASH64,
        "assetSha256": HASH64,
        "assetManifestSha256": HASH64,
        "scenarioTimelineSha256": HASH64,
    }
    for field, pattern in identity.items():
        observed = settings.get(field)
        if not isinstance(observed, str) or pattern.fullmatch(observed) is None:
            problems.append(issue(
                "identity_hash_missing",
                f"A valid identity value is required: {field}",
                observed=observed,
            ))

    observed_section = settings.get("observed") if isinstance(settings.get("observed"), dict) else {}
    if str(observed_section.get("adapterType", "")).lower() != "hardware":
        problems.append(issue(
            "non_hardware_adapter",
            "The Viewer must observe a hardware D3D11 adapter.",
            expected="hardware",
            observed=observed_section.get("adapterType"),
        ))

    last = telemetry[-1]
    telemetry_expected = {
        "totalStrandCount": int(case.get("display_strand_count", 0)),
        "guideCount": int(case.get("guide_strand_count", 0)),
        "followerCount": int(case.get("display_strand_count", 0)) - int(case.get("guide_strand_count", 0)),
        "seed": int(case.get("seed", 0)),
    }
    for field, expected in telemetry_expected.items():
        observed = last.get(field)
        if not _equal(expected, observed):
            problems.append(issue(
                "telemetry_observed_mismatch",
                f"Telemetry value mismatch: {field}",
                expected=expected,
                observed=observed,
            ))

    for frame_index, row in enumerate(telemetry):
        for field in (
            "syntheticMetricCount",
            "sameFrameReadbackCount",
            "nanStateCount",
            "infiniteStateCount",
            "fatalFailure",
            "scenarioAssertionFailureCount",
        ):
            try:
                value = int(float(row.get(field, 0) or 0))
            except (TypeError, ValueError):
                value = -1
            if value != 0:
                problems.append(issue(
                    "runtime_integrity_nonzero",
                    f"{field} must remain zero.",
                    expected=0,
                    observed={"frame": frame_index, "value": row.get(field)},
                ))
                return problems

    problems.extend(validate_environment_evidence(config, environment, run_name))
    return problems


def validate_determinism(config: Mapping[str, Any], output_dir: Path) -> list[dict[str, Any]]:
    problems: list[dict[str, Any]] = []
    for replay in config.get("determinism_replays", []):
        if not isinstance(replay, dict):
            continue
        replay_id = str(replay.get("id", ""))
        repetitions = int(replay.get("repetitions", 1))
        observed: list[tuple[int, int, int]] = []
        for index in range(repetitions):
            run_name = f"{replay_id}_r{index:02d}"
            path = final_run.output_paths(output_dir, run_name)["jsonl"]
            if not path.is_file():
                problems.append(issue(
                    "determinism_run_missing",
                    "Determinism replay output is missing.",
                    observed=run_name,
                ))
                continue
            rows = read_jsonl(path)
            if not rows:
                problems.append(issue(
                    "determinism_telemetry_empty",
                    "Determinism replay telemetry is empty.",
                    observed=run_name,
                ))
                continue
            last = rows[-1]
            hashes = tuple(
                int(float(last.get(field, 0) or 0))
                for field in (
                    "stateSequenceHash",
                    "compactionSequenceHash",
                    "finalPositionHash",
                )
            )
            mismatch_count = sum(
                int(float(row.get("determinismMismatchCount", 0) or 0))
                for row in rows
            )
            if 0 in hashes or mismatch_count != 0:
                problems.append(issue(
                    "determinism_hash_invalid",
                    "Determinism hashes must be non-zero and mismatch count must be zero.",
                    observed={
                        "run": run_name,
                        "hashes": hashes,
                        "mismatchCount": mismatch_count,
                    },
                ))
            observed.append(hashes)
        if observed and len(set(observed)) != 1:
            problems.append(issue(
                "determinism_replay_mismatch",
                "All repetitions must produce identical state, compaction and final-position hashes.",
                expected="all equal",
                observed=observed,
            ))
    return problems


def validate_long_runs(config: Mapping[str, Any], output_dir: Path) -> list[dict[str, Any]]:
    metrics_path = output_dir / "suite_metrics.json"
    if not metrics_path.is_file():
        return [issue("suite_metrics_missing", "suite_metrics.json is required for long-run validation.")]
    metrics_payload = read_json(metrics_path)
    rows_by_name = {
        str(row.get("run_name")): row
        for row in metrics_payload.get("rows", [])
        if isinstance(row, dict)
    }
    selected_long_runs = {
        name: row
        for name, row in rows_by_name.items()
        if str(row.get("run_kind", "")) == "long_run"
    }
    if not selected_long_runs:
        return [issue("long_run_not_selected", "No selected long-run result was found.")]

    gates = config.get("long_run_gates") if isinstance(config.get("long_run_gates"), dict) else {}
    problems: list[dict[str, Any]] = []
    for run_name, metric in selected_long_runs.items():
        for metric_key, gate_key in (
            ("peak_process_private_mib", "max_peak_process_private_mib"),
            ("process_private_slope_mib_per_min", "max_process_private_slope_mib_per_min"),
            ("peak_dxgi_local_usage_mib", "max_peak_dxgi_local_usage_mib"),
            ("dxgi_local_usage_slope_mib_per_min", "max_dxgi_local_usage_slope_mib_per_min"),
        ):
            observed = metric.get(metric_key)
            limit = gates.get(gate_key)
            try:
                passed = observed is not None and limit is not None and float(observed) <= float(limit)
            except (TypeError, ValueError):
                passed = False
            if not passed:
                problems.append(issue(
                    "long_run_gate_failed",
                    f"Long-run gate failed: {metric_key}",
                    expected=limit,
                    observed={"run": run_name, "value": observed},
                ))

        telemetry_path = final_run.output_paths(output_dir, run_name)["jsonl"]
        if not telemetry_path.is_file():
            problems.append(issue(
                "long_run_telemetry_missing",
                "Long-run telemetry is missing.",
                observed=run_name,
            ))
            continue
        telemetry = read_jsonl(telemetry_path)
        frames = [
            float(row["frameMilliseconds"])
            for row in telemetry
            if row.get("frameMilliseconds") not in (None, "")
        ]
        window = max(1, len(frames) // 10)
        first_p99 = percentile(frames[:window], 99.0)
        last_p99 = percentile(frames[-window:], 99.0)
        ratio = last_p99 / first_p99 if first_p99 and last_p99 else None
        ratio_limit = float(gates.get("max_p99_degradation_ratio", 1.1))
        if ratio is None or ratio > ratio_limit:
            problems.append(issue(
                "long_run_p99_degradation",
                "End-of-run p99 degraded beyond the allowed ratio.",
                expected=ratio_limit,
                observed={
                    "run": run_name,
                    "ratio": ratio,
                    "firstP99": first_p99,
                    "lastP99": last_p99,
                },
            ))

        gpu_bit = final_run.AVAILABILITY_BITS.get("GpuTiming", 0)
        available: list[int] = []
        for row in telemetry:
            mask = int(float(row.get("availabilityMask", 0) or 0)) | int(
                float(row.get("metricAvailabilityMask", 0) or 0)
            )
            available.append(1 if (mask & gpu_bit) == gpu_bit else 0)
        first_coverage = sum(available[:window]) / window if available else 0.0
        last_coverage = sum(available[-window:]) / window if available else 0.0
        minimum_coverage = float(
            config.get("run_defaults", {}).get("minimum_gpu_timing_coverage", 0.99)
        )
        if last_coverage < minimum_coverage or last_coverage + 1.0e-12 < first_coverage:
            problems.append(issue(
                "long_run_query_resolution_degraded",
                "Delayed GPU-query coverage degraded during the long run.",
                expected={"minimum": minimum_coverage, "notBelowInitial": first_coverage},
                observed={"run": run_name, "lastCoverage": last_coverage},
            ))
    return problems


def validate_suite_outputs(
    config_path: Path,
    output_dir: Path,
    run_kind: str,
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    config = read_json(config_path)
    summary_path = output_dir / "suite_summary.json"
    metrics_path = output_dir / "suite_metrics.json"
    if not summary_path.is_file() or not metrics_path.is_file():
        problems = [issue(
            "suite_output_missing",
            "Suite summary or metrics file is missing.",
            observed=str(output_dir),
        )]
        return problems, {
            "gateVersion": GATE_VERSION,
            "runKind": run_kind,
            "passed": False,
            "issueCount": len(problems),
            "issues": problems,
        }

    summary = read_json(summary_path)
    metrics = read_json(metrics_path)
    rows = [row for row in metrics.get("rows", []) if isinstance(row, dict)]
    selected = int(summary.get("selectedRunCount", -1))
    completed = int(summary.get("completedRowCount", -1))
    problems: list[dict[str, Any]] = []
    if selected <= 0 or completed != selected or len(rows) != selected:
        problems.append(issue(
            "suite_incomplete",
            "Every selected run must produce exactly one metric row.",
            expected=selected,
            observed={"completed": completed, "metrics": len(rows)},
        ))

    for row in rows:
        run_name = str(row.get("run_name", ""))
        if int(row.get("valid", 0)) != 1:
            problems.append(issue(
                "run_invalid",
                "A run failed integrity validation.",
                observed=run_name,
            ))
        if int(row.get("quality_gate_passed", 0)) != 1:
            problems.append(issue(
                "quality_gate_failed",
                "A run failed its configured quality gate.",
                observed=run_name,
            ))
        problems.extend(validate_case_evidence(config, output_dir, run_name))

    if run_kind in {"determinism", "all"}:
        problems.extend(validate_determinism(config, output_dir))
    if run_kind in {"long-run", "all"}:
        problems.extend(validate_long_runs(config, output_dir))

    report = {
        "gateVersion": GATE_VERSION,
        "runKind": run_kind,
        "outputDirectory": str(output_dir),
        "selectedRunCount": selected,
        "completedRunCount": completed,
        "issueCount": len(problems),
        "passed": not problems,
        "issues": problems,
        "generatedUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    return problems, report


def write_gate_report(output_dir: Path, report: Mapping[str, Any]) -> None:
    write_json(output_dir / "formal_environment_gate.json", report)
    lines = [
        "# Formal Environment Gate",
        "",
        f"- Gate version: `{report.get('gateVersion')}`",
        f"- Run kind: `{report.get('runKind')}`",
        f"- Passed: `{report.get('passed')}`",
        f"- Issues: `{report.get('issueCount')}`",
        "",
    ]
    for entry in report.get("issues", []):
        if isinstance(entry, dict):
            lines.append(
                f"- **{entry.get('code')}**: {entry.get('message')} "
                f"(observed: `{entry.get('observed')}`)"
            )
    (output_dir / "formal_environment_gate.md").write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
    )
    marker = output_dir / "FORMAL_ENVIRONMENT_COMPLETE.txt"
    if report.get("passed"):
        marker.write_text(
            f"PASS\nGateVersion={GATE_VERSION}\nGeneratedUtc={report.get('generatedUtc')}\n",
            encoding="utf-8",
        )
    else:
        marker.unlink(missing_ok=True)


def preflight(config_path: Path, output_dir: Path, skip_hardware: bool) -> list[dict[str, Any]]:
    repo_root = Path(__file__).resolve().parents[2]
    problems = validate_static_contract(config_path, repo_root)
    evidence = collect_host_evidence(repo_root)
    problems.extend(validate_host_contract(config_path, evidence, skip_hardware))
    write_json(
        output_dir / "formal_host_preflight.json",
        {"evidence": evidence, "issues": problems, "passed": not problems},
    )
    return problems


def self_test() -> int:
    assert _scenario_family("S01_Idle") == "S01"
    assert _scenario_family("S18_FarCamera100k") == "S18"
    assert _equal(1920, "1920")
    assert _equal(False, "0")
    assert _equal(1.0 / 60.0, 0.016666666666666666)
    assert canonical_hash({"b": 2, "a": 1}) == canonical_hash({"a": 1, "b": 2})
    assert percentile([1.0, 2.0, 3.0], 50.0) == 2.0
    print("hairgpu_formal_gate self-test: PASS")
    return EXIT_PASS


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the strict HairSystem formal environment gate."
    )
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--config")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--output")
    parser.add_argument("--run-kind", choices=sorted(runtime_suite.RUN_KINDS), default="formal")
    parser.add_argument("--only-method", action="append", default=[])
    parser.add_argument("--only-scenario", action="append", default=[])
    parser.add_argument("--only-seed", action="append", type=int, default=[])
    parser.add_argument("--only-run-name", action="append", default=[])
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--fail-policy", choices=("continue", "stop"), default="continue")
    parser.add_argument("--skip-hardware-contract", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.config:
        parser.error("--config is required")

    config_path = Path(args.config).resolve()
    output_dir = output_directory(
        config_path,
        Path(args.output) if args.output else None,
    )
    output_dir.mkdir(parents=True, exist_ok=True)
    problems = preflight(config_path, output_dir, args.skip_hardware_contract)
    if problems:
        report = {
            "gateVersion": GATE_VERSION,
            "runKind": args.run_kind,
            "passed": False,
            "issueCount": len(problems),
            "issues": problems,
            "generatedUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        write_gate_report(output_dir, report)
        print(json.dumps(report, ensure_ascii=False, indent=2), file=sys.stderr)
        return EXIT_CONTRACT

    install_runtime_patches()
    suite_exit = runtime_suite.execute_suite(
        config_path,
        Path(args.viewer_exe) if args.viewer_exe else None,
        Path(args.output) if args.output else None,
        args.run_kind,
        {final_run.normalize_method_code(value) for value in args.only_method},
        set(args.only_scenario),
        set(args.only_seed),
        set(args.only_run_name),
        args.dry_run,
        args.resume,
        args.fail_policy,
    )
    if args.dry_run:
        return suite_exit

    output_problems, report = validate_suite_outputs(
        config_path,
        output_dir,
        args.run_kind,
    )
    report["suiteExitCode"] = suite_exit
    if suite_exit not in {EXIT_PASS, EXIT_QUALITY}:
        output_problems.append(issue(
            "suite_runtime_failure",
            "The underlying runtime suite failed.",
            observed=suite_exit,
        ))
        report["issues"] = output_problems
        report["issueCount"] = len(output_problems)
        report["passed"] = False
    write_gate_report(output_dir, report)
    if output_problems:
        print(json.dumps(report, ensure_ascii=False, indent=2), file=sys.stderr)
        return EXIT_INTEGRITY
    return EXIT_PASS


if __name__ == "__main__":
    raise SystemExit(main())
