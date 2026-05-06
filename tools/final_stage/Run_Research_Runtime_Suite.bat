@echo off
setlocal
if "%~1"=="" (
  echo Usage: Run_Research_Runtime_Suite.bat ^<runtime_suite.json^> [viewer_exe] [output_dir]
  exit /b 1
)
set VIEWER_ARG=
if not "%~2"=="" set VIEWER_ARG=--viewer-exe "%~2"
set OUTPUT_ARG=
if not "%~3"=="" set OUTPUT_ARG=--output "%~3"
python "%~dp0hairgpu_runtime_suite.py" --config "%~1" --resume --fail-policy continue %VIEWER_ARG% %OUTPUT_ARG%
