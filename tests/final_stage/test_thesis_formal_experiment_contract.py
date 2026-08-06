from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_matrix_module():
    final_run_stub = types.ModuleType("hairgpu_final_run")
    final_run_stub.execute_case = lambda *args, **kwargs: {}
    preflight_stub = types.ModuleType("hairgpu_novelty01_preflight")
    preflight_stub.preflight = lambda *args, **kwargs: {"passed": True, "errors": []}
    sys.modules["hairgpu_final_run"] = final_run_stub
    sys.modules["hairgpu_novelty01_preflight"] = preflight_stub

    path = ROOT / "tools/final_stage/hairgpu_novelty01_matrix.py"
    spec = importlib.util.spec_from_file_location("thesis_n01_matrix", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load matrix module: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ThesisFormalExperimentContractTests(unittest.TestCase):
    def test_canonical_asset_contract_uses_32_groups_per_block(self) -> None:
        app = (ROOT / "apps/dx11_viewer/src/AppMain.cpp").read_text(encoding="utf-8")
        self.assertIn("constexpr uint32_t kGroupsPerBlock = 32u;", app)
        self.assertIn('TextContainsKeyValue(text, "groupsPerBlock", "32")', app)
        self.assertNotIn("constexpr uint32_t kGroupsPerBlock = 8u;", app)

        manifest = json.loads(
            (ROOT / "docs/sample_assets/novelty01_100k_asset_manifest.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(manifest["generation"]["groupsPerBlock"], 32)
        self.assertEqual(manifest["sourceBranch"], "main")
        self.assertTrue(manifest["evidenceGate"]["requireMainBranchAtFreeze"])

    def test_freeze_tool_requires_main_and_100k_contract(self) -> None:
        source = (
            ROOT / "tools/final_stage/freeze_novelty01_asset.py"
        ).read_text(encoding="utf-8")
        for marker in (
            'FORMAL_BRANCH = "main"',
            "FORMAL_GROUPS_PER_BLOCK = 32",
            '"displayStrandCount": 100000',
            '"primaryGuideStrandCount": 4000',
            '"followersPerGuide": 24',
            '"particlesPerGuide": 128',
            '"solverIterations": 6',
            '"sourceCommitPolicy": "exact-runtime-head-recorded"',
        ):
            self.assertIn(marker, source)
        self.assertNotIn('branch != "Novelty01"', source)

    def test_formal_contract_describes_single_binary_counterbalanced_design(self) -> None:
        contract = json.loads(
            (ROOT / "docs/experiments/NOVELTY01_FORMAL_CONTRACT.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(contract["branch"], "main")
        design = contract["comparisonDesign"]
        self.assertEqual(design["primaryMethods"], ["B0", "P"])
        self.assertTrue(design["sameBinary"])
        self.assertTrue(design["sameCommit"])
        self.assertEqual(design["schedule"], "counterbalanced")
        self.assertEqual(design["seeds"], [1, 2, 3, 4, 5])
        self.assertFalse(contract["baselines"]["B0"]["usesSgaptRepresentation"])

    def test_seed_parser_rejects_invalid_values_and_deduplicates(self) -> None:
        matrix = load_matrix_module()
        self.assertEqual(matrix.parse_positive_int_csv("1,2,2,5", (9,)), [1, 2, 5])
        self.assertEqual(matrix.parse_positive_int_csv(None, (1, 2)), [1, 2])
        for invalid in ("0", "-1", "abc"):
            with self.assertRaises(ValueError):
                matrix.parse_positive_int_csv(invalid, (1,))

    def test_counterbalanced_plan_reverses_adjacent_method_order(self) -> None:
        matrix = load_matrix_module()
        plan = list(
            matrix.iter_run_plan(
                variants=["B0", "P"],
                scenarios=["S01"],
                seeds=[1],
                repetitions=2,
                schedule="counterbalanced",
            )
        )
        self.assertEqual([item[-1] for item in plan], ["B0", "P", "P", "B0"])
        self.assertEqual([item[0] for item in plan], [0, 1, 2, 3])

    def test_case_id_and_case_payload_include_seed(self) -> None:
        matrix = load_matrix_module()
        contract = {
            "baselines": {"B0": {"method": "B0"}},
            "ablations": {},
            "scenarios": {
                "S01": {
                    "runtimeScenarioName": "S01_Idle",
                    "timelinePath": "S01.json",
                }
            },
        }
        case = matrix.build_case(
            {},
            variant="B0",
            scenario="S01",
            seed=5,
            repetition=0,
            profile="smoke",
            contract=contract,
        )
        self.assertIn("seed005", case["case_id"])
        self.assertEqual(case["seed"], 5)
        self.assertEqual(case["random_seed"], 5)
        self.assertEqual(case["method"], "B0")

    def test_b0_contamination_check_rejects_nonzero_payload(self) -> None:
        matrix = load_matrix_module()
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            evidence = matrix.validate_independent_baseline_evidence(
                {
                    "n01UsesSgaptRepresentation": 0,
                    "n01PayloadActiveCount": 0,
                    "n01PayloadAllocatedCount": 0,
                    "n01PayloadRetiredCount": 0,
                    "n01PayloadReusedCount": 0,
                },
                output,
                variant="B0",
                dry_run=False,
            )
            self.assertEqual(evidence["novelty01TelemetryFiles"], 0)

            with self.assertRaises(RuntimeError):
                matrix.validate_independent_baseline_evidence(
                    {"n01PayloadActiveCount": 1},
                    output,
                    variant="B0",
                    dry_run=False,
                )

    def test_formal_runner_never_creates_or_pushes_branches(self) -> None:
        source = (
            ROOT / "tools/final_stage/Run_Thesis_B0_P_Formal.ps1"
        ).read_text(encoding="utf-8")
        forbidden = (
            "git switch -c",
            "git checkout -b",
            "git branch ",
            "git push",
            "gh pr create",
        )
        lowered = source.lower()
        for marker in forbidden:
            self.assertNotIn(marker, lowered)
        self.assertIn("ls-remote --heads origin", source)
        self.assertIn('if ($branch -ne "main")', source)


if __name__ == "__main__":
    unittest.main()
