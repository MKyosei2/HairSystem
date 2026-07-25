@echo off
setlocal EnableExtensions

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
if /I not "%CONFIG%"=="Debug" if /I not "%CONFIG%"=="Release" (
  echo [ERROR] Configuration must be Debug or Release.
  exit /b 2
)

cd /d "%~dp0..\.."
if not exist "HairSystem.sln" (
  echo [ERROR] HairSystem.sln was not found. Extract the ZIP at the repository root.
  exit /b 3
)

set "OUTDIR=bin\%CONFIG%\HairSystem"
echo [V8] Removing deployed shader copies from %OUTDIR% ...
if exist "%OUTDIR%\shaders" rmdir /s /q "%OUTDIR%\shaders"
if exist "%OUTDIR%\*.hlsl" del /f /q "%OUTDIR%\*.hlsl" >nul 2>&1

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] vswhere.exe was not found. Install Visual Studio 2022 with Desktop development with C++.
  exit /b 4
)

set "MSBUILD="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
  if not defined MSBUILD set "MSBUILD=%%I"
)
if not defined MSBUILD (
  echo [ERROR] MSBuild.exe was not found.
  exit /b 5
)

set "PLATFORM=x64"
echo [V8] Rebuilding %CONFIG% %PLATFORM% with:
echo      %MSBUILD%
"%MSBUILD%" "HairSystem.sln" /t:Rebuild /m /p:Configuration=%CONFIG% /p:Platform=%PLATFORM%
if errorlevel 1 (
  echo [ERROR] Rebuild failed.
  exit /b 6
)

echo [V8] Rebuild completed successfully.
echo [V8] Start %OUTDIR%\HairSystem.exe
exit /b 0
