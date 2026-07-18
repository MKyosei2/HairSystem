@echo off
setlocal EnableExtensions EnableDelayedExpansion

if "%~1"=="" (
  echo Usage: Run_Final_Runtime_Suite.bat ^<suite_config.json^> [smoke^|baseline^|full] [output_dir]
  exit /b 1
)

set "ROOT=%~dp0..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "CONFIG=%~f1"
set "MODE=%~2"
if "%MODE%"=="" set "MODE=baseline"
set "OUTPUT_ARG="
if not "%~3"=="" set "OUTPUT_ARG=--output "%~f3""

if /I not "%MODE%"=="smoke" if /I not "%MODE%"=="baseline" if /I not "%MODE%"=="full" (
  echo ERROR: mode must be smoke, baseline, or full.
  exit /b 2
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe was not found.
  exit /b 3
)
set "MSBUILD="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
  set "MSBUILD=%%I"
  goto :msbuild_found
)
:msbuild_found
if not defined MSBUILD (
  echo ERROR: MSBuild.exe was not found.
  exit /b 4
)

echo [1/5] Rebuilding HairSystem Release x64...
"%MSBUILD%" "%ROOT%\HairSystem.sln" /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
if errorlevel 1 exit /b %errorlevel%
set "VIEWER=%ROOT%\bin\Release\HairSystem\HairSystem.exe"
if not exist "%VIEWER%" (
  echo ERROR: Release viewer was not generated: "%VIEWER%"
  exit /b 5
)

echo [2/5] Checking Python tools...
python -m py_compile "%~dp0hairgpu_final_run.py" "%~dp0hairgpu_runtime_suite.py" "%~dp0hairgpu_safe_runtime_suite.py" "%~dp0hairgpu_final_db.py" "%~dp0hairgpu_final_excel.py"
if errorlevel 1 exit /b %errorlevel%

echo [3/5] Running validator and unit tests...
python "%~dp0hairgpu_final_run.py" --self-test
if errorlevel 1 exit /b %errorlevel%
python -m unittest discover -s "%ROOT%\tests\final_stage" -p "test_*.py"
if errorlevel 1 exit /b %errorlevel%

set "FILTER_ARGS="
if /I "%MODE%"=="smoke" set "FILTER_ARGS=--only-method B0 --only-scenario S01_Idle --only-seed 1103"
if /I "%MODE%"=="baseline" set "FILTER_ARGS=--only-method B0"

echo [4/5] Running dry-run preflight (%MODE%)...
python "%~dp0hairgpu_runtime_suite.py" --config "%CONFIG%" --dry-run --viewer-exe "%VIEWER%" %FILTER_ARGS% %OUTPUT_ARG%
if errorlevel 1 exit /b %errorlevel%

echo [5/5] Running Release x64 suite (%MODE%)...
python "%~dp0hairgpu_runtime_suite.py" --config "%CONFIG%" --resume --fail-policy continue --viewer-exe "%VIEWER%" %FILTER_ARGS% %OUTPUT_ARG%
exit /b %errorlevel%
