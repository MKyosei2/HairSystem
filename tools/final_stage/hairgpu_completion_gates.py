from __future__ import annotations

"""Final completion gates for the HairSystem formal test environment.

This validator is intentionally independent from the runtime launcher. It consumes
artifacts produced by hairgpu_runtime_suite.py and converts the previously
record-only requirements into hard pass/fail gates:

* frozen asset integrity and portability
* requested/applied/observed settings parity
* hardware profile and power/thermal contract
* deterministic replay hash agreement
* long-run memory/VRAM/frame-time/query stability
* suite artifact completeness

Exit codes:
  0: complete and valid
  2: formal contract invalid
  4: output integrity failure
"""

import argparse
import csv
import hashlib
import json
import math
import os
import re
import statistics
import sys
from pathlib import Path
from typing import Any, Iterable, Mapping

EXIT_COMPLETE_VALID = 0
EXIT_FORMAL_CONTRACT_INVALID = 2
EXIT_OUTPUT_INTEGRITY_FAILURE = 4
SCHEMA_VERSION = "hairgpu-test-environment-completion-v1"


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected JSON object: {path}")
    return value


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(dict(payload), ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    os.replace(temporary, path)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def issue(code: str, message: str, *, expected: Any = None, observed: Any = None) -> dict[str, Any]:
    return {
        "code": code,
        "message": message,
        "expected": expected,
        "observed": observed,
    }


def as_float(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError, OverflowError):
        return None
    return number if math.isfinite(number) else None


def as_int(value: Any, default: int = 0) -> int:
    try:
        return int(float(value))
    except (TypeError, ValueError, OverflowError):
        return default


def flatten(payload: Mapping[str, Any], prefix: str = "") -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in payload.items():
        name = f"{prefix}.{key}" if prefix else str(key)
        if isinstance(value, Mapping):
            result.update(flatten(value, name))
        else:
            result[name] = value
    return result


def normalized_scalar(value: Any) -> Any:
    if isinstance(value, bool):
        return bool(value)
    number = as_float(value)
    if number is not None:
        return round(number, 9)
    return str(value).strip()


def first_mapping(payload: Mapping[str, Any], names: Iterable[str]) -> dict[str, Any]:
    for name in names:
        value = payload.get(name)
        if isinstance(value, Mapping):
            return dict(value)
    return {}


def validate_frozen_bundle(config: Mapping[str, Any], config_path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    problems: list[dict[str, Any]] = []
    source = config.get("source") if isinstance(config.get("source"), Mapping) else {}
    raw = Path(str(source.get("asset_manifest", "")))
    manifest_path = raw if raw.is_absolute() else (config_path.parent / raw)
    manifest_path = manifest_path.resolve()
    if not manifest_path.is_file():
        return {}, [issue("E_FROZEN_MANIFEST_MISSING", "Frozen asset manifest is missing.", observed=str(manifest_path))]
    manifest = read_json(manifest_path)
    if manifest.get("freezeStatus") != "FROZEN":
        problems.append(issue("E_FROZEN_STATUS", "Frozen asset manifest is not frozen.", expected="FROZEN", observed=manifest.get("freezeStatus")))
    if manifest.get("manifestVersion") != "novelty01-100k-v1.6":
        problems.append(issue("E_FROZEN_VERSION", "Frozen asset manifest version mismatch.", expected="novelty01-100k-v1.6", observed=manifest.get("manifestVersion")))
    entries = manifest.get("bundleFiles")
    if not isinstance(entries, list) or len(entries) != 3:
        problems.append(issue("E_FROZEN_FILES", "Frozen bundle must contain exactly three canonical members.", expected=3, observed=len(entries) if isinstance(entries, list) else None))
        return manifest, problems
    canonical: list[dict[str, Any]] = []
    root = manifest_path.parent.resolve()
    for entry in entries:
        if not isinstance(entry, Mapping):
            problems.append(issue("E_FROZEN_ENTRY", "Frozen bundle entry is not an object."))
            continue
        relative = Path(str(entry.get("path", "")))
        if relative.is_absolute() or ".." in relative.parts:
            problems.append(issue("E_FROZEN_PATH", "Frozen bundle path is not portable.", observed=str(relative)))
            continue
        member = (root / relative).resolve()
        try:
            member.relative_to(root)
        except ValueError:
            problems.append(issue("E_FROZEN_ESCAPE", "Frozen bundle member escapes its root.", observed=str(member)))
            continue
        if not member.is_file():
            problems.append(issue("E_FROZEN_MEMBER_MISSING", "Frozen bundle member is missing.", observed=str(member)))
            continue
        actual = {"path": relative.as_posix(), "sizeBytes": member.stat().st_size, "sha256": sha256_file(member)}
        canonical.append(actual)
        if actual["sizeBytes"] != as_int(entry.get("sizeBytes"), -1):
            problems.append(issue("E_FROZEN_SIZE", "Frozen member size mismatch.", expected=entry.get("sizeBytes"), observed=actual["sizeBytes"]))
        if actual["sha256"].lower() != str(entry.get("sha256", "")).lower():
            problems.append(issue("E_FROZEN_HASH", "Frozen member hash mismatch.", expected=entry.get("sha256"), observed=actual["sha256"]))
    encoded = json.dumps(sorted(canonical, key=lambda item: item["path"]), ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    bundle_hash = hashlib.sha256(encoded).hexdigest()
    if bundle_hash.lower() != str(manifest.get("bundleSha256", "")).lower():
        problems.append(issue("E_FROZEN_BUNDLE_HASH", "Frozen bundle aggregate hash mismatch.", expected=manifest.get("bundleSha256"), observed=bundle_hash))
    return manifest, problems


def settings_sections(payload: Mapping[str, Any]) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]]:
    requested = first_mapping(payload, ("requested", "requestedSettings", "requested_settings", "request"))
    applied = first_mapping(payload, ("applied", "appliedSettings", "applied_settings"))
    observed = first_mapping(payload, ("observed", "observedSettings", "observed_settings", "runtime"))
    return requested, applied, observed


def validate_settings_file(path: Path) -> list[dict[str, Any]]:
    payload = read_json(path)
    requested, applied, observed = settings_sections(payload)
    problems: list[dict[str, Any]] = []
    if not requested or not applied or not observed:
        return [issue("E_SETTINGS_SECTIONS", "Settings JSON must contain requested, applied and observed sections.", observed=str(path))]
    request_flat = flatten(requested)
    applied_flat = flatten(applied)
    observed_flat = flatten(observed)
    required_tokens = (
        "width", "height", "fixed", "timestep", "vsync", "present", "strand", "guide",
        "follower", "particle", "solver", "method", "asset", "shader", "executable", "commit",
    )
    comparable = {
        key for key in request_flat
        if any(token in key.lower() for token in required_tokens)
    }
    if not comparable:
        problems.append(issue("E_SETTINGS_EMPTY_CONTRACT", "Settings JSON has no comparable formal fields.", observed=str(path)))
        return problems
    for key in sorted(comparable):
        expected = normalized_scalar(request_flat.get(key))
        if key not in applied_flat:
            problems.append(issue("E_SETTINGS_APPLIED_MISSING", f"Applied setting is missing: {key}", expected=expected))
            continue
        if key not in observed_flat:
            problems.append(issue("E_SETTINGS_OBSERVED_MISSING", f"Observed setting is missing: {key}", expected=expected))
            continue
        applied_value = normalized_scalar(applied_flat.get(key))
        observed_value = normalized_scalar(observed_flat.get(key))
        if expected != applied_value:
            problems.append(issue("E_SETTINGS_REQUEST_APPLY", f"Requested/applied mismatch: {key}", expected=expected, observed=applied_value))
        if applied_value != observed_value:
            problems.append(issue("E_SETTINGS_APPLY_OBSERVE", f"Applied/observed mismatch: {key}", expected=applied_value, observed=observed_value))
    return problems


def collect_run_files(output_dir: Path, suffix: str) -> list[Path]:
    return sorted(path for path in output_dir.glob(f"*{suffix}") if path.is_file())


def validate_settings(output_dir: Path) -> list[dict[str, Any]]:
    files = collect_run_files(output_dir, "_settings.json")
    if not files:
        return [issue("E_SETTINGS_OUTPUT_NONE", "No settings outputs were found.", observed=str(output_dir))]
    problems: list[dict[str, Any]] = []
    for path in files:
        for item in validate_settings_file(path):
            item["run"] = path.name.removesuffix("_settings.json")
            problems.append(item)
    return problems


def load_hardware_profiles(config: Mapping[str, Any], config_path: Path) -> dict[str, Any]:
    raw = Path(str(config.get("hardware_profiles", "")))
    path = raw if raw.is_absolute() else config_path.parent / raw
    return read_json(path.resolve())


def environment_documents(output_dir: Path) -> list[tuple[str, dict[str, Any]]]:
    result: list[tuple[str, dict[str, Any]]] = []
    for path in collect_run_files(output_dir, "_environment.json"):
        result.append((path.name.removesuffix("_environment.json"), read_json(path)))
    return result


def validate_hardware(config: Mapping[str, Any], config_path: Path, output_dir: Path) -> list[dict[str, Any]]:
    documents = environment_documents(output_dir)
    if not documents:
        return [issue("E_HARDWARE_OUTPUT_NONE", "No environment outputs were found.", observed=str(output_dir))]
    profile_payload = load_hardware_profiles(config, config_path)
    profiles = profile_payload.get("profiles") if isinstance(profile_payload.get("profiles"), Mapping) else {}
    defaults = config.get("run_defaults") if isinstance(config.get("run_defaults"), Mapping) else {}
    profile_name = str(defaults.get("hardware_profile", ""))
    profile = profiles.get(profile_name) if isinstance(profiles, Mapping) else None
    if not isinstance(profile, Mapping):
        return [issue("E_HARDWARE_PROFILE", "Configured hardware profile was not found.", observed=profile_name)]
    target = profile.get("target") if isinstance(profile.get("target"), Mapping) else {}
    contract = profile.get("executionContract") if isinstance(profile.get("executionContract"), Mapping) else {}
    name_pattern = re.compile(str(target.get("gpuNameRegex", ".*")), re.IGNORECASE)
    power_pattern = re.compile(str(contract.get("requiredPowerSchemeRegex", ".*")), re.IGNORECASE)
    problems: list[dict[str, Any]] = []
    for run_name, payload in documents:
        pre = payload.get("pre") if isinstance(payload.get("pre"), Mapping) else {}
        post = payload.get("post") if isinstance(payload.get("post"), Mapping) else {}
        process = payload.get("process") if isinstance(payload.get("process"), Mapping) else {}
        pre_gpu = pre.get("gpu") if isinstance(pre.get("gpu"), Mapping) else {}
        post_gpu = post.get("gpu") if isinstance(post.get("gpu"), Mapping) else {}
        gpu_name = str(pre_gpu.get("name", ""))
        if target.get("gpuNameRegex") and not name_pattern.search(gpu_name):
            problems.append(issue("E_GPU_NAME", "GPU does not match the formal profile.", expected=target.get("gpuNameRegex"), observed=gpu_name) | {"run": run_name})
        vram = as_float(pre_gpu.get("memoryTotalMiB"))
        minimum_vram = as_float(target.get("minimumDedicatedVramMiB"))
        maximum_vram = as_float(target.get("maximumDedicatedVramMiB"))
        if minimum_vram is not None and (vram is None or vram < minimum_vram):
            problems.append(issue("E_GPU_VRAM_MIN", "GPU VRAM is below the profile minimum.", expected=minimum_vram, observed=vram) | {"run": run_name})
        if maximum_vram is not None and (vram is None or vram > maximum_vram):
            problems.append(issue("E_GPU_VRAM_MAX", "GPU VRAM is above the strict reference range.", expected=maximum_vram, observed=vram) | {"run": run_name})
        scheme = str((pre.get("power") if isinstance(pre.get("power"), Mapping) else {}).get("activeScheme", ""))
        if contract.get("requiredPowerSchemeRegex") and not power_pattern.search(scheme):
            problems.append(issue("E_POWER_SCHEME", "Windows power scheme does not satisfy the profile.", expected=contract.get("requiredPowerSchemeRegex"), observed=scheme) | {"run": run_name})
        temperatures = [as_float(pre_gpu.get("temperatureC")), as_float(post_gpu.get("temperatureC"))]
        clean_temperatures = [value for value in temperatures if value is not None]
        maximum_temperature = as_float(contract.get("maximumGpuTemperatureCelsius"))
        maximum_spread = as_float(contract.get("maximumGpuTemperatureSpreadCelsius"))
        if maximum_temperature is not None and (not clean_temperatures or max(clean_temperatures) > maximum_temperature):
            problems.append(issue("E_GPU_TEMPERATURE", "GPU temperature exceeded the profile limit or was unavailable.", expected=maximum_temperature, observed=clean_temperatures) | {"run": run_name})
        if maximum_spread is not None and len(clean_temperatures) >= 2 and max(clean_temperatures) - min(clean_temperatures) > maximum_spread:
            problems.append(issue("E_GPU_TEMPERATURE_SPREAD", "GPU temperature spread exceeded the profile limit.", expected=maximum_spread, observed=max(clean_temperatures) - min(clean_temperatures)) | {"run": run_name})
        clocks = [as_float(pre_gpu.get("graphicsClockMHz")), as_float(post_gpu.get("graphicsClockMHz"))]
        if all(value is not None for value in clocks) and clocks[0] and clocks[0] > 0:
            drop = max(0.0, (clocks[0] - clocks[1]) / clocks[0])
            maximum_drop = as_float(contract.get("maximumGpuClockDropRatio"))
            if maximum_drop is not None and drop > maximum_drop:
                problems.append(issue("E_GPU_CLOCK_DROP", "GPU clock drop exceeded the profile limit.", expected=maximum_drop, observed=drop) | {"run": run_name})
        if as_float(process.get("peakProcessPrivateMiB")) is None:
            problems.append(issue("E_PROCESS_MEMORY_MISSING", "Process private memory measurement is unavailable.") | {"run": run_name})
        if as_float(process.get("peakDxgiLocalUsageMiB")) is None:
            problems.append(issue("E_GPU_MEMORY_MISSING", "Per-process GPU memory measurement is unavailable.", observed=process.get("gpuProbe")) | {"run": run_name})
    return problems


def summary_for_run(output_dir: Path, run_name: str) -> dict[str, Any]:
    validation = output_dir / f"{run_name}_validation.json"
    summary = output_dir / f"{run_name}_summary.json"
    if validation.is_file():
        return read_json(validation)
    if summary.is_file():
        return read_json(summary)
    return {}


def extract_hashes(output_dir: Path, run_name: str) -> dict[str, str]:
    summary = summary_for_run(output_dir, run_name)
    aggregate = summary.get("aggregate") if isinstance(summary.get("aggregate"), Mapping) else summary
    result: dict[str, str] = {}
    aliases = {
        "stateSequenceHash": ("stateSequenceHash", "state_sequence_hash"),
        "compactionSequenceHash": ("compactionSequenceHash", "compaction_sequence_hash"),
        "finalPositionHash": ("finalPositionHash", "final_position_hash"),
    }
    flat = flatten(aggregate if isinstance(aggregate, Mapping) else {})
    for canonical, names in aliases.items():
        for key, value in flat.items():
            if any(key.endswith(name) for name in names) and value not in (None, ""):
                result[canonical] = str(value)
                break
    return result


def validate_determinism(config: Mapping[str, Any], output_dir: Path) -> list[dict[str, Any]]:
    problems: list[dict[str, Any]] = []
    for replay in config.get("determinism_replays", []):
        if not isinstance(replay, Mapping):
            continue
        replay_id = str(replay.get("id", ""))
        repetitions = as_int(replay.get("repetitions"), 1)
        observed = [extract_hashes(output_dir, f"{replay_id}_r{index:02d}") for index in range(repetitions)]
        for index, hashes in enumerate(observed):
            if len(hashes) != 3:
                problems.append(issue("E_DETERMINISM_HASH_MISSING", "Determinism replay does not contain all required hashes.", expected=["stateSequenceHash", "compactionSequenceHash", "finalPositionHash"], observed=hashes) | {"run": f"{replay_id}_r{index:02d}"})
        for field in ("stateSequenceHash", "compactionSequenceHash", "finalPositionHash"):
            values = [item.get(field, "") for item in observed]
            if any(not value for value in values) or len(set(values)) != 1:
                problems.append(issue("E_DETERMINISM_MISMATCH", f"Determinism hash mismatch: {field}", expected="all equal", observed=values) | {"replay": replay_id})
    return problems


def read_frame_times(path: Path) -> list[float]:
    values: list[float] = []
    if not path.is_file():
        return values
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        for row in csv.DictReader(handle):
            value = as_float(row.get("frameMilliseconds"))
            if value is not None:
                values.append(value)
    return values


def percentile(values: list[float], p: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * p / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def validate_long_runs(config: Mapping[str, Any], output_dir: Path) -> list[dict[str, Any]]:
    gates = config.get("long_run_gates") if isinstance(config.get("long_run_gates"), Mapping) else {}
    problems: list[dict[str, Any]] = []
    for entry in config.get("long_runs", []):
        if not isinstance(entry, Mapping):
            continue
        run_name = str(entry.get("id", ""))
        validation = summary_for_run(output_dir, run_name)
        if not validation:
            problems.append(issue("E_LONG_RUN_MISSING", "Long-run validation output is missing.", observed=run_name))
            continue
        process = validation.get("processMemory") if isinstance(validation.get("processMemory"), Mapping) else {}
        checks = (
            ("processPrivateSlopeMiBPerMin", "max_process_private_slope_mib_per_min"),
            ("dxgiLocalUsageSlopeMiBPerMin", "max_dxgi_local_usage_slope_mib_per_min"),
            ("peakProcessPrivateMiB", "max_peak_process_private_mib"),
            ("peakDxgiLocalUsageMiB", "max_peak_dxgi_local_usage_mib"),
        )
        for observed_key, gate_key in checks:
            limit = as_float(gates.get(gate_key))
            observed = as_float(process.get(observed_key))
            if limit is not None and (observed is None or observed > limit):
                problems.append(issue("E_LONG_RUN_GATE", f"Long-run gate failed: {observed_key}", expected=limit, observed=observed) | {"run": run_name})
        frames = read_frame_times(output_dir / f"{run_name}_frames.csv")
        if not frames:
            problems.append(issue("E_LONG_RUN_FRAMES", "Long-run frame telemetry is missing.", observed=run_name))
            continue
        segment = max(1, len(frames) // 10)
        first_p99 = percentile(frames[:segment], 99.0)
        last_p99 = percentile(frames[-segment:], 99.0)
        limit = as_float(gates.get("max_p99_degradation_ratio"))
        ratio = last_p99 / first_p99 if first_p99 and last_p99 else None
        if limit is not None and (ratio is None or ratio > limit):
            problems.append(issue("E_LONG_RUN_P99_DEGRADATION", "Long-run p99 degradation exceeded the limit.", expected=limit, observed=ratio) | {"run": run_name})
        unresolved = validation.get("unresolvedQuerySlope")
        unresolved_limit = as_float(gates.get("max_query_unresolved_slope"))
        if unresolved_limit is not None:
            observed_unresolved = as_float(unresolved)
            if observed_unresolved is None or observed_unresolved > unresolved_limit:
                problems.append(issue("E_LONG_RUN_QUERY_SLOPE", "Unresolved GPU query slope is unavailable or above the limit.", expected=unresolved_limit, observed=observed_unresolved) | {"run": run_name})
    return problems


def validate_suite_artifacts(output_dir: Path) -> list[dict[str, Any]]:
    required = (
        "suite_preflight.json", "suite_dry_run.json", "suite_metrics.csv",
        "suite_metrics.json", "suite_summary.json", "final_runtime_suite.sqlite",
        "final_runtime_suite_artifacts.zip",
    )
    problems = [issue("E_SUITE_ARTIFACT", "Required suite artifact is missing.", observed=name) for name in required if not (output_dir / name).is_file()]
    summary_path = output_dir / "suite_summary.json"
    if summary_path.is_file():
        summary = read_json(summary_path)
        selected = as_int(summary.get("selectedRunCount"), -1)
        completed = as_int(summary.get("completedRowCount"), -2)
        valid = as_int(summary.get("validRunCount"), -3)
        if selected <= 0 or completed != selected or valid != selected:
            problems.append(issue("E_SUITE_INCOMPLETE", "Suite did not complete every selected run as valid.", expected={"completed": selected, "valid": selected}, observed={"completed": completed, "valid": valid}))
    return problems


def write_markdown(path: Path, report: Mapping[str, Any]) -> None:
    lines = [
        "# HairSystem Test Environment Completion Report",
        "",
        f"- Status: **{report['status']}**",
        f"- Schema: `{report['schemaVersion']}`",
        f"- Issue count: {len(report['issues'])}",
        "",
    ]
    if report["issues"]:
        lines.extend(["## Issues", ""])
        for item in report["issues"]:
            lines.append(f"- `{item.get('code')}` {item.get('message')} — expected={item.get('expected')!r}, observed={item.get('observed')!r}")
    else:
        lines.extend(["All completion gates passed.", ""])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def execute(config_path: Path, output_dir: Path, mode: str) -> int:
    config_path = config_path.resolve()
    output_dir = output_dir.resolve()
    config = read_json(config_path)
    issues: list[dict[str, Any]] = []
    frozen_manifest, frozen_issues = validate_frozen_bundle(config, config_path)
    issues.extend(frozen_issues)
    issues.extend(validate_suite_artifacts(output_dir))
    issues.extend(validate_settings(output_dir))
    issues.extend(validate_hardware(config, config_path, output_dir))
    if mode in {"determinism", "all"}:
        issues.extend(validate_determinism(config, output_dir))
    if mode in {"long-run", "all"}:
        issues.extend(validate_long_runs(config, output_dir))
    report = {
        "schemaVersion": SCHEMA_VERSION,
        "status": "PASS" if not issues else "FAIL",
        "mode": mode,
        "configPath": str(config_path),
        "outputDirectory": str(output_dir),
        "frozenAssetBundleSha256": frozen_manifest.get("bundleSha256"),
        "issues": issues,
    }
    write_json(output_dir / "TEST_ENVIRONMENT_COMPLETION.json", report)
    write_markdown(output_dir / "TEST_ENVIRONMENT_COMPLETION.md", report)
    if issues:
        print(json.dumps(report, ensure_ascii=False, indent=2), file=sys.stderr)
        return EXIT_OUTPUT_INTEGRITY_FAILURE
    print(f"Test environment completion gates: PASS ({mode})")
    return EXIT_COMPLETE_VALID


def self_test() -> int:
    if normalized_scalar("1") != normalized_scalar(1):
        return 1
    if percentile([1.0, 2.0, 3.0], 50.0) != 2.0:
        return 1
    if as_int("4") != 4:
        return 1
    print("hairgpu_completion_gates self-test: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Apply final HairSystem test-environment completion gates.")
    parser.add_argument("--config")
    parser.add_argument("--output")
    parser.add_argument("--mode", choices=("formal", "determinism", "long-run", "all"), default="formal")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.config or not args.output:
        parser.error("--config and --output are required")
    try:
        return execute(Path(args.config), Path(args.output), args.mode)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Completion gate configuration error: {exc}", file=sys.stderr)
        return EXIT_FORMAL_CONTRACT_INVALID


if __name__ == "__main__":
    raise SystemExit(main())
