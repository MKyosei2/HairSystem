from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import random
import shutil
import statistics
import sys
import zipfile
from pathlib import Path
from typing import Any, Iterable

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

from hairgpu_final_db import FinalStageDatabase, RuntimeRunMetadata
from hairgpu_final_excel import export_workbook
from hairgpu_final_run import (
    FormalRunError,
    canonical_json_sha256,
    normalize_method_code,
    run_viewer,
    sha256_file,
    write_json,
)

try:
    import matplotlib.pyplot as plt
except Exception:
    plt = None

DEFAULT_METHOD_ORDER = ["B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8", "P"]
PRIMARY_BASELINE = "B8"
PERFORMANCE_BASELINE = "B6"
QUALITY_BASELINE = "B0"

PAIR_METRICS = (
    "avg_frame_ms",
    "p95_frame_ms",
    "p99_frame_ms",
    "avg_hair_gpu_ms",
    "sixty_fps_ratio",
    "false_static_rate",
    "predictive_wake_recall",
    "repair_verified_success_rate",
    "collision_miss_rate",
    "state_oscillation_rate",
    "max_boundary_error_mm",
)


def read_json(path: Path) -> dict[str, Any]:
    payload = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    if not isinstance(payload, dict):
        raise ValueError(f"Expected JSON object: {path}")
    return payload


def _atomic_write_text(path: Path, text: str) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".tmp.{os.getpid()}")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as handle:
            handle.write(text)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    if not rows:
        _atomic_write_text(path, "")
        return
    headers: list[str] = []
    seen: set[str] = set()
    for row in rows:
        for key in row:
            if key not in seen:
                headers.append(key)
                seen.add(key)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".tmp.{os.getpid()}")
    try:
        with temporary.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=headers, extrasaction="ignore")
            writer.writeheader()
            writer.writerows(rows)
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def resolve_path(base_dir: Path, value: str | Path | None) -> Path | None:
    if value in (None, ""):
        return None
    path = Path(value)
    return path.resolve() if path.is_absolute() else (base_dir / path).resolve()


def _safe_name(value: Any) -> str:
    text = str(value)
    return "".join(
        character if character.isalnum() or character in ("-", "_") else "_"
        for character in text
    ).strip("_")


def canonical_run_hash(
    method: dict[str, Any],
    scenario: dict[str, Any],
    seed: int,
    repetition: int,
    defaults: dict[str, Any],
) -> str:
    return canonical_json_sha256(
        {
            "method": method,
            "scenario": scenario,
            "seed": seed,
            "repetition": repetition,
            "defaults": defaults,
        }
    )


def _normalize_method_entry(value: Any) -> dict[str, Any]:
    if isinstance(value, str):
        return {"code": normalize_method_code(value), "settings": {}}
    if not isinstance(value, dict):
        raise ValueError(f"Invalid method entry: {value!r}")
    result = dict(value)
    result["code"] = normalize_method_code(str(result.get("code") or result.get("method") or ""))
    result.setdefault("settings", {})
    return result


def _normalize_scenario_entry(value: Any) -> dict[str, Any]:
    if isinstance(value, str):
        return {"code": value, "timeline": ""}
    if not isinstance(value, dict):
        raise ValueError(f"Invalid scenario entry: {value!r}")
    result = dict(value)
    result["code"] = str(result.get("code") or result.get("scenario") or "")
    return result


