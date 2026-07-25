from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import re
import subprocess
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable


DEFAULT_REQUIRED_METHODS = {
    "full_physics",
    "strand_sleep",
    "block_sleep",
    "segment_sleep_threshold",
    "segment_sleep_hysteresis",
    "segment_sleep_cache",
    "segment_sleep_compaction",
    "active_set_runtime",
    "external_state_array",
    "proposal_hybrid",
}

DEFAULT_REQUIRED_SCENARIOS = {
    "S01_Idle",
    "S02_Breathing",
    "S03_Walk",
    "S04_SuddenStop",
    "S05_HeadRotation",
    "S06_Teleport",
    "S07_ColliderApproach",
    "S08_StaticDynamicBoundaryContact",
    "S09_StrongWind",
    "S10_WindStop",
    "S11_LocalEdit",
    "S12_FalseStaticTrap",
    "S13_RepairStorm",
    "S14_AllDynamic",
    "S15_AllStatic",
    "S16_StateThrashing",
    "S17_CloseCamera100k",
    "S18_FarCamera100k",
}

PENDING_STATUS_VALUES = {
    "pending",
    "not_executed",
    "not_executed_by_policy_safe_build",
    "build_only",
    "estimated",
    "synthetic",
    "placeholder",
}

FRAME_TIME_COLUMN_CANDIDATES = (
    "frameMilliseconds",
    "frame_ms",
    "frameMs",
    "totalFrameMilliseconds",
    "cpuFrameMilliseconds",
)


class PreflightError(RuntimeError):
    pass


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def as_bool(value: Any) -> bool:
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    return str(value).strip().lower() in {"1", "true", "yes", "on", "real", "measured"}


