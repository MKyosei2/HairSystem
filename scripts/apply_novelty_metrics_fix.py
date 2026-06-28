#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Apply Novelty 61.36 Segment Group telemetry fixes to HairSystem Novelty branch.

Run from HairSystem repo root:
    python scripts/apply_novelty_metrics_fix.py

The script creates *.bak_pre_segment_metrics_fix backups before modifying files.
"""

from __future__ import annotations

import json
from pathlib import Path

ROOT = Path.cwd()

TELEMETRY_H = '''#pragma once

#include <cstdint>

namespace hairgpu
{
    struct HairNoveltyFrameTelemetry
    {
        uint32_t frameIndex = 0u;
        uint32_t measuredFrameIndex = 0u;
        uint32_t topLevelNoveltyMask = 0u;

        uint32_t cpuReadbackCount = 0u;
        uint32_t overflowCount = 0u;
        uint32_t budgetDropCount = 0u;
        uint32_t fallbackCount = 0u;
        uint32_t deferCount = 0u;

        uint32_t microtypeDecodedCount = 0u;
        uint32_t safeDecodeCount = 0u;
        uint32_t certificateFailureCount = 0u;
        uint32_t falseSafeCount = 0u;
        uint32_t typedRepairRequestCount = 0u;
        uint32_t floatFallbackRequestCount = 0u;
        uint32_t rejectPayloadCount = 0u;

        // 01: Segment Group Static/Dynamic metrics.
        uint32_t totalSegmentGroupCount = 0u;
        uint32_t staticCachedGroupCount = 0u;
        uint32_t staticTransformedGroupCount = 0u;
        uint32_t dynamicGroupCount = 0u;
        uint32_t deferredCorrectionGroupCount = 0u;
        uint32_t repairRequiredGroupCount = 0u;
        uint32_t fallbackGroupCount = 0u;

        uint32_t falseStaticCount = 0u;
        float falseStaticRate = 0.0f;

        uint32_t cacheHitCount = 0u;
        uint32_t cacheMissCount = 0u;
        uint32_t cacheInvalidationCount = 0u;

        uint32_t watchdogCheckCount = 0u;
        uint32_t watchdogRepairCount = 0u;

        float staticDynamicBoundaryMeanErrorMm = 0.0f;
        float staticDynamicBoundaryMaxErrorMm = 0.0f;

        float directionCacheMemoryMb = 0.0f;
        float fullPositionMemoryMb = 0.0f;
        float directionCacheMemorySavingRate = 0.0f;

        uint32_t riskyStrandCount = 0u;
        uint32_t repairTaskCount = 0u;
        uint32_t repairCommitCount = 0u;
        uint32_t repairRejectCount = 0u;
        uint32_t repairDeferCount = 0u;
        uint32_t islandCandidateCount = 0u;
        uint32_t islandCount = 0u;
        uint32_t islandMemberCount = 0u;
        uint32_t islandCommitCount = 0u;
        uint32_t islandRejectCount = 0u;
        uint32_t physicsFallbackCount = 0u;
        float maxPenetrationMm = 0.0f;

        uint32_t renderCertificateCount = 0u;
        uint32_t renderSkipCount = 0u;
        uint32_t renderCoarseOpacityCount = 0u;
        uint32_t renderStrandRasterCount = 0u;
        uint32_t renderHighQualityShadowCount = 0u;
        uint32_t renderHighQualitySpecularCount = 0u;
        uint32_t renderFullQualityCount = 0u;
        uint32_t renderDroppedFullQualityCount = 0u;
        float p95ShadowErrorBound = 0.0f;
        float p95SpecularErrorBound = 0.0f;
        float temporalFlickerScore = 0.0f;

        uint32_t editOperationCount = 0u;
        uint32_t editRepairTaskCount = 0u;
        uint32_t editIslandEscalationCount = 0u;
        uint32_t editCommitCount = 0u;
        uint32_t editRejectCount = 0u;
        uint32_t editPreviewFullQualityCount = 0u;
        float editLatencyMs = 0.0f;

        uint32_t gpuResidentTaskCount = 0u;
        uint32_t riskyCompactionCount = 0u;
        uint32_t dispatchIndirectCount = 0u;
        uint32_t drawIndirectCount = 0u;
        uint32_t delayedTelemetryReadCount = 0u;
    };

    inline HairNoveltyFrameTelemetry MakeZeroNoveltyTelemetry()
    {
        return HairNoveltyFrameTelemetry{};
    }
}
'''


def backup(path: Path) -> None:
    bak = path.with_suffix(path.suffix + ".bak_pre_segment_metrics_fix")
    if path.exists() and not bak.exists():
        bak.write_text(path.read_text(encoding="utf-8"), encoding="utf-8")


def replace_file(rel: str, content: str) -> None:
    path = ROOT / rel
    backup(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print(f"[fixed] {rel}")


def insert_once(rel: str, marker: str, insertion: str, token: str) -> None:
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    if token in text:
        print(f"[skip] {rel}: {token}")
        return
    if marker not in text:
        raise RuntimeError(f"marker not found in {rel}: {marker[:80]!r}")
    backup(path)
    text = text.replace(marker, marker + insertion, 1)
    path.write_text(text, encoding="utf-8")
    print(f"[patched] {rel}: {token}")


def patch_viewer_summary() -> None:
    rel = "apps/dx11_viewer/src/GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl"

    insert_once(rel,
        "        float vramMb = 0.0f;\n        float bandwidthMbFrame = 0.0f;\n",
        """
        uint64_t totalSegmentGroups = 0u;
        uint64_t staticCachedGroups = 0u;
        uint64_t staticTransformedGroups = 0u;
        uint64_t dynamicGroups = 0u;
        uint64_t deferredGroups = 0u;
        uint64_t repairRequiredGroups = 0u;
        uint64_t fallbackGroups = 0u;
        uint64_t falseStaticCount = 0u;
        uint64_t cacheHits = 0u;
        uint64_t cacheMisses = 0u;
        uint64_t cacheInvalidations = 0u;
        uint64_t watchdogChecks = 0u;
        uint64_t watchdogRepairs = 0u;
        std::vector<float> staticDynamicBoundaryMeanErrors;
        std::vector<float> staticDynamicBoundaryMaxErrors;
        float directionCacheMemoryMb = 0.0f;
        float fullPositionMemoryMb = 0.0f;
        float directionCacheMemorySavingRate = 0.0f;
