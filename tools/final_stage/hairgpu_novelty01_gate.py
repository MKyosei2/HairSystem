from __future__ import annotations

"""Evaluate the final Novelty 01 merge gate from generated evidence.

This command never treats a missing artifact or missing metric as zero. A strict
merge gate passes only when build, shader, source, runtime, robustness,
determinism, and hardware evidence are all present and valid.
"""

import argparse
import json
import math
import re
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Any, Iterable

SCHEMA = "hairgpu-novelty01-merge-gate-v1"


@dataclass
class GateFinding:
    severity: str
    code: str
    message: str
    path: str = ""


def load_json(path: Path, findings: list[GateFinding], code: str) -> dict[str, Any] | None:
    if not path.is_file():
        findings.append(GateFinding("error", code, "Required artifact is missing.", str(path)))
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:  # fail closed with the exact parse error
        findings.append(GateFinding("error", code, f"Invalid JSON: {exc}", str(path)))
        return None
    if not isinstance(value, dict):
        findings.append(GateFinding("error", code, "Expected a JSON object.", str(path)))
        return None
    return value


def walk(value: Any) -> Iterable[tuple[str, Any]]:
    if isinstance(value, dict):
        for key, child in value.items():
            yield str(key), child
            yield from walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


def find_numbers(value: Any, aliases: set[str]) -> list[float]:
    lowered = {item.lower() for item in aliases}
    result: list[float] = []
    for key, child in walk(value):
        if key.lower() not in lowered or isinstance(child, bool):
            continue
        if isinstance(child, (int, float)) and math.isfinite(float(child)):
            result.append(float(child))
    return result


def require_truthy(
    payload: dict[str, Any] | None,
    key: str,
    findings: list[GateFinding],
    code: str,
    path: Path,
) -> None:
    if payload is None:
        return
    if payload.get(key) is not True:
        findings.append(GateFinding("error", code, f"{key} is not true.", str(path)))


def validate_matrix(matrix: dict[str, Any] | None, findings: list[GateFinding], path: Path) -> None:
    if matrix is None:
        return
    if matrix.get("passed") is not True:
        findings.append(GateFinding("error", "E_MATRIX_FAILED", "Matrix summary did not pass.", str(path)))
    results = matrix.get("results")
    if not isinstance(results, list) or not results:
        findings.append(GateFinding("error", "E_MATRIX_EMPTY", "Matrix contains no run results.", str(path)))
        return
    required_variants = {"B0", "B1", "B8", "P"}
    seen_variants = {str(row.get("variant")) for row in results if isinstance(row, dict)}
    missing = required_variants - seen_variants
    if missing:
        findings.append(GateFinding(
            "error", "E_REQUIRED_VARIANTS", "Missing required variants: " + ", ".join(sorted(missing)), str(path)))
    failed = [str(row.get("caseId", "<unknown>")) for row in results
              if isinstance(row, dict) and not bool(row.get("ok", False))]
    if failed:
        findings.append(GateFinding(
            "error", "E_FAILED_RUNS", f"{len(failed)} matrix runs failed: " + ", ".join(failed[:20]), str(path)))

    hard_zero_metrics = {
        "invalidPayloadIndex": {
            "n01InvalidPayloadIndexCount", "invalidPayloadIndexCount",
        },
        "duplicatePayloadOwner": {
            "n01DuplicatePayloadOwnerCount", "duplicatePayloadOwnerCount",
        },
        "nan": {"nanStateCount", "n01NaNCount", "nanCount"},
        "inf": {"infiniteStateCount", "n01InfCount", "infCount"},
        "fatal": {"fatalFailure", "fatalRuntimeFailureCount"},
        "determinismMismatch": {"determinismMismatchCount"},
    }
    for label, aliases in hard_zero_metrics.items():
        values = find_numbers(matrix, aliases)
        if not values:
            findings.append(GateFinding(
                "error", "E_METRIC_UNAVAILABLE",
                f"Required hard-zero metric is unavailable: {label}", str(path)))
        elif max(values) > 0.0:
            findings.append(GateFinding(
                "error", "E_HARD_ZERO_VIOLATION",
                f"{label} must be zero; observed maximum={max(values)}", str(path)))

    # Independent B1/B8 must explicitly prove that SG-APT representation was not used.
    independent_rows = [row for row in results if isinstance(row, dict) and
                        str(row.get("variant")) in {"B1", "B8"} and bool(row.get("ok", False))]
    if not independent_rows:
        findings.append(GateFinding(
            "error", "E_INDEPENDENT_BASELINE_MISSING",
            "No successful B1/B8 independent baseline rows are present.", str(path)))
    for row in independent_rows:
        evidence_scope = row
        uses_values = find_numbers(evidence_scope, {"n01UsesSgaptRepresentation"})
        if not uses_values:
            findings.append(GateFinding(
                "error", "E_BASELINE_ROUTE_UNPROVEN",
                f"{row.get('caseId')} does not report n01UsesSgaptRepresentation.", str(path)))
        elif any(value != 0.0 for value in uses_values):
            findings.append(GateFinding(
                "error", "E_BASELINE_SGAPT_CONTAMINATION",
                f"{row.get('caseId')} reports SG-APT representation use.", str(path)))
        frame_values = find_numbers(evidence_scope, {"n01IndependentBaselineFrameCount"})
        if not frame_values or max(frame_values) <= 0.0:
            findings.append(GateFinding(
                "error", "E_BASELINE_ROUTE_UNPROVEN",
                f"{row.get('caseId')} has no executed independent baseline frames.", str(path)))
        payload_values = find_numbers(evidence_scope, {
            "n01PayloadActiveCount", "n01PayloadAllocatedCount",
            "n01PayloadRetiredCount", "n01PayloadReusedCount",
        })
        if payload_values and max(payload_values) > 0.0:
            findings.append(GateFinding(
                "error", "E_BASELINE_PAYLOAD_CONTAMINATION",
                f"{row.get('caseId')} reports SG-APT payload activity.", str(path)))



