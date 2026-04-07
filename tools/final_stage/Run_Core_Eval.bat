@echo off
setlocal

if "%~1"=="" (
  echo Usage: Run_Core_Eval.bat ^<hairgpu_cli.exe^> ^<inputAsset^> ^<outputDir^> [colliders.csv]
  exit /b 1
)
if "%~2"=="" (
  echo Usage: Run_Core_Eval.bat ^<hairgpu_cli.exe^> ^<inputAsset^> ^<outputDir^> [colliders.csv]
  exit /b 1
)
if "%~3"=="" (
  echo Usage: Run_Core_Eval.bat ^<hairgpu_cli.exe^> ^<inputAsset^> ^<outputDir^> [colliders.csv]
  exit /b 1
)

set CLI=%~1
set INPUT=%~2
set OUTPUT=%~3
set COLLIDERS=%~4

set SCRIPT_DIR=%~dp0
set PYTHONPATH=%SCRIPT_DIR%

if "%COLLIDERS%"=="" (
  py -3 "%SCRIPT_DIR%hairgpu_core_eval.py" --cli "%CLI%" --input "%INPUT%" --output-dir "%OUTPUT%"
) else (
  py -3 "%SCRIPT_DIR%hairgpu_core_eval.py" --cli "%CLI%" --input "%INPUT%" --output-dir "%OUTPUT%" --colliders "%COLLIDERS%"
)

endlocal
