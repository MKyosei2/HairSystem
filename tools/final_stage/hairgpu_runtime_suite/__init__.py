from __future__ import annotations

"""Compatibility facade for :mod:`hairgpu_runtime_suite`.

The strict production implementation remains in ``../hairgpu_runtime_suite.py``.
This facade preserves older import-level helpers without changing the command-
line runner or the new formal execution path.
"""

import copy
import importlib.util
import json
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

import hairgpu_final_run as final_run

_IMPLEMENTATION_PATH = Path(__file__).resolve().parent.parent / "hairgpu_runtime_suite.py"
_SPEC = importlib.util.spec_from_file_location(
    "_hairgpu_runtime_suite_implementation",
    _IMPLEMENTATION_PATH,
)
if _SPEC is None or _SPEC.loader is None:
    raise ImportError(f"Cannot load HairSystem runtime-suite implementation: {_IMPLEMENTATION_PATH}")
_impl = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_impl)

for _name, _value in vars(_impl).items():
    if _name not in {"__name__", "__package__", "__loader__", "__spec__"}:
        globals()[_name] = _value


def _selected_strings(values: Sequence[Any] | None) -> set[str]:
    return {str(value) for value in (values or []) if str(value)}


def _selected_ints(values: Sequence[Any] | None) -> set[int]:
    return {int(value) for value in (values or [])}


def prepare_suite_config(
    config: Mapping[str, Any],
    *,
    only_methods: Sequence[str] | None = None,
    only_scenarios: Sequence[str] | None = None,
    only_seeds: Sequence[int] | None = None,
) -> dict[str, Any]:
    """Normalize the legacy in-memory suite representation.

    No files are touched. The returned structure can be passed to the legacy
    two-argument ``expand_run_matrix`` helper or inspected by old tooling.
    """

    prepared = copy.deepcopy(dict(config))
    source = prepared.get("source") if isinstance(prepared.get("source"), dict) else {}
    defaults = prepared.get("run_defaults") if isinstance(prepared.get("run_defaults"), dict) else {}
    prepared["run_defaults"] = defaults

    if "guide_count" in defaults and "guide_strand_count" not in defaults:
        defaults["guide_strand_count"] = defaults.pop("guide_count")
    for key in ("viewer_exe", "asset_manifest", "shader_root"):
        if key in source and key not in defaults:
            defaults[key] = source[key]
    if "schema" in source:
        prepared["schema"] = source["schema"]

    requested_methods = {
        final_run.normalize_method_code(value)
        for value in _selected_strings(only_methods)
    }
    methods: list[dict[str, Any]] = []
    for raw in prepared.get("methods", []):
        if not isinstance(raw, dict):
            continue
        entry = copy.deepcopy(raw)
        code = final_run.normalize_method_code(str(entry.get("code", "")))
        if requested_methods and code not in requested_methods:
            continue
        entry["code"] = code
        entry.setdefault("settings", {})
        methods.append(entry)
    prepared["methods"] = methods

    requested_scenarios = _selected_strings(only_scenarios)
    scenarios: list[dict[str, Any]] = []
    for raw in prepared.get("scenarios", []):
        if not isinstance(raw, dict):
            continue
        entry = copy.deepcopy(raw)
        code = str(entry.get("code", ""))
        if requested_scenarios and code not in requested_scenarios:
            continue
        entry.setdefault("assertions", {})
        scenarios.append(entry)
    prepared["scenarios"] = scenarios

    requested_seeds = _selected_ints(only_seeds)
    seeds = [int(seed) for seed in prepared.get("seeds", [])]
    if requested_seeds:
        seeds = [seed for seed in seeds if seed in requested_seeds]
    prepared["seeds"] = seeds

    selected_method_codes = [str(entry["code"]) for entry in methods]
    preflight = prepared.get("preflight") if isinstance(prepared.get("preflight"), dict) else {}
    prepared["preflight"] = preflight
    preflight["required_method_codes"] = selected_method_codes
    preflight["required_scenario_codes"] = [
        str(entry.get("code", "")) for entry in scenarios
    ]

    prepared["scenario_matrix"] = [
        {
            "scenario": str(entry.get("code", "")),
            "timeline": entry.get("timeline"),
            "assertions": copy.deepcopy(entry.get("assertions", {})),
            "required_methods": list(selected_method_codes),
        }
        for entry in scenarios
    ]
    return prepared


def _baseline_assertions(assertions: Mapping[str, Any]) -> dict[str, Any]:
    value = assertions.get(
        "min_applied_events",
        assertions.get("minimumAppliedEventCount", 0),
    )
    return {"min_applied_events": max(0, int(value))}


