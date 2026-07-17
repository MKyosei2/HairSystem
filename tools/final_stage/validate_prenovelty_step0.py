from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from pathlib import Path
from typing import Any

EXPECTED_PRESETS = (
    "00_current",
    "01_no_readback",
    "02_no_self_collision",
    "03_low_solver",
    "04_no_debug_draw",
    "05_render_light",
    "06_simulation_light",
)


def _read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected a JSON object: {path}")
    return value


def _percentile(values: list[float], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * percentile / 100.0
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    fraction = position - lower
    if lower == upper:
        return ordered[lower]
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def _as_number(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def _find_one(directory: Path, pattern: str) -> Path | None:
    candidates = sorted(directory.glob(pattern), key=lambda path: path.stat().st_mtime)
    return candidates[-1] if candidates else None


def _validate_preset(preset_dir: Path, minimum_frames: int) -> dict[str, Any]:
    issues: list[str] = []
    frame_path = _find_one(preset_dir, "*_frames.csv")
    summary_path = _find_one(preset_dir, "*_summary.json")
    if frame_path is None:
        issues.append("frames_csv_missing")
    if summary_path is None:
        issues.append("summary_json_missing")

    values: list[float] = []
    fatal_count = 0
    nan_count = 0
    infinite_count = 0
    frame_column = ""
    if frame_path is not None:
        try:
            with frame_path.open("r", encoding="utf-8-sig", newline="") as handle:
                reader = csv.DictReader(handle)
                headers = list(reader.fieldnames or [])
                for candidate in ("frameMilliseconds", "frame_ms", "frameTimeMs"):
                    if candidate in headers:
                        frame_column = candidate
                        break
                if not frame_column:
                    issues.append("frame_time_column_missing")
                for row in reader:
                    if frame_column:
                        number = _as_number(row.get(frame_column))
                        if number is not None and number > 0.0:
                            values.append(number)
                    fatal_count += int(_as_number(row.get("fatalFailure")) or 0)
                    nan_count += int(_as_number(row.get("nanStateCount")) or 0)
                    infinite_count += int(_as_number(row.get("infiniteStateCount")) or 0)
        except Exception as exc:
            issues.append(f"frames_csv_invalid:{exc}")

    summary: dict[str, Any] = {}
    if summary_path is not None:
        try:
            summary = _read_json(summary_path)
            if summary.get("realMeasurement") is False:
                issues.append("summary_not_real_measurement")
            status = str(summary.get("validationStatus") or summary.get("status") or "").lower()
            if status in {"placeholder", "pending", "not_run", "not-executed"}:
                issues.append(f"summary_status_{status}")
        except Exception as exc:
            issues.append(f"summary_json_invalid:{exc}")

    if len(values) < minimum_frames:
        issues.append(f"insufficient_positive_frames:{len(values)}<{minimum_frames}")
    if fatal_count:
        issues.append(f"fatal_failure_count:{fatal_count}")
    if nan_count:
        issues.append(f"nan_state_count:{nan_count}")
    if infinite_count:
        issues.append(f"infinite_state_count:{infinite_count}")

    average = statistics.fmean(values) if values else None
    p95 = _percentile(values, 95.0)
    p99 = _percentile(values, 99.0)
    overrun_rate = (
        sum(value > 16.6667 for value in values) / len(values)
        if values
        else None
    )
    return {
        "preset": preset_dir.name,
        "valid": not issues,
        "issues": issues,
        "frameCsv": str(frame_path) if frame_path else "",
        "summaryJson": str(summary_path) if summary_path else "",
        "positiveFrameCount": len(values),
        "averageFrameMs": average,
        "p95FrameMs": p95,
        "p99FrameMs": p99,
        "worstFrameMs": max(values) if values else None,
        "frameBudgetOverrunRate": overrun_rate,
        "fatalFailureCount": fatal_count,
        "nanStateCount": nan_count,
        "infiniteStateCount": infinite_count,
    }


def validate_run(run_dir: Path, minimum_frames: int, label: str) -> dict[str, Any]:
    run_dir = run_dir.resolve()
    results: list[dict[str, Any]] = []
    issues: list[str] = []
    for preset in EXPECTED_PRESETS:
        preset_dir = run_dir / preset
        if not preset_dir.is_dir():
            results.append({
                "preset": preset,
                "valid": False,
                "issues": ["preset_directory_missing"],
            })
            issues.append(f"{preset}:preset_directory_missing")
            continue
        result = _validate_preset(preset_dir, minimum_frames)
        results.append(result)
        issues.extend(f"{preset}:{item}" for item in result["issues"])

    result_real_md = run_dir / "RESULT_REAL.md"
    result_real_csv = run_dir / "RESULT_REAL.csv"
    if not result_real_md.is_file():
        issues.append("RESULT_REAL.md_missing")
    if not result_real_csv.is_file():
        issues.append("RESULT_REAL.csv_missing")

    payload = {
        "schemaVersion": "prenovelty-step0-validation-v1",
        "label": label,
        "runDirectory": str(run_dir),
        "baselineMode": "B0_no_novelty_flags",
        "paperEvidenceEligible": False,
        "minimumPositiveFramesPerPreset": minimum_frames,
        "valid": not issues,
        "issues": issues,
        "presets": results,
    }
    json_path = run_dir / "PRENOVELTY_VALIDATION.json"
    md_path = run_dir / "PRENOVELTY_VALIDATION.md"
    json_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    lines = [
        "# Pre-Novelty Baseline Validation",
        "",
        f"- Label: `{label}`",
        f"- Run directory: `{run_dir}`",
        f"- Status: **{'PASS' if payload['valid'] else 'FAIL'}**",
        f"- Minimum positive frames per preset: {minimum_frames}",
        "- Evidence class: development baseline; the formal frozen-asset run is still required for paper evidence.",
        "",
        "| Preset | Status | Frames | Avg ms | p95 ms | p99 ms | Overrun |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for result in results:
        lines.append(
            "| {preset} | {status} | {frames} | {avg} | {p95} | {p99} | {overrun} |".format(
                preset=result.get("preset", ""),
                status="PASS" if result.get("valid") else "FAIL",
                frames=result.get("positiveFrameCount", 0),
                avg=(f"{result['averageFrameMs']:.4f}" if result.get("averageFrameMs") is not None else "-"),
                p95=(f"{result['p95FrameMs']:.4f}" if result.get("p95FrameMs") is not None else "-"),
                p99=(f"{result['p99FrameMs']:.4f}" if result.get("p99FrameMs") is not None else "-"),
                overrun=(f"{result['frameBudgetOverrunRate']:.4%}" if result.get("frameBudgetOverrunRate") is not None else "-"),
            )
        )
    if issues:
        lines.extend(["", "## Issues", ""])
        lines.extend(f"- {item}" for item in issues)
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return payload


def aggregate_runs(list_path: Path, output_dir: Path) -> dict[str, Any]:
    run_dirs = [
        Path(line.strip()).resolve()
        for line in list_path.read_text(encoding="utf-8-sig").splitlines()
        if line.strip()
    ]
    validations: list[dict[str, Any]] = []
    for run_dir in run_dirs:
        path = run_dir / "PRENOVELTY_VALIDATION.json"
        if path.is_file():
            validations.append(_read_json(path))
        else:
            validations.append({"runDirectory": str(run_dir), "valid": False, "issues": ["validation_json_missing"], "presets": []})

    by_preset: dict[str, list[dict[str, Any]]] = {name: [] for name in EXPECTED_PRESETS}
    for validation in validations:
        for preset in validation.get("presets", []):
            name = str(preset.get("preset", ""))
            if name in by_preset:
                by_preset[name].append(preset)

    rows: list[dict[str, Any]] = []
    for name, preset_rows in by_preset.items():
        averages = [float(row["averageFrameMs"]) for row in preset_rows if row.get("averageFrameMs") is not None]
        p99_values = [float(row["p99FrameMs"]) for row in preset_rows if row.get("p99FrameMs") is not None]
        rows.append({
            "preset": name,
            "runCount": len(preset_rows),
            "validRunCount": sum(bool(row.get("valid")) for row in preset_rows),
            "meanAverageFrameMs": statistics.fmean(averages) if averages else None,
            "meanP99FrameMs": statistics.fmean(p99_values) if p99_values else None,
        })

    payload = {
        "schemaVersion": "prenovelty-step0-session-v1",
        "baselineMode": "B0_no_novelty_flags",
        "paperEvidenceEligible": False,
        "runCount": len(validations),
        "validRunCount": sum(bool(value.get("valid")) for value in validations),
        "valid": bool(validations) and all(bool(value.get("valid")) for value in validations),
        "runs": validations,
        "presetAggregate": rows,
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "PRENOVELTY_SESSION_SUMMARY.json").write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    with (output_dir / "PRENOVELTY_SESSION_SUMMARY.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=("preset", "runCount", "validRunCount", "meanAverageFrameMs", "meanP99FrameMs"))
        writer.writeheader()
        writer.writerows(rows)
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate real Step0 output for a pre-novelty B0 baseline session.")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--run-dir")
    group.add_argument("--aggregate-list")
    parser.add_argument("--minimum-frames", type=int, default=6000)
    parser.add_argument("--label", default="pre-novelty")
    parser.add_argument("--output-dir")
    args = parser.parse_args()

    if args.run_dir:
        payload = validate_run(Path(args.run_dir), max(args.minimum_frames, 1), args.label)
    else:
        output_dir = Path(args.output_dir).resolve() if args.output_dir else Path(args.aggregate_list).resolve().parent
        payload = aggregate_runs(Path(args.aggregate_list).resolve(), output_dir)
    print(json.dumps({"valid": payload["valid"], "runCount": payload.get("runCount", 1)}, ensure_ascii=False))
    return 0 if payload["valid"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
