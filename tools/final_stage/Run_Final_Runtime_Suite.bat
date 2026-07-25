@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" (
  echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> ^<mode^> [output_dir]
  exit /b 1
)

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "CONFIG=%~f1"
set "MODE=%~2"

if "%MODE%"=="" (
  echo ERROR: mode is required.
  echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> ^<mode^> [output_dir]
  exit /b 2
)

if not exist "%CONFIG%" (
  echo ERROR: suite config was not found: "%CONFIG%"
  exit /b 2
)

rem ---------------------------------------------------------------------------
rem Composite modes. Every phase receives its own directory. The complete mode
rem accepts only after smoke, startup20, all-18-scenario coverage, determinism,
rem soak30, detailed DB import, Excel export, graph generation, and immutable
rem evidence packaging all pass.
rem ---------------------------------------------------------------------------
if /I "%MODE%"=="prenovelty-complete" (
  where python >nul 2>nul
  if errorlevel 1 (
    echo ERROR: python.exe was not found in PATH.
    exit /b 3
  )
  python -c "import openpyxl" >nul 2>nul
  if errorlevel 1 (
    echo ERROR: openpyxl is required for thesis Excel export.
    echo Install it before the formal run: python -m pip install openpyxl==3.1.5
    exit /b 13
  )

  set "COMPLETE_ROOT=%~f3"
  if not defined COMPLETE_ROOT set "COMPLETE_ROOT=%ROOT%\out\prenovelty_complete"
  if not exist "!COMPLETE_ROOT!" mkdir "!COMPLETE_ROOT!"

  call "%~f0" "%CONFIG%" prepare-asset
  if errorlevel 1 exit /b !errorlevel!

  call "%~f0" "%CONFIG%" prenovelty-smoke "!COMPLETE_ROOT!\smoke"
  if errorlevel 1 exit /b !errorlevel!

  call "%~f0" "%CONFIG%" startup20 "!COMPLETE_ROOT!\startup20"
  if errorlevel 1 exit /b !errorlevel!

  call "%~f0" "%CONFIG%" prenovelty-coverage18 "!COMPLETE_ROOT!\coverage18"
  if errorlevel 1 exit /b !errorlevel!

  call "%~f0" "%CONFIG%" determinism "!COMPLETE_ROOT!\determinism"
  if errorlevel 1 exit /b !errorlevel!

  call "%~f0" "%CONFIG%" prenovelty-soak30 "!COMPLETE_ROOT!\soak30"
  if errorlevel 1 exit /b !errorlevel!

  python "%~dp0hairgpu_prenovelty_complete_v2.py" ^
    --root "!COMPLETE_ROOT!" ^
    --config "%CONFIG%" ^
    --repo-root "%ROOT%" ^
    --coverage-seed 1103
  if errorlevel 1 exit /b !errorlevel!

  echo Pre-novelty complete evidence:
  echo   "!COMPLETE_ROOT!\complete_manifest.json"
  echo   "!COMPLETE_ROOT!\prenovelty_complete_evidence.zip"
  echo   "!COMPLETE_ROOT!\prenovelty_complete_evidence.zip.sha256"
  echo   "!COMPLETE_ROOT!\frozen_asset_bundle_snapshot.zip"
  echo   "!COMPLETE_ROOT!\analysis\validated_evidence.sqlite"
  echo   "!COMPLETE_ROOT!\analysis\formal_results.xlsx"
  echo   "!COMPLETE_ROOT!\analysis\graphs"
  exit /b 0
)

