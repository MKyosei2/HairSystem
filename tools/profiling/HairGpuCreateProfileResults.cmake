# HairGPU Step 0 complete policy-safe profile result creator.
# This script is executed from CMake/Visual Studio post-build, but it does not
# run the newly built viewer executable. It only creates deterministic result
# folders and placeholder telemetry/schema files with CMake file operations.

if(NOT DEFINED HAIRGPU_REPO_ROOT)
    get_filename_component(HAIRGPU_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
if(NOT DEFINED HAIRGPU_CONFIG)
    set(HAIRGPU_CONFIG "UnknownConfig")
endif()
if(NOT DEFINED HAIRGPU_PLATFORM)
    set(HAIRGPU_PLATFORM "Windows")
endif()
if(NOT DEFINED HAIRGPU_TARGET_FILE)
    set(HAIRGPU_TARGET_FILE "")
endif()

set(HAIRGPU_TARGET_FPS "60")
set(HAIRGPU_TARGET_FRAME_MS "16.67")
set(HAIRGPU_WARMUP_FRAMES "120")
set(HAIRGPU_MEASURE_FRAMES "600")

string(TIMESTAMP HAIRGPU_STAMP "%Y%m%d_%H%M%S")
string(TIMESTAMP HAIRGPU_HUMAN "%Y-%m-%d %H:%M:%S")
set(HAIRGPU_RESULTS_ROOT "${HAIRGPU_REPO_ROOT}/profile_results")
set(HAIRGPU_RUN_NAME "${HAIRGPU_STAMP}_STEP0_COMPLETE_POLICY_SAFE_${HAIRGPU_CONFIG}_${HAIRGPU_PLATFORM}")
set(HAIRGPU_RUN_DIR "${HAIRGPU_RESULTS_ROOT}/${HAIRGPU_RUN_NAME}")

set(HAIRGPU_PRESETS
    00_current
    01_no_readback
    02_no_self_collision
    03_low_solver
    04_no_debug_draw
    05_render_light
    06_simulation_light
)

file(MAKE_DIRECTORY "${HAIRGPU_RESULTS_ROOT}")
file(MAKE_DIRECTORY "${HAIRGPU_RUN_DIR}")
file(WRITE "${HAIRGPU_RESULTS_ROOT}/LATEST.txt" "${HAIRGPU_RUN_DIR}\nCreatedAt=${HAIRGPU_HUMAN}\nTimestamp=${HAIRGPU_STAMP}\nMode=STEP0_COMPLETE_POLICY_SAFE\n")

file(WRITE "${HAIRGPU_RUN_DIR}/AUTO_PROFILE_STARTED.txt" "Step0Profile=COMPLETE_POLICY_SAFE\n")
file(APPEND "${HAIRGPU_RUN_DIR}/AUTO_PROFILE_STARTED.txt" "CreatedAt=${HAIRGPU_HUMAN}\nTimestamp=${HAIRGPU_STAMP}\nConfiguration=${HAIRGPU_CONFIG}\nPlatform=${HAIRGPU_PLATFORM}\nTarget=${HAIRGPU_TARGET_FILE}\nRunDir=${HAIRGPU_RUN_DIR}\nWarmupFrames=${HAIRGPU_WARMUP_FRAMES}\nMeasureFrames=${HAIRGPU_MEASURE_FRAMES}\nTargetFps=${HAIRGPU_TARGET_FPS}\nTargetFrameMs=${HAIRGPU_TARGET_FRAME_MS}\nPowerShell=NOT_USED\nBAT=NOT_USED\nLocalExeExecution=NOT_USED_BY_DEFAULT\n")

file(WRITE "${HAIRGPU_RUN_DIR}/BUILD_CONDITION.txt" "CreatedAt=${HAIRGPU_HUMAN}\nConfiguration=${HAIRGPU_CONFIG}\nPlatform=${HAIRGPU_PLATFORM}\nTarget=${HAIRGPU_TARGET_FILE}\nRepoRoot=${HAIRGPU_REPO_ROOT}\nTargetFps=${HAIRGPU_TARGET_FPS}\nTargetFrameMs=${HAIRGPU_TARGET_FRAME_MS}\nWarmupFrames=${HAIRGPU_WARMUP_FRAMES}\nMeasureFrames=${HAIRGPU_MEASURE_FRAMES}\n")

file(WRITE "${HAIRGPU_RUN_DIR}/PROFILE_SCHEMA.csv" "metric,meaning,source\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_SCHEMA.csv" "avg_fps,Average FPS over measurement frames,viewer CSV when executable is allowed\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_SCHEMA.csv" "avg_frame_ms,Average CPU frame time,viewer CSV when executable is allowed\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_SCHEMA.csv" "p95_frame_ms,95th percentile frame time,viewer CSV analyzer\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_SCHEMA.csv" "gpu_sim_ms,GPU simulation timing,viewer GPU timer\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_SCHEMA.csv" "gpu_hair_ms,GPU hair draw timing,viewer GPU timer\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_SCHEMA.csv" "readback_particles,Number of particles read back to CPU,viewer telemetry\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_SCHEMA.csv" "solver_iterations,Simulation solver iteration count,viewer telemetry\n")

file(WRITE "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "{\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "  \"profile_stage\": \"step0_complete\",\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "  \"created_at\": \"${HAIRGPU_HUMAN}\",\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "  \"timestamp\": \"${HAIRGPU_STAMP}\",\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "  \"mode\": \"policy_safe_cmake_only\",\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "  \"target_fps\": ${HAIRGPU_TARGET_FPS},\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "  \"target_frame_ms\": ${HAIRGPU_TARGET_FRAME_MS},\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "  \"viewer_execution\": \"skipped_by_default\",\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "  \"real_fps_available\": false\n")
file(APPEND "${HAIRGPU_RUN_DIR}/PROFILE_MANIFEST.json" "}\n")

file(WRITE "${HAIRGPU_RUN_DIR}/RESULT.csv" "timestamp,preset,status,viewer_execution,real_fps_available,avg_fps,avg_frame_ms,p95_frame_ms,worst_frame_ms,diagnosis\n")
foreach(PRESET IN LISTS HAIRGPU_PRESETS)
    set(PRESET_DIR "${HAIRGPU_RUN_DIR}/${PRESET}")
    file(MAKE_DIRECTORY "${PRESET_DIR}")
    file(WRITE "${PRESET_DIR}/RUN_STARTED.txt" "preset=${PRESET}\nCreatedAt=${HAIRGPU_HUMAN}\nTimestamp=${HAIRGPU_STAMP}\nstatus=created_without_launching_viewer\n")
    file(WRITE "${PRESET_DIR}/process_status.txt" "status=not_executed_by_policy_safe_build\nviewer_execution=skipped_by_default\nreal_fps_available=no\ntarget=${HAIRGPU_TARGET_FILE}\n")
    file(WRITE "${PRESET_DIR}/env.txt" "HAIRGPU_BENCHMARK=1\nHAIRGPU_PROFILE_PRESET=${PRESET}\nHAIRGPU_BENCHMARK_WARMUP=${HAIRGPU_WARMUP_FRAMES}\nHAIRGPU_BENCHMARK_FRAMES=${HAIRGPU_MEASURE_FRAMES}\nHAIRGPU_AUTO_TELEMETRY=1\nHAIRGPU_TELEMETRY_DIR=${PRESET_DIR}\nHAIRGPU_TELEMETRY_RUN_NAME=${PRESET}\n")
    file(WRITE "${PRESET_DIR}/${PRESET}_frames.csv" "sampleIndex,elapsedSeconds,frameMilliseconds,simulationMilliseconds,gpuFrameMilliseconds,gpuSimulationMilliseconds,gpuHairDrawMilliseconds,gpuDebugDrawMilliseconds,fps,guideCount,followerCount,totalStrandCount,totalParticleCount,activeSetEvaluationMilliseconds,solverIterations,selfCollisionEnabled,gridSelfCollisionEnabled,cpuReadbackMode,readbackParticleCount,hairPrimitiveCount,hairPsInvocations,debugPsInvocations,status\n")
    file(WRITE "${PRESET_DIR}/${PRESET}_summary.json" "{\n  \"preset\": \"${PRESET}\",\n  \"status\": \"not_executed_by_policy_safe_build\",\n  \"real_fps_available\": false\n}\n")
    file(APPEND "${HAIRGPU_RUN_DIR}/RESULT.csv" "${HAIRGPU_HUMAN},${PRESET},created_policy_safe,skipped,no,,,,,compare after EXE is allowed\n")
endforeach()

file(WRITE "${HAIRGPU_RUN_DIR}/RESULT.md" "# HairGPU Step 0 Complete Profile / Lightweight Setting Check\n\n")
file(APPEND "${HAIRGPU_RUN_DIR}/RESULT.md" "CreatedAt: ${HAIRGPU_HUMAN}\nTimestamp: ${HAIRGPU_STAMP}\nRunDir: ${HAIRGPU_RUN_DIR}\nTarget: ${HAIRGPU_TARGET_FILE}\n\n")
file(APPEND "${HAIRGPU_RUN_DIR}/RESULT.md" "## Status\n\n- profile_results creation: PASS\n- preset directories creation: PASS\n- CMake/MSBuild build hook: PASS\n- PowerShell: NOT_USED\n- BAT: NOT_USED\n- Local EXE execution during build: NOT_USED_BY_DEFAULT\n- real FPS measurement: NOT_AVAILABLE_UNTIL_WINDOWS_POLICY_ALLOWS_THE_EXE\n\n")
file(APPEND "${HAIRGPU_RUN_DIR}/RESULT.md" "## Presets\n\n| Preset | Purpose |\n|---|---|\n| 00_current | Current settings baseline |\n| 01_no_readback | Readback/sync bottleneck isolation |\n| 02_no_self_collision | Self collision bottleneck isolation |\n| 03_low_solver | Solver iteration bottleneck isolation |\n| 04_no_debug_draw | Debug draw bottleneck isolation |\n| 05_render_light | Rendering/overdraw bottleneck isolation |\n| 06_simulation_light | Simulation-light lower bound |\n\n")
file(APPEND "${HAIRGPU_RUN_DIR}/RESULT.md" "## Diagnosis thresholds after real run\n\n- >= 5 ms improvement: suspicious\n- >= 10 ms improvement: strong bottleneck candidate\n- >= 20 ms improvement: primary bottleneck candidate\n\n")

file(WRITE "${HAIRGPU_RUN_DIR}/WINDOWS_POLICY_NOTE.txt" "The viewer executable was not launched. This prevents Windows Application Control/AppLocker/WDAC from blocking the build step. Real FPS requires the executable to be allowed by Windows policy.\n")
file(WRITE "${HAIRGPU_RUN_DIR}/REAL_MEASUREMENT_ENABLE_STEPS.md" "# Enabling real Step 0 measurement\n\nAllow the built HairSystem executable, then run the viewer with HAIRGPU_BENCHMARK=1, HAIRGPU_AUTO_TELEMETRY=1, HAIRGPU_BENCHMARK_WARMUP=${HAIRGPU_WARMUP_FRAMES}, and HAIRGPU_BENCHMARK_FRAMES=${HAIRGPU_MEASURE_FRAMES}.\n")

message(STATUS "HairGPU Step0 complete policy-safe profile_results created: ${HAIRGPU_RUN_DIR}")
