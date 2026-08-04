from __future__ import annotations

"""Generate the formal Novelty 01 statistical report.

The script consumes `novelty01_matrix_summary.json`, preserves failed runs in the
failure rate, pairs P and B8 by scenario/repetition, and reports deterministic
bootstrap confidence intervals. It uses only the Python standard library.
"""

import argparse
import csv
import json
import math
import random
import statistics
from pathlib import Path
from typing import Any, Iterable

SCHEMA = "hairgpu-novelty01-formal-report-v1"
METRIC_ALIASES: dict[str, tuple[str, ...]] = {
    "averageFrameMilliseconds": (
        "averageFrameMilliseconds", "average_frame_milliseconds",
        "averageFrameMs", "frameTimeAverageMs"),
    "p95FrameMilliseconds": (
        "p95FrameMilliseconds", "p95_frame_milliseconds",
        "p95FrameMs", "frameTimeP95Ms"),
    "p99FrameMilliseconds": (
        "p99FrameMilliseconds", "p99_frame_milliseconds",
        "p99FrameMs", "frameTimeP99Ms"),
    "maximumFrameMilliseconds": (
        "maximumFrameMilliseconds", "maximum_frame_milliseconds",
        "maxFrameMs", "frameTimeMaximumMs"),
    "positionRmseMm": (
        "positionRmseMm", "position_rmse_mm", "n01PositionRmseMm"),
    "falseStaticRate": (
        "falseStaticRate", "false_static_rate", "n01FalseStaticRate"),
    "payloadActiveCount": (
        "n01PayloadActiveCount", "payloadActiveCount", "payload_active_count"),
    "groupReadBytes": (
        "n01GroupReadBytes", "groupReadBytes", "group_read_bytes"),
    "groupWriteBytes": (
        "n01GroupWriteBytes", "groupWriteBytes", "group_write_bytes"),
    "usesSgaptRepresentation": (
        "n01UsesSgaptRepresentation", "usesSgaptRepresentation"),
    "independentBaselineFrameCount": (
        "n01IndependentBaselineFrameCount", "independentBaselineFrameCount"),
    "payloadAllocatedCount": (
        "n01PayloadAllocatedCount", "payloadAllocatedCount"),
    "payloadRetiredCount": (
        "n01PayloadRetiredCount", "payloadRetiredCount"),
    "payloadReusedCount": (
        "n01PayloadReusedCount", "payloadReusedCount"),
}


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected JSON object: {path}")
    return value


def walk(value: Any) -> Iterable[tuple[str, Any]]:
    if isinstance(value, dict):
        for key, child in value.items():
            yield str(key), child
            yield from walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


def find_number(value: Any, aliases: tuple[str, ...]) -> float | None:
    alias_set = {alias.lower() for alias in aliases}
    for key, child in walk(value):
        if key.lower() not in alias_set:
            continue
        if isinstance(child, bool):
            continue
        if isinstance(child, (int, float)) and math.isfinite(float(child)):
            return float(child)
    return None


