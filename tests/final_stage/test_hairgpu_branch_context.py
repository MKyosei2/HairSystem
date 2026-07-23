from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS = Path(__file__).resolve().parents[2] / "tools" / "final_stage"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

import hairgpu_branch_context as branch_context


class BranchContextTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        subprocess.run(["git", "init"], cwd=self.root, check=True, capture_output=True)
        subprocess.run(
            ["git", "config", "user.email", "test@example.invalid"],
            cwd=self.root,
            check=True,
        )
        subprocess.run(
            ["git", "config", "user.name", "HairGPU Test"],
            cwd=self.root,
            check=True,
        )
        (self.root / "seed.txt").write_text("seed\n", encoding="utf-8")
        subprocess.run(["git", "add", "seed.txt"], cwd=self.root, check=True)
        subprocess.run(["git", "commit", "-m", "seed"], cwd=self.root, check=True)
        subprocess.run(
            ["git", "switch", "-c", "feature/novelty-01"],
            cwd=self.root,
            check=True,
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_config(self) -> tuple[Path, Path]:
        config_dir = self.root / "docs" / "sample_assets"
        scenario_dir = self.root / "benchmarks" / "novelty01" / "scenarios"
        config_dir.mkdir(parents=True)
        scenario_dir.mkdir(parents=True)
        timeline = scenario_dir / "S01_idle.json"
        timeline.write_text(
            json.dumps(
                {
                    "scenario": "S01_Idle",
                    "durationFrames": 6600,
                    "fixedTimeStepSeconds": 1.0 / 60.0,
                    "events": [],
                    "assertions": {
                        "exactAppliedEventCount": 0,
                        "timelineFinalFrameRequired": True,
                    },
                }
            ),
            encoding="utf-8",
        )
        (self.root / "profiles.json").write_text('{"profiles":{}}\n', encoding="utf-8")
        source_dir = self.root / "source"
        source_dir.mkdir()
        for name in ("viewer.exe", "schema.sql", "asset.json"):
            (source_dir / name).write_text(name, encoding="utf-8")
        shader_dir = source_dir / "shaders"
        shader_dir.mkdir()
        config = {
            "outputs_dir": "../../out/prenovelty",
            "telemetry_dir": "../../out/prenovelty/telemetry",
            "hardware_profiles": "../../profiles.json",
            "source": {
                "viewer_exe": "../../source/viewer.exe",
                "schema": "../../source/schema.sql",
                "asset_manifest": "../../source/asset.json",
                "shader_root": "../../source/shaders",
                "required_branch": "Novelty",
            },
            "scenarios": [
                {
                    "code": "S01_Idle",
                    "timeline": "../../benchmarks/novelty01/scenarios/S01_idle.json",
                }
            ],
        }
        config_path = config_dir / "suite.json"
        config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        return config_path, timeline

    def test_detects_current_named_branch_and_commit(self) -> None:
        context = branch_context.detect_branch(self.root)
        self.assertEqual(context["branch"], "feature/novelty-01")
        self.assertRegex(context["commitSha"], r"^[0-9a-f]{40}$")
        self.assertFalse(context["detachedHead"])

    def test_materialization_pins_branch_without_modifying_inputs(self) -> None:
        config_path, timeline = self._write_config()
        original_config = config_path.read_bytes()
        original_timeline = timeline.read_bytes()
        context = branch_context.detect_branch(self.root)
        output = self.root / "out" / "run"

        resolved_path, resolved_output, payload = branch_context.materialize_config(
            config_path,
            self.root,
            output,
            context,
        )

        self.assertEqual(resolved_output, output.resolve())
        self.assertEqual(config_path.read_bytes(), original_config)
        self.assertEqual(timeline.read_bytes(), original_timeline)
        resolved = json.loads(resolved_path.read_text(encoding="utf-8"))
        self.assertEqual(
            resolved["source"]["required_branch"],
            "feature/novelty-01",
        )
        self.assertEqual(
            resolved["source"]["detected_commit_sha"],
            context["commitSha"],
        )
        materialized_timeline = Path(resolved["scenarios"][0]["timeline"])
        self.assertTrue(materialized_timeline.is_file())
        self.assertTrue(str(materialized_timeline).startswith(str(output.resolve())))
        timeline_payload = json.loads(materialized_timeline.read_text(encoding="utf-8"))
        self.assertEqual(timeline_payload["assertions"]["exactAppliedEventCount"], 0)
        self.assertEqual(
            timeline_payload["materializedForBranch"],
            "feature/novelty-01",
        )
        context_file = output / "suite_branch_context.json"
        self.assertTrue(context_file.is_file())
        self.assertEqual(payload["branch"], "feature/novelty-01")
        self.assertFalse(payload["sourceTimelinesModified"])

    def test_output_directory_cannot_mix_branch_or_commit_context(self) -> None:
        config_path, _ = self._write_config()
        output = self.root / "out" / "shared"
        context = branch_context.detect_branch(self.root)
        branch_context.materialize_config(config_path, self.root, output, context)
        conflicting = dict(context)
        conflicting["branch"] = "feature/another-branch"
        with self.assertRaises(branch_context.BranchContextError):
            branch_context.materialize_config(
                config_path,
                self.root,
                output,
                conflicting,
            )

    def test_detached_head_is_rejected_by_default(self) -> None:
        commit = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=self.root,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        subprocess.run(
            ["git", "switch", "--detach", commit],
            cwd=self.root,
            check=True,
            capture_output=True,
        )
        with self.assertRaises(branch_context.BranchContextError):
            branch_context.detect_branch(self.root)
        diagnostic = branch_context.detect_branch(self.root, allow_detached=True)
        self.assertTrue(diagnostic["detachedHead"])
        self.assertTrue(diagnostic["branch"].startswith("detached-"))


if __name__ == "__main__":
    unittest.main()
