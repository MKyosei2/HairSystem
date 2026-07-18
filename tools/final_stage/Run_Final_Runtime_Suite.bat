@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" (
  echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> [smoke^|baseline^|full^|soak30^|soak2h^|soak8h] [output_dir]
  exit /b 1
)

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "CONFIG=%~f1"
set "MODE=%~2"
if "%MODE%"=="" set "MODE=baseline"
set "OUTPUT_ARG="
if not "%~3"=="" set "OUTPUT_ARG=--output "%~f3""

if /I not "%MODE%"=="smoke" if /I not "%MODE%"=="baseline" if /I not "%MODE%"=="full" if /I not "%MODE%"=="soak30" if /I not "%MODE%"=="soak2h" if /I not "%MODE%"=="soak8h" (
  echo ERROR: mode must be smoke, baseline, full, soak30, soak2h, or soak8h.
  exit /b 2
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe was not found.
  exit /b 3
)
set "MSBUILD="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
  set "MSBUILD=%%I"
  goto :msbuild_found
)
:msbuild_found
if not defined MSBUILD (
  echo ERROR: MSBuild.exe was not found.
  exit /b 4
)

set "VIEWER_DIR=%ROOT%\bin\Release\HairSystem"
for %%F in (PENDING_STEP0_REAL.ini STEP0_FORCE_RUN.ini STEP0_REAL_MEASUREMENT_PENDING.txt STEP0_REAL_MEASUREMENT_READY.txt) do (
  if exist "%VIEWER_DIR%\%%F" del /Q "%VIEWER_DIR%\%%F"
  if exist "%VIEWER_DIR%\profile_results\%%F" del /Q "%VIEWER_DIR%\profile_results\%%F"
)

echo [1/7] Rebuilding HairSystem Release x64 without Step0...
"%MSBUILD%" "%ROOT%\HairSystem.sln" /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:HairGpuStep0ProfileEnabled=false /p:HairGpuFormalBuild=true
if errorlevel 1 exit /b %errorlevel%
set "VIEWER=%VIEWER_DIR%\HairSystem.exe"
if not exist "%VIEWER%" (
  echo ERROR: Release viewer was not generated: "%VIEWER%"
  exit /b 5
)

echo [2/7] Running native ABI and workload self-test...
"%VIEWER%" --native-self-test
if errorlevel 1 exit /b %errorlevel%

echo [3/7] Running HLSL load/compile self-test...
"%VIEWER%" --shader-self-test
if errorlevel 1 exit /b %errorlevel%

echo [4/7] Checking Python tools...
python -m py_compile "%~dp0freeze_novelty01_asset.py" "%~dp0hairgpu_final_run.py" "%~dp0hairgpu_runtime_suite.py" "%~dp0hairgpu_safe_runtime_suite.py" "%~dp0hairgpu_final_db.py" "%~dp0hairgpu_final_excel.py"
if errorlevel 1 exit /b %errorlevel%

echo [5/7] Running validator and regression tests...
python "%~dp0hairgpu_final_run.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python -m unittest discover -s "%ROOT%\tests\final_stage" -p "test_*.py"
if errorlevel 1 exit /b %errorlevel%

set "FILTER_ARGS="
if /I "%MODE%"=="smoke" set "FILTER_ARGS=--only-method B0 --only-scenario S01_Idle --only-seed 1103"
if /I "%MODE%"=="baseline" set "FILTER_ARGS=--only-method B0"
if /I "%MODE%"=="soak30" set "FILTER_ARGS=--only-method P --only-scenario S03_Walk --only-seed 1103"
if /I "%MODE%"=="soak2h" set "FILTER_ARGS=--only-method P --only-scenario S09_StrongWind --only-seed 2207"
if /I "%MODE%"=="soak8h" set "FILTER_ARGS=--only-method P --only-scenario S16_StateThrashing --only-seed 3301"

echo [6/7] Running strict dry-run preflight (%MODE%)...
python "%~dp0hairgpu_runtime_suite.py" --config "%CONFIG%" --dry-run --viewer-exe "%VIEWER%" %FILTER_ARGS% %OUTPUT_ARG%
if errorlevel 1 exit /b %errorlevel%

echo [7/7] Running Release x64 suite (%MODE%)...
python "%~dp0hairgpu_runtime_suite.py" --config "%CONFIG%" --resume --fail-policy continue --viewer-exe "%VIEWER%" %FILTER_ARGS% %OUTPUT_ARG%
exit /b %errorlevel%
