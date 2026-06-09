from __future__ import annotations

# Phase1 wrapper overlay.
# Keep using the existing Novelty hairgpu_final_run.py and add these env assignments
# inside run_viewer(case) after the legacy HAIRGPU_ENABLE_GPU_RESIDENT assignment:
#
#     env["HAIRGPU_ENABLE_BUDGETED_PHYSICS"] = "1" if (
#         case.get("enableBudgetedPhysics") or case.get("enableCBMA") or
#         case.get("enablePhysicsIslands") or case.get("enableRodRepair")
#     ) else "0"
#     env["HAIRGPU_ENABLE_CERTIFICATE_RENDER"] = "1" if case.get("enableCertificateRender") else "0"
#     env["HAIRGPU_ENABLE_PER_STRAND_EDIT"] = "1" if case.get("enablePerStrandEdit") else "0"
#
# The actual patch helper tools/final_stage/apply_phase1_patch.py also patches viewer-side code.

import runpy
from pathlib import Path

if __name__ == "__main__":
    runpy.run_path(str(Path(__file__).with_name("hairgpu_final_run.py")), run_name="__main__")
