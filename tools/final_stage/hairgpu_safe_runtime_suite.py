from __future__ import annotations

import argparse
import copy
import csv
import hashlib
import json
import math
import random
import re
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable

THIS_DIR = Path(__file__).resolve().parent
PLACEHOLDER_PATTERN = re.compile(r"(?:REPLACE|PENDING|NOT_EXECUTED|SYNTHETIC|UNKNOWN|TBD)", re.IGNORECASE)
FRAME_TIME_KEYS = ("frameMilliseconds", "frame_ms", "frameMs", "totalFrameMilliseconds", "frame_time_ms")


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def write_csv(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    headers: list[str] = []
    for row in rows:
        for key in row:
            if key not in headers:
                headers.append(key)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def canonical_json_sha256(payload: Any) -> str:
    encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return sha256_bytes(encoded)


def deep_merge(base: dict[str, Any], overlay: dict[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(base)
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = deep_merge(result[key], value)
        else:
            result[key] = copy.deepcopy(value)
    return result


def resolve_path(config_dir: Path, value: str | None) -> Path | None:
    if not value:
        return None
    path = Path(value)
    return path if path.is_absolute() else (config_dir / path).resolve()


def absolutize_run_paths(config_dir: Path, runs: list[dict[str, Any]]) -> None:
    path_keys = (
        "viewer_exe", "scenario_timeline", "asset_manifest", "telemetry_dir",
        "summary_json", "frame_csv", "frame_jsonl", "settings_json",
    )
    for run in runs:
        for key in path_keys:
            value = run.get(key)
            if value in (None, ""):
                continue
            path = Path(str(value))
            if not path.is_absolute():
                run[key] = str((config_dir / path).resolve())
        signature_payload = copy.deepcopy(run)
        signature_payload.pop("settings_hash", None)
        run["settings_hash"] = canonical_json_sha256(signature_payload)


def flatten_mapping(mapping: Any) -> dict[str, list[Any]]:
    flattened: dict[str, list[Any]] = {}

    def visit(value: Any, key: str = "") -> None:
        if isinstance(value, dict):
            for child_key, child_value in value.items():
                visit(child_value, str(child_key))
            return
        if isinstance(value, list):
            return
        token = normalize_key(key)
        if token:
            flattened.setdefault(token, []).append(value)

    visit(mapping)
    return flattened


def values_equivalent(expected: Any, observed: Any) -> bool:
    if isinstance(expected, bool):
        normalized = str(observed).strip().lower()
        return (expected and normalized in {"1", "true", "yes", "on"}) or (not expected and normalized in {"0", "false", "no", "off"})
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        try:
            return math.isclose(float(expected), float(observed), rel_tol=1.0e-6, abs_tol=1.0e-9)
        except (TypeError, ValueError):
            return False
    return str(expected).strip() == str(observed).strip()


def safe_slug(value: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return slug.strip("_") or "case"


def is_placeholder(value: Any) -> bool:
    if value is None:
        return True
    text = str(value).strip()
    return not text or bool(PLACEHOLDER_PATTERN.search(text))


def normalize_key(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())


def get_any(mapping: dict[str, Any], *keys: str, default: Any = None) -> Any:
    normalized = {normalize_key(str(k)): v for k, v in mapping.items()}
    for key in keys:
        token = normalize_key(key)
        if token in normalized and normalized[token] is not None:
            return normalized[token]
    return default


def expand_matrix(config: dict[str, Any]) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    expanded = copy.deepcopy(config)
    defaults = copy.deepcopy(config.get("run_defaults", {}))
    methods = config.get("methods", [])
    scenarios = config.get("scenarios", [])
    seeds = config.get("seeds", [])

    if config.get("runs"):
        runs = []
        for index, source in enumerate(config["runs"]):
            run = deep_merge(defaults, source)
            run.setdefault("case_id", f"case_{index:05d}")
            run.setdefault("run_name", safe_slug(str(run["case_id"])))
            run.setdefault("experiment", config.get("experiment_name", "runtime_suite"))
            run.setdefault("settings_hash", canonical_json_sha256(run))
            runs.append(run)
    else:
        if not methods or not scenarios or not seeds:
            raise ValueError("Matrix expansion requires non-empty methods, scenarios, and seeds when runs is empty.")
        runs = []
        case_index = 0
        for scenario in scenarios:
            scenario_code = str(scenario.get("code", "")).strip()
            if not scenario_code:
                raise ValueError("Every scenario requires a non-empty code.")
            for method in methods:
                method_code = str(method.get("code", "")).strip()
                if not method_code:
                    raise ValueError("Every method requires a non-empty code.")
                for seed in seeds:
                    case_index += 1
                    run = deep_merge(defaults, method.get("settings", {}))
                    run.update({
                        "experiment": config.get("experiment_name", "runtime_suite"),
                        "method": method_code,
                        "variant": method_code,
                        "method_label": method.get("label", method_code),
                        "method_settings": copy.deepcopy(method.get("settings", {})),
                        "scenario": scenario_code,
                        "scenario_category": scenario.get("category", ""),
                        "scenario_timeline": scenario.get("timeline", ""),
                        "seed": int(seed),
                    })
                    case_id = f"case_{case_index:05d}_{safe_slug(scenario_code)}_{safe_slug(method_code)}_seed{int(seed)}"
                    run["case_id"] = case_id
                    run["run_name"] = case_id
                    run["settings_hash"] = canonical_json_sha256({
                        "suite_version": config.get("suite_version", ""),
                        "run": run,
                    })
                    runs.append(run)

    expanded["runs"] = runs
    if not expanded.get("scenario_matrix") and methods and scenarios:
        required_methods = [str(item["code"]) for item in methods]
        expanded["scenario_matrix"] = [
            {
                "scenario": str(item["code"]),
                "category": item.get("category", ""),
                "required_methods": required_methods,
                "notes": "Automatically expanded safe matrix.",
            }
            for item in scenarios
        ]
    return expanded, runs


def validate_static_contract(config: dict[str, Any], config_path: Path, runs: list[dict[str, Any]]) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    config_dir = config_path.parent
    preflight = config.get("preflight", {})

    def issue(severity: str, code: str, message: str, case_id: str = "") -> None:
        issues.append({"severity": severity, "code": code, "case_id": case_id, "message": message})

    methods = config.get("methods", [])
    scenarios = config.get("scenarios", [])
    seeds = config.get("seeds", [])
    method_codes = [str(item.get("code", "")) for item in methods]
    if len(method_codes) != len(set(method_codes)):
        issue("error", "duplicate_method", "Method codes must be unique.")
    scenario_codes = [str(item.get("code", "")) for item in scenarios]
    if len(scenario_codes) != len(set(scenario_codes)):
        issue("error", "duplicate_scenario", "Scenario codes must be unique.")
    baseline = str(config.get("baseline_method", ""))
    if baseline and baseline not in method_codes:
        issue("error", "missing_baseline", f"Baseline method '{baseline}' is not present in methods.")

    minimum_seed_count = int(preflight.get("minimum_seed_count", 1))
    if len(set(int(seed) for seed in seeds)) < minimum_seed_count:
        issue("error", "insufficient_seeds", f"At least {minimum_seed_count} independent seeds are required.")

    expected_count = len(methods) * len(scenarios) * len(seeds) if methods and scenarios and seeds else len(runs)
    if len(runs) != expected_count:
        issue("error", "matrix_size_mismatch", f"Expanded run count is {len(runs)} but expected {expected_count}.")

    for scenario in scenarios:
        timeline = resolve_path(config_dir, scenario.get("timeline"))
        if timeline is None or not timeline.exists():
            issue("error", "missing_timeline", f"Scenario timeline is missing: {scenario.get('timeline', '')}")
        elif timeline.suffix.lower() != ".json":
            issue("error", "invalid_timeline_type", f"Scenario timeline must be JSON: {timeline}")

    asset_manifest = resolve_path(config_dir, config.get("run_defaults", {}).get("asset_manifest"))
    if asset_manifest is None or not asset_manifest.exists():
        issue("error", "missing_asset_manifest", "The 100k asset manifest is missing.")
    else:
        try:
            asset_payload = read_json(asset_manifest)
            if is_placeholder(asset_payload.get("sourceAssetPath")) or is_placeholder(asset_payload.get("sourceAssetSha256")):
                issue("error", "unfrozen_asset", "The 100k benchmark asset is not frozen; replace path/hash placeholders before evidence capture.")
            generation = asset_payload.get("generation", {})
            if int(generation.get("displayStrandCount", 0)) != 100000:
                issue("error", "wrong_strand_count", "Benchmark asset must contain exactly 100,000 displayed strands.")
        except Exception as exc:
            issue("error", "invalid_asset_manifest", f"Failed to read asset manifest: {exc}")

    seen_case_ids: set[str] = set()
    for run in runs:
        case_id = str(run.get("case_id", ""))
        if not case_id or case_id in seen_case_ids:
            issue("error", "duplicate_case_id", "Every run requires a unique case_id.", case_id)
        seen_case_ids.add(case_id)
        if preflight.get("require_release_build", True) and str(run.get("build_configuration", "")) != "Release":
            issue("error", "non_release_run", "Run is not configured for Release.", case_id)
        required_platform = str(preflight.get("required_platform", "x64"))
        if str(run.get("platform", "")) != required_platform:
            issue("error", "wrong_platform", f"Run platform must be {required_platform}.", case_id)
        if int(run.get("warmup_frames", 0)) < int(preflight.get("minimum_warmup_frames", 0)):
            issue("error", "short_warmup", "Warm-up frame count is below the contract.", case_id)
        if int(run.get("measurement_frames", 0)) < int(preflight.get("minimum_measurement_frames", 0)):
            issue("error", "short_measurement", "Measurement frame count is below the contract.", case_id)
        if int(run.get("display_strand_count", 0)) != 100000:
            issue("error", "run_strand_count", "Run must request exactly 100,000 displayed strands.", case_id)
    return issues


def read_frame_csv(path: Path) -> tuple[list[dict[str, str]], list[float], set[str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
        headers = set(reader.fieldnames or [])
    frame_key = next((key for key in FRAME_TIME_KEYS if key in headers), None)
    values: list[float] = []
    if frame_key:
        for row in rows:
            raw = row.get(frame_key, "")
            try:
                value = float(raw)
            except (TypeError, ValueError):
                continue
            if math.isfinite(value) and value > 0.0:
                values.append(value)
    return rows, values, headers


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * q
    lo = int(math.floor(position))
    hi = int(math.ceil(position))
    if lo == hi:
        return ordered[lo]
    weight = position - lo
    return ordered[lo] * (1.0 - weight) + ordered[hi] * weight


def load_run_evidence(config_path: Path, config: dict[str, Any], run: dict[str, Any]) -> dict[str, Any]:
    config_dir = config_path.parent
    telemetry_root = resolve_path(config_dir, run.get("telemetry_dir") or config.get("telemetry_dir"))
    if telemetry_root is None:
        telemetry_root = config_dir / "runtime_telemetry"
    run_name = str(run["run_name"])
    summary_path = resolve_path(config_dir, run.get("summary_json")) or telemetry_root / f"{run_name}_summary.json"
    frame_path = resolve_path(config_dir, run.get("frame_csv")) or telemetry_root / f"{run_name}_frames.csv"
    settings_path = resolve_path(config_dir, run.get("settings_json")) or telemetry_root / f"{run_name}_settings.json"
    case_path = telemetry_root / f"{run_name}_case.json"

    summary = read_json(summary_path) if summary_path.exists() else {}
    settings = read_json(settings_path) if settings_path.exists() else {}
    case_snapshot = read_json(case_path) if case_path.exists() else {}
    rows: list[dict[str, str]] = []
    frame_ms: list[float] = []
    headers: set[str] = set()
    if frame_path.exists():
        rows, frame_ms, headers = read_frame_csv(frame_path)

    runtime_combined = deep_merge(settings, summary)
    combined = deep_merge(case_snapshot, runtime_combined)
    evidence = {
        "case_id": run.get("case_id", ""),
        "run_name": run_name,
        "method": run.get("method", ""),
        "scenario": run.get("scenario", ""),
        "seed": int(run.get("seed", 0)),
        "asset": run.get("asset", ""),
        "hardware_profile": run.get("hardware_profile", "default"),
        "summary_path": str(summary_path),
        "frame_path": str(frame_path),
        "settings_path": str(settings_path),
        "case_path": str(case_path),
        "summary_exists": summary_path.exists(),
        "frame_exists": frame_path.exists(),
        "settings_exists": settings_path.exists(),
        "frame_row_count": len(rows),
        "frame_headers": sorted(headers),
        "frame_ms": frame_ms,
        "combined": combined,
        "runtime_combined": runtime_combined,
        "case_snapshot": case_snapshot,
    }
    if frame_ms:
        budget = float(config.get("preflight", {}).get("frame_budget_milliseconds", 16.67))
        evidence["derived"] = {
            "avg_frame_ms": statistics.fmean(frame_ms),
            "p50_frame_ms": percentile(frame_ms, 0.50),
            "p95_frame_ms": percentile(frame_ms, 0.95),
            "p99_frame_ms": percentile(frame_ms, 0.99),
            "p999_frame_ms": percentile(frame_ms, 0.999),
            "worst_frame_ms": max(frame_ms),
            "sixty_fps_ratio": sum(value <= budget for value in frame_ms) / len(frame_ms),
            "frame_budget_overrun_rate": sum(value > budget for value in frame_ms) / len(frame_ms),
        }
    else:
        evidence["derived"] = {}
    return evidence


def validate_run_evidence(config: dict[str, Any], run: dict[str, Any], evidence: dict[str, Any]) -> list[dict[str, Any]]:
    preflight = config.get("preflight", {})
    issues: list[dict[str, Any]] = []
    case_id = str(run.get("case_id", ""))
    combined = evidence["combined"]
    runtime_combined = evidence.get("runtime_combined", {})
    runtime_flat = flatten_mapping(runtime_combined)

    def issue(severity: str, code: str, message: str) -> None:
        issues.append({"severity": severity, "code": code, "case_id": case_id, "message": message})

    if not evidence["summary_exists"]:
        issue("error", "missing_summary", "Summary JSON is missing.")
    if not evidence["frame_exists"]:
        issue("error", "missing_frames", "Frame CSV is missing.")
    if not evidence["settings_exists"]:
        issue("error", "missing_settings", "Settings JSON is missing.")

    status_text = " ".join(str(get_any(combined, key, default="")) for key in ("status", "measurementStatus", "realFpsAvailable"))
    if preflight.get("reject_pending_or_synthetic_results", True) and PLACEHOLDER_PATTERN.search(status_text):
        issue("error", "non_real_status", f"Pending/synthetic status is not admissible: {status_text.strip()}")

    real_measurement = get_any(runtime_combined, "realMeasurement", "real_measurement", "isRealMeasurement", default=None)
    if preflight.get("require_real_measurement", True) and real_measurement not in (True, 1, "1", "true", "True", "yes"):
        issue("error", "not_real_measurement", "Telemetry does not explicitly identify itself as a real measurement.")

    build_configuration = str(get_any(runtime_combined, "buildConfiguration", "build_configuration", "observedBuildConfiguration", default=""))
    if preflight.get("require_release_build", True) and build_configuration != "Release":
        issue("error", "non_release_evidence", f"Evidence build configuration is '{build_configuration}', not Release.")

    platform = str(get_any(runtime_combined, "platform", "observedPlatform", default=""))
    if platform != str(preflight.get("required_platform", "x64")):
        issue("error", "wrong_evidence_platform", f"Evidence platform is '{platform}'.")

    vsync = get_any(runtime_combined, "vsyncEnabled", "vsync", default=None)
    if preflight.get("require_vsync_disabled", True) and vsync not in (False, 0, "0", "false", "False"):
        issue("error", "vsync_enabled", "VSync must be disabled.")

    width = int(get_any(runtime_combined, "width", "renderWidth", default=0) or 0)
    height = int(get_any(runtime_combined, "height", "renderHeight", default=0) or 0)
    required_resolution = preflight.get("required_resolution", [1920, 1080])
    if [width, height] != [int(required_resolution[0]), int(required_resolution[1])]:
        issue("error", "wrong_resolution", f"Evidence resolution is {width}x{height}.")

    expected_frames = int(run.get("measurement_frames", preflight.get("minimum_measurement_frames", 0)))
    if evidence["frame_row_count"] < expected_frames:
        issue("error", "insufficient_frame_rows", f"Frame CSV has {evidence['frame_row_count']} rows; expected at least {expected_frames}.")
    if not evidence["frame_ms"]:
        issue("error", "missing_frame_time", "No valid positive finite frame-time column was found.")

    required_hashes = preflight.get("require_hashes", [])
    for key in required_hashes:
        value = get_any(combined, str(key), default=run.get(str(key), ""))
        if is_placeholder(value):
            issue("error", "missing_hash", f"Required reproducibility hash is missing or a placeholder: {key}")

    for key in preflight.get("require_hardware_fields", []):
        value = get_any(combined, str(key), default=run.get(str(key), ""))
        if is_placeholder(value):
            issue("error", "missing_hardware", f"Required hardware identity is missing: {key}")

    normalized_headers = {normalize_key(key) for key in evidence["frame_headers"]}
    normalized_combined = {normalize_key(str(key)) for key in combined.keys()}
    for key in preflight.get("required_telemetry_fields", []):
        token = normalize_key(str(key))
        if token not in normalized_headers and token not in normalized_combined:
            issue("error", "missing_telemetry_field", f"Required telemetry field is missing: {key}")

    if preflight.get("require_identity_match", True):
        identity_checks = {
            "case_id": ("caseId", "case_id"),
            "method": ("appliedMethodCode", "method", "variant"),
            "scenario": ("scenario",),
            "seed": ("seed",),
            "settings_hash": ("appliedMethodSettingsSha256", "settingsHash", "settings_hash"),
        }
        expected_values = {
            "case_id": str(run.get("case_id", "")),
            "method": str(run.get("method", "")),
            "scenario": str(run.get("scenario", "")),
            "seed": int(run.get("seed", 0)),
            "settings_hash": str(run.get("settings_hash", "")),
        }
        for label, keys in identity_checks.items():
            observed = get_any(runtime_combined, *keys, default=None)
            expected = expected_values[label]
            if observed is None or not values_equivalent(expected, observed):
                issue("error", "identity_mismatch", f"Runtime {label} does not match the requested case: expected={expected!r}, observed={observed!r}")

    if preflight.get("require_method_setting_acknowledgement", True):
        for key, expected in dict(run.get("method_settings", {})).items():
            token = normalize_key(str(key))
            observed_values = runtime_flat.get(token, [])
            if not observed_values:
                issue("error", "unacknowledged_method_setting", f"Runtime telemetry does not acknowledge method setting: {key}")
                continue
            if not any(values_equivalent(expected, observed) for observed in observed_values):
                issue("error", "method_setting_mismatch", f"Runtime method setting differs for {key}: expected={expected!r}, observed={observed_values!r}")

    for name, value in evidence.get("derived", {}).items():
        if not math.isfinite(float(value)):
            issue("error", "non_finite_metric", f"Derived metric is not finite: {name}")
    return issues


def bootstrap_ci(values: list[float], confidence: float, iterations: int, seed: int = 20260715) -> tuple[float, float]:
    if not values:
        return 0.0, 0.0
    if len(values) == 1:
        return values[0], values[0]
    rng = random.Random(seed)
    means = []
    for _ in range(max(iterations, 100)):
        sample = [values[rng.randrange(len(values))] for _ in values]
        means.append(statistics.fmean(sample))
    alpha = (1.0 - confidence) / 2.0
    return percentile(means, alpha), percentile(means, 1.0 - alpha)


def build_matched_pairs(config: dict[str, Any], evidences: list[dict[str, Any]]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    baseline_method = str(config.get("baseline_method", ""))
    stats = config.get("statistics", {})
    confidence = float(stats.get("confidence_level", 0.95))
    iterations = int(stats.get("bootstrap_iterations", 2000))
    min_seeds = int(stats.get("minimum_matched_seed_count", config.get("preflight", {}).get("minimum_seed_count", 1)))

    valid = [item for item in evidences if item.get("valid") and item.get("derived")]
    lookup = {
        (item["scenario"], item["seed"], item["asset"], item["hardware_profile"], item["method"]): item
        for item in valid
    }
    methods = sorted({item["method"] for item in valid if item["method"] != baseline_method})
    scenarios = sorted({item["scenario"] for item in valid})
    pair_rows: list[dict[str, Any]] = []
    aggregate_rows: list[dict[str, Any]] = []

    for scenario in scenarios:
        for method in methods:
            deltas_p95: list[float] = []
            deltas_p99: list[float] = []
            deltas_ratio: list[float] = []
            for item in valid:
                if item["scenario"] != scenario or item["method"] != method:
                    continue
                key = (item["scenario"], item["seed"], item["asset"], item["hardware_profile"], baseline_method)
                baseline = lookup.get(key)
                if not baseline:
                    continue
                row = {
                    "scenario": scenario,
                    "seed": item["seed"],
                    "asset": item["asset"],
                    "hardware_profile": item["hardware_profile"],
                    "method": method,
                    "baseline": baseline_method,
                    "delta_avg_frame_ms": item["derived"]["avg_frame_ms"] - baseline["derived"]["avg_frame_ms"],
                    "delta_p95_frame_ms": item["derived"]["p95_frame_ms"] - baseline["derived"]["p95_frame_ms"],
                    "delta_p99_frame_ms": item["derived"]["p99_frame_ms"] - baseline["derived"]["p99_frame_ms"],
                    "delta_sixty_fps_ratio": item["derived"]["sixty_fps_ratio"] - baseline["derived"]["sixty_fps_ratio"],
                }
                pair_rows.append(row)
                deltas_p95.append(float(row["delta_p95_frame_ms"]))
                deltas_p99.append(float(row["delta_p99_frame_ms"]))
                deltas_ratio.append(float(row["delta_sixty_fps_ratio"]))
            if deltas_p95:
                p95_lo, p95_hi = bootstrap_ci(deltas_p95, confidence, iterations)
                p99_lo, p99_hi = bootstrap_ci(deltas_p99, confidence, iterations, seed=20260716)
                ratio_lo, ratio_hi = bootstrap_ci(deltas_ratio, confidence, iterations, seed=20260717)
                aggregate_rows.append({
                    "scenario": scenario,
                    "method": method,
                    "baseline": baseline_method,
                    "matched_seed_count": len(deltas_p95),
                    "minimum_seed_count_met": len(deltas_p95) >= min_seeds,
                    "mean_delta_p95_frame_ms": statistics.fmean(deltas_p95),
                    "ci_low_delta_p95_frame_ms": p95_lo,
                    "ci_high_delta_p95_frame_ms": p95_hi,
                    "mean_delta_p99_frame_ms": statistics.fmean(deltas_p99),
                    "ci_low_delta_p99_frame_ms": p99_lo,
                    "ci_high_delta_p99_frame_ms": p99_hi,
                    "mean_delta_sixty_fps_ratio": statistics.fmean(deltas_ratio),
                    "ci_low_delta_sixty_fps_ratio": ratio_lo,
                    "ci_high_delta_sixty_fps_ratio": ratio_hi,
                    "conservative_performance_win": p95_hi < 0.0 and ratio_lo >= 0.0,
                })
    return pair_rows, aggregate_rows


def validate_coverage(config: dict[str, Any], evidences: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows = []
    valid_keys = {(item["scenario"], item["method"], item["seed"]) for item in evidences if item.get("valid")}
    for scenario in config.get("scenarios", []):
        for method in config.get("methods", []):
            for seed in config.get("seeds", []):
                key = (str(scenario["code"]), str(method["code"]), int(seed))
                rows.append({
                    "scenario": key[0],
                    "method": key[1],
                    "seed": key[2],
                    "valid_evidence_present": key in valid_keys,
                })
    return rows


def run_original_suite(expanded_path: Path, args: argparse.Namespace) -> int:
    runner = THIS_DIR / "hairgpu_runtime_suite.py"
    command = [sys.executable, str(runner), "--config", str(expanded_path), "--fail-policy", args.fail_policy]
    if args.viewer_exe:
        command += ["--viewer-exe", args.viewer_exe]
    if args.output:
        command += ["--output", args.output]
    if args.resume:
        command.append("--resume")
    if args.rerun_failed:
        command.append("--rerun-failed")
    if args.case_filter:
        command += ["--case-filter", args.case_filter]
    if args.variant_filter:
        command += ["--variant-filter", args.variant_filter]
    if args.scenario_filter:
        command += ["--scenario-filter", args.scenario_filter]
    return subprocess.run(command, check=False).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Strict evidence gate and matrix expander for HairSystem novelty01 runtime evaluation.")
    parser.add_argument("--config", "--suite", dest="config", required=True)
    parser.add_argument("--viewer-exe")
    parser.add_argument("--output")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--rerun-failed", action="store_true")
    parser.add_argument("--fail-policy", choices=["stop", "continue"], default="stop")
    parser.add_argument("--case-filter")
    parser.add_argument("--variant-filter")
    parser.add_argument("--scenario-filter")
    parser.add_argument("--prepare-only", action="store_true", help="Expand and validate the matrix without launching/importing runs.")
    parser.add_argument("--allow-unfrozen-asset", action="store_true", help="Development-only escape hatch; never use for paper evidence.")
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    config = read_json(config_path)
    expanded, runs = expand_matrix(config)
    absolutize_run_paths(config_path.parent, runs)
    expanded["runs"] = runs
    expanded_path = config_path.with_name(f"{config_path.stem}.expanded.json")
    write_json(expanded_path, expanded)

    static_issues = validate_static_contract(expanded, config_path, runs)
    if args.allow_unfrozen_asset:
        static_issues = [item for item in static_issues if item["code"] != "unfrozen_asset"]
    static_report_path = config_path.with_name(f"{config_path.stem}.static_preflight.json")
    write_json(static_report_path, {
        "sourceConfig": str(config_path),
        "sourceConfigSha256": sha256_file(config_path),
        "expandedConfig": str(expanded_path),
        "expandedConfigSha256": sha256_file(expanded_path),
        "runCount": len(runs),
        "issues": static_issues,
        "valid": not any(item["severity"] == "error" for item in static_issues),
    })

    static_invalid = any(item["severity"] == "error" for item in static_issues)
    if static_invalid and expanded.get("preflight", {}).get("strict", True):
        print(f"Static preflight failed. See {static_report_path}", file=sys.stderr)
        return 2
    if args.prepare_only:
        print(f"Expanded {len(runs)} runs: {expanded_path}")
        print(f"Static preflight: {static_report_path}")
        return 0

    runner_code = run_original_suite(expanded_path, args)
    evidences: list[dict[str, Any]] = []
    validation_rows: list[dict[str, Any]] = []
    for run in runs:
        evidence = load_run_evidence(config_path, expanded, run)
        issues = validate_run_evidence(expanded, run, evidence)
        evidence["issues"] = issues
        evidence["valid"] = not any(item["severity"] == "error" for item in issues)
        evidence.pop("frame_ms", None)
        evidence.pop("combined", None)
        evidence.pop("runtime_combined", None)
        evidence.pop("case_snapshot", None)
        evidence.pop("frame_headers", None)
        evidences.append(evidence)
        if issues:
            validation_rows.extend(issues)
        else:
            validation_rows.append({"severity": "info", "code": "valid", "case_id": run.get("case_id", ""), "message": "Evidence passed the strict gate."})

    pair_rows, pair_aggregates = build_matched_pairs(expanded, evidences)
    coverage_rows = validate_coverage(expanded, evidences)
    valid_count = sum(bool(item.get("valid")) for item in evidences)
    output_dir = resolve_path(config_path.parent, expanded.get("outputs_dir")) or config_path.parent / "runtime_suite_outputs"
    output_dir.mkdir(parents=True, exist_ok=True)
    report_path = output_dir / "safe_evidence_report.json"
    validation_csv = output_dir / "safe_evidence_validation.csv"
    pair_csv = output_dir / "safe_matched_pairwise.csv"
    pair_summary_csv = output_dir / "safe_matched_pairwise_summary.csv"
    coverage_csv = output_dir / "safe_matrix_coverage.csv"
    report = {
        "suiteName": expanded.get("suite_name", config_path.stem),
        "sourceBranch": "Novelty",
        "sourceCommit": "ed2d14473a5ba6b7a8b44a8d8ba4d004899c2b6e",
        "expectedRunCount": len(runs),
        "validEvidenceCount": valid_count,
        "invalidEvidenceCount": len(runs) - valid_count,
        "coverageRate": valid_count / len(runs) if runs else 0.0,
        "runnerExitCode": runner_code,
        "strictEvidencePassed": runner_code == 0 and valid_count == len(runs) and all(row.get("minimum_seed_count_met", False) for row in pair_aggregates),
        "runs": evidences,
        "pairwiseSummary": pair_aggregates,
    }
    write_json(report_path, report)
    write_csv(validation_csv, validation_rows)
    write_csv(pair_csv, pair_rows)
    write_csv(pair_summary_csv, pair_aggregates)
    write_csv(coverage_csv, coverage_rows)

    print(f"Expanded runs: {len(runs)}")
    print(f"Valid evidence: {valid_count}/{len(runs)}")
    print(f"Safe evidence report: {report_path}")
    if expanded.get("preflight", {}).get("strict", True) and not report["strictEvidencePassed"]:
        return 3
    return runner_code


if __name__ == "__main__":
    raise SystemExit(main())
