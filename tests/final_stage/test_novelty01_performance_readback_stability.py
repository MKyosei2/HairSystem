from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE_PATH = ROOT / "apps/dx11_viewer/src/GpuHairViewer.cpp"
RUNNER_PATH = ROOT / "tools/final_stage/Run_Thesis_B0_P_Formal.ps1"


class Novelty01PerformanceReadbackStabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = SOURCE_PATH.read_text(encoding="utf-8")
        cls.runner = RUNNER_PATH.read_text(encoding="utf-8-sig")

    def test_performance_counter_only_controller_is_present(self) -> None:
        for marker in (
            "HAIRGPU_N01_PERFORMANCE_QUERY_GATED_READBACK_V2",
            "HairGpuN01PerformanceCounterReadbackSlot",
            "HairGpuN01PerformanceCounterReadbackState",
            "HairGpuN01PerformanceCounterReadbacks",
            "HairGpuReleaseN01PerformanceCounterReadbacks",
            "ComPtr<ID3D11Query> completionQuery;",
            "D3D11_QUERY_EVENT",
            "D3D11_ASYNC_GETDATA_DONOTFLUSH",
            "m_context->End(slot.completionQuery.Get());",
            "D3D11_MAP_READ,\n                0u,",
            "runtime->latestCountersValid = true;",
            "runtime->latestCounterSourceFrame = slot.sourceFrame;",
        ):
            self.assertIn(marker, self.source)

    def test_performance_ground_truth_contract_is_fail_closed(self) -> None:
        wrapper_start = self.source.index(
            "void GpuHairViewer::ExecuteNovelty01ExperimentTick(float dt)"
        )
        wrapper_end = self.source.index(
            "#define ShutdownNovelty01Routed ShutdownNovelty01RoutedCore",
            wrapper_start,
        )
        wrapper = self.source[wrapper_start:wrapper_end]

        self.assertIn("if (runtime->formalGroundTruthEnabled)", wrapper)
        self.assertIn("1502u", wrapper)
        self.assertIn(
            "runtime->featureMask &=\n            ~hairgpu::novelty01::Feature_GroundTruth;",
            wrapper,
        )
        self.assertIn("runtime->formalGroundTruthValidThisFrame = false;", wrapper)

    def test_performance_pins_legacy_multi_megabyte_readback_ring(self) -> None:
        wrapper_start = self.source.index(
            "void GpuHairViewer::ExecuteNovelty01ExperimentTick(float dt)"
        )
        wrapper_end = self.source.index(
            "#define ShutdownNovelty01Routed ShutdownNovelty01RoutedCore",
            wrapper_start,
        )
        wrapper = self.source[wrapper_start:wrapper_end]

        # Pin once before the legacy P tick and once after it. This prevents the
        # original SG-APT code from mapping or re-issuing its full dynamic/repair
        # list staging copies while Performance is measuring throughput.
        self.assertGreaterEqual(wrapper.count("for (auto& legacySlot : runtime->readbacks)"), 2)
        self.assertGreaterEqual(wrapper.count("legacySlot.issued = true;"), 2)
        self.assertGreaterEqual(wrapper.count("legacySlot.ageFrames = 0u;"), 2)
        self.assertGreaterEqual(wrapper.count("legacySlot.formalMetricsIssued = false;"), 2)

        # The replacement Performance controller itself is counters-only.
        perf_controller_start = wrapper.index(
            "HairGpuN01PerformanceCounterReadbackState& state ="
        )
        perf_controller_end = wrapper.index(
            "const uint32_t savedDesignScenarioIndex",
            perf_controller_start,
        )
        perf_controller = wrapper[perf_controller_start:perf_controller_end]
        self.assertNotIn("slot.dynamicList", perf_controller)
        self.assertNotIn("slot.repairList", perf_controller)
        self.assertNotIn("runtime->dynamicList.buffer.Get()", perf_controller)
        self.assertNotIn("runtime->repairList.buffer.Get()", perf_controller)

    def test_p_does_not_use_unrelated_legacy_terminal_counter_copy(self) -> None:
        macro_start = self.source.index("#define DrainGpuTimingAndCounterReadbacks()")
        macro_end = self.source.index("#define Shutdown ShutdownNovelty01Routed", macro_start)
        macro = self.source[macro_start:macro_end]

        self.assertIn("QueueCounterReadback(m_globalFrameIndex);", macro)
        self.assertIn("const bool hairGpuFormalPPerformance", macro)
        self.assertIn("!hairGpuFormalPPerformance", macro)
        self.assertIn("ExperimentMethodId::P_IntegratedSegmentGroup", macro)
        self.assertIn("HairGpuWaitForGpuIdleAfterMeasurement", macro)
        self.assertIn("m_context->ClearState();", macro)
        self.assertIn("HairGpuReleaseN01PerformanceCounterReadbacks(this);", macro)

    def test_counter_only_feedback_keeps_fast_path_inputs_without_cpu_group_lists(self) -> None:
        wrapper_start = self.source.index(
            "void GpuHairViewer::ExecuteNovelty01ExperimentTick(float dt)"
        )
        wrapper_end = self.source.index(
            "#define ShutdownNovelty01Routed ShutdownNovelty01RoutedCore",
            wrapper_start,
        )
        wrapper = self.source[wrapper_start:wrapper_end]

        for marker in (
            "runtime->latestCounters.values.data()",
            "hairgpu::novelty01::kCounterCount",
            "Counter_DirtyFollowers",
            "runtime->activeFollowers.assign(dirtyFollowers, 0u);",
            "m_context->CopyResource(\n                slot.counters.Get(),\n                runtime->counters.buffer.Get());",
        ):
            self.assertIn(marker, wrapper)

    def test_query_gated_map_replaces_age_only_do_not_wait_map(self) -> None:
        wrapper_start = self.source.index(
            "void GpuHairViewer::ExecuteNovelty01ExperimentTick(float dt)"
        )
        wrapper_end = self.source.index(
            "#define ShutdownNovelty01Routed ShutdownNovelty01RoutedCore",
            wrapper_start,
        )
        wrapper = self.source[wrapper_start:wrapper_end]
        self.assertIn("GetData(", wrapper)
        self.assertIn("D3D11_ASYNC_GETDATA_DONOTFLUSH", wrapper)
        self.assertIn("m_context->End(slot.completionQuery.Get());", wrapper)
        self.assertNotIn("D3D11_MAP_FLAG_DO_NOT_WAIT", wrapper)

    def test_formal_method_window_labels_are_explicit(self) -> None:
        self.assertIn("HAIRGPU_FORMAL_METHOD_LABEL_V1", self.source)
        self.assertIn("PRE-NOVELTY BASELINE [B0]", self.source)
        self.assertIn("POST-NOVELTY [P / SG-APT]", self.source)


    def test_post_measurement_p_readbacks_are_released_before_legacy_drain(self) -> None:
        self.assertIn("HAIRGPU_N01_PERFORMANCE_POST_MEASUREMENT_DRAIN_V3", self.source)
        self.assertIn(
            "HairGpuDrainAndReleaseN01PerformanceCounterReadbacksAfterGpuIdle",
            self.source,
        )
        macro_start = self.source.index("#define DrainGpuTimingAndCounterReadbacks()")
        macro_end = self.source.index("#define Shutdown ShutdownNovelty01Routed", macro_start)
        macro = self.source[macro_start:macro_end]
        release = macro.index(
            "HairGpuDrainAndReleaseN01PerformanceCounterReadbacksAfterGpuIdle"
        )
        legacy = macro.index("DrainGpuTimingAndCounterReadbacks();", release)
        self.assertLess(release, legacy)
        self.assertIn("1510u", macro)

    def test_qualification_only_crash_checkpoints_are_present(self) -> None:
        self.assertIn("HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_V1", self.source)
        self.assertIn("HAIRGPU_FORMAL_DIAGNOSTIC_CHECKPOINT_FILE", self.source)
        for marker in (
            'L"p_performance_before_tick"',
            'L"p_performance_after_legacy_tick"',
            'L"post_measurement_before_terminal_drain"',
            'L"post_measurement_after_gpu_idle"',
            'L"post_measurement_before_legacy_drain"',
            'L"post_measurement_after_legacy_drain"',
        ):
            self.assertIn(marker, self.source)

    def test_formal_samples_are_preallocated_before_measurement(self) -> None:
        self.assertIn("HAIRGPU_FORMAL_SAMPLE_PREALLOC_V1", self.source)
        wrapper_start = self.source.index(
            "void GpuHairViewer::ExecuteNovelty01ExperimentTick(float dt)"
        )
        first_legacy = self.source.index(
            "ExecuteNovelty01ExperimentTickLegacy(dt);", wrapper_start
        )
        prelude = self.source[wrapper_start:first_legacy]
        self.assertIn("m_simulationTickIndex == 0u", prelude)
        self.assertIn("m_performanceSamples.clear();", prelude)
        self.assertIn("m_performanceSamples.reserve(", prelude)
        self.assertIn("m_experimentCase.measurementFrames", prelude)

    def test_formal_soak_retains_more_than_12000_samples(self) -> None:
        self.assertIn("HAIRGPU_FORMAL_SOAK_SAMPLE_RETENTION_V1", self.source)
        self.assertIn("HairGpuPerformanceSampleHistoryView", self.source)
        self.assertIn("HairGpuFormalPerformanceSampleHistory", self.source)
        self.assertIn("m_formalExperiment", self.source)
        self.assertIn("std::min<size_t>(m_container.size(), 12000u)", self.source)
        include_start = self.source.index("#define m_performanceSamples")
        include_end = self.source.index("#undef m_performanceSamples", include_start)
        include_route = self.source[include_start:include_end]
        self.assertIn("IsExperimentModeEnabled()", include_route)

    def test_runner_qualifies_late_stages_before_formal_smoke(self) -> None:
        for marker in (
            "Invoke-PreFormalQualificationSuite",
            'Profile "performance"',
            'Scenarios "S01"',
            'Scenarios "S01,S07,S13,S16,S17,S18"',
            'Profile "determinism"',
            'Profile "formal"',
            'Scenarios "S13"',
            "PRE-FORMAL QUALIFICATION (NOT THESIS EVIDENCE)",
        ):
            self.assertIn(marker, self.runner)
        qualification_call = self.runner.index(
            "Invoke-PreFormalQualificationSuite -Session $session"
        )
        asset_complete = self.runner.index(
            'Add-CompletedStage -Session $session -Name "Asset"',
            qualification_call,
        )
        self.assertLess(qualification_call, asset_complete)

    def test_runner_captures_full_dumps_and_prevalidates_report_counts(self) -> None:
        for marker in (
            "Enable-HairSystemCrashDumpCapture",
            "Windows Error Reporting\\LocalDumps\\HairSystem.exe",
            '-Name "DumpType" -Value 2',
            '-Name "DumpCount" -Value 1',
            "Assert-MatrixSummaryContract",
            "Assert-FormalEvidenceReadyForReport",
            "Pre-Report evidence gate: PASS (100 total / 50 B0 / 50 P)",
            "Assert-FreeDiskSpace",
        ):
            self.assertIn(marker, self.runner)

    def test_new_fail_closed_codes_are_present(self) -> None:
        for code in (
            "1502u", "1503u", "1504u", "1505u",
            "1506u", "1507u", "1508u", "1509u", "1510u",
        ):
            self.assertIn(code, self.source)


if __name__ == "__main__":
    unittest.main()