def _proposal_assertions(assertions: Mapping[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(dict(assertions))
    if "minimumAppliedEventCount" in result and "min_applied_events" not in result:
        result["min_applied_events"] = result.pop("minimumAppliedEventCount")
    if "min_applied_events" not in result:
        result["min_applied_events"] = 0
    if int(result["min_applied_events"]) < 0:
        raise ValueError("min_applied_events cannot be negative for non-baseline methods")
    return result


def _resolve_timeline(config_dir: Path, value: Any) -> str:
    path = Path(str(value))
    if not path.is_absolute():
        path = config_dir / path
    return str(path.resolve())


def _formal_runs(
    config: Mapping[str, Any],
    config_dir: Path,
) -> list[dict[str, Any]]:
    defaults = copy.deepcopy(
        config.get("run_defaults")
        if isinstance(config.get("run_defaults"), dict)
        else {}
    )
    source = config.get("source") if isinstance(config.get("source"), dict) else {}
    repetitions = int(config.get("repetitions_per_seed", 1))
    runs: list[dict[str, Any]] = []

    for method_entry in config.get("methods", []):
        if not isinstance(method_entry, dict):
            continue
        method = final_run.normalize_method_code(str(method_entry.get("code", "")))
        for scenario_entry in config.get("scenarios", []):
            if not isinstance(scenario_entry, dict):
                continue
        
            scenario = str(scenario_entry.get("code", ""))
            timeline = _resolve_timeline(config_dir, scenario_entry.get("timeline"))
            raw_assertions = (
                scenario_entry.get("assertions")
                if isinstance(scenario_entry.get("assertions"), dict)
                else {}
            )
            assertions = (
                _baseline_assertions(raw_assertions)
                if method == "B0"
                else _proposal_assertions(raw_assertions)
            )
            for seed in config.get("seeds", []):
                for repetition in range(repetitions):
                    run_name = (
                        f"{method}__{scenario}__seed{int(seed)}__r{repetition:02d}"
                    )
                    run = copy.deepcopy(defaults)
                    run.update({
                        "run_name": run_name,
                        "case_id": run_name,
                        "method": method,
                        "variant": method,
                        "scenario": scenario,
                        "scenario_timeline": timeline,
                        "seed": int(seed),
                        "repetition_index": repetition,
                        "assertions": assertions,
                        "method_settings": copy.deepcopy(
                            method_entry.get("settings", {})
                        ),
                        "asset_manifest": source.get(
                            "asset_manifest",
                            defaults.get("asset_manifest"),
                        ),
                        "shader_root": source.get(
                            "shader_root",
                            defaults.get("shader_root"),
                        ),
                    })
                    runs.append(final_run._normalize_case(run))
    return runs


def _find_template(
    runs: Sequence[Mapping[str, Any]],
    method: str,
    scenario: str,
    seed: int,
) -> dict[str, Any]:
    normalized_method = final_run.normalize_method_code(method)
    for raw in runs:
        if (
            str(raw.get("method")) == normalized_method
            and str(raw.get("scenario")) == scenario
            and int(raw.get("seed", -1)) == int(seed)
        ):
            return copy.deepcopy(dict(raw))
    raise ValueError(
        f"Special-run template not found: {normalized_method}/{scenario}/{seed}"
    )


def expand_run_matrix(
    config: Mapping[str, Any],
    config_dir: Path,
    repo_root: Path | None = None,
) -> list[dict[str, Any]]:
    """Expand the legacy all-in-one run matrix.

    Production ``execute_suite`` continues to use the strict implementation's
    separated formal/determinism/long-run paths. This helper exists for older
    callers that expect one combined list.
    """

    config_dir = Path(config_dir).resolve()
    prepared = (
        prepare_suite_config(config)
        if "scenario_matrix" not in config
        else copy.deepcopy(dict(config))
    )
    runs = _formal_runs(prepared, config_dir)

    for replay in prepared.get("determinism_replays", []):
        if not isinstance(replay, dict):
            continue
        template = _find_template(
            runs,
            str(replay.get("method", "")),
            str(replay.get("scenario", "")),
            int(replay.get("seed", 0)),
        )
        for index in range(int(replay.get("repetitions", 1))):
            run = copy.deepcopy(template)
            run_name = f"{replay.get('id', 'DET')}_r{index:02d}"
            run.update({
                "run_name": run_name,
                "case_id": run_name,
                "repetition_index": index,
                "measurement_mode": "determinism",
                "special_run_kind": "determinism",
                "special_kind": "determinism",
            })
            runs.append(final_run._normalize_case(run))

    for long_run in prepared.get("long_runs", []):
        if not isinstance(long_run, dict):
            continue
        template = _find_template(
            runs,
            str(long_run.get("method", "")),
            str(long_run.get("scenario", "")),
            int(long_run.get("seed", 0)),
        )
        run = copy.deepcopy(template)
        run_name = str(long_run.get("id", "LONG"))
        run.update({
            "run_name": run_name,
            "case_id": run_name,
            "measurement_frames": int(
                long_run.get(
                    "measurement_frames",
                    run.get("measurement_frames", 6000),
                )
            ),
            "timeout_seconds": int(
                long_run.get(
                    "timeout_seconds",
                    run.get("timeout_seconds", 900),
                )
            ),
            "timeline_repeat": bool(long_run.get("timeline_repeat", True)),
            "measurement_mode": "robustness",
            "special_run_kind": "long_run",
            "special_kind": "long_run",
        })
        runs.append(final_run._normalize_case(run))

    return runs


def expected_telemetry_paths(
    output_root: Path,
    run: Mapping[str, Any],
    telemetry_root: Path | None = None,
) -> dict[str, Path]:
    output_root = Path(output_root).resolve()
    telemetry_root = (
        Path(telemetry_root).resolve()
        if telemetry_root is not None
        else output_root
    )
    output_root.mkdir(parents=True, exist_ok=True)
    telemetry_root.mkdir(parents=True, exist_ok=True)
    run_name = str(run.get("run_name", run.get("case_id", "case")))
    return {
        "summary": output_root / f"{run_name}_summary.json",
        "frame_csv": telemetry_root / f"{run_name}_frames.csv",
        "frame_jsonl": telemetry_root / f"{run_name}_frames.jsonl",
        "settings": output_root / f"{run_name}_settings.json",
        "environment": output_root / f"{run_name}_environment.json",
        "validation": output_root / f"{run_name}_validation.json",
        "manifest": output_root / f"{run_name}_artifact_manifest.json",
        "complete": output_root / f"{run_name}_COMPLETE.json",
        "incomplete": output_root / f"{run_name}_INCOMPLETE.json",
        "error": output_root / f"{run_name}_ERROR.json",
        "lock": output_root / f"{run_name}.run.lock",
    }


def _artifact_manifest_valid(
    manifest: Mapping[str, Any],
    output_root: Path,
    telemetry_root: Path,
) -> bool:
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        return False
    for raw in artifacts:
        if not isinstance(raw, dict):
            return False
        name = str(raw.get("path", ""))
        candidates = [output_root / name, telemetry_root / name]
        path = next((candidate for candidate in candidates if candidate.is_file()), None)
        if path is None:
            return False
        try:
            expected_size = int(raw.get("sizeBytes", -1))
        except (TypeError, ValueError):
            return False
        if path.stat().st_size != expected_size:
            return False
        if final_run.sha256_file(path).lower() != str(raw.get("sha256", "")).lower():
            return False
    return True


def is_completed_case(
    output_root: Path,
    run: Mapping[str, Any],
    telemetry_root: Path | None = None,
) -> bool:
    paths = expected_telemetry_paths(output_root, run, telemetry_root)
    if paths["error"].exists() or paths["incomplete"].exists() or paths["lock"].exists():
        return False
    for key in (
        "summary",
        "frame_csv",
        "frame_jsonl",
        "settings",
        "environment",
        "validation",
        "manifest",
    ):
        if not paths[key].is_file():
            return False
    try:
        summary = final_run.read_json(paths["summary"])
        validation = final_run.read_json(paths["validation"])
        manifest = final_run.read_json(paths["manifest"])
    except (OSError, ValueError, json.JSONDecodeError):
        return False

    case_id = str(run.get("case_id", run.get("run_name", "")))
    settings_hash = str(run.get("settings_hash", ""))
    if str(summary.get("caseId", case_id)) != case_id:
        return False
    if settings_hash and str(summary.get("settingsSha256", settings_hash)).lower() != settings_hash.lower():
        return False
    if validation.get("isValid") is not True:
        return False
    if validation.get("evidenceIntegrityPassed") is not True:
        return False
    status = str(validation.get("status", summary.get("validationStatus", "")))
    if status not in {"complete_valid", "quality_gate_failed"}:
        return False
    result_hash = str(summary.get("canonicalResultSha256", ""))
    manifest_hash = str(manifest.get("canonicalResultSha256", result_hash))
    if result_hash and manifest_hash.lower() != result_hash.lower():
        return False
    return _artifact_manifest_valid(
        manifest,
        Path(output_root).resolve(),
        Path(telemetry_root).resolve() if telemetry_root is not None else Path(output_root).resolve(),
    )


__all__ = sorted(name for name in globals() if not name.startswith("__"))
