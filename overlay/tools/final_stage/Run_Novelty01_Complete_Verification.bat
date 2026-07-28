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

msbuild HairSystem.vcxproj /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:HairGpuFormalSelfTest=true
if errorlevel 1 exit /b 10

python tools\final_stage\hairgpu_novelty01_preflight.py --repo . --require-fxc
if errorlevel 1 exit /b 11

python -m pytest tests\final_stage\test_novelty01_complete_contract.py -q
if errorlevel 1 exit /b 12

echo Novelty 01 compile, shader, native self-test, and source-contract verification passed.
exit /b 0
