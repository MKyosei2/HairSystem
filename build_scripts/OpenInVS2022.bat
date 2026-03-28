@echo off
setlocal
set ROOT=%~dp0..
set SLN=%ROOT%\HairSystem_GPUBase.sln
if not exist "%SLN%" (
  echo Solution file not found: %SLN%
  exit /b 1
)
start "" "%SLN%"
