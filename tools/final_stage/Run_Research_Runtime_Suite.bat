@echo off
setlocal
if "%~4"=="" (
  echo Usage: Run_Research_Runtime_Suite.bat ^<preset.json^> ^<db_path^> ^<excel_path^> ^<output_dir^> [viewer_exe]
  exit /b 1
)
set VIEWER_ARG=
if not "%~5"=="" set VIEWER_ARG=--viewer-exe "%~5"
python "%~dp0hairgpu_runtime_suite.py" --preset "%~1" --db "%~2" --excel "%~3" --output-dir "%~4" %VIEWER_ARG%
