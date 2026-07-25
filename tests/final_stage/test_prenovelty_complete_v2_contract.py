from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "final_stage"


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class PrenoveltyCompleteV2ContractTests(unittest.TestCase):
    def test_complete_mode_contains_all_acceptance_phases(self) -> None:
        batch = (TOOLS / "Run_Final_Runtime_Suite.bat").read_text(encoding="utf-8")
        for required in (
            r"!COMPLETE_ROOT!\smoke",
            r"!COMPLETE_ROOT!\startup20",
            r"!COMPLETE_ROOT!\coverage18",
            r"!COMPLETE_ROOT!\determinism",
            r"!COMPLETE_ROOT!\soak30",
            "hairgpu_prenovelty_complete_v2.py",
        ):
            self.assertIn(required, batch)

    def test_coverage18_selects_one_seed_without_dropping_scenarios(self) -> None:
        batch = (TOOLS / "Run_Final_Runtime_Suite.bat").read_text(encoding="utf-8")
        marker = 'if /I "%MODE%"=="prenovelty-coverage18" set "FILTER_ARGS=--only-seed 1103"'
        self.assertIn(marker, batch)
        self.assertNotIn(
            'if /I "%MODE%"=="prenovelty-coverage18" set "FILTER_ARGS=--only-scenario',
            batch,
        )

    def test_frozen_asset_generation_is_explicit(self) -> None:
        batch = (TOOLS / "Run_Final_Runtime_Suite.bat").read_text(encoding="utf-8")
        self.assertIn("hairgpu_frozen_bundle_lock.py", batch)
        self.assertIn("--ensure", batch)
        self.assertIn("--refreeze", batch)
        complete_block = batch.split('if /I "%MODE%"=="prenovelty-complete"', 1)[1].split(
            'if /I "%MODE%"=="phase13-full"', 1
        )[0]
        self.assertNotIn("--force", complete_block)

    def test_complete_v2_contract_has_five_phases_and_54_coverage_runs(self) -> None:
        module = load_module(
            "hairgpu_prenovelty_complete_v2_contract",
            TOOLS / "hairgpu_prenovelty_complete_v2.py",
        )
        self.assertEqual(
            module.PHASES,
            ("smoke", "startup20", "coverage18", "determinism", "soak30"),
        )
        self.assertEqual(
            module.ANALYSIS_PHASES,
            ("coverage18", "determinism", "soak30"),
        )
        self.assertNotIn("smoke", module.ANALYSIS_PHASES)
        config = {
            "methods": [{"code": "B0"}, {"code": "B6"}, {"code": "B8"}],
            "scenarios": [{"code": f"S{index:02d}"} for index in range(1, 19)],
            "determinism_replays": [{"repetitions": 5}] * 3,
            "long_runs": [{"id": str(index)} for index in range(4)],
        }
        self.assertEqual(module.expected_counts(config, 1103)["coverage18"], 54)

    def test_analysis_prevents_duplicate_identity_and_statistical_reweighting(self) -> None:
        source = (TOOLS / "hairgpu_prenovelty_complete_v2.py").read_text(
            encoding="utf-8"
        )
        self.assertIn('experiment_name = f"{base_experiment}__{phase}"', source)
        self.assertIn("run_name LIKE 'coverage18__%'", source)
        self.assertIn("Smoke is a fast acceptance subset of coverage18", source)

    def test_postprocess_outputs_are_mandatory(self) -> None:
        source = (TOOLS / "hairgpu_prenovelty_complete_v2.py").read_text(encoding="utf-8")
        for required in (
            "validated_evidence.sqlite",
            "formal_results.xlsx",
            "formal_report.md",
            "method_average_fps.svg",
            "scenario_p99_frame_ms.svg",
            "method_average_hair_gpu_ms.svg",
        ):
            self.assertIn(required, source)


if __name__ == "__main__":
    unittest.main()
