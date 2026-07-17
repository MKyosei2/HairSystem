@echo off
setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0\..\..") do set "ROOT=%%~fI"
set "REPEATS=%~1"
set "WARMUP_FRAMES=%~2"
set "MEASUREMENT_FRAMES=%~3"
if not defined REPEATS set "REPEATS=3"
if not defined WARMUP_FRAMES set "WARMUP_FRAMES=600"
if not defined MEASUREMENT_FRAMES set "MEASUREMENT_FRAMES=6000"

set "PYTHON_CMD="
where py >nul 2>nul
if not errorlevel 1 set "PYTHON_CMD=py -3"
if not defined PYTHON_CMD (
    where python >nul 2>nul
    if not errorlevel 1 set "PYTHON_CMD=python"
)
if not defined PYTHON_CMD (
    echo [ERROR] Python 3 was not found.
    exit /b 10
)

set "MSBUILD_EXE="
for /f "delims=" %%M in ('where msbuild 2^>nul') do if not defined MSBUILD_EXE set "MSBUILD_EXE=%%M"
if not defined MSBUILD_EXE (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%M in (`"!VSWHERE!" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD_EXE set "MSBUILD_EXE=%%M"
    )
)
if not defined MSBUILD_EXE (
    echo [ERROR] MSBuild was not found. Run this from a Visual Studio 2022 Developer Command Prompt.
    exit /b 11
)

if not exist "%ROOT%\HairSystem.sln" (
    echo [ERROR] HairSystem.sln was not found: %ROOT%\HairSystem.sln
    exit /b 12
)
if not exist "%ROOT%\tools\final_stage\validate_prenovelty_step0.py" (
    echo [ERROR] Baseline validator is missing.
    exit /b 13
)

for /f %%T in ('%PYTHON_CMD% -c "import datetime; print(datetime.datetime.now().strftime('%%Y%%m%%d_%%H%%M%%S'))"') do set "SESSION_ID=%%T"
set "SESSION_DIR=%ROOT%\profile_results\PRENOVELTY_SESSION_!SESSION_ID!"
set "SESSION_RUNS=!SESSION_DIR!\run_directories.txt"
mkdir "!SESSION_DIR!" >nul 2>nul
type nul > "!SESSION_RUNS!"

(
    echo session_id=!SESSION_ID!
    echo repeats=%REPEATS%
    echo warmup_frames=%WARMUP_FRAMES%
    echo measurement_frames=%MEASUREMENT_FRAMES%
    echo configuration=Release
    echo platform=x64
    echo baseline=B0_no_novelty_flags
) > "!SESSION_DIR!\SESSION_CONFIG.txt"

git -C "%ROOT%" rev-parse HEAD > "!SESSION_DIR!\SOURCE_COMMIT.txt" 2>nul
git -C "%ROOT%" status --porcelain --untracked-files=normal > "!SESSION_DIR!\SOURCE_WORKTREE_STATUS.txt" 2>nul
for %%S in ("!SESSION_DIR!\SOURCE_WORKTREE_STATUS.txt") do if %%~zS GTR 0 (
    echo [WARN] The worktree is not clean. Results remain useful for development, but are not formal paper evidence.
)

rem Explicit B0 / pre-novelty feature state. Step0 diagnostic presets inherit these values.
set "HAIRGPU_ENABLE_NO_NOVELTY_BASELINE=1"
set "HAIRGPU_ENABLE_MICROTYPE=0"
set "HAIRGPU_ENABLE_BUDGETED_PHYSICS=0"
set "HAIRGPU_ENABLE_CERTIFICATE_RENDER=0"
set "HAIRGPU_ENABLE_PER_STRAND_EDIT=0"
set "HAIRGPU_ENABLE_CBMA=0"
set "HAIRGPU_ENABLE_PHYSICS_ISLANDS=0"
set "HAIRGPU_ENABLE_ROD_REPAIR=0"
set "HAIRGPU_ENABLE_GPU_RESIDENT=0"
set "HAIRGPU_ENABLE_SEGMENT_GROUP_SLEEP=0"
set "HAIRGPU_ENABLE_SEGMENT_GROUP_INTEGRATED_STATE=0"
set "HAIRGPU_ENABLE_EXTERNAL_STATE_ARRAY=0"
set "HAIRGPU_ENABLE_SHARED_POINT_OWNERSHIP=0"
set "HAIRGPU_ENABLE_DIRECTIONAL_RESPONSIBILITY=0"
set "HAIRGPU_ENABLE_STATIC_CERTIFICATE=0"
set "HAIRGPU_ENABLE_PREDICTIVE_WAKE=0"
set "HAIRGPU_ENABLE_VELOCITY_RECOVERY=0"
set "HAIRGPU_ENABLE_REPAIR=0"
set "HAIRGPU_ENABLE_GPU_COMPACTION=0"
set "HAIRGPU_ENABLE_ACTIVE_SET=0"
set "HAIRGPU_ENABLE_STRAND_SLEEP=0"
set "HAIRGPU_ENABLE_BLOCK_SLEEP=0"
set "HAIRGPU_ENABLE_SLEEP_HYSTERESIS=0"
set "HAIRGPU_ENABLE_STATIC_CACHE=0"
set "HAIRGPU_PRENOVELTY_BASELINE=1"

set "EXE=%ROOT%\bin\Release\HairSystem\HairSystem.exe"
set "EXE_DIR=%ROOT%\bin\Release\HairSystem"

for /L %%R in (1,1,%REPEATS%) do (
    echo.
    echo ============================================================
    echo [Pre-Novelty] Repetition %%R of %REPEATS%
    echo ============================================================

    set "REP_STAMP=!SESSION_ID!_r%%R"
    "!MSBUILD_EXE!" "%ROOT%\HairSystem.sln" /m /t:Build /nologo /verbosity:minimal ^
        /p:Configuration=Release /p:Platform=x64 ^
        /p:HairGpuStep0ProfileEnabled=true ^
        /p:HairGpuStep0WarmupFrames=%WARMUP_FRAMES% ^
        /p:HairGpuStep0MeasureFrames=%MEASUREMENT_FRAMES% ^
        /p:HairGpuProfileTimestamp=!REP_STAMP!
    if errorlevel 1 (
        echo [ERROR] Release x64 build failed during repetition %%R.
        exit /b 20
    )

    if not exist "!EXE!" (
        echo [ERROR] Release executable was not produced: !EXE!
        exit /b 21
    )
    if not exist "%ROOT%\profile_results\LATEST.txt" (
        echo [ERROR] profile_results\LATEST.txt was not produced.
        exit /b 22
    )

    set "RUN_DIR="
    for /f "usebackq tokens=1 delims=;" %%D in ("%ROOT%\profile_results\LATEST.txt") do if not defined RUN_DIR set "RUN_DIR=%%D"
    if not defined RUN_DIR (
        echo [ERROR] Could not resolve the Step0 run directory.
        exit /b 23
    )
    if not exist "!RUN_DIR!" (
        echo [ERROR] Step0 run directory does not exist: !RUN_DIR!
        exit /b 24
    )

    echo [INFO] Launching real measurement: !EXE!
    echo [INFO] Output directory: !RUN_DIR!
    pushd "!EXE_DIR!"
    start "" /wait "!EXE!"
    set "VIEWER_RC=!ERRORLEVEL!"
    popd
    if not "!VIEWER_RC!"=="0" (
        echo [ERROR] HairSystem.exe exited with code !VIEWER_RC!.
        echo [HINT] Check Windows Application Control, AppLocker, WDAC, antivirus, and the run directory.
        exit /b 25
    )

    %PYTHON_CMD% "%ROOT%\tools\final_stage\validate_prenovelty_step0.py" ^
        --run-dir "!RUN_DIR!" ^
        --minimum-frames %MEASUREMENT_FRAMES% ^
        --label "pre-novelty-repetition-%%R"
    if errorlevel 1 (
        echo [ERROR] Repetition %%R did not produce valid real frame data.
        echo [INFO] Inspect: !RUN_DIR!\PRENOVELTY_VALIDATION.md
        exit /b 26
    )

    >> "!SESSION_RUNS!" echo !RUN_DIR!
    echo [PASS] Repetition %%R completed and validated.
)

%PYTHON_CMD% "%ROOT%\tools\final_stage\validate_prenovelty_step0.py" ^
    --aggregate-list "!SESSION_RUNS!" ^
    --output-dir "!SESSION_DIR!"
if errorlevel 1 (
    echo [ERROR] Session aggregation found one or more invalid repetitions.
    exit /b 27
)

(
    echo status=complete_valid
    echo completed_repetitions=%REPEATS%
    echo session_directory=!SESSION_DIR!
) > "!SESSION_DIR!\PRENOVELTY_BASELINE_COMPLETE.txt"

echo.
echo ============================================================
echo [PASS] Pre-novelty baseline collection completed.
echo [RESULT] !SESSION_DIR!
echo [SUMMARY] !SESSION_DIR!\PRENOVELTY_SESSION_SUMMARY.json
echo ============================================================
exit /b 0
