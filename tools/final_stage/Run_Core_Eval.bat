@echo off
setlocal
if "%~5"=="" (
  echo Usage: Run_Core_Eval.bat ^<hairgpu_cli.exe^> ^<input.hairbin^> ^<teacher_dir^> ^<bundle_dir^> ^<output_dir^>
  exit /b 1
)
python "%~dp0hairgpu_core_eval.py" --cli "%~1" --input "%~2" --teacher-dir "%~3" --bundle-dir "%~4" --output-dir "%~5"
