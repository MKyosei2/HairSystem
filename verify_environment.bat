@echo off
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug
set ROOT=%~dp0
set BIN=%ROOT%bin\%CONFIG%

echo [HairSystem] Verifying %CONFIG% x64 build...

if not exist "%BIN%\solver_lab.exe" (
  echo [NG] "%BIN%\solver_lab.exe" が見つかりません。
  echo      solver_lab プロジェクトを %CONFIG%^|x64 で Build してください。
  exit /b 1
)

echo [OK] solver_lab.exe

if not exist "%BIN%\hair_core.lib" (
  echo [NG] "%BIN%\hair_core.lib" が見つかりません。
  echo      hair_core プロジェクトを %CONFIG%^|x64 で Build してください。
  exit /b 1
)

echo [OK] hair_core.lib

if /I "%PROCESSOR_ARCHITECTURE%"=="x86" (
  echo [WARN] 32-bit 環境から確認しています。Visual Studio 側は x64 構成で実行してください。
)

where powershell >nul 2>&1
if %errorlevel% neq 0 (
  echo [WARN] PowerShell が見つからないため、追加の実行確認を一部スキップします。
)

for %%F in (d3d11.dll dxgi.dll d3dcompiler_47.dll) do (
  if exist "%SystemRoot%\System32\%%F" (
    echo [OK] %%F
  ) else (
    echo [WARN] %%F が System32 に見つかりません。
  )
)

echo [OK] 基本確認は完了しました。
echo      注: OS の実行禁止ポリシーや管理者制御など、PC 側のあらゆる制限をこのスクリプトで回避することはできません。
echo      ただし、起動前に主要な欠落や構成ミスは確認できます。
exit /b 0
