@echo off
setlocal
set ROOT=%~dp0..\..
set OUT=%ROOT%\results\exp_650
set VIEWER=%ROOT%\bin\Debug\HairSystem\HairSystem.exe
if not "%~1"=="" set VIEWER=%~1

python "%~dp0generate_experiment_650.py" ^
  --out-jsonl "%OUT%\experiments_650.jsonl" ^
  --out-suite "%OUT%\runtime_suite_650.json" ^
  --viewer-exe "../../bin/Debug/HairSystem/HairSystem.exe"
if errorlevel 1 exit /b %errorlevel%

python "%~dp0hairgpu_runtime_suite.py" ^
  --config "%OUT%\runtime_suite_650.json" ^
  --viewer-exe "%VIEWER%" ^
  --resume ^
  --fail-policy continue
exit /b %errorlevel%
