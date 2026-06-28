#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Apply HairSystem Novelty 61.39 final metrics fixes.

Run from HairSystem repository root:
    python scripts/apply_novelty_61_39_final_fix.py
"""

from __future__ import annotations

import json
from pathlib import Path


ROOT = Path.cwd()
TELEMETRY_H = '#pragma once\n\n#include <cstdint>\n\nnamespace hairgpu\n{\n    struct HairNoveltyFrameTelemetry\n    {\n        uint32_t frameIndex = 0u;\n        uint32_t measuredFrameIndex = 0u;\n        uint32_t topLevelNoveltyMask = 0u;\n\n        uint32_t cpuReadbackCount = 0u;\n        uint32_t overflowCount = 0u;\n        uint32_t budgetDropCount = 0u;\n        uint32_t fallbackCount = 0u;\n        uint32_t deferCount = 0u;\n\n        uint32_t microtypeDecodedCount = 0u;\n        uint32_t safeDecodeCount = 0u;\n        uint32_t certificateFailureCount = 0u;\n        uint32_t falseSafeCount = 0u;\n        uint32_t typedRepairRequestCount = 0u;\n        uint32_t floatFallbackRequestCount = 0u;\n        uint32_t rejectPayloadCount = 0u;\n\n        // 01: Segment Group Static/Dynamic metrics.\n        uint32_t totalSegmentGroupCount = 0u;\n        uint32_t staticCachedGroupCount = 0u;\n        uint32_t staticTransformedGroupCount = 0u;\n        uint32_t dynamicGroupCount = 0u;\n        uint32_t deferredCorrectionGroupCount = 0u;\n        uint32_t repairRequiredGroupCount = 0u;\n        uint32_t fallbackGroupCount = 0u;\n\n        uint32_t falseStaticCount = 0u;\n        float falseStaticRate = 0.0f;\n\n        uint32_t cacheHitCount = 0u;\n        uint32_t cacheMissCount = 0u;\n        uint32_t cacheInvalidationCount = 0u;\n\n        uint32_t watchdogCheckCount = 0u;\n        uint32_t watchdogRepairCount = 0u;\n\n        float staticDynamicBoundaryMeanErrorMm = 0.0f;\n        float staticDynamicBoundaryMaxErrorMm = 0.0f;\n\n        float directionCacheMemoryMb = 0.0f;\n        float fullPositionMemoryMb = 0.0f;\n        float directionCacheMemorySavingRate = 0.0f;\n\n        uint32_t riskyStrandCount = 0u;\n        uint32_t repairTaskCount = 0u;\n        uint32_t repairCommitCount = 0u;\n        uint32_t repairRejectCount = 0u;\n        uint32_t repairDeferCount = 0u;\n        uint32_t islandCandidateCount = 0u;\n        uint32_t islandCount = 0u;\n        uint32_t islandMemberCount = 0u;\n        uint32_t islandCommitCount = 0u;\n        uint32_t islandRejectCount = 0u;\n        uint32_t physicsFallbackCount = 0u;\n        float maxPenetrationMm = 0.0f;\n\n        uint32_t renderCertificateCount = 0u;\n        uint32_t renderSkipCount = 0u;\n        uint32_t renderCoarseOpacityCount = 0u;\n        uint32_t renderStrandRasterCount = 0u;\n        uint32_t renderHighQualityShadowCount = 0u;\n        uint32_t renderHighQualitySpecularCount = 0u;\n        uint32_t renderFullQualityCount = 0u;\n        uint32_t renderDroppedFullQualityCount = 0u;\n        float p95ShadowErrorBound = 0.0f;\n        float p95SpecularErrorBound = 0.0f;\n        float temporalFlickerScore = 0.0f;\n\n        uint32_t editOperationCount = 0u;\n        uint32_t editRepairTaskCount = 0u;\n        uint32_t editIslandEscalationCount = 0u;\n        uint32_t editCommitCount = 0u;\n        uint32_t editRejectCount = 0u;\n        uint32_t editPreviewFullQualityCount = 0u;\n        float editLatencyMs = 0.0f;\n\n        uint32_t gpuResidentTaskCount = 0u;\n        uint32_t riskyCompactionCount = 0u;\n        uint32_t dispatchIndirectCount = 0u;\n        uint32_t drawIndirectCount = 0u;\n        uint32_t delayedTelemetryReadCount = 0u;\n    };\n\n    inline HairNoveltyFrameTelemetry MakeZeroNoveltyTelemetry()\n    {\n        return HairNoveltyFrameTelemetry{};\n    }\n}\n'
SEGMENT_JSON_BLOCK = '        json << "  \\"false_static_count\\": " << falseStaticCount << ",\\\\n";\n        json << "  \\"false_static_rate\\": " << falseStaticRate << ",\\\\n";\n        json << "  \\"group_static_reuse_rate\\": " << groupStaticReuseRate << ",\\\\n";\n        json << "  \\"group_dynamic_rate\\": " << groupDynamicRate << ",\\\\n";\n        json << "  \\"deferred_correction_rate\\": " << deferredCorrectionRate << ",\\\\n";\n        json << "  \\"total_segment_group_count\\": " << totalSegmentGroups << ",\\\\n";\n        json << "  \\"static_cached_group_count\\": " << staticCachedGroups << ",\\\\n";\n        json << "  \\"static_transformed_group_count\\": " << staticTransformedGroups << ",\\\\n";\n        json << "  \\"dynamic_group_count\\": " << dynamicGroups << ",\\\\n";\n        json << "  \\"deferred_correction_group_count\\": " << deferredGroups << ",\\\\n";\n        json << "  \\"repair_required_group_count\\": " << repairRequiredGroups << ",\\\\n";\n        json << "  \\"fallback_group_count\\": " << fallbackGroups << ",\\\\n";\n        json << "  \\"cache_hit_count\\": " << cacheHits << ",\\\\n";\n        json << "  \\"cache_miss_count\\": " << cacheMisses << ",\\\\n";\n        json << "  \\"cache_invalidation_count\\": " << cacheInvalidations << ",\\\\n";\n        json << "  \\"watchdog_check_count\\": " << watchdogChecks << ",\\\\n";\n        json << "  \\"watchdog_repair_count\\": " << watchdogRepairs << ",\\\\n";\n        json << "  \\"avg_static_dynamic_boundary_error_mm\\": " << ComputeAverage(staticDynamicBoundaryMeanErrors) << ",\\\\n";\n        json << "  \\"max_static_dynamic_boundary_error_mm\\": " << ComputePercentile(staticDynamicBoundaryMaxErrors, 100.0f) << ",\\\\n";\n        json << "  \\"direction_cache_memory_mb\\": " << directionCacheMemoryMb << ",\\\\n";\n        json << "  \\"full_position_memory_mb\\": " << fullPositionMemoryMb << ",\\\\n";\n        json << "  \\"direction_cache_memory_saving_rate\\": " << directionCacheMemorySavingRate << ",\\\\n";\n'
SEGMENT_UPDATE_BLOCK = '\n        with Path(frame_csv_path).open("r", encoding="utf-8", newline="") as handle:\n            reader = csv.DictReader(handle)\n            segment_metric_rows = [\n                (\n                    int(float(self._pick_csv_value(row, "totalSegmentGroupCount", "total_segment_group_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "staticCachedGroupCount", "static_cached_group_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "staticTransformedGroupCount", "static_transformed_group_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "dynamicGroupCount", "dynamic_group_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "deferredCorrectionGroupCount", "deferred_correction_group_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "repairRequiredGroupCount", "repair_required_group_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "fallbackGroupCount", "fallback_group_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "falseStaticCount", "false_static_count", default="0"))),\n                    float(self._pick_csv_value(row, "falseStaticRate", "false_static_rate", default="0")),\n                    int(float(self._pick_csv_value(row, "cacheHitCount", "cache_hit_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "cacheMissCount", "cache_miss_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "cacheInvalidationCount", "cache_invalidation_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "watchdogCheckCount", "watchdog_check_count", default="0"))),\n                    int(float(self._pick_csv_value(row, "watchdogRepairCount", "watchdog_repair_count", default="0"))),\n                    float(self._pick_csv_value(row, "staticDynamicBoundaryMeanErrorMm", "static_dynamic_boundary_mean_error_mm", default="0")),\n                    float(self._pick_csv_value(row, "staticDynamicBoundaryMaxErrorMm", "static_dynamic_boundary_max_error_mm", default="0")),\n                    float(self._pick_csv_value(row, "directionCacheMemoryMb", "direction_cache_memory_mb", default="0")),\n                    float(self._pick_csv_value(row, "fullPositionMemoryMb", "full_position_memory_mb", default="0")),\n                    float(self._pick_csv_value(row, "directionCacheMemorySavingRate", "direction_cache_memory_saving_rate", default="0")),\n                    run_id,\n                    int(float(self._pick_csv_value(row, "sampleIndex", "sample_index", default="0"))),\n                )\n                for row in reader\n            ]\n        if segment_metric_rows:\n            self.connection.executemany(\n                """\n                UPDATE runtime_frame_metrics\n                SET total_segment_group_count = ?,\n                    static_cached_group_count = ?,\n                    static_transformed_group_count = ?,\n                    dynamic_group_count = ?,\n                    deferred_correction_group_count = ?,\n                    repair_required_group_count = ?,\n                    fallback_group_count = ?,\n                    false_static_count = ?,\n                    false_static_rate = ?,\n                    cache_hit_count = ?,\n                    cache_miss_count = ?,\n                    cache_invalidation_count = ?,\n                    watchdog_check_count = ?,\n                    watchdog_repair_count = ?,\n                    static_dynamic_boundary_mean_error_mm = ?,\n                    static_dynamic_boundary_max_error_mm = ?,\n                    direction_cache_memory_mb = ?,\n                    full_position_memory_mb = ?,\n                    direction_cache_memory_saving_rate = ?\n                WHERE run_id = ? AND sample_index = ?\n                """,\n                segment_metric_rows,\n            )\n\n'
RUNTIME_METRICS = [{'name': 'selectedClusterCount', 'formula': 'number of active-set clusters selected under the current budget', 'interpretation': 'Higher generally means more corrective work scheduled this frame.'}, {'name': 'selectedGuideCount', 'formula': 'total guide strands promoted to full corrective simulation', 'interpretation': 'Higher means more of the hairstyle receives high-cost correction.'}, {'name': 'estimatedFrameMilliseconds', 'formula': 'estimatedFixedCostMilliseconds + selectedClusterCount * estimatedClusterCostMilliseconds', 'interpretation': 'Predicted frame cost used for bounded-latency reasoning; lower is safer for the 16.67 ms budget.'}, {'name': 'recommendedSolverIterations', 'formula': 'budget-adapted iteration recommendation derived from the current budget scale', 'interpretation': 'Higher means the runtime can afford more local corrective iterations this frame.'}, {'name': 'falseStaticRate', 'formula': 'falseStaticCount / max(staticCachedGroupCount + staticTransformedGroupCount, 1)', 'interpretation': 'Lower is better; measures how often a group was incorrectly kept Static among groups that reused Static cache.'}, {'name': 'groupStaticReuseRate', 'formula': '(staticCachedGroupCount + staticTransformedGroupCount) / max(totalSegmentGroupCount, 1)', 'interpretation': 'Higher is better when quality constraints are satisfied; measures how much segment-group computation was avoided by cache reuse.'}, {'name': 'groupDynamicRate', 'formula': 'dynamicGroupCount / max(totalSegmentGroupCount, 1)', 'interpretation': 'Reports how much of the hairstyle required full group-level dynamic solving.'}, {'name': 'deferredCorrectionRate', 'formula': 'deferredCorrectionGroupCount / max(totalSegmentGroupCount, 1)', 'interpretation': 'Reports how often temporary correction was used before full solve or repair.'}, {'name': 'staticDynamicBoundaryErrorMm', 'formula': 'mean or max discontinuity error at boundaries between Static and Dynamic segment groups', 'interpretation': 'Lower is better; measures whether Static/Dynamic mixing creates visible or physical discontinuities.'}, {'name': 'watchdogRepairCount', 'formula': 'number of Static Watchdog checks that escalated a group to repair or dynamic solve', 'interpretation': 'Shows whether the safety net detected frozen/static-cache artifacts during stillness or emergency recovery.'}, {'name': 'directionCacheMemorySavingRate', 'formula': '1 - directionCacheMemoryMb / max(fullPositionMemoryMb, eps)', 'interpretation': 'Higher is better; estimates memory saved by storing root position plus direction vectors instead of full point positions.'}]


def backup(path: Path) -> None:
    bak = path.with_suffix(path.suffix + ".bak_61_39_final_fix")
    if path.exists() and not bak.exists():
        bak.write_text(path.read_text(encoding="utf-8"), encoding="utf-8")


def write_clean(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    backup(path)
    path.write_text(text, encoding="utf-8")
    print(f"[fixed] {path}")


def remove_broken_summary_block(text: str) -> str:
    lines = text.splitlines(keepends=True)
    result = []
    skipping = False
    for line in lines:
        if not skipping and 'json << "  \\"false_static_count\\": " << falseStaticCount' in line:
            skipping = True
            continue
        if skipping:
            if 'json << "  \\"direction_cache_memory_saving_rate\\": " << directionCacheMemorySavingRate' in line:
                skipping = False
            continue
        result.append(line)
    return "".join(result)


def patch_viewer_part11() -> None:
    path = ROOT / "apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl"
    text = path.read_text(encoding="utf-8")
    backup(path)

    text = remove_broken_summary_block(text)

    if 'json << "  \\"false_static_count\\": " << falseStaticCount' not in text:
        marker = '        json << "  \\"peak_ram_mb\\": " << m_activeSetPlan.summary.estimatedMemoryMegabytes << ",\\\\n";\n'
        if marker not in text:
            raise RuntimeError("Could not find peak_ram_mb marker for summary JSON insertion.")
        text = text.replace(marker, marker + SEGMENT_JSON_BLOCK, 1)

    path.write_text(text, encoding="utf-8")
    print(f"[patched] {path}")


def patch_final_db() -> None:
    path = ROOT / "tools/final_stage/hairgpu_final_db.py"
    text = path.read_text(encoding="utf-8")
    backup(path)

    if "segment_metric_rows = [" not in text:
        marker = "        self.connection.commit()\n\n        avg_frame_ms ="
        if marker not in text:
            raise RuntimeError("Could not find frame metrics commit marker.")
        text = text.replace(marker, SEGMENT_UPDATE_BLOCK + marker, 1)

    if "AVG(false_static_rate) AS avg_false_static_rate" not in text:
        avg_insert = """AVG(avg_penetration_per_sec) AS avg_penetration_per_sec,
                AVG(false_static_rate) AS avg_false_static_rate,
                AVG(group_static_reuse_rate) AS avg_group_static_reuse_rate,
                AVG(group_dynamic_rate) AS avg_group_dynamic_rate,
                AVG(deferred_correction_rate) AS avg_deferred_correction_rate,
                AVG(avg_static_dynamic_boundary_error_mm) AS avg_static_dynamic_boundary_error_mm,
                AVG(max_static_dynamic_boundary_error_mm) AS avg_max_static_dynamic_boundary_error_mm,
                AVG(direction_cache_memory_saving_rate) AS avg_direction_cache_memory_saving_rate"""
        text = text.replace("AVG(avg_penetration_per_sec) AS avg_penetration_per_sec", avg_insert)

    path.write_text(text, encoding="utf-8")
    print(f"[patched] {path}")


def patch_runtime_suite() -> None:
    path = ROOT / "tools/final_stage/hairgpu_runtime_suite.py"
    text = path.read_text(encoding="utf-8")
    backup(path)

    old_method_header = (
        '"| Method | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio | Avg locality | Avg boundary | Avg edit latency ms | Avg penetration / sec |",\n'
        '        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",'
    )
    new_method_header = (
        '"| Method | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio | False static | Static reuse | Deferred | Boundary err mm | Memory saving | Avg locality | Avg boundary | Avg edit latency ms | Avg penetration / sec |",\n'
        '        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",'
    )
    text = text.replace(old_method_header, new_method_header)

    old_method_row = "f\"| {row['method_code']} | {row['run_count']} | {float(row['avg_frame_ms'] or 0.0):.3f} | {float(row['avg_p95_frame_ms'] or 0.0):.3f} | {float(row['avg_worst_frame_ms'] or 0.0):.3f} | {float(row['avg_fps'] or 0.0):.3f} | {float(row['avg_sixty_fps_ratio'] or 0.0):.3f} | {float(row.get('avg_edit_locality') or 0.0):.3f} | {float(row.get('avg_boundary_naturalness') or 0.0):.3f} | {float(row.get('avg_edit_to_run_latency_ms') or 0.0):.3f} | {float(row.get('avg_penetration_per_sec') or 0.0):.3f} |\""
    new_method_row = "f\"| {row['method_code']} | {row['run_count']} | {float(row['avg_frame_ms'] or 0.0):.3f} | {float(row['avg_p95_frame_ms'] or 0.0):.3f} | {float(row['avg_worst_frame_ms'] or 0.0):.3f} | {float(row['avg_fps'] or 0.0):.3f} | {float(row['avg_sixty_fps_ratio'] or 0.0):.3f} | {float(row.get('avg_false_static_rate') or 0.0):.6f} | {float(row.get('avg_group_static_reuse_rate') or 0.0):.3f} | {float(row.get('avg_deferred_correction_rate') or 0.0):.3f} | {float(row.get('avg_static_dynamic_boundary_error_mm') or 0.0):.3f} | {float(row.get('avg_direction_cache_memory_saving_rate') or 0.0):.3f} | {float(row.get('avg_edit_locality') or 0.0):.3f} | {float(row.get('avg_boundary_naturalness') or 0.0):.3f} | {float(row.get('avg_edit_to_run_latency_ms') or 0.0):.3f} | {float(row.get('avg_penetration_per_sec') or 0.0):.3f} |\""
    text = text.replace(old_method_row, new_method_row)

    old_scenario_header = 'lines += ["", "## Scenario summary", "", "| Scenario | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio | Avg locality | Avg boundary | Avg edit latency ms | Avg penetration / sec |", "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]'
    new_scenario_header = 'lines += ["", "## Scenario summary", "", "| Scenario | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio | False static | Static reuse | Deferred | Boundary err mm | Memory saving | Avg locality | Avg boundary | Avg edit latency ms | Avg penetration / sec |", "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]'
    text = text.replace(old_scenario_header, new_scenario_header)

    old_scenario_row = "f\"| {row['scenario_code']} | {row['run_count']} | {float(row['avg_frame_ms'] or 0.0):.3f} | {float(row['avg_p95_frame_ms'] or 0.0):.3f} | {float(row['avg_worst_frame_ms'] or 0.0):.3f} | {float(row['avg_fps'] or 0.0):.3f} | {float(row['avg_sixty_fps_ratio'] or 0.0):.3f} | {float(row.get('avg_edit_locality') or 0.0):.3f} | {float(row.get('avg_boundary_naturalness') or 0.0):.3f} | {float(row.get('avg_edit_to_run_latency_ms') or 0.0):.3f} | {float(row.get('avg_penetration_per_sec') or 0.0):.3f} |\""
    new_scenario_row = "f\"| {row['scenario_code']} | {row['run_count']} | {float(row['avg_frame_ms'] or 0.0):.3f} | {float(row['avg_p95_frame_ms'] or 0.0):.3f} | {float(row['avg_worst_frame_ms'] or 0.0):.3f} | {float(row['avg_fps'] or 0.0):.3f} | {float(row['avg_sixty_fps_ratio'] or 0.0):.3f} | {float(row.get('avg_false_static_rate') or 0.0):.6f} | {float(row.get('avg_group_static_reuse_rate') or 0.0):.3f} | {float(row.get('avg_deferred_correction_rate') or 0.0):.3f} | {float(row.get('avg_static_dynamic_boundary_error_mm') or 0.0):.3f} | {float(row.get('avg_direction_cache_memory_saving_rate') or 0.0):.3f} | {float(row.get('avg_edit_locality') or 0.0):.3f} | {float(row.get('avg_boundary_naturalness') or 0.0):.3f} | {float(row.get('avg_edit_to_run_latency_ms') or 0.0):.3f} | {float(row.get('avg_penetration_per_sec') or 0.0):.3f} |\""
    text = text.replace(old_scenario_row, new_scenario_row)

    path.write_text(text, encoding="utf-8")
    print(f"[patched] {path}")


def patch_metric_definitions() -> None:
    path = ROOT / "docs/sample_assets/metric_definitions.json"
    if not path.exists():
        print(f"[skip] {path}: not found")
        return
    backup(path)
    data = json.loads(path.read_text(encoding="utf-8"))
    data["runtimeMetrics"] = RUNTIME_METRICS
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[patched] {path}")


def main() -> None:
    write_clean(ROOT / "include/hairgpu/HairNoveltyTelemetry.h", TELEMETRY_H)
    patch_viewer_part11()
    patch_final_db()
    patch_runtime_suite()
    patch_metric_definitions()
    print("\nDone. Recommended checks:")
    print("  1. Build C++ target that includes GpuHairViewer.cpp")
    print("  2. Export a runtime summary JSON and validate it with: python -m json.tool <summary.json>")
    print("  3. Run the final_stage runtime suite import path")


if __name__ == "__main__":
    main()
