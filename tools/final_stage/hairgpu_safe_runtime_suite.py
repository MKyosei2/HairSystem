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
PLACEHOLDER_PATTERN = re.compile(
    r"(?:REPLACE|PENDING|NOT_EXECUTED|SYNTHETIC|UNKNOWN|TBD|UNFROZEN)",
    re.IGNORECASE,
)
SHA256_PATTERN = re.compile(r"^[0-9a-fA-F]{64}$")
COMMIT_SHA_PATTERN = re.compile(r"^[0-9a-fA-F]{40}$")
FRAME_TIME_KEYS = (
    "frameMilliseconds",
    "frame_ms",
    "frameMs",
    "totalFrameMilliseconds",
    "frame_time_ms",
)
TRUE_VALUES = {True, 1, "1", "true", "True", "yes", "YES", "on", "ON"}
FALSE_VALUES = {False, 0, "0", "false", "False", "no", "NO", "off", "OFF"}


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


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
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return sha256_bytes(encoded)


def is_sha256(value: Any) -> bool:
    return bool(SHA256_PATTERN.fullmatch(str(value or "")))


def is_commit_sha(value: Any) -> bool:
    return bool(COMMIT_SHA_PATTERN.fullmatch(str(value or "")))


def is_placeholder(value: Any) -> bool:
    if value is None:
        return True
    text = str(value).strip()
    return not text or bool(PLACEHOLDER_PATTERN.search(text))


def normalize_key(value: str) -> str:
    return re.sub(r"[^a-z0-9]", "", value.lower())


