@echo off
setlocal
set ROOT=%~dp0..
if exist "%ROOT%\HairSystem.sln" (
    start "" "%ROOT%\HairSystem.sln"
) else (
    echo HairSystem.sln was not found next to build_scripts.
    exit /b 1
)