""",
        "uint64_t totalSegmentGroups")

    insert_once(rel,
        "            vramMb = std::max(vramMb, s.vramMegabytes);\n            bandwidthMbFrame = std::max(bandwidthMbFrame, s.bandwidthMegabytesPerFrame);\n",
        """
            totalSegmentGroups += s.totalSegmentGroupCount;
            staticCachedGroups += s.staticCachedGroupCount;
            staticTransformedGroups += s.staticTransformedGroupCount;
            dynamicGroups += s.dynamicGroupCount;
            deferredGroups += s.deferredCorrectionGroupCount;
            repairRequiredGroups += s.repairRequiredGroupCount;
            fallbackGroups += s.fallbackGroupCount;
            falseStaticCount += s.falseStaticCount;
            cacheHits += s.cacheHitCount;
            cacheMisses += s.cacheMissCount;
            cacheInvalidations += s.cacheInvalidationCount;
            watchdogChecks += s.watchdogCheckCount;
            watchdogRepairs += s.watchdogRepairCount;
            staticDynamicBoundaryMeanErrors.push_back(s.staticDynamicBoundaryMeanErrorMm);
            staticDynamicBoundaryMaxErrors.push_back(s.staticDynamicBoundaryMaxErrorMm);
            directionCacheMemoryMb = std::max(directionCacheMemoryMb, s.directionCacheMemoryMb);
            fullPositionMemoryMb = std::max(fullPositionMemoryMb, s.fullPositionMemoryMb);
            directionCacheMemorySavingRate = std::max(directionCacheMemorySavingRate, s.directionCacheMemorySavingRate);
