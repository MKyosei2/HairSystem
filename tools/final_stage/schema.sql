CREATE TABLE IF NOT EXISTS experiments (
    experiment_id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_name TEXT NOT NULL,
    created_at TEXT NOT NULL,
    operator_name TEXT,
    notes TEXT
);

CREATE TABLE IF NOT EXISTS runtime_runs (
    run_id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_id INTEGER NOT NULL,
    run_name TEXT NOT NULL,
    method_code TEXT NOT NULL,
    scenario_code TEXT NOT NULL,
    asset_name TEXT,
    runtime_source TEXT NOT NULL,
    summary_json_path TEXT,
    frame_csv_path TEXT,
    sample_count INTEGER,
    avg_frame_ms REAL,
    p95_frame_ms REAL,
    worst_frame_ms REAL,
    avg_simulation_ms REAL,
    avg_fps REAL,
    sixty_fps_ratio REAL,
    guide_count INTEGER,
    follower_count INTEGER,
    total_strand_count INTEGER,
    total_particle_count INTEGER,
    created_at TEXT NOT NULL,
    FOREIGN KEY (experiment_id) REFERENCES experiments(experiment_id)
);

CREATE TABLE IF NOT EXISTS runtime_frame_metrics (
    frame_metric_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL,
    sample_index INTEGER NOT NULL,
    elapsed_seconds REAL,
    frame_ms REAL,
    simulation_ms REAL,
    fps REAL,
    guide_count INTEGER,
    follower_count INTEGER,
    total_strand_count INTEGER,
    total_particle_count INTEGER,
    FOREIGN KEY (run_id) REFERENCES runtime_runs(run_id)
);

CREATE TABLE IF NOT EXISTS run_artifacts (
    artifact_id INTEGER PRIMARY KEY AUTOINCREMENT,
    run_id INTEGER NOT NULL,
    category TEXT NOT NULL,
    label TEXT NOT NULL,
    artifact_path TEXT NOT NULL,
    sha256 TEXT,
    size_bytes INTEGER,
    created_at TEXT NOT NULL,
    FOREIGN KEY (run_id) REFERENCES runtime_runs(run_id)
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
    FOREIGN KEY (run_id) REFERENCES runtime_runs(run_id)
);

CREATE TABLE IF NOT EXISTS user_task_results (
    task_result_id INTEGER PRIMARY KEY AUTOINCREMENT,
    experiment_id INTEGER NOT NULL,
    task_id TEXT NOT NULL,
    title TEXT NOT NULL,
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
    is_present INTEGER NOT NULL,
    notes TEXT,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS hypothesis_checks (
    check_id INTEGER PRIMARY KEY AUTOINCREMENT,
    suite_name TEXT NOT NULL,
    hypothesis_id TEXT NOT NULL,
    title TEXT NOT NULL,
    source_type TEXT NOT NULL,
    scenario_code TEXT,
    task_id TEXT,
    method_code TEXT,
    baseline_method_code TEXT,
    metric_name TEXT NOT NULL,
    actual_value REAL,
    threshold_value REAL,
    comparator TEXT NOT NULL,
    passed INTEGER NOT NULL,
    notes TEXT,
    created_at TEXT NOT NULL
);
