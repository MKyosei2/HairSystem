from __future__ import annotations

"""Bounded P/S01 SG-APT readiness smoke.

This is an implementation-readiness test, not paper-grade performance evidence.
It proves that current Main plus the Novelty01 integration can:
- load the canonical frozen 100k asset;
- execute P for 120 warm-up + exactly 600 measured frames;
- emit ordinary CSV/JSONL output;
- emit SG-APT's 64-counter telemetry;
- keep ownership/version/index/dispatch/non-finite invariants at zero;
- exercise Optional Payload, Ground Truth, and the RenderDirtyRange consumer.
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Mapping

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import hairgpu_b0_readiness_smoke as b0
import hairgpu_final_run as final_run


SAFE_ZERO_COUNTERS = {
    35: "payload_owner_mismatch",
    36: "payload_version_mismatch",
    37: "duplicate_payload_owner",
    38: "invalid_payload_index",
    47: "ground_truth_violation",
    48: "dispatch_mismatch",
    49: "shared_point_unresolved",
    58: "memory_leak_suspect",
    59: "nan",
    60: "inf",
    62: "descriptor_violation",
    63: "safe_stop",
}


class NoveltyReadinessError(RuntimeError):
    pass


def write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def build_proposal_case(
    repo: Path,
    run_name: str,
    manifest_path: Path,
    warmup_frames: int,
    measurement_frames: int,
) -> dict[str, Any]:
    case = b0.build_case(
        repo,
        run_name,
        manifest_path,
        warmup_frames,
        measurement_frames,
    )
    case["method"] = "P"
    case["variant"] = "P"
    case["novelty01_variant"] = "P"
    case["novelty01_design_scenario"] = "S01"
    case["measurement_mode"] = "quality"
    case["method_settings"] = dict(final_run.METHOD_PRESETS["P"])
    case["minimum_average_fps"] = 0.0
    case["minimum_sixty_fps_ratio"] = 0.0
    case["maximum_frame_budget_overrun_rate"] = 1.0
    case["maximum_p95_frame_ms"] = 1000.0
    case["maximum_p99_frame_ms"] = 1000.0
    case["maximum_single_frame_ms"] = 5000.0
    case["maximum_frames_over_100ms"] = measurement_frames
    case["strict_visual_validation"] = False
    case["hardware_profile_strict"] = False
    case["minimum_process_memory_sample_coverage"] = 0.0
    case["required_telemetry_fields"] = []
    case["required_availability"] = []
    case["evaluation_phase"] = "novelty01_readiness_smoke"
    case["settings_hash"] = ""
    return final_run.normalize_case(case)


def load_novelty_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        fields = set(reader.fieldnames or ())
        required = {
            "schema_version",
            "frame",
            "source_frame",
            "variant",
            "feature_mask",
            "ground_truth_samples",
            "false_static_samples",
            "false_dynamic_samples",
        } | {f"counter_{index}" for index in range(64)}
        missing = sorted(required - fields)
        if missing:
            raise NoveltyReadinessError(
                "SG-APT telemetry columns missing: " + ", ".join(missing)
            )
        rows = list(reader)
    if not rows:
        raise NoveltyReadinessError(f"SG-APT telemetry is empty: {path}")
    return rows


def int_value(row: Mapping[str, Any], key: str) -> int:
    try:
        return int(float(str(row.get(key, "0"))))
    except (TypeError, ValueError, OverflowError) as exc:
        raise NoveltyReadinessError(
            f"Invalid integer field {key}: {row.get(key)!r}"
        ) from exc


def validate_novelty_outputs(
    run_dir: Path,
    run_name: str,
    measurement_frames: int,
) -> dict[str, Any]:
    csv_path = run_dir / f"{run_name}_novelty01.csv"
    jsonl_path = run_dir / f"{run_name}_novelty01.jsonl"
    summary_path = run_dir / f"{run_name}_novelty01_summary.json"

    for path in (csv_path, jsonl_path, summary_path):
        if not path.is_file() or path.stat().st_size <= 0:
            raise NoveltyReadinessError(
                f"Required SG-APT output missing: {path}"
            )

    rows = load_novelty_csv(csv_path)
    if len(rows) < measurement_frames:
        raise NoveltyReadinessError(
            "SG-APT telemetry row count is too small: "
            f"expected_at_least={measurement_frames} observed={len(rows)}"
        )

    maximums: dict[int, int] = {}
    for counter_index, counter_name in SAFE_ZERO_COUNTERS.items():
        key = f"counter_{counter_index}"
        maximum = max(int_value(row, key) for row in rows)
        maximums[counter_index] = maximum
        if maximum != 0:
            raise NoveltyReadinessError(
                f"SG-APT invariant failed: {counter_name}={maximum}"
            )

    payload_active_max = max(
        int_value(row, "counter_13") for row in rows
    )
    payload_overflow_max = max(
        int_value(row, "counter_14") for row in rows
    )
    payload_allocated_max = max(
        int_value(row, "counter_32") for row in rows
    )
    render_dirty_ranges_max = max(
        int_value(row, "counter_42") for row in rows
    )
    render_regenerated_vertices_max = max(
        int_value(row, "counter_44") for row in rows
    )
    ground_truth_samples_max = max(
        int_value(row, "ground_truth_samples") for row in rows
    )

    if payload_active_max <= 0 and payload_allocated_max <= 0:
        raise NoveltyReadinessError(
            "Optional Dynamic Payload was never active or allocated."
        )
    if render_dirty_ranges_max <= 0:
        raise NoveltyReadinessError(
            "RenderDirtyRange was never produced."
        )
    if render_regenerated_vertices_max <= 0:
        raise NoveltyReadinessError(
            "RenderDirtyRange consumer reported no regenerated vertices."
        )
    if ground_truth_samples_max <= 0:
        raise NoveltyReadinessError(
            "Formal Ground Truth produced no samples."
        )

    summary = json.loads(summary_path.read_text(encoding="utf-8-sig"))
    if not isinstance(summary, dict):
        raise NoveltyReadinessError("SG-APT summary is not a JSON object.")

    return {
        "noveltyCsv": str(csv_path),
        "noveltyJsonl": str(jsonl_path),
        "noveltySummary": str(summary_path),
        "noveltyRows": len(rows),
        "payloadActiveMax": payload_active_max,
        "payloadOverflowMax": payload_overflow_max,
        "payloadAllocatedMax": payload_allocated_max,
        "renderDirtyRangesMax": render_dirty_ranges_max,
        "renderRegeneratedVerticesMax": render_regenerated_vertices_max,
        "groundTruthSamplesMax": ground_truth_samples_max,
        "safeCounterMaximums": {
            SAFE_ZERO_COUNTERS[index]: value
            for index, value in maximums.items()
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--viewer-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--measurement-frames", type=int, default=600)
    parser.add_argument("--timeout-seconds", type=int, default=600)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    viewer = args.viewer_exe.resolve()
    output_root = args.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    if args.measurement_frames != 600:
        raise NoveltyReadinessError(
            "Novelty01 readiness smoke is fixed to 600 measured frames."
        )
    if not viewer.is_file():
        raise NoveltyReadinessError(f"Viewer missing: {viewer}")

    branch = b0.git_value(repo, "branch", "--show-current")
    commit = b0.git_value(repo, "rev-parse", "HEAD")
    allowed_branches = {"Novelty01", "main"}
    allowed_branch_prefixes = ("integration/novelty01-safe-",)
    if (
        branch not in allowed_branches
        and not branch.startswith(allowed_branch_prefixes)
    ):
        raise NoveltyReadinessError(
            "Novelty01 or main branch required: "
            f"observed={branch}"
        )

    b0.stop_stale_viewer()
    asset_dir = b0.discover_asset_source(
        repo,
        viewer,
        output_root,
    )
    manifest_path = output_root / "frozen_asset_manifest.json"
    manifest = b0.make_manifest(
        asset_dir,
        manifest_path,
        commit,
    )

    run_name = (
        "novelty01_readiness_"
        + time.strftime("%Y%m%d_%H%M%S")
    )
    run_dir = output_root / "data"
    run_dir.mkdir(parents=True, exist_ok=True)
    case = build_proposal_case(
        repo,
        run_name,
        manifest_path,
        args.warmup_frames,
        args.measurement_frames,
    )

    paths = final_run.output_paths(run_dir, run_name)
    case_path = Path(paths["case"])
    write_json(case_path, case)

    environment = {
        key: value
        for key, value in os.environ.items()
        if not key.startswith("HAIRGPU_")
    }
    environment.update(
        {
            key: str(value)
            for key, value in final_run._build_environment(
                case,
                paths,
                repo,
                viewer,
                manifest,
            ).items()
        }
    )
    environment.update(
        {
            "HAIRGPU_CASE_JSON": str(case_path.resolve()),
            "HAIRGPU_FORMAL_ASSET_PATH": manifest["sourceAssetPath"],
            "HAIRGPU_FORMAL_ASSET_SHA256": manifest[
                "sourceAssetSha256"
            ],
            "HAIRGPU_FORMAL_TOPOLOGY_SHA256": manifest[
                "generatedTopologySha256"
            ],
            "HAIRGPU_GENERATOR_SETTINGS_SHA256": manifest[
                "generatorSettingsSha256"
            ],
            "HAIRGPU_N01_VARIANT": "P",
            "HAIRGPU_N01_DESIGN_SCENARIO": "S01",
            "HAIRGPU_N01_GROUND_TRUTH": "1",
            "HAIRGPU_N01_GROUND_TRUTH_INTERVAL": "1",
            "HAIRGPU_N01_FORMAL_TELEMETRY": "1",
            "HAIRGPU_N01_TELEMETRY_DIR": str(run_dir),
            "HAIRGPU_N01_PAYLOAD_PERCENT": "50",
            "HAIRGPU_N01_RENDER_DIRTY_CACHE": "1",
            "HAIRGPU_TELEMETRY_RUN_NAME": run_name,
            "PYTHONHASHSEED": "0",
            "PYTHONUTF8": "0",
        }
    )

    print("=" * 72)
    print("HairSystem Novelty01 P/S01 readiness smoke")
    print(f"Commit      : {commit}")
    print(f"Asset       : {manifest['sourceAssetPath']}")
    print(f"Warm-up     : {args.warmup_frames}")
    print(f"Measurement : {args.measurement_frames}")
    print(f"Output      : {output_root}")
    print("=" * 72, flush=True)

    process = subprocess.Popen(
        [str(viewer)],
        cwd=viewer.parent,
        env=environment,
    )
    started = time.monotonic()
    next_progress = 0
    while process.poll() is None:
        time.sleep(2)
        elapsed = int(time.monotonic() - started)
        if elapsed >= next_progress:
            frames = run_dir / f"{run_name}_frames.csv"
            size = frames.stat().st_size if frames.is_file() else 0
            print(
                f"[RUNNING] elapsed={elapsed}s framesCsvBytes={size}",
                flush=True,
            )
            next_progress += 15
        if elapsed > args.timeout_seconds:
            process.kill()
            process.wait(timeout=30)
            b0.stop_stale_viewer()
            raise NoveltyReadinessError(
                f"Viewer timeout after {args.timeout_seconds} seconds"
            )

    viewer_exit = process.returncode
    if viewer_exit not in (0, 1):
        raise NoveltyReadinessError(
            f"Viewer failed: exit={viewer_exit}"
        )

    ordinary = b0.validate_readiness_outputs(
        run_dir,
        run_name,
        args.measurement_frames,
    )

    frames_path = Path(ordinary["framesCsv"])
    with frames_path.open("r", encoding="utf-8-sig", newline="") as handle:
        frame_rows = list(csv.DictReader(handle))
    for row_index, row in enumerate(frame_rows):
        for field in ("fullCpuGroupUploadCount", "sameFrameReadbackCount"):
            if field in row and int(float(row[field] or 0)) != 0:
                raise NoveltyReadinessError(
                    f"{field} must be zero at row {row_index}"
                )

    novelty = validate_novelty_outputs(
        run_dir,
        run_name,
        args.measurement_frames,
    )

    result = {
        **ordinary,
        **novelty,
        "viewerExitCode": viewer_exit,
        "branch": branch,
        "commit": commit,
        "method": "P",
        "scenario": "S01_Idle",
        "assetDirectory": str(asset_dir),
        "assetSha256": manifest["sourceAssetSha256"],
        "output": str(output_root),
        "formalEvidence": False,
        "evidenceClass": "novelty01_implementation_readiness_smoke",
        "limitations": [
            "This proves execution and invariant readiness only.",
            "Paper-grade B0/B6/B8/P repetitions and hardware gates remain separate.",
        ],
    }
    result_path = output_root / "NOVELTY01_READINESS_RESULT.json"
    write_json(result_path, result)

    print()
    print("=" * 72)
    print("NOVELTY01 READINESS SMOKE PASS")
    print("=" * 72)
    print(f"CSV rows                : {ordinary['csvRows']}")
    print(f"JSONL rows              : {ordinary['jsonlRows']}")
    print(f"SG-APT telemetry rows   : {novelty['noveltyRows']}")
    print(f"Payload active max      : {novelty['payloadActiveMax']}")
    print(
        "Render regenerated max : "
        f"{novelty['renderRegeneratedVerticesMax']}"
    )
    print(
        "Ground truth samples   : "
        f"{novelty['groundTruthSamplesMax']}"
    )
    print(f"Result                  : {result_path}")
    print("=" * 72)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        b0.stop_stale_viewer()
        print()
        print("=" * 72)
        print("NOVELTY01 READINESS SMOKE FAILED")
        print("=" * 72)
        print(str(exc))
        print("=" * 72)
        raise
