@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" (
  echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> [proposal-smoke^|comparison-smoke^|critical^|baseline^|full^|determinism^|soak30^|soak2h^|soak8h] [output_dir]
  exit /b 1
)

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "CONFIG=%~f1"
set "MODE=%~2"
if "%MODE%"=="" set "MODE=proposal-smoke"
set "OUTPUT_ARG="
if not "%~3"=="" set "OUTPUT_ARG=--output "%~f3""

if /I not "%MODE%"=="proposal-smoke" if /I not "%MODE%"=="comparison-smoke" if /I not "%MODE%"=="critical" if /I not "%MODE%"=="baseline" if /I not "%MODE%"=="full" if /I not "%MODE%"=="determinism" if /I not "%MODE%"=="soak30" if /I not "%MODE%"=="soak2h" if /I not "%MODE%"=="soak8h" (
  echo ERROR: unsupported mode "%MODE%".
  exit /b 2
)

where python >nul 2>nul
if errorlevel 1 (
  echo ERROR: python.exe was not found in PATH.
  exit /b 3
)
for /f "delims=" %%V in ('python -c "import sys; print(sys.executable); print('.'.join(map(str,sys.version_info[:3])))"') do echo Python: %%V
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
if /I "%MODE%"=="proposal-smoke" set "FILTER_ARGS=--only-method P --only-scenario S01_Idle --only-seed 1103"
if /I "%MODE%"=="comparison-smoke" set "FILTER_ARGS=--only-method B0 --only-method P --only-scenario S01_Idle --only-seed 1103"
if /I "%MODE%"=="critical" set "FILTER_ARGS=--only-method B0 --only-method B8 --only-method P --only-scenario S01_Idle --only-scenario S07_ColliderApproach --only-scenario S08_StaticDynamicBoundaryContact --only-scenario S12_FalseStaticTrap --only-scenario S13_RepairStorm --only-scenario S16_StateThrashing --only-scenario S17_CloseCamera100k --only-seed 1103"
if /I "%MODE%"=="baseline" set "FILTER_ARGS=--only-method B0"
if /I "%MODE%"=="full" set "FILTER_ARGS="
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

rem Resolve the portable frozen bundle. Formal measurement never auto-generates it.
if not defined HAIRGPU_FROZEN_ASSET_MANIFEST (
  for /f "usebackq delims=" %%M in (`python -c "import json,pathlib; c=pathlib.Path(r'%CONFIG%').resolve(); j=json.loads(c.read_text(encoding='utf-8-sig')); p=pathlib.Path(j['source']['asset_manifest']); print((p if p.is_absolute() else c.parent/p).resolve())"`) do set "HAIRGPU_FROZEN_ASSET_MANIFEST=%%M"
)
if not exist "%HAIRGPU_FROZEN_ASSET_MANIFEST%" (
  echo ERROR: Frozen Asset Manifest was not found:
  echo   "%HAIRGPU_FROZEN_ASSET_MANIFEST%"
  echo Generate it explicitly with freeze_novelty01_asset.py before formal measurement.
  exit /b 9
)

echo [1/8] Rebuilding HairSystem Release x64 without Step0...
"%MSBUILD%" "%ROOT%\HairSystem.sln" /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:HairGpuStep0ProfileEnabled=false /p:HairGpuFormalBuild=true
if errorlevel 1 exit /b %errorlevel%
set "VIEWER=%VIEWER_DIR%\HairSystem.exe"
if not exist "%VIEWER%" (
  echo ERROR: Release viewer was not generated: "%VIEWER%"
  exit /b 7
)

echo [2/8] Running native ABI and workload self-test...
"%VIEWER%" --native-self-test
if errorlevel 1 exit /b %errorlevel%

echo [3/8] Running HLSL load/compile self-test...
"%VIEWER%" --shader-self-test
if errorlevel 1 exit /b %errorlevel%

echo [4/8] Checking Python syntax...
python -m py_compile "%~dp0freeze_novelty01_asset.py" "%~dp0hairgpu_final_run.py" "%~dp0hairgpu_runtime_suite.py" "%~dp0hairgpu_safe_runtime_suite.py" "%~dp0hairgpu_final_db.py" "%~dp0hairgpu_final_excel.py"
if errorlevel 1 exit /b %errorlevel%

echo [5/8] Running lock, telemetry-contract, and suite-selection self-tests...
python "%~dp0hairgpu_final_run.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python "%~dp0hairgpu_runtime_suite.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python -m unittest discover -s "%ROOT%\tests\final_stage" -p "test_*.py"
if errorlevel 1 exit /b %errorlevel%

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
