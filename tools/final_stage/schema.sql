PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS experiments (
    experiment_id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_name TEXT NOT NULL,
    created_at TEXT NOT NULL,
    operator_name TEXT,
    git_commit TEXT,
    build_type TEXT,
    notes TEXT
);

CREATE TABLE IF NOT EXISTS runtime_runs (
    run_id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_id INTEGER NOT NULL,
    run_name TEXT NOT NULL,
    method_code TEXT NOT NULL,
    scenario_code TEXT NOT NULL,
    asset_name TEXT,
    runtime_source TEXT NOT NULL DEFAULT 'dx11_viewer',

    summary_json_path TEXT NOT NULL,
    frame_csv_path TEXT NOT NULL,
    frame_jsonl_path TEXT,
    settings_json_path TEXT,
    environment_json_path TEXT,
    validation_json_path TEXT,
    manifest_json_path TEXT,

    validation_status TEXT NOT NULL DEFAULT 'incomplete'
        CHECK (validation_status IN (
            'in_progress','complete_valid','complete_invalid',
            'quality_gate_failed','incomplete','failed','synthetic'
        )),
    is_valid INTEGER NOT NULL DEFAULT 0 CHECK (is_valid IN (0,1)),
    invalid_reason_json TEXT,
    real_measurement INTEGER NOT NULL DEFAULT 0 CHECK (real_measurement IN (0,1)),

    requested_method_code TEXT NOT NULL,
    applied_method_code TEXT NOT NULL,
    observed_method_code TEXT NOT NULL,
    measurement_mode TEXT,
    seed INTEGER,
    repetition_index INTEGER,

    commit_sha TEXT,
    executable_sha256 TEXT,
    shader_bundle_sha256 TEXT,
    observed_shader_bytecode_sha256 TEXT,
    asset_sha256 TEXT,
    asset_manifest_sha256 TEXT,
    settings_sha256 TEXT,
    scenario_timeline_sha256 TEXT,
    method_settings_sha256 TEXT,
    canonical_result_sha256 TEXT,
    result_manifest_sha256 TEXT,
    summary_sha256 TEXT,
    frame_csv_sha256 TEXT,
    frame_jsonl_sha256 TEXT,
    settings_json_sha256 TEXT,
    environment_json_sha256 TEXT,
    validation_json_sha256 TEXT,
    decision_sequence_sha256 TEXT,
    state_sequence_sha256 TEXT,
    compaction_sequence_sha256 TEXT,
    final_position_sha256 TEXT,

    hardware_profile TEXT,
    gpu_name TEXT,
    gpu_driver TEXT,
    gpu_vendor_id INTEGER,
    gpu_device_id INTEGER,
    dedicated_vram_mb REAL,
    cpu_name TEXT,
    power_scheme TEXT,
    thermal_valid INTEGER CHECK (thermal_valid IS NULL OR thermal_valid IN (0,1)),
    environment_valid INTEGER CHECK (environment_valid IS NULL OR environment_valid IN (0,1)),
    required_metric_coverage_valid INTEGER
        CHECK (required_metric_coverage_valid IS NULL OR required_metric_coverage_valid IN (0,1)),
    thermal_evidence_available INTEGER
        CHECK (thermal_evidence_available IS NULL OR thermal_evidence_available IN (0,1)),
    ground_truth_valid INTEGER
        CHECK (ground_truth_valid IS NULL OR ground_truth_valid IN (0,1)),
    determinism_valid INTEGER
        CHECK (determinism_valid IS NULL OR determinism_valid IN (0,1)),
    long_run_valid INTEGER
        CHECK (long_run_valid IS NULL OR long_run_valid IN (0,1)),

    requested_render_width INTEGER,
    requested_render_height INTEGER,
    actual_render_width INTEGER,
    actual_render_height INTEGER,
    requested_guide_count INTEGER,
    actual_guide_count INTEGER,
    requested_display_strand_count INTEGER,
    actual_display_strand_count INTEGER,
    requested_particles_per_guide INTEGER,
    actual_particles_per_guide INTEGER,
    requested_solver_iterations INTEGER,
    actual_solver_iterations INTEGER,

    sample_count INTEGER,
    avg_frame_ms REAL,
    p50_frame_ms REAL,
    p95_frame_ms REAL,
    p99_frame_ms REAL,
    p999_frame_ms REAL,
    worst_frame_ms REAL,
    avg_simulation_ms REAL,
    avg_hair_gpu_ms REAL,
    p95_hair_gpu_ms REAL,
    p99_hair_gpu_ms REAL,
    avg_fps REAL,
    sixty_fps_ratio REAL,
    frame_overrun_rate REAL,
    guide_count INTEGER,
    follower_count INTEGER,
    total_strand_count INTEGER,
    total_particle_count INTEGER,

    false_static_rate REAL,
    predictive_wake_precision REAL,
    predictive_wake_recall REAL,
    repair_execution_success_rate REAL,
    repair_verified_success_rate REAL,
    repair_recurrence_rate REAL,
    collision_miss_rate REAL,
    state_oscillation_rate REAL,
    max_boundary_error_mm REAL,

    avg_silhouette_error REAL,
    avg_clump_preservation REAL,
    avg_edit_locality REAL,
    avg_boundary_naturalness REAL,
    avg_edit_to_run_latency_ms REAL,
    avg_penetration_per_sec REAL,
    peak_ram_mb REAL,
    peak_vram_mb REAL,
    vram_slope_mb_per_min REAL,
    ram_slope_mb_per_min REAL,
    p99_degradation_ratio REAL,
    query_unresolved_slope REAL,

    gpu_timing_coverage REAL,
    counter_coverage REAL,
    fatal_runtime_failure_count INTEGER,
    nan_count INTEGER,
    inf_count INTEGER,
    full_cpu_upload_count INTEGER,
    same_frame_readback_count INTEGER,
    synthetic_metric_count INTEGER,
    unavailable_metric_count INTEGER,

    created_at TEXT NOT NULL,
    FOREIGN KEY (experiment_id) REFERENCES experiments(experiment_id),
    UNIQUE (experiment_id, run_name)
);