def percentile(values: list[float], q: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * q
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def bootstrap_mean_ci(
    values: list[float], *, samples: int, confidence: float, seed: int
) -> dict[str, float | int | None]:
    if not values:
        return {"n": 0, "mean": None, "lower": None, "upper": None}
    generator = random.Random(seed)
    means: list[float] = []
    for _ in range(max(samples, 1)):
        draw = [values[generator.randrange(len(values))] for _ in values]
        means.append(statistics.fmean(draw))
    alpha = (1.0 - confidence) * 0.5
    return {
        "n": len(values),
        "mean": statistics.fmean(values),
        "lower": percentile(means, alpha),
        "upper": percentile(means, 1.0 - alpha),
    }


def extract_rows(matrix: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    raw_results = matrix.get("results")
    if not isinstance(raw_results, list):
        raise ValueError("matrix summary does not contain a results list")
    for item in raw_results:
        if not isinstance(item, dict):
            continue
        row: dict[str, Any] = {
            "profile": str(matrix.get("profile", "")),
            "caseId": item.get("caseId"),
            "variant": str(item.get("variant", "")),
            "scenario": str(item.get("scenario", "")),
            "repetition": int(item.get("repetition", 0)),
            "ok": bool(item.get("ok", False)),
            "error": item.get("error"),
        }
        result = item.get("result") if isinstance(item.get("result"), dict) else {}
        for metric, aliases in METRIC_ALIASES.items():
            row[metric] = find_number(item, aliases)
        rows.append(row)
    return rows


def summarize_variant(rows: list[dict[str, Any]], variant: str) -> dict[str, Any]:
    selected = [row for row in rows if row["variant"] == variant]
    passed = [row for row in selected if row["ok"]]
    metrics: dict[str, Any] = {}
    for metric in METRIC_ALIASES:
        values = [float(row[metric]) for row in passed if row.get(metric) is not None]
        metrics[metric] = {
            "n": len(values),
            "mean": statistics.fmean(values) if values else None,
            "median": statistics.median(values) if values else None,
            "p95": percentile(values, 0.95),
            "maximum": max(values) if values else None,
        }
    return {
        "runCount": len(selected),
        "passedRunCount": len(passed),
        "failedRunCount": len(selected) - len(passed),
        "failureRate": (
            (len(selected) - len(passed)) / len(selected) if selected else None
        ),
        "metrics": metrics,
    }


def paired_differences(
    rows: list[dict[str, Any]], metric: str, left: str = "P", right: str = "B8"
) -> list[float]:
    index: dict[tuple[str, str, int], dict[str, Any]] = {}
    for row in rows:
        if row["ok"] and row.get(metric) is not None:
            index[(row["variant"], row["scenario"], row["repetition"])] = row
    differences: list[float] = []
    scenarios = sorted({row["scenario"] for row in rows})
    repetitions = sorted({row["repetition"] for row in rows})
    for scenario in scenarios:
        for repetition in repetitions:
            left_row = index.get((left, scenario, repetition))
            right_row = index.get((right, scenario, repetition))
            if left_row is None or right_row is None:
                continue
            differences.append(float(left_row[metric]) - float(right_row[metric]))
    return differences


def evaluate_gates(
        quality_rows: list[dict[str, Any]],
        performance_rows: list[dict[str, Any]]) -> dict[str, Any]:
    quality_p_rows = [row for row in quality_rows if row["variant"] == "P"]
    performance_p_rows = [row for row in performance_rows if row["variant"] == "P"]
    failures = [row for row in quality_p_rows + performance_p_rows if not row["ok"]]
    false_static = [
        float(row["falseStaticRate"])
        for row in quality_p_rows
        if row["ok"] and row.get("falseStaticRate") is not None
    ]
    p95_differences = paired_differences(
        performance_rows, "p95FrameMilliseconds")
    independent_rows = [
        row for row in performance_rows
        if row["variant"] in {"B1", "B8"} and row["ok"]
    ]
    independent_uncontaminated = bool(independent_rows) and all(
        row.get("usesSgaptRepresentation") == 0.0
        and (row.get("independentBaselineFrameCount") or 0.0) > 0.0
        and (row.get("payloadActiveCount") or 0.0) == 0.0
        and (row.get("payloadAllocatedCount") or 0.0) == 0.0
        and (row.get("payloadRetiredCount") or 0.0) == 0.0
        and (row.get("payloadReusedCount") or 0.0) == 0.0
        for row in independent_rows
    )
    return {
        "allProposalRunsPassed": (
            not failures and bool(quality_p_rows) and bool(performance_p_rows)
        ),
        "falseStaticRateAtMostPointOnePercent": (
            bool(false_static) and max(false_static) <= 0.001
        ),
        "pairedP95ImprovesB8": (
            bool(p95_differences) and statistics.fmean(p95_differences) < 0.0
        ),
        "independentBaselinesUncontaminated": independent_uncontaminated,
        "evidenceAvailable": {
            "qualityProposalRuns": len(quality_p_rows),
            "performanceProposalRuns": len(performance_p_rows),
            "falseStaticSamples": len(false_static),
            "pairedP95Samples": len(p95_differences),
            "independentBaselineRuns": len(independent_rows),
        },
    }


def write_rows(path: Path, rows: list[dict[str, Any]]) -> None:
    fieldnames = [
        "profile", "caseId", "variant", "scenario", "repetition", "ok", "error",
        *METRIC_ALIASES.keys(),
    ]
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix-summary", required=True, type=Path)
    parser.add_argument(
        "--performance-matrix-summary", type=Path,
        help="Ground-truth-free performance matrix. Defaults to --matrix-summary only for compatibility.")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--bootstrap-samples", type=int, default=10000)
    parser.add_argument("--confidence", type=float, default=0.95)
    parser.add_argument("--seed", type=int, default=20260728)
    parser.add_argument("--strict-gates", action="store_true")
    args = parser.parse_args()

    quality_matrix = load_json(args.matrix_summary.resolve())
    performance_matrix = (
        load_json(args.performance_matrix_summary.resolve())
        if args.performance_matrix_summary is not None
        else quality_matrix
    )
    quality_rows = extract_rows(quality_matrix)
    performance_rows = extract_rows(performance_matrix)
    rows = quality_rows + ([] if performance_matrix is quality_matrix else performance_rows)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    variants = sorted({row["variant"] for row in rows if row["variant"]})
    performance_metrics = {
        "averageFrameMilliseconds", "p95FrameMilliseconds",
        "p99FrameMilliseconds", "maximumFrameMilliseconds",
    }
    paired: dict[str, Any] = {}
    for metric in METRIC_ALIASES:
        metric_rows = performance_rows if metric in performance_metrics else quality_rows
        differences = paired_differences(metric_rows, metric)
        paired[metric] = {
            "definition": "P minus B8; negative favors P",
            "bootstrapMeanDifference": bootstrap_mean_ci(
                differences,
                samples=args.bootstrap_samples,
                confidence=args.confidence,
                seed=args.seed + sum(ord(char) for char in metric),
            ),
        }

    report = {
        "schema": SCHEMA,
        "source": {
            "quality": str(args.matrix_summary.resolve()),
            "performance": str(
                args.performance_matrix_summary.resolve()
                if args.performance_matrix_summary is not None
                else args.matrix_summary.resolve()),
        },
        "profile": {
            "quality": quality_matrix.get("profile"),
            "performance": performance_matrix.get("profile"),
        },
        "failedRunsRetained": True,
        "warmupExcludedByRunner": True,
        "bootstrapSamples": args.bootstrap_samples,
        "confidence": args.confidence,
        "variants": {
            variant: {
                "quality": summarize_variant(quality_rows, variant),
                "performance": summarize_variant(performance_rows, variant),
            }
            for variant in variants
        },
        "pairedPMinusB8": paired,
        "gates": evaluate_gates(quality_rows, performance_rows),
    }

    (output / "novelty01_formal_report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_rows(output / "novelty01_formal_rows.csv", rows)

    gates = report["gates"]
    passed = all(
        bool(value) for key, value in gates.items()
        if key != "evidenceAvailable"
    )
    print(json.dumps({"passed": passed, "gates": gates}, ensure_ascii=False, indent=2))
    return 0 if passed or not args.strict_gates else 2


if __name__ == "__main__":
    raise SystemExit(main())
