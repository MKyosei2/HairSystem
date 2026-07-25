-- Phase1 additive SQL fragment.
-- Apply after tools/final_stage/schema.sql, or merge these columns into CREATE TABLE definitions.

ALTER TABLE runtime_runs ADD COLUMN top_level_novelty_mask INTEGER DEFAULT 0;
ALTER TABLE runtime_runs ADD COLUMN enable_budgeted_physics INTEGER DEFAULT 0;
ALTER TABLE runtime_runs ADD COLUMN enable_certificate_render INTEGER DEFAULT 0;
ALTER TABLE runtime_runs ADD COLUMN enable_per_strand_edit INTEGER DEFAULT 0;
ALTER TABLE runtime_runs ADD COLUMN phase TEXT;
ALTER TABLE runtime_runs ADD COLUMN novelty_mapping_version TEXT;

ALTER TABLE runtime_frame_metrics ADD COLUMN top_level_novelty_mask INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN overflow_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN budget_drop_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN defer_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN microtype_decoded_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN safe_decode_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN false_safe_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN typed_repair_request_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN float_fallback_request_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN reject_payload_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN risky_strand_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN island_candidate_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN repair_commit_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN repair_reject_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN repair_defer_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN physics_fallback_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN render_certificate_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN render_skip_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN render_coarse_opacity_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN render_strand_raster_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN render_high_quality_shadow_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN render_high_quality_specular_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN render_full_quality_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN render_dropped_full_quality_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN p95_shadow_error_bound REAL DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN p95_specular_error_bound REAL DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN temporal_flicker_score REAL DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN edit_operation_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN edit_repair_task_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN edit_island_escalation_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN edit_commit_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN edit_reject_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN edit_preview_full_quality_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN edit_latency_ms REAL DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN gpu_resident_task_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN risky_compaction_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN dispatch_indirect_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN draw_indirect_count INTEGER DEFAULT 0;
ALTER TABLE runtime_frame_metrics ADD COLUMN delayed_telemetry_read_count INTEGER DEFAULT 0;
