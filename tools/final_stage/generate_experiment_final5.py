from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

FEATURES = [
    ("enableMicrotype", "microtype"),
    ("enableBudgetedPhysics", "budgeted_physics"),
    ("enableCertificateRender", "certificate_render"),
    ("enablePerStrandEdit", "per_strand_edit"),
    ("enableGpuResident", "gpu_resident"),
]

DEFAULT_SCENARIOS = [
    "S01_StaticGroom", "S02_HeadMotion", "S03_WindMedium", "S04_WindStrong",
    "S05_ContactHeavy_Sphere", "S06_ContactHeavy_Capsule", "S07_EditLocalized",
    "S08_EditBoundary", "S09_LodFar", "S10_DenseHero",
]


def settings_hash(payload: dict[str, Any]) -> str:
    clean = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(clean.encode("utf-8")).hexdigest()[:16]


def make_case(mask: int, scenario: str, seed: int, *, warmup: int, measurement: int, viewer_exe: str, telemetry_dir: str) -> dict[str, Any]:
    method = f"Combo_{mask:05b}"
    flags = {name: bool(mask & (1 << (4 - i))) for i, (name, _slug) in enumerate(FEATURES)}
    case = {
        "case_id": f"final5_{method}_{scenario}_seed{seed}",
        "run_name": f"final5_{method}_{scenario}_seed{seed}",
        "experiment": "exp_final5",
        "method": method,
        "variant": method,
        "scenario": scenario,
        "asset": "Sample Hybrid",
        "seed": seed,
        "warmup_frames": warmup,
        "measurement_frames": measurement,
        "viewer_exe": viewer_exe,
        "telemetry_dir": f"{telemetry_dir}/{method}/{scenario}/seed{seed}",
        "phase": "phase1_pre_novelty_final_adjustment",
        "novelty_mapping_version": "final5_v1",
        "top_level_novelty_mask": mask,
        "top_level_novelty_bits": f"{mask:05b}",
        "enableNoNoveltyBaseline": False,
        "enableCBMA": False,
        "enablePhysicsIslands": False,
        "enableRodRepair": False,
        **flags,
    }
    case["settings_hash"] = settings_hash(case)
    return case


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Phase1 final5 manifest without modifying exp_650.")
    parser.add_argument("--out-jsonl", default="results/exp_final5/experiments_final5.jsonl")
    parser.add_argument("--out-suite", default="results/exp_final5/runtime_suite_final5.json")
    parser.add_argument("--viewer-exe", default="../../bin/Debug/HairSystem/HairSystem.exe")
    parser.add_argument("--telemetry-dir", default="results/exp_final5/telemetry")
    parser.add_argument("--seeds", default="0")
    parser.add_argument("--scenarios", default=",".join(DEFAULT_SCENARIOS))
    parser.add_argument("--warmup", type=int, default=120)
    parser.add_argument("--measurement", type=int, default=600)
    parser.add_argument("--full", action="store_true", help="Generate all 32 final5 combos. Default emits only Combo_00000 and Combo_11111.")
    args = parser.parse_args()

    seeds = [int(v.strip()) for v in args.seeds.split(",") if v.strip()]
    scenarios = [v.strip() for v in args.scenarios.split(",") if v.strip()]
    masks = list(range(32)) if args.full else [0, 31]
    cases = [
        make_case(mask, scenario, seed, warmup=args.warmup, measurement=args.measurement, viewer_exe=args.viewer_exe, telemetry_dir=args.telemetry_dir)
        for mask in masks for scenario in scenarios for seed in seeds
    ]

    out_jsonl = Path(args.out_jsonl)
    out_jsonl.parent.mkdir(parents=True, exist_ok=True)
    out_jsonl.write_text("\n".join(json.dumps(c, ensure_ascii=False, sort_keys=True) for c in cases) + "\n", encoding="utf-8")

    suite = {
        "suite_name": "exp_final5_runtime_suite",
        "experiment_name": "exp_final5",
        "phase": "phase1_pre_novelty_final_adjustment",
        "novelty_mapping_version": "final5_v1",
        "feature_axes": [{"index": i + 1, "flag": name, "slug": slug} for i, (name, slug) in enumerate(FEATURES)],
        "smoke_only": not args.full,
        "required_smoke_methods": ["Combo_00000", "Combo_11111"],
        "runs_jsonl": str(out_jsonl),
        "runs": cases,
    }
    out_suite = Path(args.out_suite)
    out_suite.parent.mkdir(parents=True, exist_ok=True)
    out_suite.write_text(json.dumps(suite, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Wrote {len(cases)} final5 cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
