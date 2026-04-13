@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
set BUILD_DIR=%ROOT%\build\vs2022-debug
set SOLUTION=%ROOT%\HairSystem.sln
if exist "%SOLUTION%" (
    start "" "%SOLUTION%"
    exit /b 0
)
where cmake >nul 2>nul
if errorlevel 1 (
    echo HairSystem.sln was not found and CMake is unavailable to generate one.
    exit /b 1
)
if not exist "%BUILD_DIR%\HairSystem.sln" (
    cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 exit /b %errorlevel%
)
start "" "%BUILD_DIR%\HairSystem.sln"
