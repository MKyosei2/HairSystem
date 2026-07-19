from __future__ import annotations

"""Compatibility facade for :mod:`hairgpu_final_run`.

The production implementation remains in ``../hairgpu_final_run.py`` so the
existing command-line entry point is unchanged. Importers receive this package,
which re-exports the production implementation and restores the small private
API used by older automation and regression tests.
"""

import importlib.util
import math
import os
import re
from pathlib import Path
from types import ModuleType
from typing import Any, Mapping

_IMPLEMENTATION_PATH = Path(__file__).resolve().parent.parent / "hairgpu_final_run.py"
_SPEC = importlib.util.spec_from_file_location(
    "_hairgpu_final_run_implementation",
    _IMPLEMENTATION_PATH,
)
if _SPEC is None or _SPEC.loader is None:
    raise ImportError(f"Cannot load HairSystem final-run implementation: {_IMPLEMENTATION_PATH}")
_impl = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_impl)

for _name, _value in vars(_impl).items():
    if _name not in {"__name__", "__package__", "__loader__", "__spec__"}:
        globals()[_name] = _value


def _git_commit_sha(repo_root: Path) -> str:
    return str(_impl.git_value(Path(repo_root), "rev-parse", "HEAD"))


def _required_quality_metrics(method: str, scenario: str) -> list[str]:
    method = _impl.normalize_method_code(method)
    if method == "B0":
        return []
    metrics: list[str] = []
    if scenario == "S07_ColliderApproach":
        metrics.extend(["collisionMissCount", "predictiveWakeRecall"])
    elif scenario == "S08_StaticDynamicBoundaryContact":
        metrics.append("staticDynamicBoundaryMaxErrorMm")
    elif scenario == "S12_FalseStaticTrap":
        metrics.extend(["falseStaticRate", "predictiveWakeRecall"])
    elif scenario == "S13_RepairStorm":
        metrics.extend(["repairVerifiedSuccessRate", "repairRecurrenceRate"])
    elif scenario == "S16_StateThrashing":
        metrics.extend(["stateOscillationCount", "determinismMismatchCount"])
    return metrics


def _normalize_case(case: Mapping[str, Any]) -> dict[str, Any]:
    normalized = _impl.normalize_case(case)
    for camel, snake in (
        ("formalAssetPath", "formal_asset_path"),
        ("formalAssetSha256", "formal_asset_sha256"),
        ("formalTopologySha256", "formal_topology_sha256"),
        ("generatorSettingsSha256", "generator_settings_sha256"),
    ):
        if camel in case and snake not in normalized:
            normalized[snake] = case[camel]
        if camel in case:
            normalized[camel] = case[camel]
    normalized.setdefault(
        "required_quality_metrics",
        _required_quality_metrics(
            str(normalized.get("method", "")),
            str(normalized.get("scenario", "")),
        ),
    )
    return normalized


def _validate_timeline(path: Path, case: Mapping[str, Any]) -> list[dict[str, Any]]:
    timeline = _impl.read_json(Path(path))
    problems: list[dict[str, Any]] = []
    expected_scenario = str(case.get("scenario", ""))
    observed_scenario = str(timeline.get("scenario", ""))
    if expected_scenario and observed_scenario != expected_scenario:
        problems.append(_impl.issue(
            "E_TIMELINE_SCENARIO",
            "Timeline scenario mismatch.",
            expected=expected_scenario,
            observed=observed_scenario,
        ))

    required_duration = int(case.get("warmup_frames", 0)) + int(
        case.get("measurement_frames", 0)
    )
    try:
        duration = int(timeline.get("durationFrames", -1))
    except (TypeError, ValueError):
        duration = -1
    if duration < required_duration:
        problems.append(_impl.issue(
            "E_TIMELINE_DURATION",
            "Timeline is shorter than the requested run.",
            expected=required_duration,
            observed=duration,
        ))

    try:
        observed_dt = float(timeline.get("fixedTimeStepSeconds"))
        expected_dt = float(case.get("fixed_timestep_seconds", 1.0 / 60.0))
    except (TypeError, ValueError):
        observed_dt = math.nan
        expected_dt = 1.0 / 60.0
    if not math.isfinite(observed_dt) or abs(observed_dt - expected_dt) > 1.0e-8:
        problems.append(_impl.issue(
            "E_TIMELINE_TIMESTEP",
            "Timeline fixed timestep mismatch.",
            expected=expected_dt,
            observed=timeline.get("fixedTimeStepSeconds"),
        ))

    events = timeline.get("events")
    if not isinstance(events, list):
        return problems + [_impl.issue(
            "E_TIMELINE_EVENTS",
            "Timeline events must be an array.",
        )]

    previous_frame = -1
    stable_ids: set[str] = set()
    for index, event in enumerate(events):
        if not isinstance(event, dict):
            problems.append(_impl.issue(
                "E_TIMELINE_EVENT_TYPE",
                f"Event {index} is not an object.",
            ))
            continue
        try:
            frame = int(event.get("frame", -1))
        except (TypeError, ValueError):
            frame = -1
        if frame < 0 or frame >= duration or frame < previous_frame:
            problems.append(_impl.issue(
                "E_TIMELINE_EVENT_FRAME",
                f"Invalid event frame at index {index}.",
                observed=frame,
            ))
        previous_frame = max(previous_frame, frame)
        event_id = str(event.get("id", ""))
        if event_id:
            if event_id in stable_ids:
                problems.append(_impl.issue(
                    "E_TIMELINE_EVENT_ID_DUPLICATE",
                    "Timeline event id is duplicated.",
                    observed=event_id,
                ))
            stable_ids.add(event_id)

    assertions = case.get("assertions") if isinstance(case.get("assertions"), dict) else {}
    required_events = assertions.get(
        "min_applied_events",
        assertions.get("minimumAppliedEventCount", 0),
    )
    try:
        required_events = int(required_events)
    except (TypeError, ValueError):
        required_events = -1
    if required_events < 0 or len(events) < required_events:
        problems.append(_impl.issue(
            "E_TIMELINE_EVENTS",
            "Timeline does not satisfy the discrete-event contract.",
            expected=max(required_events, 0),
            observed=len(events),
        ))
    return problems


