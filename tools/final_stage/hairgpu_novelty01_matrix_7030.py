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

Important import detail:
``hairgpu_final_run`` is a compatibility package which re-exports functions from
its sibling ``hairgpu_final_run.py`` implementation module. ``execute_case``
therefore resolves globals from that hidden implementation module, not from the
facade module. Any validation-policy override must patch the exact module globals
owned by ``execute_case``. Merely assigning ``hairgpu_final_run.validate_outputs``
on the facade does not affect the production execution path.
"""

import copy
import math
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

# Only measured thermal/clock *outcomes* are non-fatal. Hardware identity,
# power-plan/AC requirements, missing telemetry, malformed stability windows and
# other environment/integrity failures remain fatal. An unavailable observation
# (None/NaN/Inf) is never demoted.
NON_FATAL_HARDWARE_OUTCOME_CODES = frozenset(
    {
        "E_HARDWARE_GPU_TEMPERATURE",
        "E_HARDWARE_GPU_TEMPERATURE_SPREAD",
        "E_HARDWARE_GPU_CLOCK_DROP",
        "E_HARDWARE_GPU_TEMPERATURE_ENDPOINT",
    }
)


def _has_finite_observation(item: Mapping[str, Any]) -> bool:
    observed = item.get("observed")
    try:
        return observed is not None and math.isfinite(float(observed))
    except (TypeError, ValueError, OverflowError):
        return False


def _demote_nonfatal_outcomes(validation: Mapping[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(dict(validation))
    issues_raw = result.get("issues", [])
    issues = [dict(item) for item in issues_raw if isinstance(item, Mapping)]
    demoted: list[dict[str, Any]] = []

    for item in issues:
        code = str(item.get("code", ""))
        severity = str(item.get("severity", ""))
        if (
            severity == "error"
            and code in NON_FATAL_HARDWARE_OUTCOME_CODES
            and _has_finite_observation(item)
        ):
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


# ``final_run`` can be the compatibility facade. The production ``execute_case``
# function was defined in a hidden implementation module, so patch the module
# dictionary that actually owns execute_case.__globals__. This removes the
# facade/implementation split which previously let Soak re-enter the strict
# 70.29 hardware validator and abort on a non-fatal thermal observation.
_RUNTIME_GLOBALS = legacy.final_run.execute_case.__globals__
_original_validate_outputs = _RUNTIME_GLOBALS.get("validate_outputs")
if not callable(_original_validate_outputs):
    raise RuntimeError(
        "70.30 could not resolve the production validate_outputs binding used by execute_case"
    )


def _validate_outputs_7030(*args: Any, **kwargs: Any) -> dict[str, Any]:
    validation = _original_validate_outputs(*args, **kwargs)
    return _demote_nonfatal_outcomes(validation)


# Patch every public alias for consistency, but the execute_case globals binding
# below is the authoritative fix and is verified fail-closed immediately.
_RUNTIME_GLOBALS["validate_outputs"] = _validate_outputs_7030
legacy.final_run.validate_outputs = _validate_outputs_7030
final_run.validate_outputs = _validate_outputs_7030

if legacy.final_run.execute_case.__globals__.get("validate_outputs") is not _validate_outputs_7030:
    raise RuntimeError(
        "70.30 validation policy is not bound to the production execute_case path"
    )


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


legacy.FORMAL_CONTRACT_RELATIVE = CONTRACT_RELATIVE
legacy.build_matrix_summary = _build_matrix_summary_7030


if __name__ == "__main__":
    raise SystemExit(legacy.main())
