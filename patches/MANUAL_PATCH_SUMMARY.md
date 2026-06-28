# Manual patch summary

## 1. include/hairgpu/HairNoveltyTelemetry.h

現在のNoveltyブランチでは、途中に `#pragma once` / `namespace hairgpu` / `struct HairNoveltyFrameTelemetry` が重複しており、末尾にも余分な閉じ括弧があります。

このZIP内の `include/hairgpu/HairNoveltyTelemetry.h` で丸ごと差し替えてください。

## 2. GpuHairViewer_parts/11_experiment_env_and_performance_sample.inl

既にCSV/JSONLには新metricsが入っていますが、summary JSONへの集計が不足しています。

追加されるsummary metrics:

- false_static_count
- false_static_rate
- group_static_reuse_rate
- group_dynamic_rate
- deferred_correction_rate
- total_segment_group_count
- static_cached_group_count
- static_transformed_group_count
- dynamic_group_count
- deferred_correction_group_count
- repair_required_group_count
- fallback_group_count
- cache_hit_count
- cache_miss_count
- cache_invalidation_count
- watchdog_check_count
- watchdog_repair_count
- avg_static_dynamic_boundary_error_mm
- max_static_dynamic_boundary_error_mm
- direction_cache_memory_mb
- full_position_memory_mb
- direction_cache_memory_saving_rate

## 3. tools/final_stage/hairgpu_final_db.py

壊れやすいINSERT文の列数を直接変更する代わりに、`run_id` 作成後の UPDATE と、frame metrics用の追加 UPDATE を挿入します。

これにより、既存のINSERT placeholder数ズレを避けながら新metricsをDBに保存します。

## 4. tools/final_stage/hairgpu_runtime_suite.py

Method leaderboard と Scenario summary に以下の列を追加します。

- False static
- Static reuse
- Deferred
- Boundary err mm
- Memory saving

## 5. docs/sample_assets/metric_definitions.json

重複している runtimeMetrics を整理し、`falseStaticRate` は以下に統一します。

```txt
falseStaticRate = falseStaticCount / max(staticCachedGroupCount + staticTransformedGroupCount, 1)
```

理由: false-staticは「Staticにしたものの中で間違っていた割合」として見る方が論文上わかりやすいため。
