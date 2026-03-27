@echo off
setlocal

where devenv >nul 2>&1
if %errorlevel% neq 0 (
  echo Visual Studio の devenv が見つかりませんでした。
  echo Visual Studio 2022 の Developer Command Prompt か、IDE から HairSystem.sln を開いてください。
  pause
  exit /b 1
)

echo Building HairSystem.sln (Debug x64)...
devenv HairSystem.sln /Build "Debug|x64"

if %errorlevel% neq 0 (
  echo Build failed.
  pause
  exit /b 1
)

if not exist bin\Debug\solver_lab.exe (
  echo solver_lab.exe が見つかりません。
  pause
  exit /b 1
)

echo.
echo Launching bin\Debug\solver_lab.exe ...
echo Window を閉じたあとにこのコンソールへ戻ります。
pushd bin\Debug
solver_lab.exe
set APP_EXIT=%errorlevel%
popd

echo.
echo solver_lab.exe finished with exit code %APP_EXIT%.
pause
exit /b %APP_EXIT%
