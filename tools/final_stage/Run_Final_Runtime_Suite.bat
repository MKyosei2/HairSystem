@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" goto :usage
if "%~2"=="" goto :usage

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "ORIGINAL_CONFIG=%~f1"
set "MODE=%~2"
set "REQUESTED_OUTPUT=%~3"

if not exist "%ORIGINAL_CONFIG%" (
  echo ERROR: suite config was not found: "%ORIGINAL_CONFIG%"
  exit /b 2
)

if /I not "%MODE%"=="prepare-asset" if /I not "%MODE%"=="prenovelty-smoke" if /I not "%MODE%"=="startup20" if /I not "%MODE%"=="prenovelty-complete" if /I not "%MODE%"=="prenovelty-soak30" if /I not "%MODE%"=="projection4070-to-1660" if /I not "%MODE%"=="phase13-full" if /I not "%MODE%"=="proposal-smoke" if /I not "%MODE%"=="comparison-smoke" if /I not "%MODE%"=="critical" if /I not "%MODE%"=="baseline" if /I not "%MODE%"=="full" if /I not "%MODE%"=="determinism" if /I not "%MODE%"=="soak30" if /I not "%MODE%"=="soak2h" if /I not "%MODE%"=="soak8h" if /I not "%MODE%"=="all" (
  echo ERROR: unsupported mode "%MODE%".
  exit /b 2
)

where python >nul 2>nul
if errorlevel 1 (
  echo ERROR: python.exe was not found in PATH.
  exit /b 3
)
python -c "import sys; raise SystemExit(0 if sys.version_info >= (3,10) else 1)"
if errorlevel 1 (
  echo ERROR: Python 3.10 or later is required.
  exit /b 4
)
set "PYTHONHASHSEED=0"
set "PYTHONUTF8=1"

rem Composite modes recurse through this same entry point so every sub-run uses
rem the exact same detected branch and commit context.
if /I "%MODE%"=="phase13-full" (
  call "%~f0" "%ORIGINAL_CONFIG%" prepare-asset "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" proposal-smoke "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" comparison-smoke "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" critical "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" determinism "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" soak30 "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" soak2h "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" soak8h "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" full "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" projection4070-to-1660 "%REQUESTED_OUTPUT%"
  exit /b !errorlevel!
)
if /I "%MODE%"=="prenovelty-complete" (
  call "%~f0" "%ORIGINAL_CONFIG%" prepare-asset "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" prenovelty-smoke "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" startup20 "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" determinism "%REQUESTED_OUTPUT%"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%ORIGINAL_CONFIG%" prenovelty-soak30 "%REQUESTED_OUTPUT%"
  exit /b !errorlevel!
)

rem Detect the named work branch before creating any run files.  A detached
rem checkout is rejected locally because branch identity is part of the formal
rem evidence contract.  GitHub Actions can provide GITHUB_HEAD_REF/REF_NAME.
set "BOOTSTRAP_BRANCH="
for /f "usebackq delims=" %%B in (`git -C "%ROOT%" branch --show-current 2^>nul`) do set "BOOTSTRAP_BRANCH=%%B"
if not defined BOOTSTRAP_BRANCH if defined GITHUB_HEAD_REF set "BOOTSTRAP_BRANCH=%GITHUB_HEAD_REF%"
if not defined BOOTSTRAP_BRANCH if defined GITHUB_REF_NAME set "BOOTSTRAP_BRANCH=%GITHUB_REF_NAME%"
if not defined BOOTSTRAP_BRANCH (
  echo ERROR: a named current Git branch could not be detected.
  echo Check out the branch that must own this evidence and run the suite again.
  exit /b 11
)
set "BOOTSTRAP_COMMIT="
for /f "usebackq delims=" %%S in (`git -C "%ROOT%" rev-parse HEAD 2^>nul`) do set "BOOTSTRAP_COMMIT=%%S"
if not defined BOOTSTRAP_COMMIT (
  echo ERROR: current Git commit could not be detected.
  exit /b 11
)
set "BRANCH_COMPONENT=%BOOTSTRAP_BRANCH:/=_%"
set "BRANCH_COMPONENT=%BRANCH_COMPONENT:\=_%"
set "SHORT_COMMIT=%BOOTSTRAP_COMMIT:~0,12%"