def _materialize_repeated_timeline(
    case: Mapping[str, Any],
    output_dir: Path,
    run_name: str = "repeated_timeline",
) -> dict[str, Any]:
    normalized = dict(case)
    normalized.setdefault("run_name", run_name)
    normalized.setdefault("case_id", run_name)
    normalized.setdefault("fixed_timestep_seconds", 1.0 / 60.0)
    normalized.setdefault("scenario", "")
    return _impl.materialize_repeated_timeline(
        normalized,
        Path(output_dir),
        Path(__file__).resolve().parents[3],
    )


def _expected_runtime_settings(case: Mapping[str, Any]) -> dict[str, Any]:
    normalized = _normalize_case(case)
    return {
        "buildConfiguration": normalized.get("build_configuration", "Release"),
        "platform": normalized.get("platform", "x64"),
        "renderWidth": int(normalized.get("width", 1920)),
        "renderHeight": int(normalized.get("height", 1080)),
        "displayStrandCount": int(normalized.get("display_strand_count", 100000)),
        "guideCount": int(normalized.get("guide_strand_count", 4000)),
        "particlesPerGuide": int(normalized.get("particles_per_guide", 128)),
        "solverIterations": int(normalized.get("solver_iterations", 6)),
        "fixedTimeStepSeconds": float(
            normalized.get("fixed_timestep_seconds", 1.0 / 60.0)
        ),
        "vsync": bool(normalized.get("vsync", False)),
        "presentSyncInterval": int(normalized.get("present_sync_interval", 0)),
    }


def _canonical_path(value: Any) -> str:
    if value in (None, ""):
        return ""
    try:
        return os.path.normcase(str(Path(str(value)).resolve()))
    except OSError:
        return os.path.normcase(str(Path(str(value))))