def deep_merge(base: dict[str, Any], overlay: dict[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(base)
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = deep_merge(result[key], value)
        else:
            result[key] = copy.deepcopy(value)
    return result


def resolve_path(base_dir: Path, value: str | None) -> Path | None:
    if not value:
        return None
    path = Path(value)
    return path if path.is_absolute() else (base_dir / path).resolve()


def safe_slug(value: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    return slug.strip("_") or "case"


def flatten_mapping(mapping: Any) -> dict[str, list[Any]]:
    flattened: dict[str, list[Any]] = {}

    def visit(value: Any, key: str = "") -> None:
        if isinstance(value, dict):
            for child_key, child_value in value.items():
                visit(child_value, str(child_key))
            return
        if isinstance(value, list):
            for child_value in value:
                if isinstance(child_value, dict):
                    visit(child_value, key)
            return
        token = normalize_key(key)
        if token:
            flattened.setdefault(token, []).append(value)

    visit(mapping)
    return flattened


def get_any(mapping: dict[str, Any], *keys: str, default: Any = None) -> Any:
    flattened = flatten_mapping(mapping)
    for key in keys:
        values = flattened.get(normalize_key(key), [])
        for value in values:
            if value is not None:
                return value
    return default


def values_equivalent(expected: Any, observed: Any) -> bool:
    if isinstance(expected, bool):
        return (expected and observed in TRUE_VALUES) or ((not expected) and observed in FALSE_VALUES)
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        try:
            return math.isclose(float(expected), float(observed), rel_tol=1.0e-6, abs_tol=1.0e-9)
        except (TypeError, ValueError):
            return False
    return str(expected).strip() == str(observed).strip()


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * q
    low = int(math.floor(position))
    high = int(math.ceil(position))
    if low == high:
        return ordered[low]
    weight = position - low
    return ordered[low] * (1.0 - weight) + ordered[high] * weight


def bootstrap_ci(
    values: list[float],
    confidence: float,
    iterations: int,
    seed: int = 20260715,
) -> tuple[float, float]:
    if not values:
        return 0.0, 0.0
    if len(values) == 1:
        return values[0], values[0]
    rng = random.Random(seed)
    means: list[float] = []
    for _ in range(max(iterations, 100)):
        sample = [values[rng.randrange(len(values))] for _ in values]
        means.append(statistics.fmean(sample))
    alpha = (1.0 - confidence) / 2.0
    return percentile(means, alpha), percentile(means, 1.0 - alpha)


def run_git(repo_root: Path, *arguments: str) -> tuple[int, str]:
    try:
        completed = subprocess.run(
            ["git", *arguments],
            cwd=repo_root,
            check=False,
            capture_output=True,
            text=True,
            timeout=20,
        )
    except (FileNotFoundError, subprocess.SubprocessError):
        return 127, ""
    return completed.returncode, completed.stdout.strip()


def find_repo_root(config_path: Path) -> Path | None:
    for candidate in [config_path.parent, *config_path.parents]:
        if (candidate / ".git").exists():
            return candidate
    code, output = run_git(config_path.parent, "rev-parse", "--show-toplevel")
    return Path(output).resolve() if code == 0 and output else None


def resolve_source_identity(
    config: dict[str, Any],
    config_path: Path,
) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    resolved = copy.deepcopy(config)
    source = resolved.setdefault("source", {})
    issues: list[dict[str, Any]] = []

    def issue(severity: str, code: str, message: str) -> None:
        issues.append({"severity": severity, "code": code, "case_id": "", "message": message})

    commit_mode = str(source.get("commit_mode", "declared")).strip()
    declared_commit = str(source.get("commit", "")).strip()
    require_clean = bool(resolved.get("preflight", {}).get("require_clean_git_worktree", True))
    if commit_mode == "git_head_clean" or declared_commit.upper() == "AUTO":
        repo_root = find_repo_root(config_path)
        if repo_root is None:
            issue("error", "git_repo_missing", "Cannot resolve the repository root for source.commit=AUTO.")
        else:
            code, head = run_git(repo_root, "rev-parse", "HEAD")
            if code != 0 or not is_commit_sha(head):
                issue("error", "git_head_unavailable", "Cannot resolve a valid 40-character Git HEAD.")
            else:
                source["commit"] = head.lower()
                source["resolved_repository_root"] = str(repo_root)
            if require_clean:
                status_code, status_text = run_git(repo_root, "status", "--porcelain", "--untracked-files=normal")
                if status_code != 0:
                    issue("error", "git_status_unavailable", "Cannot verify whether the Git worktree is clean.")
                elif status_text:
                    issue(
                        "error",
                        "dirty_git_worktree",
                        "The Git worktree contains tracked or untracked changes. Commit the patch before evidence capture.",
                    )
    elif not is_commit_sha(declared_commit):
        issue("error", "invalid_source_commit", "source.commit must be AUTO or an exact 40-character commit SHA.")

    return resolved, issues


def stable_run_signature(run: dict[str, Any], suite_version: str, source_commit: str) -> dict[str, Any]:
    excluded = {
        "settings_hash",
        "method_settings_sha256",
        "execution_index",
        "summary_json",
        "frame_csv",
        "frame_jsonl",
        "settings_json",
        "telemetry_dir",
        "viewer_exe",
    }
    stable = {key: copy.deepcopy(value) for key, value in run.items() if key not in excluded}
    return {"suite_version": suite_version, "source_commit": source_commit, "run": stable}


def absolutize_and_hash_run_paths(config_dir: Path, config: dict[str, Any], runs: list[dict[str, Any]]) -> None:
    path_keys = (
        "viewer_exe",
        "scenario_timeline",
        "asset_manifest",
        "telemetry_dir",
        "summary_json",
        "frame_csv",
        "frame_jsonl",
        "settings_json",
    )
    source_commit = str(config.get("source", {}).get("commit", ""))
    suite_version = str(config.get("suite_version", ""))
    for run in runs:
        for key in path_keys:
            value = run.get(key)
            if value in (None, ""):
                continue
            path = Path(str(value))
            if not path.is_absolute():
                run[key] = str((config_dir / path).resolve())
        timeline = Path(str(run.get("scenario_timeline", "")))
        manifest = Path(str(run.get("asset_manifest", "")))
        run["scenario_timeline_sha256"] = sha256_file(timeline) if timeline.exists() else ""
        run["asset_manifest_sha256"] = sha256_file(manifest) if manifest.exists() else ""
        run["method_settings_sha256"] = canonical_json_sha256(run.get("method_settings", {}))
        run["settings_hash"] = canonical_json_sha256(stable_run_signature(run, suite_version, source_commit))


def expand_matrix(config: dict[str, Any], config_path: Path | None = None) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    expanded = copy.deepcopy(config)
    defaults = copy.deepcopy(config.get("run_defaults", {}))
    methods = config.get("methods", [])
    scenarios = config.get("scenarios", [])
    seeds = config.get("seeds", [])
    method_by_code = {str(item.get("code", "")): item for item in methods}
    scenario_by_code = {str(item.get("code", "")): item for item in scenarios}
    runs: list[dict[str, Any]] = []

    if config.get("runs"):
        for index, source in enumerate(config["runs"]):
            run = deep_merge(defaults, source)
            run.setdefault("case_id", f"case_{index + 1:05d}")
            run.setdefault("run_name", safe_slug(str(run["case_id"])))
            run.setdefault("run_role", "primary")
            run.setdefault("experiment", config.get("experiment_name", "runtime_suite"))
            run.setdefault("method_settings", {})
            runs.append(run)
    else:
        if not methods or not scenarios or not seeds:
            raise ValueError("Matrix expansion requires non-empty methods, scenarios, and seeds when runs is empty.")
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
                    run.update(
                        {
                            "experiment": config.get("experiment_name", "runtime_suite"),
                            "run_role": "primary",
                            "method": method_code,
                            "variant": method_code,
                            "method_label": method.get("label", method_code),
                            "method_settings": copy.deepcopy(method.get("settings", {})),
                            "scenario": scenario_code,
                            "scenario_category": scenario.get("category", ""),
                            "scenario_timeline": scenario.get("timeline", ""),
                            "seed": int(seed),
                            "repeat_index": 0,
                        }
                    )
                    case_id = (
                        f"case_{case_index:05d}_{safe_slug(scenario_code)}_"
                        f"{safe_slug(method_code)}_seed{int(seed)}"
                    )
                    run["case_id"] = case_id
                    run["run_name"] = case_id
                    runs.append(run)

        for replay_index, replay in enumerate(config.get("determinism_replays", []), start=1):
            scenario_code = str(replay.get("scenario", ""))
            method_code = str(replay.get("method", ""))
            seed = int(replay.get("seed", 0))
            repetitions = int(replay.get("repetitions", 2))
            scenario = scenario_by_code.get(scenario_code)
            method = method_by_code.get(method_code)
            if not scenario or not method or repetitions < 2:
                continue
            replay_group_id = str(replay.get("id", f"replay_{replay_index:03d}"))
            for repeat_index in range(1, repetitions + 1):
                case_index += 1
                run = deep_merge(defaults, method.get("settings", {}))
                run.update(
                    {
                        "experiment": config.get("experiment_name", "runtime_suite"),
                        "run_role": "determinism_replay",
                        "replay_group_id": replay_group_id,
                        "repeat_index": repeat_index,
                        "method": method_code,
                        "variant": method_code,
                        "method_label": method.get("label", method_code),
                        "method_settings": copy.deepcopy(method.get("settings", {})),
                        "scenario": scenario_code,
                        "scenario_category": scenario.get("category", ""),
                        "scenario_timeline": scenario.get("timeline", ""),
                        "seed": seed,
                    }
                )
                case_id = (
                    f"det_{safe_slug(replay_group_id)}_{safe_slug(scenario_code)}_"
                    f"{safe_slug(method_code)}_seed{seed}_r{repeat_index}"
                )
                run["case_id"] = case_id
                run["run_name"] = case_id
                runs.append(run)

    if config_path is not None:
        absolutize_and_hash_run_paths(config_path.parent, expanded, runs)

    statistics_config = config.get("statistics", {})
    if statistics_config.get("randomize_execution_order", True):
        order_seed = int(statistics_config.get("execution_order_seed", 20260715))
        random.Random(order_seed).shuffle(runs)
    for execution_index, run in enumerate(runs):
        run["execution_index"] = execution_index

    expanded["runs"] = runs
    if not expanded.get("scenario_matrix") and methods and scenarios:
        required_methods = [str(item["code"]) for item in methods]
        expanded["scenario_matrix"] = [
            {
                "scenario": str(item["code"]),
                "category": item.get("category", ""),
                "required_methods": required_methods,
                "notes": "Automatically expanded strict matrix.",
            }
            for item in scenarios
        ]
    return expanded, runs


def _issue(
    issues: list[dict[str, Any]],
    severity: str,
    code: str,
    message: str,
    case_id: str = "",
) -> None:
    issues.append({"severity": severity, "code": code, "case_id": case_id, "message": message})


def validate_timeline(
    timeline_path: Path,
    scenario_code: str,
    preflight: dict[str, Any],
    run_defaults: dict[str, Any],
    issues: list[dict[str, Any]],
) -> None:
    if not timeline_path.exists():
        _issue(issues, "error", "missing_timeline", f"Scenario timeline is missing: {timeline_path}")
        return
    try:
        timeline = read_json(timeline_path)
    except Exception as exc:
        _issue(issues, "error", "invalid_timeline", f"Cannot parse {timeline_path}: {exc}")
        return
    if str(timeline.get("scenario", "")) != scenario_code:
        _issue(
            issues,
            "error",
            "timeline_identity_mismatch",
            f"Timeline scenario must be {scenario_code}: {timeline_path}",
        )
    minimum_duration = int(run_defaults.get("warmup_frames", 0)) + int(run_defaults.get("measurement_frames", 0))
    if int(timeline.get("durationFrames", 0)) < minimum_duration:
        _issue(
            issues,
            "error",
            "short_timeline",
            f"Timeline {scenario_code} has fewer than {minimum_duration} frames.",
        )
    expected_step = float(preflight.get("fixed_timestep_seconds", 1.0 / 60.0))
    tolerance = float(preflight.get("fixed_timestep_tolerance_seconds", 1.0e-5))
    actual_step = float(timeline.get("fixedTimeStepSeconds", 0.0) or 0.0)
    if not math.isfinite(actual_step) or abs(actual_step - expected_step) > tolerance:
        _issue(issues, "error", "timeline_timestep_mismatch", f"Timeline {scenario_code} uses {actual_step} seconds.")
    if preflight.get("require_seed_driven_timeline", True) and timeline.get("seedDriven") is not True:
        _issue(issues, "error", "timeline_not_seed_driven", f"Timeline {scenario_code} must declare seedDriven=true.")


def validate_asset_manifest(
    manifest_path: Path,
    config_dir: Path,
    preflight: dict[str, Any],
    issues: list[dict[str, Any]],
) -> dict[str, Any]:
    if not manifest_path.exists():
        _issue(issues, "error", "missing_asset_manifest", f"Asset manifest is missing: {manifest_path}")
        return {}
    try:
        manifest = read_json(manifest_path)
    except Exception as exc:
        _issue(issues, "error", "invalid_asset_manifest", f"Failed to read asset manifest: {exc}")
        return {}

    if preflight.get("require_frozen_asset", True):
        if str(manifest.get("freezeStatus", "")) != "FROZEN":
            _issue(issues, "error", "unfrozen_asset", "The 100k benchmark asset is not frozen.")
        for key in ("sourceAssetSha256", "generatedTopologySha256", "generatorSettingsSha256"):
            if not is_sha256(manifest.get(key)):
                _issue(issues, "error", "invalid_asset_hash", f"Asset manifest field {key} must be an exact SHA-256.")

    generation = manifest.get("generation", {})
    if int(generation.get("displayStrandCount", 0)) != 100000:
        _issue(issues, "error", "wrong_strand_count", "Benchmark asset must contain exactly 100,000 displayed strands.")
    if not generation.get("requireStableTopologyHash", False):
        _issue(issues, "error", "unstable_topology_contract", "The benchmark must require a stable topology hash.")

    if preflight.get("verify_source_asset_file", True) and str(manifest.get("freezeStatus", "")) == "FROZEN":
        asset_path = resolve_path(manifest_path.parent, str(manifest.get("sourceAssetPath", "")))
        if asset_path is None or not asset_path.exists():
            _issue(issues, "error", "frozen_asset_missing", "Frozen source asset path does not exist.")
        elif sha256_file(asset_path) != str(manifest.get("sourceAssetSha256", "")).lower():
            _issue(issues, "error", "frozen_asset_hash_mismatch", "Frozen source asset bytes do not match sourceAssetSha256.")
    return manifest


def validate_static_contract(
    config: dict[str, Any],
    config_path: Path,
    runs: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    config_dir = config_path.parent
    preflight = config.get("preflight", {})
    methods = config.get("methods", [])
    scenarios = config.get("scenarios", [])
    seeds = config.get("seeds", [])
    defaults = config.get("run_defaults", {})

    source = config.get("source", {})
    if not is_commit_sha(source.get("commit")):
        _issue(issues, "error", "invalid_source_commit", "The resolved source commit must be an exact 40-character SHA.")

    method_codes = [str(item.get("code", "")) for item in methods]
    if not method_codes or len(method_codes) != len(set(method_codes)):
        _issue(issues, "error", "duplicate_method", "Method codes must be non-empty and unique.")
    scenario_codes = [str(item.get("code", "")) for item in scenarios]
    if not scenario_codes or len(scenario_codes) != len(set(scenario_codes)):
        _issue(issues, "error", "duplicate_scenario", "Scenario codes must be non-empty and unique.")

    baseline = str(config.get("baseline_method", ""))
    if not baseline or baseline not in method_codes:
        _issue(issues, "error", "missing_baseline", f"Baseline method '{baseline}' is not present in methods.")
    for required_method in preflight.get("required_method_codes", []):
        if str(required_method) not in method_codes:
            _issue(issues, "error", "missing_required_method", f"Required comparison method is missing: {required_method}")

    minimum_seed_count = int(preflight.get("minimum_seed_count", 1))
    if len(set(int(seed) for seed in seeds)) < minimum_seed_count:
        _issue(issues, "error", "insufficient_seeds", f"At least {minimum_seed_count} independent seeds are required.")

    for scenario in scenarios:
        scenario_code = str(scenario.get("code", ""))
        timeline_path = resolve_path(config_dir, scenario.get("timeline"))
        if timeline_path is None:
            _issue(issues, "error", "missing_timeline", f"Scenario timeline is missing for {scenario_code}.")
        else:
            validate_timeline(timeline_path, scenario_code, preflight, defaults, issues)

    manifest_path = resolve_path(config_dir, defaults.get("asset_manifest"))
    if manifest_path is None:
        _issue(issues, "error", "missing_asset_manifest", "run_defaults.asset_manifest is required.")
    else:
        validate_asset_manifest(manifest_path, config_dir, preflight, issues)

    hardware_profiles_path = resolve_path(config_dir, config.get("hardware_profiles"))
    hardware_profiles: dict[str, Any] = {}
    if hardware_profiles_path is None or not hardware_profiles_path.exists():
        _issue(issues, "error", "missing_hardware_profiles", "hardware_profiles JSON is required.")
    else:
        try:
            hardware_profiles = read_json(hardware_profiles_path).get("profiles", {})
        except Exception as exc:
            _issue(issues, "error", "invalid_hardware_profiles", f"Cannot parse hardware profiles: {exc}")

    primary_runs = [run for run in runs if run.get("run_role", "primary") == "primary"]
    expected_primary_count = len(methods) * len(scenarios) * len(seeds)
    if len(primary_runs) != expected_primary_count:
        _issue(
            issues,
            "error",
            "matrix_size_mismatch",
            f"Primary run count is {len(primary_runs)} but expected {expected_primary_count}.",
        )

    seen_case_ids: set[str] = set()
    for run in runs:
        case_id = str(run.get("case_id", ""))
        if not case_id or case_id in seen_case_ids:
            _issue(issues, "error", "duplicate_case_id", "Every run requires a unique case_id.", case_id)
        seen_case_ids.add(case_id)
        if preflight.get("require_release_build", True) and str(run.get("build_configuration", "")) != "Release":
            _issue(issues, "error", "non_release_run", "Run is not configured for Release.", case_id)
        required_platform = str(preflight.get("required_platform", "x64"))
        if str(run.get("platform", "")) != required_platform:
            _issue(issues, "error", "wrong_platform", f"Run platform must be {required_platform}.", case_id)
        if bool(run.get("vsync", True)) and preflight.get("require_vsync_disabled", True):
            _issue(issues, "error", "run_vsync_enabled", "Run configuration enables VSync.", case_id)
        if preflight.get("require_fixed_timestep", True) and not bool(run.get("fixed_timestep", False)):
            _issue(issues, "error", "run_fixed_timestep_disabled", "Run configuration disables fixed timestep.", case_id)
        expected_step = float(preflight.get("fixed_timestep_seconds", 1.0 / 60.0))
        tolerance = float(preflight.get("fixed_timestep_tolerance_seconds", 1.0e-5))
        run_step = float(run.get("fixed_timestep_seconds", 0.0) or 0.0)
        if abs(run_step - expected_step) > tolerance:
            _issue(issues, "error", "run_timestep_mismatch", "Run fixed timestep differs from preflight.", case_id)
        if int(run.get("warmup_frames", 0)) < int(preflight.get("minimum_warmup_frames", 0)):
            _issue(issues, "error", "short_warmup", "Warm-up frame count is below the contract.", case_id)
        if int(run.get("measurement_frames", 0)) < int(preflight.get("minimum_measurement_frames", 0)):
            _issue(issues, "error", "short_measurement", "Measurement frame count is below the contract.", case_id)
        if int(run.get("display_strand_count", 0)) != 100000:
            _issue(issues, "error", "run_strand_count", "Run must request exactly 100,000 displayed strands.", case_id)
        if not is_sha256(run.get("scenario_timeline_sha256")):
            _issue(issues, "error", "timeline_hash_missing", "Run scenario timeline hash is missing.", case_id)
        if not is_sha256(run.get("asset_manifest_sha256")):
            _issue(issues, "error", "manifest_hash_missing", "Run asset manifest hash is missing.", case_id)
        if not is_sha256(run.get("method_settings_sha256")) or not is_sha256(run.get("settings_hash")):
            _issue(issues, "error", "run_settings_hash_missing", "Run settings hashes are missing.", case_id)
        profile = str(run.get("hardware_profile", ""))
        if hardware_profiles and profile not in hardware_profiles:
            _issue(issues, "error", "unknown_hardware_profile", f"Unknown hardware profile: {profile}", case_id)

    for replay in config.get("determinism_replays", []):
        if int(replay.get("repetitions", 0)) < 2:
            _issue(issues, "error", "short_determinism_replay", "Determinism replay requires at least two repetitions.")
        if str(replay.get("method", "")) not in method_codes or str(replay.get("scenario", "")) not in scenario_codes:
            _issue(issues, "error", "invalid_determinism_replay", "Determinism replay refers to an unknown method or scenario.")

    return issues


def read_frame_csv(path: Path) -> tuple[list[dict[str, str]], list[float], set[str], int]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
        headers = set(reader.fieldnames or [])
    frame_key = next((key for key in FRAME_TIME_KEYS if key in headers), None)
    values: list[float] = []
    invalid_count = 0
    for row in rows:
        if frame_key is None:
            invalid_count += 1
            continue
        raw = row.get(frame_key, "")
        try:
            value = float(raw)
        except (TypeError, ValueError):
            invalid_count += 1
            continue
        if math.isfinite(value) and value > 0.0:
            values.append(value)
        else:
            invalid_count += 1
    return rows, values, headers, invalid_count


def extract_runtime_metrics(combined: dict[str, Any]) -> dict[str, float]:
    attempts = float(get_any(combined, "repairAttemptCount", default=0.0) or 0.0)
    successes = float(get_any(combined, "repairSuccessCount", default=0.0) or 0.0)
    static_count = float(get_any(combined, "staticCachedGroupCount", default=0.0) or 0.0) + float(
        get_any(combined, "staticTransformedGroupCount", default=0.0) or 0.0
    )
    false_static = float(get_any(combined, "falseStaticCount", default=0.0) or 0.0)
    candidate_count = float(get_any(combined, "collisionCandidateCount", default=0.0) or 0.0)
    miss_count = float(get_any(combined, "collisionMissCount", default=0.0) or 0.0)
    transition_count = float(get_any(combined, "stateTransitionCount", default=0.0) or 0.0)
    oscillation_count = float(get_any(combined, "stateOscillationCount", default=0.0) or 0.0)
    return {
        "false_static_rate": false_static / max(static_count, 1.0),
        "repair_success_rate": successes / max(attempts, 1.0),
        "collision_miss_rate": miss_count / max(candidate_count, 1.0),
        "state_oscillation_rate": oscillation_count / max(transition_count, 1.0),
        "static_dynamic_boundary_error_mm": float(
            get_any(combined, "staticDynamicBoundaryMaxErrorMm", "staticDynamicBoundaryErrorMm", default=0.0) or 0.0
        ),
        "max_penetration_mm": float(get_any(combined, "maxPenetrationMm", default=0.0) or 0.0),
    }


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
    invalid_frame_count = 0
    if frame_path.exists():
        rows, frame_ms, headers, invalid_frame_count = read_frame_csv(frame_path)

    runtime_combined = deep_merge(settings, summary)
    combined = deep_merge(case_snapshot, runtime_combined)
    evidence: dict[str, Any] = {
        "case_id": run.get("case_id", ""),
        "run_name": run_name,
        "run_role": run.get("run_role", "primary"),
        "replay_group_id": run.get("replay_group_id", ""),
        "repeat_index": int(run.get("repeat_index", 0)),
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
        "invalid_frame_count": invalid_frame_count,
        "frame_headers": sorted(headers),
        "frame_ms": frame_ms,
        "combined": combined,
        "runtime_combined": runtime_combined,
        "case_snapshot": case_snapshot,
        "canonical_result_sha256": str(
            get_any(combined, "canonicalResultSha256", "determinismResultSha256", default="")
        ),
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
            "invalid_frame_sample_rate": invalid_frame_count / max(len(rows), 1),
        }
    else:
        evidence["derived"] = {}
    evidence["metrics"] = extract_runtime_metrics(combined)
    return evidence


def validate_run_evidence(
    config: dict[str, Any],
    run: dict[str, Any],
    evidence: dict[str, Any],
    asset_manifest: dict[str, Any] | None = None,
) -> list[dict[str, Any]]:
    preflight = config.get("preflight", {})
    issues: list[dict[str, Any]] = []
    case_id = str(run.get("case_id", ""))
    combined = evidence.get("combined", {})
    runtime_combined = evidence.get("runtime_combined", {})
    runtime_flat = flatten_mapping(runtime_combined)

    def issue(severity: str, code: str, message: str) -> None:
        _issue(issues, severity, code, message, case_id)

    if not evidence.get("summary_exists"):
        issue("error", "missing_summary", "Summary JSON is missing.")
    if not evidence.get("frame_exists"):
        issue("error", "missing_frames", "Frame CSV is missing.")
    if not evidence.get("settings_exists"):
        issue("error", "missing_settings", "Settings JSON is missing.")

    status_text = " ".join(
        str(get_any(combined, key, default="")) for key in ("status", "measurementStatus", "realFpsAvailable")
    )
    if preflight.get("reject_pending_or_synthetic_results", True) and PLACEHOLDER_PATTERN.search(status_text):
        issue("error", "non_real_status", f"Pending/synthetic status is not admissible: {status_text.strip()}")

    real_measurement = get_any(runtime_combined, "realMeasurement", "real_measurement", "isRealMeasurement", default=None)
    if preflight.get("require_real_measurement", True) and real_measurement not in TRUE_VALUES:
        issue("error", "not_real_measurement", "Telemetry does not explicitly identify itself as a real measurement.")

    build_configuration = str(
        get_any(runtime_combined, "buildConfiguration", "build_configuration", "observedBuildConfiguration", default="")
    )
    if preflight.get("require_release_build", True) and build_configuration != "Release":
        issue("error", "non_release_evidence", f"Evidence build configuration is '{build_configuration}', not Release.")

    platform = str(get_any(runtime_combined, "platform", "observedPlatform", default=""))
    if platform != str(preflight.get("required_platform", "x64")):
        issue("error", "wrong_evidence_platform", f"Evidence platform is '{platform}'.")

    vsync = get_any(runtime_combined, "vsyncEnabled", "vsync", default=None)
    if preflight.get("require_vsync_disabled", True) and vsync not in FALSE_VALUES:
        issue("error", "vsync_enabled", "VSync must be disabled.")

    fixed_enabled = get_any(runtime_combined, "fixedTimeStepEnabled", "fixed_timestep", default=None)
    if preflight.get("require_fixed_timestep", True) and fixed_enabled not in TRUE_VALUES:
        issue("error", "fixed_timestep_disabled", "Runtime telemetry does not confirm fixed timestep.")
    observed_step = float(get_any(runtime_combined, "fixedTimeStepSeconds", "fixed_timestep_seconds", default=0.0) or 0.0)
    expected_step = float(preflight.get("fixed_timestep_seconds", 1.0 / 60.0))
    tolerance = float(preflight.get("fixed_timestep_tolerance_seconds", 1.0e-5))
    if not math.isfinite(observed_step) or abs(observed_step - expected_step) > tolerance:
        issue("error", "fixed_timestep_mismatch", f"Observed fixed timestep is {observed_step} seconds.")

    width = int(get_any(runtime_combined, "width", "renderWidth", default=0) or 0)
    height = int(get_any(runtime_combined, "height", "renderHeight", default=0) or 0)
    required_resolution = preflight.get("required_resolution", [1920, 1080])
    if [width, height] != [int(required_resolution[0]), int(required_resolution[1])]:
        issue("error", "wrong_resolution", f"Evidence resolution is {width}x{height}.")

    observed_strands = int(get_any(runtime_combined, "displayStrandCount", "displayedStrandCount", default=0) or 0)
    if observed_strands != int(run.get("display_strand_count", 100000)):
        issue("error", "display_strand_mismatch", f"Telemetry reports {observed_strands} displayed strands.")

    expected_frames = int(run.get("measurement_frames", preflight.get("minimum_measurement_frames", 0)))
    if int(evidence.get("frame_row_count", 0)) < expected_frames:
        issue(
            "error",
            "insufficient_frame_rows",
            f"Frame CSV has {evidence.get('frame_row_count', 0)} rows; expected at least {expected_frames}.",
        )
    if not evidence.get("frame_ms"):
        issue("error", "missing_frame_time", "No valid positive finite frame-time column was found.")
    maximum_invalid_fraction = float(preflight.get("max_invalid_frame_fraction", 0.0))
    invalid_fraction = float(evidence.get("derived", {}).get("invalid_frame_sample_rate", 1.0))
    if invalid_fraction > maximum_invalid_fraction:
        issue("error", "invalid_frame_samples", f"Invalid frame sample rate is {invalid_fraction:.6f}.")

    expected_commit = str(config.get("source", {}).get("commit", ""))
    observed_commit = str(get_any(combined, "commitSha", "commit_sha", default=""))
    if not is_commit_sha(observed_commit):
        issue("error", "invalid_commit_sha", "Telemetry commitSha is missing or malformed.")
    elif preflight.get("require_source_commit_match", True) and observed_commit.lower() != expected_commit.lower():
        issue("error", "source_commit_mismatch", f"Telemetry commit {observed_commit} does not match {expected_commit}.")

    for key in preflight.get("require_hashes", []):
        value = get_any(combined, str(key), default=run.get(str(key), ""))
        if str(key) == "commitSha":
            if not is_commit_sha(value):
                issue("error", "invalid_hash", f"Required commit hash is malformed: {key}")
        elif not is_sha256(value):
            issue("error", "invalid_hash", f"Required SHA-256 is missing or malformed: {key}")

    expected_hashes = {
        "scenarioTimelineSha256": str(run.get("scenario_timeline_sha256", "")),
        "assetManifestSha256": str(run.get("asset_manifest_sha256", "")),
        "appliedMethodSettingsSha256": str(run.get("method_settings_sha256", "")),
        "settingsSha256": str(run.get("settings_hash", "")),
    }
    for key, expected in expected_hashes.items():
        observed = str(get_any(runtime_combined, key, default=""))
        if not is_sha256(observed) or observed.lower() != expected.lower():
            issue("error", "identity_hash_mismatch", f"{key} does not match the requested run signature.")

    if asset_manifest:
        expected_asset_hash = str(asset_manifest.get("sourceAssetSha256", ""))
        observed_asset_hash = str(get_any(combined, "assetSha256", default=""))
        if not is_sha256(observed_asset_hash) or observed_asset_hash.lower() != expected_asset_hash.lower():
            issue("error", "asset_hash_mismatch", "Telemetry assetSha256 does not match the frozen manifest.")
        observed_frozen = get_any(combined, "assetFrozen", "freezeStatus", default=None)
        if str(observed_frozen) not in {"FROZEN", "True", "true", "1"} and observed_frozen is not True:
            issue("error", "asset_not_acknowledged_frozen", "Telemetry does not acknowledge the frozen asset.")

    for key in preflight.get("require_hardware_fields", []):
        value = get_any(combined, str(key), default=run.get(str(key), ""))
        if is_placeholder(value):
            issue("error", "missing_hardware", f"Required hardware identity is missing: {key}")
    observed_profile = str(get_any(combined, "hardwareProfile", default=""))
    if observed_profile != str(run.get("hardware_profile", "")):
        issue("error", "hardware_profile_mismatch", "Telemetry hardware profile does not match the requested case.")

    combined_flat = flatten_mapping(combined)
    for key in preflight.get("required_telemetry_fields", []):
        token = normalize_key(str(key))
        normalized_headers = {normalize_key(item) for item in evidence.get("frame_headers", [])}
        if token not in normalized_headers and token not in combined_flat:
            issue("error", "missing_telemetry_field", f"Required telemetry field is missing: {key}")

    if preflight.get("require_identity_match", True):
        identity_checks = {
            "case_id": (("caseId", "case_id"), str(run.get("case_id", ""))),
            "method": (("appliedMethodCode", "method", "variant"), str(run.get("method", ""))),
            "scenario": (("scenario",), str(run.get("scenario", ""))),
            "seed": (("seed",), int(run.get("seed", 0))),
        }
        for label, (keys, expected) in identity_checks.items():
            observed = get_any(runtime_combined, *keys, default=None)
            if observed is None or not values_equivalent(expected, observed):
                issue(
                    "error",
                    "identity_mismatch",
                    f"Runtime {label} does not match the requested case: expected={expected!r}, observed={observed!r}",
                )

    if preflight.get("require_method_setting_acknowledgement", True):
        for key, expected in dict(run.get("method_settings", {})).items():
            observed_values = runtime_flat.get(normalize_key(str(key)), [])
            if not observed_values:
                issue("error", "unacknowledged_method_setting", f"Runtime telemetry does not acknowledge: {key}")
            elif not any(values_equivalent(expected, observed) for observed in observed_values):
                issue(
                    "error",
                    "method_setting_mismatch",
                    f"Runtime method setting differs for {key}: expected={expected!r}, observed={observed_values!r}",
                )

    for name, value in evidence.get("derived", {}).items():
        if not math.isfinite(float(value)):
            issue("error", "non_finite_metric", f"Derived metric is not finite: {name}")
    tails = evidence.get("derived", {})
    if tails and not (
        float(tails.get("p95_frame_ms", 0.0)) <= float(tails.get("p99_frame_ms", 0.0))
        <= float(tails.get("p999_frame_ms", 0.0))
    ):
        issue("error", "unordered_tail_percentiles", "p95, p99, and p99.9 are not monotonically ordered.")

    fatal_count = int(get_any(combined, "fatalRuntimeFailureCount", "forcedStopCount", default=0) or 0)
    if preflight.get("require_no_fatal_runtime_failure", True) and fatal_count > 0:
        issue("error", "fatal_runtime_failure", f"Runtime recorded {fatal_count} fatal failures.")
    return issues


def build_matched_pairs(
    config: dict[str, Any],
    evidences: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]:
    baseline_method = str(config.get("baseline_method", ""))
    statistics_config = config.get("statistics", {})
    confidence = float(statistics_config.get("confidence_level", 0.95))
    iterations = int(statistics_config.get("bootstrap_iterations", 2000))
    min_seeds = int(
        statistics_config.get(
            "minimum_matched_seed_count",
            config.get("preflight", {}).get("minimum_seed_count", 1),
        )
    )
    valid = [
        item
        for item in evidences
        if item.get("valid") and item.get("derived") and item.get("run_role", "primary") == "primary"
    ]
    lookup = {
        (item["scenario"], item["seed"], item["asset"], item["hardware_profile"], item["method"]): item
        for item in valid
    }
    method_codes = [str(item.get("code", "")) for item in config.get("methods", [])]
    scenario_codes = [str(item.get("code", "")) for item in config.get("scenarios", [])]
    seeds = [int(seed) for seed in config.get("seeds", [])]
    assets = sorted({str(item.get("asset", "")) for item in valid}) or [str(config.get("run_defaults", {}).get("asset", ""))]
    profiles = sorted({str(item.get("hardware_profile", "")) for item in valid}) or [
        str(config.get("run_defaults", {}).get("hardware_profile", ""))
    ]

    pair_rows: list[dict[str, Any]] = []
    aggregate_rows: list[dict[str, Any]] = []
    issues: list[dict[str, Any]] = []

    metric_names = (
        "avg_frame_ms",
        "p95_frame_ms",
        "p99_frame_ms",
        "p999_frame_ms",
        "sixty_fps_ratio",
        "frame_budget_overrun_rate",
    )
    for scenario in scenario_codes:
        for method in method_codes:
            if method == baseline_method:
                continue
            expected_pairs = 0
            collected: dict[str, list[float]] = {name: [] for name in metric_names}
            for seed in seeds:
                for asset in assets:
                    for profile in profiles:
                        expected_pairs += 1
                        item = lookup.get((scenario, seed, asset, profile, method))
                        baseline = lookup.get((scenario, seed, asset, profile, baseline_method))
                        if item is None or baseline is None:
                            _issue(
                                issues,
                                "error",
                                "missing_matched_baseline_pair",
                                f"Missing pair for scenario={scenario}, method={method}, seed={seed}, asset={asset}, profile={profile}.",
                            )
                            continue
                        row: dict[str, Any] = {
                            "scenario": scenario,
                            "seed": seed,
                            "asset": asset,
                            "hardware_profile": profile,
                            "method": method,
                            "baseline": baseline_method,
                        }
                        for name in metric_names:
                            delta = float(item["derived"][name]) - float(baseline["derived"][name])
                            row[f"delta_{name}"] = delta
                            collected[name].append(delta)
                        baseline_p95 = max(float(baseline["derived"]["p95_frame_ms"]), 1.0e-12)
                        row["relative_delta_p95"] = row["delta_p95_frame_ms"] / baseline_p95
                        pair_rows.append(row)

            matched_count = len(collected["p95_frame_ms"])
            aggregate: dict[str, Any] = {
                "scenario": scenario,
                "method": method,
                "baseline": baseline_method,
                "matched_seed_count": matched_count,
                "expected_pair_count": expected_pairs,
                "pair_coverage_rate": matched_count / expected_pairs if expected_pairs else 0.0,
                "minimum_seed_count_met": matched_count >= min_seeds,
                "pairing_complete": matched_count == expected_pairs,
            }
            for metric_index, name in enumerate(metric_names):
                values = collected[name]
                if values:
                    low, high = bootstrap_ci(values, confidence, iterations, seed=20260715 + metric_index)
                    aggregate[f"mean_delta_{name}"] = statistics.fmean(values)
                    aggregate[f"ci_low_delta_{name}"] = low
                    aggregate[f"ci_high_delta_{name}"] = high
                else:
                    aggregate[f"mean_delta_{name}"] = 0.0
                    aggregate[f"ci_low_delta_{name}"] = 0.0
                    aggregate[f"ci_high_delta_{name}"] = 0.0
            aggregate["conservative_performance_win"] = (
                aggregate["ci_high_delta_p95_frame_ms"] < 0.0
                and aggregate["ci_high_delta_p99_frame_ms"] <= 0.0
                and aggregate["ci_low_delta_sixty_fps_ratio"] >= 0.0
            )
            aggregate_rows.append(aggregate)

    for method in method_codes:
        if method == baseline_method:
            continue
        method_rows = [row for row in pair_rows if row["method"] == method]
        if not method_rows:
            continue
        overall: dict[str, Any] = {
            "scenario": "__ALL__",
            "method": method,
            "baseline": baseline_method,
            "matched_seed_count": len(method_rows),
            "expected_pair_count": len(method_rows),
            "pair_coverage_rate": 1.0,
            "minimum_seed_count_met": len(method_rows) >= min_seeds,
            "pairing_complete": True,
        }
        for metric_index, name in enumerate(metric_names):
            values = [float(row[f"delta_{name}"]) for row in method_rows]
            low, high = bootstrap_ci(values, confidence, iterations, seed=20260815 + metric_index)
            overall[f"mean_delta_{name}"] = statistics.fmean(values)
            overall[f"ci_low_delta_{name}"] = low
            overall[f"ci_high_delta_{name}"] = high
        overall["conservative_performance_win"] = (
            overall["ci_high_delta_p95_frame_ms"] < 0.0
            and overall["ci_high_delta_p99_frame_ms"] <= 0.0
            and overall["ci_low_delta_sixty_fps_ratio"] >= 0.0
        )
        aggregate_rows.append(overall)
    return pair_rows, aggregate_rows, issues


def validate_coverage(config: dict[str, Any], evidences: list[dict[str, Any]]) -> list[dict[str, Any]]:
    valid_keys = {
        (item["scenario"], item["method"], item["seed"])
        for item in evidences
        if item.get("valid") and item.get("run_role", "primary") == "primary"
    }
    rows: list[dict[str, Any]] = []
    for scenario in config.get("scenarios", []):
        for method in config.get("methods", []):
            for seed in config.get("seeds", []):
                key = (str(scenario["code"]), str(method["code"]), int(seed))
                rows.append(
                    {
                        "scenario": key[0],
                        "method": key[1],
                        "seed": key[2],
                        "valid_evidence_present": key in valid_keys,
                    }
                )
    return rows


def validate_determinism(
    config: dict[str, Any],
    evidences: list[dict[str, Any]],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    rows: list[dict[str, Any]] = []
    issues: list[dict[str, Any]] = []
    replay_groups = {str(item.get("id", "")): item for item in config.get("determinism_replays", [])}
    for group_id, contract in replay_groups.items():
        group = [item for item in evidences if item.get("replay_group_id") == group_id and item.get("valid")]
        hashes = [str(item.get("canonical_result_sha256", "")) for item in group]
        expected_repetitions = int(contract.get("repetitions", 2))
        valid_hashes = [value for value in hashes if is_sha256(value)]
        mismatch = len(set(valid_hashes)) > 1
        row = {
            "replay_group_id": group_id,
            "method": contract.get("method", ""),
            "scenario": contract.get("scenario", ""),
            "seed": contract.get("seed", 0),
            "expected_repetitions": expected_repetitions,
            "valid_repetitions": len(group),
            "valid_hash_count": len(valid_hashes),
            "hash_mismatch": mismatch,
            "passed": len(group) == expected_repetitions and len(valid_hashes) == expected_repetitions and not mismatch,
        }
        rows.append(row)
        if not row["passed"]:
            _issue(
                issues,
                "error",
                "determinism_replay_failed",
                f"Determinism replay {group_id} is incomplete, missing hashes, or mismatched.",
            )
    return rows, issues


def compare_value(value: float, comparator: str, threshold: float) -> bool:
    if comparator == "<":
        return value < threshold
    if comparator == "<=":
        return value <= threshold
    if comparator == ">":
        return value > threshold
    if comparator == ">=":
        return value >= threshold
    if comparator == "==":
        return math.isclose(value, threshold, rel_tol=1.0e-9, abs_tol=1.0e-12)
    raise ValueError(f"Unsupported comparator: {comparator}")


def evaluate_hypotheses(
    config: dict[str, Any],
    evidences: list[dict[str, Any]],
    pair_aggregates: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for hypothesis in config.get("hypotheses", []):
        source = str(hypothesis.get("source", "runtime"))
        metric = str(hypothesis.get("metric", ""))
        comparator = str(hypothesis.get("comparator", "<="))
        threshold = float(hypothesis.get("threshold", hypothesis.get("threshold_delta", 0.0)))
        observed: float | None = None
        sample_count = 0

        if source == "safe_pairwise":
            candidates = [
                row
                for row in pair_aggregates
                if row.get("scenario") == hypothesis.get("scenario", row.get("scenario"))
                and row.get("method") == hypothesis.get("method")
                and row.get("baseline") == hypothesis.get("baseline", config.get("baseline_method"))
            ]
            if not hypothesis.get("scenario"):
                all_rows = [row for row in candidates if row.get("scenario") == "__ALL__"]
                candidates = all_rows or candidates
            if candidates and metric in candidates[0]:
                observed = float(candidates[0][metric])
                sample_count = int(candidates[0].get("matched_seed_count", 0))
        else:
            candidates = [
                item
                for item in evidences
                if item.get("valid")
                and item.get("run_role", "primary") == "primary"
                and (not hypothesis.get("scenario") or item.get("scenario") == hypothesis.get("scenario"))
                and (not hypothesis.get("method") or item.get("method") == hypothesis.get("method"))
            ]
            values: list[float] = []
            for item in candidates:
                metric_map = {
                    "avg_p95_frame_ms": item.get("derived", {}).get("p95_frame_ms"),
                    "avg_p99_frame_ms": item.get("derived", {}).get("p99_frame_ms"),
                    "avg_sixty_fps_ratio": item.get("derived", {}).get("sixty_fps_ratio"),
                    "avg_false_static_rate": item.get("metrics", {}).get("false_static_rate"),
                    "avg_repair_success_rate": item.get("metrics", {}).get("repair_success_rate"),
                    "avg_collision_miss_rate": item.get("metrics", {}).get("collision_miss_rate"),
                    "avg_state_oscillation_rate": item.get("metrics", {}).get("state_oscillation_rate"),
                }
                value = metric_map.get(metric)
                if value is not None and math.isfinite(float(value)):
                    values.append(float(value))
            if values:
                observed = statistics.fmean(values)
                sample_count = len(values)

        status = "not_evaluable" if observed is None else ("supported" if compare_value(observed, comparator, threshold) else "not_supported")
        results.append(
            {
                "hypothesis_id": hypothesis.get("hypothesis_id", ""),
                "title": hypothesis.get("title", ""),
                "source": source,
                "metric": metric,
                "comparator": comparator,
                "threshold": threshold,
                "observed": observed,
                "sample_count": sample_count,
                "status": status,
            }
        )
    return results


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


def _strip_private_payload(evidence: dict[str, Any]) -> dict[str, Any]:
    cleaned = copy.deepcopy(evidence)
    for key in ("frame_ms", "combined", "runtime_combined", "case_snapshot", "frame_headers"):
        cleaned.pop(key, None)
    return cleaned


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Strict evidence gate, randomized matrix expander, and matched-baseline validator for HairSystem novelty01."
    )
    parser.add_argument("--config", "--suite", dest="config", required=True)
    parser.add_argument("--viewer-exe")
    parser.add_argument("--output")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--rerun-failed", action="store_true")
    parser.add_argument("--fail-policy", choices=["stop", "continue"], default="stop")
    parser.add_argument("--case-filter")
    parser.add_argument("--variant-filter")
    parser.add_argument("--scenario-filter")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--validate-only", action="store_true", help="Validate existing telemetry without launching the viewer.")
    parser.add_argument("--allow-unfrozen-asset", action="store_true", help="Development-only escape hatch; never use for paper evidence.")
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    raw_config = read_json(config_path)
    resolved_config, source_issues = resolve_source_identity(raw_config, config_path)
    expanded, runs = expand_matrix(resolved_config, config_path)
    expanded_path = config_path.with_name(f"{config_path.stem}.expanded.json")
    write_json(expanded_path, expanded)

    static_issues = source_issues + validate_static_contract(expanded, config_path, runs)
    if args.allow_unfrozen_asset:
        removable_codes = {
            "unfrozen_asset",
            "invalid_asset_hash",
            "frozen_asset_missing",
            "frozen_asset_hash_mismatch",
            "dirty_git_worktree",
        }
        static_issues = [item for item in static_issues if item["code"] not in removable_codes]
    static_report_path = config_path.with_name(f"{config_path.stem}.static_preflight.json")
    write_json(
        static_report_path,
        {
            "sourceConfig": str(config_path),
            "sourceConfigSha256": sha256_file(config_path),
            "expandedConfig": str(expanded_path),
            "expandedConfigSha256": sha256_file(expanded_path),
            "resolvedSource": expanded.get("source", {}),
            "runCount": len(runs),
            "primaryRunCount": sum(run.get("run_role", "primary") == "primary" for run in runs),
            "issues": static_issues,
            "valid": not any(item["severity"] == "error" for item in static_issues),
        },
    )

    static_invalid = any(item["severity"] == "error" for item in static_issues)
    if static_invalid and expanded.get("preflight", {}).get("strict", True):
        print(f"Static preflight failed. See {static_report_path}", file=sys.stderr)
        return 2
    if args.prepare_only:
        print(f"Expanded {len(runs)} runs: {expanded_path}")
        print(f"Static preflight: {static_report_path}")
        return 0

    runner_code = 0 if args.validate_only else run_original_suite(expanded_path, args)
    manifest_path = Path(str(runs[0].get("asset_manifest", ""))) if runs else Path()
    asset_manifest = read_json(manifest_path) if manifest_path.exists() else {}
    evidences: list[dict[str, Any]] = []
    validation_rows: list[dict[str, Any]] = []
    for run in runs:
        evidence = load_run_evidence(config_path, expanded, run)
        issues = validate_run_evidence(expanded, run, evidence, asset_manifest)
        evidence["issues"] = issues
        evidence["valid"] = not any(item["severity"] == "error" for item in issues)
        evidences.append(evidence)
        if issues:
            validation_rows.extend(issues)
        else:
            validation_rows.append(
                {
                    "severity": "info",
                    "code": "valid",
                    "case_id": run.get("case_id", ""),
                    "message": "Evidence passed the strict gate.",
                }
            )

    pair_rows, pair_aggregates, pairing_issues = build_matched_pairs(expanded, evidences)
    validation_rows.extend(pairing_issues)
    determinism_rows, determinism_issues = validate_determinism(expanded, evidences)
    validation_rows.extend(determinism_issues)
    coverage_rows = validate_coverage(expanded, evidences)
    hypothesis_rows = evaluate_hypotheses(expanded, evidences, pair_aggregates)

    primary_evidences = [item for item in evidences if item.get("run_role", "primary") == "primary"]
    valid_primary_count = sum(bool(item.get("valid")) for item in primary_evidences)
    expected_pair_aggregate_count = len(expanded.get("scenarios", [])) * max(len(expanded.get("methods", [])) - 1, 0)
    scenario_aggregate_rows = [row for row in pair_aggregates if row.get("scenario") != "__ALL__"]
    pairing_complete = (
        len(scenario_aggregate_rows) == expected_pair_aggregate_count
        and not any(item["severity"] == "error" for item in pairing_issues)
        and all(row.get("pairing_complete") and row.get("minimum_seed_count_met") for row in scenario_aggregate_rows)
    )
    coverage_complete = bool(coverage_rows) and all(row["valid_evidence_present"] for row in coverage_rows)
    determinism_complete = not determinism_issues and all(row.get("passed") for row in determinism_rows)
    evidence_integrity_passed = (
        runner_code == 0
        and valid_primary_count == len(primary_evidences)
        and pairing_complete
        and coverage_complete
        and determinism_complete
    )

    output_dir = resolve_path(config_path.parent, expanded.get("outputs_dir")) or config_path.parent / "runtime_suite_outputs"
    output_dir.mkdir(parents=True, exist_ok=True)
    report_path = output_dir / "safe_evidence_report.json"
    validation_csv = output_dir / "safe_evidence_validation.csv"
    pair_csv = output_dir / "safe_matched_pairwise.csv"
    pair_summary_csv = output_dir / "safe_matched_pairwise_summary.csv"
    coverage_csv = output_dir / "safe_matrix_coverage.csv"
    determinism_csv = output_dir / "safe_determinism_replay.csv"
    hypothesis_csv = output_dir / "safe_hypothesis_results.csv"

    report = {
        "suiteName": expanded.get("suite_name", config_path.stem),
        "suiteVersion": expanded.get("suite_version", ""),
        "sourceRepository": expanded.get("source", {}).get("repository", ""),
        "sourceBranch": expanded.get("source", {}).get("branch", "Novelty"),
        "sourceCommit": expanded.get("source", {}).get("commit", ""),
        "expectedPrimaryRunCount": len(primary_evidences),
        "validPrimaryEvidenceCount": valid_primary_count,
        "invalidPrimaryEvidenceCount": len(primary_evidences) - valid_primary_count,
        "coverageRate": valid_primary_count / len(primary_evidences) if primary_evidences else 0.0,
        "runnerExitCode": runner_code,
        "pairingComplete": pairing_complete,
        "coverageComplete": coverage_complete,
        "determinismComplete": determinism_complete,
        "evidenceIntegrityPassed": evidence_integrity_passed,
        "allHypothesesSupported": bool(hypothesis_rows) and all(row["status"] == "supported" for row in hypothesis_rows),
        "hypotheses": hypothesis_rows,
        "runs": [_strip_private_payload(item) for item in evidences],
        "pairwiseSummary": pair_aggregates,
        "determinism": determinism_rows,
    }
    write_json(report_path, report)
    write_csv(validation_csv, validation_rows)
    write_csv(pair_csv, pair_rows)
    write_csv(pair_summary_csv, pair_aggregates)
    write_csv(coverage_csv, coverage_rows)
    write_csv(determinism_csv, determinism_rows)
    write_csv(hypothesis_csv, hypothesis_rows)

    print(f"Expanded runs: {len(runs)}")
    print(f"Valid primary evidence: {valid_primary_count}/{len(primary_evidences)}")
    print(f"Evidence integrity: {'PASS' if evidence_integrity_passed else 'FAIL'}")
    print(f"Hypotheses: {sum(row['status'] == 'supported' for row in hypothesis_rows)}/{len(hypothesis_rows)} supported")
    print(f"Safe evidence report: {report_path}")
    if expanded.get("preflight", {}).get("strict", True) and not evidence_integrity_passed:
        return 3
    return runner_code


if __name__ == "__main__":
    raise SystemExit(main())
