from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable, Mapping

FRAME_BUDGET_MS = 1000.0 / 60.0
METRIC_ALIASES = {
    "frameMilliseconds": ("frameMilliseconds", "frame_ms", "frameTimeMs"),
    "gpuHairMilliseconds": (
        "gpuHairMilliseconds",
        "totalHairGpuMilliseconds",
        "gpu_hair_ms",
    ),
    "vramMegabytes": ("vramMegabytes", "vramMiB", "dxgiLocalUsageMiB"),
    "falseStaticRate": ("falseStaticRate",),
    "maxPenetrationMm": ("maxPenetrationMm",),
    "nanStateCount": ("nanStateCount",),
    "infiniteStateCount": ("infiniteStateCount",),
    "fatalFailure": ("fatalFailure",),
}


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected JSON object: {path}")
    return value


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def to_float(value: Any) -> float | None:
    try:
        number = float(value)
    except (TypeError, ValueError, OverflowError):
        return None
    return number if math.isfinite(number) else None


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * fraction
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize(values: Iterable[float], *, budget: float | None = None) -> dict[str, Any]:
    clean = [float(value) for value in values if math.isfinite(float(value))]
    if not clean:
        return {"available": False, "count": 0}
    result: dict[str, Any] = {
        "available": True,
        "count": len(clean),
        "mean": statistics.fmean(clean),
        "median": statistics.median(clean),
        "p95": percentile(clean, 0.95),
        "p99": percentile(clean, 0.99),
        "minimum": min(clean),
        "maximum": max(clean),
    }
    if budget is not None:
        result["overBudgetRate"] = sum(value > budget for value in clean) / len(clean)
    return result


def choose_value(row: Mapping[str, Any], aliases: tuple[str, ...]) -> float | None:
    for name in aliases:
        if name in row:
            value = to_float(row.get(name))
            if value is not None:
                return value
    return None


def read_case_metrics(case_directory: Path) -> dict[str, list[float]]:
    values: dict[str, list[float]] = {name: [] for name in METRIC_ALIASES}
    for path in sorted(case_directory.rglob("*.csv")):
        try:
            with path.open("r", encoding="utf-8-sig", newline="") as handle:
                reader = csv.DictReader(handle)
                if not reader.fieldnames:
                    continue
                for row in reader:
                    for metric, aliases in METRIC_ALIASES.items():
                        value = choose_value(row, aliases)
                        if value is not None:
                            values[metric].append(value)
        except (OSError, csv.Error, UnicodeError):
            continue
    return values


