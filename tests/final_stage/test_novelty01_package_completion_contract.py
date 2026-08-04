from __future__ import annotations

import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Novelty01PackageCompletionContractTests(unittest.TestCase):
    def test_obsolete_distribution_trees_are_absent(self) -> None:
        for relative in (
            "files", "overlay", "replacement",
            "tools/repair_novelty01_premerge.py",
            "tools/verify_novelty01_sgapt.py",
            "tools/apply_novelty01_69_3_complete.py",
            "tools/apply_novelty01_69_4_complete.py",
            "tools/apply_novelty01_complete.py",
            "tools/apply_novelty01_sgapt.py",
            "tools/final_stage/apply_formal_asset_binding_patch.py",
            "patches/hairgpu_final_run_formal_asset_binding.patch",
        ):
            self.assertFalse((ROOT / relative).exists(), relative)

    def test_formal_source_tree_is_complete(self) -> None:
        required = (
            ".github/workflows/novelty01-formal-gate.yml",
            "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json",
            "docs/experiments/NOVELTY01_IMPLEMENTATION_TRACEABILITY.json",
            "docs/experiments/NOVELTY01_RESOURCE_BINDING_CONTRACT.md",
            "docs/sample_assets/novelty01_frozen_parameters.json",
            "tools/final_stage/Run_Novelty01_Complete_Verification.bat",
            "tools/final_stage/Run_Novelty01_Formal_Matrix_And_Gate.bat",
            "tools/final_stage/hairgpu_novelty01_formal_suite.py",
            "tools/final_stage/hairgpu_novelty01_gate.py",
            "tools/final_stage/hairgpu_novelty01_report.py",
            "tools/final_stage/hairgpu_novelty01_resource_audit.py",
            "設計書/01A_SGAPT_Implementation_Contract_and_Evidence.md",
        )
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)
        for index in range(1, 19):
            self.assertTrue(
                (ROOT / f"benchmarks/novelty01/sgapt_scenarios/S{index:02d}.json").is_file()
            )

    def test_header_and_baseline_contracts_are_formal(self) -> None:
        cpp = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp").read_text(encoding="utf-8")
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8")
        runtime = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/16_segment_group_adaptive_payload_complete.inl").read_text(encoding="utf-8")
        matrix = (ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py").read_text(encoding="utf-8")
        contract = json.loads((ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json").read_text(encoding="utf-8-sig"))
        self.assertNotIn("#define private", cpp)
        self.assertIn("struct Novelty01RuntimeState;", header)
        self.assertIn("ExperimentMethodId::P_IntegratedSegmentGroup", runtime)
        self.assertIn("ExecuteExperimentSimulationTickLegacy(dt);", runtime)
        self.assertIn('baseline_contract.get("method", "P")', matrix)
        self.assertEqual(contract["baselines"]["B1"]["method"], "B1")
        self.assertEqual(contract["baselines"]["B8"]["method"], "B8")

    def test_branch_and_asset_contracts_are_consistent(self) -> None:
        readiness = (ROOT / "tools/final_stage/hairgpu_novelty01_readiness_smoke.py").read_text(encoding="utf-8")
        freeze = (ROOT / "tools/final_stage/freeze_novelty01_asset.py").read_text(encoding="utf-8")
        manifest = json.loads((ROOT / "docs/sample_assets/novelty01_100k_asset_manifest.json").read_text(encoding="utf-8-sig"))
        self.assertIn('{"Novelty01", "main"}', readiness)
        self.assertIn('branch != "Novelty01"', freeze)
        self.assertEqual(manifest["manifestVersion"], "novelty01-100k-v1.6")
        self.assertEqual(manifest["sourceBranch"], "Novelty01")
        self.assertEqual(manifest["generation"]["groupsPerBlock"], 32)


if __name__ == "__main__":
    unittest.main()