""",
        "staticCachedGroups += s.staticCachedGroupCount")

    insert_once(rel,
        "        const float fallbackRate = m_performanceSamples.empty() ? 0.0f : static_cast<float>(fallbackCount) / static_cast<float>(m_performanceSamples.size());\n        const float repairSuccessRate = repairTasks > 0u ? static_cast<float>(repairCommitted) / static_cast<float>(repairTasks) : 0.0f;\n",
        """
        const uint64_t staticLikeGroups = staticCachedGroups + staticTransformedGroups;
        const float falseStaticRate = staticLikeGroups > 0u
            ? static_cast<float>(falseStaticCount) / static_cast<float>(staticLikeGroups)
            : 0.0f;
        const float groupStaticReuseRate = totalSegmentGroups > 0u
            ? static_cast<float>(staticLikeGroups) / static_cast<float>(totalSegmentGroups)
            : 0.0f;
        const float groupDynamicRate = totalSegmentGroups > 0u
            ? static_cast<float>(dynamicGroups) / static_cast<float>(totalSegmentGroups)
            : 0.0f;
        const float deferredCorrectionRate = totalSegmentGroups > 0u
            ? static_cast<float>(deferredGroups) / static_cast<float>(totalSegmentGroups)
            : 0.0f;
""",
        "const float groupStaticReuseRate")

    insert_once(rel,
        '        json << "  \\"repair_success_rate\\": " << repairSuccessRate << ",\\n";\n',
        '''        json << "  \\"false_static_count\\": " << falseStaticCount << ",\\n";
        json << "  \\"false_static_rate\\": " << falseStaticRate << ",\\n";
        json << "  \\"group_static_reuse_rate\\": " << groupStaticReuseRate << ",\\n";
        json << "  \\"group_dynamic_rate\\": " << groupDynamicRate << ",\\n";
        json << "  \\"deferred_correction_rate\\": " << deferredCorrectionRate << ",\\n";
        json << "  \\"total_segment_group_count\\": " << totalSegmentGroups << ",\\n";
        json << "  \\"static_cached_group_count\\": " << staticCachedGroups << ",\\n";
        json << "  \\"static_transformed_group_count\\": " << staticTransformedGroups << ",\\n";
        json << "  \\"dynamic_group_count\\": " << dynamicGroups << ",\\n";
        json << "  \\"deferred_correction_group_count\\": " << deferredGroups << ",\\n";
        json << "  \\"repair_required_group_count\\": " << repairRequiredGroups << ",\\n";
        json << "  \\"fallback_group_count\\": " << fallbackGroups << ",\\n";
        json << "  \\"cache_hit_count\\": " << cacheHits << ",\\n";
        json << "  \\"cache_miss_count\\": " << cacheMisses << ",\\n";
        json << "  \\"cache_invalidation_count\\": " << cacheInvalidations << ",\\n";
        json << "  \\"watchdog_check_count\\": " << watchdogChecks << ",\\n";
        json << "  \\"watchdog_repair_count\\": " << watchdogRepairs << ",\\n";
        json << "  \\"avg_static_dynamic_boundary_error_mm\\": " << ComputeAverage(staticDynamicBoundaryMeanErrors) << ",\\n";
        json << "  \\"max_static_dynamic_boundary_error_mm\\": " << ComputePercentile(staticDynamicBoundaryMaxErrors, 100.0f) << ",\\n";
        json << "  \\"direction_cache_memory_mb\\": " << directionCacheMemoryMb << ",\\n";
        json << "  \\"full_position_memory_mb\\": " << fullPositionMemoryMb << ",\\n";
        json << "  \\"direction_cache_memory_saving_rate\\": " << directionCacheMemorySavingRate << ",\\n";
''',
        '\\"group_static_reuse_rate\\"')


def patch_final_db() -> None:
    rel = "tools/final_stage/hairgpu_final_db.py"
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    backup(path)

    if "UPDATE runtime_runs\n            SET false_static_count = ?" not in text:
        marker = "        run_id = int(cursor.lastrowid)\n\n"
        insert = '''        self.connection.execute(
            """
            UPDATE runtime_runs
            SET false_static_count = ?,
                false_static_rate = ?,
                group_static_reuse_rate = ?,
                group_dynamic_rate = ?,
                deferred_correction_rate = ?,
                avg_static_dynamic_boundary_error_mm = ?,
                max_static_dynamic_boundary_error_mm = ?,
                cache_hit_count = ?,
                cache_miss_count = ?,
                cache_invalidation_count = ?,
                watchdog_check_count = ?,
                watchdog_repair_count = ?,
                direction_cache_memory_mb = ?,
                full_position_memory_mb = ?,
                direction_cache_memory_saving_rate = ?
            WHERE run_id = ?
            """,
            (
                int(self._resolve_summary_value(summary, "false_static_count", "falseStaticCount", default=0)),
                float(self._resolve_summary_value(summary, "false_static_rate", "falseStaticRate", default=0.0)),
                float(self._resolve_summary_value(summary, "group_static_reuse_rate", "groupStaticReuseRate", default=0.0)),
                float(self._resolve_summary_value(summary, "group_dynamic_rate", "groupDynamicRate", default=0.0)),
                float(self._resolve_summary_value(summary, "deferred_correction_rate", "deferredCorrectionRate", default=0.0)),
                float(self._resolve_summary_value(summary, "avg_static_dynamic_boundary_error_mm", "avgStaticDynamicBoundaryErrorMm", default=0.0)),
                float(self._resolve_summary_value(summary, "max_static_dynamic_boundary_error_mm", "maxStaticDynamicBoundaryErrorMm", default=0.0)),
                int(self._resolve_summary_value(summary, "cache_hit_count", "cacheHitCount", default=0)),
                int(self._resolve_summary_value(summary, "cache_miss_count", "cacheMissCount", default=0)),
                int(self._resolve_summary_value(summary, "cache_invalidation_count", "cacheInvalidationCount", default=0)),
                int(self._resolve_summary_value(summary, "watchdog_check_count", "watchdogCheckCount", default=0)),
                int(self._resolve_summary_value(summary, "watchdog_repair_count", "watchdogRepairCount", default=0)),
                float(self._resolve_summary_value(summary, "direction_cache_memory_mb", "directionCacheMemoryMb", default=0.0)),
                float(self._resolve_summary_value(summary, "full_position_memory_mb", "fullPositionMemoryMb", default=0.0)),
                float(self._resolve_summary_value(summary, "direction_cache_memory_saving_rate", "directionCacheMemorySavingRate", default=0.0)),
                run_id,
            ),
        )

'''
        if marker not in text:
            raise RuntimeError("run_id marker not found")
        text = text.replace(marker, marker + insert, 1)

    if "segment_metric_rows = [" not in text:
        marker = "        self.connection.commit()\n\n        avg_frame_ms ="
        insert = '''        with Path(frame_csv_path).open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            segment_metric_rows = [
                (
                    int(float(self._pick_csv_value(row, "totalSegmentGroupCount", "total_segment_group_count", default="0"))),
                    int(float(self._pick_csv_value(row, "staticCachedGroupCount", "static_cached_group_count", default="0"))),
                    int(float(self._pick_csv_value(row, "staticTransformedGroupCount", "static_transformed_group_count", default="0"))),
                    int(float(self._pick_csv_value(row, "dynamicGroupCount", "dynamic_group_count", default="0"))),
                    int(float(self._pick_csv_value(row, "deferredCorrectionGroupCount", "deferred_correction_group_count", default="0"))),
                    int(float(self._pick_csv_value(row, "repairRequiredGroupCount", "repair_required_group_count", default="0"))),
                    int(float(self._pick_csv_value(row, "fallbackGroupCount", "fallback_group_count", default="0"))),
                    int(float(self._pick_csv_value(row, "falseStaticCount", "false_static_count", default="0"))),
                    float(self._pick_csv_value(row, "falseStaticRate", "false_static_rate", default="0")),
                    int(float(self._pick_csv_value(row, "cacheHitCount", "cache_hit_count", default="0"))),
                    int(float(self._pick_csv_value(row, "cacheMissCount", "cache_miss_count", default="0"))),
                    int(float(self._pick_csv_value(row, "cacheInvalidationCount", "cache_invalidation_count", default="0"))),
                    int(float(self._pick_csv_value(row, "watchdogCheckCount", "watchdog_check_count", default="0"))),
                    int(float(self._pick_csv_value(row, "watchdogRepairCount", "watchdog_repair_count", default="0"))),
                    float(self._pick_csv_value(row, "staticDynamicBoundaryMeanErrorMm", "static_dynamic_boundary_mean_error_mm", default="0")),
                    float(self._pick_csv_value(row, "staticDynamicBoundaryMaxErrorMm", "static_dynamic_boundary_max_error_mm", default="0")),
                    float(self._pick_csv_value(row, "directionCacheMemoryMb", "direction_cache_memory_mb", default="0")),
                    float(self._pick_csv_value(row, "fullPositionMemoryMb", "full_position_memory_mb", default="0")),
                    float(self._pick_csv_value(row, "directionCacheMemorySavingRate", "direction_cache_memory_saving_rate", default="0")),
                    run_id,
                    int(float(self._pick_csv_value(row, "sampleIndex", "sample_index", default="0"))),
                )
                for row in reader
            ]
        if segment_metric_rows:
            self.connection.executemany(
                """
                UPDATE runtime_frame_metrics
                SET total_segment_group_count = ?,
                    static_cached_group_count = ?,
                    static_transformed_group_count = ?,
                    dynamic_group_count = ?,
                    deferred_correction_group_count = ?,
                    repair_required_group_count = ?,
                    fallback_group_count = ?,
                    false_static_count = ?,
                    false_static_rate = ?,
                    cache_hit_count = ?,
                    cache_miss_count = ?,
                    cache_invalidation_count = ?,
                    watchdog_check_count = ?,
                    watchdog_repair_count = ?,
                    static_dynamic_boundary_mean_error_mm = ?,
                    static_dynamic_boundary_max_error_mm = ?,
                    direction_cache_memory_mb = ?,
                    full_position_memory_mb = ?,
                    direction_cache_memory_saving_rate = ?
                WHERE run_id = ? AND sample_index = ?
                """,
                segment_metric_rows,
            )

'''
        if marker not in text:
            raise RuntimeError("segment metric marker not found")
        text = text.replace(marker, insert + marker, 1)

    avg_repl = """AVG(avg_penetration_per_sec) AS avg_penetration_per_sec,
                AVG(false_static_rate) AS avg_false_static_rate,
                AVG(group_static_reuse_rate) AS avg_group_static_reuse_rate,
                AVG(group_dynamic_rate) AS avg_group_dynamic_rate,
                AVG(deferred_correction_rate) AS avg_deferred_correction_rate,
                AVG(avg_static_dynamic_boundary_error_mm) AS avg_static_dynamic_boundary_error_mm,
                AVG(max_static_dynamic_boundary_error_mm) AS avg_max_static_dynamic_boundary_error_mm,
                AVG(direction_cache_memory_saving_rate) AS avg_direction_cache_memory_saving_rate"""
    text = text.replace("AVG(avg_penetration_per_sec) AS avg_penetration_per_sec\n            FROM runtime_runs", avg_repl + "\n            FROM runtime_runs")
    path.write_text(text, encoding="utf-8")
    print(f"[patched] {rel}")


def patch_runtime_suite() -> None:
    rel = "tools/final_stage/hairgpu_runtime_suite.py"
    path = ROOT / rel
    text = path.read_text(encoding="utf-8")
    backup(path)
    text = text.replace(
        "| Method | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio | Avg locality | Avg boundary | Avg edit latency ms | Avg penetration / sec |",
        "| Method | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio | False static | Static reuse | Deferred | Boundary err mm | Memory saving | Avg locality | Avg boundary | Avg edit latency ms | Avg penetration / sec |",
    )
    text = text.replace(
        "| Scenario | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio | Avg locality | Avg boundary | Avg edit latency ms | Avg penetration / sec |",
        "| Scenario | Runs | Avg frame ms | Avg p95 ms | Avg worst ms | Avg FPS | Avg 60 FPS ratio | False static | Static reuse | Deferred | Boundary err mm | Memory saving | Avg locality | Avg boundary | Avg edit latency ms | Avg penetration / sec |",
    )
    text = text.replace(
        "{float(row['avg_sixty_fps_ratio'] or 0.0):.3f} | {float(row.get('avg_edit_locality') or 0.0):.3f}",
        "{float(row['avg_sixty_fps_ratio'] or 0.0):.3f} | {float(row.get('avg_false_static_rate') or 0.0):.6f} | {float(row.get('avg_group_static_reuse_rate') or 0.0):.3f} | {float(row.get('avg_deferred_correction_rate') or 0.0):.3f} | {float(row.get('avg_static_dynamic_boundary_error_mm') or 0.0):.3f} | {float(row.get('avg_direction_cache_memory_saving_rate') or 0.0):.3f} | {float(row.get('avg_edit_locality') or 0.0):.3f}",
    )
    path.write_text(text, encoding="utf-8")
    print(f"[patched] {rel}")


def patch_metric_definitions() -> None:
    rel = "docs/sample_assets/metric_definitions.json"
    path = ROOT / rel
    if not path.exists():
        print(f"[skip] {rel}: not found")
        return
    backup(path)
    data = json.loads(path.read_text(encoding="utf-8"))
    data["runtimeMetrics"] = [
        {"name": "selectedClusterCount", "formula": "number of active-set clusters selected under the current budget", "interpretation": "Higher generally means more corrective work scheduled this frame."},
        {"name": "selectedGuideCount", "formula": "total guide strands promoted to full corrective simulation", "interpretation": "Higher means more of the hairstyle receives high-cost correction."},
        {"name": "estimatedFrameMilliseconds", "formula": "estimatedFixedCostMilliseconds + selectedClusterCount * estimatedClusterCostMilliseconds", "interpretation": "Predicted frame cost used for bounded-latency reasoning; lower is safer for the 16.67 ms budget."},
        {"name": "recommendedSolverIterations", "formula": "budget-adapted iteration recommendation derived from the current budget scale", "interpretation": "Higher means the runtime can afford more local corrective iterations this frame."},
        {"name": "falseStaticRate", "formula": "falseStaticCount / max(staticCachedGroupCount + staticTransformedGroupCount, 1)", "interpretation": "Lower is better; measures how often a group was incorrectly kept Static among groups that reused Static cache."},
        {"name": "groupStaticReuseRate", "formula": "(staticCachedGroupCount + staticTransformedGroupCount) / max(totalSegmentGroupCount, 1)", "interpretation": "Higher is better when quality constraints are satisfied; measures how much segment-group computation was avoided by cache reuse."},
        {"name": "groupDynamicRate", "formula": "dynamicGroupCount / max(totalSegmentGroupCount, 1)", "interpretation": "Reports how much of the hairstyle required full group-level dynamic solving."},
        {"name": "deferredCorrectionRate", "formula": "deferredCorrectionGroupCount / max(totalSegmentGroupCount, 1)", "interpretation": "Reports how often temporary correction was used before full solve or repair."},
        {"name": "staticDynamicBoundaryErrorMm", "formula": "mean or max discontinuity error at boundaries between Static and Dynamic segment groups", "interpretation": "Lower is better; measures whether Static/Dynamic mixing creates visible or physical discontinuities."},
        {"name": "watchdogRepairCount", "formula": "number of Static Watchdog checks that escalated a group to repair or dynamic solve", "interpretation": "Shows whether the safety net detected frozen/static-cache artifacts during stillness or emergency recovery."},
        {"name": "directionCacheMemorySavingRate", "formula": "1 - directionCacheMemoryMb / max(fullPositionMemoryMb, eps)", "interpretation": "Higher is better; estimates memory saved by storing root position plus direction vectors instead of full point positions."},
    ]
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"[patched] {rel}")


def main() -> None:
    replace_file("include/hairgpu/HairNoveltyTelemetry.h", TELEMETRY_H)
    patch_viewer_summary()
    patch_final_db()
    patch_runtime_suite()
    patch_metric_definitions()
    print("Done. Build and test after applying.")


if __name__ == "__main__":
    main()
