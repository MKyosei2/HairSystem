from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Iterable, Mapping

REQUIRED_STAGES = {
    "Prepare",
    "Asset",
    "Smoke",
    "Pilot",
    "Quality",
    "Performance",
    "Determinism",
    "Soak",
    "VerifyBranches",
}
REQUIRED_MATRIX_PROFILES = {"smoke", "pilot", "formal", "performance", "determinism", "soak"}


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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_value(payload: Any, name: str) -> Any | None:
    if isinstance(payload, Mapping):
        if name in payload:
            return payload[name]
        for value in payload.values():
            found = find_value(value, name)
            if found is not None:
                return found
    elif isinstance(payload, list):
        for value in payload:
            found = find_value(value, name)
            if found is not None:
                return found
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--session", type=Path, required=True)
    args = parser.parse_args()

    session = args.session.resolve()
    issues: list[dict[str, Any]] = []

    session_path = session / "formal_session.json"
    session_manifest = read_json(session_path)
    completed = set(str(value) for value in session_manifest.get("completedStages", []))
    missing_stages = sorted(REQUIRED_STAGES - completed)
    if missing_stages:
        issues.append({"code": "E_STAGES_MISSING", "stages": missing_stages})

    formal_commit = str(session_manifest.get("formalCommit", ""))
    if len(formal_commit) != 40:
        issues.append({"code": "E_COMMIT_INVALID", "observed": formal_commit})
    if session_manifest.get("branch") != "main":
        issues.append({"code": "E_BRANCH", "observed": session_manifest.get("branch")})
    if session_manifest.get("sameBinary") is not True or session_manifest.get("sameCommit") is not True:
        issues.append({"code": "E_COMPARISON_IDENTITY"})
    if session_manifest.get("seedAlgorithm") != "sha256-scenario-input-v1":
        issues.append({"code": "E_SEED_ALGORITHM", "observed": session_manifest.get("seedAlgorithm")})

    asset_manifest_path = session / "asset" / "frozen_asset_manifest.json"
    if not asset_manifest_path.is_file():
        issues.append({"code": "E_ASSET_MANIFEST_MISSING"})
    else:
        asset_manifest = read_json(asset_manifest_path)
        if asset_manifest.get("freezeStatus") != "FROZEN":
            issues.append({"code": "E_ASSET_NOT_FROZEN"})
        generation = asset_manifest.get("generation", {})
        expected = {
            "displayStrandCount": 100000,
            "primaryGuideStrandCount": 4000,
            "followersPerGuide": 24,
            "particlesPerGuide": 128,
            "solverIterations": 6,
            "groupsPerBlock": 32,
        }
        for name, value in expected.items():
            if generation.get(name) != value:
                issues.append({
                    "code": "E_ASSET_CONTRACT",
                    "field": name,
                    "expected": value,
                    "observed": generation.get(name),
                })
        observed_manifest_hash = sha256_file(asset_manifest_path)
        if observed_manifest_hash != str(session_manifest.get("assetManifestSha256", "")).lower():
            issues.append({"code": "E_ASSET_MANIFEST_HASH"})

    branch_result_path = session / "integrity" / "remote_branch_integrity_result.json"
    if not branch_result_path.is_file():
        issues.append({"code": "E_BRANCH_RESULT_MISSING"})
    else:
        branch_result = read_json(branch_result_path)
        if branch_result.get("status") != "PASS" or branch_result.get("branchesUnchanged") is not True:
            issues.append({"code": "E_BRANCH_CHANGED"})

    hardware_profile_path = session / "environment" / "hardware_profile.json"
    if not hardware_profile_path.is_file():
        issues.append({"code": "E_HARDWARE_PROFILE_MISSING"})
    else:
        hardware_profile = read_json(hardware_profile_path)
        if hardware_profile.get("status") != "READY":
            issues.append({"code": "E_HARDWARE_PROFILE_NOT_READY"})
        profile_hash = sha256_file(hardware_profile_path)
        if profile_hash != str(session_manifest.get("hardwareProfileSha256", "")).lower():
            issues.append({"code": "E_HARDWARE_PROFILE_HASH"})

    profiles_seen: set[str] = set()
    b0_run_count = 0
    proposal_run_count = 0
    for summary_path in sorted(session.rglob("novelty01_matrix_summary.json")):
        summary = read_json(summary_path)
        profile = str(summary.get("profile", ""))
        profiles_seen.add(profile)
        if summary.get("passed") is not True:
            issues.append({"code": "E_MATRIX_FAILED", "profile": profile, "path": str(summary_path)})
        for issue_name in (
            "scenarioInputParityIssues",
            "seedVariationIssues",
            "determinismIssues",
        ):
            if summary.get(issue_name):
                issues.append({
                    "code": "E_MATRIX_ISSUES",
                    "profile": profile,
                    "issueType": issue_name,
                    "details": summary.get(issue_name),
                })
        for row in summary.get("results", []):
            if not isinstance(row, dict) or row.get("ok") is not True:
                continue
            variant = str(row.get("variant", ""))
            if variant == "B0":
                b0_run_count += 1
                evidence = row.get("independentBaselineEvidence")
                if not isinstance(evidence, dict) or evidence.get("baselineContractComplete") is not True:
                    issues.append({
                        "code": "E_B0_EVIDENCE",
                        "caseId": row.get("caseId"),
                    })
            elif variant == "P":
                proposal_run_count += 1
            if not row.get("scenarioInputSha256"):
                issues.append({
                    "code": "E_SCENARIO_INPUT_HASH_MISSING",
                    "caseId": row.get("caseId"),
                })

    missing_profiles = sorted(REQUIRED_MATRIX_PROFILES - profiles_seen)
    if missing_profiles:
        issues.append({"code": "E_MATRIX_PROFILES_MISSING", "profiles": missing_profiles})
    if b0_run_count == 0 or proposal_run_count == 0 or b0_run_count != proposal_run_count:
        issues.append({
            "code": "E_METHOD_RUN_BALANCE",
            "b0Runs": b0_run_count,
            "proposalRuns": proposal_run_count,
        })

    report_path = session / "report" / "thesis_statistical_report.json"
    if not report_path.is_file():
        issues.append({"code": "E_REPORT_MISSING"})
    else:
        report = read_json(report_path)
        mismatched_pairs = [
            row for row in report.get("pairedRows", [])
            if isinstance(row, dict) and row.get("scenarioInputHashMatch") is not True
        ]
        if mismatched_pairs:
            issues.append({"code": "E_PAIRED_INPUT_MISMATCH", "count": len(mismatched_pairs)})
        if int(report.get("pairedCaseCount", 0)) <= 0:
            issues.append({"code": "E_NO_PAIRED_RESULTS"})

    result = {
        "schema": "hairgpu-thesis-final-gate-v1",
        "status": "PASS" if not issues else "FAIL",
        "sessionId": session_manifest.get("sessionId"),
        "formalCommit": formal_commit,
        "b0RunCount": b0_run_count,
        "proposalRunCount": proposal_run_count,
        "profilesSeen": sorted(profiles_seen),
        "issues": issues,
    }
    output = session / "integrity" / "final_gate.json"
    write_json(output, result)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if not issues else 1


if __name__ == "__main__":
    raise SystemExit(main())
