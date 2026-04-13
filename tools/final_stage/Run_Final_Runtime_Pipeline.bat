@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
if "%~7"=="" (
  echo Usage: Run_Final_Runtime_Pipeline.bat ^<viewer_exe^> ^<db_path^> ^<excel_path^> ^<experiment^> ^<method^> ^<scenario^> ^<run_name^>
  exit /b 1
)
python "%ROOT%\tools\final_stage\hairgpu_final_run.py" --viewer-exe "%~1" --db "%~2" --excel "%~3" --experiment "%~4" --method "%~5" --scenario "%~6" --run-name "%~7" --telemetry-dir "%ROOT%\runtime_telemetry"