CREATE UNIQUE INDEX IF NOT EXISTS ux_runtime_run_identity
ON runtime_runs (
    experiment_id,
    method_code,
    scenario_code,
    COALESCE(asset_sha256, ''),
    COALESCE(seed, -1),
    COALESCE(hardware_profile, ''),
    COALESCE(repetition_index, -1),
    COALESCE(measurement_mode, '')
);

CREATE TABLE IF NOT EXISTS runtime_frame_metrics (
    frame_metric_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL,
    sample_index INTEGER NOT NULL,
    source_frame_index INTEGER,
    elapsed_seconds REAL,
    frame_ms REAL,
    frame_interval_ms REAL,
    cpu_frame_work_ms REAL,
    simulation_ms REAL,
    fps REAL,
    guide_count INTEGER,
    follower_count INTEGER,
    total_strand_count INTEGER,
    total_particle_count INTEGER,

    availability_mask INTEGER,
    metric_availability_mask INTEGER,
    unavailable_metric_count INTEGER,
    synthetic_metric_count INTEGER,
    fatal_failure INTEGER,
    fatal_failure_code INTEGER,

    total_segment_group_count INTEGER,
    static_cached_group_count INTEGER,
    static_transformed_group_count INTEGER,
    dynamic_group_count INTEGER,
    deferred_group_count INTEGER,
    repair_group_count INTEGER,
    fallback_group_count INTEGER,

    collision_candidate_count INTEGER,
    collision_miss_count INTEGER,
    collision_miss_rate REAL,
    penetration_event_count INTEGER,
    mean_penetration_mm REAL,
    max_penetration_mm REAL,
    predictive_wake_tp INTEGER,
    predictive_wake_fp INTEGER,
    predictive_wake_fn INTEGER,
    predictive_wake_precision REAL,
    predictive_wake_recall REAL,
    wake_delay_frame_max INTEGER,

    repair_attempt_count INTEGER,
    repair_execution_success_count INTEGER,
    repair_verified_count INTEGER,
    repair_unverified_count INTEGER,
    repair_failure_count INTEGER,
    repair_recurrence_count INTEGER,
    repair_execution_success_rate REAL,
    repair_verified_success_rate REAL,
    repair_recurrence_rate REAL,

    state_transition_count INTEGER,
    state_oscillation_count INTEGER,
    state_oscillation_rate REAL,
    false_static_count INTEGER,
    false_static_rate REAL,
    static_dynamic_boundary_mean_error_mm REAL,
    static_dynamic_boundary_max_error_mm REAL,
    shared_point_conflict_count INTEGER,

    cache_hit_count INTEGER,
    cache_miss_count INTEGER,
    cache_invalidation_count INTEGER,
    dynamic_compaction_input_count INTEGER,
    dynamic_compaction_output_count INTEGER,
    repair_compaction_output_count INTEGER,

    full_cpu_group_upload_count INTEGER,
    same_frame_readback_count INTEGER,
    nan_state_count INTEGER,
    infinite_state_count INTEGER,

    block_classification_gpu_ms REAL,
    group_classification_gpu_ms REAL,
    predictive_wake_gpu_ms REAL,
    wake_propagation_gpu_ms REAL,
    compaction_gpu_ms REAL,
    static_transform_gpu_ms REAL,
    dynamic_solve_gpu_ms REAL,
    velocity_recovery_gpu_ms REAL,
    repair_solve_gpu_ms REAL,
    boundary_repair_gpu_ms REAL,
    cache_update_gpu_ms REAL,
    watchdog_gpu_ms REAL,
    follower_reconstruction_gpu_ms REAL,
    render_preparation_gpu_ms REAL,
    hair_draw_gpu_ms REAL,
    telemetry_reduction_gpu_ms REAL,
    total_hair_gpu_ms REAL,
    determinism_mismatch_count INTEGER,
    state_sequence_hash TEXT,
    compaction_sequence_hash TEXT,
    final_position_hash TEXT,

    FOREIGN KEY (run_id) REFERENCES runtime_runs(run_id) ON DELETE CASCADE,
    UNIQUE (run_id, sample_index)
);

