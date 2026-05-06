from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

FEATURES = [
    ("enableMicrotype", "microtype"),
    ("enableCBMA", "cbma"),
    ("enablePhysicsIslands", "physics_islands"),
    ("enableRodRepair", "rod_repair"),
    ("enableGpuResident", "gpu_resident"),
]


def bit_string(mask: int) -> str:
    return format(mask, "05b")


def make_combo_variant(mask: int) -> dict[str, Any]:
    bits = bit_string(mask)
    payload: dict[str, Any] = {
        "method": f"Combo_{bits}",
        "variant": f"Combo_{bits}",
        "feature_mask": mask,
        "feature_bits": bits,
    }
    for bit, (key, _) in enumerate(FEATURES):
        payload[key] = bool(mask & (1 << bit))
    return payload


def default_scenarios() -> list[dict[str, Any]]:
    return [
        {"scenario": "S01_StaticGroom", "category": "static", "description": "Static groom baseline viewport."},
        {"scenario": "S02_HeadMotion", "category": "motion", "description": "Moderate scalp/head animation."},
        {"scenario": "S03_WindMedium", "category": "wind", "description": "Medium wind response."},
        {"scenario": "S04_WindStrong", "category": "wind", "description": "Strong wind stress case."},
        {"scenario": "S05_ContactHeavy_Sphere", "category": "contact", "description": "Dense sphere contact."},
        {"scenario": "S06_ContactHeavy_Capsule", "category": "contact", "description": "Capsule collider stress case."},
        {"scenario": "S07_EditLocalized", "category": "edit", "description": "Localized groom edit active-set response."},
        {"scenario": "S08_EditBoundary", "category": "edit", "description": "Boundary/parting edit response."},
        {"scenario": "S09_LodFar", "category": "lod", "description": "Far camera LOD budget case."},
        {"scenario": "S10_DenseHero", "category": "dense", "description": "Dense hero groom stress case."},
    ]


