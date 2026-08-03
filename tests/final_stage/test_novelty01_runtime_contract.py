from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class Novelty01RuntimeContractTests(unittest.TestCase):
    def test_formal_100k_render_dispatch_is_within_d3d11_limit(self) -> None:
        follower_count = 4000 * 24
        particles_per_strand = 128
        threads_per_group = 256
        groups = (
            follower_count * particles_per_strand + threads_per_group - 1
        ) // threads_per_group
        self.assertEqual(groups, 48000)
        self.assertLessEqual(groups, 65535)

        consumer = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl"
        ).read_text(encoding="utf-8")
        shader = (
            ROOT / "apps/dx11_viewer/shaders/HairNovelty01RenderDirtyCache.hlsl"
        ).read_text(encoding="utf-8")
        self.assertIn("N01DispatchGroups(followerPointCount, 256u)", consumer)
        self.assertIn("D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION", consumer)
        self.assertIn("linearPointIndex / gParticlesPerStrand", shader)
        self.assertNotIn("const uint followerIndex = groupId.x;", shader)

    def test_readiness_fails_closed_on_sgapt_invariants(self) -> None:
        readiness = (
            ROOT / "tools/final_stage/hairgpu_novelty01_readiness_smoke.py"
        ).read_text(encoding="utf-8")
        for counter_index in (35, 36, 37, 38, 47, 48, 49, 58, 59, 60, 62, 63):
            self.assertIn(f"{counter_index}:", readiness)
        for marker in (
            "payloadActiveMax",
            "payloadOverflowMax",
            "renderRegeneratedVerticesMax",
            "groundTruthSamplesMax",
            "fullCpuGroupUploadCount",
            "sameFrameReadbackCount",
        ):
            self.assertIn(marker, readiness)

    def test_formal_telemetry_output_is_configured_before_shutdown(self) -> None:
        consumer = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl"
        ).read_text(encoding="utf-8")
        for marker in (
            "ConfigureNovelty01FormalTelemetryOutput();",
            "HAIRGPU_N01_TELEMETRY_DIR",
            "HAIRGPU_TELEMETRY_DIR",
            "HAIRGPU_TELEMETRY_RUN_NAME",
        ):
            self.assertIn(marker, consumer)

    def test_render_cache_is_gpu_resident_and_has_no_cpu_readback(self) -> None:
        consumer = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/18_novelty01_render_dirty_range_consumer.inl"
        ).read_text(encoding="utf-8")
        for marker in (
            "D3D11_USAGE_STAGING",
            "D3D11_MAP_READ",
            "ReadBackCurrentParticlePositions",
            "Map(cache->renderPositions",
        ):
            self.assertNotIn(marker, consumer)
        for marker in (
            "CreateStructuredBuffer",
            "CSSetShaderResources",
            "CSSetUnorderedAccessViews",
            "Dispatch(",
        ):
            self.assertIn(marker, consumer)

    def test_readiness_runner_is_bounded_and_does_not_patch_sources(self) -> None:
        source = (
            ROOT / "tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1"
        ).read_text(encoding="utf-8")
        self.assertIn('$Branch -notin @("Novelty01", "main")', source)
        self.assertIn("--measurement-frames", source)
        self.assertIn('"600"', source)
        self.assertIn("--timeout-seconds", source)
        self.assertIn("NOVELTY01_READINESS_RESULT.json", source)
        self.assertIn("--require-fxc", source)
        self.assertIn("Direct formal runner self-test", source)
        self.assertIn("4f4c6210d7be40a0b5d522789d2684ade86d3de9", source)
        self.assertNotIn("$FormalAssetPatcher", source)
        self.assertNotIn("Apply/verify formal asset binding", source)
        self.assertNotIn("git push", source.lower())
        self.assertNotIn("git commit", source.lower())

    def test_preflight_records_real_git_identity(self) -> None:
        module = load_module(
            "n01_preflight_runtime",
            ROOT / "tools/final_stage/hairgpu_novelty01_preflight.py",
        )
        self.assertEqual(
            module.SOURCE_BASELINE_COMMIT,
            "4f4c6210d7be40a0b5d522789d2684ade86d3de9",
        )
        source = (
            ROOT / "tools/final_stage/hairgpu_novelty01_preflight.py"
        ).read_text(encoding="utf-8")
        for marker in (
            "git_output",
            "merge-base",
            "sourceBaselineIsAncestor",
            "worktreeDirty",
            "--require-clean-git",
        ):
            self.assertIn(marker, source)
        self.assertNotIn('NOVELTY_SOURCE_COMMIT = "4ad791c6', source)



    def test_payload_owner_lifecycle_has_no_counter_alias_or_stale_list_dependency(
        self,
    ) -> None:
        lifecycle = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/17_novelty01_formal_evidence.inl"
        ).read_text(encoding="utf-8")
        shader = (
            ROOT
            / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
        ).read_text(encoding="utf-8")

        pre_begin = lifecycle.index(
            "void GpuHairViewer::ExecuteNovelty01PreClassificationValidation()"
        )
        post_begin = lifecycle.index(
            "void GpuHairViewer::ExecuteNovelty01PostCompactionLifecycle()"
        )
        next_begin = lifecycle.index(
            "void GpuHairViewer::ExecuteNovelty01DirtyRangePropagation()"
        )
        pre_body = lifecycle[pre_begin:post_begin]
        post_body = lifecycle[post_begin:next_begin]

        self.assertNotIn("runtime.counters.srv.Get()", pre_body)
        self.assertNotIn("runtime.counters.srv.Get()", post_body)

        writer_begin = shader.index("void N01WritePayloadOwnersCS")
        writer_end = shader.index("void N01RetirePayloadOwnersCS")
        writer = shader[writer_begin:writer_end]

        self.assertIn(
            "const uint groupIndex = dispatchThreadId.x;",
            writer,
        )
        self.assertIn(
            "const uint targetPayloadIndex = extension.payloadIndex;",
            writer,
        )
        self.assertIn("const bool dynamicState", writer)
        self.assertIn("const bool payloadValid", writer)
        self.assertNotIn(
            "N01LifecycleCountersIn[N01_COUNTER_DYNAMIC]",
            writer,
        )
        self.assertNotIn("N01LifecycleDynamicList[", writer)

if __name__ == "__main__":
    unittest.main()