if /I "%MODE%"=="phase13-full" (
  set "PHASE13_ROOT=%~f3"
  if not defined PHASE13_ROOT set "PHASE13_ROOT=%ROOT%\out\phase13_full"
  if not exist "!PHASE13_ROOT!" mkdir "!PHASE13_ROOT!"

  call "%~f0" "%CONFIG%" prepare-asset
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" proposal-smoke "!PHASE13_ROOT!\proposal_smoke"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" comparison-smoke "!PHASE13_ROOT!\comparison_smoke"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" critical "!PHASE13_ROOT!\critical"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" determinism "!PHASE13_ROOT!\determinism"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" soak30 "!PHASE13_ROOT!\soak30"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" soak2h "!PHASE13_ROOT!\soak2h"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" soak8h "!PHASE13_ROOT!\soak8h"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" full "!PHASE13_ROOT!\full"
  if errorlevel 1 exit /b !errorlevel!
  call "%~f0" "%CONFIG%" projection4070-to-1660 "!PHASE13_ROOT!\full"
  exit /b !errorlevel!
)

set "OUTPUT_ARG="
if not "%~3"=="" set "OUTPUT_ARG=--output "%~f3""

if /I not "%MODE%"=="prepare-asset" if /I not "%MODE%"=="refreeze-asset" if /I not "%MODE%"=="prenovelty-soak30" if /I not "%MODE%"=="projection4070-to-1660" if /I not "%MODE%"=="prenovelty-smoke" if /I not "%MODE%"=="prenovelty-coverage18" if /I not "%MODE%"=="startup20" if /I not "%MODE%"=="proposal-smoke" if /I not "%MODE%"=="comparison-smoke" if /I not "%MODE%"=="critical" if /I not "%MODE%"=="baseline" if /I not "%MODE%"=="full" if /I not "%MODE%"=="determinism" if /I not "%MODE%"=="soak30" if /I not "%MODE%"=="soak2h" if /I not "%MODE%"=="soak8h" if /I not "%MODE%"=="all" (
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
if /I "%MODE%"=="prenovelty-coverage18" set "FILTER_ARGS=--only-seed 1103"
if /I "%MODE%"=="prenovelty-soak30" (
  set "RUN_KIND_ARGS=--run-kind long-run"
  set "FILTER_ARGS=--only-run-name L30_B0_Walk --only-run-name L30_B6_Walk --only-run-name L30_B8_Walk --only-run-name L30_B8_StrongWind"
)
if /I "%MODE%"=="proposal-smoke" set "FILTER_ARGS=--only-method P --only-scenario S01_Idle --only-seed 1103"
if /I "%MODE%"=="comparison-smoke" set "FILTER_ARGS=--only-method B0 --only-method P --only-scenario S01_Idle --only-seed 1103"
if /I "%MODE%"=="critical" set "FILTER_ARGS=--only-method B0 --only-method B8 --only-method P --only-scenario S01_Idle --only-scenario S07_ColliderApproach --only-scenario S08_StaticDynamicBoundaryContact --only-scenario S12_FalseStaticTrap --only-scenario S13_RepairStorm --only-scenario S16_StateThrashing --only-scenario S17_CloseCamera100k --only-seed 1103"
if /I "%MODE%"=="baseline" set "FILTER_ARGS=--only-method B0"
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
  for /f "usebackq delims=" %%M in (`python -c "import json,pathlib; c=pathlib.Path(r'%CONFIG%').resolve(); j=json.loads(c.read_text(encoding='utf-8-sig')); p=pathlib.Path(j['source']['asset_manifest']); print((p if p.is_absolute() else c.parent/p).resolve())"`) do set "HAIRGPU_FROZEN_ASSET_MANIFEST=%%M"
)