rem When the caller omits an output directory, isolate evidence automatically by
rem branch and commit.  This prevents results from future novelty branches from
rem being mixed with the pre-novelty baseline.
if not defined REQUESTED_OUTPUT (
  set "BASE_OUTPUT="
  for /f "usebackq delims=" %%O in (`python -c "import json,pathlib; c=pathlib.Path(r'%ORIGINAL_CONFIG%').resolve(); j=json.loads(c.read_text(encoding='utf-8-sig')); p=pathlib.Path(j.get('outputs_dir','../../out/runtime_suite')); print((p if p.is_absolute() else c.parent/p).resolve())"`) do set "BASE_OUTPUT=%%O"
  if not defined BASE_OUTPUT (
    echo ERROR: outputs_dir could not be resolved from the suite config.
    exit /b 12
  )
  set "REQUESTED_OUTPUT=!BASE_OUTPUT!\!BRANCH_COMPONENT!\!SHORT_COMMIT!"
)
for %%I in ("%REQUESTED_OUTPUT%") do set "REQUESTED_OUTPUT=%%~fI"

set "BRANCH_ENV=%TEMP%\hairgpu_branch_context_%RANDOM%_%RANDOM%.env"
python "%~dp0hairgpu_branch_context.py" ^
  --repo-root "%ROOT%" ^
  --config "%ORIGINAL_CONFIG%" ^
  --output-dir "%REQUESTED_OUTPUT%" ^
  --env-file "%BRANCH_ENV%"
if errorlevel 1 (
  if exist "%BRANCH_ENV%" del /Q "%BRANCH_ENV%"
  exit /b 11
)
if not exist "%BRANCH_ENV%" (
  echo ERROR: branch context resolver did not create its environment file.
  exit /b 11
)
for /f "usebackq tokens=1,* delims==" %%A in ("%BRANCH_ENV%") do set "%%A=%%B"
del /Q "%BRANCH_ENV%"
if not defined HAIRGPU_RESOLVED_CONFIG (
  echo ERROR: resolved suite configuration path is missing.
  exit /b 11
)
if not defined HAIRGPU_RESOLVED_OUTPUT (
  echo ERROR: resolved suite output path is missing.
  exit /b 11
)
set "CONFIG=%HAIRGPU_RESOLVED_CONFIG%"
set "OUTPUT_DIR=%HAIRGPU_RESOLVED_OUTPUT%"
set "OUTPUT_ARG=--output "%OUTPUT_DIR%""
echo Active branch : %HAIRGPU_DETECTED_BRANCH%
echo Pinned commit : %HAIRGPU_DETECTED_COMMIT%
echo Resolved config: %CONFIG%
echo Output         : %OUTPUT_DIR%

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe was not found.
  exit /b 5
)
set "MSBUILD="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
  set "MSBUILD=%%I"
  goto :msbuild_found
)
:msbuild_found
if not defined MSBUILD (
  echo ERROR: MSBuild.exe was not found.
  exit /b 6
)

set "VIEWER_DIR=%ROOT%\bin\Release\HairSystem"
for %%F in (PENDING_STEP0_REAL.ini STEP0_FORCE_RUN.ini STEP0_REAL_MEASUREMENT_PENDING.txt STEP0_REAL_MEASUREMENT_READY.txt) do (
  if exist "%VIEWER_DIR%\%%F" del /Q "%VIEWER_DIR%\%%F"
  if exist "%VIEWER_DIR%\profile_results\%%F" del /Q "%VIEWER_DIR%\profile_results\%%F"
)

