from __future__ import annotations

"""Cartesian formal-suite orchestrator for Novelty01 stable-runtime evaluation."""

import argparse
import csv
import json
import math
import os
import random
import re
import shutil
import sqlite3
import statistics
import subprocess
import sys
import tempfile
import time
import zipfile
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable, Mapping

from hairgpu_final_run import (
    EXIT_COMPLETE_VALID,
    EXIT_FORMAL_CONTRACT_INVALID,
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

SUITE_VERSION = "3.4-stable-runtime"


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
        run = subprocess.run(["git", *args], cwd=repo_root, capture_output=True,
                             text=True, timeout=20, check=False)
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return run.stdout.strip() if run.returncode == 0 else ""


def load_config(config_path: Path, output_override: Path | None = None) -> tuple[dict[str, Any], Path, Path]:
    config_path = config_path.resolve()
    config = read_json(config_path)
    base = config_path.parent
    repo_root = Path(__file__).resolve().parents[2]
    outputs = output_override.resolve() if output_override else resolve_path(base, config.get("outputs_dir"))
    if outputs is None:
        raise ValueError("outputs_dir is required")
    config["outputs_dir"] = str(outputs)
    config["telemetry_dir"] = str(outputs / "telemetry")
    source = config.setdefault("source", {})
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
    defaults["hardware_profile_contract"] = profiles[profile_name]
    defaults["hardware_profiles_sha256"] = sha256_file(profile_path)
    return config, outputs, repo_root


def scenario_assertions(timeline_path: Path) -> dict[str, Any]:
    timeline = read_json(timeline_path)
    assertions = timeline.get("assertions", {})
    if not isinstance(assertions, dict):
        raise ValueError(f"Scenario assertions must be an object: {timeline_path}")
    return assertions


def expand_run_matrix(config: Mapping[str, Any], config_dir: Path, repo_root: Path) -> list[dict[str, Any]]:
    defaults = dict(config.get("run_defaults", {}))
    source = dict(config.get("source", {}))
    methods = config.get("methods", [])
    scenarios = config.get("scenarios", [])
    seeds = config.get("seeds", [])
    repetitions = int(config.get("repetitions_per_seed", 1))
    if not all((isinstance(methods, list), isinstance(scenarios, list), isinstance(seeds, list))):
        raise ValueError("methods, scenarios and seeds must be arrays")
    runs: list[dict[str, Any]] = []
    seen: set[str] = set()
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
                        "run_name": run_name,
                        "case_id": run_name,
                        "method": method_code,
                        "variant": method_code,
                        "scenario": scenario_code,
                        "scenario_timeline": str(timeline),
                        "seed": int(seed),
                        "repetition_index": repetition,
                        "measurement_mode": scenario_entry.get("measurement_mode", defaults.get("measurement_mode", "performance")),
                        "assertions": assertions,
                        "asset_manifest": source.get("asset_manifest"),
                        "shader_root": source.get("shader_root"),
                    })
                    method_settings = dict(method_entry.get("settings", {}))
                    run["method_settings"] = method_settings
                    runs.append(normalize_case(run))
    return runs


