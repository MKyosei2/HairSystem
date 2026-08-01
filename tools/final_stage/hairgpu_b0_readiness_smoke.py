from __future__ import annotations

"""Run a bounded B0/S01 readiness smoke without the paper-grade gate.

This command answers one narrow question before Novelty01 implementation work:
can the current Release viewer load the canonical 100k asset, execute exactly
600 measured frames, and emit internally sane CSV/JSONL telemetry?

It deliberately does not claim formal performance evidence. The strict runtime
suite remains authoritative for paper-grade hardware, visual, availability and
quality gates.
"""

import argparse
import csv
import hashlib
import json
import math
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Iterable, Mapping

THIS_DIR = Path(__file__).resolve().parent
if str(THIS_DIR) not in sys.path:
    sys.path.insert(0, str(THIS_DIR))

import hairgpu_final_run as final_run

ASSET_NAME = "novelty01_100k.hairbin"
TOPOLOGY_NAME = "novelty01_100k_topology.bin"
SETTINGS_NAME = "generator_settings.json"
EXPECTED_STRANDS = 100000
EXPECTED_MEASURED_FRAMES = 600


class ReadinessError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ReadinessError(f"JSON object required: {path}")
    return value


def write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def run_wait(command: list[str], cwd: Path, timeout_seconds: int) -> int:
    process = subprocess.Popen(command, cwd=cwd)
    try:
        return process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as exc:
        process.kill()
        process.wait(timeout=30)
        raise ReadinessError(
            f"Process timeout after {timeout_seconds} seconds: {' '.join(command)}"
        ) from exc


