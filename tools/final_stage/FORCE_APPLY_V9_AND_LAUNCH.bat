@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0\..\.."
if not exist "HairSystem.sln" (
  echo [ERROR] HairSystem.sln was not found. Extract v9 at the repository root.
  exit /b 2
)

echo [V9] Stopping every old HairSystem.exe ...
taskkill /F /IM HairSystem.exe >nul 2>&1

echo [V9] Verifying v9 source markers ...
findstr /C:"HAIRGPU_BUILD_V9_20260724_FORCE_APPLIED" "apps\dx11_viewer\src\AppMain.cpp" >nul || (
  echo [ERROR] AppMain.cpp is not v9. ZIP was not extracted at the repository root.
  exit /b 3
)
findstr /C:"HAIRGPU V9 FORCE-APPLIED" "apps\dx11_viewer\src\GpuHairViewer_parts\06_render_mesh_and_visibility_recovery.inl" >nul || (
  echo [ERROR] Viewer title source is not v9.
  exit /b 4
)
findstr /C:"sample_hybrid_cache_v3_runtime_verified" "apps\dx11_viewer\src\GpuHairViewer_parts\01_window_device_and_ui_setup.inl" >nul || (
  echo [ERROR] Asset cache source is not v9.
  exit /b 5
)

echo [V9] Removing stale executable, PDB, shaders, object files, settings and every asset cache ...
if exist "bin\Debug\HairSystem" rmdir /s /q "bin\Debug\HairSystem"
if exist "bin\Release\HairSystem" rmdir /s /q "bin\Release\HairSystem"
if exist "bin-int\Debug\HairSystem" rmdir /s /q "bin-int\Debug\HairSystem"
if exist "bin-int\Release\HairSystem" rmdir /s /q "bin-int\Release\HairSystem"
if exist ".vs" rmdir /s /q ".vs"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] vswhere.exe was not found.
  exit /b 6
)
set "MSBUILD="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD set "MSBUILD=%%I"
if not defined MSBUILD (
  echo [ERROR] MSBuild.exe was not found.
  exit /b 7
)

echo [V9] Rebuilding RELEASE x64. Debug is intentionally not used for performance judgment.
"%MSBUILD%" "HairSystem.sln" /t:Rebuild /m /p:Configuration=Release /p:Platform=x64 /p:HairGpuStep0ProfileEnabled=false /p:HairGpuFormalBuild=false
if errorlevel 1 (
  echo [ERROR] Release x64 rebuild failed.
  exit /b 8
)

set "EXE=%CD%\bin\Release\HairSystem\HairSystem.exe"
set "SHADER=%CD%\bin\Release\HairSystem\shaders\HairStrandInstanced.hlsl"
if not exist "%EXE%" (
  echo [ERROR] Expected EXE was not created: %EXE%
  exit /b 9
)
if not exist "%SHADER%" (
  echo [ERROR] Deployed strand shader is missing: %SHADER%
  exit /b 10
)
findstr /C:"HAIRGPU_STRAND_RIBBON_SHADER_ABI_1" "%SHADER%" >nul || (
  echo [ERROR] Deployed shader has the wrong ABI.
  exit /b 11
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$b=[IO.File]::ReadAllBytes('%EXE%'); $a=[Text.Encoding]::ASCII.GetString($b); if($a -notmatch 'HAIRGPU_BUILD_V9_20260724_FORCE_APPLIED'){exit 12}"
if errorlevel 1 (
  echo [ERROR] The built EXE does not contain the v9 marker. A different project/output is being built.
  exit /b 12
)

for %%F in ("%EXE%") do set "EXE_TIME=%%~tF"
echo [V9] VERIFIED EXE: %EXE%
echo [V9] EXE timestamp: !EXE_TIME!

del /f /q "%CD%\bin\Release\HairSystem\HAIRGPU_RUNTIME_IDENTITY_V9.txt" >nul 2>&1
start "HairSystem V9" /D "%CD%\bin\Release\HairSystem" "%EXE%"

echo [V9] Waiting for runtime identity proof ...
for /L %%I in (1,1,30) do (
  if exist "%CD%\bin\Release\HairSystem\HAIRGPU_RUNTIME_IDENTITY_V9.txt" goto :proof
  timeout /t 1 /nobreak >nul
)
echo [ERROR] Viewer did not write HAIRGPU_RUNTIME_IDENTITY_V9.txt.
echo [ERROR] The exact v9 EXE did not initialize successfully.
exit /b 13

:proof
echo.
echo ================= V9 RUNTIME PROOF =================
type "%CD%\bin\Release\HairSystem\HAIRGPU_RUNTIME_IDENTITY_V9.txt"
echo ====================================================
echo [PASS] The exact verified Release v9 executable is running.
echo [PASS] The window title must begin with: [HAIRGPU V9 FORCE-APPLIED]
exit /b 0
