from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "tools" / "final_stage"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import hairgpu_final_run as final_run
import hairgpu_runtime_suite as suite


class PrenoveltyTestEnvironmentContractTests(unittest.TestCase):
    def test_suite_detects_current_branch_without_hardcoded_name(self) -> None:
        payload = json.loads(
            (ROOT / "docs/sample_assets/prenovelty_runtime_suite.json").read_text(
                encoding="utf-8"
            )
        )
        source = payload["source"]
        self.assertNotIn("required_branch", source)
        contract = source["branch_contract"]
        self.assertEqual(contract["mode"], "detect_current")
        self.assertTrue(contract["require_attached_head"])
        self.assertTrue(contract["record_commit"])

    def test_frozen_asset_source_allows_prenovelty_and_novelty_branches(self) -> None:
        payload = json.loads(
            (ROOT / "docs/sample_assets/prenovelty_runtime_suite.json").read_text(
                encoding="utf-8"
            )
        )
        allowed = payload["source"]["allowed_asset_source_branches"]
        self.assertEqual(allowed, ["prenovelty-ready", "Novelty"])
        runs = suite.expand_run_matrix(payload, ROOT / "docs/sample_assets", ROOT)
        self.assertTrue(runs)
        self.assertEqual(
            runs[0]["allowed_asset_source_branches"],
            ["prenovelty-ready", "Novelty"],
        )

    def test_expand_run_matrix_does_not_modify_timeline_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            timeline = root / "idle.json"
            timeline.write_text(
                json.dumps(
                    {
                        "scenario": "S01_Idle",
                        "durationFrames": 6600,
                        "fixedTimeStepSeconds": 1.0 / 60.0,
                        "events": [],
                        "assertions": {"exactAppliedEventCount": 0},
                    }
                ),
                encoding="utf-8",
            )
            before = timeline.read_bytes()
            config = {
                "run_defaults": {
                    "guide_strand_count": 4000,
                    "followers_per_guide": 24,
                    "display_strand_count": 100000,
                    "particles_per_guide": 128,
                    "solver_iterations": 6,
                    "warmup_frames": 600,
                    "measurement_frames": 6000,
                    "fixed_timestep": True,
                    "fixed_timestep_seconds": 1.0 / 60.0,
                    "width": 1920,
                    "height": 1080,
                },
                "source": {},
                "methods": [{"code": "B0", "settings": {}}],
                "scenarios": [
                    {
                        "code": "S01_Idle",
                        "timeline": str(timeline),
                        "assertions": {"min_applied_events": 0},
                    }
                ],
                "seeds": [1103],
                "repetitions_per_seed": 1,
            }
            runs = suite.expand_run_matrix(config, root, ROOT)
            self.assertTrue(runs)
            self.assertEqual(before, timeline.read_bytes())


    def test_prenovelty_smoke_expands_exactly_twelve_formal_runs(self) -> None:
        config_path = ROOT / "docs/sample_assets/prenovelty_runtime_suite.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        prepared = suite.prepare_suite_config(
            config,
            only_methods=["B0", "B6", "B8"],
            only_scenarios=[
                "S01_Idle",
                "S03_Walk",
                "S09_StrongWind",
                "S17_CloseCamera100k",
            ],
            only_seeds=[1103],
        )
        runs = suite.expand_run_matrix(prepared, config_path.parent, ROOT)
        self.assertEqual(len(runs), 12)
        self.assertTrue(all(run["special_kind"] == "formal" for run in runs))
        self.assertEqual(
            {(run["method"], run["scenario"], int(run["seed"])) for run in runs},
            {
                (method, scenario, 1103)
                for method in ("B0", "B6", "B8")
                for scenario in (
                    "S01_Idle",
                    "S03_Walk",
                    "S09_StrongWind",
                    "S17_CloseCamera100k",
                )
            },
        )

    def test_formal_matrix_never_materializes_special_runs(self) -> None:
        config_path = ROOT / "docs/sample_assets/prenovelty_runtime_suite.json"
        config = json.loads(config_path.read_text(encoding="utf-8"))
        formal = suite.expand_run_matrix(config, config_path.parent, ROOT)
        all_runs = suite.expand_all_runs(config, config_path.parent, ROOT)
        self.assertTrue(formal)
        self.assertTrue(all(run["special_kind"] == "formal" for run in formal))
        self.assertGreater(len(all_runs), len(formal))
        self.assertTrue(any(run["special_kind"] == "determinism" for run in all_runs))
        self.assertTrue(any(run["special_kind"] == "long_run" for run in all_runs))

    def test_idle_zero_event_guard_is_present(self) -> None:
        parts_dir = ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts"
        source = "\n".join(
            path.read_text(encoding="utf-8")
            for path in sorted(parts_dir.glob("*.inl"))
        )
        self.assertIn("allowsZeroMeasuredEvents", source)
        self.assertIn("ExperimentScenarioId::S01_Idle", source)

    def test_formal_launcher_supplies_case_and_exact_output_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            viewer = root / "HairSystem.exe"
            viewer.write_bytes(b"viewer")
            timeline = root / "timeline.json"
            timeline.write_text("{}", encoding="utf-8")

            source_asset = root / "frozen_asset.bin"
            source_asset.write_bytes(b"frozen-asset")
            asset_manifest = {
                "_resolvedMembers": {
                    "sourceAssetPath": str(source_asset.resolve()),
                },
                "sourceAssetSha256": "0" * 64,
                "generatedTopologySha256": "1" * 64,
                "generatorSettingsSha256": "2" * 64,
            }

            manifest = root / "manifest.json"
            manifest.write_text(
                json.dumps(asset_manifest),
                encoding="utf-8",
            )

            output = root / "out"
            paths = final_run.output_paths(output, "formal_case")
            case = final_run.normalize_case({
                "run_name": "formal_case",
                "case_id": "formal_case",
                "method": "B0",
                "variant": "B0",
                "scenario": "S01_Idle",
                "scenario_timeline": str(timeline),
                "asset_manifest": str(manifest),
            })
            environment = final_run._build_environment(
                case,
                paths,
                root,
                viewer,
                asset_manifest,
            )
            self.assertEqual(
                environment["HAIRGPU_CASE_JSON"],
                str(paths["case"]),
            )
            self.assertEqual(environment["HAIRGPU_AUTO_TELEMETRY"], "1")
            self.assertEqual(
                environment["HAIRGPU_TELEMETRY_CSV"],
                str(paths["frames"]),
            )
            self.assertEqual(
                environment["HAIRGPU_TELEMETRY_JSONL"],
                str(paths["jsonl"]),
            )
            self.assertEqual(
                environment["HAIRGPU_SETTINGS_JSON"],
                str(paths["settings"]),
            )
            self.assertEqual(
                environment["HAIRGPU_SUMMARY_JSON"],
                str(paths["summary"]),
            )
            self.assertEqual(
                environment["HAIRGPU_FORMAL_ASSET_PATH"],
                str(source_asset.resolve()),
            )
            self.assertEqual(
                environment["HAIRGPU_FORMAL_ASSET_SHA256"],
                "0" * 64,
            )
            self.assertEqual(
                environment["HAIRGPU_FORMAL_TOPOLOGY_SHA256"],
                "1" * 64,
            )
            self.assertEqual(
                environment["HAIRGPU_GENERATOR_SETTINGS_SHA256"],
                "2" * 64,
            )

    def test_environment_variable_reader_preserves_last_character(self) -> None:
        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/00_preamble_helpers_and_phase1_hooks.inl"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "value.resize(\n                        static_cast<size_t>(written));",
            source,
        )
        self.assertIn("capacity = written;", source)
        self.assertNotIn("(written - 1u)", source)

    def test_viewer_configuration_dialog_uses_detailed_failure_message(self) -> None:
        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/01_window_device_and_ui_setup.inl"
        ).read_text(encoding="utf-8")

        self.assertIn("m_experimentFailureMessage.empty()", source)
        self.assertIn("detail.c_str()", source)
    def test_viewer_honors_explicit_formal_output_paths(self) -> None:
        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/02_resource_creation_and_shader_loading.inl"
        ).read_text(encoding="utf-8")
        for name in (
            "HAIRGPU_TELEMETRY_CSV",
            "HAIRGPU_TELEMETRY_JSONL",
            "HAIRGPU_SETTINGS_JSON",
            "HAIRGPU_SUMMARY_JSON",
        ):
            self.assertIn(name, source)
        self.assertIn("explicitPath", source)

    def test_startup20_uses_real_viewer_lifecycle_test(self) -> None:
        batch = (ROOT / "tools/final_stage/Run_Final_Runtime_Suite.bat").read_text(
            encoding="utf-8"
        )
        self.assertIn("hairgpu_startup_self_test.py", batch)
        self.assertIn("mode is required", batch)


if __name__ == "__main__":
    unittest.main()
