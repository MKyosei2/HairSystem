from __future__ import annotations

"""HairSystem Novelty01 formal matrix policy shim for thesis revision 70.30.

The 70.29 matrix remains the implementation owner for case construction,
B0/P counterbalancing, deterministic inputs, telemetry, frozen-asset checks and
execution. 70.30 changes only evidence policy:

* use the 90-run/no-Smoke formal contract;
* performance/thermal/hardware threshold misses are outcomes, not execution
  failures;
* determinism mismatches are preserved as outcomes;
* NaN/Inf, crash/runtime failure, input mismatch and corrupted/incomplete
  evidence remain fail-closed through the unchanged 70.29 integrity validator.
"""

import copy
import sys
from pathlib import Path
from typing import Any, Mapping

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_final_run as final_run  # type: ignore
import hairgpu_novelty01_matrix as legacy  # type: ignore

FORMAL_REVISION = "70.30"
SAMPLING_POLICY = "paired-thesis-90-v1"
FATAL_POLICY = "nan-inf-crash-input-mismatch-corruption-v1"
CONTRACT_RELATIVE = Path("docs/experiments/NOVELTY01_FORMAL_CONTRACT_7030.json")

# The strict 70.29 validator reports environment stability as E_HARDWARE_*.
# Those observations remain in each result, but 70.30 does not treat them as
# evidence corruption. A machine can miss a temperature/clock/performance
# target and still produce scientifically useful comparison data.
NON_FATAL_ERROR_PREFIXES = ("E_HARDWARE_",)


def _demote_nonfatal_outcomes(validation: Mapping[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(dict(validation))
    issues_raw = result.get("issues", [])
    issues = [dict(item) for item in issues_raw if isinstance(item, Mapping)]
    demoted: list[dict[str, Any]] = []

    for item in issues:
        code = str(item.get("code", ""))
        severity = str(item.get("severity", ""))
        if severity == "error" and code.startswith(NON_FATAL_ERROR_PREFIXES):
            item["severity"] = "quality"
            item["formalOutcomeOnly"] = True
            item["originalSeverity"] = "error"
            demoted.append(dict(item))

    result["issues"] = issues
    result["formalRevision"] = FORMAL_REVISION
    result["fatalIssuePolicy"] = FATAL_POLICY
    result["nonFatalOutcomeIssues"] = demoted
    result["valid"] = not any(
        str(item.get("severity", "")) == "error" for item in issues
    )
    result["qualityGatePassed"] = bool(result["valid"]) and not any(
        str(item.get("severity", "")) == "quality" for item in issues
    )
    return result


_original_validate_outputs = final_run.validate_outputs


def _validate_outputs_7030(*args: Any, **kwargs: Any) -> dict[str, Any]:
    validation = _original_validate_outputs(*args, **kwargs)
    return _demote_nonfatal_outcomes(validation)


_original_build_matrix_summary = legacy.build_matrix_summary


def _build_matrix_summary_7030(*args: Any, **kwargs: Any) -> dict[str, Any]:
    payload = _original_build_matrix_summary(*args, **kwargs)

    # Determinism is an experimental outcome in 70.30. Preserve the observed
    # mismatches explicitly, but do not classify them as an integrity failure.
    determinism = list(payload.get("determinismIssues", []) or [])
    payload["determinismOutcomeIssues"] = determinism
    payload["determinismIssues"] = []
    payload["formalRevision"] = FORMAL_REVISION
    payload["samplingPolicy"] = SAMPLING_POLICY
    payload["fatalIssuePolicy"] = FATAL_POLICY

    rows = payload.get("results", [])
    payload["passed"] = (
        payload.get("aborted") is not True
        and isinstance(rows, list)
        and all(isinstance(row, Mapping) and row.get("ok") is True for row in rows)
        and not payload.get("scenarioInputParityIssues")
        and not payload.get("seedVariationIssues")
    )
    return payload


# Patch the already-imported module objects. legacy.execute_matrix resolves these
# globals at call time, so no copy of the large stable 70.29 implementation is
# needed.
final_run.validate_outputs = _validate_outputs_7030
legacy.FORMAL_CONTRACT_RELATIVE = CONTRACT_RELATIVE
legacy.build_matrix_summary = _build_matrix_summary_7030


if __name__ == "__main__":
    raise SystemExit(legacy.main())
