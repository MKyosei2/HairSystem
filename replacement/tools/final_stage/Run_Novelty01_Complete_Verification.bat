@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0\..\.."

if not exist artifacts mkdir artifacts

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

rem overlay/ and replacement/ may remain until the later compression pass.
rem They are distribution copies only; HairSystem.vcxproj must never reference them.
findstr /I /C:"overlay\" HairSystem.vcxproj >nul 2>nul
if not errorlevel 1 (
  echo ERROR: HairSystem.vcxproj references overlay distribution sources.
  exit /b 5
)
findstr /I /C:"replacement\" HairSystem.vcxproj >nul 2>nul
if not errorlevel 1 (
  echo ERROR: HairSystem.vcxproj references replacement distribution sources.
  exit /b 6
)

python tools\final_stage\hairgpu_novelty01_preflight.py ^
  --repo . --require-fxc ^
  --json artifacts\novelty01_preflight.json ^
  --fxc-json artifacts\fxc_entrypoints.json
if errorlevel 1 exit /b 10

python tools\final_stage\hairgpu_novelty01_resource_audit.py ^
  --repo . --output artifacts\novelty01_resource_audit.json --strict
if errorlevel 1 exit /b 11

python -m pytest tests\final_stage\test_novelty01_complete_contract.py -q ^
  > artifacts\novelty01_pytest.log 2>&1
set PYTEST_RESULT=!ERRORLEVEL!
type artifacts\novelty01_pytest.log
if not "!PYTEST_RESULT!"=="0" exit /b 12

msbuild HairSystem.vcxproj /m /t:Rebuild ^
  /p:Configuration=Release /p:Platform=x64 /p:HairGpuFormalSelfTest=true ^
  /fl /flp:logfile=artifacts\release_x64_build.log;verbosity=normal
if errorlevel 1 exit /b 13

if not exist bin\Release\HairSystem\HairSystem.exe (
  echo ERROR: Release x64 viewer executable was not produced.
  exit /b 14
)

set HAIRGPU_SHADER_SELF_TEST=1
bin\Release\HairSystem\HairSystem.exe --self-test ^
  > artifacts\native_self_test.log 2>&1
set SELF_TEST_RESULT=!ERRORLEVEL!
type artifacts\native_self_test.log
set HAIRGPU_SHADER_SELF_TEST=
if not "!SELF_TEST_RESULT!"=="0" exit /b 15

echo Novelty 01 source, FXC, resource, pytest, Release x64, and native self-test verification passed.
echo Build evidence: artifacts\release_x64_build.log
echo FXC evidence: artifacts\fxc_entrypoints.json
echo Native evidence: artifacts\native_self_test.log
exit /b 0
