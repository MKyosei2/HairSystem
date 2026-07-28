@echo off
setlocal EnableExtensions
cd /d "%~dp0\..\.."

where msbuild >nul 2>nul
if errorlevel 1 (
  echo ERROR: msbuild was not found. Run this from a Visual Studio 2022 Developer Command Prompt.
  exit /b 2
)
where python >nul 2>nul
if errorlevel 1 (
  echo ERROR: python was not found.
  exit /b 3
)
where fxc >nul 2>nul
if errorlevel 1 (
  echo ERROR: fxc.exe was not found. Install the Windows SDK or set FXC.
  exit /b 4
)

if exist overlay (
  echo ERROR: overlay directory remains in the runtime repository.
  exit /b 5
)

python tools\final_stage\hairgpu_novelty01_preflight.py --repo . --require-fxc --json artifacts\novelty01_preflight.json
if errorlevel 1 exit /b 10

python tools\final_stage\hairgpu_novelty01_resource_audit.py --repo . --output artifacts\novelty01_resource_audit.json --strict
if errorlevel 1 exit /b 11

python -m pytest tests\final_stage\test_novelty01_complete_contract.py -q
if errorlevel 1 exit /b 12

msbuild HairSystem.vcxproj /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:HairGpuFormalSelfTest=true
if errorlevel 1 exit /b 13

if not exist bin\Release\HairSystem\HairSystem.exe (
  echo ERROR: Release x64 viewer executable was not produced.
  exit /b 14
)

set HAIRGPU_SHADER_SELF_TEST=1
bin\Release\HairSystem\HairSystem.exe --self-test
if errorlevel 1 exit /b 15

set HAIRGPU_SHADER_SELF_TEST=
echo Novelty 01 source, FXC, resource, pytest, Release x64, and native self-test verification passed.
exit /b 0
