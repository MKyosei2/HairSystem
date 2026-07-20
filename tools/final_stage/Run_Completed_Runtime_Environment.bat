@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" (
  echo Usage: Run_Completed_Runtime_Environment.bat ^<suite_config.json^> [prepare-asset^|proposal-smoke^|comparison-smoke^|critical^|baseline^|full^|determinism^|soak30^|soak2h^|soak8h^|all] [output_dir]
  exit /b 1
)

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "CONFIG=%~f1"
set "MODE=%~2"
if "%MODE%"=="" set "MODE=proposal-smoke"
set "OUTPUT=%~3"
if "%OUTPUT%"=="" (
  for /f "usebackq delims=" %%O in (`python -c "import json,pathlib; c=pathlib.Path(r'%CONFIG%').resolve(); j=json.loads(c.read_text(encoding='utf-8-sig')); p=pathlib.Path(j['outputs_dir']); print((p if p.is_absolute() else c.parent/p).resolve())"`) do set "OUTPUT=%%O"
) else (
  for %%I in ("%OUTPUT%") do set "OUTPUT=%%~fI"
)

if /I "%MODE%"=="all" goto :run_all

call "%~dp0Run_Final_Runtime_Suite.bat" "%CONFIG%" "%MODE%" "%OUTPUT%"
set "SUITE_EXIT=%ERRORLEVEL%"
if not "%SUITE_EXIT%"=="0" exit /b %SUITE_EXIT%

if /I "%MODE%"=="prepare-asset" exit /b 0

set "GATE_MODE=formal"
if /I "%MODE%"=="determinism" set "GATE_MODE=determinism"
if /I "%MODE%"=="soak30" set "GATE_MODE=long-run"
if /I "%MODE%"=="soak2h" set "GATE_MODE=long-run"
if /I "%MODE%"=="soak8h" set "GATE_MODE=long-run"

python "%~dp0hairgpu_completion_gates.py" --config "%CONFIG%" --output "%OUTPUT%" --mode "%GATE_MODE%"
exit /b %ERRORLEVEL%

:run_all
call "%~dp0Run_Final_Runtime_Suite.bat" "%CONFIG%" full "%OUTPUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~dp0Run_Final_Runtime_Suite.bat" "%CONFIG%" determinism "%OUTPUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~dp0Run_Final_Runtime_Suite.bat" "%CONFIG%" soak30 "%OUTPUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~dp0Run_Final_Runtime_Suite.bat" "%CONFIG%" soak2h "%OUTPUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~dp0Run_Final_Runtime_Suite.bat" "%CONFIG%" soak8h "%OUTPUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
python "%~dp0hairgpu_completion_gates.py" --config "%CONFIG%" --output "%OUTPUT%" --mode all
exit /b %ERRORLEVEL%