def settings_hash(case: dict[str, Any]) -> str:
    stable = {
        key: case.get(key)
        for key in [
            "method",
            "variant",
            "scenario",
            "seed",
            "warmup_frames",
            "measurement_frames",
            "enableMicrotype",
            "enableCBMA",
            "enablePhysicsIslands",
            "enableRodRepair",
            "enableGpuResident",
        ]
    }
    blob = json.dumps(stable, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(blob.encode("utf-8")).hexdigest()


def make_case(
    index: int,
    variant: dict[str, Any],
    scenario: dict[str, Any],
    seed: int,
    warmup_frames: int,
    measurement_frames: int,
    asset: str,
    viewer_exe: str,
    experiment: str,
    timeout_seconds: int,
) -> dict[str, Any]:
    case_id = f"case_{index:04d}"
    method = str(variant["method"])
    scenario_code = str(scenario["scenario"])
    run_name = f"{case_id}_{method}_{scenario_code}_seed{seed}"
    case = {
        "case_id": case_id,
        "run_name": run_name,
        "experiment": experiment,
        "method": method,
        "variant": str(variant.get("variant", method)),
        "scenario": scenario_code,
        "scenario_category": scenario.get("category", ""),
        "scenario_description": scenario.get("description", ""),
        "asset": asset,
        "seed": seed,
        "warmup_frames": warmup_frames,
        "measurement_frames": measurement_frames,
        "enableMicrotype": bool(variant.get("enableMicrotype", False)),
        "enableCBMA": bool(variant.get("enableCBMA", False)),
        "enablePhysicsIslands": bool(variant.get("enablePhysicsIslands", False)),
        "enableRodRepair": bool(variant.get("enableRodRepair", False)),
        "enableGpuResident": bool(variant.get("enableGpuResident", False)),
        "feature_mask": int(variant.get("feature_mask", 0)),
        "feature_bits": str(variant.get("feature_bits", "00000")),
        "output_subdir": f"cases/{case_id}",
        "telemetry_dir": f"cases/{case_id}",
        "viewer_exe": viewer_exe,
        "timeout_seconds": timeout_seconds,
        "runtime_source": "dx11_viewer",
    }
    case["settings_hash"] = settings_hash(case)
    return case


def generate_cases(
    seeds: list[int],
    scenarios: list[dict[str, Any]],
    *,
    warmup_frames: int = 120,
    measurement_frames: int = 600,
    asset: str = "default",
    viewer_exe: str = "bin/Debug/HairSystem/HairSystem.exe",
    experiment: str = "exp_650",
    timeout_seconds: int = 180,
) -> list[dict[str, Any]]:
    cases: list[dict[str, Any]] = []
    index = 1
    for mask in range(32):
        variant = make_combo_variant(mask)
        for scenario in scenarios:
            for seed in seeds:
                cases.append(make_case(index, variant, scenario, seed, warmup_frames, measurement_frames, asset, viewer_exe, experiment, timeout_seconds))
                index += 1

    baseline_variant = {
        "method": "Baseline_CurrentProject",
        "variant": "Baseline_CurrentProject",
        "feature_mask": 0,
        "feature_bits": "baseline",
        "enableMicrotype": False,
        "enableCBMA": False,
        "enablePhysicsIslands": False,
        "enableRodRepair": False,
        "enableGpuResident": False,
    }
    baseline_seed = seeds[0] if seeds else 0
    for scenario in scenarios:
        cases.append(make_case(index, baseline_variant, scenario, baseline_seed, warmup_frames, measurement_frames, asset, viewer_exe, experiment, timeout_seconds))
        index += 1
    return cases


def write_jsonl(path: Path, cases: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for case in cases:
            handle.write(json.dumps(case, ensure_ascii=False, sort_keys=True) + "\n")


def write_suite_json(path: Path, cases: list[dict[str, Any]], *, db: str, excel: str, schema: str, outputs_dir: str, telemetry_dir: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    suite = {
        "suite_name": "exp_650_runtime_suite",
        "experiment_name": "exp_650",
        "baseline_method": "Baseline_CurrentProject",
        "schema": schema,
        "db": db,
        "excel": excel,
        "telemetry_dir": telemetry_dir,
        "outputs_dir": outputs_dir,
        "package_zip": "runtime_suite_outputs.zip",
        "runs": cases,
    }
    path.write_text(json.dumps(suite, ensure_ascii=False, indent=2), encoding="utf-8")


def parse_seeds(value: str) -> list[int]:
    seeds = [int(item.strip()) for item in value.split(",") if item.strip()]
    return seeds or [0, 1]


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate the 650-case HairSystem runtime experiment manifest.")
    parser.add_argument("--out-jsonl", default="results/exp_650/experiments_650.jsonl")
    parser.add_argument("--out-suite", default="results/exp_650/runtime_suite_650.json")
    parser.add_argument("--viewer-exe", default="../../bin/Debug/HairSystem/HairSystem.exe")
    parser.add_argument("--asset", default="default")
    parser.add_argument("--experiment", default="exp_650")
    parser.add_argument("--seeds", default="0,1")
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--measurement-frames", type=int, default=600)
    parser.add_argument("--timeout-seconds", type=int, default=180)
    parser.add_argument("--db", default="runtime_suite.sqlite")
    parser.add_argument("--excel", default="runtime_suite.xlsx")
    parser.add_argument("--schema", default="../../tools/final_stage/schema.sql")
    parser.add_argument("--outputs-dir", default="runtime_suite_outputs")
    parser.add_argument("--telemetry-dir", default=".")
    args = parser.parse_args()

    scenarios = default_scenarios()
    seeds = parse_seeds(args.seeds)
    cases = generate_cases(
        seeds,
        scenarios,
        warmup_frames=args.warmup_frames,
        measurement_frames=args.measurement_frames,
        asset=args.asset,
        viewer_exe=args.viewer_exe,
        experiment=args.experiment,
        timeout_seconds=args.timeout_seconds,
    )
    if len(cases) != 650:
        raise RuntimeError(f"Expected 650 cases, generated {len(cases)}")
    write_jsonl(Path(args.out_jsonl), cases)
    write_suite_json(
        Path(args.out_suite),
        cases,
        db=args.db,
        excel=args.excel,
        schema=args.schema,
        outputs_dir=args.outputs_dir,
        telemetry_dir=args.telemetry_dir,
    )
    print(f"Wrote {len(cases)} cases")
    print(f"JSONL: {args.out_jsonl}")
    print(f"Suite: {args.out_suite}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
