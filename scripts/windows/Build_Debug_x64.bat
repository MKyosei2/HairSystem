@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
set BUILD_DIR=%ROOT%\build\vs2022-debug
set SOLUTION=%ROOT%\HairSystem.sln
if not exist "%SOLUTION%" (
    set SOLUTION=%BUILD_DIR%\HairSystem.sln
)
where msbuild >nul 2>nul
if errorlevel 1 (
    echo MSBuild was not found in PATH. Open a Visual Studio 2022 Developer Command Prompt and run this script again.
    exit /b 1
)
if not exist "%SOLUTION%" (
    where cmake >nul 2>nul
    if errorlevel 1 (
        echo CMake was not found in PATH and no HairSystem.sln is available.
        exit /b 1
    )
    cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 exit /b %errorlevel%
    set SOLUTION=%BUILD_DIR%\HairSystem.sln
)
msbuild "%SOLUTION%" /t:Build /p:Configuration=Debug;Platform=x64