def safe_name(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return value.strip("_") or "unnamed"


def resolve_path(base_dir: Path, value: str | None) -> Path | None:
    if not value:
        return None
    path = Path(value)
    return path.resolve() if path.is_absolute() else (base_dir / path).resolve()


def add_issue(
    issues: list[dict[str, Any]],
    severity: str,
    code: str,
    message: str,
    *,
    run_name: str = "",
    path: str = "",
) -> None:
    issues.append(
        {
            "severity": severity,
            "code": code,
            "message": message,
            "run_name": run_name,
            "path": path,
        }
    )


def merge_dicts(*values: dict[str, Any]) -> dict[str, Any]:
    merged: dict[str, Any] = {}
    for value in values:
        merged.update(value)
    return merged


def validate_config(config: dict[str, Any], config_dir: Path) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    preflight = config.get("preflight", {})
    defaults = config.get("run_defaults", {})
    methods = config.get("methods", [])
    scenarios = config.get("scenarios", [])
    seeds = config.get("seeds", [])

    method_codes = [str(item.get("code", "")).strip() for item in methods]
    scenario_codes = [str(item.get("code", "")).strip() for item in scenarios]

    for code, count in Counter(method_codes).items():
        if not code:
            add_issue(issues, "error", "METHOD_CODE_EMPTY", "Every method requires a non-empty code.")
        elif count > 1:
            add_issue(issues, "error", "METHOD_CODE_DUPLICATE", f"Method code '{code}' is duplicated.")

    for code, count in Counter(scenario_codes).items():
        if not code:
            add_issue(issues, "error", "SCENARIO_CODE_EMPTY", "Every scenario requires a non-empty code.")
        elif count > 1:
            add_issue(issues, "error", "SCENARIO_CODE_DUPLICATE", f"Scenario code '{code}' is duplicated.")

    missing_methods = sorted(DEFAULT_REQUIRED_METHODS - set(method_codes))
    if missing_methods:
        add_issue(
            issues,
            "error",
            "REQUIRED_METHODS_MISSING",
            "Missing mandatory strong baselines: " + ", ".join(missing_methods),
        )

    missing_scenarios = sorted(DEFAULT_REQUIRED_SCENARIOS - set(scenario_codes))
    if missing_scenarios:
        add_issue(
            issues,
            "error",
            "REQUIRED_SCENARIOS_MISSING",
            "Missing mandatory scenarios: " + ", ".join(missing_scenarios),
        )

    min_seed_count = int(preflight.get("minimum_seed_count", 5))
    unique_seeds = sorted({int(seed) for seed in seeds})
    if len(unique_seeds) < min_seed_count:
        add_issue(
            issues,
            "error",
            "SEED_COVERAGE_LOW",
            f"At least {min_seed_count} independent seeds are required; found {len(unique_seeds)}.",
        )

    if int(defaults.get("display_strand_count", 0)) != 100000:
        add_issue(
            issues,
            "error",
            "DISPLAY_STRAND_COUNT_INVALID",
            "The final Novelty 01 evaluation must use exactly 100,000 displayed strands.",
        )

    guide_count = int(defaults.get("guide_strand_count", 0))
    if guide_count < 2000 or guide_count > 5000:
        add_issue(
            issues,
            "warning",
            "GUIDE_COUNT_OUTSIDE_DECLARED_RANGE",
            "The declared evaluation range is 2,000 to 5,000 guides.",
        )

    if preflight.get("require_release_build", True) and defaults.get("build_configuration") != "Release":
        add_issue(issues, "error", "NOT_RELEASE", "run_defaults.build_configuration must be 'Release'.")

    required_platform = str(preflight.get("required_platform", "x64"))
    if str(defaults.get("platform", "")) != required_platform:
        add_issue(
            issues,
            "error",
            "PLATFORM_MISMATCH",
            f"run_defaults.platform must be '{required_platform}'.",
        )

    required_resolution = preflight.get("required_resolution", [1920, 1080])
    actual_resolution = [int(defaults.get("width", 0)), int(defaults.get("height", 0))]
    if actual_resolution != list(required_resolution):
        add_issue(
            issues,
            "error",
            "RESOLUTION_MISMATCH",
            f"Resolution must be {required_resolution[0]}x{required_resolution[1]}.",
        )

    if preflight.get("require_vsync_disabled", True) and as_bool(defaults.get("vsync", True)):
        add_issue(issues, "error", "VSYNC_ENABLED", "VSync must be disabled.")

    if preflight.get("require_fixed_timestep", True) and not as_bool(defaults.get("fixed_timestep", False)):
        add_issue(issues, "error", "FIXED_TIMESTEP_DISABLED", "Fixed timestep must be enabled.")

    expected_dt = float(preflight.get("fixed_timestep_seconds", 1.0 / 60.0))
    tolerance = float(preflight.get("fixed_timestep_tolerance_seconds", 1e-5))
    actual_dt = float(defaults.get("fixed_timestep_seconds", 0.0))
    if not math.isfinite(actual_dt) or abs(actual_dt - expected_dt) > tolerance:
        add_issue(
            issues,
            "error",
            "FIXED_TIMESTEP_MISMATCH",
            f"fixed_timestep_seconds must be {expected_dt} ± {tolerance}.",
        )

    min_warmup = int(preflight.get("minimum_warmup_frames", 600))
    min_measurement = int(preflight.get("minimum_measurement_frames", 6000))
    if int(defaults.get("warmup_frames", 0)) < min_warmup:
        add_issue(issues, "error", "WARMUP_TOO_SHORT", f"Warm-up must be at least {min_warmup} frames.")
    if int(defaults.get("measurement_frames", 0)) < min_measurement:
        add_issue(
            issues,
            "error",
            "MEASUREMENT_TOO_SHORT",
            f"Measurement must be at least {min_measurement} frames.",
        )

    baseline = str(config.get("baseline_method", ""))
    if baseline != "external_state_array":
        add_issue(
            issues,
            "error",
            "WEAK_PRIMARY_BASELINE",
            "The primary novelty baseline must be external_state_array so the same decisions are compared under a different representation.",
        )

    for scenario in scenarios:
        timeline = resolve_path(config_dir, scenario.get("timeline"))
        if timeline is None or not timeline.exists():
            add_issue(
                issues,
                "error",
                "SCENARIO_TIMELINE_MISSING",
                f"Scenario '{scenario.get('code', '')}' timeline is missing.",
                path=str(timeline or ""),
            )

    asset_manifest = resolve_path(config_dir, defaults.get("asset_manifest"))
    if asset_manifest is None or not asset_manifest.exists():
        add_issue(
            issues,
            "error",
            "ASSET_MANIFEST_MISSING",
            "The deterministic 100k asset manifest is missing.",
            path=str(asset_manifest or ""),
        )
    else:
        try:
            asset_contract = read_json(asset_manifest)
            source_path = str(asset_contract.get("sourceAssetPath", ""))
            source_sha = str(asset_contract.get("sourceAssetSha256", ""))
            if not source_path or source_path.startswith("REPLACE_"):
                add_issue(
                    issues,
                    "error",
                    "ASSET_PATH_NOT_FROZEN",
                    "Freeze the final asset and replace sourceAssetPath in the asset manifest.",
                    path=str(asset_manifest),
                )
            if not re.fullmatch(r"[0-9a-fA-F]{64}", source_sha):
                add_issue(
                    issues,
                    "error",
                    "ASSET_HASH_NOT_FROZEN",
                    "sourceAssetSha256 must contain the final 64-character SHA-256.",
                    path=str(asset_manifest),
                )
        except Exception as exc:
            add_issue(
                issues,
                "error",
                "ASSET_MANIFEST_PARSE_FAILED",
                f"Failed to parse the asset manifest: {type(exc).__name__}: {exc}",
                path=str(asset_manifest),
            )

    return issues


def expand_runs(config: dict[str, Any], config_dir: Path) -> list[dict[str, Any]]:
    defaults = dict(config.get("run_defaults", {}))
    telemetry_dir = resolve_path(config_dir, config.get("telemetry_dir"))
    if telemetry_dir is None:
        telemetry_dir = (config_dir / "runtime_telemetry").resolve()

    experiment_name = str(config.get("experiment_name", config.get("suite_name", "novelty01")))
    methods = config.get("methods", [])
    scenarios = config.get("scenarios", [])
    seeds = [int(seed) for seed in config.get("seeds", [])]

    absolute_defaults = dict(defaults)
    for path_key in ("viewer_exe", "asset_manifest"):
        if defaults.get(path_key):
            absolute_defaults[path_key] = str(resolve_path(config_dir, str(defaults[path_key])))

    runs: list[dict[str, Any]] = []
    case_index = 0
    for scenario in scenarios:
        scenario_code = str(scenario["code"])
        scenario_timeline = resolve_path(config_dir, scenario.get("timeline"))
        scenario_settings = dict(scenario.get("settings", {}))
        for method in methods:
            method_code = str(method["code"])
            method_settings = dict(method.get("settings", {}))
            for seed in seeds:
                case_index += 1
                run_name = safe_name(f"{scenario_code}__{method_code}__seed{seed}")
                run = merge_dicts(absolute_defaults, scenario_settings, method_settings)
                run.update(
                    {
                        "case_id": f"N01_{case_index:04d}",
                        "experiment": experiment_name,
                        "method": method_code,
                        "variant": method_code,
                        "scenario": scenario_code,
                        "scenario_category": scenario.get("category", ""),
                        "scenario_timeline": str(scenario_timeline or ""),
                        "seed": seed,
                        "run_name": run_name,
                        "telemetry_dir": str(telemetry_dir),
                        "summary_json": str(telemetry_dir / f"{run_name}_summary.json"),
                        "frame_csv": str(telemetry_dir / f"{run_name}_frames.csv"),
                        "frame_jsonl": str(telemetry_dir / f"{run_name}_frames.jsonl"),
                        "settings_json": str(telemetry_dir / f"{run_name}_settings.json"),
                        "notes": (
                            f"{method.get('label', method_code)} | "
                            f"{scenario.get('category', '')} | deterministic seed {seed}"
                        ),
                    }
                )
                runs.append(run)
    return runs


def validate_expanded_runs(config: dict[str, Any], runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    methods = {str(item["code"]) for item in config.get("methods", [])}
    scenarios = {str(item["code"]) for item in config.get("scenarios", [])}
    seeds = {int(seed) for seed in config.get("seeds", [])}
    expected = {(scenario, method, seed) for scenario in scenarios for method in methods for seed in seeds}
    actual = {
        (str(run.get("scenario", "")), str(run.get("method", "")), int(run.get("seed", -1)))
        for run in runs
    }

    missing = sorted(expected - actual)
    duplicates = Counter(
        (str(run.get("scenario", "")), str(run.get("method", "")), int(run.get("seed", -1)))
        for run in runs
    )
    duplicate_cells = [cell for cell, count in duplicates.items() if count > 1]

    if missing:
        add_issue(
            issues,
            "error",
            "MATRIX_CELLS_MISSING",
            f"The expanded matrix is missing {len(missing)} scenario-method-seed cells.",
        )
    if duplicate_cells:
        add_issue(
            issues,
            "error",
            "MATRIX_CELLS_DUPLICATED",
            f"The expanded matrix contains {len(duplicate_cells)} duplicated cells.",
        )

    run_names = [str(run.get("run_name", "")) for run in runs]
    duplicate_names = [name for name, count in Counter(run_names).items() if count > 1]
    if duplicate_names:
        add_issue(
            issues,
            "error",
            "RUN_NAME_DUPLICATE",
            "Duplicate run names: " + ", ".join(duplicate_names[:10]),
        )

    comparable_keys = (
        "asset",
        "display_strand_count",
        "guide_strand_count",
        "particles_per_guide",
        "width",
        "height",
        "fixed_timestep_seconds",
        "solver_iterations",
    )
    by_scenario_seed: dict[tuple[str, int], list[dict[str, Any]]] = defaultdict(list)
    for run in runs:
        by_scenario_seed[(str(run["scenario"]), int(run["seed"]))].append(run)

    for (scenario, seed), group in by_scenario_seed.items():
        for key in comparable_keys:
            values = {json.dumps(item.get(key), sort_keys=True) for item in group}
            if len(values) > 1:
                add_issue(
                    issues,
                    "error",
                    "UNFAIR_METHOD_INPUT",
                    f"Scenario {scenario}, seed {seed}: '{key}' differs across methods.",
                )

    return issues


def percentile(sorted_values: list[float], q: float) -> float:
    if not sorted_values:
        return 0.0
    if len(sorted_values) == 1:
        return sorted_values[0]
    position = (len(sorted_values) - 1) * q
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return sorted_values[lower]
    weight = position - lower
    return sorted_values[lower] * (1.0 - weight) + sorted_values[upper] * weight


def find_first_value(sources: Iterable[dict[str, Any]], names: Iterable[str]) -> Any:
    for source in sources:
        for name in names:
            if name in source and source[name] not in (None, ""):
                return source[name]
    return None


def flatten_dict(payload: dict[str, Any], prefix: str = "") -> dict[str, Any]:
    flat: dict[str, Any] = {}
    for key, value in payload.items():
        path = f"{prefix}.{key}" if prefix else str(key)
        flat[path] = value
        flat[str(key)] = value
        if isinstance(value, dict):
            flat.update(flatten_dict(value, path))
    return flat


def read_frame_csv(path: Path) -> tuple[list[dict[str, str]], list[str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
        return rows, list(reader.fieldnames or [])


def validate_run_telemetry(
    run: dict[str, Any],
    config: dict[str, Any],
    config_dir: Path,
    issues: list[dict[str, Any]],
) -> dict[str, Any]:
    preflight = config.get("preflight", {})
    run_name = str(run["run_name"])
    summary_path = resolve_path(config_dir, str(run.get("summary_json", "")))
    frame_path = resolve_path(config_dir, str(run.get("frame_csv", "")))
    settings_path = resolve_path(config_dir, str(run.get("settings_json", "")))

    row: dict[str, Any] = {
        "run_name": run_name,
        "scenario": run.get("scenario", ""),
        "method": run.get("method", ""),
        "seed": run.get("seed", 0),
        "telemetry_valid": False,
        "measured_frame_count": 0,
    }

    missing_paths = [path for path in (summary_path, frame_path, settings_path) if path is None or not path.exists()]
    if missing_paths:
        add_issue(
            issues,
            "error",
            "TELEMETRY_FILE_MISSING",
            "Summary, frame CSV, or settings JSON is missing.",
            run_name=run_name,
            path=", ".join(str(path) for path in missing_paths),
        )
        return row

    try:
        summary = read_json(summary_path)
        settings = read_json(settings_path)
        frame_rows, frame_columns = read_frame_csv(frame_path)
    except Exception as exc:
        add_issue(
            issues,
            "error",
            "TELEMETRY_PARSE_FAILED",
            f"Failed to parse telemetry: {type(exc).__name__}: {exc}",
            run_name=run_name,
        )
        return row

    flat_summary = flatten_dict(summary)
    flat_settings = flatten_dict(settings)
    sources = (flat_settings, flat_summary, run)

    status = str(find_first_value(sources, ("status", "measurementStatus", "measurement.status")) or "").strip().lower()
    if preflight.get("reject_pending_or_synthetic_results", True) and status in PENDING_STATUS_VALUES:
        add_issue(
            issues,
            "error",
            "NON_REAL_STATUS",
            f"Telemetry status '{status}' is not publishable real measurement evidence.",
            run_name=run_name,
        )

    real_measurement = as_bool(
        find_first_value(
            sources,
            ("realMeasurement", "real_measurement", "real_fps_available", "measurement.real"),
        )
    )
    if preflight.get("require_real_measurement", True) and not real_measurement:
        add_issue(
            issues,
            "error",
            "REAL_MEASUREMENT_FALSE",
            "The executable did not confirm a real measurement run.",
            run_name=run_name,
        )

    build_configuration = str(
        find_first_value(sources, ("buildConfiguration", "build_configuration", "configuration")) or ""
    )
    platform = str(find_first_value(sources, ("platform", "targetPlatform")) or "")
    if preflight.get("require_release_build", True) and build_configuration != "Release":
        add_issue(
            issues,
            "error",
            "TELEMETRY_NOT_RELEASE",
            f"Observed build configuration is '{build_configuration or 'missing'}'.",
            run_name=run_name,
        )
    if platform != str(preflight.get("required_platform", "x64")):
        add_issue(
            issues,
            "error",
            "TELEMETRY_PLATFORM_MISMATCH",
            f"Observed platform is '{platform or 'missing'}'.",
            run_name=run_name,
        )

    required_hashes = [str(item) for item in preflight.get("require_hashes", [])]
    required_hardware = [str(item) for item in preflight.get("require_hardware_fields", [])]
    for field in required_hashes + required_hardware:
        value = find_first_value(sources, (field, f"reproducibility.{field}", f"hardware.{field}"))
        if value in (None, ""):
            add_issue(
                issues,
                "error",
                "REPRO_FIELD_MISSING",
                f"Required reproducibility field '{field}' is missing.",
                run_name=run_name,
            )

    required_telemetry = [str(item) for item in preflight.get("required_telemetry_fields", [])]
    available_fields = set(frame_columns) | set(flat_summary) | set(flat_settings)
    missing_telemetry = [field for field in required_telemetry if field not in available_fields]
    if missing_telemetry:
        add_issue(
            issues,
            "error",
            "REQUIRED_TELEMETRY_FIELDS_MISSING",
            "Missing fields: " + ", ".join(missing_telemetry),
            run_name=run_name,
        )

    minimum_frames = int(preflight.get("minimum_measurement_frames", 6000))
    if len(frame_rows) < minimum_frames:
        add_issue(
            issues,
            "error",
            "FRAME_SAMPLE_COUNT_LOW",
            f"Expected at least {minimum_frames} measured frames; found {len(frame_rows)}.",
            run_name=run_name,
        )

    required_candidates = tuple(preflight.get("required_frame_columns_any", FRAME_TIME_COLUMN_CANDIDATES))
    frame_time_column = next((name for name in required_candidates if name in frame_columns), None)
    if not frame_time_column:
        add_issue(
            issues,
            "error",
            "FRAME_TIME_COLUMN_MISSING",
            "No accepted frame-time column exists in the frame CSV.",
            run_name=run_name,
        )
        return row

    frame_values: list[float] = []
    for index, frame in enumerate(frame_rows):
        try:
            value = float(frame.get(frame_time_column, ""))
        except (TypeError, ValueError):
            add_issue(
                issues,
                "error",
                "FRAME_TIME_PARSE_FAILED",
                f"Invalid frame time at row {index + 2}.",
                run_name=run_name,
            )
            continue
        if not math.isfinite(value) or value < 0.0:
            add_issue(
                issues,
                "error",
                "FRAME_TIME_NONFINITE",
                f"Non-finite or negative frame time at row {index + 2}.",
                run_name=run_name,
            )
            continue
        frame_values.append(value)

    if not frame_values:
        return row

    ordered = sorted(frame_values)
    frame_budget = float(preflight.get("frame_budget_milliseconds", 16.67))
    overrun_count = sum(value > frame_budget for value in frame_values)
    row.update(
        {
            "telemetry_valid": True,
            "real_measurement": real_measurement,
            "build_configuration": build_configuration,
            "platform": platform,
            "summary_sha256": sha256_file(summary_path),
            "frame_csv_sha256": sha256_file(frame_path),
            "settings_sha256": sha256_file(settings_path),
            "measured_frame_count": len(frame_values),
            "avg_frame_ms": sum(frame_values) / len(frame_values),
            "p50_frame_ms": percentile(ordered, 0.50),
            "p95_frame_ms": percentile(ordered, 0.95),
            "p99_frame_ms": percentile(ordered, 0.99),
            "p999_frame_ms": percentile(ordered, 0.999),
            "max_frame_ms": ordered[-1],
            "frame_budget_overrun_rate": overrun_count / len(frame_values),
        }
    )
    return row


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    headers: list[str] = []
    seen: set[str] = set()
    for row in rows:
        for key in row:
            if key not in seen:
                seen.add(key)
                headers.append(key)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)


def write_markdown_report(
    path: Path,
    config_path: Path,
    expanded_path: Path,
    issues: list[dict[str, Any]],
    runs: list[dict[str, Any]],
    telemetry_rows: list[dict[str, Any]],
) -> None:
    counts = Counter(issue["severity"] for issue in issues)
    error_count = counts.get("error", 0)
    warning_count = counts.get("warning", 0)
    status = "PASS" if error_count == 0 else "FAIL"

    lines = [
        "# Novelty 01 Benchmark Preflight",
        "",
        f"- Status: **{status}**",
        f"- Config: `{config_path}`",
        f"- Expanded suite: `{expanded_path}`",
        f"- Expanded runs: {len(runs)}",
        f"- Errors: {error_count}",
        f"- Warnings: {warning_count}",
        "",
        "## Evidence policy",
        "",
        "Only Release/x64, executable-confirmed real measurements with VSync disabled, fixed timestep, required hashes, hardware identity, sufficient warm-up, and sufficient measured frames are accepted.",
        "Build-only, pending, estimated, placeholder, or synthetic FPS values are rejected.",
        "",
    ]

    if telemetry_rows:
        valid_rows = [row for row in telemetry_rows if row.get("telemetry_valid")]
        lines += [
            "## Telemetry summary",
            "",
            f"- Telemetry rows: {len(telemetry_rows)}",
            f"- Parsed frame-time rows: {len(valid_rows)}",
            "",
            "| Run | Scenario | Method | Seed | Frames | p95 ms | p99 ms | p99.9 ms | Overrun rate |",
            "|---|---|---|---:|---:|---:|---:|---:|---:|",
        ]
        for row in valid_rows[:200]:
            lines.append(
                f"| {row['run_name']} | {row['scenario']} | {row['method']} | {row['seed']} | "
                f"{row['measured_frame_count']} | {row['p95_frame_ms']:.3f} | "
                f"{row['p99_frame_ms']:.3f} | {row['p999_frame_ms']:.3f} | "
                f"{row['frame_budget_overrun_rate']:.4f} |"
            )
        lines.append("")

    lines += ["## Issues", ""]
    if not issues:
        lines.append("No contract violations were detected.")
    else:
        lines += [
            "| Severity | Code | Run | Message | Path |",
            "|---|---|---|---|---|",
        ]
        for issue in issues:
            message = str(issue["message"]).replace("|", "\\|").replace("\n", " ")
            issue_path = str(issue.get("path", "")).replace("|", "\\|")
            lines.append(
                f"| {issue['severity']} | {issue['code']} | {issue.get('run_name', '')} | {message} | {issue_path} |"
            )

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Expand and validate the Novelty 01 research matrix before importing it into "
            "hairgpu_runtime_suite.py."
        )
    )
    parser.add_argument("--config", required=True, help="Extended Novelty 01 suite JSON.")
    parser.add_argument("--output-config", help="Expanded runtime-suite JSON path.")
    parser.add_argument("--report-dir", help="Directory for preflight reports.")
    parser.add_argument(
        "--validate-telemetry",
        action="store_true",
        help="Require and validate real summary/settings/frame files for every expanded run.",
    )
    parser.add_argument(
        "--run-suite",
        action="store_true",
        help="Run hairgpu_runtime_suite.py after preflight passes.",
    )
    parser.add_argument("--runtime-suite-script", help="Override hairgpu_runtime_suite.py path.")
    parser.add_argument("--python", default=sys.executable, help="Python executable for --run-suite.")
    parser.add_argument(
        "--allow-incomplete",
        action="store_true",
        help="Return success even when preflight errors exist. Reports still record failures.",
    )
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    config_dir = config_path.parent
    config = read_json(config_path)

    report_dir = Path(args.report_dir).resolve() if args.report_dir else (config_dir / "novelty01_preflight")
    expanded_path = (
        Path(args.output_config).resolve()
        if args.output_config
        else report_dir / "novelty01_expanded_runtime_suite.json"
    )

    issues = validate_config(config, config_dir)
    runs = expand_runs(config, config_dir)
    issues.extend(validate_expanded_runs(config, runs))

    expanded_config = dict(config)
    expanded_config["runs"] = runs
    expanded_config["source_config"] = str(config_path)
    expanded_config["source_config_sha256"] = sha256_file(config_path)
    expanded_config["preflight_generated"] = True
    expanded_config["scenario_matrix"] = [
        {
            "scenario": str(scenario["code"]),
            "category": scenario.get("category", ""),
            "required_methods": [str(method["code"]) for method in config.get("methods", [])],
            "notes": "Novelty 01 safe matrix; all methods are required for every seed.",
        }
        for scenario in config.get("scenarios", [])
    ]
    for path_key in (
        "db",
        "excel",
        "outputs_dir",
        "telemetry_dir",
        "schema",
        "metric_definitions",
        "package_zip",
        "user_task_results_csv",
    ):
        if expanded_config.get(path_key):
            expanded_config[path_key] = str(resolve_path(config_dir, str(expanded_config[path_key])))
    expanded_config.pop("methods", None)
    expanded_config.pop("scenarios", None)
    expanded_config.pop("seeds", None)
    expanded_config.pop("run_defaults", None)
    write_json(expanded_path, expanded_config)

    telemetry_rows: list[dict[str, Any]] = []
    if args.validate_telemetry:
        for run in runs:
            telemetry_rows.append(validate_run_telemetry(run, config, config_dir, issues))

    report_payload = {
        "status": "PASS" if not any(issue["severity"] == "error" for issue in issues) else "FAIL",
        "config": str(config_path),
        "configSha256": sha256_file(config_path),
        "expandedConfig": str(expanded_path),
        "expandedConfigSha256": sha256_file(expanded_path),
        "expandedRunCount": len(runs),
        "expectedMatrixCellCount": (
            len(config.get("methods", []))
            * len(config.get("scenarios", []))
            * len(set(config.get("seeds", [])))
        ),
        "validateTelemetry": bool(args.validate_telemetry),
        "issues": issues,
        "telemetryRows": telemetry_rows,
    }

    report_dir.mkdir(parents=True, exist_ok=True)
    report_json = report_dir / "novelty01_preflight_report.json"
    report_md = report_dir / "novelty01_preflight_report.md"
    telemetry_csv = report_dir / "novelty01_telemetry_tail_statistics.csv"
    write_json(report_json, report_payload)
    write_markdown_report(report_md, config_path, expanded_path, issues, runs, telemetry_rows)
    write_csv(telemetry_csv, telemetry_rows)

    error_count = sum(issue["severity"] == "error" for issue in issues)
    warning_count = sum(issue["severity"] == "warning" for issue in issues)
    print(f"Expanded runs: {len(runs)}")
    print(f"Preflight errors: {error_count}")
    print(f"Preflight warnings: {warning_count}")
    print(f"Expanded config: {expanded_path}")
    print(f"Report: {report_md}")

    if args.run_suite:
        if error_count > 0:
            raise PreflightError("Runtime suite was not started because preflight failed.")
        runtime_suite_script = (
            Path(args.runtime_suite_script).resolve()
            if args.runtime_suite_script
            else Path(__file__).resolve().parent / "hairgpu_runtime_suite.py"
        )
        command = [args.python, str(runtime_suite_script), "--config", str(expanded_path)]
        completed = subprocess.run(command, check=False)
        if completed.returncode != 0:
            return completed.returncode

    if error_count > 0 and not args.allow_incomplete:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
