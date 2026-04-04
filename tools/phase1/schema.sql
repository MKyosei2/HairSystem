PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS experiments (
    experiment_id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_name TEXT NOT NULL,
    created_at TEXT NOT NULL,
    operator_name TEXT,
    git_commit TEXT,
    build_type TEXT,
    notes TEXT
);

CREATE TABLE IF NOT EXISTS scenarios (
    scenario_id INTEGER PRIMARY KEY AUTOINCREMENT,
    scenario_code TEXT NOT NULL UNIQUE,
    scenario_name TEXT NOT NULL,
    description TEXT
);

CREATE TABLE IF NOT EXISTS methods (
    method_id INTEGER PRIMARY KEY AUTOINCREMENT,
    method_code TEXT NOT NULL UNIQUE,
    method_name TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS runs (
    run_id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_id INTEGER NOT NULL,
    scenario_id INTEGER NOT NULL,
    method_id INTEGER NOT NULL,
    asset_name TEXT,
    asset_path TEXT,
    camera_setup TEXT,
    hardware_profile TEXT,
    status TEXT,
    started_at TEXT,
    ended_at TEXT,
    config_json TEXT,
    cli_stdout TEXT,
    cli_stderr TEXT,
    notes TEXT,
    FOREIGN KEY (experiment_id) REFERENCES experiments(experiment_id),
    FOREIGN KEY (scenario_id) REFERENCES scenarios(scenario_id),
    FOREIGN KEY (method_id) REFERENCES methods(method_id)
);

CREATE TABLE IF NOT EXISTS metrics_summary (
    run_id INTEGER PRIMARY KEY,
    runtime_mode TEXT,
    frame_count INTEGER,
    target_fps REAL,
    strand_count INTEGER,
    guide_count INTEGER,
    follower_count INTEGER,
    point_count INTEGER,
    particles_per_strand INTEGER,
    total_length REAL,
    average_length REAL,
    max_length REAL,
    average_width REAL,
    average_follower_per_guide REAL,
    fps_60_ratio REAL,
    frame_time_p95_ms REAL,
    worst_frame_time_ms REAL,
    peak_ram_mb REAL,
    average_active_cluster_count REAL,
    max_active_cluster_count INTEGER,
    penetration_count_per_sec REAL,
    max_penetration_depth_mm REAL,
    silhouette_error REAL,
    clump_preservation_score REAL,
    edit_to_run_latency_ms REAL,
    valid INTEGER,
    validation_info_count INTEGER,
    validation_warning_count INTEGER,
    validation_error_count INTEGER,
    min_segment_length REAL,
    average_segment_length REAL,
    max_segment_length REAL,
    zero_length_segment_count INTEGER,
    non_finite_point_count INTEGER,
    non_positive_width_count INTEGER,
    invalid_root_binding_count INTEGER,
    invalid_follow_reference_count INTEGER,
    metrics_json_path TEXT,
    validation_json_path TEXT,
    frame_metrics_csv_path TEXT,
    FOREIGN KEY (run_id) REFERENCES runs(run_id)
);

CREATE TABLE IF NOT EXISTS metrics_frame (
    frame_metric_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL,
    frame_index INTEGER NOT NULL,
    frame_time_ms REAL,
    active_cluster_count INTEGER,
    penetration_count INTEGER,
    max_penetration_depth_mm REAL,
    visible_hair_ratio REAL,
    gpu_memory_mb REAL,
    cpu_memory_mb REAL,
    FOREIGN KEY (run_id) REFERENCES runs(run_id)
);