set "FILTER_ARGS="
set "RUN_KIND_ARGS=--run-kind formal"
if /I "%MODE%"=="prenovelty-smoke" set "FILTER_ARGS=--only-method B0 --only-method B6 --only-method B8 --only-scenario S01_Idle --only-scenario S03_Walk --only-scenario S09_StrongWind --only-scenario S17_CloseCamera100k --only-seed 1103"
if /I "%MODE%"=="prenovelty-soak30" set "RUN_KIND_ARGS=--run-kind long-run"
if /I "%MODE%"=="proposal-smoke" set "FILTER_ARGS=--only-method P --only-scenario S01_Idle --only-seed 1103"
if /I "%MODE%"=="comparison-smoke" set "FILTER_ARGS=--only-method B0 --only-method P --only-scenario S01_Idle --only-seed 1103"
if /I "%MODE%"=="critical" set "FILTER_ARGS=--only-method B0 --only-method B8 --only-method P --only-scenario S01_Idle --only-scenario S07_ColliderApproach --only-scenario S08_StaticDynamicBoundaryContact --only-scenario S12_FalseStaticTrap --only-scenario S13_RepairStorm --only-scenario S16_StateThrashing --only-scenario S17_CloseCamera100k --only-seed 1103"
if /I "%MODE%"=="baseline" set "FILTER_ARGS=--only-method B0"
if /I "%MODE%"=="full" set "FILTER_ARGS="
if /I "%MODE%"=="all" set "RUN_KIND_ARGS=--run-kind all"
if /I "%MODE%"=="determinism" set "RUN_KIND_ARGS=--run-kind determinism"
if /I "%MODE%"=="soak30" (
  set "RUN_KIND_ARGS=--run-kind long-run"
  set "FILTER_ARGS=--only-run-name L30_Walk"
)
if /I "%MODE%"=="soak2h" (
  set "RUN_KIND_ARGS=--run-kind long-run"
  set "FILTER_ARGS=--only-run-name L2H_StrongWind"
)
if /I "%MODE%"=="soak8h" (
  set "RUN_KIND_ARGS=--run-kind long-run"
  set "FILTER_ARGS=--only-run-name L8H_StateThrashing"
)

set "FROZEN_BUNDLE=%ROOT%\benchmarks\novelty01\frozen_asset_bundle"
if not defined HAIRGPU_FROZEN_ASSET_MANIFEST (
  for /f "usebackq delims=" %%M in (`python -c "import json,pathlib; c=pathlib.Path(r'%CONFIG%').resolve(); j=json.loads(c.read_text(encoding='utf-8-sig')); print(pathlib.Path(j['source']['asset_manifest']).resolve())"`) do set "HAIRGPU_FROZEN_ASSET_MANIFEST=%%M"
)
if /I not "%MODE%"=="prepare-asset" (
  if not exist "%HAIRGPU_FROZEN_ASSET_MANIFEST%" (
    echo ERROR: Frozen Asset Manifest was not found:
    echo   "%HAIRGPU_FROZEN_ASSET_MANIFEST%"
    echo Run this command first:
    echo   set HAIRGPU_FREEZE_OPERATOR=Your_Name
    echo   "%~f0" "%ORIGINAL_CONFIG%" prepare-asset "%OUTPUT_DIR%"
    exit /b 9
  )
)

echo [1/8] Rebuilding HairSystem Release x64 without Step0...
"%MSBUILD%" "%ROOT%\HairSystem.sln" /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:HairGpuStep0ProfileEnabled=false /p:HairGpuFormalBuild=true
if errorlevel 1 exit /b %errorlevel%
set "VIEWER=%VIEWER_DIR%\HairSystem.exe"
if not exist "%VIEWER%" (
  echo ERROR: Release viewer was not generated: "%VIEWER%"
  exit /b 7
)

if /I "%MODE%"=="startup20" goto :startup20
if /I "%MODE%"=="projection4070-to-1660" goto :projection4070_to_1660

echo [2/8] Running native ABI, canonical asset, and workload self-test...
"%VIEWER%" --native-self-test
if errorlevel 1 exit /b %errorlevel%

echo [3/8] Running HLSL load/compile self-test...
"%VIEWER%" --shader-self-test
if errorlevel 1 exit /b %errorlevel%

echo [4/8] Checking Python syntax...
python -m py_compile "%~dp0freeze_novelty01_asset.py" "%~dp0hairgpu_branch_context.py" "%~dp0hairgpu_final_run.py" "%~dp0hairgpu_runtime_suite.py" "%~dp0hairgpu_safe_runtime_suite.py" "%~dp0hairgpu_final_db.py" "%~dp0hairgpu_final_excel.py" "%~dp0hairgpu_startup_self_test.py"
if errorlevel 1 exit /b %errorlevel%

echo [5/8] Running lock, branch-context, telemetry-contract, and suite-selection self-tests...
python "%~dp0hairgpu_final_run.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python "%~dp0hairgpu_runtime_suite.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python -m unittest discover -s "%ROOT%\tests\final_stage" -p "test_*.py"
if errorlevel 1 exit /b %errorlevel%