def collect_matrix_summaries(session: Path) -> list[tuple[Path, dict[str, Any]]]:
    summaries: list[tuple[Path, dict[str, Any]]] = []
    for path in sorted(session.rglob("novelty01_matrix_summary.json")):
        summaries.append((path, read_json(path)))
    return summaries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", type=Path, required=True)
    args = parser.parse_args()

    session = args.session.resolve()
    session_manifest = read_json(session / "formal_session.json")
    summaries = collect_matrix_summaries(session)
    if not summaries:
        raise SystemExit("No matrix summaries were found in the formal session.")

    case_rows: list[dict[str, Any]] = []
    warnings: list[str] = []
    profile_status: dict[str, Any] = {}

    for summary_path, summary in summaries:
        profile = str(summary.get("profile", summary_path.parent.name))
        profile_status[profile] = {
            "path": str(summary_path),
            "passed": bool(summary.get("passed")),
            "runCount": int(summary.get("runCount", len(summary.get("results", [])))),
            "scenarioInputParityIssues": summary.get("scenarioInputParityIssues", []),
            "seedVariationIssues": summary.get("seedVariationIssues", []),
            "determinismIssues": summary.get("determinismIssues", []),
        }
        for result in summary.get("results", []):
            if not isinstance(result, dict):
                continue
            case_id = str(result.get("caseId", ""))
            case_directory = summary_path.parent / case_id
            metrics = read_case_metrics(case_directory)
            row: dict[str, Any] = {
                "profile": profile,
                "caseId": case_id,
                "variant": str(result.get("variant", "")),
                "scenario": str(result.get("scenario", "")),
                "seed": int(result.get("seed", 0)),
                "repetition": int(result.get("repetition", 0)),
                "runOrder": int(result.get("runOrder", -1)),
                "ok": bool(result.get("ok")),
                "scenarioInputSha256": str(result.get("scenarioInputSha256", "")),
            }
            for metric, metric_values in metrics.items():
                row[metric] = summarize(
                    metric_values,
                    budget=FRAME_BUDGET_MS if metric in {"frameMilliseconds", "gpuHairMilliseconds"} else None,
                )
            if not row["frameMilliseconds"]["available"]:
                warnings.append(f"{case_id}: frameMilliseconds unavailable")
            if profile in {"performance", "gtx1660"} and not row["gpuHairMilliseconds"]["available"]:
                warnings.append(f"{case_id}: gpuHairMilliseconds unavailable")
            case_rows.append(row)

    grouped_values: dict[tuple[str, str, str, str], list[float]] = defaultdict(list)
    for row in case_rows:
        for metric in ("frameMilliseconds", "gpuHairMilliseconds", "vramMegabytes", "falseStaticRate", "maxPenetrationMm"):
            metric_summary = row[metric]
            if metric_summary.get("available"):
                grouped_values[(row["profile"], row["variant"], row["scenario"], metric)].append(
                    float(metric_summary["median"])
                )

    aggregate_rows: list[dict[str, Any]] = []
    for (profile, variant, scenario, metric), values in sorted(grouped_values.items()):
        aggregate_rows.append(
            {
                "profile": profile,
                "variant": variant,
                "scenario": scenario,
                "metric": metric,
                **summarize(values, budget=FRAME_BUDGET_MS if metric in {"frameMilliseconds", "gpuHairMilliseconds"} else None),
            }
        )

    by_pair: dict[tuple[str, str, int, int], dict[str, dict[str, Any]]] = defaultdict(dict)
    for row in case_rows:
        key = (row["profile"], row["scenario"], row["seed"], row["repetition"])
        by_pair[key][row["variant"]] = row

    paired_rows: list[dict[str, Any]] = []
    for key, variants in sorted(by_pair.items()):
        if "B0" not in variants or "P" not in variants:
            continue
        b0 = variants["B0"]
        proposal = variants["P"]
        input_match = b0["scenarioInputSha256"] == proposal["scenarioInputSha256"]
        pair: dict[str, Any] = {
            "profile": key[0],
            "scenario": key[1],
            "seed": key[2],
            "repetition": key[3],
            "scenarioInputHashMatch": input_match,
        }
        for metric in ("frameMilliseconds", "gpuHairMilliseconds"):
            b0_metric = b0[metric]
            p_metric = proposal[metric]
            if b0_metric.get("available") and p_metric.get("available"):
                b0_value = float(b0_metric["median"])
                p_value = float(p_metric["median"])
                pair[f"{metric}B0Median"] = b0_value
                pair[f"{metric}PMedian"] = p_value
                pair[f"{metric}DeltaMs"] = p_value - b0_value
                pair[f"{metric}ImprovementRate"] = (
                    (b0_value - p_value) / b0_value if b0_value > 0.0 else None
                )
        paired_rows.append(pair)

    report = {
        "schema": "hairgpu-thesis-statistical-report-v1",
        "sessionId": session_manifest.get("sessionId"),
        "formalCommit": session_manifest.get("formalCommit"),
        "frameBudgetMilliseconds": FRAME_BUDGET_MS,
        "profileStatus": profile_status,
        "caseCount": len(case_rows),
        "pairedCaseCount": len(paired_rows),
        "warnings": sorted(set(warnings)),
        "caseRows": case_rows,
        "aggregateRows": aggregate_rows,
        "pairedRows": paired_rows,
    }

    report_directory = session / "report"
    write_json(report_directory / "thesis_statistical_report.json", report)

    with (report_directory / "thesis_aggregate_table.csv").open(
        "w", encoding="utf-8-sig", newline=""
    ) as handle:
        fieldnames = [
            "profile", "variant", "scenario", "metric", "count", "mean", "median",
            "p95", "p99", "minimum", "maximum", "overBudgetRate",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(aggregate_rows)

    with (report_directory / "thesis_paired_b0_p.csv").open(
        "w", encoding="utf-8-sig", newline=""
    ) as handle:
        fieldnames = [
            "profile", "scenario", "seed", "repetition", "scenarioInputHashMatch",
            "frameMillisecondsB0Median", "frameMillisecondsPMedian",
            "frameMillisecondsDeltaMs", "frameMillisecondsImprovementRate",
            "gpuHairMillisecondsB0Median", "gpuHairMillisecondsPMedian",
            "gpuHairMillisecondsDeltaMs", "gpuHairMillisecondsImprovementRate",
        ]
        writer = csv.DictWriter(handle, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(paired_rows)

    print(json.dumps({
        "status": "PASS",
        "report": str(report_directory / "thesis_statistical_report.json"),
        "caseCount": len(case_rows),
        "pairedCaseCount": len(paired_rows),
        "warningCount": len(set(warnings)),
    }, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