def _compare_settings(
    settings: Mapping[str, Any],
    case: Mapping[str, Any],
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    normalized = _normalize_case(case)
    problems: list[dict[str, Any]] = []
    method = str(normalized.get("method", ""))
    expected_flags = dict(_impl.METHOD_PRESETS.get(method, {}))
    requested = settings.get("requested")
    applied = settings.get("applied")
    observed_flags = settings.get("observedFlags")
    if expected_flags and requested != expected_flags:
        problems.append(_impl.issue(
            "E_REQUESTED_METHOD_FLAGS",
            "Requested method flags do not match the method preset.",
            expected=expected_flags,
            observed=requested,
        ))
    if requested != applied or applied != observed_flags:
        problems.append(_impl.issue(
            "E_METHOD_FLAG_PARITY",
            "Requested, applied and observed method flags differ.",
            expected=requested,
            observed={"applied": applied, "observed": observed_flags},
        ))

    observed_runtime = settings.get("observed")
    expected_runtime = _expected_runtime_settings(normalized)
    if not isinstance(observed_runtime, dict):
        problems.append(_impl.issue(
            "E_OBSERVED_SETTINGS_MISSING",
            "Observed runtime settings are missing.",
        ))
    else:
        for key, expected in expected_runtime.items():
            observed = observed_runtime.get(key)
            equal = (
                abs(float(expected) - float(observed)) <= 1.0e-8
                if isinstance(expected, float) and observed is not None
                else expected == observed
            )
            if not equal:
                problems.append(_impl.issue(
                    "E_OBSERVED_SETTINGS_MISMATCH",
                    f"Observed runtime setting mismatch: {key}",
                    expected=expected,
                    observed=observed,
                ))

    formal_path = normalized.get(
        "formalAssetPath",
        normalized.get("formal_asset_path", ""),
    )
    if formal_path:
        if settings.get("formalAssetLoaded") is not True:
            problems.append(_impl.issue(
                "E_FORMAL_ASSET_NOT_LOADED",
                "Formal asset was not loaded.",
                expected=True,
                observed=settings.get("formalAssetLoaded"),
            ))
        observed_path = settings.get("observedAssetPath", "")
        if _canonical_path(observed_path) != _canonical_path(formal_path):
            problems.append(_impl.issue(
                "E_FORMAL_ASSET_PATH_MISMATCH",
                "Observed formal asset path differs from the requested path.",
                expected=_canonical_path(formal_path),
                observed=_canonical_path(observed_path),
            ))
        for key, aliases in (
            ("formalAssetSha256", ("formalAssetSha256", "formal_asset_sha256")),
            ("formalTopologySha256", ("formalTopologySha256", "formal_topology_sha256")),
            ("generatorSettingsSha256", ("generatorSettingsSha256", "generator_settings_sha256")),
        ):
            expected = next(
                (normalized.get(alias) for alias in aliases if normalized.get(alias)),
                "",
            )
            if expected and str(settings.get(key, "")).lower() != str(expected).lower():
                problems.append(_impl.issue(
                    "E_FORMAL_ASSET_HASH_MISMATCH",
                    f"Formal asset identity mismatch: {key}",
                    expected=expected,
                    observed=settings.get(key),
                ))
    return problems, expected_runtime


def _validate_asset_manifest(path: Path) -> list[dict[str, Any]]:
    manifest_path = Path(path)
    manifest = _impl.read_json(manifest_path)
    problems: list[dict[str, Any]] = []
    if manifest.get("freezeStatus") != "FROZEN":
        problems.append(_impl.issue(
            "E_ASSET_NOT_FROZEN",
            "Formal asset manifest must be frozen.",
            observed=manifest.get("freezeStatus"),
        ))

    generation = manifest.get("generation") if isinstance(manifest.get("generation"), dict) else {}
    expected_generation = {
        "displayStrandCount": 100000,
        "primaryGuideStrandCount": 4000,
        "followersPerGuide": 24,
        "particlesPerGuide": 128,
        "solverIterations": 6,
    }
    for key, expected in expected_generation.items():
        try:
            observed = int(generation.get(key, -1))
        except (TypeError, ValueError):
            observed = -1
        if observed != expected:
            problems.append(_impl.issue(
                "E_ASSET_GENERATION_CONTRACT",
                f"Frozen asset generation mismatch: {key}",
                expected=expected,
                observed=generation.get(key),
            ))

    for path_key, hash_key, size_key in (
        ("sourceAssetPath", "sourceAssetSha256", "sourceAssetSizeBytes"),
        ("generatedTopologyPath", "generatedTopologySha256", None),
        ("generatorSettingsPath", "generatorSettingsSha256", None),
    ):
        raw = manifest.get(path_key)
        member = Path(str(raw)) if raw else Path()
        if raw and not member.is_absolute():
            member = manifest_path.parent / member
        if not raw or not member.is_file():
            problems.append(_impl.issue(
                "E_ASSET_MEMBER_MISSING",
                f"Frozen asset member is missing: {path_key}",
                observed=str(member),
            ))
            continue
        expected_hash = str(manifest.get(hash_key, ""))
        actual_hash = _impl.sha256_file(member)
        if not re.fullmatch(r"[0-9a-fA-F]{64}", expected_hash) or actual_hash.lower() != expected_hash.lower():
            problems.append(_impl.issue(
                "E_ASSET_MEMBER_HASH",
                f"Frozen asset member hash mismatch: {path_key}",
                expected=expected_hash,
                observed=actual_hash,
            ))
        if size_key is not None:
            try:
                expected_size = int(manifest.get(size_key, -1))
            except (TypeError, ValueError):
                expected_size = -1
            if expected_size != member.stat().st_size:
                problems.append(_impl.issue(
                    "E_ASSET_MEMBER_SIZE",
                    f"Frozen asset member size mismatch: {path_key}",
                    expected=expected_size,
                    observed=member.stat().st_size,
                ))

    commit = str(manifest.get("assetSourceCommitSha", manifest.get("sourceCommitSha", "")))
    if not re.fullmatch(r"[0-9a-fA-F]{40}", commit):
        problems.append(_impl.issue(
            "E_ASSET_SOURCE_COMMIT_FORMAT",
            "Frozen asset source commit is invalid.",
            observed=commit,
        ))
    if manifest.get("sourceBranch") != "Novelty":
        problems.append(_impl.issue(
            "E_ASSET_SOURCE_BRANCH",
            "Frozen asset must come from Novelty.",
            expected="Novelty",
            observed=manifest.get("sourceBranch"),
        ))
    return problems


__all__ = sorted(
    name for name in globals()
    if not name.startswith("__") and name not in {"ModuleType"}
)
