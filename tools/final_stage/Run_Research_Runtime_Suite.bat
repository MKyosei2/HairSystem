@echo off
setlocal EnableExtensions

if "%~1"=="" (
  echo Usage: Run_Research_Runtime_Suite.bat ^<runtime_suite.json^> [viewer_exe] [output_dir]
  exit /b 1
)

set "CONFIG=%~f1"
set "VIEWER_ARG="
set "OUTPUT_ARG="
if not "%~2"=="" set "VIEWER_ARG=--viewer-exe "%~f2""
if not "%~3"=="" set "OUTPUT_ARG=--output "%~f3""

python -m py_compile ^
  "%~dp0hairgpu_final_run.py" ^
  "%~dp0hairgpu_runtime_suite.py" ^
  "%~dp0hairgpu_final_db.py" ^
  "%~dp0hairgpu_final_excel.py"
if errorlevel 1 exit /b %errorlevel%

python "%~dp0hairgpu_runtime_suite.py" --config "%CONFIG%" --dry-run %VIEWER_ARG% %OUTPUT_ARG%
if errorlevel 1 exit /b %errorlevel%

python "%~dp0hairgpu_runtime_suite.py" --config "%CONFIG%" --resume --fail-policy continue %VIEWER_ARG% %OUTPUT_ARG%
exit /b %errorlevel%
