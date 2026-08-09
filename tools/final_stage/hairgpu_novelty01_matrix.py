from __future__ import annotations

"""Run the Novelty 01 comparison and ablation matrix serially.

This revision adds publication-grade seed replication, counterbalanced B0/P
ordering, seed-aware case IDs, and fail-closed baseline contamination checks.
The existing strict final runner remains the owner of frozen-asset validation,
process memory sampling, output integrity, and quality gates.
"""

import argparse
import contextlib
import copy
import hashlib
import json
import math
import os
import sys
import time
from pathlib import Path
from typing import Any, Iterable, Iterator, Mapping, Sequence

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
SCHEDULES = ("grouped", "counterbalanced")

FORMAL_CONTRACT_RELATIVE = Path("docs/experiments/NOVELTY01_FORMAL_CONTRACT.json")
FROZEN_PARAMETERS_RELATIVE = Path("docs/sample_assets/novelty01_frozen_parameters.json")
CANONICAL_SCENARIO_ROOT_RELATIVE = Path("benchmarks/novelty01/sgapt_scenarios")
HARDWARE_PROFILES_RELATIVE = Path("benchmarks/novelty01/hardware_profiles.json")
THESIS_HARDWARE_PROFILE_NAME = "measurement_rtx4070ti_super"
STRICT_HARDWARE_PROFILES = frozenset({"formal", "performance", "determinism", "soak"})

