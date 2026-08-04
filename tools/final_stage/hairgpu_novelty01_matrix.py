from __future__ import annotations

"""Run the complete Novelty 01 comparison and ablation matrix serially.

The existing strict final runner remains the owner of frozen-asset validation,
process memory sampling, output integrity, and quality gates. This module adds
SG-APT variants, formal ground truth, soak, determinism, and GTX 1660 profiles.
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

FORMAL_CONTRACT_RELATIVE = Path("docs/experiments/NOVELTY01_FORMAL_CONTRACT.json")
FROZEN_PARAMETERS_RELATIVE = Path("docs/sample_assets/novelty01_frozen_parameters.json")
CANONICAL_SCENARIO_ROOT_RELATIVE = Path("benchmarks/novelty01/sgapt_scenarios")

# Compatibility names accepted by the existing Viewer scenario parser. The
# research meaning is owned by novelty01DesignScenario and the canonical Sxx JSON.
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

DETERMINISM_FIELDS = (
    "stateSequenceHash",
    "compactionSequenceHash",
    "finalPositionHash",
)

PROFILE_SETTINGS: dict[str, dict[str, Any]] = {
    "smoke": {
        "warmup_frames": 120,
        "measurement_frames": 360,
        "measurement_mode": "quality",
        "ground_truth": True,
        "repetitions": 1,
    },
    "formal": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "quality",
        "ground_truth": True,
        "repetitions": 3,
    },
    "performance": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "performance",
        "ground_truth": False,
        "repetitions": 3,
    },
    "soak": {
        "warmup_frames": 600,
        "measurement_frames": 108000,
        "measurement_mode": "robustness",
        "ground_truth": True,
        "ground_truth_interval": 300,
        "timeout_seconds": 3600,
        "repetitions": 1,
    },
    "determinism": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "determinism",
        "ground_truth": True,
        "repetitions": 3,
    },
    "gtx1660": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "performance",
        "ground_truth": False,
        "hardware_profile": "GTX1660",
        "required_adapter_name_regex": "(?i)(GTX\\s*1660|GeForce\\s*GTX\\s*1660)",
        "maximum_dxgi_local_usage_mib": 6144.0,
        "repetitions": 3,
    },
}

@contextlib.contextmanager
def environment(overrides: Mapping[str, str]):
    previous: dict[str, str | None] = {key: os.environ.get(key) for key in overrides}
    os.environ.update(overrides)
    try:
        yield
    finally:
        for key, value in previous.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value


def load_json(path: Path) -> dict[str, Any]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(payload, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return payload


def load_formal_contract(repo: Path) -> dict[str, Any]:
    contract = load_json(repo / FORMAL_CONTRACT_RELATIVE)
    if contract.get("schema") != "hairgpu-novelty01-formal-contract-v1":
        raise ValueError("Unexpected Novelty 01 formal contract schema")
    return contract


def load_frozen_environment(repo: Path) -> dict[str, str]:
    frozen = load_json(repo / FROZEN_PARAMETERS_RELATIVE)
    parameters = frozen.get("parameters")
    if not isinstance(parameters, dict):
        raise ValueError("Frozen parameter file does not contain parameters")
    environment_values: dict[str, str] = {}
    for item in parameters.values():
        if not isinstance(item, dict) or not item.get("env"):
            continue
        value = item.get("value")
        if isinstance(value, bool):
            environment_values[str(item["env"])] = "1" if value else "0"
        elif isinstance(value, (int, float)):
            environment_values[str(item["env"])] = str(value)
    return environment_values




def materialize_case_template(
        suite_template: Mapping[str, Any],
        template_path: Path) -> dict[str, Any]:
    defaults = suite_template.get("run_defaults")
    source = suite_template.get("source")
    if not isinstance(defaults, Mapping):
        raise ValueError(
            "Suite template does not contain a run_defaults mapping")
    if not isinstance(source, Mapping):
        raise ValueError(
            "Suite template does not contain a source mapping")

    case_template = dict(defaults)
    template_directory = template_path.parent.resolve()

    manifest_override = os.environ.get(
        "HAIRGPU_FROZEN_ASSET_MANIFEST", "").strip()
    manifest_value = (
        manifest_override
        if manifest_override
        else str(source.get("asset_manifest", "")).strip()
    )
    if not manifest_value:
        raise ValueError(
            "Frozen asset manifest is not configured for the matrix")

    manifest_path = Path(manifest_value)
    if not manifest_path.is_absolute():
        manifest_path = template_directory / manifest_path
    manifest_path = manifest_path.resolve()
    if not manifest_path.is_file():
        raise FileNotFoundError(
            f"Frozen asset manifest is missing: {manifest_path}")

    shader_value = str(source.get("shader_root", "")).strip()
    if not shader_value:
        raise ValueError("Shader root is not configured for the matrix")
    shader_root = Path(shader_value)
    if not shader_root.is_absolute():
        shader_root = template_directory / shader_root
    shader_root = shader_root.resolve()
    if not shader_root.is_dir():
        raise FileNotFoundError(
            f"Shader root is missing: {shader_root}")

    case_template["asset_manifest"] = str(manifest_path)
    case_template["shader_root"] = str(shader_root)
    case_template["timeline_repeat"] = True
    case_template["hardware_profile_strict"] = False
    case_template["allowed_asset_source_branches"] = [
        "Novelty01",
        "Novelty",
        "main",
    ]
    return case_template


def validate_contract(contract: Mapping[str, Any]) -> None:
    baselines = contract.get("baselines")
    scenarios = contract.get("scenarios")
    if not isinstance(baselines, dict) or set(baselines) != set(BASELINES + PROPOSAL):
        raise ValueError("Formal contract baseline set differs from B0-B8/P")
    if not isinstance(scenarios, dict) or set(scenarios) != set(SCENARIOS):
        raise ValueError("Formal contract scenario set differs from S01-S18")
    for scenario, runtime_name in SCENARIO_RUNTIME_NAMES.items():
        row = scenarios[scenario]
        if not isinstance(row, dict) or row.get("runtimeScenarioName") != runtime_name:
            raise ValueError(f"Runtime scenario contract mismatch: {scenario}")


def parse_csv_values(raw: str, allowed: Iterable[str]) -> list[str]:
    allowed_set = {item.upper(): item for item in allowed}
    if raw.strip().lower() == "all":
        return list(allowed)
    result: list[str] = []
    for token in raw.split(","):
        key = token.strip().upper()
        if not key:
            continue
        if key not in allowed_set:
            raise ValueError(f"Unknown value: {token}")
        result.append(allowed_set[key])
    return result


def scenario_id(value: str) -> str:
    # Preserve the exact Sxx code in caseId while allowing the template to own
    # its canonical descriptive scenario string/timeline path.
    return value.upper()


def build_case(template: Mapping[str, Any], *, variant: str, scenario: str,
               repetition: int, profile: str,
               contract: Mapping[str, Any]) -> dict[str, Any]:
    settings = PROFILE_SETTINGS[profile]
    case = dict(template)
    scenario_code = scenario_id(scenario)
    case["case_id"] = f"N01_{profile}_{variant}_{scenario_code}_r{repetition:02d}"
    case["run_name"] = case["case_id"]
    baseline_contract = contract["baselines"].get(variant)
    if baseline_contract is None:
        baseline_contract = contract.get("ablations", {}).get(variant, {"method": "P"})
    scenario_contract = contract["scenarios"][scenario_code]
    # B0 is the untouched legacy route. B1-B8/P/A1-A15 use the controlled
    # Proposal dispatcher with a specific representation contract.
    case["method"] = str(baseline_contract.get("method", "P"))
    case["variant"] = case["method"]
    case["novelty01_variant"] = variant
    case["scenario"] = str(scenario_contract["runtimeScenarioName"])
    case["novelty01_design_scenario"] = scenario_code
    case["scenario_timeline"] = str(scenario_contract["timelinePath"])
    case["repetition_index"] = repetition
    case["warmup_frames"] = int(settings["warmup_frames"])
    case["measurement_frames"] = int(settings["measurement_frames"])
    case["measurement_mode"] = str(settings["measurement_mode"])
    case["fixed_timestep_seconds"] = 1.0 / 60.0
    case["width"] = 1920
    case["height"] = 1080
    case["display_strand_count"] = 100000
    case["guide_strand_count"] = 4000
    case["follower_strand_count"] = 96000
    case["particles_per_guide"] = 128
    case["solver_iterations"] = 6
    case["vsync"] = False
    case["present_sync_interval"] = 0
    case["real_measurement_required"] = True
    case["timeout_seconds"] = int(settings.get("timeout_seconds", case.get("timeout_seconds", 1800)))
    if settings.get("hardware_profile"):
        case["hardware_profile"] = settings["hardware_profile"]
    if settings.get("required_adapter_name_regex"):
        case["required_adapter_name_regex"] = settings["required_adapter_name_regex"]
    if settings.get("maximum_dxgi_local_usage_mib") is not None:
        case["maximum_dxgi_local_usage_mib"] = settings["maximum_dxgi_local_usage_mib"]
    return case


def resolve_timeline(case: dict[str, Any], timeline_root: Path, scenario: str) -> None:
    path = timeline_root / f"{scenario}.json"
    if not path.is_file():
        raise FileNotFoundError(f"Canonical Novelty 01 timeline is missing: {path}")
    payload = load_json(path)
    if payload.get("novelty01DesignScenario") != scenario:
        raise ValueError(f"Timeline design-scenario mismatch: {path}")
    case["scenario_timeline"] = str(path.resolve())


def _find_metric(payload: Any, name: str) -> Any | None:
    if isinstance(payload, Mapping):
        if name in payload:
            return payload[name]
        for value in payload.values():
            found = _find_metric(value, name)
            if found is not None:
                return found
    elif isinstance(payload, list):
        for value in payload:
            found = _find_metric(value, name)
            if found is not None:
                return found
    return None


def _read_jsonl_metric(case_output: Path, name: str) -> Any | None:
    candidates = sorted(case_output.rglob("*.jsonl"))
    for path in reversed(candidates):
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except OSError:
            continue
        for raw in reversed(lines):
            if not raw.strip():
                continue
            try:
                row = json.loads(raw)
            except json.JSONDecodeError:
                continue
            found = _find_metric(row, name)
            if found is not None:
                return found
    return None


def validate_independent_baseline_evidence(
        result: Any, case_output: Path, *, variant: str, dry_run: bool) -> dict[str, Any] | None:
    if dry_run or variant not in {"B1", "B8"}:
        return None

    names = (
        "n01UsesSgaptRepresentation",
        "n01ExecutionRoute",
        "n01IndependentBaselineFrameCount",
        "n01PayloadActiveCount",
        "n01PayloadAllocatedCount",
        "n01PayloadRetiredCount",
        "n01PayloadReusedCount",
    )
    observed: dict[str, Any] = {}
    for name in names:
        value = _find_metric(result, name)
        if value is None:
            value = _read_jsonl_metric(case_output, name)
        if value is None:
            raise RuntimeError(
                f"Independent baseline {variant} is missing fail-closed evidence: {name}")
        observed[name] = value

    if int(observed["n01UsesSgaptRepresentation"]) != 0:
        raise RuntimeError(f"{variant} was contaminated by the SG-APT representation")
    expected_route = 2 if variant == "B1" else 9
    if int(observed["n01ExecutionRoute"]) != expected_route:
        raise RuntimeError(
            f"{variant} execution route mismatch: expected {expected_route}, "
            f"observed {observed['n01ExecutionRoute']}")
    if int(observed["n01IndependentBaselineFrameCount"]) <= 0:
        raise RuntimeError(f"{variant} did not execute its dedicated per-frame route")
    for name in names[3:]:
        if int(observed[name]) != 0:
            raise RuntimeError(f"{variant} illegally touched SG-APT payload state: {name}={observed[name]}")
    return observed


def validate_determinism(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for row in results:
        if not row.get("ok"):
            continue
        grouped.setdefault((str(row["variant"]), str(row["scenario"])), []).append(row)
    for key, rows in grouped.items():
        for field in DETERMINISM_FIELDS:
            values: list[Any] = []
            for row in rows:
                result = row.get("result") or {}
                aggregate = result.get("aggregate") if isinstance(result, dict) else {}
                value = aggregate.get(field) if isinstance(aggregate, dict) else None
                if value is not None:
                    values.append(value)
            if len(values) > 1 and len({str(value) for value in values}) != 1:
                issues.append({
                    "variant": key[0], "scenario": key[1],
                    "field": field, "values": values,
                })
    return issues


def execute_matrix(args: argparse.Namespace) -> dict[str, Any]:
    repo = args.repo.resolve()
    viewer = args.viewer.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    preflight_result = preflight(repo, require_fxc=args.require_fxc, telemetry=None)
    (output / "novelty01_preflight.json").write_text(
        json.dumps(preflight_result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    if not preflight_result["passed"]:
        raise RuntimeError("Novelty 01 source preflight failed.")

    template_path = args.template.resolve()
    suite_template = load_json(template_path)
    template = materialize_case_template(
        suite_template,
        template_path,
    )
    contract = load_formal_contract(repo)
    validate_contract(contract)
    frozen_environment = load_frozen_environment(repo)
    if args.timeline_root is None:
        default_timeline_root = repo / CANONICAL_SCENARIO_ROOT_RELATIVE
        args.timeline_root = default_timeline_root if default_timeline_root.is_dir() else None
    if args.timeline_root is None:
        raise FileNotFoundError("Canonical Novelty 01 scenario root is missing")
    variants = parse_csv_values(args.variants, ALL_VARIANTS)
    scenarios = parse_csv_values(args.scenarios, SCENARIOS)
    repetitions = args.repetitions or int(PROFILE_SETTINGS[args.profile]["repetitions"])

    results: list[dict[str, Any]] = []
    started = time.time()
    for variant in variants:
        for scenario in scenarios:
            for repetition in range(repetitions):
                case = build_case(
                    template, variant=variant, scenario=scenario,
                    repetition=repetition, profile=args.profile,
                    contract=contract)
                if args.timeline_root:
                    resolve_timeline(case, args.timeline_root.resolve(), scenario)
                case_output = output / case["case_id"]
                settings = PROFILE_SETTINGS[args.profile]
                env = dict(frozen_environment)
                env.update({
                    "HAIRGPU_N01_VARIANT": "P" if variant == "B0" else variant,
                    "HAIRGPU_N01_GROUND_TRUTH": "1"
                        if settings.get("ground_truth") and variant not in {"B0", "B1"}
                        else "0",
                    "HAIRGPU_N01_GROUND_TRUTH_INTERVAL": str(settings.get("ground_truth_interval", 1)),
                    "HAIRGPU_N01_FORMAL_TELEMETRY": "1",
                    "HAIRGPU_N01_TELEMETRY_DIR": str(case_output),
                    # S17 intentionally drives pool overflow. Other scenarios use
                    # the frozen default capacity so comparisons remain controlled.
                    "HAIRGPU_N01_PAYLOAD_PERCENT": "5" if scenario == "S17" else "50",
                    "HAIRGPU_N01_DESIGN_SCENARIO": scenario,
                })
                with environment(env):
                    try:
                        result = final_run.execute_case(
                            case, viewer, case_output, repo, dry_run=args.dry_run)
                        telemetry_validation = None
                        if not args.dry_run and variant not in {"B0", "B1", "B8"}:
                            telemetry_path = case_output / (
                                str(case["run_name"]) + "_novelty01.csv")
                            telemetry_validation = preflight(
                                repo, require_fxc=False, telemetry=telemetry_path)
                            if not telemetry_validation["passed"]:
                                raise RuntimeError(
                                    "Novelty 01 formal telemetry validation failed: " +
                                    json.dumps(telemetry_validation["errors"], ensure_ascii=False))
                        independent_evidence = validate_independent_baseline_evidence(
                            result, case_output, variant=variant, dry_run=args.dry_run)
                        results.append({
                            "caseId": case["case_id"],
                            "variant": variant,
                            "scenario": scenario,
                            "repetition": repetition,
                            "ok": True,
                            "result": result,
                            "novelty01TelemetryValidation": telemetry_validation,
                            "independentBaselineRoute": variant in {"B1", "B8"},
                            "independentBaselineEvidence": independent_evidence,
                        })
                    except Exception as exc:
                        failure = {
                            "caseId": case["case_id"],
                            "variant": variant,
                            "scenario": scenario,
                            "repetition": repetition,
                            "ok": False,
                            "error": str(exc),
                        }
                        formal_issues = getattr(exc, "issues", None)
                        if formal_issues:
                            failure["issues"] = formal_issues
                        results.append(failure)
                        if not args.continue_on_error:
                            detail = str(exc)
                            if formal_issues:
                                detail += ": " + json.dumps(
                                    formal_issues,
                                    ensure_ascii=False,
                                )
                            raise RuntimeError(detail) from exc

    determinism_issues = validate_determinism(results) if args.profile == "determinism" else []
    payload = {
        "schema": "novelty01-matrix-v2",
        "profile": args.profile,
        "variants": variants,
        "scenarios": scenarios,
        "repetitions": repetitions,
        "dryRun": args.dry_run,
        "elapsedSeconds": time.time() - started,
        "passed": all(item["ok"] for item in results) and not determinism_issues,
        "determinismIssues": determinism_issues,
        "results": results,
    }
    (output / "novelty01_matrix_summary.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--viewer", type=Path, required=True)
    parser.add_argument("--template", type=Path, required=True,
                        help="Existing frozen formal case JSON used as the base.")
    parser.add_argument("--timeline-root", type=Path,
                        help="Directory containing S01*.json through S18*.json.")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", choices=tuple(PROFILE_SETTINGS), default="smoke")
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
        print(json.dumps({"error": str(exc)}, ensure_ascii=False, indent=2), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
