from __future__ import annotations

import csv
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class LocalDebugCaptureContractTests(unittest.TestCase):
    def test_visual_studio_arguments_are_kept_for_debug_and_release(self) -> None:
        text = (ROOT / "HairSystem.vcxproj.user").read_text(encoding="utf-8-sig")
        arguments = "--local-capture --local-capture-warmup 120 --local-capture-frames 1800"
        self.assertEqual(text.count(arguments), 2)

    def test_cpp_contract_contains_streaming_and_startup_recovery(self) -> None:
        app = (ROOT / "apps/dx11_viewer/src/AppMain.cpp").read_text(encoding="utf-8-sig")
        header = (ROOT / "apps/dx11_viewer/src/GpuHairViewer.h").read_text(encoding="utf-8-sig")
        inl10 = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl").read_text(encoding="utf-8-sig")
        inl11 = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl").read_text(encoding="utf-8-sig")
        inl15 = (ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/15_shutdown_and_tail.inl").read_text(encoding="utf-8-sig")

        self.assertIn("RecoverInterruptedLocalDebugRuns", app)
        self.assertIn("LaunchLocalDebugStopWatcher", app)
        self.assertIn("CREATE_BREAKAWAY_FROM_JOB", app)
        self.assertIn("--watch-pid", app)
        self.assertIn("FinalizeLocalDebugStreaming", app)
        self.assertIn("run_state.json", app)
        self.assertIn("m_localDebugWarmupFrames", header)
        self.assertIn("m_localDebugCaptureFrames", header)
        self.assertIn("m_localDebugPartialCsvPath", header)
        self.assertIn("m_localDebugCsvStream", header)
        self.assertIn("HAIRGPU_F5_LOCAL_CAPTURE_CONFIG_V2", inl10)
        self.assertIn("HAIRGPU_LOCAL_DEBUG_WARMUP_FRAMES", inl10)
        self.assertIn("HAIRGPU_LOCAL_DEBUG_CAPTURE_FRAMES", inl10)
        self.assertIn("EnsureLocalDebugStreamingInitialized()", inl10)
        self.assertIn("HAIRGPU_LOCAL_DEBUG_PARTIAL_STREAM_V1", inl10)
        self.assertIn("FlushFileBuffers", inl10)
        self.assertIn("HAIRGPU_F5_LOCAL_CAPTURE_RANGE_V2", inl11)
        self.assertIn("m_globalFrameIndex >= m_localDebugWarmupFrames", inl11)
        self.assertIn("m_localDebugStreamSampleCount < static_cast<uint64_t>(m_localDebugCaptureFrames)", inl11)
        self.assertNotIn("m_measuredFrameIndex < m_experimentCase.measurementFrames", inl11)
        self.assertIn("AppendLocalDebugSample(localDebugSample)", inl11)
        self.assertIn("bool GpuHairViewer::FinalizeLocalDebugStreaming()", inl15)
        self.assertIn("std::filesystem::rename(partial, finalPath", inl15)

    def test_recover_partial_generates_report_and_marks_interrupted(self) -> None:
        module_path = ROOT / "tools/final_stage/hairgpu_local_debug_report.py"
        spec = importlib.util.spec_from_file_location("hairgpu_local_debug_report", module_path)
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temp_text:
            temp = Path(temp_text)
            run_dir = temp / "results/committed/local_debug/20260725T000000Z_999999"
            run_dir.mkdir(parents=True)
            (run_dir / "capture_context.json").write_text(
                json.dumps({"evidenceClass": "diagnostic_debugger"}),
                encoding="utf-8",
            )
            (run_dir / "run_state.json").write_text(
                json.dumps({"status": "RUNNING", "processId": 999999}),
                encoding="utf-8",
            )
            fields = [
                "sampleIndex", "frameMilliseconds", "renderFrameMilliseconds",
                "simulationMilliseconds", "fps", "gpuHairMilliseconds",
                "totalHairGpuMilliseconds", "simulationTicksThisFrame",
                "droppedSimulationMilliseconds", "presentSyncInterval", "evidenceClass",
                "variant", "staticCachedGroupCount", "staticTransformedGroupCount",
                "dynamicGroupCount", "repairRequiredGroupCount", "falseStaticRate",
                "staticDynamicBoundaryMaxErrorMm", "nanStateCount", "infiniteStateCount",
                "fatalFailure",
            ]
            partial = run_dir / "local_debug_frames.partial.csv"
            with partial.open("w", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=fields)
                writer.writeheader()
                for index in range(3):
                    writer.writerow({
                        "sampleIndex": index,
                        "frameMilliseconds": 16.6,
                        "renderFrameMilliseconds": 16.6,
                        "simulationMilliseconds": 4.0,
                        "fps": 60.0,
                        "gpuHairMilliseconds": 2.0,
                        "totalHairGpuMilliseconds": 2.5,
                        "simulationTicksThisFrame": 1,
                        "droppedSimulationMilliseconds": 0.0,
                        "presentSyncInterval": 1,
                        "evidenceClass": "diagnostic_debugger",
                        "variant": "DEV",
                        "staticCachedGroupCount": 10,
                        "staticTransformedGroupCount": 2,
                        "dynamicGroupCount": 4,
                        "repairRequiredGroupCount": 0,
                        "falseStaticRate": 0.0,
                        "staticDynamicBoundaryMaxErrorMm": 0.0,
                        "nanStateCount": 0,
                        "infiniteStateCount": 0,
                        "fatalFailure": 0,
                    })

            result = module.main([
                "--repo-root", str(temp),
                "--run-dir", str(run_dir),
                "--run-name", "local_debug",
                "--viewer-exit-code", "143",
                "--recover-partial",
            ])
            self.assertEqual(result, 0)
            self.assertTrue((run_dir / "local_debug_frames.csv").is_file())
            self.assertTrue((run_dir / "report.md").is_file())
            self.assertTrue((run_dir / "frame_time.svg").is_file())
            self.assertTrue((run_dir / "postprocess_status.json").is_file())
            state = json.loads((run_dir / "run_state.json").read_text(encoding="utf-8"))
            self.assertEqual(state["status"], "INTERRUPTED")
            self.assertTrue(state["partialDataRecovered"])


    def test_detached_watcher_finalizes_after_watched_process_exits(self) -> None:
        module_path = ROOT / "tools/final_stage/hairgpu_local_debug_report.py"
        spec = importlib.util.spec_from_file_location(
            "hairgpu_local_debug_report_watcher", module_path
        )
        self.assertIsNotNone(spec)
        self.assertIsNotNone(spec.loader)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)

        with tempfile.TemporaryDirectory() as temp_text:
            temp = Path(temp_text)
            run_dir = temp / "results/committed/local_debug/20260725T000001Z_999998"
            run_dir.mkdir(parents=True)
            (run_dir / "capture_context.json").write_text(
                json.dumps({"evidenceClass": "diagnostic_debugger"}),
                encoding="utf-8",
            )
            (run_dir / "run_state.json").write_text(
                json.dumps({"status": "RUNNING", "processId": 999998}),
                encoding="utf-8",
            )
            fields = [
                "sampleIndex", "frameMilliseconds", "renderFrameMilliseconds",
                "simulationMilliseconds", "fps", "gpuHairMilliseconds",
                "totalHairGpuMilliseconds", "simulationTicksThisFrame",
                "droppedSimulationMilliseconds", "presentSyncInterval", "evidenceClass",
                "variant", "staticCachedGroupCount", "staticTransformedGroupCount",
                "dynamicGroupCount", "repairRequiredGroupCount", "falseStaticRate",
                "staticDynamicBoundaryMaxErrorMm", "nanStateCount", "infiniteStateCount",
                "fatalFailure",
            ]
            partial = run_dir / "local_debug_frames.partial.csv"
            with partial.open("w", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=fields)
                writer.writeheader()
                writer.writerow({
                    "sampleIndex": 0,
                    "frameMilliseconds": 16.6,
                    "renderFrameMilliseconds": 16.6,
                    "simulationMilliseconds": 4.0,
                    "fps": 60.0,
                    "gpuHairMilliseconds": 2.0,
                    "totalHairGpuMilliseconds": 2.5,
                    "simulationTicksThisFrame": 1,
                    "droppedSimulationMilliseconds": 0.0,
                    "presentSyncInterval": 1,
                    "evidenceClass": "diagnostic_debugger",
                    "variant": "DEV",
                    "staticCachedGroupCount": 10,
                    "staticTransformedGroupCount": 2,
                    "dynamicGroupCount": 4,
                    "repairRequiredGroupCount": 0,
                    "falseStaticRate": 0.0,
                    "staticDynamicBoundaryMaxErrorMm": 0.0,
                    "nanStateCount": 0,
                    "infiniteStateCount": 0,
                    "fatalFailure": 0,
                })

            child = subprocess.Popen([
                sys.executable,
                "-c",
                "import time; time.sleep(0.25)",
            ])
            result = module.main([
                "--repo-root", str(temp),
                "--run-dir", str(run_dir),
                "--run-name", "local_debug",
                "--viewer-exit-code", "143",
                "--watch-pid", str(child.pid),
                "--watch-timeout-seconds", "5",
            ])
            child.wait(timeout=5)
            self.assertEqual(result, 0)
            self.assertTrue((run_dir / "local_debug_frames.csv").is_file())
            self.assertTrue((run_dir / "report.md").is_file())
            state = json.loads((run_dir / "run_state.json").read_text(encoding="utf-8"))
            self.assertEqual(state["status"], "INTERRUPTED")
            self.assertTrue(state["partialDataRecovered"])


if __name__ == "__main__":
    unittest.main()
