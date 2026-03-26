@echo off
setlocal

where devenv >nul 2>&1
if %errorlevel% neq 0 (
  echo Visual Studio の devenv が見つかりませんでした。
  echo Visual Studio 2022 の Developer Command Prompt か、IDE から HairSystem.sln を開いてください。
  exit /b 1
)

echo Building HairSystem.sln (Debug x64)...
devenv HairSystem.sln /Build "Debug|x64"

if %errorlevel% neq 0 (
  echo Build failed.
  exit /b 1
)

call verify_environment.bat Debug
if %errorlevel% neq 0 exit /b %errorlevel%

echo Running solver_lab.exe ...
start "" bin\Debug\solver_lab.exe
