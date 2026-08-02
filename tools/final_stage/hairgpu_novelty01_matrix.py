from __future__ import annotations

"""Run the Novelty 01 comparison/ablation matrix with a complete asset contract.

The previous launcher configured SG-APT variables but did not propagate the four
formal-asset variables required by the Viewer. This implementation validates the
portable frozen bundle first, exports the resolved asset contract to the child
environment, and validates both the ordinary formal output and SG-APT telemetry.
"""

import argparse
import contextlib
import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Iterable, Mapping

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_final_run as final_run  # type: ignore
from hairgpu_novelty01_preflight import preflight  # type: ignore

BASELINES = ("B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8")
ABLATIONS = tuple(f"A{i}" for i in range(1, 16))
PROPOSAL = ("P",)
ALL_VARIANTS = BASELINES + PROPOSAL + ABLATIONS
SCENARIOS = tuple(f"S{i:02d}" for i in range(1, 19))

SCENARIO_RUNTIME_NAMES = {
    "S01": "S01_Idle",
    "S02": "S02_Breathing",
    "S03": "S03_Walk",
    "S04": "S04_SuddenStop",
    "S05": "S05_HeadRotation",
    "S06": "S06_Teleport",
    "S07": "S07_ColliderApproach",
    "S08": "S08_StaticDynamicBoundaryContact",
    "S09": "S09_StrongWind",
    "S10": "S10_WindStop",
    "S11": "S11_LocalEdit",
    "S12": "S12_FalseStaticTrap",
    "S13": "S13_RepairStorm",
    "S14": "S14_AllDynamic",
    "S15": "S15_AllStatic",
    "S16": "S16_StateThrashing",
    "S17": "S17_CloseCamera100k",
    "S18": "S18_FarCamera100k",
}

PROFILE_SETTINGS: dict[str, dict[str, Any]] = {
    "smoke": {
        "warmup_frames": 120,
        "measurement_frames": 600,
        "measurement_mode": "quality",
        "ground_truth": True,
        "repetitions": 1,
        "timeout_seconds": 600,
    },
    "formal": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "quality",
        "ground_truth": True,
        "repetitions": 3,
        "timeout_seconds": 1800,
    },
    "performance": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "performance",
        "ground_truth": False,
        "repetitions": 3,
        "timeout_seconds": 1800,
    },
    "soak": {
        "warmup_frames": 600,
        "measurement_frames": 108000,
        "measurement_mode": "robustness",
        "ground_truth": True,
        "ground_truth_interval": 300,
        "repetitions": 1,
        "timeout_seconds": 7200,
    },
    "determinism": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "determinism",
        "ground_truth": True,
        "repetitions": 3,
        "timeout_seconds": 1800,
    },
    "gtx1660": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "performance",
        "ground_truth": False,
        "repetitions": 3,
        "timeout_seconds": 1800,
        "hardware_profile": "GTX1660",
        "required_adapter_name_regex": r"(?i)(GTX\s*1660|GeForce\s*GTX\s*1660)",
        "maximum_dxgi_local_usage_mib": 6144.0,
    },
}

DETERMINISM_FIELDS = (
    "stateSequenceHash",
    "compactionSequenceHash",
    "finalPositionHash",
)


@contextlib.contextmanager
def environment(overrides: Mapping[str, str]):
    previous = {key: os.environ.get(key) for key in overrides}
    os.environ.update({key: str(value) for key, value in overrides.items()})
    try:
        yield
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def parse_csv_values(raw: str, allowed: Iterable[str]) -> list[str]:
    items = list(allowed)
    by_upper = {item.upper(): item for item in items}
    if raw.strip().lower() == "all":
        return items
    result: list[str] = []
    for token in raw.split(","):
        key = token.strip().upper()
        if not key:
            continue
        if key not in by_upper:
            raise ValueError(f"Unknown value: {token}")
        result.append(by_upper[key])
    if not result:
        raise ValueError("At least one value is required.")
    return result


def resolve_frozen_asset_environment(
    case: Mapping[str, Any],
    repo: Path,
) -> tuple[dict[str, str], dict[str, Any]]:
    manifest_value = case.get("asset_manifest")
    if not manifest_value:
        raise ValueError("asset_manifest is required.")

    manifest_path = Path(str(manifest_value))
    if not manifest_path.is_absolute():
        manifest_path = repo / manifest_path
    manifest_path = manifest_path.resolve()
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Frozen manifest missing: {manifest_path}")

    allowed = case.get("allowed_asset_source_branches", ("main", "Novelty01"))
    manifest, problems = final_run.validate_asset_manifest(
        manifest_path,
        repo,
        allowed_source_branches=allowed,
    )
    if problems:
        raise RuntimeError(
            "Frozen asset validation failed: "
            + json.dumps(problems, ensure_ascii=False)
        )

    resolved = manifest.get("_resolvedMembers")
    if not isinstance(resolved, dict):
        raise RuntimeError("Frozen asset resolved members are missing.")

    source_asset = Path(str(resolved.get("sourceAssetPath", ""))).resolve()
    topology = Path(str(resolved.get("generatedTopologyPath", ""))).resolve()
    settings = Path(str(resolved.get("generatorSettingsPath", ""))).resolve()
    for path in (source_asset, topology, settings):
        if not path.is_file():
            raise FileNotFoundError(f"Frozen bundle member missing: {path}")

    values = {
        "HAIRGPU_FORMAL_ASSET_PATH": str(source_asset),
        "HAIRGPU_FORMAL_ASSET_SHA256": str(manifest["sourceAssetSha256"]),
        "HAIRGPU_FORMAL_TOPOLOGY_SHA256": str(
            manifest["generatedTopologySha256"]
        ),
        "HAIRGPU_GENERATOR_SETTINGS_SHA256": str(
            manifest["generatorSettingsSha256"]
        ),
    }
    return values, manifest