def validate_auxiliary_matrix(
    payload: dict[str, Any] | None,
    findings: list[GateFinding],
    path: Path,
    *,
    required_variants: set[str],
    required_scenarios: set[str] | None = None,
    require_determinism_clean: bool = False,
) -> None:
    if payload is None:
        return
    if payload.get("passed") is not True:
        findings.append(GateFinding(
            "error", "E_AUX_MATRIX_FAILED",
            f"Auxiliary matrix did not pass: {path.name}", str(path)))
    results = payload.get("results")
    if not isinstance(results, list) or not results:
        findings.append(GateFinding(
            "error", "E_AUX_MATRIX_EMPTY",
            f"Auxiliary matrix has no results: {path.name}", str(path)))
        return
    successful = [row for row in results if isinstance(row, dict) and row.get("ok") is True]
    variants = {str(row.get("variant")) for row in successful}
    missing_variants = required_variants - variants
    if missing_variants:
        findings.append(GateFinding(
            "error", "E_AUX_VARIANTS",
            f"{path.name} is missing successful variants: " +
            ", ".join(sorted(missing_variants)), str(path)))
    if required_scenarios:
        scenarios = {str(row.get("scenario")) for row in successful}
        missing_scenarios = required_scenarios - scenarios
        if missing_scenarios:
            findings.append(GateFinding(
                "error", "E_AUX_SCENARIOS",
                f"{path.name} is missing successful scenarios: " +
                ", ".join(sorted(missing_scenarios)), str(path)))
    if require_determinism_clean and payload.get("determinismIssues") not in ([], None):
        findings.append(GateFinding(
            "error", "E_DETERMINISM_ISSUES",
            "Determinism matrix reports mismatched hashes.", str(path)))


def validate_soak_memory(
        payload: dict[str, Any] | None, findings: list[GateFinding], path: Path) -> None:
    if payload is None:
        return
    leak_values = find_numbers(payload, {
        "n01MemoryLeakSuspectCount", "memoryLeakSuspectCount",
        "payloadLeakCount", "memoryLeakCount",
    })
    growth_mib = find_numbers(payload, {
        "memoryGrowthMiB", "privateBytesGrowthMiB",
        "processPrivateBytesGrowthMiB", "workingSetGrowthMiB",
        "dxgiLocalUsageGrowthMiB", "vramGrowthMiB",
    })
    if not leak_values and not growth_mib:
        findings.append(GateFinding(
            "error", "E_SOAK_MEMORY_UNAVAILABLE",
            "The 108,000-frame soak does not contain a leak flag or memory-growth metric.",
            str(path)))
        return
    if leak_values and max(leak_values) > 0.0:
        findings.append(GateFinding(
            "error", "E_SOAK_MEMORY_LEAK",
            f"Soak reports a memory leak; maximum count={max(leak_values)}", str(path)))
    if growth_mib and max(growth_mib) > 32.0:
        findings.append(GateFinding(
            "error", "E_SOAK_MEMORY_GROWTH",
            f"Soak memory growth exceeds 32 MiB; maximum={max(growth_mib):.3f} MiB",
            str(path)))