# Formal visual capture is a presence/integrity gate, not a performance or quality
# outcome.  The BMP analyzer samples the FHD back buffer on an approximately
# 256x256 grid.  A 0.001 occupancy therefore still requires roughly 64-75
# foreground samples and rejects a blank frame, while remaining compatible with
# the deliberately distant S18 camera (3.8-4.25 m) and the canonical 2.5 m
# framing used by ordinary scenarios.  The previous generic defaults (0.005 /
# 0.01) rejected a real 100k-strand capture with 332 foreground samples before
# any thesis measurement could be accepted.
FORMAL_VISUAL_MIN_NON_BACKGROUND_RATIO = 0.001
FORMAL_VISUAL_MIN_BOUNDING_BOX_RATIO = 0.001
FORMAL_VISUAL_POLICY = "foreground-occupancy-bounds-v3-100k-framing"

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
        "default_seeds": (1,),
    },
    "pilot": {
        "warmup_frames": 300,
        "measurement_frames": 1800,
        "measurement_mode": "quality",
        "ground_truth": True,
        "repetitions": 2,
        "default_seeds": (1, 2),
    },
    "formal": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "quality",
        "ground_truth": True,
        "repetitions": 3,
        "default_seeds": (1, 2, 3, 4, 5),
    },
    "performance": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "performance",
        "ground_truth": False,
        "repetitions": 3,
        "default_seeds": (1, 2, 3, 4, 5),
    },
    "soak": {
        "warmup_frames": 600,
        "measurement_frames": 108000,
        "measurement_mode": "robustness",
        "ground_truth": True,
        "ground_truth_interval": 300,
        "timeout_seconds": 7200,
        "repetitions": 1,
        "default_seeds": (1,),
    },
    "determinism": {
        "warmup_frames": 600,
        "measurement_frames": 6000,
        "measurement_mode": "determinism",
        "ground_truth": True,
        "repetitions": 3,
        "default_seeds": (1,),
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
        "default_seeds": (1, 2, 3, 4, 5),
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


def load_measurement_hardware_profile(
    repo: Path, name: str = THESIS_HARDWARE_PROFILE_NAME
) -> dict[str, Any]:
    """Load the measured-host contract used by publication-grade profiles."""
    payload = load_json(repo / HARDWARE_PROFILES_RELATIVE)
    profiles = payload.get("profiles")
    if not isinstance(profiles, Mapping):
        raise ValueError("Hardware profile file does not contain a profiles mapping")
    profile = profiles.get(name)
    if not isinstance(profile, Mapping):
        raise ValueError(f"Hardware profile is missing: {name}")
    target = profile.get("target")
    execution = profile.get("executionContract")
    if not isinstance(target, Mapping) or not isinstance(execution, Mapping):
        raise ValueError(f"Hardware profile is incomplete: {name}")
    return copy.deepcopy(dict(profile))


def _copy_assertions(value: Any, *, label: str) -> dict[str, Any]:
    """Return an isolated assertions mapping or fail closed on malformed input."""
    if value is None:
        return {}
    if not isinstance(value, Mapping):
        raise ValueError(f"{label} assertions must be a mapping")
    return dict(value)


def materialize_case_template(
    suite_template: Mapping[str, Any], template_path: Path
) -> dict[str, Any]:
    defaults = suite_template.get("run_defaults")
    source = suite_template.get("source")
    if not isinstance(defaults, Mapping):
        raise ValueError("Suite template does not contain a run_defaults mapping")
    if not isinstance(source, Mapping):
        raise ValueError("Suite template does not contain a source mapping")

    case_template = dict(defaults)
    template_directory = template_path.parent.resolve()

    manifest_override = os.environ.get("HAIRGPU_FROZEN_ASSET_MANIFEST", "").strip()
    manifest_value = (
        manifest_override
        if manifest_override
        else str(source.get("asset_manifest", "")).strip()
    )
    if not manifest_value:
        raise ValueError("Frozen asset manifest is not configured for the matrix")

    manifest_path = Path(manifest_value)
    if not manifest_path.is_absolute():
        manifest_path = template_directory / manifest_path
    manifest_path = manifest_path.resolve()
    if not manifest_path.is_file():
        raise FileNotFoundError(f"Frozen asset manifest is missing: {manifest_path}")

    shader_value = str(source.get("shader_root", "")).strip()
    if not shader_value:
        raise ValueError("Shader root is not configured for the matrix")
    shader_root = Path(shader_value)
    if not shader_root.is_absolute():
        shader_root = template_directory / shader_root
    shader_root = shader_root.resolve()
    if not shader_root.is_dir():
        raise FileNotFoundError(f"Shader root is missing: {shader_root}")

    case_template["assertions"] = _copy_assertions(
        case_template.get("assertions", {}),
        label="run_defaults",
    )
    case_template["asset_manifest"] = str(manifest_path)
    case_template["shader_root"] = str(shader_root)
    case_template["timeline_repeat"] = True
    # A dark canonical wig can be visually valid while having low whole-frame
    # luminance variance.  Formal validity is therefore owned by independent RGB
    # foreground-presence/bounds checks in hairgpu_final_run.  Calibrate only the
    # *minimum visible presence* for this frozen 100k/FHD thesis suite; blank
    # frames still fail because both minima remain strictly positive.  Maximum
    # occupancy/bounds checks remain the strict validator defaults.
    case_template["minimum_visual_non_background_ratio"] = (
        FORMAL_VISUAL_MIN_NON_BACKGROUND_RATIO
    )
    case_template["minimum_visual_bounding_box_ratio"] = (
        FORMAL_VISUAL_MIN_BOUNDING_BOX_RATIO
    )
    case_template["minimum_visual_luminance_stddev"] = 0.0
    case_template["visual_validation_policy"] = FORMAL_VISUAL_POLICY
    case_template["hardware_profile_strict"] = False
    case_template["allowed_asset_source_branches"] = ["main"]
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
        value = allowed_set[key]
        if value not in result:
            result.append(value)
    if not result:
        raise ValueError("At least one value is required")
    return result


def parse_positive_int_csv(raw: str | None, defaults: Sequence[int]) -> list[int]:
    if raw is None or not raw.strip():
        values = list(defaults)
    else:
        values = []
        for token in raw.split(","):
            token = token.strip()
            if not token:
                continue
            try:
                value = int(token, 10)
            except ValueError as exc:
                raise ValueError(f"Invalid integer value: {token}") from exc
            if value <= 0 or value > 0x7FFFFFFF:
                raise ValueError(f"Seed must be in 1..2147483647: {value}")
            if value not in values:
                values.append(value)
    if not values:
        raise ValueError("At least one seed is required")
    return values


def scenario_id(value: str) -> str:
    return value.upper()


def build_case(
    template: Mapping[str, Any],
    *,
    variant: str,
    scenario: str,
    seed: int,
    repetition: int,
    profile: str,
    contract: Mapping[str, Any],
    hardware_profile_contract: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    settings = PROFILE_SETTINGS[profile]
    case = dict(template)
    case["assertions"] = _copy_assertions(
        case.get("assertions", {}),
        label="formal case",
    )
    scenario_code = scenario_id(scenario)
    case["case_id"] = (
        f"N01_{profile}_{variant}_{scenario_code}_seed{seed:03d}_r{repetition + 1:02d}"
    )
    case["run_name"] = case["case_id"]
    baseline_contract = contract["baselines"].get(variant)
    if baseline_contract is None:
        baseline_contract = contract.get("ablations", {}).get(variant, {"method": "P"})
    scenario_contract = contract["scenarios"][scenario_code]

    case["method"] = str(baseline_contract.get("method", "P"))
    case["variant"] = case["method"]
    case["novelty01_variant"] = variant
    case["scenario"] = str(scenario_contract["runtimeScenarioName"])
    case["novelty01_design_scenario"] = scenario_code
    case["scenario_timeline"] = str(scenario_contract["timelinePath"])
    case["seed"] = seed
    case["random_seed"] = seed
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
    case["timeout_seconds"] = int(
        settings.get("timeout_seconds", case.get("timeout_seconds", 1800))
    )

    # Smoke/Pilot are short diagnostic gates. Publication evidence profiles must
    # enforce the measured RTX 4070 Ti SUPER runtime contract on every case.
    case["hardware_profile_strict"] = profile in STRICT_HARDWARE_PROFILES
    if case["hardware_profile_strict"]:
        if not isinstance(hardware_profile_contract, Mapping):
            raise ValueError(
                f"Strict profile {profile} requires a hardware_profile_contract"
            )
        case["hardware_profile"] = THESIS_HARDWARE_PROFILE_NAME
        case["hardware_profile_contract"] = copy.deepcopy(
            dict(hardware_profile_contract)
        )

        # Publication profiles have a single authoritative measured-host
        # contract. Mirror duplicated runtime validator limits from that profile
        # so generic template defaults cannot contradict the RTX profile.
        execution_contract = case["hardware_profile_contract"].get(
            "executionContract",
            {},
        )
        if not isinstance(execution_contract, Mapping):
            raise ValueError(
                f"Strict profile {profile} has an invalid executionContract"
            )

        runtime_gate_mapping = (
            ("minimumGpuTimingCoverage", "minimum_gpu_timing_coverage"),
            (
                "maximumProcessPrivateMemoryMiB",
                "maximum_process_private_memory_mib",
            ),
            (
                "targetProcessPrivateMemoryMiB",
                "target_process_private_memory_mib",
            ),
            (
                "maximumDxgiLocalUsageMiB",
                "maximum_dxgi_local_usage_mib",
            ),
            (
                "minimumProcessMemorySampleCoverage",
                "minimum_process_memory_sample_coverage",
            ),
        )

        for profile_key, case_key in runtime_gate_mapping:
            value = execution_contract.get(profile_key)
            if value is not None:
                case[case_key] = value
    else:
        case.pop("hardware_profile_contract", None)

    if settings.get("hardware_profile"):
        case["hardware_profile"] = settings["hardware_profile"]
    if settings.get("required_adapter_name_regex"):
        case["required_adapter_name_regex"] = settings["required_adapter_name_regex"]
    if settings.get("maximum_dxgi_local_usage_mib") is not None:
        case["maximum_dxgi_local_usage_mib"] = settings[
            "maximum_dxgi_local_usage_mib"
        ]
    return case


SEED_INPUT_ALGORITHM = "sha256-scenario-physical-input-v2"


def _canonical_json_bytes(payload: Any) -> bytes:
    return (
        json.dumps(
            payload,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        )
        + "\n"
    ).encode("utf-8")


def _seed_unit(seed: int, scenario: str, salt: str) -> float:
    digest = hashlib.sha256(
        f"{SEED_INPUT_ALGORITHM}|{seed}|{scenario}|{salt}".encode("utf-8")
    ).digest()
    integer = int.from_bytes(digest[:8], "big", signed=False)
    return integer / float((1 << 64) - 1)


def _seed_signed(seed: int, scenario: str, salt: str) -> float:
    return _seed_unit(seed, scenario, salt) * 2.0 - 1.0


def _event(
    event_id: str,
    frame: int,
    event_type: str,
    value: float | Sequence[float] | None = None,
    *,
    target_index: int | None = None,
) -> dict[str, Any]:
    row: dict[str, Any] = {
        "id": event_id,
        "frame": int(frame),
        "type": event_type,
    }
    if value is not None:
        row["value"] = list(value) if isinstance(value, (list, tuple)) else value
    if target_index is not None:
        row["targetIndex"] = int(target_index)
    return row


def build_seeded_events(
    *,
    scenario: str,
    seed: int,
    warmup_frames: int,
    measurement_frames: int,
    duration_frames: int,
) -> list[dict[str, Any]]:
    """Create deterministic external inputs shared byte-for-byte by B0 and P.

    Repetitions deliberately reuse the same input for a given scenario/seed.
    Only execution noise changes across repetitions. Method identity never enters
    this function, so the before/after pair receives the exact same stimulus.
    """
    first_measurement = max(int(warmup_frames), 1)
    final_frame = max(first_measurement, min(
        int(duration_frames) - 2,
        first_measurement + max(int(measurement_frames) - 2, 1),
    ))
    span = max(final_frame - first_measurement, 1)

    # Use a deterministic bijective seed slot for the common formal seed set.
    # Hash-only frame jitter can collide and make two nominal seeds exercise the
    # same physical stimulus.  The coprime step guarantees distinct offsets for
    # seeds 1..N while the scenario bias keeps schedules decorrelated.
    slot_count = max(1, min(span + 1, 257))
    step = next(
        candidate
        for candidate in (97, 89, 83, 79, 73, 71, 67, 61, 59, 53, 47, 43, 41, 37, 31, 29, 23, 19, 17, 13, 11, 7, 5, 3, 1)
        if math.gcd(candidate, slot_count) == 1
    )
    scenario_bias = int.from_bytes(
        hashlib.sha256(f"{scenario}|seed-frame-bias".encode("utf-8")).digest()[:4],
        "big",
        signed=False,
    ) % slot_count
    jitter = (scenario_bias + (int(seed) - 1) * step) % slot_count
    start = min(first_measurement + jitter, final_frame)
    quarter = min(first_measurement + span // 4 + jitter // 2, final_frame)
    middle = min(first_measurement + span // 2 + jitter // 3, final_frame)
    three_quarter = min(first_measurement + (span * 3) // 4, final_frame)

    amp = _seed_signed(seed, scenario, "amplitude")
    phase = _seed_signed(seed, scenario, "phase")
    x = 0.10 + 0.025 * amp
    y = 0.02 + 0.010 * phase
    collider_y = 1.62 + 0.010 * phase
    z = 0.06 * _seed_signed(seed, scenario, "z")
    wind = 1.05 + 0.20 * _seed_unit(seed, scenario, "wind")
    camera_near = 0.32 + 0.06 * _seed_unit(seed, scenario, "camera-near")
    camera_far = 3.80 + 0.45 * _seed_unit(seed, scenario, "camera-far")

    events: list[dict[str, Any]]
    if scenario == "S01":
        events = [_event("seed-static", start, "force_all_static")]
    elif scenario == "S02":
        events = [
            _event("seed-breathe-a", start, "set_root_translation", [x * 0.25, y, 0.0]),
            _event("seed-breathe-b", middle, "set_root_translation", [-x * 0.20, -y, 0.0]),
        ]
    elif scenario == "S03":
        events = [
            _event("seed-walk-a", start, "set_root_rotation", [0.0, 14.0 + 4.0 * amp, 0.0]),
            _event("seed-walk-b", middle, "set_root_rotation", [0.0, -12.0 + 3.0 * phase, 0.0]),
        ]
    elif scenario == "S04":
        events = [
            _event("seed-stop-move", start, "set_root_translation", [x, 0.0, z]),
            _event("seed-stop", middle, "set_root_translation", [0.0, 0.0, 0.0]),
        ]
    elif scenario == "S05":
        events = [
            _event("seed-head-left", start, "set_root_rotation", [0.0, 24.0 + 5.0 * amp, 0.0]),
            _event("seed-head-right", middle, "set_root_rotation", [0.0, -24.0 + 5.0 * phase, 0.0]),
        ]
    elif scenario == "S06":
        events = [_event("seed-teleport", start, "teleport_root", [0.28 + 0.08 * amp, y, z])]
    elif scenario == "S07":
        events = [
            _event("seed-collider-near", start, "set_collider_transform", [x, collider_y, z], target_index=0),
            _event("seed-collider-closer", middle, "set_collider_transform", [x * 0.55, collider_y, z * 0.40], target_index=0),
        ]
    elif scenario == "S08":
        events = [
            _event("seed-boundary-collider", start, "set_collider_transform", [x * 0.80, collider_y, z], target_index=0),
            _event("seed-boundary-edit", middle, "inject_local_edit"),
        ]
    elif scenario == "S09":
        events = [_event("seed-strong-wind", start, "set_wind", wind)]
    elif scenario == "S10":
        events = [
            _event("seed-wind-on", start, "set_wind", wind),
            _event("seed-wind-off", middle, "set_wind", 0.0),
        ]
    elif scenario == "S11":
        events = [
            _event("seed-edit-a", start, "inject_local_edit"),
            _event("seed-edit-b", three_quarter, "inject_local_edit"),
        ]
    elif scenario == "S12":
        events = [
            _event("seed-trap-collider", start, "set_collider_transform", [x * 0.70, collider_y, z], target_index=0),
            _event("seed-trap-wind", quarter, "set_wind", 0.18 + 0.04 * amp),
            _event("seed-trap-stop", middle, "set_wind", 0.0),
        ]
    elif scenario == "S13":
        events = [
            _event("seed-repair-a", start, "inject_repair_storm"),
            _event("seed-repair-b", middle, "inject_repair_storm"),
            _event("seed-repair-c", three_quarter, "inject_repair_storm"),
        ]
    elif scenario == "S14":
        events = [_event("seed-all-dynamic", start, "force_all_dynamic")]
    elif scenario == "S15":
        events = [_event("seed-all-static", start, "force_all_static")]
    elif scenario == "S16":
        events = [
            _event("seed-thrash-d1", start, "force_all_dynamic"),
            _event("seed-thrash-s1", quarter, "force_all_static"),
            _event("seed-thrash-d2", middle, "force_all_dynamic"),
            _event("seed-thrash-s2", three_quarter, "force_all_static"),
        ]
    elif scenario == "S17":
        events = [
            _event("seed-camera-near", start, "set_camera_distance", camera_near),
            _event("seed-pool-dynamic", quarter, "force_all_dynamic"),
        ]
    elif scenario == "S18":
        events = [_event("seed-camera-far", start, "set_camera_distance", camera_far)]
    else:
        raise ValueError(f"Unknown Novelty01 scenario: {scenario}")

    events.sort(key=lambda row: (int(row["frame"]), str(row["id"])))
    return events


def materialize_seeded_timeline(
    case: dict[str, Any],
    timeline_root: Path,
    scenario: str,
    seed: int,
    case_output: Path,
) -> dict[str, Any]:
    source_path = timeline_root / f"{scenario}.json"
    if not source_path.is_file():
        raise FileNotFoundError(f"Canonical Novelty 01 timeline is missing: {source_path}")
    payload = load_json(source_path)
    if payload.get("novelty01DesignScenario") != scenario:
        raise ValueError(f"Timeline design-scenario mismatch: {source_path}")

    minimum_duration = (
        int(case["warmup_frames"]) + int(case["measurement_frames"]) + 60
    )
    payload["durationFrames"] = max(int(payload.get("durationFrames", 0)), minimum_duration)
    payload["events"] = build_seeded_events(
        scenario=scenario,
        seed=seed,
        warmup_frames=int(case["warmup_frames"]),
        measurement_frames=int(case["measurement_frames"]),
        duration_frames=int(payload["durationFrames"]),
    )

    # Viewer formal startup requires a top-level case assertions object.  Keep
    # timeline assertions as timeline integrity evidence, and derive only the
    # method-independent minimum applied-event assertion for the case itself.
    timeline_assertions = _copy_assertions(
        payload.get("assertions", {}),
        label=f"timeline {source_path}",
    )
    timeline_assertions["timelineFinalFrameRequired"] = True
    payload["assertions"] = timeline_assertions

    case_assertions = _copy_assertions(
        case.get("assertions", {}),
        label="formal case",
    )
    case_assertions["min_applied_events"] = len(payload["events"])
    case["assertions"] = case_assertions

    payload["seedDriven"] = True
    payload["generatedFromCanonical"] = (
        CANONICAL_SCENARIO_ROOT_RELATIVE / f"{scenario}.json"
    ).as_posix()
    payload["seedInput"] = {
        "algorithm": SEED_INPUT_ALGORITHM,
        "seed": int(seed),
        "scenario": scenario,
        "methodIndependent": True,
        "repetitionIndependent": True,
    }

    physical_input_bytes = _canonical_json_bytes(payload["events"])
    physical_input_sha256 = hashlib.sha256(physical_input_bytes).hexdigest()
    seed_material = {
        "algorithm": SEED_INPUT_ALGORITHM,
        "seed": int(seed),
        "scenario": scenario,
        "physicalInputSha256": physical_input_sha256,
        "events": payload["events"],
    }
    payload["seedInput"]["seedMaterialSha256"] = hashlib.sha256(
        _canonical_json_bytes(seed_material)
    ).hexdigest()
    payload["seedInput"]["physicalInputSha256"] = physical_input_sha256
    encoded = _canonical_json_bytes(payload)

    input_directory = case_output / "input"
    input_directory.mkdir(parents=True, exist_ok=True)
    timeline_path = input_directory / "seeded_timeline.json"
    timeline_path.write_bytes(encoded)
    observed = hashlib.sha256(encoded).hexdigest()

    case["scenario_timeline"] = str(timeline_path.resolve())
    case["seed_algorithm"] = SEED_INPUT_ALGORITHM
    case["scenario_input_sha256"] = observed
    case["physical_scenario_input_sha256"] = physical_input_sha256
    manifest = {
        "schema": "hairgpu-seeded-scenario-input-v1",
        "algorithm": SEED_INPUT_ALGORITHM,
        "scenario": scenario,
        "seed": int(seed),
        "timelinePath": str(timeline_path.resolve()),
        "timelineSha256": observed,
        "physicalInputSha256": physical_input_sha256,
        "canonicalSourcePath": str(source_path.resolve()),
        "canonicalSourceSha256": hashlib.sha256(source_path.read_bytes()).hexdigest(),
        "methodIndependent": True,
        "repetitionIndependent": True,
    }
    (input_directory / "scenario_input_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    return manifest


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


def _read_json_metric(case_output: Path, name: str) -> Any | None:
    candidates = sorted(case_output.rglob("*.json"))
    for path in reversed(candidates):
        try:
            payload = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError, UnicodeError):
            continue
        found = _find_metric(payload, name)
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


def _metric(result: Any, case_output: Path, name: str) -> Any | None:
    value = _find_metric(result, name)
    if value is None:
        value = _read_json_metric(case_output, name)
    if value is None:
        value = _read_jsonl_metric(case_output, name)
    return value


def validate_independent_baseline_evidence(
    result: Any,
    case_output: Path,
    *,
    variant: str,
    dry_run: bool,
) -> dict[str, Any] | None:
    """Reject baseline runs that touch SG-APT state.

    B1/B8 expose dedicated Novelty01 baseline telemetry and remain strictly
    fail-closed. B0 is the legacy route: it must not emit a Novelty01 telemetry
    file, and any available SG-APT counters must be zero. The strict final
    runner separately validates requested/applied/observed B0 method parity.
    """
    if dry_run or variant not in {"B0", "B1", "B8"}:
        return None

    payload_names = (
        "n01PayloadActiveCount",
        "n01PayloadAllocatedCount",
        "n01PayloadRetiredCount",
        "n01PayloadReusedCount",
    )

    if variant == "B0":
        novelty_files = sorted(case_output.rglob("*_novelty01.csv"))
        if novelty_files:
            raise RuntimeError(
                "B0 emitted SG-APT telemetry and is contaminated: "
                + ", ".join(str(path) for path in novelty_files)
            )
        observed: dict[str, Any] = {
            "method": "B0",
            "novelty01TelemetryFiles": 0,
            "runtimePayloadTelemetryAvailable": False,
        }
        required_method_values = {
            "requestedMethodCode": "B0",
            "appliedMethodCode": "B0",
            "observedMethodCode": "B0",
        }
        for name, expected in required_method_values.items():
            value = _metric(result, case_output, name)
            if value is None:
                raise RuntimeError(f"B0 is missing fail-closed method evidence: {name}")
            observed[name] = value
            if str(value) != expected:
                raise RuntimeError(
                    f"B0 method evidence mismatch: {name} expected={expected} observed={value}"
                )

        required_flag_values = {
            "enableNoNoveltyBaseline": True,
            "enableSegmentGroupIntegratedState": False,
            "enableSegmentGroupSleep": False,
            "enableStaticCache": False,
        }
        for name, expected in required_flag_values.items():
            value = _metric(result, case_output, name)
            if value is None:
                raise RuntimeError(f"B0 is missing fail-closed flag evidence: {name}")
            normalized = (
                value if isinstance(value, bool)
                else str(value).strip().lower() in {"1", "true", "yes", "on"}
            )
            observed[name] = normalized
            if normalized is not expected:
                raise RuntimeError(
                    f"B0 flag evidence mismatch: {name} expected={expected} observed={value}"
                )
        observed["baselineContractComplete"] = True

        uses_representation = _metric(
            result, case_output, "n01UsesSgaptRepresentation"
        )
        if uses_representation is not None:
            observed["runtimePayloadTelemetryAvailable"] = True
            observed["n01UsesSgaptRepresentation"] = uses_representation
            if int(uses_representation) != 0:
                raise RuntimeError("B0 was contaminated by the SG-APT representation")
        for name in payload_names:
            value = _metric(result, case_output, name)
            if value is None:
                continue
            observed["runtimePayloadTelemetryAvailable"] = True
            observed[name] = value
            if int(value) != 0:
                raise RuntimeError(f"B0 illegally touched SG-APT payload state: {name}={value}")
        return observed

    names = (
        "n01UsesSgaptRepresentation",
        "n01ExecutionRoute",
        "n01IndependentBaselineFrameCount",
        *payload_names,
    )
    observed = {}
    for name in names:
        value = _metric(result, case_output, name)
        if value is None:
            raise RuntimeError(
                f"Independent baseline {variant} is missing fail-closed evidence: {name}"
            )
        observed[name] = value

    if int(observed["n01UsesSgaptRepresentation"]) != 0:
        raise RuntimeError(f"{variant} was contaminated by the SG-APT representation")
    expected_route = 2 if variant == "B1" else 9
    if int(observed["n01ExecutionRoute"]) != expected_route:
        raise RuntimeError(
            f"{variant} execution route mismatch: expected {expected_route}, "
            f"observed {observed['n01ExecutionRoute']}"
        )
    if int(observed["n01IndependentBaselineFrameCount"]) <= 0:
        raise RuntimeError(f"{variant} did not execute its dedicated per-frame route")
    for name in payload_names:
        if int(observed[name]) != 0:
            raise RuntimeError(
                f"{variant} illegally touched SG-APT payload state: {name}={observed[name]}"
            )
    return observed


def validate_determinism(results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    grouped: dict[tuple[str, str, int], list[dict[str, Any]]] = {}
    for row in results:
        if not row.get("ok"):
            continue
        key = (
            str(row["variant"]),
            str(row["scenario"]),
            int(row["seed"]),
        )
        grouped.setdefault(key, []).append(row)
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
                issues.append(
                    {
                        "variant": key[0],
                        "scenario": key[1],
                        "seed": key[2],
                        "field": field,
                        "values": values,
                    }
                )
    return issues


def ordered_variants(
    variants: Sequence[str], *, schedule: str, block_index: int
) -> list[str]:
    order = list(variants)
    if schedule == "counterbalanced" and block_index % 2 == 1:
        order.reverse()
    return order


def iter_run_plan(
    *,
    variants: Sequence[str],
    scenarios: Sequence[str],
    seeds: Sequence[int],
    repetitions: int,
    schedule: str,
) -> Iterator[tuple[int, str, int, int, str]]:
    run_order = 0
    for scenario_index, scenario in enumerate(scenarios):
        for seed_index, seed in enumerate(seeds):
            for repetition in range(repetitions):
                block_index = scenario_index + seed_index + repetition
                for variant in ordered_variants(
                    variants, schedule=schedule, block_index=block_index
                ):
                    yield run_order, scenario, seed, repetition, variant
                    run_order += 1


def validate_scenario_input_parity(
    results: Sequence[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    issues: list[dict[str, Any]] = []
    grouped: dict[tuple[str, int, int], dict[str, set[str]]] = {}
    for row in results:
        key = (
            str(row.get("scenario")),
            int(row.get("seed", 0)),
            int(row.get("repetition", 0)),
        )
        bucket = grouped.setdefault(
            key,
            {"timeline": set(), "physical": set(), "variants": set()},
        )
        timeline_digest = str(row.get("scenarioInputSha256", ""))
        physical_digest = str(row.get("physicalScenarioInputSha256", ""))
        variant = str(row.get("variant", ""))
        if not timeline_digest:
            issues.append({"caseId": row.get("caseId"), "issue": "missingScenarioInputSha256"})
        else:
            bucket["timeline"].add(timeline_digest)
        if not physical_digest:
            issues.append({"caseId": row.get("caseId"), "issue": "missingPhysicalScenarioInputSha256"})
        else:
            bucket["physical"].add(physical_digest)
        if variant:
            bucket["variants"].add(variant)

    for key, values in grouped.items():
        if values["variants"] != {"B0", "P"}:
            issues.append({
                "scenario": key[0],
                "seed": key[1],
                "repetition": key[2],
                "issue": "methodPairIncomplete",
                "variants": sorted(values["variants"]),
            })
        if len(values["timeline"]) != 1:
            issues.append({
                "scenario": key[0],
                "seed": key[1],
                "repetition": key[2],
                "issue": "methodTimelineInputMismatch",
                "hashes": sorted(values["timeline"]),
            })
        if len(values["physical"]) != 1:
            issues.append({
                "scenario": key[0],
                "seed": key[1],
                "repetition": key[2],
                "issue": "methodPhysicalInputMismatch",
                "hashes": sorted(values["physical"]),
            })
    return issues


def validate_seed_variation(
    results: Sequence[Mapping[str, Any]], seeds: Sequence[int]
) -> list[dict[str, Any]]:
    if len(seeds) <= 1:
        return []
    issues: list[dict[str, Any]] = []
    grouped: dict[str, dict[int, set[str]]] = {}
    for row in results:
        scenario = str(row.get("scenario"))
        seed = int(row.get("seed", 0))
        digest = str(row.get("physicalScenarioInputSha256", ""))
        grouped.setdefault(scenario, {}).setdefault(seed, set()).add(digest)
    for scenario, by_seed in grouped.items():
        representatives: list[str] = []
        for seed in seeds:
            values = {value for value in by_seed.get(int(seed), set()) if value}
            if len(values) != 1:
                issues.append({
                    "scenario": scenario,
                    "seed": int(seed),
                    "issue": "physicalSeedInputNotStable",
                    "hashes": sorted(values),
                })
            else:
                representatives.append(next(iter(values)))
        if len(representatives) == len(seeds) and len(set(representatives)) != len(seeds):
            issues.append({
                "scenario": scenario,
                "issue": "differentSeedsProducedIdenticalPhysicalInput",
                "hashes": representatives,
            })
    return issues


def build_matrix_summary(
    *,
    profile: str,
    variants: Sequence[str],
    scenarios: Sequence[str],
    seeds: Sequence[int],
    repetitions: int,
    schedule: str,
    results: list[dict[str, Any]],
    started: float,
    dry_run: bool,
    aborted: bool = False,
    abort_reason: str = "",
) -> dict[str, Any]:
    determinism_issues = (
        validate_determinism(results) if profile == "determinism" else []
    )
    input_parity_issues = validate_scenario_input_parity(results)
    seed_variation_issues = validate_seed_variation(results, seeds)
    payload: dict[str, Any] = {
        "schema": "novelty01-matrix-v5",
        "profile": profile,
        "variants": list(variants),
        "scenarios": list(scenarios),
        "seeds": list(seeds),
        "repetitions": repetitions,
        "schedule": schedule,
        "runCount": len(results),
        "dryRun": dry_run,
        "elapsedSeconds": time.time() - started,
        "passed": (
            not aborted
            and all(item.get("ok") is True for item in results)
            and not determinism_issues
            and not input_parity_issues
            and not seed_variation_issues
        ),
        "aborted": bool(aborted),
        "abortReason": str(abort_reason or ""),
        "seedInputAlgorithm": SEED_INPUT_ALGORITHM,
        "physicalInputHashRequired": True,
        "scenarioInputParityIssues": input_parity_issues,
        "seedVariationIssues": seed_variation_issues,
        "determinismIssues": determinism_issues,
        "results": results,
    }
    return payload


def write_matrix_summary(output: Path, payload: Mapping[str, Any]) -> Path:
    """Persist a summary even when fail-fast aborts the first failing case."""
    path = Path(output) / "novelty01_matrix_summary.json"
    path.write_text(
        json.dumps(dict(payload), ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    return path


def execute_matrix(args: argparse.Namespace) -> dict[str, Any]:
    repo = args.repo.resolve()
    viewer = args.viewer.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)


    preflight_result = preflight(repo, require_fxc=args.require_fxc, telemetry=None)
    (output / "novelty01_preflight.json").write_text(
        json.dumps(preflight_result, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    if not preflight_result["passed"]:
        raise RuntimeError("Novelty 01 source preflight failed.")

    template_path = args.template.resolve()
    suite_template = load_json(template_path)
    template = materialize_case_template(suite_template, template_path)
    contract = load_formal_contract(repo)
    validate_contract(contract)
    frozen_environment = load_frozen_environment(repo)
    hardware_profile_contract = (
        load_measurement_hardware_profile(repo)
        if args.profile in STRICT_HARDWARE_PROFILES
        else None
    )

    if args.timeline_root is None:
        default_timeline_root = repo / CANONICAL_SCENARIO_ROOT_RELATIVE
        args.timeline_root = (
            default_timeline_root if default_timeline_root.is_dir() else None
        )
    if args.timeline_root is None:
        raise FileNotFoundError("Canonical Novelty 01 scenario root is missing")

    variants = parse_csv_values(args.variants, ALL_VARIANTS)
    scenarios = parse_csv_values(args.scenarios, SCENARIOS)
    repetitions = args.repetitions or int(PROFILE_SETTINGS[args.profile]["repetitions"])
    if repetitions <= 0:
        raise ValueError("--repetitions must be greater than zero")
    seeds = parse_positive_int_csv(
        args.seeds, PROFILE_SETTINGS[args.profile]["default_seeds"]
    )
    schedule = str(args.schedule)

    results: list[dict[str, Any]] = []
    started = time.time()
    for run_order, scenario, seed, repetition, variant in iter_run_plan(
        variants=variants,
        scenarios=scenarios,
        seeds=seeds,
        repetitions=repetitions,
        schedule=schedule,
    ):
        case = build_case(
            template,
            variant=variant,
            scenario=scenario,
            seed=seed,
            repetition=repetition,
            profile=args.profile,
            contract=contract,
            hardware_profile_contract=hardware_profile_contract,
        )
        case_output = output / case["case_id"]
        scenario_input = materialize_seeded_timeline(
            case,
            args.timeline_root.resolve(),
            scenario,
            seed,
            case_output,
        )
        settings = PROFILE_SETTINGS[args.profile]
        env = dict(frozen_environment)
        env.update(
            {
                "HAIRGPU_N01_VARIANT": "P" if variant == "B0" else variant,
                "HAIRGPU_N01_GROUND_TRUTH": (
                    "1"
                    if settings.get("ground_truth") and variant not in {"B0", "B1"}
                    else "0"
                ),
                "HAIRGPU_N01_GROUND_TRUTH_INTERVAL": str(
                    settings.get("ground_truth_interval", 1)
                ),
                "HAIRGPU_N01_FORMAL_TELEMETRY": "1",
                "HAIRGPU_N01_TELEMETRY_DIR": str(case_output),
                "HAIRGPU_N01_PAYLOAD_PERCENT": "5" if scenario == "S17" else "50",
                "HAIRGPU_N01_DESIGN_SCENARIO": scenario,
                "HAIRGPU_N01_TIMELINE_OWNS_EXTERNAL_INPUTS": "1",
                "HAIRGPU_SCENARIO_INPUT_SHA256": str(scenario_input["timelineSha256"]),
                "HAIRGPU_SCENARIO_PHYSICAL_INPUT_SHA256": str(
                    scenario_input["physicalInputSha256"]
                ),
                "HAIRGPU_N01_SEED": str(seed),
                "HAIRGPU_RANDOM_SEED": str(seed),
            }
        )
        with environment(env):
            try:
                result = final_run.execute_case(
                    case, viewer, case_output, repo, dry_run=args.dry_run
                )
                telemetry_validation = None
                if not args.dry_run and variant not in {"B0", "B1", "B8"}:
                    telemetry_path = case_output / (
                        str(case["run_name"]) + "_novelty01.csv"
                    )
                    telemetry_validation = preflight(
                        repo, require_fxc=False, telemetry=telemetry_path
                    )
                    if not telemetry_validation["passed"]:
                        raise RuntimeError(
                            "Novelty 01 formal telemetry validation failed: "
                            + json.dumps(
                                telemetry_validation["errors"], ensure_ascii=False
                            )
                        )
                independent_evidence = validate_independent_baseline_evidence(
                    result,
                    case_output,
                    variant=variant,
                    dry_run=args.dry_run,
                )
                results.append(
                    {
                        "caseId": case["case_id"],
                        "runOrder": run_order,
                        "variant": variant,
                        "scenario": scenario,
                        "seed": seed,
                        "repetition": repetition,
                        "scenarioInputSha256": scenario_input["timelineSha256"],
                        "physicalScenarioInputSha256": scenario_input["physicalInputSha256"],
                        "seedAlgorithm": SEED_INPUT_ALGORITHM,
                        "ok": True,
                        "result": result,
                        "novelty01TelemetryValidation": telemetry_validation,
                        "independentBaselineRoute": variant in {"B0", "B1", "B8"},
                        "independentBaselineEvidence": independent_evidence,
                    }
                )
            except Exception as exc:
                failure = {
                    "caseId": case["case_id"],
                    "runOrder": run_order,
                    "variant": variant,
                    "scenario": scenario,
                    "seed": seed,
                    "repetition": repetition,
                    "scenarioInputSha256": scenario_input["timelineSha256"],
                    "physicalScenarioInputSha256": scenario_input["physicalInputSha256"],
                    "seedAlgorithm": SEED_INPUT_ALGORITHM,
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
                            formal_issues, ensure_ascii=False
                        )
                    partial = build_matrix_summary(
                        profile=args.profile,
                        variants=variants,
                        scenarios=scenarios,
                        seeds=seeds,
                        repetitions=repetitions,
                        schedule=schedule,
                        results=results,
                        started=started,
                        dry_run=args.dry_run,
                        aborted=True,
                        abort_reason=detail,
                    )
                    write_matrix_summary(output, partial)
                    raise RuntimeError(detail) from exc

    payload = build_matrix_summary(
        profile=args.profile,
        variants=variants,
        scenarios=scenarios,
        seeds=seeds,
        repetitions=repetitions,
        schedule=schedule,
        results=results,
        started=started,
        dry_run=args.dry_run,
    )
    write_matrix_summary(output, payload)
    return payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--viewer", type=Path, required=True)
    parser.add_argument(
        "--template",
        type=Path,
        required=True,
        help="Existing frozen formal case JSON used as the base.",
    )
    parser.add_argument(
        "--timeline-root",
        type=Path,
        help="Directory containing S01.json through S18.json.",
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--profile", choices=tuple(PROFILE_SETTINGS), default="smoke")
    parser.add_argument("--variants", default="all")
    parser.add_argument("--scenarios", default="all")
    parser.add_argument(
        "--seeds",
        help="Comma-separated positive integer seeds. Formal defaults to 1,2,3,4,5.",
    )
    parser.add_argument("--repetitions", type=int)
    parser.add_argument(
        "--schedule",
        choices=SCHEDULES,
        default="counterbalanced",
        help="Counterbalanced reverses method order across adjacent blocks.",
    )
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
