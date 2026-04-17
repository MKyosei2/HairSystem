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