def validate_report(report: dict[str, Any] | None, findings: list[GateFinding], path: Path) -> None:
    if report is None:
        return
    gates = report.get("gates")
    if not isinstance(gates, dict):
        findings.append(GateFinding("error", "E_REPORT_GATES", "Report has no gates object.", str(path)))
        return
    for key in (
        "allProposalRunsPassed",
        "falseStaticRateAtMostPointOnePercent",
        "pairedP95ImprovesB8",
        "independentBaselinesUncontaminated",
    ):
        if gates.get(key) is not True:
            findings.append(GateFinding("error", "E_REPORT_GATE", f"Report gate failed: {key}", str(path)))


def validate_text_artifact(path: Path, findings: list[GateFinding], code: str) -> None:
    if not path.is_file():
        findings.append(GateFinding("error", code, "Required text artifact is missing.", str(path)))
        return
    text = path.read_text(encoding="utf-8", errors="replace")
    if not text.strip():
        findings.append(GateFinding("error", code, "Required text artifact is empty.", str(path)))
        return
    lower = text.lower()
    failure_patterns = (
        r"\bbuild\s+failed\b",
        r"\bfatal\s+error\b",
        r":\s*error\b",
        r"\b[1-9][0-9]*\s+error(?:s|\(s\))?\b",
        r"\btest(?:s)?\s+failed\b",
    )
    if any(re.search(pattern, lower) for pattern in failure_patterns):
        findings.append(GateFinding(
            "error", code, "Artifact contains a failure marker.", str(path)))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--allow-missing-gtx1660", action="store_true")
    args = parser.parse_args()

    root = args.evidence_root.resolve()
    findings: list[GateFinding] = []

    preflight_path = root / "novelty01_preflight.json"
    resource_path = root / "novelty01_resource_audit.json"
    matrix_path = root / "novelty01_matrix_summary.json"
    performance_path = root / "novelty01_performance_matrix_summary.json"
    determinism_path = root / "novelty01_determinism_summary.json"
    soak_path = root / "novelty01_soak_summary.json"
    report_path = root / "novelty01_formal_report.json"
    gtx_path = root / "novelty01_gtx1660_summary.json"

    preflight = load_json(preflight_path, findings, "E_PREFLIGHT_MISSING")
    resource = load_json(resource_path, findings, "E_RESOURCE_MISSING")
    matrix = load_json(matrix_path, findings, "E_MATRIX_MISSING")
    performance = load_json(
        performance_path, findings, "E_PERFORMANCE_MATRIX_MISSING")
    determinism = load_json(
        determinism_path, findings, "E_DETERMINISM_MATRIX_MISSING")
    soak = load_json(soak_path, findings, "E_SOAK_MATRIX_MISSING")
    report = load_json(report_path, findings, "E_REPORT_MISSING")
    gtx = None if args.allow_missing_gtx1660 else load_json(
        gtx_path, findings, "E_GTX1660_MISSING")

    require_truthy(preflight, "passed", findings, "E_PREFLIGHT_FAILED", preflight_path)
    if resource is not None and resource.get("gtx1660SixGiBBudgetPassed") is not True:
        findings.append(GateFinding(
            "error", "E_RESOURCE_BUDGET", "GTX 1660 6GiB design budget failed.", str(resource_path)))
    validate_matrix(matrix, findings, matrix_path)
    validate_auxiliary_matrix(
        performance, findings, performance_path,
        required_variants={"B0", "B1", "B8", "P"})
    validate_auxiliary_matrix(
        determinism, findings, determinism_path,
        required_variants={"P"}, require_determinism_clean=True)
    validate_auxiliary_matrix(
        soak, findings, soak_path,
        required_variants={"P"}, required_scenarios={"S15"})
    validate_soak_memory(soak, findings, soak_path)
    validate_report(report, findings, report_path)
    if gtx is not None:
        require_truthy(gtx, "passed", findings, "E_GTX1660_FAILED", gtx_path)

    validate_text_artifact(root / "release_x64_build.log", findings, "E_BUILD_LOG")
    validate_text_artifact(root / "native_self_test.log", findings, "E_NATIVE_LOG")
    fxc = load_json(root / "fxc_entrypoints.json", findings, "E_FXC_LOG")
    if fxc is not None:
        if fxc.get("passed") is not True or int(fxc.get("failureCount", 1)) != 0:
            findings.append(GateFinding(
                "error", "E_FXC_FAILED", "One or more FXC entry points failed.",
                str(root / "fxc_entrypoints.json")))

    passed = not any(item.severity == "error" for item in findings)
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "schema": SCHEMA,
        "evidenceRoot": str(root),
        "passed": passed,
        "findingCount": len(findings),
        "findings": [asdict(item) for item in findings],
    }
    output.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0 if passed else 2


if __name__ == "__main__":
    raise SystemExit(main())
