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

SUITE_VERSION = "3.5-process-evidence"
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


def expand_run_matrix(config: Mapping[str, Any], config_dir: Path, repo_root: Path) -> list[dict[str, Any]]:
    defaults = dict(config.get("run_defaults", {})); source = dict(config.get("source", {}))
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
            assertions = scenario_assertions(timeline)
            config_assertions = scenario_entry.get("assertions")
            if config_assertions and config_assertions != assertions:
                raise ValueError(f"Scenario assertion drift: {scenario_code}")
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
                        "shader_root": source.get("shader_root"), "special_kind": "formal",
                    })
                    run["method_settings"] = dict(method_entry.get("settings", {}))
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
    free = shutil.disk_usage(output_dir).free
    required = int(float(preflight.get("minimum_free_disk_gib", 20.0)) * (1024 ** 3))
    if free < required:
        problems.append({"code": "disk_space_low", "expected": required, "observed": free})
    if bool(preflight.get("reject_network_output_path", True)) and is_network_path(output_dir):
        problems.append({"code": "network_output_rejected", "observed": str(output_dir)})
    if not viewer_exe.is_file():
        problems.append({"code": "viewer_missing", "observed": str(viewer_exe)})
    branch = git_value(repo_root, "branch", "--show-current")
    required_branch = str(config.get("source", {}).get("required_branch", "Novelty"))
    if branch and branch != required_branch:
        problems.append({"code": "branch_mismatch", "expected": required_branch, "observed": branch})
    dirty = git_value(repo_root, "status", "--porcelain", "--untracked-files=no")
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
    if run_kind == "formal" and any(run.get("special_kind") != "formal" for run in selected_runs):
        problems.append({"code": "special_run_leaked_into_formal"})
    if run_kind == "long-run" and any(run.get("special_kind") != "long_run" for run in selected_runs):
        problems.append({"code": "formal_run_leaked_into_long_run"})
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
                process_memory_sample_coverage REAL, issue_count INTEGER
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
    write_json(preflight_path, {"suiteVersion": SUITE_VERSION, "runKind": run_kind, "selectedRunCount": len(runs), "problems": problems})
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
    summary = {
        "suiteVersion": SUITE_VERSION, "runKind": run_kind,
        "selectedRunCount": len(runs), "completedRowCount": len(rows),
        "validRunCount": sum(int(row["valid"]) for row in rows),
        "qualityPassCount": sum(int(row["quality_gate_passed"]) for row in rows),
        "worstExitCode": worst_exit, "runtimeCommitSha": git_value(repo_root, "rev-parse", "HEAD"),
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


if __name__ == "__main__":
    raise SystemExit(main())
