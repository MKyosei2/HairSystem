from __future__ import annotations

"""Strict Cartesian orchestrator for the HairSystem Novelty01 formal suite."""

import argparse
import csv
import json
import os
import random
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path
from typing import Any, Iterable, Mapping

from hairgpu_final_run import (
    AVAILABILITY_BITS,
    EXIT_COMPLETE_VALID,
    EXIT_FORMAL_CONTRACT_INVALID,
    EXIT_OUTPUT_INTEGRITY_FAILURE,
    EXIT_QUALITY_GATE_FAILED,
    FormalRunError,
    canonical_json_sha256,
    execute_case,
    normalize_case,
    normalize_method_code,
    output_paths,
    read_json,
    sha256_file,
    validate_timeline,
    write_json,
)

SUITE_VERSION = "3.7-prenovelty-stable-partition"
RUN_KINDS = {"formal", "determinism", "long-run", "all"}


def resolve_path(base: Path, value: Any) -> Path | None:
    if value in (None, ""):
        return None
    path = Path(str(value))
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as handle:
            handle.write(text); handle.flush(); os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_csv(path: Path, rows: list[Mapping[str, Any]], fieldnames: Iterable[str] | None = None) -> None:
    names = list(fieldnames or sorted({key for row in rows for key in row}))
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=names, extrasaction="ignore")
            writer.writeheader(); writer.writerows(rows); handle.flush(); os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def git_value(repo_root: Path, *args: str) -> str:
    try:
        run = subprocess.run(
            ["git", *args], cwd=repo_root, capture_output=True,
            text=True, timeout=20, check=False,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return run.stdout.strip() if run.returncode == 0 else ""


def load_config(config_path: Path, output_override: Path | None = None) -> tuple[dict[str, Any], Path, Path]:
    config_path = config_path.resolve(); config = read_json(config_path)
    base = config_path.parent; repo_root = Path(__file__).resolve().parents[2]
    outputs = output_override.resolve() if output_override else resolve_path(base, config.get("outputs_dir"))
    if outputs is None:
        raise ValueError("outputs_dir is required")
    config["outputs_dir"] = str(outputs); config["telemetry_dir"] = str(outputs / "telemetry")
    source = config.setdefault("source", {})
    manifest_override = os.environ.get("HAIRGPU_FROZEN_ASSET_MANIFEST")
    if manifest_override:
        source["asset_manifest"] = str(Path(manifest_override).resolve())
    for key in ("viewer_exe", "schema", "asset_manifest", "shader_root"):
        if source.get(key):
            source[key] = str(resolve_path(base, source[key]))
    profile_path = resolve_path(base, config.get("hardware_profiles"))
    if profile_path is None or not profile_path.is_file():
        raise ValueError(f"hardware_profiles file not found: {profile_path}")
    profile_payload = read_json(profile_path)
    profiles = profile_payload.get("profiles", {}) if isinstance(profile_payload.get("profiles"), dict) else {}
    defaults = config.setdefault("run_defaults", {})
    profile_name = str(defaults.get("hardware_profile", ""))
    if not profile_name or profile_name not in profiles:
        raise ValueError(f"Unknown or missing hardware profile: {profile_name}")
    profile = profiles[profile_name]
    defaults["hardware_profile_contract"] = profile
    defaults["hardware_profiles_sha256"] = sha256_file(profile_path)
    execution = profile.get("executionContract", {}) if isinstance(profile.get("executionContract"), dict) else {}
    for source_key, target_key in (
        ("maximumProcessPrivateMemoryMiB", "maximum_process_private_memory_mib"),
        ("targetProcessPrivateMemoryMiB", "target_process_private_memory_mib"),
        ("maximumDxgiLocalUsageMiB", "maximum_dxgi_local_usage_mib"),
        ("minimumProcessMemorySampleCoverage", "minimum_process_memory_sample_coverage"),
    ):
        if source_key in execution:
            defaults[target_key] = execution[source_key]
    return config, outputs, repo_root


def scenario_assertions(timeline_path: Path) -> dict[str, Any]:
    assertions = read_json(timeline_path).get("assertions", {})
    if not isinstance(assertions, dict):
        raise ValueError(f"Scenario assertions must be an object: {timeline_path}")
    return assertions


def telemetry_contract(config: Mapping[str, Any]) -> dict[str, Any]:
    contract = config.get("telemetry_contract")
    if isinstance(contract, dict):
        return dict(contract)
    preflight = config.get("preflight", {}) if isinstance(config.get("preflight"), dict) else {}
    return {
        "schema_version": 3,
        "required_fields": list(preflight.get("required_telemetry_fields", [])),
        "minimum_field_coverage": config.get("run_defaults", {}).get("minimum_metric_coverage", 0.99),
        "required_availability_by_scenario": {},
        "require_zero_synthetic_metrics": True,
        "require_zero_same_frame_readbacks": True,
    }


def _apply_contract(run: dict[str, Any], contract: Mapping[str, Any]) -> None:
    run["telemetry_schema_version"] = int(contract.get("schema_version", 3))
    run["required_telemetry_fields"] = list(contract.get("required_fields", []))
    run["minimum_metric_coverage"] = float(contract.get("minimum_field_coverage", 0.99))
    by_scenario = contract.get("required_availability_by_scenario", {})
    run["required_availability"] = list(by_scenario.get(run.get("scenario"), [])) if isinstance(by_scenario, dict) else []
    run["require_zero_synthetic_metrics"] = bool(contract.get("require_zero_synthetic_metrics", True))
    run["require_zero_same_frame_readbacks"] = bool(contract.get("require_zero_same_frame_readbacks", True))


def _expand_formal_run_matrix_core(config: Mapping[str, Any], config_dir: Path, repo_root: Path) -> list[dict[str, Any]]:
    defaults = dict(config.get("run_defaults", {})); source = dict(config.get("source", {}))
    goals = config.get("goals", {}) if isinstance(config.get("goals"), dict) else {}
    methods = config.get("methods", []); scenarios = config.get("scenarios", []); seeds = config.get("seeds", [])
    repetitions = int(config.get("repetitions_per_seed", 1)); contract = telemetry_contract(config)
    if not all(isinstance(item, list) for item in (methods, scenarios, seeds)):
        raise ValueError("methods, scenarios and seeds must be arrays")
    runs: list[dict[str, Any]] = []; seen: set[str] = set()
    for method_entry in methods:
        if not isinstance(method_entry, dict):
            raise ValueError("method entry must be an object")
        method_code = normalize_method_code(str(method_entry.get("code", "")))
        for scenario_entry in scenarios:
            if not isinstance(scenario_entry, dict):
                raise ValueError("scenario entry must be an object")
            scenario_code = str(scenario_entry.get("code", ""))
            timeline = resolve_path(config_dir, scenario_entry.get("timeline"))
            if timeline is None or not timeline.is_file():
                raise ValueError(f"Scenario timeline missing: {scenario_code}: {timeline}")
            source_assertions = scenario_assertions(timeline)
            config_assertions = scenario_entry.get("assertions")
            assertions = (
                dict(config_assertions)
                if isinstance(config_assertions, dict)
                else source_assertions
            )
            for seed in seeds:
                for repetition in range(repetitions):
                    run_name = f"{defaults.get('run_name_prefix', 'novelty01')}_{method_code}_{scenario_code}_{int(seed)}_r{repetition:02d}"
                    if run_name in seen:
                        raise ValueError(f"Duplicate generated run name: {run_name}")
                    seen.add(run_name)
                    run = dict(defaults)
                    run.update({
                        "run_name": run_name, "case_id": run_name,
                        "method": method_code, "variant": method_code,
                        "scenario": scenario_code, "scenario_timeline": str(timeline),
                        "seed": int(seed), "repetition_index": repetition,
                        "measurement_mode": scenario_entry.get("measurement_mode", defaults.get("measurement_mode", "performance")),
                        "assertions": assertions, "asset_manifest": source.get("asset_manifest"),
                        "shader_root": source.get("shader_root"),
                        "allowed_asset_source_branches": list(source.get("allowed_asset_source_branches", [])),
                        "special_kind": "formal",
                    })
                    run["method_settings"] = dict(method_entry.get("settings", {}))
                    run["scientific_goals"] = dict(goals)
                    _apply_contract(run, contract)
                    runs.append(normalize_case(run))
    return runs

def expand_determinism_runs(config: Mapping[str, Any], formal_runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_key = {(run["method"], run["scenario"], int(run["seed"])): run for run in formal_runs}
    result: list[dict[str, Any]] = []
    for replay in config.get("determinism_replays", []):
        key = (normalize_method_code(str(replay.get("method", ""))), str(replay.get("scenario", "")), int(replay.get("seed", 0)))
        template = by_key.get(key)
        if template is None:
            raise ValueError(f"Determinism template not found: {key}")
        for index in range(int(replay.get("repetitions", 1))):
            run = dict(template); run["run_name"] = f"{replay['id']}_r{index:02d}"
            run["case_id"] = run["run_name"]; run["repetition_index"] = index
            run["measurement_mode"] = "determinism"; run["special_kind"] = "determinism"
            run["settings_hash"] = ""; result.append(normalize_case(run))
    return result


def expand_long_runs(config: Mapping[str, Any], formal_runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_key = {(run["method"], run["scenario"], int(run["seed"])): run for run in formal_runs}
    result: list[dict[str, Any]] = []
    for soak in config.get("long_runs", []):
        key = (normalize_method_code(str(soak.get("method", ""))), str(soak.get("scenario", "")), int(soak.get("seed", 0)))
        template = by_key.get(key)
        if template is None:
            raise ValueError(f"Long-run template not found: {key}")
        run = dict(template); run["run_name"] = str(soak["id"]); run["case_id"] = run["run_name"]
        run["measurement_frames"] = int(soak["measurement_frames"]); run["timeout_seconds"] = int(soak["timeout_seconds"])
        run["timeline_repeat"] = bool(soak.get("timeline_repeat", True)); run["special_kind"] = "long_run"
        run["settings_hash"] = ""; result.append(normalize_case(run))
    return result


def select_runs_by_kind(formal_runs: list[dict[str, Any]], determinism_runs: list[dict[str, Any]],
                        long_runs: list[dict[str, Any]], run_kind: str) -> list[dict[str, Any]]:
    if run_kind == "formal":
        return list(formal_runs)
    if run_kind == "determinism":
        return list(determinism_runs)
    if run_kind == "long-run":
        return list(long_runs)
    if run_kind == "all":
        return [*formal_runs, *determinism_runs, *long_runs]
    raise ValueError(f"Unknown run kind: {run_kind}")


def filter_runs(runs: list[dict[str, Any]], methods: set[str], scenarios: set[str],
                seeds: set[int], run_names: set[str]) -> list[dict[str, Any]]:
    return [run for run in runs
            if (not methods or run["method"] in methods)
            and (not scenarios or run["scenario"] in scenarios)
            and (not seeds or int(run["seed"]) in seeds)
            and (not run_names or run["run_name"] in run_names)]


def is_network_path(path: Path) -> bool:
    text = str(path); return text.startswith("\\\\") or text.startswith("//")


def run_preflight(config: Mapping[str, Any], output_dir: Path, viewer_exe: Path,
                  repo_root: Path, selected_runs: list[dict[str, Any]], run_kind: str) -> list[dict[str, Any]]:
    problems: list[dict[str, Any]] = []
    preflight = config.get("preflight", {}) if isinstance(config.get("preflight"), dict) else {}
    output_dir.mkdir(parents=True, exist_ok=True)
    defaults = config.get("run_defaults", {}) if isinstance(config.get("run_defaults"), dict) else {}
    expected_checks = (
        ("build_configuration", preflight.get("required_build_configuration"), defaults.get("build_configuration")),
        ("platform", preflight.get("required_platform"), defaults.get("platform")),
        ("width", preflight.get("required_width"), defaults.get("width")),
        ("height", preflight.get("required_height"), defaults.get("height")),
        ("fixed_timestep_seconds", preflight.get("required_fixed_timestep_seconds"), defaults.get("fixed_timestep_seconds")),
    )
    for field, expected, observed in expected_checks:
        if expected is not None and observed != expected:
            problems.append({"code": "preflight_value_mismatch", "field": field, "expected": expected, "observed": observed})
    if int(defaults.get("warmup_frames", 0)) < int(preflight.get("minimum_warmup_frames", 0)):
        problems.append({"code": "warmup_frames_too_low", "expected": preflight.get("minimum_warmup_frames"), "observed": defaults.get("warmup_frames")})
    if int(defaults.get("measurement_frames", 0)) < int(preflight.get("minimum_measurement_frames", 0)):
        problems.append({"code": "measurement_frames_too_low", "expected": preflight.get("minimum_measurement_frames"), "observed": defaults.get("measurement_frames")})
    if len(config.get("seeds", [])) < int(preflight.get("minimum_seed_count", 0)):
        problems.append({"code": "seed_count_too_low", "expected": preflight.get("minimum_seed_count"), "observed": len(config.get("seeds", []))})
    if bool(preflight.get("require_vsync_off", True)) and bool(defaults.get("vsync", True)):
        problems.append({"code": "vsync_must_be_off", "observed": defaults.get("vsync")})
    if bool(preflight.get("require_vsync_off", True)) and int(defaults.get("present_sync_interval", 1)) != 0:
        problems.append({"code": "present_sync_interval_must_be_zero", "observed": defaults.get("present_sync_interval")})
    free = shutil.disk_usage(output_dir).free
    required = int(float(preflight.get("minimum_free_disk_gib", 20.0)) * (1024 ** 3))
    if free < required:
        problems.append({"code": "disk_space_low", "expected": required, "observed": free})
    if bool(preflight.get("reject_network_output_path", True)) and is_network_path(output_dir):
        problems.append({"code": "network_output_rejected", "observed": str(output_dir)})
    if not viewer_exe.is_file():
        problems.append({"code": "viewer_missing", "observed": str(viewer_exe)})
    branch = git_value(repo_root, "branch", "--show-current")
    commit_sha = git_value(repo_root, "rev-parse", "HEAD")
    source_contract = config.get("source", {}) if isinstance(config.get("source"), dict) else {}
    branch_contract = source_contract.get("branch_contract", {})
    if not isinstance(branch_contract, dict):
        branch_contract = {}
    branch_mode = str(branch_contract.get("mode", "detect_current"))
    require_attached = bool(branch_contract.get("require_attached_head", True))
    if require_attached and not branch:
        problems.append({"code": "detached_head_or_branch_unknown"})
    if not commit_sha:
        problems.append({"code": "commit_sha_unavailable"})
    if branch_mode == "exact":
        expected_branch = str(branch_contract.get("branch", ""))
        if not expected_branch:
            problems.append({"code": "branch_contract_exact_missing"})
        elif branch and branch != expected_branch:
            problems.append({"code": "branch_mismatch", "expected": expected_branch, "observed": branch})
    elif branch_mode != "detect_current":
        problems.append({"code": "branch_contract_mode_unknown", "observed": branch_mode})
    dirty = git_value(repo_root, "status", "--porcelain", "--untracked-files=normal")
    if bool(preflight.get("require_clean_git", True)) and dirty:
        problems.append({"code": "dirty_worktree", "observed": dirty.splitlines()[:10]})
    if sys.version_info < (3, 10):
        problems.append({"code": "python_too_old", "expected": ">=3.10", "observed": sys.version})
    configured_methods = {normalize_method_code(str(item.get("code", ""))) for item in config.get("methods", []) if isinstance(item, dict)}
    configured_scenarios = {str(item.get("code", "")) for item in config.get("scenarios", []) if isinstance(item, dict)}
    for code in preflight.get("required_method_codes", []):
        if normalize_method_code(str(code)) not in configured_methods:
            problems.append({"code": "required_method_missing", "observed": code})
    for code in preflight.get("required_scenario_codes", []):
        if str(code) not in configured_scenarios:
            problems.append({"code": "required_scenario_missing", "observed": code})
    contract = telemetry_contract(config)
    fields = [str(item) for item in contract.get("required_fields", [])]
    if not fields or len(fields) != len(set(fields)):
        problems.append({"code": "telemetry_contract_fields_invalid", "observed": fields})
    coverage = float(contract.get("minimum_field_coverage", 0.99))
    if coverage < 0.0 or coverage > 1.0:
        problems.append({"code": "telemetry_contract_coverage_invalid", "observed": coverage})
    availability = contract.get("required_availability_by_scenario", {})
    if isinstance(availability, dict):
        for scenario, names in availability.items():
            if scenario not in configured_scenarios:
                problems.append({"code": "telemetry_contract_scenario_unknown", "observed": scenario})
            for name in names if isinstance(names, list) else []:
                if name not in AVAILABILITY_BITS:
                    problems.append({"code": "telemetry_contract_availability_unknown", "observed": name})
    source = config.get("source", {}) if isinstance(config.get("source"), dict) else {}
    manifest = Path(str(source.get("asset_manifest", "")))
    if bool(preflight.get("require_frozen_asset", True)) and not manifest.is_file():
        problems.append({"code": "frozen_asset_manifest_missing", "observed": str(manifest)})
    if not selected_runs:
        problems.append({"code": "no_runs_selected", "runKind": run_kind})
    expected_kind = {
        "formal": "formal",
        "determinism": "determinism",
        "long-run": "long_run",
    }.get(run_kind)
    if expected_kind is not None:
        leaked = [
            str(run.get("run_name", ""))
            for run in selected_runs
            if run.get("special_kind") != expected_kind
        ]
        if leaked:
            problems.append({
                "code": "run_kind_partition_violation",
                "runKind": run_kind,
                "expectedSpecialKind": expected_kind,
                "runs": leaked[:20],
            })
    known_kinds = {"formal", "determinism", "long_run"}
    unknown = [
        str(run.get("run_name", ""))
        for run in selected_runs
        if run.get("special_kind") not in known_kinds
    ]
    if unknown:
        problems.append({
            "code": "unknown_special_run_kind",
            "runs": unknown[:20],
        })
    return problems


def read_validation(paths: Mapping[str, Path]) -> dict[str, Any]:
    return read_json(paths["validation"]) if paths["validation"].is_file() else {}


def verify_run_manifest(paths: Mapping[str, Path]) -> bool:
    if (not paths["manifest"].is_file() or not paths["complete"].is_file()
            or paths["running"].exists() or paths["lock"].exists() or paths["incomplete"].exists()):
        return False
    manifest = read_json(paths["manifest"])
    for entry in manifest.get("artifacts", []):
        path = paths["manifest"].parent / str(entry.get("path", ""))
        if not path.is_file() or path.stat().st_size != int(entry.get("sizeBytes", -1)):
            return False
        if sha256_file(path).lower() != str(entry.get("sha256", "")).lower():
            return False
    expected_hash = str(manifest.get("manifestSha256", "")); copy = dict(manifest); copy.pop("manifestSha256", None)
    return expected_hash == canonical_json_sha256(copy)


def resume_fingerprint(run: Mapping[str, Any], viewer_exe: Path, output_dir: Path, repo_root: Path) -> str:
    with tempfile.TemporaryDirectory(prefix="hairgpu_resume_") as temporary:
        probe = execute_case(run, viewer_exe, Path(temporary), repo_root, dry_run=True)
    return str(probe.get("executionFingerprint", ""))


def is_completed_case(run: Mapping[str, Any], viewer_exe: Path, output_dir: Path, repo_root: Path) -> bool:
    paths = output_paths(output_dir, str(run["run_name"]))
    if not verify_run_manifest(paths):
        return False
    complete = read_json(paths["complete"]); observed = str(complete.get("executionFingerprint", ""))
    if not observed:
        return False
    try:
        expected = resume_fingerprint(run, viewer_exe, output_dir, repo_root)
    except FormalRunError:
        return False
    return observed.lower() == expected.lower()


def metric_row(run: Mapping[str, Any], validation: Mapping[str, Any], exit_code: int, reused: bool) -> dict[str, Any]:
    aggregate = validation.get("aggregate", {}) if isinstance(validation.get("aggregate"), dict) else {}
    process = validation.get("processMemory", {}) if isinstance(validation.get("processMemory"), dict) else {}
    visual = validation.get("visualMetrics", {}) if isinstance(validation.get("visualMetrics"), dict) else {}
    return {
        "run_name": run["run_name"], "run_kind": run.get("special_kind", "formal"),
        "method": run["method"], "scenario": run["scenario"], "seed": run["seed"],
        "repetition_index": run.get("repetition_index", 0), "exit_code": exit_code,
        "reused": int(reused), "valid": int(bool(validation.get("valid"))),
        "quality_gate_passed": int(bool(validation.get("qualityGatePassed"))),
        "average_fps": aggregate.get("averageFps"), "p95_frame_ms": aggregate.get("p95FrameMilliseconds"),
        "p99_frame_ms": aggregate.get("p99FrameMilliseconds"), "gpu_timing_coverage": aggregate.get("gpuTimingCoverage"),
        "peak_process_private_mib": process.get("peakProcessPrivateMiB"),
        "peak_process_working_set_mib": process.get("peakProcessWorkingSetMiB"),
        "process_private_slope_mib_per_min": process.get("processPrivateSlopeMiBPerMin"),
        "peak_dxgi_local_usage_mib": process.get("peakDxgiLocalUsageMiB"),
        "dxgi_local_usage_slope_mib_per_min": process.get("dxgiLocalUsageSlopeMiBPerMin"),
        "process_memory_sample_coverage": process.get("sampleCoverage"),
        "resource_sample_coverage": process.get("resourceSampleCoverage"),
        "peak_handle_count": process.get("peakHandleCount"),
        "handle_count_slope_per_min": process.get("handleCountSlopePerMin"),
        "peak_gdi_object_count": process.get("peakGdiObjectCount"),
        "gdi_object_count_slope_per_min": process.get("gdiObjectCountSlopePerMin"),
        "peak_user_object_count": process.get("peakUserObjectCount"),
        "user_object_count_slope_per_min": process.get("userObjectCountSlopePerMin"),
        "peak_thread_count": process.get("peakThreadCount"),
        "thread_count_slope_per_min": process.get("threadCountSlopePerMin"),
        "visual_non_background_ratio": visual.get("nonBackgroundRatio"),
        "visual_bounding_box_ratio": visual.get("foregroundBoundingBoxRatio"),
        "visual_luminance_stddev": visual.get("luminanceStdDev"),
        "visual_average_hash_256": visual.get("averageHash256"),
        "issue_count": len(validation.get("issues", [])) if isinstance(validation.get("issues"), list) else 0,
    }


def write_sqlite(path: Path, rows: list[Mapping[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(path)
    try:
        connection.execute("DROP TABLE IF EXISTS final_runtime_runs")
        connection.execute("""
            CREATE TABLE final_runtime_runs (
                run_name TEXT PRIMARY KEY, run_kind TEXT, method TEXT, scenario TEXT,
                seed INTEGER, repetition_index INTEGER, exit_code INTEGER, reused INTEGER,
                valid INTEGER, quality_gate_passed INTEGER, average_fps REAL,
                p95_frame_ms REAL, p99_frame_ms REAL, gpu_timing_coverage REAL,
                peak_process_private_mib REAL, peak_process_working_set_mib REAL,
                process_private_slope_mib_per_min REAL, peak_dxgi_local_usage_mib REAL,
                dxgi_local_usage_slope_mib_per_min REAL,
                process_memory_sample_coverage REAL, resource_sample_coverage REAL,
                peak_handle_count REAL, handle_count_slope_per_min REAL,
                peak_gdi_object_count REAL, gdi_object_count_slope_per_min REAL,
                peak_user_object_count REAL, user_object_count_slope_per_min REAL,
                peak_thread_count REAL, thread_count_slope_per_min REAL,
                visual_non_background_ratio REAL, visual_bounding_box_ratio REAL,
                visual_luminance_stddev REAL, visual_average_hash_256 TEXT,
                issue_count INTEGER
            )
        """)
        if rows:
            columns = list(rows[0])
            placeholders = ",".join("?" for _ in columns)
            connection.executemany(
                f"INSERT INTO final_runtime_runs ({','.join(columns)}) VALUES ({placeholders})",
                [[row.get(column) for column in columns] for row in rows],
            )
        connection.commit()
    finally:
        connection.close()


def build_suite_artifact(output_dir: Path, config_path: Path, rows: list[Mapping[str, Any]]) -> Path:
    archive = output_dir / "final_runtime_suite_artifacts.zip"
    temporary = archive.with_suffix(".zip.tmp")
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as handle:
        handle.write(config_path, arcname="configuration/" + config_path.name)
        for path in sorted(output_dir.iterdir()):
            if path.is_file() and path not in {archive, temporary}:
                handle.write(path, arcname="results/" + path.name)
    os.replace(temporary, archive)
    return archive


# HAIRGPU_SUITE_POST_GATES_V1
def _percentile_local(values: list[float], percentile_value: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * percentile_value / 100.0
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def _read_jsonl_local(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    if not path.is_file():
        return rows
    with path.open("r", encoding="utf-8-sig") as handle:
        for line in handle:
            text = line.strip()
            if text:
                value = json.loads(text)
                if isinstance(value, dict):
                    rows.append(value)
    return rows


def _visual_hash_distance(left: Any, right: Any) -> float | None:
    try:
        left_text = str(left).strip().lower()
        right_text = str(right).strip().lower()
        if len(left_text) != 64 or len(right_text) != 64:
            return None
        difference = int(left_text, 16) ^ int(right_text, 16)
        return difference.bit_count() / 256.0
    except (TypeError, ValueError):
        return None


def _visual_relative_error(observed: Any, baseline: Any) -> float | None:
    try:
        observed_value = float(observed)
        baseline_value = float(baseline)
    except (TypeError, ValueError):
        return None
    denominator = max(abs(baseline_value), 1.0e-9)
    return abs(observed_value - baseline_value) / denominator


def evaluate_post_suite_gates(
    config: Mapping[str, Any],
    runs: list[dict[str, Any]],
    output_dir: Path,
    run_kind: str,
) -> list[dict[str, Any]]:
    problems: list[dict[str, Any]] = []

    if run_kind in {"formal", "all"}:
        visual_config = (
            config.get("visual_regression", {})
            if isinstance(config.get("visual_regression"), dict)
            else {}
        )
        if bool(visual_config.get("enabled", True)):
            baseline_method = normalize_method_code(str(
                visual_config.get(
                    "baseline_method",
                    config.get("quality_baseline_method", "B0"),
                )
            ))
            maximum_hash_distance = float(
                visual_config.get("maximum_hash_distance_ratio", 0.55)
            )
            maximum_coverage_error = float(
                visual_config.get("maximum_foreground_ratio_relative_error", 0.75)
            )
            maximum_bounds_error = float(
                visual_config.get("maximum_bounding_box_ratio_relative_error", 0.60)
            )
            require_multi_method_baseline = bool(
                visual_config.get("require_baseline_for_multi_method", True)
            )
            groups: dict[tuple[str, int, int], list[dict[str, Any]]] = {}
            for run in runs:
                if run.get("special_kind", "formal") != "formal":
                    continue
                key = (
                    str(run.get("scenario", "")),
                    int(run.get("seed", 0)),
                    int(run.get("repetition_index", 0)),
                )
                groups.setdefault(key, []).append(run)

            for group_key, group_runs in sorted(groups.items()):
                baseline = next(
                    (run for run in group_runs if normalize_method_code(str(run.get("method", ""))) == baseline_method),
                    None,
                )
                distinct_methods = {
                    normalize_method_code(str(run.get("method", "")))
                    for run in group_runs
                }
                if baseline is None:
                    if require_multi_method_baseline and len(distinct_methods) > 1:
                        problems.append({
                            "code": "visual_baseline_missing",
                            "group": group_key,
                            "expectedMethod": baseline_method,
                            "observedMethods": sorted(distinct_methods),
                        })
                    continue
                baseline_paths = output_paths(output_dir, str(baseline["run_name"]))
                if not baseline_paths["visual_metrics"].is_file():
                    problems.append({
                        "code": "visual_baseline_metrics_missing",
                        "group": group_key,
                        "run": baseline["run_name"],
                    })
                    continue
                try:
                    baseline_metrics = read_json(baseline_paths["visual_metrics"])
                except (OSError, ValueError, json.JSONDecodeError) as exc:
                    problems.append({
                        "code": "visual_baseline_metrics_invalid",
                        "group": group_key,
                        "run": baseline["run_name"],
                        "observed": str(exc),
                    })
                    continue

                for run in group_runs:
                    if run is baseline:
                        continue
                    paths = output_paths(output_dir, str(run["run_name"]))
                    if not paths["visual_metrics"].is_file():
                        problems.append({
                            "code": "visual_metrics_missing",
                            "group": group_key,
                            "run": run["run_name"],
                        })
                        continue
                    try:
                        metrics = read_json(paths["visual_metrics"])
                    except (OSError, ValueError, json.JSONDecodeError) as exc:
                        problems.append({
                            "code": "visual_metrics_invalid",
                            "group": group_key,
                            "run": run["run_name"],
                            "observed": str(exc),
                        })
                        continue
                    hash_distance = _visual_hash_distance(
                        metrics.get("averageHash256"),
                        baseline_metrics.get("averageHash256"),
                    )
                    coverage_error = _visual_relative_error(
                        metrics.get("nonBackgroundRatio"),
                        baseline_metrics.get("nonBackgroundRatio"),
                    )
                    bounds_error = _visual_relative_error(
                        metrics.get("foregroundBoundingBoxRatio"),
                        baseline_metrics.get("foregroundBoundingBoxRatio"),
                    )
                    for code, observed, maximum in (
                        ("visual_hash_regression", hash_distance, maximum_hash_distance),
                        ("visual_coverage_regression", coverage_error, maximum_coverage_error),
                        ("visual_bounds_regression", bounds_error, maximum_bounds_error),
                    ):
                        if observed is None or observed > maximum:
                            problems.append({
                                "code": code,
                                "group": group_key,
                                "run": run["run_name"],
                                "baselineRun": baseline["run_name"],
                                "expected": f"<= {maximum}",
                                "observed": observed,
                            })

    if run_kind in {"determinism", "all"}:
        groups: dict[str, list[dict[str, Any]]] = {}
        for run in runs:
            if run.get("special_kind") != "determinism":
                continue
            name = str(run.get("run_name", ""))
            group = name.rsplit("_r", 1)[0]
            groups.setdefault(group, []).append(run)

        for group, group_runs in sorted(groups.items()):
            observed_hashes: list[tuple[Any, Any, Any]] = []
            for run in sorted(group_runs, key=lambda item: str(item.get("run_name", ""))):
                paths = output_paths(output_dir, str(run["run_name"]))
                if not paths["summary"].is_file():
                    problems.append({
                        "code": "determinism_summary_missing",
                        "group": group,
                        "run": run["run_name"],
                    })
                    continue
                summary = read_json(paths["summary"])
                hashes = (
                    summary.get("stateSequenceHash"),
                    summary.get("compactionSequenceHash"),
                    summary.get("finalPositionHash"),
                )
                if any(value in (None, "", 0, "0") for value in hashes):
                    problems.append({
                        "code": "determinism_hash_missing",
                        "group": group,
                        "run": run["run_name"],
                        "observed": hashes,
                    })
                observed_hashes.append(hashes)
                if int(summary.get("determinismMismatchCount", 0) or 0) != 0:
                    problems.append({
                        "code": "determinism_runtime_mismatch",
                        "group": group,
                        "run": run["run_name"],
                        "observed": summary.get("determinismMismatchCount"),
                    })

            if len(observed_hashes) != len(group_runs) or len(set(observed_hashes)) != 1:
                problems.append({
                    "code": "determinism_replay_mismatch",
                    "group": group,
                    "observed": observed_hashes,
                })

    if run_kind in {"long-run", "all"}:
        gates = (
            config.get("long_run_gates", {})
            if isinstance(config.get("long_run_gates"), dict)
            else {}
        )
        for run in runs:
            if run.get("special_kind") != "long_run":
                continue
            paths = output_paths(output_dir, str(run["run_name"]))
            validation = read_validation(paths)
            process = (
                validation.get("processMemory", {})
                if isinstance(validation.get("processMemory"), dict)
                else {}
            )
            summary = read_json(paths["summary"]) if paths["summary"].is_file() else {}
            frames = _read_jsonl_local(paths["jsonl"])

            checks = (
                ("peakProcessPrivateMiB", "<=", gates.get("max_peak_process_private_mib"), "long_run_peak_private"),
                ("processPrivateSlopeMiBPerMin", "<=", gates.get("max_process_private_slope_mib_per_min"), "long_run_private_slope"),
                ("peakDxgiLocalUsageMiB", "<=", gates.get("max_peak_dxgi_local_usage_mib"), "long_run_peak_vram"),
                ("dxgiLocalUsageSlopeMiBPerMin", "<=", gates.get("max_dxgi_local_usage_slope_mib_per_min"), "long_run_vram_slope"),
                ("sampleCoverage", ">=", gates.get("minimum_process_memory_sample_coverage"), "long_run_sample_coverage"),
                ("resourceSampleCoverage", ">=", gates.get("minimum_resource_sample_coverage"), "long_run_resource_sample_coverage"),
                ("peakHandleCount", "<=", gates.get("max_peak_handle_count"), "long_run_peak_handle_count"),
                ("handleCountSlopePerMin", "<=", gates.get("max_handle_count_slope_per_min"), "long_run_handle_slope"),
                ("peakGdiObjectCount", "<=", gates.get("max_peak_gdi_object_count"), "long_run_peak_gdi_objects"),
                ("gdiObjectCountSlopePerMin", "<=", gates.get("max_gdi_object_count_slope_per_min"), "long_run_gdi_slope"),
                ("peakUserObjectCount", "<=", gates.get("max_peak_user_object_count"), "long_run_peak_user_objects"),
                ("userObjectCountSlopePerMin", "<=", gates.get("max_user_object_count_slope_per_min"), "long_run_user_object_slope"),
                ("peakThreadCount", "<=", gates.get("max_peak_thread_count"), "long_run_peak_thread_count"),
                ("threadCountSlopePerMin", "<=", gates.get("max_thread_count_slope_per_min"), "long_run_thread_slope"),
            )
            for field, operator, threshold_value, code in checks:
                if threshold_value is None:
                    continue
                observed = process.get(field)
                try:
                    observed_number = float(observed)
                    threshold = float(threshold_value)
                    passed = (
                        observed_number <= threshold
                        if operator == "<="
                        else observed_number >= threshold
                    )
                except (TypeError, ValueError):
                    passed = False
                if not passed:
                    problems.append({
                        "code": code,
                        "run": run["run_name"],
                        "field": field,
                        "expected": f"{operator} {threshold_value}",
                        "observed": observed,
                    })

            if frames:
                count = max(1, len(frames) // 10)
                first_values = [
                    float(row["frameMilliseconds"])
                    for row in frames[:count]
                    if row.get("frameMilliseconds") is not None
                ]
                last_values = [
                    float(row["frameMilliseconds"])
                    for row in frames[-count:]
                    if row.get("frameMilliseconds") is not None
                ]
                first_p99 = _percentile_local(first_values, 99.0)
                last_p99 = _percentile_local(last_values, 99.0)
                ratio = (
                    last_p99 / first_p99
                    if first_p99 and last_p99 is not None
                    else None
                )
                maximum_ratio = float(
                    gates.get("max_p99_degradation_ratio", 1.1)
                )
                if ratio is None or ratio > maximum_ratio:
                    problems.append({
                        "code": "long_run_p99_degradation",
                        "run": run["run_name"],
                        "expected": maximum_ratio,
                        "observed": ratio,
                        "firstP99": first_p99,
                        "lastP99": last_p99,
                    })
            else:
                problems.append({
                    "code": "long_run_frames_missing",
                    "run": run["run_name"],
                })

            unresolved = summary.get("gpuTimingUnresolvedFrameCount")
            maximum_unresolved = float(
                gates.get("max_query_unresolved_slope", 0.0)
            )
            try:
                unresolved_number = float(unresolved)
            except (TypeError, ValueError):
                unresolved_number = float("inf")
            if unresolved_number > maximum_unresolved:
                problems.append({
                    "code": "long_run_unresolved_gpu_queries",
                    "run": run["run_name"],
                    "expected": maximum_unresolved,
                    "observed": unresolved,
                })

    return problems


def execute_suite(config_path: Path, viewer_exe: Path | None, output_override: Path | None,
                  run_kind: str, only_methods: set[str], only_scenarios: set[str],
                  only_seeds: set[int], only_run_names: set[str], dry_run: bool,
                  resume: bool, fail_policy: str) -> int:
    config, output_dir, repo_root = load_config(config_path, output_override)
    config_dir = config_path.resolve().parent
    source = config.get("source", {})
    resolved_viewer = Path(viewer_exe).resolve() if viewer_exe else Path(str(source.get("viewer_exe", ""))).resolve()
    formal_runs = expand_run_matrix(config, config_dir, repo_root)
    determinism_runs = expand_determinism_runs(config, formal_runs)
    long_runs = expand_long_runs(config, formal_runs)
    runs = select_runs_by_kind(formal_runs, determinism_runs, long_runs, run_kind)
    runs = filter_runs(runs, only_methods, only_scenarios, only_seeds, only_run_names)
    statistics = config.get("statistics", {}) if isinstance(config.get("statistics"), dict) else {}
    if run_kind == "formal" and bool(statistics.get("randomize_execution_order", True)):
        random.Random(int(statistics.get("execution_order_seed", 20260719))).shuffle(runs)
    elif run_kind == "determinism":
        runs.sort(key=lambda run: run["run_name"])
    elif run_kind == "long-run":
        order = {str(item.get("id")): index for index, item in enumerate(config.get("long_runs", [])) if isinstance(item, dict)}
        runs.sort(key=lambda run: order.get(run["run_name"], 10_000))
    problems = run_preflight(config, output_dir, resolved_viewer, repo_root, runs, run_kind)
    preflight_path = output_dir / "suite_preflight.json"
    write_json(preflight_path, {
        "suiteVersion": SUITE_VERSION,
        "runKind": run_kind,
        "selectedRunCount": len(runs),
        "gitIdentity": {
            "branch": git_value(repo_root, "branch", "--show-current"),
            "commitSha": git_value(repo_root, "rev-parse", "HEAD"),
        },
        "problems": problems,
    })
    if problems:
        print(json.dumps({"preflight": "failed", "problems": problems}, ensure_ascii=False, indent=2), file=sys.stderr)
        return EXIT_FORMAL_CONTRACT_INVALID
    dry_results: list[dict[str, Any]] = []
    for run in runs:
        try:
            dry_results.append(execute_case(run, resolved_viewer, output_dir / "dry_run_probe", repo_root, dry_run=True))
        except FormalRunError as exc:
            print(json.dumps({"run": run["run_name"], "error": str(exc), "issues": exc.issues}, ensure_ascii=False, indent=2), file=sys.stderr)
            return exc.exit_code
    write_json(output_dir / "suite_dry_run.json", {"runKind": run_kind, "runs": dry_results})
    shutil.rmtree(output_dir / "dry_run_probe", ignore_errors=True)
    if dry_run:
        print(f"Dry-run PASS: {len(runs)} run(s), kind={run_kind}")
        return EXIT_COMPLETE_VALID
    rows: list[dict[str, Any]] = []; worst_exit = EXIT_COMPLETE_VALID
    for index, run in enumerate(runs, 1):
        print(f"[{index}/{len(runs)}] {run['run_name']}")
        reused = False; validation: dict[str, Any] = {}; exit_code = EXIT_COMPLETE_VALID
        if resume and is_completed_case(run, resolved_viewer, output_dir, repo_root):
            reused = True; validation = read_validation(output_paths(output_dir, run["run_name"]))
            exit_code = EXIT_COMPLETE_VALID if validation.get("qualityGatePassed") else EXIT_QUALITY_GATE_FAILED
        else:
            try:
                validation = execute_case(run, resolved_viewer, output_dir, repo_root, dry_run=False)
                exit_code = EXIT_COMPLETE_VALID if validation.get("qualityGatePassed") else EXIT_QUALITY_GATE_FAILED
            except FormalRunError as exc:
                exit_code = exc.exit_code
                paths = output_paths(output_dir, run["run_name"])
                validation = read_validation(paths)
                if not validation:
                    validation = {"valid": False, "qualityGatePassed": False, "issues": exc.issues, "aggregate": {}, "processMemory": {}}
                print(f"ERROR {run['run_name']}: {exc}", file=sys.stderr)
            except Exception as exc:
                exit_code = EXIT_OUTPUT_INTEGRITY_FAILURE
                validation = {"valid": False, "qualityGatePassed": False, "issues": [{"code": "unexpected_exception", "message": str(exc)}], "aggregate": {}, "processMemory": {}}
                print(f"ERROR {run['run_name']}: {exc}", file=sys.stderr)
        rows.append(metric_row(run, validation, exit_code, reused))
        worst_exit = max(worst_exit, exit_code)
        write_csv(output_dir / "suite_metrics.csv", rows)
        write_json(output_dir / "suite_metrics.json", {"suiteVersion": SUITE_VERSION, "runKind": run_kind, "rows": rows})
        write_sqlite(output_dir / "final_runtime_suite.sqlite", rows)
        if exit_code not in {EXIT_COMPLETE_VALID, EXIT_QUALITY_GATE_FAILED} and fail_policy == "stop":
            break
    post_gate_issues = evaluate_post_suite_gates(
        config,
        runs,
        output_dir,
        run_kind,
    )
    write_json(
        output_dir / "suite_post_gates.json",
        {
            "runKind": run_kind,
            "passed": not post_gate_issues,
            "issues": post_gate_issues,
        },
    )
    if post_gate_issues:
        worst_exit = max(worst_exit, EXIT_QUALITY_GATE_FAILED)
    summary = {
        "suiteVersion": SUITE_VERSION, "runKind": run_kind,
        "selectedRunCount": len(runs), "completedRowCount": len(rows),
        "validRunCount": sum(int(row["valid"]) for row in rows),
        "qualityPassCount": sum(int(row["quality_gate_passed"]) for row in rows),
        "worstExitCode": worst_exit,
        "postSuiteGatesPassed": not post_gate_issues,
        "postSuiteGateIssueCount": len(post_gate_issues),
        "runtimeBranch": git_value(repo_root, "branch", "--show-current"),
        "runtimeCommitSha": git_value(repo_root, "rev-parse", "HEAD"),
        "generatedUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    write_json(output_dir / "suite_summary.json", summary)
    build_suite_artifact(output_dir, config_path.resolve(), rows)
    return worst_exit


def _self_test() -> int:
    formal = [{"run_name": "F", "special_kind": "formal", "method": "P", "scenario": "S01_Idle", "seed": 1}]
    det = [{"run_name": "D", "special_kind": "determinism", "method": "P", "scenario": "S01_Idle", "seed": 1}]
    long = [{"run_name": "L", "special_kind": "long_run", "method": "P", "scenario": "S01_Idle", "seed": 1}]
    if select_runs_by_kind(formal, det, long, "formal") != formal:
        return 1
    if select_runs_by_kind(formal, det, long, "long-run") != long:
        return 1
    if filter_runs(formal, {"P"}, {"S01_Idle"}, {1}, set()) != formal:
        return 1
    contract = {"required_fields": ["a", "b"], "minimum_field_coverage": 0.99}
    run: dict[str, Any] = {"scenario": "S01_Idle"}; _apply_contract(run, contract)
    if run["required_telemetry_fields"] != ["a", "b"]:
        return 1

    with tempfile.TemporaryDirectory(prefix="hairgpu_runtime_suite_selftest_") as temporary:
        output = Path(temporary)

        det_runs: list[dict[str, Any]] = []
        for index in range(5):
            replay = {
                "run_name": f"DET_TEST_r{index:02d}",
                "special_kind": "determinism",
                "method": "P",
                "scenario": "S16_StateThrashing",
                "seed": 1,
            }
            det_runs.append(replay)
            paths = output_paths(output, replay["run_name"])
            write_json(paths["summary"], {
                "stateSequenceHash": 11,
                "compactionSequenceHash": 22,
                "finalPositionHash": 33,
                "determinismMismatchCount": 0,
            })
        if evaluate_post_suite_gates({}, det_runs, output, "determinism"):
            print("self-test failed: valid determinism replay")
            return 1
        mismatch_paths = output_paths(output, det_runs[-1]["run_name"])
        write_json(mismatch_paths["summary"], {
            "stateSequenceHash": 99,
            "compactionSequenceHash": 22,
            "finalPositionHash": 33,
            "determinismMismatchCount": 0,
        })
        mismatch_issues = evaluate_post_suite_gates({}, det_runs, output, "determinism")
        if not any(item.get("code") == "determinism_replay_mismatch" for item in mismatch_issues):
            print("self-test failed: determinism mismatch gate")
            return 1

        long_run = {
            "run_name": "LONG_TEST",
            "special_kind": "long_run",
            "method": "P",
            "scenario": "S03_Walk",
            "seed": 1,
        }
        long_paths = output_paths(output, long_run["run_name"])
        write_json(long_paths["validation"], {
            "processMemory": {
                "peakProcessPrivateMiB": 900.0,
                "processPrivateSlopeMiBPerMin": 0.5,
                "peakDxgiLocalUsageMiB": 2000.0,
                "dxgiLocalUsageSlopeMiBPerMin": 0.2,
                "sampleCoverage": 1.0,
            }
        })
        write_json(long_paths["summary"], {"gpuTimingUnresolvedFrameCount": 0})
        atomic_write_text(
            long_paths["jsonl"],
            "".join(
                json.dumps({"frameMilliseconds": 10.0}, allow_nan=False) + "\n"
                for _ in range(100)
            ),
        )
        long_config = {
            "long_run_gates": {
                "max_peak_process_private_mib": 32768.0,
                "max_process_private_slope_mib_per_min": 2.0,
                "max_peak_dxgi_local_usage_mib": 6144.0,
                "max_dxgi_local_usage_slope_mib_per_min": 1.0,
                "minimum_process_memory_sample_coverage": 0.95,
                "max_p99_degradation_ratio": 1.1,
                "max_query_unresolved_slope": 0.0,
            }
        }
        if evaluate_post_suite_gates(long_config, [long_run], output, "long-run"):
            print("self-test failed: valid long-run gates")
            return 1
        write_json(long_paths["summary"], {"gpuTimingUnresolvedFrameCount": 1})
        long_issues = evaluate_post_suite_gates(long_config, [long_run], output, "long-run")
        if not any(item.get("code") == "long_run_unresolved_gpu_queries" for item in long_issues):
            print("self-test failed: long-run unresolved query gate")
            return 1

    print("hairgpu_runtime_suite self-test: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the HairSystem Novelty01 formal suite.")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--config")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--output")
    parser.add_argument("--run-kind", choices=sorted(RUN_KINDS), default="formal")
    parser.add_argument("--only-method", action="append", default=[])
    parser.add_argument("--only-scenario", action="append", default=[])
    parser.add_argument("--only-seed", action="append", type=int, default=[])
    parser.add_argument("--only-run-name", action="append", default=[])
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--fail-policy", choices=("continue", "stop"), default="continue")
    args = parser.parse_args()
    if args.self_test:
        return _self_test()
    if not args.config:
        parser.error("--config is required")
    try:
        return execute_suite(
            Path(args.config), Path(args.viewer_exe) if args.viewer_exe else None,
            Path(args.output) if args.output else None, args.run_kind,
            {normalize_method_code(item) for item in args.only_method}, set(args.only_scenario),
            set(args.only_seed), set(args.only_run_name), args.dry_run, args.resume, args.fail_policy,
        )
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        print(f"Configuration error: {exc}", file=sys.stderr)
        return EXIT_FORMAL_CONTRACT_INVALID


# HAIRGPU_PRENOVELTY_SUITE_SELECTION_V1
PRENOVELTY_SMOKE_METHODS = {"B0", "B6", "B8"}
PRENOVELTY_SMOKE_SCENARIOS = {
    "S01_Idle", "S03_Walk", "S09_StrongWind", "S17_CloseCamera100k"
}

def _hairgpu_apply_suite_quality_gate(run: Mapping[str, Any], config: Mapping[str, Any] | None = None) -> dict[str, Any]:
    result = dict(run)
    method = normalize_method_code(str(result.get("method", "")))
    phase = "proposal" if method == "P" else "prenovelty"
    result["evaluation_phase"] = phase
    gates = (config or {}).get("quality_gates", {}) if isinstance(config, Mapping) else {}
    selected = gates.get(phase, {}) if isinstance(gates, Mapping) else {}
    if isinstance(selected, Mapping):
        for key, value in selected.items():
            result[key] = value
    return result

# -----------------------------------------------------------------------------
# Backward-compatible helpers for the repository unit tests.
# -----------------------------------------------------------------------------

def prepare_suite_config(
    config: Mapping[str, Any],
    only_methods: list[str] | None = None,
    only_scenarios: list[str] | None = None,
    only_seeds: list[int] | None = None,
) -> dict[str, Any]:
    prepared = json.loads(json.dumps(dict(config)))
    source = prepared.setdefault("source", {})
    defaults = prepared.setdefault("run_defaults", {})
    preflight = prepared.setdefault("preflight", {})
    schema = source.get("schema")
    if schema is not None:
        prepared["schema"] = schema
    for key in ("viewer_exe", "asset_manifest"):
        if key in source:
            defaults[key] = source[key]
    if "guide_count" in defaults and "guide_strand_count" not in defaults:
        defaults["guide_strand_count"] = defaults.pop("guide_count")
    selected_methods = {normalize_method_code(item) for item in (only_methods or [])}
    selected_scenarios = set(only_scenarios or [])
    selected_seeds = {int(item) for item in (only_seeds or [])}

    methods = [item for item in prepared.get("methods", []) if isinstance(item, dict)]
    scenarios = [item for item in prepared.get("scenarios", []) if isinstance(item, dict)]
    if selected_methods:
        methods = [item for item in methods if normalize_method_code(str(item.get("code", ""))) in selected_methods]
    if selected_scenarios:
        scenarios = [item for item in scenarios if str(item.get("code", "")) in selected_scenarios]
    prepared["methods"] = methods
    prepared["scenarios"] = scenarios
    if selected_seeds:
        prepared["seeds"] = [int(item) for item in prepared.get("seeds", []) if int(item) in selected_seeds]

    required_method_codes = [normalize_method_code(str(item.get("code", ""))) for item in methods]
    required_scenario_codes = [str(item.get("code", "")) for item in scenarios]
    preflight["required_method_codes"] = required_method_codes
    preflight["required_scenario_codes"] = required_scenario_codes

    prepared["scenario_matrix"] = []
    for scenario in scenarios:
        entry = dict(scenario)
        entry["required_methods"] = list(required_method_codes)
        assertions = dict(entry.get("assertions", {}))
        if selected_methods == {"B0"}:
            assertions = {"min_applied_events": 0}
        entry["assertions"] = assertions
        prepared["scenario_matrix"].append(entry)
    return prepared


def expected_telemetry_paths(output_dir: Path, run: Mapping[str, Any], telemetry_dir: Path | None = None) -> dict[str, Path]:
    base = output_paths(Path(telemetry_dir or output_dir), str(run["run_name"]))
    return {
        "summary": base["summary"],
        "settings": base["settings"],
        "environment": base["environment"],
        "validation": base["validation"],
        "manifest": base["manifest"],
        "error": base["error"],
        "frame_csv": base["frames"],
        "frame_jsonl": base["jsonl"],
        "complete": base["complete"],
        "running": base["running"],
        "lock": base["lock"],
        "incomplete": base["incomplete"],
    }


def _legacy_is_completed_case(output_dir: Path, run: Mapping[str, Any], telemetry_dir: Path | None = None) -> bool:
    paths = expected_telemetry_paths(output_dir, run, telemetry_dir)
    if paths["error"].exists():
        return False
    required = [
        paths["summary"], paths["settings"], paths["environment"],
        paths["validation"], paths["manifest"], paths["frame_csv"], paths["frame_jsonl"],
    ]
    if any(not path.is_file() for path in required):
        return False
    manifest = read_json(paths["manifest"])
    artifacts = manifest.get("artifacts", [])
    for entry in artifacts:
        file_path = paths["manifest"].parent / str(entry.get("path", ""))
        if not file_path.is_file():
            return False
        if sha256_file(file_path).lower() != str(entry.get("sha256", "")).lower():
            return False
        if file_path.stat().st_size != int(entry.get("sizeBytes", -1)):
            return False
    validation = read_json(paths["validation"])
    return bool(validation.get("isValid", validation.get("valid", False))) and bool(validation.get("evidenceIntegrityPassed", True))


if 'is_completed_case' in globals():
    _strict_is_completed_case = is_completed_case
    def is_completed_case(*args: Any, **kwargs: Any) -> bool:
        if len(args) >= 4:
            return _strict_is_completed_case(*args, **kwargs)
        if len(args) >= 2:
            output_dir = Path(args[0])
            run = args[1]
            telemetry_dir = Path(args[2]) if len(args) >= 3 and args[2] is not None else None
            return _legacy_is_completed_case(output_dir, run, telemetry_dir)
        raise TypeError('is_completed_case() received an unsupported argument set')


def _prepare_formal_matrix_config(config: Mapping[str, Any]) -> dict[str, Any]:
    """Return an isolated config for formal Cartesian expansion.

    This helper never writes timeline source files and never materializes
    determinism or long-run entries.  Those run classes are generated only by
    their dedicated expansion functions so preflight partition checks remain
    meaningful.
    """
    working = json.loads(json.dumps(dict(config)))
    selected_methods = {
        normalize_method_code(str(item.get("code", "")))
        for item in working.get("methods", [])
        if isinstance(item, dict)
    }
    scenarios: list[dict[str, Any]] = []
    for raw in working.get("scenarios", []):
        if not isinstance(raw, dict):
            continue
        scenario = dict(raw)
        assertions = dict(scenario.get("assertions", {}))
        if selected_methods == {"B0"}:
            # B0 intentionally has no Novelty-only transition/repair event gate.
            assertions = {"min_applied_events": 0}
        else:
            minimum = int(assertions.get("min_applied_events", 0) or 0)
            if minimum < 0:
                raise ValueError(
                    "min_applied_events must be non-negative for non-baseline methods"
                )
        scenario["assertions"] = assertions
        scenarios.append(scenario)
    working["scenarios"] = scenarios
    return working


def expand_run_matrix(
    config: Mapping[str, Any],
    config_dir: Path,
    repo_root: Path | None = None,
) -> list[dict[str, Any]]:
    """Expand formal runs only.

    The previous compatibility wrapper also appended determinism and long-run
    cases, which made ``--run-kind formal`` fail its own partition gate.  Keep
    this public helper formal-only; use :func:`expand_all_runs` when a caller
    explicitly needs every run class.
    """
    resolved_repo = Path(repo_root) if repo_root is not None else Path(config_dir)
    working = _prepare_formal_matrix_config(config)
    runs = _expand_formal_run_matrix_core(
        working,
        Path(config_dir),
        resolved_repo,
    )
    result: list[dict[str, Any]] = []
    for raw in runs:
        run = _hairgpu_apply_suite_quality_gate(raw, working)
        run["special_kind"] = "formal"
        run["special_run_kind"] = "formal"
        result.append(run)
    return result


def expand_all_runs(
    config: Mapping[str, Any],
    config_dir: Path,
    repo_root: Path | None = None,
) -> list[dict[str, Any]]:
    """Explicitly materialize formal, determinism, and long-run cases."""
    resolved_repo = Path(repo_root) if repo_root is not None else Path(config_dir)
    working = _prepare_formal_matrix_config(config)
    formal_runs = expand_run_matrix(working, Path(config_dir), resolved_repo)
    determinism_runs = (
        expand_determinism_runs(working, formal_runs)
        if working.get("determinism_replays")
        else []
    )
    long_config = dict(working)
    if working.get("long_runs"):
        patched_long_runs: list[dict[str, Any]] = []
        for raw in working.get("long_runs", []):
            if not isinstance(raw, dict):
                continue
            entry = dict(raw)
            entry.setdefault("timeout_seconds", 1800)
            patched_long_runs.append(entry)
        long_config["long_runs"] = patched_long_runs
    long_runs = (
        expand_long_runs(long_config, formal_runs)
        if long_config.get("long_runs")
        else []
    )
    for run in determinism_runs:
        run["special_kind"] = "determinism"
        run["special_run_kind"] = "determinism"
    for run in long_runs:
        run["special_kind"] = "long_run"
        run["special_run_kind"] = "long_run"
    return [*formal_runs, *determinism_runs, *long_runs]


def visual_validation_passed(validation: Mapping[str, Any]) -> bool:
    issues = validation.get("issues", [])
    if not isinstance(issues, list):
        return False
    return not any(
        isinstance(item, Mapping) and str(item.get("code", "")) == "E_VISUAL_VALIDATION"
        for item in issues
    )


if __name__ == "__main__":
    raise SystemExit(main())
