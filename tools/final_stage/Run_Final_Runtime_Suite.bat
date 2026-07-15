@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
if "%~1"=="" (
  echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> [additional options]
  exit /b 1
)
set SUITE=%~1
shift
python "%ROOT%\tools\final_stage\hairgpu_safe_runtime_suite.py" --config "%SUITE%" %*
exit /b %ERRORLEVEL%
