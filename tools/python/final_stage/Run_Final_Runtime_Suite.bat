@echo off
setlocal
for %%I in ("%~dp0\..\..\..") do set ROOT=%%~fI
if "%~1"=="" (
  echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^>
  exit /b 1
)
python "%ROOT%\tools\python\final_stage\hairgpu_runtime_suite.py" --config "%~1"
