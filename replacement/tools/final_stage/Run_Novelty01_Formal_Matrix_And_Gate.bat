@echo off
setlocal EnableExtensions
cd /d "%~dp0\..\.."

call tools\final_stage\Run_Novelty01_Complete_Verification.bat
if errorlevel 1 exit /b 10

if not exist bin\Release\HairSystem\HairSystem.exe (
  echo ERROR: Release x64 viewer executable is missing.
  exit /b 11
)
if not exist docs\sample_assets\final_runtime_suite_template.json (
  echo ERROR: frozen runtime suite template is missing.
  exit /b 12
)

python tools\final_stage\hairgpu_novelty01_formal_suite.py ^
  --repo . ^
  --viewer bin\Release\HairSystem\HairSystem.exe ^
  --template docs\sample_assets\final_runtime_suite_template.json ^
  --evidence-root artifacts
if errorlevel 1 exit /b 20

echo Novelty 01 formal quality, performance, determinism, soak, GTX1660, report, and merge gate passed.
exit /b 0
