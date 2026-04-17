@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
set OUTPUT_DIR=%ROOT%\build\apps\dx11_viewer\Debug
if not exist "%OUTPUT_DIR%\hairgpu_dx11_viewer.exe" set OUTPUT_DIR=%ROOT%\build\vs2022-debug\apps\dx11_viewer\Debug
powershell -ExecutionPolicy Bypass -File "%ROOT%\tools\signing\Create-HairSystemTestBundle.ps1" -BuildOutputDir "%OUTPUT_DIR%" %*
