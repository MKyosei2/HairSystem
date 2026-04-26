@echo off
setlocal
REM Native runner fallback. This file does not invoke PowerShell.
REM In normal Visual Studio/CMake builds, CMake invokes hairgpu_auto_profile_runner.exe directly.
set EXE=%~1
set RUNNER=%~2
if "%EXE%"=="" (
  echo Usage: Run_AutoProfile_AfterBuild.bat path\to\hairgpu_dx11_viewer.exe [path\to\hairgpu_auto_profile_runner.exe]
  exit /b 0
)
if "%RUNNER%"=="" (
  set RUNNER=%~dp1hairgpu_auto_profile_runner.exe
)
if exist "%RUNNER%" (
  "%RUNNER%" --exe "%EXE%" --source-dir "%~dp0..\.." --configuration "Manual" --platform "Windows"
) else (
  echo [HairGPU AutoProfile] runner missing: %RUNNER%
  mkdir "%~dp0..\..\profile_results" 2>nul
  echo runner missing: %RUNNER%> "%~dp0..\..\profile_results\RUNNER_MISSING.txt"
)
exit /b 0