def build_case(
    template: Mapping[str, Any],
    *,
    variant: str,
    scenario: str,
    repetition: int,
    profile: str,
) -> dict[str, Any]:
    settings = PROFILE_SETTINGS[profile]
    scenario_code = scenario.upper()
    case = dict(template)
    case["case_id"] = (
        f"N01_{profile}_{variant}_{scenario_code}_r{repetition:02d}"
    )
    case["run_name"] = case["case_id"]
    case["method"] = "B0" if variant == "B0" else "P"
    case["variant"] = case["method"]
    case["novelty01_variant"] = variant
    case["novelty01_design_scenario"] = scenario_code
    case["scenario"] = SCENARIO_RUNTIME_NAMES[scenario_code]
    case["repetition_index"] = repetition
    case["warmup_frames"] = int(settings["warmup_frames"])
    case["measurement_frames"] = int(settings["measurement_frames"])
    case["measurement_mode"] = str(settings["measurement_mode"])
    case["fixed_timestep"] = True
    case["fixed_timestep_seconds"] = 1.0 / 60.0
    case["width"] = 1920
    case["height"] = 1080
    case["display_strand_count"] = 100000
    case["guide_strand_count"] = 4000
    case["followers_per_guide"] = 24
    case["particles_per_guide"] = 128
    case["solver_iterations"] = 6
    case["vsync"] = False
    case["present_sync_interval"] = 0
    case["real_measurement_required"] = True
    case["timeout_seconds"] = int(settings["timeout_seconds"])
    case["settings_hash"] = ""

    for key in (
        "hardware_profile",
        "required_adapter_name_regex",
        "maximum_dxgi_local_usage_mib",
    ):
        if key in settings:
            case[key] = settings[key]

    return final_run.normalize_case(case)


def resolve_timeline(
    case: dict[str, Any],
    timeline_root: Path,
    scenario: str,
) -> None:
    runtime_name = SCENARIO_RUNTIME_NAMES[scenario]
    canonical_prefix = runtime_name.split("_", 1)[0]
    candidates = sorted(timeline_root.glob(f"{canonical_prefix}*.json"))
    if not candidates:
        raise FileNotFoundError(
            f"No timeline beginning with {canonical_prefix} in {timeline_root}"
        )
    case["scenario_timeline"] = str(candidates[0].resolve())
    case["settings_hash"] = ""


def validate_novelty_telemetry(
    repo: Path,
    case_output: Path,
    run_name: str,
) -> dict[str, Any]:
    telemetry_path = case_output / f"{run_name}_novelty01.csv"
    validation = preflight(
        repo,
        require_fxc=False,
        telemetry=telemetry_path,
    )
    if not validation["passed"]:
        raise RuntimeError(
            "Novelty 01 telemetry validation failed: "
            + json.dumps(validation["errors"], ensure_ascii=False)
        )
    return validation


def validate_determinism(
    results: list[dict[str, Any]],
) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for row in results:
        if row.get("ok"):
            grouped.setdefault(
                (str(row["variant"]), str(row["scenario"])),
                [],
            ).append(row)

    issues: list[dict[str, Any]] = []
    for (variant, scenario), rows in grouped.items():
        for field in DETERMINISM_FIELDS:
            values: list[Any] = []
            for row in rows:
                result = row.get("result")
                aggregate = (
                    result.get("aggregate")
                    if isinstance(result, dict)
                    else None
                )
                if isinstance(aggregate, dict) and aggregate.get(field) is not None:
                    values.append(aggregate[field])
            if len(values) > 1 and len({str(value) for value in values}) != 1:
                issues.append(
                    {
                        "variant": variant,
                        "scenario": scenario,
                        "field": field,
                        "values": values,
                    }
                )
    return issues