CREATE INDEX IF NOT EXISTS ix_runtime_frames_run_source
ON runtime_frame_metrics (run_id, source_frame_index);

CREATE TABLE IF NOT EXISTS run_artifacts (
    artifact_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL,
    category TEXT NOT NULL,
    label TEXT NOT NULL,
    artifact_path TEXT NOT NULL,
    sha256 TEXT,
    size_bytes INTEGER,
    created_at TEXT NOT NULL,
    FOREIGN KEY (run_id) REFERENCES runtime_runs(run_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS run_validations (
    validation_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL,
    rule_code TEXT NOT NULL,
    severity TEXT NOT NULL CHECK (severity IN ('info','warning','error')),
    passed INTEGER NOT NULL CHECK (passed IN (0,1)),
    expected_json TEXT,
    observed_json TEXT,
    message TEXT,
    created_at TEXT NOT NULL,
    FOREIGN KEY (run_id) REFERENCES runtime_runs(run_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS derived_metrics (
    derived_metric_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER,
    scope_type TEXT NOT NULL,
    scope_key TEXT NOT NULL,
    metric_name TEXT NOT NULL,
    metric_value REAL,
    units TEXT,
    notes TEXT,
    created_at TEXT NOT NULL,
    FOREIGN KEY (run_id) REFERENCES runtime_runs(run_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS user_task_results (
    task_result_id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_id INTEGER NOT NULL,
    task_id TEXT NOT NULL,
    title TEXT,
    method_code TEXT NOT NULL,
    scenario_code TEXT,
    asset_name TEXT,
    participant_id TEXT,
    completion_seconds REAL,
    redo_count INTEGER,
    unintended_change_score REAL,
    satisfaction_score REAL,
    notes TEXT,
    created_at TEXT NOT NULL,
    FOREIGN KEY (experiment_id) REFERENCES experiments(experiment_id)
);

CREATE TABLE IF NOT EXISTS scenario_coverage (
    coverage_id INTEGER PRIMARY KEY AUTOINCREMENT,
    suite_name TEXT NOT NULL,
    scenario_code TEXT NOT NULL,
    category TEXT,
    required_method_code TEXT NOT NULL,
    is_present INTEGER NOT NULL CHECK (is_present IN (0,1)),
    notes TEXT,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS hypothesis_checks (
    check_id INTEGER PRIMARY KEY AUTOINCREMENT,
    suite_name TEXT NOT NULL,
    hypothesis_id TEXT NOT NULL,
    title TEXT,
    source_type TEXT,
    scenario_code TEXT,
    task_id TEXT,
    method_code TEXT,
    baseline_method_code TEXT,
    metric_name TEXT NOT NULL,
    actual_value REAL,
    threshold_value REAL,
    comparator TEXT,
    passed INTEGER NOT NULL CHECK (passed IN (0,1)),
    notes TEXT,
    created_at TEXT NOT NULL
);