def stop_stale_viewer() -> None:
    if os.name == "nt":
        subprocess.run(
            ["taskkill", "/F", "/IM", "HairSystem.exe"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )


def valid_asset_directory(path: Path) -> bool:
    required = (path / ASSET_NAME, path / TOPOLOGY_NAME, path / SETTINGS_NAME)
    return all(item.is_file() and item.stat().st_size > 0 for item in required)


def discover_asset_source(repo: Path, viewer: Path, work_root: Path) -> Path:
    out_root = repo / "out"
    candidates = sorted(
        [
            *out_root.glob("fresh_canonical_asset_waited_*"),
            *out_root.glob("fresh_canonical_asset_*"),
        ],
        key=lambda item: item.stat().st_mtime if item.exists() else 0.0,
        reverse=True,
    )

    for candidate in candidates:
        if not valid_asset_directory(candidate):
            continue
        code = run_wait(
            [str(viewer), "--validate-frozen-source", str(candidate)],
            viewer.parent,
            300,
        )
        if code == 0:
            return candidate.resolve()

    generated = work_root / "canonical_source"
    generated.mkdir(parents=True, exist_ok=True)
    export_code = run_wait(
        [str(viewer), "--export-frozen-source", str(generated)],
        viewer.parent,
        900,
    )
    if export_code != 0:
        raise ReadinessError(f"Canonical asset export failed: exit={export_code}")
    if not valid_asset_directory(generated):
        raise ReadinessError(f"Canonical asset files were not generated: {generated}")
    validate_code = run_wait(
        [str(viewer), "--validate-frozen-source", str(generated)],
        viewer.parent,
        300,
    )
    if validate_code != 0:
        raise ReadinessError(f"Canonical asset validation failed: exit={validate_code}")
    return generated.resolve()


def make_manifest(asset_dir: Path, output_path: Path, commit_sha: str) -> dict[str, Any]:
    asset = (asset_dir / ASSET_NAME).resolve()
    topology = (asset_dir / TOPOLOGY_NAME).resolve()
    settings = (asset_dir / SETTINGS_NAME).resolve()
    manifest = {
        "assetName": "Novelty01 canonical 100k readiness asset",
        "manifestVersion": "novelty01-100k-v1.6",
        "freezeStatus": "FROZEN",
        "generationMode": "viewer-canonical-export",
        "generatorSettingsSchemaVersion": "novelty01-generator-settings-v1",
        "generation": {
            "displayStrandCount": 100000,
            "primaryGuideStrandCount": 4000,
            "followersPerGuide": 24,
            "particlesPerGuide": 128,
            "solverIterations": 6,
            "groupsPerBlock": 8,
        },
        "sourceAssetPath": str(asset),
        "sourceAssetSha256": sha256_file(asset),
        "sourceAssetSizeBytes": asset.stat().st_size,
        "generatedTopologyPath": str(topology),
        "generatedTopologySha256": sha256_file(topology),
        "generatorSettingsPath": str(settings),
        "generatorSettingsSha256": sha256_file(settings),
        "assetSourceCommitSha": commit_sha,
        "sourceCommitSha": commit_sha,
        "sourceBranch": "main",
        "minimumCompatibleRuntimeVersion": 5,
        "assetFormatVersion": 1,
        "topologyFormatVersion": 1,
        "assetInternalValidation": "PASS",
        "topologyInternalValidation": "PASS",
        "roundTripValidation": "PASS",
        "_resolvedMembers": {
            "sourceAssetPath": str(asset),
            "generatedTopologyPath": str(topology),
            "generatorSettingsPath": str(settings),
        },
    }
    write_json(output_path, manifest)
    return manifest


def git_value(repo: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
        timeout=30,
    )
    if result.returncode != 0:
        raise ReadinessError(f"git {' '.join(args)} failed: {result.stderr.strip()}")
    return result.stdout.strip()


def build_case(
    repo: Path,
    run_name: str,
    manifest_path: Path,
    warmup_frames: int,
    measurement_frames: int,
) -> dict[str, Any]:
    timeline = (repo / "benchmarks/novelty01/scenarios/S01_idle.json").resolve()
    if not timeline.is_file():
        raise ReadinessError(f"S01 timeline missing: {timeline}")
    case = {
        "run_name": run_name,
        "case_id": run_name,
        "method": "B0",
        "variant": "B0",
        "scenario": "S01_Idle",
        "scenario_timeline": str(timeline),
        "asset_manifest": str(manifest_path.resolve()),
        "shader_root": str((repo / "apps/dx11_viewer/shaders").resolve()),
        "seed": 1103,
        "repetition_index": 0,
        "measurement_mode": "performance",
        "width": 1920,
        "height": 1080,
        "fixed_timestep": True,
        "fixed_timestep_seconds": 1.0 / 60.0,
        "vsync": False,
        "present_sync_interval": 0,
        "warmup_frames": warmup_frames,
        "measurement_frames": measurement_frames,
        "guide_strand_count": 4000,
        "display_strand_count": 100000,
        "particles_per_guide": 128,
        "solver_iterations": 6,
        "gpu_timestamp_delay_frames": 6,
        "timeout_seconds": 300,
        "auto_exit_after_capture": True,
        "strict_validation": True,
        "real_measurement_required": True,
        "build_configuration": "Release",
        "platform": "x64",
        "method_settings": {},
        "assertions": {
            "min_applied_events": 0,
            "min_collision_candidates": 0,
            "min_wake_requests": 0,
            "min_repairs": 0,
            "min_state_transitions": 0,
        },
        "required_telemetry_fields": [],
        "required_availability": [],
        "hardware_profile_strict": False,
        "strict_visual_validation": False,
        "minimum_process_memory_sample_coverage": 0.0,
        "settings_hash": "",
        "evaluation_phase": "readiness_smoke",
    }
    return final_run.normalize_case(case)


def parse_float(value: Any) -> float | None:
    if value in (None, "", "null", "None"):
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def parse_int(value: Any) -> int | None:
    number = parse_float(value)
    if number is None:
        return None
    return int(number)


def load_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8-sig") as handle:
        for line_number, line in enumerate(handle, start=1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ReadinessError(f"Invalid JSONL at line {line_number}: {path}") from exc
            if not isinstance(value, dict):
                raise ReadinessError(f"JSONL row is not an object at line {line_number}")
            rows.append(value)
    return rows


def find_output(output_dir: Path, run_name: str, suffixes: Iterable[str]) -> Path:
    for suffix in suffixes:
        candidate = output_dir / f"{run_name}{suffix}"
        if candidate.is_file() and candidate.stat().st_size > 0:
            return candidate
    suffix_text = ", ".join(suffixes)
    raise ReadinessError(f"Required output missing for {run_name}: {suffix_text}")


def validate_readiness_outputs(
    output_dir: Path,
    run_name: str,
    expected_frames: int = EXPECTED_MEASURED_FRAMES,
) -> dict[str, Any]:
    frames_path = find_output(output_dir, run_name, ("_frames.csv",))
    jsonl_path = find_output(
        output_dir,
        run_name,
        ("_telemetry.jsonl", "_frames.jsonl"),
    )
    csv_rows = load_csv(frames_path)
    json_rows = load_jsonl(jsonl_path)
    if len(csv_rows) != expected_frames:
        raise ReadinessError(
            f"CSV frame count mismatch: expected={expected_frames} observed={len(csv_rows)}"
        )
    if len(json_rows) != expected_frames:
        raise ReadinessError(
            f"JSONL frame count mismatch: expected={expected_frames} observed={len(json_rows)}"
        )

    required_csv = {
        "sourceFrameIndex",
        "totalStrandCount",
        "nanStateCount",
        "infiniteStateCount",
        "fatalFailure",
    }
    missing = sorted(required_csv - set(csv_rows[0]))
    if missing:
        raise ReadinessError(f"CSV required columns missing: {missing}")

    source_indices: list[int] = []
    for index, row in enumerate(csv_rows):
        source = parse_int(row.get("sourceFrameIndex"))
        strands = parse_int(row.get("totalStrandCount"))
        nan_count = parse_int(row.get("nanStateCount"))
        inf_count = parse_int(row.get("infiniteStateCount"))
        fatal = parse_int(row.get("fatalFailure"))
        if source is None:
            raise ReadinessError(f"sourceFrameIndex unavailable at row {index}")
        source_indices.append(source)
        if strands != EXPECTED_STRANDS:
            raise ReadinessError(
                f"totalStrandCount mismatch at row {index}: expected={EXPECTED_STRANDS} observed={strands}"
            )
        if nan_count != 0 or inf_count != 0 or fatal != 0:
            raise ReadinessError(
                f"Invalid runtime state at row {index}: nan={nan_count} inf={inf_count} fatal={fatal}"
            )
        for zero_field in ("syntheticMetricCount", "sameFrameReadbackCount"):
            if zero_field in row and parse_int(row.get(zero_field)) not in (None, 0):
                raise ReadinessError(f"{zero_field} must be zero at row {index}")

    if any(right < left for left, right in zip(source_indices, source_indices[1:])):
        raise ReadinessError("sourceFrameIndex is not monotonic")

    timing_candidates = (
        "frameIntervalMilliseconds",
        "renderFrameMilliseconds",
        "frameMilliseconds",
        "cpuFrameWorkMilliseconds",
    )
    selected_timing = ""
    selected_values: list[float] = []
    for field in timing_candidates:
        values = [parse_float(row.get(field)) for row in csv_rows]
        finite_positive = [value for value in values if value is not None and value > 0.0]
        if len(finite_positive) / expected_frames >= 0.95:
            selected_timing = field
            selected_values = finite_positive
            break
    if not selected_values:
        raise ReadinessError(
            "No real timing column reached 95% finite-positive coverage: "
            + ", ".join(timing_candidates)
        )

    parity_fields = (
        "sourceFrameIndex",
        "totalStrandCount",
        "nanStateCount",
        "infiniteStateCount",
        "fatalFailure",
    )
    for index, (csv_row, json_row) in enumerate(zip(csv_rows, json_rows)):
        for field in parity_fields:
            if field not in json_row:
                continue
            left = parse_int(csv_row.get(field))
            right = parse_int(json_row.get(field))
            if left != right:
                raise ReadinessError(
                    f"CSV/JSONL core parity mismatch at row {index}, field={field}: {left} != {right}"
                )

    sorted_values = sorted(selected_values)
    p95_index = max(0, min(len(sorted_values) - 1, math.ceil(len(sorted_values) * 0.95) - 1))
    return {
        "status": "PASS",
        "formalEvidence": False,
        "evidenceClass": "implementation_readiness_smoke",
        "runName": run_name,
        "csvRows": len(csv_rows),
        "jsonlRows": len(json_rows),
        "totalStrandCount": EXPECTED_STRANDS,
        "timingField": selected_timing,
        "timingCoverage": len(selected_values) / expected_frames,
        "meanTimingMilliseconds": statistics.fmean(selected_values),
        "p95TimingMilliseconds": sorted_values[p95_index],
        "framesCsv": str(frames_path),
        "telemetryJsonl": str(jsonl_path),
        "limitations": [
            "This is an implementation-readiness smoke, not paper-grade performance evidence.",
            "Strict hardware, visual, availability and statistical gates remain in the formal suite.",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--viewer-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--warmup-frames", type=int, default=120)
    parser.add_argument("--measurement-frames", type=int, default=600)
    parser.add_argument("--timeout-seconds", type=int, default=420)
    args = parser.parse_args()

    repo = args.repo_root.resolve()
    viewer = args.viewer_exe.resolve()
    output_root = args.output.resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    if not viewer.is_file():
        raise ReadinessError(f"Viewer not found: {viewer}")
    if args.measurement_frames != EXPECTED_MEASURED_FRAMES:
        raise ReadinessError(
            f"Readiness smoke is fixed to {EXPECTED_MEASURED_FRAMES} measured frames"
        )

    branch = git_value(repo, "branch", "--show-current")
    commit = git_value(repo, "rev-parse", "HEAD")
    if branch != "main":
        raise ReadinessError(f"main branch required: observed={branch}")

    stop_stale_viewer()
    asset_dir = discover_asset_source(repo, viewer, output_root)
    manifest_path = output_root / "frozen_asset_manifest.json"
    manifest = make_manifest(asset_dir, manifest_path, commit)
    run_name = f"b0_readiness_{time.strftime('%Y%m%d_%H%M%S')}"
    run_dir = output_root / "data"
    run_dir.mkdir(parents=True, exist_ok=True)
    case = build_case(
        repo,
        run_name,
        manifest_path,
        args.warmup_frames,
        args.measurement_frames,
    )
    paths = final_run.output_paths(run_dir, run_name)
    case_path = Path(paths.get("case", run_dir / f"{run_name}_case.json"))
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
    environment["HAIRGPU_CASE_JSON"] = str(case_path.resolve())
    environment["HAIRGPU_FORMAL_ASSET_PATH"] = manifest["sourceAssetPath"]
    environment["HAIRGPU_FORMAL_ASSET_SHA256"] = manifest["sourceAssetSha256"]
    environment["HAIRGPU_FORMAL_TOPOLOGY_SHA256"] = manifest["generatedTopologySha256"]
    environment["HAIRGPU_GENERATOR_SETTINGS_SHA256"] = manifest["generatorSettingsSha256"]
    environment["PYTHONHASHSEED"] = "0"
    environment["PYTHONUTF8"] = "0"

    print("=" * 68)
    print("HairSystem B0 readiness smoke")
    print(f"Commit      : {commit}")
    print(f"Asset       : {manifest['sourceAssetPath']}")
    print(f"Warm-up     : {args.warmup_frames}")
    print(f"Measurement : {args.measurement_frames}")
    print(f"Output      : {output_root}")
    print("=" * 68, flush=True)

    process = subprocess.Popen([str(viewer)], cwd=viewer.parent, env=environment)
    started = time.monotonic()
    next_progress = 0
    while process.poll() is None:
        time.sleep(2)
        elapsed = int(time.monotonic() - started)
        if elapsed >= next_progress:
            frames = run_dir / f"{run_name}_frames.csv"
            size = frames.stat().st_size if frames.is_file() else 0
            print(f"[RUNNING] elapsed={elapsed}s framesCsvBytes={size}", flush=True)
            next_progress += 15
        if elapsed > args.timeout_seconds:
            process.kill()
            process.wait(timeout=30)
            stop_stale_viewer()
            raise ReadinessError(f"Viewer timeout after {args.timeout_seconds} seconds")

    viewer_exit = process.returncode
    if viewer_exit not in (0, 1):
        raise ReadinessError(f"Viewer failed: exit={viewer_exit}")

    result = validate_readiness_outputs(run_dir, run_name, args.measurement_frames)
    result.update(
        {
            "viewerExitCode": viewer_exit,
            "branch": branch,
            "commit": commit,
            "assetDirectory": str(asset_dir),
            "assetSha256": manifest["sourceAssetSha256"],
            "output": str(output_root),
        }
    )
    result_path = output_root / "READINESS_SMOKE_RESULT.json"
    write_json(result_path, result)
    print()
    print("=" * 68)
    print("B0 READINESS SMOKE PASS")
    print("=" * 68)
    print(f"CSV rows     : {result['csvRows']}")
    print(f"JSONL rows   : {result['jsonlRows']}")
    print(f"Strands      : {result['totalStrandCount']}")
    print(f"Timing field : {result['timingField']}")
    print(f"Timing cover : {result['timingCoverage']:.3f}")
    print(f"Result       : {result_path}")
    print("=" * 68)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        stop_stale_viewer()
        print()
        print("=" * 68)
        print("B0 READINESS SMOKE FAILED")
        print("=" * 68)
        print(str(exc))
        print("=" * 68)
        raise