def expand_run_matrix(config: dict[str, Any], base_dir: Path) -> list[dict[str, Any]]:
    explicit = config.get("runs")
    generation = config.get("run_generation", {})
    if explicit and generation.get("mode") != "cartesian_product":
        return [dict(run) for run in explicit]

    methods = [_normalize_method_entry(item) for item in config.get("methods", [])]
    scenarios = [_normalize_scenario_entry(item) for item in config.get("scenarios", [])]
    seeds = [int(seed) for seed in config.get("seeds", [])]
    repetitions = max(int(config.get("repetitions_per_seed", 1)), 1)
    defaults = dict(config.get("run_defaults", {}))
    experiment = str(config.get("experiment_name", config.get("suite_name", "runtime_suite")))
    if not methods or not scenarios or not seeds:
        raise ValueError(
            "Cartesian run generation requires non-empty methods, scenarios, and seeds"
        )

    telemetry_dir = resolve_path(base_dir, config.get("telemetry_dir")) or (
        base_dir / "runtime_telemetry"
    )
    statistics_config = dict(config.get("statistics", {}))
    randomize = bool(statistics_config.get("randomize_execution_order", True))
    random_seed = int(statistics_config.get("execution_order_seed", 20260716))
    rng = random.Random(random_seed)

    # Block by scenario × seed × repetition, then randomize only method order
    # inside each block. This prevents thermal drift from being confounded with
    # one method while retaining matched-pair structure.
    blocks: list[list[dict[str, Any]]] = []
    for scenario in scenarios:
        scenario_code = str(scenario["code"])
        for seed in seeds:
            for repetition in range(repetitions):
                ordered_methods = list(methods)
                if randomize:
                    rng.shuffle(ordered_methods)
                block: list[dict[str, Any]] = []
                for method in ordered_methods:
                    method_code = normalize_method_code(str(method["code"]))
                    case_id = (
                        f"{_safe_name(method_code)}__{_safe_name(scenario_code)}__"
                        f"seed{seed}__r{repetition:02d}"
                    )
                    run = dict(defaults)
                    run.update(
                        {
                            "experiment": experiment,
                            "case_id": case_id,
                            "run_name": case_id,
                            "method": method_code,
                            "method_settings": dict(method.get("settings", {})),
                            "scenario": scenario_code,
                            "scenario_timeline": scenario.get("timeline", ""),
                            "scenario_assertions": dict(
                                scenario.get("assertions", {})
                            ),
                            "measurement_mode": scenario.get(
                                "measurement_mode",
                                defaults.get("measurement_mode", "performance"),
                            ),
                            "seed": seed,
                            "repeat_index": repetition,
                            "execution_block": (
                                f"{_safe_name(scenario_code)}__seed{seed}__r{repetition:02d}"
                            ),
                            "settings_hash": canonical_run_hash(
                                method, scenario, seed, repetition, defaults
                            ),
                        }
                    )
                    by_method = scenario.get("assertions_by_method", {})
                    if isinstance(by_method, dict) and method_code in by_method:
                        run["scenario_assertions"].update(by_method[method_code])
                    run_telemetry_dir = (
                        resolve_path(base_dir, run.get("telemetry_dir"))
                        or telemetry_dir
                    )
                    run.setdefault(
                        "summary_json",
                        str(run_telemetry_dir / f"{case_id}_summary.json"),
                    )
                    run.setdefault(
                        "frame_csv",
                        str(run_telemetry_dir / f"{case_id}_frames.csv"),
                    )
                    run.setdefault(
                        "frame_jsonl",
                        str(run_telemetry_dir / f"{case_id}_frames.jsonl"),
                    )
                    run.setdefault(
                        "settings_json",
                        str(run_telemetry_dir / f"{case_id}_settings.json"),
                    )
                    block.append(run)
                blocks.append(block)
    if bool(statistics_config.get("randomize_block_order", True)):
        rng.shuffle(blocks)
    return [run for block in blocks for run in block]


def expected_telemetry_paths(
    base_dir: Path,
    run: dict[str, Any],
    default_telemetry_dir: Path,
) -> dict[str, Path]:
    run_name = str(run["run_name"])
    telemetry_dir = resolve_path(base_dir, run.get("telemetry_dir")) or default_telemetry_dir
    return {
        "summary": resolve_path(base_dir, run.get("summary_json"))
        or telemetry_dir / f"{run_name}_summary.json",
        "frame_csv": resolve_path(base_dir, run.get("frame_csv"))
        or telemetry_dir / f"{run_name}_frames.csv",
        "frame_jsonl": resolve_path(base_dir, run.get("frame_jsonl"))
        or telemetry_dir / f"{run_name}_frames.jsonl",
        "settings": resolve_path(base_dir, run.get("settings_json"))
        or telemetry_dir / f"{run_name}_settings.json",
        "environment": telemetry_dir / f"{run_name}_environment.json",
        "validation": telemetry_dir / f"{run_name}_validation.json",
        "manifest": telemetry_dir / f"{run_name}_manifest.json",
        "case": telemetry_dir / f"{run_name}_case.json",
        "preflight": telemetry_dir / f"{run_name}_preflight.json",
        "hardware": telemetry_dir / f"{run_name}_hardware_timeseries.csv",
        "error": telemetry_dir / f"{run_name}_error.json",
    }


def _verify_manifest(
    manifest_path: Path,
    expected_paths: Iterable[Path],
    canonical_hash: str | None = None,
) -> bool:
    try:
        manifest = read_json(manifest_path)
        entries = manifest.get("artifacts")
        if not isinstance(entries, list):
            return False
        lookup = {
            str(entry.get("path")): entry
            for entry in entries
            if isinstance(entry, dict) and entry.get("path")
        }
        if canonical_hash and str(manifest.get("canonicalResultSha256") or "").lower() != canonical_hash.lower():
            return False
        for path in expected_paths:
            path = Path(path)
            if not path.is_file():
                return False
            entry = lookup.get(path.name)
            if not entry:
                return False
            if str(entry.get("sha256") or "").lower() != sha256_file(path).lower():
                return False
            if int(entry.get("sizeBytes", -1)) != path.stat().st_size:
                return False
        return True
    except Exception:
        return False


