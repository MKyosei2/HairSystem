from __future__ import annotations

import csv
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools" / "final_stage" / "hairgpu_local_debug_report.py"
SPEC = importlib.util.spec_from_file_location("hairgpu_local_debug_report", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
reporter = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = reporter
SPEC.loader.exec_module(reporter)


class LocalDebugReportTests(unittest.TestCase):
    def _write_frames(self, run_dir: Path, run_name: str) -> None:
        columns = [
            "frameMilliseconds",
            "simulationMilliseconds",
            "gpuHairMilliseconds",
            "fps",
            "totalHairGpuMilliseconds",
            "staticCachedGroupCount",
            "staticTransformedGroupCount",
            "dynamicGroupCount",
            "repairRequiredGroupCount",
            "staticDynamicBoundaryMaxErrorMm",
            "falseStaticRate",
            "blockClassificationGpuMilliseconds",
            "groupClassificationGpuMilliseconds",
            "compactionGpuMilliseconds",
            "staticTransformGpuMilliseconds",
            "dynamicSolveGpuMilliseconds",
            "boundaryRepairGpuMilliseconds",
            "watchdogGpuMilliseconds",
            "followerReconstructionGpuMilliseconds",
            "hairDrawGpuMilliseconds",
            "sharedPointConflictCount",
            "variant",
        ]
        rows = []
        for index in range(32):
            rows.append({
                "frameMilliseconds": 10.0 + index * 0.1,
                "simulationMilliseconds": 3.0 + index * 0.02,
                "gpuHairMilliseconds": 4.0 + index * 0.03,
                "fps": 1000.0 / (10.0 + index * 0.1),
                "totalHairGpuMilliseconds": 4.3 + index * 0.03,
                "staticCachedGroupCount": 1000 + index,
                "staticTransformedGroupCount": 120 + index,
                "dynamicGroupCount": 2800 - index,
                "repairRequiredGroupCount": index % 3,
                "staticDynamicBoundaryMaxErrorMm": 0.25 + index * 0.01,
                "falseStaticRate": 0.0001 * index,
                "blockClassificationGpuMilliseconds": 0.10,
                "groupClassificationGpuMilliseconds": 0.20,
                "compactionGpuMilliseconds": 0.15,
                "staticTransformGpuMilliseconds": 0.25,
                "dynamicSolveGpuMilliseconds": 1.80,
                "boundaryRepairGpuMilliseconds": 0.12,
                "watchdogGpuMilliseconds": 0.05,
                "followerReconstructionGpuMilliseconds": 0.80,
                "hairDrawGpuMilliseconds": 1.00,
                "sharedPointConflictCount": 0,
                "variant": "P",
            })
        with (run_dir / f"{run_name}_frames.csv").open(
            "w", encoding="utf-8", newline=""
        ) as handle:
            writer = csv.DictWriter(handle, fieldnames=columns)
            writer.writeheader()
            writer.writerows(rows)

    def test_build_report_creates_diagnostic_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run_dir = root / "results" / "committed" / "local_debug" / "sample"
            run_dir.mkdir(parents=True)
            run_name = "local_debug"
            self._write_frames(run_dir, run_name)
            (run_dir / "capture_context.json").write_text(
                json.dumps({"debuggerAttached": True}), encoding="utf-8"
            )

            summary = reporter.build_report(reporter.ReportInputs(
                repo_root=ROOT,
                run_dir=run_dir,
                run_name=run_name,
                viewer_exit_code=0,
                auto_commit=False,
                push=False,
            ))

            self.assertEqual(summary["evidenceClass"], "diagnostic_debugger")
            self.assertFalse(summary["formalEvidence"])
            self.assertEqual(summary["capturedFrameCount"], 32)
            self.assertTrue(summary["novelty01Interpretation"]["proposalComparable"])
            for name in (
                "frame_time.svg",
                "per_second_realtime.svg",
                f"{run_name}_per_second.csv",
                "segment_group_state.svg",
                "gpu_pass_breakdown.svg",
                "summary_metrics.csv",
                "report.md",
                "run_manifest.json",
                "files.sha256",
            ):
                self.assertTrue((run_dir / name).is_file(), name)
            manifest = json.loads((run_dir / "run_manifest.json").read_text(encoding="utf-8"))
            self.assertFalse(manifest["formalEvidence"])
            self.assertGreater(manifest["frameMilliseconds"]["p95"], 0.0)
            self.assertGreaterEqual(manifest["perSecondSampleCount"], 1)
            self.assertGreater(manifest["perSecondRenderFps"]["mean"], 0.0)


    def test_report_survives_missing_git(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run_dir = root / "results" / "committed" / "local_debug" / "sample"
            run_dir.mkdir(parents=True)
            self._write_frames(run_dir, "local_debug")
            with mock.patch.object(reporter, "_find_git_executable", return_value=None):
                summary = reporter.build_report(reporter.ReportInputs(
                    repo_root=root,
                    run_dir=run_dir,
                    run_name="local_debug",
                    viewer_exit_code=0,
                    auto_commit=False,
                    push=False,
                ))
            self.assertFalse(summary["git"]["gitAvailable"])
            self.assertTrue((run_dir / "report.md").is_file())
            self.assertTrue((run_dir / "run_manifest.json").is_file())

    def test_main_hashes_success_status_before_delivery(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run_dir = root / "results" / "committed" / "local_debug" / "sample"
            run_dir.mkdir(parents=True)
            self._write_frames(run_dir, "local_debug")
            with mock.patch.object(reporter, "_find_git_executable", return_value=None):
                exit_code = reporter.main([
                    "--repo-root", str(root),
                    "--run-dir", str(run_dir),
                    "--run-name", "local_debug",
                ])
            self.assertEqual(exit_code, 0)
            status = json.loads((run_dir / "postprocess_status.json").read_text(encoding="utf-8"))
            self.assertTrue(status["success"])
            hashes = (run_dir / "files.sha256").read_text(encoding="utf-8")
            self.assertIn("postprocess_status.json", hashes)
            self.assertNotIn("files.sha256.tmp", hashes)

    def test_auto_commit_skips_when_git_is_unavailable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            run_dir = repo / "results" / "committed" / "local_debug" / "sample"
            run_dir.mkdir(parents=True)
            with mock.patch.object(reporter, "_find_git_executable", return_value=None):
                result = reporter._auto_commit(repo, run_dir, "sample", True)
            self.assertFalse(result["committed"])
            self.assertEqual(result["reason"], "git_unavailable")

    def test_failed_capture_directory_is_ignored_by_default(self) -> None:
        gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertIn("results/committed/local_debug/", gitignore)
        self.assertNotIn("!results/committed/local_debug/**", gitignore)
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertIn('"add", "-f"', source)

    def test_auto_commit_refuses_unapproved_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            outside = root / "unapproved"
            outside.mkdir()
            with self.assertRaises(RuntimeError):
                reporter._auto_commit(ROOT, outside, "bad", False)


if __name__ == "__main__":
    unittest.main()
