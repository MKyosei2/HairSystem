#!/usr/bin/env python3
from __future__ import annotations

import argparse
from pathlib import Path
import sys

REQUIRED_TEXT = {
    "apps/dx11_viewer/src/GpuHairViewer.h": [
        '#include "hairgpu/SegmentGroupAdaptivePayload.h"',
        "CreateSgaptPayloadResources",
        "UpdateSgaptPayloadConstants",
        "ExecuteSgaptPayloadTransitions",
        "m_sgaptDynamicPayloadBuffer",
        "m_sgaptPayloadPoolCountersBuffer",
        "m_sgaptPayloadTransitionCS",
        "m_sgaptPayloadConstants",
    ],
    "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl": [
        "CreateSgaptPayloadResources",
    ],
    "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl": [
        "HairSegmentGroupAdaptivePayloadRuntime.hlsl",
        "SgaptPayloadTransitionCS",
        "SgaptPayloadPreSolveCS",
        "SgaptPayloadPostSolveCS",
        "ResetSgaptPayloadResources",
        "UpdateSgaptPayloadConstants",
        "ExecuteSgaptPayloadTransitions",
        "ExecuteSgaptPayloadPreSolve",
        "ExecuteSgaptPayloadPostSolve",
    ],
    "apps/dx11_viewer/src/GpuHairViewer.cpp": [
        "16_segment_group_adaptive_payload.inl",
    ],
    "include/hairgpu/SegmentGroupAdaptivePayload.h": [
        "SegmentDynamicPayloadGpuData",
        "SgaptPayloadConstants",
        "sizeof(SegmentDynamicPayloadGpuData) == 32u",
    ],
    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl": [
        "SgaptPayloadTransitionCS",
        "SgaptPayloadPreSolveCS",
        "SgaptPayloadPostSolveCS",
        "SGAPT_FLAG_PAYLOAD_FALLBACK",
        "Retired slots are deliberately not returned",
    ],
    "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload.inl": [
        "CreateSgaptPayloadResources",
        "ResetSgaptPayloadResources",
        "ExecuteSgaptPayloadTransitions",
        "ExecuteSgaptPayloadPreSolve",
        "ExecuteSgaptPayloadPostSolve",
    ],
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    args = parser.parse_args()
    repo = args.repo.resolve()

    failures: list[str] = []
    for relative, needles in REQUIRED_TEXT.items():
        path = repo / relative
        if not path.is_file():
            failures.append(f"missing: {relative}")
            continue
        text = path.read_text(encoding="utf-8")
        for needle in needles:
            if needle not in text:
                failures.append(f"{relative}: missing marker: {needle}")

    if failures:
        print("SG-APT static verification failed:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print("SG-APT static verification passed.")
    print("Note: DirectX/HLSL compilation still must be run on Windows/Visual Studio.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