def execute_matrix(args: argparse.Namespace) -> dict[str, Any]:
    repo = args.repo.resolve()
    viewer = args.viewer.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    source_validation = preflight(
        repo,
        require_fxc=args.require_fxc,
        telemetry=None,
    )
    (output / "novelty01_preflight.json").write_text(
        json.dumps(source_validation, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if not source_validation["passed"]:
        raise RuntimeError("Novelty 01 source preflight failed.")

    template = load_json(args.template.resolve())
    if args.timeline_root is None:
        args.timeline_root = repo / "benchmarks" / "novelty01" / "scenarios"
    timeline_root = args.timeline_root.resolve()
    if not timeline_root.is_dir():
        raise FileNotFoundError(f"Timeline root missing: {timeline_root}")

    variants = parse_csv_values(args.variants, ALL_VARIANTS)
    scenarios = parse_csv_values(args.scenarios, SCENARIOS)
    repetitions = (
        args.repetitions
        if args.repetitions is not None
        else int(PROFILE_SETTINGS[args.profile]["repetitions"])
    )

    results: list[dict[str, Any]] = []
    started = time.time()
    for variant in variants:
        for scenario in scenarios:
            for repetition in range(repetitions):
                case = build_case(
                    template,
                    variant=variant,
                    scenario=scenario,
                    repetition=repetition,
                    profile=args.profile,
                )
                resolve_timeline(case, timeline_root, scenario)
                case = final_run.normalize_case(case)
                case_output = output / str(case["case_id"])
                case_output.mkdir(parents=True, exist_ok=True)

                formal_asset_env, manifest = resolve_frozen_asset_environment(
                    case,
                    repo,
                )
                settings = PROFILE_SETTINGS[args.profile]
                overrides = {
                    **formal_asset_env,
                    "HAIRGPU_N01_VARIANT": "P" if variant == "B0" else variant,
                    "HAIRGPU_N01_GROUND_TRUTH": (
                        "1"
                        if settings.get("ground_truth")
                        and variant not in {"B0", "B1"}
                        else "0"
                    ),
                    "HAIRGPU_N01_GROUND_TRUTH_INTERVAL": str(
                        settings.get("ground_truth_interval", 1)
                    ),
                    "HAIRGPU_N01_FORMAL_TELEMETRY": (
                        "0" if variant == "B0" else "1"
                    ),
                    "HAIRGPU_N01_TELEMETRY_DIR": str(case_output),
                    "HAIRGPU_N01_RENDER_DIRTY_CACHE": "1",
                    "HAIRGPU_N01_PAYLOAD_PERCENT": (
                        "5" if scenario == "S17" else "50"
                    ),
                    "HAIRGPU_N01_DESIGN_SCENARIO": scenario,
                    "PYTHONHASHSEED": "0",
                    "PYTHONUTF8": "0",
                }

                try:
                    with environment(overrides):
                        result = final_run.execute_case(
                            case,
                            viewer,
                            case_output,
                            repo,
                            dry_run=args.dry_run,
                        )

                    telemetry_validation = None
                    if not args.dry_run and variant != "B0":
                        telemetry_validation = validate_novelty_telemetry(
                            repo,
                            case_output,
                            str(case["run_name"]),
                        )
                    results.append(
                        {
                            "caseId": case["case_id"],
                            "variant": variant,
                            "scenario": scenario,
                            "repetition": repetition,
                            "ok": True,
                            "assetSha256": manifest["sourceAssetSha256"],
                            "result": result,
                            "novelty01TelemetryValidation": telemetry_validation,
                        }
                    )
                except Exception as exc:
                    results.append(
                        {
                            "caseId": case["case_id"],
                            "variant": variant,
                            "scenario": scenario,
                            "repetition": repetition,
                            "ok": False,
                            "error": str(exc),
                        }
                    )
                    if not args.continue_on_error:
                        raise

    determinism_issues = (
        validate_determinism(results)
        if args.profile == "determinism"
        else []
    )
    payload = {
        "schema": "novelty01-matrix-v3",
        "profile": args.profile,
        "variants": variants,
        "scenarios": scenarios,
        "repetitions": repetitions,
        "dryRun": args.dry_run,
        "elapsedSeconds": time.time() - started,
        "passed": all(item["ok"] for item in results)
        and not determinism_issues,
        "determinismIssues": determinism_issues,
        "results": results,
    }
    (output / "novelty01_matrix_summary.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--viewer", type=Path, required=True)
    parser.add_argument(
        "--template",
        type=Path,
        required=True,
        help="Frozen formal case JSON used as the base.",
    )
    parser.add_argument("--timeline-root", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--profile",
        choices=tuple(PROFILE_SETTINGS),
        default="smoke",
    )
    parser.add_argument("--variants", default="all")
    parser.add_argument("--scenarios", default="all")
    parser.add_argument("--repetitions", type=int)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--continue-on-error", action="store_true")
    parser.add_argument("--require-fxc", action="store_true")
    args = parser.parse_args()

    try:
        payload = execute_matrix(args)
        print(json.dumps(payload, ensure_ascii=False, indent=2))
        return 0 if payload["passed"] else 1
    except Exception as exc:
        print(
            json.dumps({"error": str(exc)}, ensure_ascii=False, indent=2),
            file=sys.stderr,
        )
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