def add_special_runs(config: Mapping[str, Any], runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    by_key = {(run["method"], run["scenario"], int(run["seed"])): run for run in runs}
    result = list(runs)
    for replay in config.get("determinism_replays", []):
        key = (normalize_method_code(str(replay.get("method", ""))), str(replay.get("scenario", "")), int(replay.get("seed", 0)))
        template = by_key.get(key)
        if template is None:
            raise ValueError(f"Determinism template not found: {key}")
        for index in range(int(replay.get("repetitions", 1))):
            run = dict(template)
            run["run_name"] = f"{replay['id']}_r{index:02d}"
            run["case_id"] = run["run_name"]
            run["repetition_index"] = index
            run["measurement_mode"] = "determinism"
            run["special_kind"] = "determinism"
            run["settings_hash"] = ""
            result.append(normalize_case(run))
    for soak in config.get("long_runs", []):
        key = (normalize_method_code(str(soak.get("method", ""))), str(soak.get("scenario", "")), int(soak.get("seed", 0)))
        template = by_key.get(key)
        if template is None:
            raise ValueError(f"Long-run template not found: {key}")
        run = dict(template)
        run["run_name"] = str(soak["id"]); run["case_id"] = run["run_name"]
        run["measurement_frames"] = int(soak["measurement_frames"])
        run["timeout_seconds"] = int(soak["timeout_seconds"])
        run["timeline_repeat"] = bool(soak.get("timeline_repeat", True))
        run["special_kind"] = "long_run"
        run["settings_hash"] = ""
        result.append(normalize_case(run))
    names = [run["run_name"] for run in result]
    if len(names) != len(set(names)):
        raise ValueError("Special runs created duplicate run names")
    return result


def filter_runs(runs: list[dict[str, Any]], methods: set[str], scenarios: set[str], seeds: set[int]) -> list[dict[str, Any]]:
    return [run for run in runs
            if (not methods or run["method"] in methods)
            and (not scenarios or run["scenario"] in scenarios)
            and (not seeds or int(run["seed"]) in seeds)]


def is_network_path(path: Path) -> bool:
    text = str(path)
    return text.startswith("\\\\") or text.startswith("//")


def run_preflight(config: Mapping[str, Any], output_dir: Path, viewer_exe: Path, repo_root: Path) -> list[dict[str, Any]]:
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
    return problems


def read_validation(paths: Mapping[str, Path]) -> dict[str, Any]:
    return read_json(paths["validation"]) if paths["validation"].is_file() else {}


def verify_run_manifest(paths: Mapping[str, Path]) -> bool:
    if not paths["manifest"].is_file() or not paths["complete"].is_file() or paths["running"].exists():
        return False
    manifest = read_json(paths["manifest"])
    for entry in manifest.get("artifacts", []):
        path = paths["manifest"].parent / str(entry.get("path", ""))
        if not path.is_file() or path.stat().st_size != int(entry.get("sizeBytes", -1)):
            return False
        if sha256_file(path).lower() != str(entry.get("sha256", "")).lower():
            return False
    expected_hash = str(manifest.get("manifestSha256", ""))
    copy = dict(manifest); copy.pop("manifestSha256", None)
    return expected_hash == canonical_json_sha256(copy)


def resume_fingerprint(run: Mapping[str, Any], viewer_exe: Path, output_dir: Path, repo_root: Path) -> str:
    # Never write dry-run snapshots into an already completed run directory.
    # Fingerprints are content-based, so a temporary materialized timeline has
    # the same identity as the completed run's timeline.
    with tempfile.TemporaryDirectory(prefix="hairgpu_resume_") as temporary:
        probe = execute_case(run, viewer_exe, Path(temporary), repo_root, dry_run=True)
    return str(probe.get("executionFingerprint", ""))


def is_completed_case(run: Mapping[str, Any], viewer_exe: Path, output_dir: Path, repo_root: Path) -> bool:
    paths = output_paths(output_dir, str(run["run_name"]))
    if not verify_run_manifest(paths):
        return False
    complete = read_json(paths["complete"])
    observed = str(complete.get("executionFingerprint", ""))
    if not observed:
        return False
    try:
        expected = resume_fingerprint(run, viewer_exe, output_dir, repo_root)
    except FormalRunError:
        return False
    return observed.lower() == expected.lower()


def metric_row(run: Mapping[str, Any], validation: Mapping[str, Any], exit_code: int, reused: bool) -> dict[str, Any]:
    aggregate = validation.get("aggregate", {}) if isinstance(validation.get("aggregate"), dict) else {}
    row = {
        "run_name": run["run_name"], "method": run["method"], "scenario": run["scenario"],
        "seed": run["seed"], "repetition_index": run.get("repetition_index", 0),
        "special_kind": run.get("special_kind", "formal"), "exit_code": exit_code, "reused": reused,
        "evidence_status": validation.get("evidenceStatus", "missing"),
        "stable_60fps_status": validation.get("stable60FpsStatus", "not_achieved"),
        "integrity_issue_count": len(validation.get("integrityIssues", [])),
        "goal_failure_count": len(validation.get("goalFailures", [])),
    }
    for key, value in aggregate.items():
        if not isinstance(value, (dict, list)):
            row[key] = value
    return row


def aggregate_rows(rows: list[dict[str, Any]], group_key: str) -> list[dict[str, Any]]:
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        if row.get("special_kind") == "formal":
            groups[str(row.get(group_key, ""))].append(row)
    numeric_keys = ["averageFps", "sixtyFpsRatio", "frameBudgetOverrunRate", "p95FrameMilliseconds",
                    "p99FrameMilliseconds", "p999FrameMilliseconds", "gpuTimingCoverage",
                    "falseStaticRate", "collisionMissRate", "repairVerifiedSuccessRate",
                    "repairRecurrenceRate", "stateOscillationRate", "staticDynamicBoundaryMaxErrorMm"]
    output: list[dict[str, Any]] = []
    for name, group in sorted(groups.items()):
        item: dict[str, Any] = {group_key: name, "run_count": len(group),
                                "valid_count": sum(row.get("evidence_status") == "complete_valid" for row in group),
                                "stable_60_count": sum(row.get("stable_60fps_status") == "achieved" for row in group)}
        for key in numeric_keys:
            values = [float(row[key]) for row in group if isinstance(row.get(key), (int, float))]
            item[f"avg_{key}"] = statistics.fmean(values) if values else None
            item[f"max_{key}"] = max(values) if values else None
        output.append(item)
    return output


def evaluate_goals(config: Mapping[str, Any], rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    goals = config.get("goals", {}) if isinstance(config.get("goals"), dict) else {}
    formal = [row for row in rows if row.get("special_kind") == "formal" and row.get("evidence_status") == "complete_valid"]
    mapping = {
        "frame_time_p999_ms_max": ("p999FrameMilliseconds", "maximum"),
        "frame_budget_overrun_rate_max": ("frameBudgetOverrunRate", "maximum"),
        "false_static_rate_max": ("falseStaticRate", "maximum"),
        "collision_miss_rate_max": ("collisionMissRate", "maximum"),
        "repair_failure_rate_max": ("repairVerifiedSuccessRate", "failure_from_success"),
        "state_oscillation_rate_max": ("stateOscillationRate", "maximum"),
        "static_dynamic_boundary_max_error_mm_max": ("staticDynamicBoundaryMaxErrorMm", "maximum"),
        "determinism_mismatch_count_max": ("determinismMismatchCount", "maximum"),
        "minimum_gpu_timing_coverage": ("gpuTimingCoverage", "minimum"),
        "minimum_sixty_fps_ratio": ("sixtyFpsRatio", "minimum"),
        "minimum_average_fps": ("averageFps", "minimum"),
    }
    result: list[dict[str, Any]] = []
    for goal_name, threshold in goals.items():
        if goal_name not in mapping:
            result.append({"goal": goal_name, "passed": "no", "reason": "unsupported_goal", "threshold": threshold})
            continue
        metric, mode = mapping[goal_name]
        values = [float(row[metric]) for row in formal if isinstance(row.get(metric), (int, float))]
        if not values:
            result.append({"goal": goal_name, "passed": "no", "reason": "metric_unavailable", "threshold": threshold})
            continue
        if mode == "maximum": observed = max(values); passed = observed <= float(threshold)
        elif mode == "minimum": observed = min(values); passed = observed >= float(threshold)
        else: observed = max(1.0 - value for value in values); passed = observed <= float(threshold)
        result.append({"goal": goal_name, "metric": metric, "mode": mode, "threshold": threshold,
                       "observed": observed, "passed": "yes" if passed else "no"})
    return result


def validate_determinism(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        if row.get("special_kind") == "determinism":
            groups[re.sub(r"_r\d+$", "", str(row["run_name"]))].append(row)
    result = []
    for name, group in sorted(groups.items()):
        hashes = {(row.get("stateSequenceHash"), row.get("compactionSequenceHash"), row.get("finalPositionHash")) for row in group}
        valid = all(row.get("evidence_status") == "complete_valid" for row in group)
        result.append({"id": name, "run_count": len(group), "hash_set_count": len(hashes),
                       "passed": "yes" if valid and len(hashes) == 1 else "no"})
    return result


def validate_long_runs(config: Mapping[str, Any], rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    gates = config.get("long_run_gates", {}) if isinstance(config.get("long_run_gates"), dict) else {}
    result: list[dict[str, Any]] = []

    def finite_number(value: Any) -> float | None:
        try:
            number = float(value)
        except (TypeError, ValueError):
            return None
        return number if math.isfinite(number) else None

    for row in rows:
        if row.get("special_kind") != "long_run":
            continue
        checks: list[dict[str, Any]] = []
        checks.append({"name": "evidence", "passed": row.get("evidence_status") == "complete_valid",
                       "observed": row.get("evidence_status"), "expected": "complete_valid"})
        definitions = [
            ("peakVramMb", "max_peak_vram_mb"),
            ("vramSlopeMbPerMin", "max_vram_slope_mb_per_min"),
            ("ramSlopeMbPerMin", "max_ram_slope_mb_per_min"),
            ("p99DegradationRatio", "max_p99_degradation_ratio"),
            ("queryUnresolvedSlope", "max_query_unresolved_slope"),
        ]
        for metric, gate_name in definitions:
            observed = finite_number(row.get(metric))
            threshold = finite_number(gates.get(gate_name))
            passed = observed is not None and threshold is not None and observed <= threshold
            checks.append({"name": metric, "passed": passed, "observed": observed, "expected": threshold})
        result.append({
            "id": row["run_name"],
            "passed": "yes" if all(check["passed"] for check in checks) else "no",
            "evidence_status": row.get("evidence_status"),
            "checks_json": json.dumps(checks, ensure_ascii=False, allow_nan=False),
        })
    return result


def write_database(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(path) as database:
        database.execute("DROP TABLE IF EXISTS runtime_runs")
        database.execute("CREATE TABLE runtime_runs (run_name TEXT PRIMARY KEY, method TEXT, scenario TEXT, seed INTEGER, repetition_index INTEGER, special_kind TEXT, exit_code INTEGER, evidence_status TEXT, stable_60fps_status TEXT, payload_json TEXT NOT NULL)")
        database.executemany("INSERT INTO runtime_runs VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", [
            (row["run_name"], row.get("method"), row.get("scenario"), int(row.get("seed", 0)),
             int(row.get("repetition_index", 0)), row.get("special_kind"), int(row.get("exit_code", 0)),
             row.get("evidence_status"), row.get("stable_60fps_status"), json.dumps(row, ensure_ascii=False, allow_nan=False))
            for row in rows
        ])
        database.commit()


def write_repro_manifest(path: Path, files: list[Path], source_config: Path) -> dict[str, Any]:
    entries = []
    for file in sorted({item.resolve() for item in files if item.is_file()}, key=lambda item: item.as_posix().lower()):
        entries.append({"path": str(file), "sizeBytes": file.stat().st_size, "sha256": sha256_file(file)})
    payload = {"manifestVersion": SUITE_VERSION, "sourceConfig": str(source_config.resolve()),
               "sourceConfigSha256": sha256_file(source_config), "generatedFiles": entries}
    payload["manifestSha256"] = canonical_json_sha256(payload)
    write_json(path, payload)
    return payload


def verify_repro_manifest(path: Path) -> bool:
    payload = read_json(path)
    expected = str(payload.get("manifestSha256", "")); copy = dict(payload); copy.pop("manifestSha256", None)
    if expected != canonical_json_sha256(copy):
        return False
    for entry in payload.get("generatedFiles", []):
        file = Path(str(entry.get("path", "")))
        if not file.is_file() or file.stat().st_size != int(entry.get("sizeBytes", -1)):
            return False
        if sha256_file(file) != str(entry.get("sha256", "")):
            return False
    return True


def package_outputs(path: Path, files: list[Path], base: Path) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for file in sorted({item.resolve() for item in files if item.is_file()}, key=lambda item: item.as_posix().lower()):
            try: name = file.relative_to(base.resolve()).as_posix()
            except ValueError: name = file.name
            archive.write(file, name)


def write_markdown(path: Path, payload: Mapping[str, Any]) -> None:
    lines = ["# Novelty01 Runtime Suite", "", f"- Suite status: `{payload['suiteStatus']}`",
             f"- Evidence status: `{payload['evidenceStatus']}`",
             f"- Stable 60 FPS: `{payload['stable60FpsStatus']}`",
             f"- Runs: {payload['runCount']}", f"- Valid evidence: {payload['validEvidenceCount']}",
             f"- Goal-achieved runs: {payload['stable60RunCount']}", "", "## Goal evaluation", "",
             "| Goal | Observed | Threshold | Passed |", "|---|---:|---:|---|"]
    for row in payload.get("goalRows", []):
        lines.append(f"| {row.get('goal')} | {row.get('observed', '')} | {row.get('threshold', '')} | {row.get('passed')} |")
    atomic_write_text(path, "\n".join(lines) + "\n")


def self_test() -> int:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        file = root / "a.json"; write_json(file, {"a": 1})
        manifest = root / "manifest.json"
        write_repro_manifest(manifest, [file], file)
        assert verify_repro_manifest(manifest)
        file.write_text('{"a":2}\n', encoding="utf-8")
        assert not verify_repro_manifest(manifest)
    sample = [{"special_kind": "formal", "evidence_status": "complete_valid", "averageFps": 61.0,
               "sixtyFpsRatio": 0.97, "frameBudgetOverrunRate": 0.03, "p999FrameMilliseconds": 18.0,
               "gpuTimingCoverage": 1.0}]
    goals = evaluate_goals({"goals": {"minimum_sixty_fps_ratio": 0.95}}, sample)
    assert goals[0]["passed"] == "yes"
    print("hairgpu_runtime_suite self-test: PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config")
    parser.add_argument("--output")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--fail-policy", choices=("stop", "continue"), default="stop")
    parser.add_argument("--only-method", action="append", default=[])
    parser.add_argument("--only-scenario", action="append", default=[])
    parser.add_argument("--only-seed", action="append", type=int, default=[])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.config:
        parser.error("--config is required")
    config_path = Path(args.config).resolve()
    try:
        config, outputs_dir, repo_root = load_config(config_path, Path(args.output) if args.output else None)
        source = config.get("source", {})
        viewer_exe = Path(args.viewer_exe).resolve() if args.viewer_exe else Path(str(source.get("viewer_exe", ""))).resolve()
        problems = run_preflight(config, outputs_dir, viewer_exe, repo_root)
        write_json(outputs_dir / "runtime_suite_preflight.json", {"status": "passed" if not problems else "failed", "issues": problems})
        if problems:
            print(json.dumps(problems, ensure_ascii=False, indent=2), file=sys.stderr)
            return EXIT_FORMAL_CONTRACT_INVALID
        runs = expand_run_matrix(config, config_path.parent, repo_root)
        runs = add_special_runs(config, runs)
        runs = filter_runs(runs, {normalize_method_code(item) for item in args.only_method}, set(args.only_scenario), set(args.only_seed))
        if bool(config.get("statistics", {}).get("randomize_execution_order", True)):
            random.Random(int(config.get("statistics", {}).get("execution_order_seed", 20260716))).shuffle(runs)
        telemetry_dir = Path(str(config["telemetry_dir"])); telemetry_dir.mkdir(parents=True, exist_ok=True)
        rows: list[dict[str, Any]] = []
        if args.dry_run:
            # Validate every timeline/config cheaply, but hash the executable,
            # shaders, frozen asset and hardware only once. Long runs are also
            # materialized once each to validate their repeated timelines.
            representative_validated = False
            for index, run in enumerate(runs, 1):
                print(f"[{index}/{len(runs)}] preflight {run['run_name']}")
                try:
                    if run.get("special_kind") == "long_run" or not representative_validated:
                        validation = execute_case(run, viewer_exe, telemetry_dir, repo_root, dry_run=True)
                        representative_validated = True
                    else:
                        timeline = Path(str(run["scenario_timeline"]))
                        timeline_issues = validate_timeline(timeline, run)
                        if timeline_issues:
                            raise FormalRunError("Timeline preflight failed.", EXIT_FORMAL_CONTRACT_INVALID, timeline_issues)
                        validation = {
                            "evidenceStatus": "preflight_valid",
                            "stable60FpsStatus": "not_evaluated",
                            "aggregate": {},
                            "integrityIssues": [],
                            "goalFailures": [],
                        }
                    rows.append(metric_row(run, validation, EXIT_COMPLETE_VALID, False))
                except FormalRunError as exc:
                    validation = {
                        "evidenceStatus": "complete_invalid",
                        "stable60FpsStatus": "not_achieved",
                        "integrityIssues": exc.issues,
                        "goalFailures": [],
                    }
                    rows.append(metric_row(run, validation, exc.exit_code, False))
                    if args.fail_policy == "stop":
                        break
            write_json(outputs_dir / "runtime_suite_dry_run.json", {
                "suiteVersion": SUITE_VERSION,
                "runCount": len(rows),
                "expectedRunCount": len(runs),
                "runs": rows,
            })
            return 0 if len(rows) == len(runs) and all(row["exit_code"] == 0 for row in rows) else EXIT_FORMAL_CONTRACT_INVALID

        for index, run in enumerate(runs, 1):
            print(f"[{index}/{len(runs)}] {run['run_name']}")
            if args.resume and is_completed_case(run, viewer_exe, telemetry_dir, repo_root):
                paths = output_paths(telemetry_dir, str(run["run_name"])); validation = read_validation(paths)
                rows.append(metric_row(run, validation, EXIT_COMPLETE_VALID, True)); continue
            try:
                validation = execute_case(run, viewer_exe, telemetry_dir, repo_root, dry_run=args.dry_run)
                exit_code = EXIT_COMPLETE_VALID if args.dry_run or validation.get("stable60FpsStatus") == "achieved" else EXIT_QUALITY_GATE_FAILED
            except FormalRunError as exc:
                validation = {"evidenceStatus": "complete_invalid", "stable60FpsStatus": "not_achieved",
                              "integrityIssues": exc.issues, "goalFailures": []}
                exit_code = exc.exit_code
                if args.fail_policy == "stop":
                    rows.append(metric_row(run, validation, exit_code, False)); break
            rows.append(metric_row(run, validation, exit_code, False))
        method_rows = aggregate_rows(rows, "method"); scenario_rows = aggregate_rows(rows, "scenario")
        goal_rows = evaluate_goals(config, rows); determinism_rows = validate_determinism(rows); long_run_rows = validate_long_runs(config, rows)
        evidence_valid = len(rows) == len(runs) and all(row.get("evidence_status") == "complete_valid" for row in rows)
        determinism_passed = all(row.get("passed") == "yes" for row in determinism_rows)
        long_runs_passed = all(row.get("passed") == "yes" for row in long_run_rows)
        evidence_status = "complete_valid" if evidence_valid and determinism_passed and long_runs_passed else "complete_invalid"
        full_matrix_requested = not (args.only_method or args.only_scenario or args.only_seed)
        if full_matrix_requested:
            goals_passed = bool(goal_rows) and all(row.get("passed") == "yes" for row in goal_rows)
        else:
            # A smoke/baseline subset cannot produce every scenario-specific metric.
            # Its status is based on the selected runs' own stable-60 gates.
            goals_passed = bool(rows) and all(row.get("stable_60fps_status") == "achieved" for row in rows)
        stable_status = "achieved" if evidence_status == "complete_valid" and goals_passed else "not_achieved"
        if not full_matrix_requested and evidence_status == "complete_valid":
            suite_status = "partial_valid_goal_achieved" if stable_status == "achieved" else "partial_valid_goal_not_achieved"
        else:
            suite_status = "complete_valid_goal_achieved" if stable_status == "achieved" else ("complete_valid_goal_not_achieved" if evidence_status == "complete_valid" else "complete_invalid")
        summary_path = outputs_dir / "runtime_suite_summary.json"
        files = {
            "runs": outputs_dir / "runtime_suite_runs.csv", "methods": outputs_dir / "runtime_suite_methods.csv",
            "scenarios": outputs_dir / "runtime_suite_scenarios.csv", "goals": outputs_dir / "runtime_suite_goals.csv",
            "determinism": outputs_dir / "runtime_suite_determinism.csv", "long_runs": outputs_dir / "runtime_suite_long_runs.csv",
            "markdown": outputs_dir / "runtime_suite_report.md", "database": outputs_dir / "runtime_suite.sqlite3",
            "preflight": outputs_dir / "runtime_suite_preflight.json",
        }
        write_csv(files["runs"], rows); write_csv(files["methods"], method_rows); write_csv(files["scenarios"], scenario_rows)
        write_csv(files["goals"], goal_rows); write_csv(files["determinism"], determinism_rows); write_csv(files["long_runs"], long_run_rows)
        write_database(files["database"], rows)
        payload = {"suiteVersion": SUITE_VERSION, "suiteName": config.get("suite_name"), "suiteStatus": suite_status,
                   "evidenceStatus": evidence_status, "stable60FpsStatus": stable_status,
                   "hypothesisSupportStatus": "supported" if stable_status == "achieved" else "not_supported",
                   "runCount": len(rows), "expectedRunCount": len(runs),
                   "fullMatrixRequested": full_matrix_requested,
                   "validEvidenceCount": sum(row.get("evidence_status") == "complete_valid" for row in rows),
                   "stable60RunCount": sum(row.get("stable_60fps_status") == "achieved" for row in rows),
                   "goalRows": goal_rows, "determinismRows": determinism_rows, "longRunRows": long_run_rows,
                   "reproManifestPath": str(outputs_dir / "runtime_suite_repro_manifest.json")}
        write_markdown(files["markdown"], payload)
        write_json(summary_path, payload)  # final summary write: never mutate after this line
        generated = [summary_path, *files.values()]
        repro_path = outputs_dir / "runtime_suite_repro_manifest.json"
        write_repro_manifest(repro_path, generated, config_path)
        if not verify_repro_manifest(repro_path):
            print("Reproducibility manifest verification failed.", file=sys.stderr); return EXIT_FORMAL_CONTRACT_INVALID
        archive = outputs_dir / "runtime_suite_results.zip"
        package_outputs(archive, generated + [repro_path], outputs_dir)
        if evidence_status != "complete_valid":
            return EXIT_FORMAL_CONTRACT_INVALID
        return EXIT_COMPLETE_VALID if stable_status == "achieved" else EXIT_QUALITY_GATE_FAILED
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        print(f"Configuration failure: {exc}", file=sys.stderr)
        return EXIT_FORMAL_CONTRACT_INVALID


if __name__ == "__main__":
    raise SystemExit(main())
