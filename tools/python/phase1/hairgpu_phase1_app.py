from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import streamlit as st

ROOT = Path(__file__).resolve().parents[2]
CLI_SCRIPT = Path(__file__).with_name("hairgpu_phase1_cli.py")

st.set_page_config(page_title="HairGPU Phase 1", layout="wide")
st.title("HairGPU Phase 1: Instrumentation + SQL + Excel")
st.caption("フォーム入力 → proxy 計測 → SQLite 保存 → Excel 出力")

with st.sidebar:
    st.header("Paths")
    repo_root = st.text_input("Repo root", str(ROOT))
    cli_binary = st.text_input("hairgpu_cli path", str(ROOT / "build_check/apps/hairgpu_cli/hairgpu_cli"))
    db_path = st.text_input("SQLite DB", str(ROOT / "phase1_outputs/phase1_experiments.sqlite"))
    output_dir = st.text_input("Output dir", str(ROOT / "phase1_outputs"))

col1, col2 = st.columns(2)
with col1:
    experiment_name = st.text_input("Experiment name", "phase1_baseline")
    operator_name = st.text_input("Operator", "")
    build_type = st.selectbox("Build type", ["Debug", "Release"])
    scenario_code = st.selectbox("Scenario", ["S1", "S2", "S3", "S4"])
    method_code = st.selectbox("Method", ["A", "B", "C", "D"], index=3)
    asset_path = st.text_input("Asset path", str(ROOT / "generated_scaffold/asset_normalized.hairbin"))

with col2:
    camera_setup = st.text_input("Camera setup", "default")
    hardware_profile = st.text_input("Hardware profile", "local_dev_gpu")
    notes = st.text_area("Notes", "")
    frame_count = st.number_input("Frame count", min_value=16, value=240, step=16)
    target_fps = st.number_input("Target FPS", min_value=1.0, value=60.0, step=1.0)

st.subheader("Optional manual overrides")
st.caption("未入力なら CLI の proxy 計測結果をそのまま保存します。")
col3, col4, col5 = st.columns(3)
with col3:
    fps_60_ratio = st.text_input("60 FPS ratio override", "")
    frame_time_p95_ms = st.text_input("Frame time p95 override (ms)", "")
    worst_frame_time_ms = st.text_input("Worst frame time override (ms)", "")
with col4:
    peak_ram_mb = st.text_input("Peak RAM override (MB)", "")
    penetration_count_per_sec = st.text_input("Penetration count / sec override", "")
    max_penetration_depth_mm = st.text_input("Max penetration depth override (mm)", "")
with col5:
    silhouette_error = st.text_input("Silhouette error override", "")
    clump_preservation_score = st.text_input("Clump preservation score override", "")
    edit_to_run_latency_ms = st.text_input("Edit-to-run latency override (ms)", "")


def append_optional_float(cmd: list[str], flag: str, value: str) -> None:
    value = value.strip()
    if value:
        cmd.extend([flag, value])


if st.button("Run Phase 1 pipeline", type="primary"):
    cmd = [
        sys.executable,
        str(CLI_SCRIPT),
        "--repo-root",
        repo_root,
        "--db-path",
        db_path,
        "--output-dir",
        output_dir,
        "--cli-path",
        cli_binary,
        "--asset-path",
        asset_path,
        "--experiment-name",
        experiment_name,
        "--operator-name",
        operator_name,
        "--build-type",
        build_type,
        "--scenario-code",
        scenario_code,
        "--method-code",
        method_code,
        "--camera-setup",
        camera_setup,
        "--hardware-profile",
        hardware_profile,
        "--notes",
        notes,
        "--frame-count",
        str(frame_count),
        "--target-fps",
        str(target_fps),
    ]
    append_optional_float(cmd, "--fps-60-ratio", fps_60_ratio)
    append_optional_float(cmd, "--frame-time-p95-ms", frame_time_p95_ms)
    append_optional_float(cmd, "--worst-frame-time-ms", worst_frame_time_ms)
    append_optional_float(cmd, "--peak-ram-mb", peak_ram_mb)
    append_optional_float(cmd, "--penetration-count-per-sec", penetration_count_per_sec)
    append_optional_float(cmd, "--max-penetration-depth-mm", max_penetration_depth_mm)
    append_optional_float(cmd, "--silhouette-error", silhouette_error)
    append_optional_float(cmd, "--clump-preservation-score", clump_preservation_score)
    append_optional_float(cmd, "--edit-to-run-latency-ms", edit_to_run_latency_ms)

    result = subprocess.run(cmd, capture_output=True, text=True)
    st.code(" ".join(cmd), language="bash")
    if result.stdout:
        st.text_area("stdout", result.stdout, height=240)
    if result.stderr:
        st.text_area("stderr", result.stderr, height=180)
    if result.returncode == 0:
        st.success("Phase 1 pipeline completed.")
    else:
        st.error(f"Pipeline failed with exit code {result.returncode}")
