@echo off
setlocal
set ROOT=%~dp0..
if not exist "%ROOT%\HairSystem.sln" (
    echo HairSystem.sln was not found next to build_scripts.
    exit /b 1
)
where msbuild >nul 2>nul
if errorlevel 1 (
    echo MSBuild was not found in PATH. Open a Visual Studio 2022 Developer Command Prompt and run this script again.
    exit /b 1
)
msbuild "%ROOT%\HairSystem.sln" /t:Build /p:Configuration=Debug;Platform=x64