def is_completed_case(
    base_dir: Path,
    run: dict[str, Any],
    default_telemetry_dir: Path,
) -> bool:
    paths = expected_telemetry_paths(base_dir, run, default_telemetry_dir)
    if paths["error"].exists():
        return False
    required = [
        paths["summary"],
        paths["frame_csv"],
        paths["frame_jsonl"],
        paths["settings"],
        paths["environment"],
        paths["validation"],
        paths["manifest"],
    ]
    if any(not path.is_file() for path in required):
        return False
    try:
        validation = read_json(paths["validation"])
        summary = read_json(paths["summary"])
        if validation.get("status") != "complete_valid" or not validation.get(
            "isValid", False
        ):
            return False
        if summary.get("validationStatus") != "complete_valid":
            return False
        if str(summary.get("caseId") or summary.get("case_id") or "") != str(
            run.get("case_id", "")
        ):
            return False
        if str(summary.get("settingsSha256") or "").lower() != str(
            run.get("settings_hash", "")
        ).lower():
            return False
        return _verify_manifest(
            paths["manifest"],
            required[:-1],
            str(summary.get("canonicalResultSha256") or "") or None,
        )
    except Exception:
        return False


def validate_suite_manifest(
    config: dict[str, Any], runs: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    preflight = dict(config.get("preflight", {}))
    method_codes = {
        normalize_method_code(str(item.get("code") if isinstance(item, dict) else item))
        for item in config.get("methods", [])
    }
    scenario_codes = {
        str(item.get("code") if isinstance(item, dict) else item)
        for item in config.get("scenarios", [])
    }
    seeds = {int(seed) for seed in config.get("seeds", [])}

    def issue(code: str, message: str, expected: Any = None, observed: Any = None) -> None:
        issues.append(
            {
                "code": code,
                "message": message,
                "expected": expected,
                "observed": observed,
            }
        )

    required_methods = {
        normalize_method_code(value)
        for value in preflight.get("required_method_codes", ["B0", "B6", "B8", "P"])
    }
    missing_methods = sorted(required_methods - method_codes)
    if missing_methods:
        issue("missing_methods", "Required methods are absent.", sorted(required_methods), sorted(method_codes))
    required_scenarios = set(preflight.get("required_scenario_codes", []))
    missing_scenarios = sorted(required_scenarios - scenario_codes)
    if missing_scenarios:
        issue("missing_scenarios", "Required scenarios are absent.", sorted(required_scenarios), sorted(scenario_codes))
    minimum_seed_count = int(preflight.get("minimum_seed_count", 5))
    if len(seeds) < minimum_seed_count:
        issue("seed_count", "Insufficient distinct seeds.", minimum_seed_count, len(seeds))

    repetitions = max(int(config.get("repetitions_per_seed", 1)), 1)
    expected_count = len(method_codes) * len(scenario_codes) * len(seeds) * repetitions
    if len(runs) != expected_count:
        issue("run_count", "Expanded run matrix is incomplete.", expected_count, len(runs))
    identities = [str(run.get("case_id") or run.get("run_name") or "") for run in runs]
    if len(identities) != len(set(identities)):
        issue("duplicate_case_id", "Run matrix contains duplicate case identities.")

    defaults = dict(config.get("run_defaults", {}))
    if [int(defaults.get("width", 0)), int(defaults.get("height", 0))] != [1920, 1080]:
        issue("resolution", "Formal suite requires 1920x1080.")
    if abs(float(defaults.get("fixed_timestep_seconds", 0.0)) - 1.0 / 60.0) > 1.0e-6:
        issue("fixed_timestep", "Formal suite requires a 1/60 second fixed timestep.")
    if int(defaults.get("warmup_frames", 0)) < 600:
        issue("warmup_frames", "Warm-up frame count is too low.")
    if int(defaults.get("measurement_frames", 0)) < 6000:
        issue("measurement_frames", "Measurement frame count is too low.")
    if int(defaults.get("display_strand_count", 0)) != 100000:
        issue("display_strands", "Formal suite requires exactly 100000 displayed strands.")
    return issues


def run_preflight(
    config: dict[str, Any],
    config_path: Path,
    runs: list[dict[str, Any]],
    viewer_override: Path | None = None,
) -> dict[str, Any]:
    issues = validate_suite_manifest(config, runs)
    base_dir = config_path.parent
    defaults = dict(config.get("run_defaults", {}))
    viewer = viewer_override or resolve_path(base_dir, defaults.get("viewer_exe"))
    if sys.platform == "win32" and (viewer is None or not viewer.is_file()):
        issues.append(
            {
                "code": "viewer_missing",
                "message": "Viewer executable is missing.",
                "observed": str(viewer),
            }
        )
    asset_manifest = resolve_path(base_dir, defaults.get("asset_manifest"))
    if bool(config.get("preflight", {}).get("require_frozen_asset", True)):
        if asset_manifest is None or not asset_manifest.is_file():
            issues.append(
                {
                    "code": "asset_manifest_missing",
                    "message": "Frozen asset manifest is required.",
                }
            )
        else:
            manifest = read_json(asset_manifest)
            if manifest.get("freezeStatus") != "FROZEN":
                issues.append(
                    {
                        "code": "asset_not_frozen",
                        "message": "Asset manifest is not frozen.",
                    }
                )
    for scenario in config.get("scenarios", []):
        entry = _normalize_scenario_entry(scenario)
        timeline = resolve_path(base_dir, entry.get("timeline"))
        if timeline is None or not timeline.is_file():
            issues.append(
                {
                    "code": "timeline_missing",
                    "message": f"Timeline is missing for {entry.get('code')}",
                    "observed": str(timeline),
                }
            )
    return {
        "status": "passed" if not issues else "failed",
        "isValid": not issues,
        "expandedRunCount": len(runs),
        "issues": issues,
    }


def import_run(
    db: FinalStageDatabase,
    base_dir: Path,
    run: dict[str, Any],
    default_telemetry_dir: Path,
    viewer_override: Path | None = None,
) -> int:
    run_name = str(run["run_name"])
    paths = expected_telemetry_paths(base_dir, run, default_telemetry_dir)
    viewer_exe = viewer_override or resolve_path(base_dir, run.get("viewer_exe"))
    timeout_seconds = int(run.get("timeout_seconds", 900))

    if not is_completed_case(base_dir, run, default_telemetry_dir):
        if viewer_exe is None:
            raise ValueError(f"Run '{run_name}' must provide valid telemetry or viewer_exe")
        if sys.platform != "win32":
            raise RuntimeError(
                f"Run '{run_name}' requires viewer launch, which is Windows-only"
            )
        run_viewer(viewer_exe, paths["summary"].parent, run_name, timeout_seconds, run)

    if not is_completed_case(base_dir, run, default_telemetry_dir):
        raise RuntimeError(
            f"Run '{run_name}' is not complete_valid with intact artifacts"
        )

    metadata = RuntimeRunMetadata(
        experiment_name=str(run["experiment"]),
        method_code=normalize_method_code(str(run["method"])),
        scenario_code=str(run["scenario"]),
        asset_name=str(run.get("asset", "")),
        run_name=run_name,
        runtime_source=str(run.get("runtime_source", "dx11_viewer")),
        operator_name=str(run.get("operator", "")),
        notes=str(run.get("notes", "")),
    )
    return db.import_runtime_run(
        metadata,
        paths["summary"],
        paths["frame_csv"],
        frame_jsonl_path=paths["frame_jsonl"],
        settings_json_path=paths["settings"],
        environment_json_path=paths["environment"],
        validation_json_path=paths["validation"],
        manifest_json_path=paths["manifest"],
    )


def build_pairwise_rows(
    comparison_rows: list[dict[str, Any]], baseline_method: str
) -> list[dict[str, Any]]:
    lookup = {
        (str(row["scenario_code"]), str(row["method_code"])): row
        for row in comparison_rows
    }
    scenarios = sorted({str(row["scenario_code"]) for row in comparison_rows})
    methods = sorted(
        {str(row["method_code"]) for row in comparison_rows if row["method_code"] != baseline_method}
    )
    output: list[dict[str, Any]] = []
    for scenario in scenarios:
        baseline = lookup.get((scenario, baseline_method))
        if not baseline:
            continue
        for method in methods:
            row = lookup.get((scenario, method))
            if not row:
                continue
            result: dict[str, Any] = {
                "scenario_code": scenario,
                "method_code": method,
                "baseline_method": baseline_method,
            }
            for metric in (
                "avg_frame_ms",
                "avg_p95_frame_ms",
                "avg_p99_frame_ms",
                "avg_hair_gpu_ms",
                "avg_sixty_fps_ratio",
                "avg_false_static_rate",
                "avg_predictive_wake_recall",
                "avg_repair_verified_success_rate",
                "avg_collision_miss_rate",
                "avg_state_oscillation_rate",
                "avg_max_boundary_error_mm",
            ):
                target = row.get(metric)
                reference = baseline.get(metric)
                result[f"method_{metric}"] = target
                result[f"baseline_{metric}"] = reference
                result[f"delta_{metric}"] = (
                    float(target) - float(reference)
                    if target is not None and reference is not None
                    else None
                )
            output.append(result)
    return output


def paired_bootstrap_ci(
    values: list[float],
    iterations: int,
    confidence: float,
    seed: int = 20260716,
) -> tuple[float | None, float | None]:
    if not values:
        return None, None
    rng = random.Random(seed)
    means = [
        statistics.fmean(values[rng.randrange(len(values))] for _ in values)
        for _ in range(max(iterations, 1))
    ]
    means.sort()
    alpha = (1.0 - confidence) / 2.0
    low_index = min(max(int(alpha * len(means)), 0), len(means) - 1)
    high_index = min(max(int((1.0 - alpha) * len(means)) - 1, 0), len(means) - 1)
    return means[low_index], means[high_index]


def _paired_effect_size(values: list[float]) -> float | None:
    if len(values) < 2:
        return None
    deviation = statistics.stdev(values)
    return statistics.fmean(values) / deviation if deviation > 0 else None


def _holm_adjust(p_values: list[float]) -> list[float]:
    if not p_values:
        return []
    order = sorted(range(len(p_values)), key=lambda index: p_values[index])
    adjusted = [1.0] * len(p_values)
    running = 0.0
    count = len(p_values)
    for rank, index in enumerate(order):
        value = min(1.0, (count - rank) * p_values[index])
        running = max(running, value)
        adjusted[index] = running
    return adjusted


def build_matched_pair_rows(
    raw_runs: list[dict[str, Any]],
    baseline_method: str,
    statistics_config: dict[str, Any],
) -> list[dict[str, Any]]:
    by_key: dict[tuple[Any, ...], dict[str, dict[str, Any]]] = {}
    for row in raw_runs:
        key = (
            row.get("scenario_code"),
            row.get("seed"),
            row.get("asset_sha256"),
            row.get("hardware_profile"),
            row.get("repetition_index"),
            row.get("measurement_mode"),
        )
        by_key.setdefault(key, {})[str(row.get("method_code"))] = row

    iterations = int(statistics_config.get("bootstrap_iterations", 5000))
    confidence = float(statistics_config.get("confidence_level", 0.95))
    output: list[dict[str, Any]] = []
    pseudo_p_values: list[float] = []
    for scenario in sorted({str(key[0]) for key in by_key}):
        methods = sorted(
            {
                method
                for key, values in by_key.items()
                if str(key[0]) == scenario
                for method in values
                if method != baseline_method
            }
        )
        for method in methods:
            for metric in PAIR_METRICS:
                deltas: list[float] = []
                for key, methods_by_code in by_key.items():
                    if str(key[0]) != scenario:
                        continue
                    baseline = methods_by_code.get(baseline_method)
                    target = methods_by_code.get(method)
                    if not baseline or not target:
                        continue
                    baseline_value = baseline.get(metric)
                    target_value = target.get(metric)
                    if baseline_value is None or target_value is None:
                        continue
                    deltas.append(float(target_value) - float(baseline_value))
                if not deltas:
                    continue
                low, high = paired_bootstrap_ci(deltas, iterations, confidence)
                # Bootstrap sign probability is recorded as a conservative
                # two-sided pseudo-p value for Holm ordering.
                positive = sum(delta >= 0 for delta in deltas) / len(deltas)
                pseudo_p = min(1.0, 2.0 * min(positive, 1.0 - positive))
                pseudo_p_values.append(pseudo_p)
                output.append(
                    {
                        "scenario_code": scenario,
                        "method_code": method,
                        "baseline_method": baseline_method,
                        "metric": metric,
                        "matched_pair_count": len(deltas),
                        "mean_delta": statistics.fmean(deltas),
                        "median_delta": statistics.median(deltas),
                        "ci_low": low,
                        "ci_high": high,
                        "paired_effect_size_dz": _paired_effect_size(deltas),
                        "bootstrap_sign_p": pseudo_p,
                    }
                )
    adjusted = _holm_adjust(pseudo_p_values)
    for row, value in zip(output, adjusted):
        row["holm_adjusted_p"] = value
        row["ci_excludes_zero"] = (
            row["ci_low"] is not None
            and row["ci_high"] is not None
            and (row["ci_low"] > 0 or row["ci_high"] < 0)
        )
    return output


def build_scenario_coverage_rows(
    config: dict[str, Any],
    comparison_rows: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    present = {
        (str(row["scenario_code"]), str(row["method_code"]))
        for row in comparison_rows
    }
    matrix = config.get("scenario_matrix", [])
    if not matrix:
        matrix = [
            {
                "scenario": str(item.get("code") if isinstance(item, dict) else item),
                "required_methods": ["B0", "B6", "B8", "P"],
                "category": "formal",
            }
            for item in config.get("scenarios", [])
        ]
    rows: list[dict[str, Any]] = []
    for item in matrix:
        scenario = str(item.get("scenario", ""))
        methods = [normalize_method_code(value) for value in item.get("required_methods", [])]
        for method in methods:
            rows.append(
                {
                    "scenario_code": scenario,
                    "category": item.get("category", ""),
                    "required_method_code": method,
                    "present": "yes" if (scenario, method) in present else "no",
                    "notes": item.get("notes", ""),
                }
            )
    return rows


def build_goal_rows(
    summary: dict[str, Any], goals: dict[str, Any]
) -> list[dict[str, Any]]:
    mapping = (
        ("avg_p95_frame_ms", "max_avg_p95_frame_ms", "le"),
        ("avg_p99_frame_ms", "max_avg_p99_frame_ms", "le"),
        ("avg_frame_ms", "max_avg_frame_ms", "le"),
        ("avg_sixty_fps_ratio", "min_avg_sixty_fps_ratio", "ge"),
        ("avg_fps", "min_avg_fps", "ge"),
    )
    rows: list[dict[str, Any]] = []
    for actual_key, threshold_key, comparator in mapping:
        if threshold_key not in goals:
            continue
        actual = summary.get(actual_key)
        threshold = float(goals[threshold_key])
        passed = actual is not None and (
            float(actual) <= threshold if comparator == "le" else float(actual) >= threshold
        )
        rows.append(
            {
                "goal": threshold_key,
                "actual": actual,
                "threshold": threshold,
                "passed": "yes" if passed else "no",
            }
        )
    return rows


def write_markdown(
    path: Path,
    summary: dict[str, Any],
    method_rows: list[dict[str, Any]],
    coverage_rows: list[dict[str, Any]],
    failed_runs: list[dict[str, Any]],
) -> None:
    lines = [
        "# HairSystem Formal Runtime Suite",
        "",
        f"- Valid runs: {summary.get('run_count', 0)}",
        f"- Methods: {summary.get('method_count', 0)}",
        f"- Scenarios: {summary.get('scenario_count', 0)}",
        f"- Average p95 frame time: {float(summary.get('avg_p95_frame_ms') or 0.0):.3f} ms",
        f"- Average p99 frame time: {float(summary.get('avg_p99_frame_ms') or 0.0):.3f} ms",
        f"- Failed runs: {len(failed_runs)}",
        "",
        "## Method summary",
        "",
        "| Method | Runs | Avg ms | P95 ms | P99 ms | Hair GPU ms | 60 FPS ratio | False static | Wake recall | Repair verified | Collision miss |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in method_rows:
        lines.append(
            "| {method_code} | {run_count} | {avg_frame_ms:.3f} | "
            "{avg_p95_frame_ms:.3f} | {avg_p99_frame_ms:.3f} | "
            "{avg_hair_gpu_ms:.3f} | {avg_sixty_fps_ratio:.3f} | "
            "{avg_false_static_rate:.5f} | {avg_predictive_wake_recall:.5f} | "
            "{avg_repair_verified_success_rate:.5f} | {avg_collision_miss_rate:.5f} |".format(
                **{
                    key: (float(row.get(key) or 0.0) if key != "method_code" else row.get(key))
                    for key in (
                        "method_code", "run_count", "avg_frame_ms",
                        "avg_p95_frame_ms", "avg_p99_frame_ms", "avg_hair_gpu_ms",
                        "avg_sixty_fps_ratio", "avg_false_static_rate",
                        "avg_predictive_wake_recall",
                        "avg_repair_verified_success_rate", "avg_collision_miss_rate",
                    )
                }
            )
        )
    missing = [row for row in coverage_rows if row["present"] != "yes"]
    lines.extend(["", "## Coverage", "", f"- Missing required cells: {len(missing)}"])
    if failed_runs:
        lines.extend(["", "## Failed runs", ""])
        for row in failed_runs:
            lines.append(f"- `{row.get('run_name')}`: {row.get('error')}")
    _atomic_write_text(path, "\n".join(lines) + "\n")


def emit_charts(
    charts_dir: Path,
    method_rows: list[dict[str, Any]],
) -> list[Path]:
    if plt is None or not method_rows:
        return []
    charts_dir.mkdir(parents=True, exist_ok=True)
    ordered = sorted(
        method_rows,
        key=lambda row: (
            DEFAULT_METHOD_ORDER.index(row["method_code"])
            if row["method_code"] in DEFAULT_METHOD_ORDER
            else len(DEFAULT_METHOD_ORDER)
        ),
    )
    labels = [row["method_code"] for row in ordered]
    outputs: list[Path] = []
    for metric, title, ylabel, filename in (
        ("avg_p95_frame_ms", "P95 frame time by method", "P95 frame ms", "method_p95_frame_ms.png"),
        ("avg_p99_frame_ms", "P99 frame time by method", "P99 frame ms", "method_p99_frame_ms.png"),
        ("avg_hair_gpu_ms", "Hair GPU time by method", "Hair GPU ms", "method_hair_gpu_ms.png"),
        ("avg_sixty_fps_ratio", "60 FPS ratio by method", "60 FPS ratio", "method_sixty_fps_ratio.png"),
    ):
        values = [float(row.get(metric) or 0.0) for row in ordered]
        plt.figure(figsize=(9, 4.8))
        plt.bar(labels, values)
        plt.ylabel(ylabel)
        plt.title(title)
        plt.xticks(rotation=20, ha="right")
        plt.tight_layout()
        output = charts_dir / filename
        plt.savefig(output, dpi=160)
        plt.close()
        outputs.append(output)
    return outputs


def write_repro_manifest(
    path: Path,
    config_path: Path,
    generated_files: list[Path],
    telemetry_files: list[Path],
) -> dict[str, Any]:
    files = []
    for item in sorted(set(generated_files + telemetry_files)):
        if item.is_file():
            files.append(
                {
                    "path": str(item),
                    "sha256": sha256_file(item),
                    "sizeBytes": item.stat().st_size,
                }
            )
    manifest = {
        "manifestVersion": "01-safe-v3.2-suite",
        "configPath": str(config_path),
        "configSha256": sha256_file(config_path),
        "generatedFiles": files,
    }
    manifest["manifestSha256"] = canonical_json_sha256(manifest)
    write_json(path, manifest)
    return manifest


def _zip_add_tree(
    archive: zipfile.ZipFile,
    source: Path,
    archive_root: str,
    seen: set[str],
) -> None:
    source = Path(source)
    if not source.exists():
        return
    files = [source] if source.is_file() else [path for path in source.rglob("*") if path.is_file()]
    for path in sorted(files):
        relative = path.name if source.is_file() else path.relative_to(source).as_posix()
        arcname = f"{archive_root.rstrip('/')}/{relative}" if archive_root else relative
        if arcname in seen:
            continue
        archive.write(path, arcname=arcname)
        seen.add(arcname)


def package_complete_results(
    zip_path: Path,
    outputs_dir: Path,
    telemetry_dir: Path,
    db_path: Path,
    excel_path: Path,
    config_path: Path,
) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = zip_path.with_name(zip_path.name + ".tmp")
    seen: set[str] = set()
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        _zip_add_tree(archive, outputs_dir, "outputs", seen)
        _zip_add_tree(archive, telemetry_dir, "runtime_telemetry", seen)
        _zip_add_tree(archive, db_path, "database", seen)
        _zip_add_tree(archive, excel_path, "reports", seen)
        _zip_add_tree(archive, config_path, "config", seen)
    os.replace(temporary, zip_path)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run/import a formal HairSystem runtime suite."
    )
    parser.add_argument("--config", required=True)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--rerun-failed", action="store_true")
    parser.add_argument("--viewer-exe")
    parser.add_argument("--output")
    parser.add_argument(
        "--fail-policy",
        choices=("stop", "continue"),
        default="stop",
    )
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    config = read_json(config_path)
    base_dir = config_path.parent
    if args.viewer_exe:
        config.setdefault("run_defaults", {})["viewer_exe"] = str(
            Path(args.viewer_exe).resolve()
        )
    if args.output:
        config["outputs_dir"] = str(Path(args.output).resolve())

    runs = expand_run_matrix(config, base_dir)
    viewer_override = Path(args.viewer_exe).resolve() if args.viewer_exe else None
    preflight = run_preflight(config, config_path, runs, viewer_override)

    db_path = resolve_path(base_dir, config.get("db")) or (
        base_dir / "runtime_suite.sqlite"
    )
    excel_path = resolve_path(base_dir, config.get("excel")) or db_path.with_suffix(
        ".xlsx"
    )
    telemetry_dir = resolve_path(base_dir, config.get("telemetry_dir")) or (
        base_dir / "runtime_telemetry"
    )
    schema_path = resolve_path(base_dir, config.get("schema")) or (
        THIS_DIR / "schema.sql"
    )
    outputs_dir = resolve_path(base_dir, config.get("outputs_dir")) or (
        base_dir / "runtime_suite_outputs"
    )
    outputs_dir.mkdir(parents=True, exist_ok=True)
    write_json(
        outputs_dir / "expanded_run_matrix.json",
        {"runCount": len(runs), "runs": runs},
    )
    write_json(outputs_dir / "preflight_result.json", preflight)
    if not preflight["isValid"]:
        raise RuntimeError(f"Suite preflight failed: {preflight['issues']}")
    if args.dry_run:
        print(f"Expanded {len(runs)} valid runs")
        return 0

    suite_name = str(config.get("suite_name", config_path.stem))
    imported_ids: list[int] = []
    skipped_names: list[str] = []
    failed_runs: list[dict[str, Any]] = []
    db = FinalStageDatabase(db_path)
    db.initialize(schema_path)
    db.clear_suite_rows(suite_name)

    for run in runs:
        paths = expected_telemetry_paths(base_dir, run, telemetry_dir)
        if args.rerun_failed and paths["error"].exists():
            paths["error"].unlink()
        if args.resume and is_completed_case(base_dir, run, telemetry_dir):
            skipped_names.append(str(run["run_name"]))
            # Completed telemetry still needs importing if it is not already in
            # the current database. Duplicate identities are handled below.
        try:
            imported_ids.append(
                import_run(db, base_dir, run, telemetry_dir, viewer_override)
            )
        except Exception as exc:
            failed_runs.append(
                {
                    "run_name": run.get("run_name"),
                    "method": run.get("method"),
                    "scenario": run.get("scenario"),
                    "seed": run.get("seed"),
                    "repeat_index": run.get("repeat_index"),
                    "error": str(exc),
                }
            )
            if args.fail_policy == "stop":
                db.close()
                raise

    method_rows = db.fetch_method_summary_rows()
    scenario_rows = db.fetch_scenario_summary_rows()
    summary = db.fetch_experiment_summary()
    comparison_rows = [dict(row) for row in db.fetch_comparison_rows()]
    raw_valid_runs = [
        dict(row) for row in db.fetch_runs() if int(row["is_valid"] or 0) == 1
    ]

    baselines = {
        "representation": normalize_method_code(
            config.get("baseline_method", PRIMARY_BASELINE)
        ),
        "performance": normalize_method_code(
            config.get("performance_baseline_method", PERFORMANCE_BASELINE)
        ),
        "quality": normalize_method_code(
            config.get("quality_baseline_method", QUALITY_BASELINE)
        ),
    }
    pairwise_by_purpose = {
        purpose: build_pairwise_rows(comparison_rows, baseline)
        for purpose, baseline in baselines.items()
    }
    matched_by_purpose = {
        purpose: build_matched_pair_rows(
            raw_valid_runs, baseline, dict(config.get("statistics", {}))
        )
        for purpose, baseline in baselines.items()
    }
    coverage_rows = build_scenario_coverage_rows(config, comparison_rows)
    for row in coverage_rows:
        db.add_scenario_coverage(
            suite_name,
            row["scenario_code"],
            row["required_method_code"],
            row["present"] == "yes",
            category=row.get("category", ""),
            notes=row.get("notes", ""),
        )
    goal_rows = build_goal_rows(summary, dict(config.get("goals", {})))
    db.close()

    export_workbook(db_path, excel_path)
    charts = emit_charts(outputs_dir / "charts", method_rows)

    summary_path = outputs_dir / "runtime_suite_summary.json"
    method_csv = outputs_dir / "runtime_suite_method_summary.csv"
    scenario_csv = outputs_dir / "runtime_suite_scenario_summary.csv"
    coverage_csv = outputs_dir / "runtime_suite_scenario_coverage.csv"
    goals_csv = outputs_dir / "runtime_suite_goal_checks.csv"
    failures_csv = outputs_dir / "runtime_suite_failed_runs.csv"
    markdown_path = outputs_dir / "runtime_suite_summary.md"

    write_csv(method_csv, method_rows)
    write_csv(scenario_csv, scenario_rows)
    write_csv(coverage_csv, coverage_rows)
    write_csv(goals_csv, goal_rows)
    write_csv(failures_csv, failed_runs)
    for purpose, rows in pairwise_by_purpose.items():
        write_csv(outputs_dir / f"runtime_suite_pairwise_{purpose}.csv", rows)
    for purpose, rows in matched_by_purpose.items():
        write_csv(outputs_dir / f"runtime_suite_matched_pairs_{purpose}.csv", rows)
    write_markdown(markdown_path, summary, method_rows, coverage_rows, failed_runs)

    missing_coverage = [row for row in coverage_rows if row["present"] != "yes"]
    suite_status = (
        "complete_valid"
        if not failed_runs and not missing_coverage
        else "complete_invalid"
    )
    payload: dict[str, Any] = {
        "suiteName": suite_name,
        "validationStatus": suite_status,
        "expandedRunCount": len(runs),
        "importedRunIds": imported_ids,
        "skippedRunNames": skipped_names,
        "invalidRunCount": len(failed_runs),
        "failedRuns": failed_runs,
        "summary": summary,
        "methodSummary": method_rows,
        "scenarioSummary": scenario_rows,
        "baselines": baselines,
        "pairwise": pairwise_by_purpose,
        "matchedPairs": matched_by_purpose,
        "scenarioCoverage": coverage_rows,
        "goalChecks": goal_rows,
        "preflight": preflight,
        "databasePath": str(db_path),
        "excelPath": str(excel_path),
        "charts": [str(path) for path in charts],
    }
    write_json(summary_path, payload)

    telemetry_files = [path for path in telemetry_dir.rglob("*") if path.is_file()]
    generated_files = [
        summary_path,
        method_csv,
        scenario_csv,
        coverage_csv,
        goals_csv,
        failures_csv,
        markdown_path,
        db_path,
        excel_path,
        *charts,
    ]
    generated_files.extend(outputs_dir.glob("runtime_suite_pairwise_*.csv"))
    generated_files.extend(outputs_dir.glob("runtime_suite_matched_pairs_*.csv"))
    repro_manifest_path = outputs_dir / "runtime_suite_repro_manifest.json"
    repro_manifest = write_repro_manifest(
        repro_manifest_path, config_path, list(generated_files), telemetry_files
    )
    payload["reproManifest"] = repro_manifest
    write_json(summary_path, payload)

    package_zip = resolve_path(base_dir, config.get("package_zip"))
    if package_zip:
        package_complete_results(
            package_zip,
            outputs_dir,
            telemetry_dir,
            db_path,
            excel_path,
            config_path,
        )

    print(f"Imported {len(imported_ids)} runs")
    print(f"Failed {len(failed_runs)} runs")
    print(f"Database: {db_path}")
    print(f"Excel: {excel_path}")
    return 0 if suite_status == "complete_valid" else 2


if __name__ == "__main__":
    raise SystemExit(main())
