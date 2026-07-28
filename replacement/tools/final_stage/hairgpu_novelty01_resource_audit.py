from __future__ import annotations

"""Estimate Novelty 01 steady-state and peak GPU memory from the formal contract.

The result is a transparent engineering estimate, not a DXGI measurement. Formal
runs must additionally record real process-local/DXGI usage through the existing
final runner.
"""

import argparse
import json
from pathlib import Path
from typing import Any

MIB = 1024 * 1024


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected JSON object: {path}")
    return value


def bytes_for(count: int, stride: int, generations: int = 1) -> int:
    return int(count) * int(stride) * int(generations)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path("."))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--strict", action="store_true")
    args = parser.parse_args()
    repo = args.repo.resolve()
    frozen = load_json(repo / "docs/sample_assets/novelty01_frozen_parameters.json")
    workload = frozen["fixedWorkload"]
    parameters = frozen["parameters"]

    guides = int(workload["guides"])
    followers = int(workload["followers"])
    strands = int(workload["displayStrands"])
    points = int(workload["pointsPerStrand"])
    groups = int(workload["guideGroups"])
    blocks = (groups + int(parameters["groupsPerBlock"]["value"]) - 1) // int(
        parameters["groupsPerBlock"]["value"])
    payload_capacity = (groups * int(parameters["payloadCapacityPercent"]["value"]) + 99) // 100
    payload_generations = int(parameters["payloadGenerationCount"]["value"])
    total_points = strands * points
    guide_points = guides * points

    # Strides are enforced by C++ static_asserts in SegmentGroupAdaptivePayload.h.
    resources = {
        "core": bytes_for(groups, 64),
        "coreExtension": bytes_for(groups, 64),
        "staticCache": bytes_for(groups, 64),
        "descriptor": bytes_for(groups, 64),
        "blockDescriptor": bytes_for(blocks, 64),
        "payloadGenerations": bytes_for(payload_capacity, 64, payload_generations),
        "payloadOwnerGenerations": bytes_for(payload_capacity, 16, payload_generations),
        "flagsAndPrefix": bytes_for(groups, 16, 2),
        "dynamicStaticRepairInvalidLists": bytes_for(groups, 4, 4),
        "sharedCandidates": bytes_for(groups * 2, 48),
        "guideDirtyRanges": bytes_for(guides, 16),
        "followerDirtyRanges": bytes_for(followers, 16),
        "renderDirtyRanges": bytes_for(strands, 16),
        "renderPositionCache": bytes_for(total_points, 16),
        "selfCollisionHashHeads": bytes_for(32768, 4),
        "selfCollisionNext": bytes_for(guide_points, 4),
        "countersAndIndirectArgs": 64 * 4 + 16 * 4,
        "readbackRing": (64 * 4 + groups * 4 * 2) * 8,
    }
    proposal_steady = sum(resources.values())

    ground_truth = {
        "groundTruthPositionsPingPong": bytes_for(total_points, 16, 4),
        "groundTruthDistanceLambda": bytes_for(guide_points, 4),
        "groundTruthBendLambda": bytes_for(guide_points, 4),
        "groundTruthMetrics": bytes_for(groups, 32),
    }
    ground_truth_bytes = sum(ground_truth.values())

    # Existing viewer position/rest buffers are common to baselines and Proposal.
    common = {
        "currentPositionsPingPong": bytes_for(total_points, 16, 2),
        "previousPositionsPingPong": bytes_for(total_points, 16, 2),
        "restPositions": bytes_for(total_points, 16),
    }
    common_bytes = sum(common.values())
    peak = proposal_steady + ground_truth_bytes + common_bytes

    report = {
        "schema": "hairgpu-novelty01-resource-audit-v1",
        "note": "engineering estimate; formal DXGI/process-local measurements remain required",
        "counts": {
            "groups": groups,
            "blocks": blocks,
            "payloadCapacity": payload_capacity,
            "payloadGenerations": payload_generations,
            "totalPoints": total_points,
        },
        "proposalResourcesBytes": resources,
        "commonViewerResourcesBytes": common,
        "groundTruthAdditionalBytes": ground_truth,
        "totals": {
            "proposalSteadyBytes": proposal_steady,
            "proposalSteadyMiB": proposal_steady / MIB,
            "commonViewerBytes": common_bytes,
            "commonViewerMiB": common_bytes / MIB,
            "groundTruthAdditionalBytes": ground_truth_bytes,
            "groundTruthAdditionalMiB": ground_truth_bytes / MIB,
            "qualityRunPeakEstimateBytes": peak,
            "qualityRunPeakEstimateMiB": peak / MIB,
        },
        "gtx1660SixGiBBudgetPassed": peak <= 6 * 1024 * MIB,
    }
    output = args.output.resolve() if args.output else (
        repo / "artifacts/novelty01_resource_audit.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report["totals"], ensure_ascii=False, indent=2))
    return 0 if report["gtx1660SixGiBBudgetPassed"] or not args.strict else 2


if __name__ == "__main__":
    raise SystemExit(main())