rem Asset creation/refreeze intentionally does not require an existing manifest.
if /I not "%MODE%"=="prepare-asset" if /I not "%MODE%"=="refreeze-asset" (
  if not exist "%HAIRGPU_FROZEN_ASSET_MANIFEST%" (
    echo ERROR: Frozen Asset Manifest was not found:
    echo   "%HAIRGPU_FROZEN_ASSET_MANIFEST%"
    echo Run this command first:
    echo   set HAIRGPU_FREEZE_OPERATOR=Your_Name
    echo   "%~f0" "%CONFIG%" prepare-asset
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
python -m py_compile "%~dp0freeze_novelty01_asset.py" "%~dp0hairgpu_final_run.py" "%~dp0hairgpu_runtime_suite.py" "%~dp0hairgpu_safe_runtime_suite.py" "%~dp0hairgpu_final_db.py" "%~dp0hairgpu_final_excel.py" "%~dp0hairgpu_prenovelty_complete.py" "%~dp0hairgpu_prenovelty_complete_v2.py" "%~dp0hairgpu_frozen_bundle_lock.py"
if errorlevel 1 exit /b %errorlevel%

echo [5/8] Running lock, telemetry-contract, suite-selection, and aggregate self-tests...
python "%~dp0hairgpu_final_run.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python "%~dp0hairgpu_runtime_suite.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python "%~dp0hairgpu_prenovelty_complete.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python "%~dp0hairgpu_prenovelty_complete_v2.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python "%~dp0hairgpu_frozen_bundle_lock.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python -m unittest discover -s "%ROOT%\tests\final_stage" -p "test_*.py"
if errorlevel 1 exit /b %errorlevel%

if /I "%MODE%"=="prepare-asset" goto :prepare_asset
if /I "%MODE%"=="refreeze-asset" goto :refreeze_asset

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
exit /b %errorlevel%

:prepare_asset
echo [6/8] Ensuring one immutable canonical 100k Frozen Asset bundle...
python "%~dp0hairgpu_frozen_bundle_lock.py" ^
  --ensure ^
  --bundle-dir "%FROZEN_BUNDLE%" ^
  --config "%CONFIG%" ^
  --viewer "%VIEWER%" ^
  --freeze-tool "%~dp0freeze_novelty01_asset.py" ^
  --operator "%HAIRGPU_FREEZE_OPERATOR%" ^
  --repo-root "%ROOT%"
if errorlevel 1 exit /b %errorlevel%
echo [8/8] Frozen Asset bundle verified without implicit regeneration.
echo Manifest: "%FROZEN_BUNDLE%\frozen_asset_manifest.json"
echo Lock: "%FROZEN_BUNDLE%\frozen_bundle_lock.json"
exit /b 0

:refreeze_asset
if not defined HAIRGPU_FREEZE_OPERATOR (
  echo ERROR: HAIRGPU_FREEZE_OPERATOR is required for explicit refreeze.
  exit /b 10
)
echo [6/8] Explicitly regenerating and re-locking the canonical 100k bundle...
python "%~dp0hairgpu_frozen_bundle_lock.py" ^
  --refreeze ^
  --bundle-dir "%FROZEN_BUNDLE%" ^
  --config "%CONFIG%" ^
  --viewer "%VIEWER%" ^
  --freeze-tool "%~dp0freeze_novelty01_asset.py" ^
  --operator "%HAIRGPU_FREEZE_OPERATOR%" ^
  --repo-root "%ROOT%"
exit /b %errorlevel%

:startup20
set "STARTUP_REPORT=%ROOT%\out\startup20\startup20_summary.json"
if not "%~3"=="" set "STARTUP_REPORT=%~f3\startup20_summary.json"
for %%I in ("%STARTUP_REPORT%") do if not exist "%%~dpI" mkdir "%%~dpI"
echo Running 20 real Viewer startup/render-loop/shutdown repetitions...
python "%~dp0hairgpu_startup_self_test.py" ^
  --viewer "%VIEWER%" ^
  --repetitions 20 ^
  --startup-timeout 120 ^
  --run-seconds 5 ^
  --shutdown-timeout 30 ^
  --report "%STARTUP_REPORT%"
exit /b %errorlevel%

:projection4070_to_1660
set "PROJECTION_OUTPUT=%~f3"
if "%~3"=="" (
  for /f "usebackq delims=" %%O in (`python -c "import json,pathlib; c=pathlib.Path(r'%CONFIG%').resolve(); j=json.loads(c.read_text(encoding='utf-8-sig')); p=pathlib.Path(j.get('outputs_dir','../../out/novelty01_baseline')); print((p if p.is_absolute() else c.parent/p).resolve())"`) do set "PROJECTION_OUTPUT=%%O"
)
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