if /I "%MODE%"=="prepare-asset" goto :prepare_asset

echo [6/8] Confirming active power scheme...
powercfg /GETACTIVESCHEME
if errorlevel 1 (
  echo ERROR: Failed to read the active Windows power scheme.
  exit /b 8
)

echo [7/8] Running strict dry-run preflight (%MODE%)...
python "%~dp0hairgpu_runtime_suite.py" --config "%CONFIG%" --dry-run --viewer-exe "%VIEWER%" %RUN_KIND_ARGS% %FILTER_ARGS% %OUTPUT_ARG%
if errorlevel 1 exit /b %errorlevel%

echo [8/8] Running Release x64 suite (%MODE%)...
python "%~dp0hairgpu_runtime_suite.py" --config "%CONFIG%" --resume --fail-policy continue --viewer-exe "%VIEWER%" %RUN_KIND_ARGS% %FILTER_ARGS% %OUTPUT_ARG%
set "RUN_EXIT=!ERRORLEVEL!"

git -C "%ROOT%" diff --exit-code
if errorlevel 1 (
  echo ERROR: the runtime suite modified tracked source files.
  exit /b 13
)
for /f "usebackq delims=" %%D in (`git -C "%ROOT%" status --porcelain --untracked-files=no`) do (
  echo ERROR: tracked worktree change remained after the runtime suite: %%D
  exit /b 13
)
exit /b !RUN_EXIT!

:prepare_asset
if not defined HAIRGPU_FREEZE_OPERATOR (
  echo ERROR: HAIRGPU_FREEZE_OPERATOR is required for prepare-asset.
  echo Example: set HAIRGPU_FREEZE_OPERATOR=Your_Name
  exit /b 10
)
echo [6/8] Exporting canonical 100k Hair Asset and topology from the Release viewer...
python "%~dp0freeze_novelty01_asset.py" ^
  --template-manifest "%CONFIG%" ^
  --bundle-dir "%FROZEN_BUNDLE%" ^
  --viewer-exe "%VIEWER%" ^
  --generate-source ^
  --operator "%HAIRGPU_FREEZE_OPERATOR%" ^
  --repo-root "%ROOT%" ^
  --force
if errorlevel 1 exit /b %errorlevel%
echo [7/8] Verifying portable bundle hashes and Viewer round trip...
python "%~dp0freeze_novelty01_asset.py" ^
  --bundle-dir "%FROZEN_BUNDLE%" ^
  --viewer-exe "%VIEWER%" ^
  --verify-only
if errorlevel 1 exit /b %errorlevel%
echo [8/8] Frozen Asset preparation completed.
echo Manifest: "%FROZEN_BUNDLE%\frozen_asset_manifest.json"
exit /b 0

:startup20
echo Running 20 real Viewer startup/render-loop/graceful-shutdown repetitions...
python "%~dp0hairgpu_startup_self_test.py" ^
  --viewer "%VIEWER%" ^
  --repetitions 20 ^
  --startup-timeout 120 ^
  --run-seconds 5 ^
  --shutdown-timeout 30 ^
  --report "%OUTPUT_DIR%\startup20_summary.json"
exit /b %errorlevel%

:projection4070_to_1660
set "PROJECTION_OUTPUT=%OUTPUT_DIR%"
if not defined PROJECTION_OUTPUT (
  echo ERROR: Failed to resolve the projection source output directory.
  exit /b 12
)
echo Generating separate theoretical GTX 1660 projection from actual RTX 4070 Ti SUPER evidence...
python "%~dp0hairgpu_runtime_suite.py" --emit-gtx1660-projection --config "%CONFIG%" --output "%PROJECTION_OUTPUT%"
if errorlevel 1 exit /b !errorlevel!
echo Projection report: "%PROJECTION_OUTPUT%\gtx1660_theoretical_projection.json"
echo NOTE: This report is theoretical only and is not GTX 1660 formal evidence.
exit /b 0

:usage
echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> ^<mode^> [output_dir]
echo.
echo Modes:
echo   prepare-asset, prenovelty-smoke, startup20, prenovelty-complete,
echo   prenovelty-soak30, projection4070-to-1660, phase13-full,
echo   proposal-smoke, comparison-smoke, critical, baseline, full,
echo   determinism, soak30, soak2h, soak8h, all
exit /b 1
