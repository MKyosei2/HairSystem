@echo off
setlocal

if "%~1"=="" (
  echo Usage: Run_Core_Eval.bat ^<hairgpu_cli.exe^> ^<inputAsset^> ^<outputDir^> [colliders.csv] [viewer_perf.csv]
  exit /b 1
)
if "%~2"=="" (
  echo Usage: Run_Core_Eval.bat ^<hairgpu_cli.exe^> ^<inputAsset^> ^<outputDir^> [colliders.csv] [viewer_perf.csv]
  exit /b 1
)
if "%~3"=="" (
  echo Usage: Run_Core_Eval.bat ^<hairgpu_cli.exe^> ^<inputAsset^> ^<outputDir^> [colliders.csv] [viewer_perf.csv]
  exit /b 1
)

set CLI=%~1
set INPUT=%~2
set OUTPUT=%~3
set COLLIDERS=%~4
set VIEWER_PERF=%~5

set SCRIPT_DIR=%~dp0
set PYTHONPATH=%SCRIPT_DIR%

set EXTRA_ARGS=
if not "%COLLIDERS%"=="" set EXTRA_ARGS=%EXTRA_ARGS% --colliders "%COLLIDERS%"
if not "%VIEWER_PERF%"=="" set EXTRA_ARGS=%EXTRA_ARGS% --viewer-perf-csv "%VIEWER_PERF%"
py -3 "%SCRIPT_DIR%hairgpu_core_eval.py" --cli "%CLI%" --input "%INPUT%" --output-dir "%OUTPUT%" %EXTRA_ARGS%

endlocal
