@echo off
setlocal
set ROOT=%~dp0..
set SLN=%ROOT%\HairSystem_GPUBase.sln
if not exist "%SLN%" (
  echo Solution file not found: %SLN%
  exit /b 1
)
where msbuild >nul 2>nul
if errorlevel 1 (
  echo MSBuild not found in PATH. Please open a "x64 Native Tools Command Prompt for VS 2022" and run this script again.
  exit /b 1
)
msbuild "%SLN%" /t:Build /p:Configuration=Debug /p:Platform=x64 /m
