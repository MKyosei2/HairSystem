@echo off
setlocal
set OUTPUT_DIR=%~dp0..\bin\Debug\hairgpu_dx11_viewer
if not exist "%OUTPUT_DIR%\hairgpu_dx11_viewer.exe" set OUTPUT_DIR=%~dp0..\build\apps\dx11_viewer\Debug
powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\signing\Sign-BuildOutputIfPossible.ps1" -BuildOutputDir "%OUTPUT_DIR%"
powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\signing\Create-HairSystemTestBundle.ps1" -BuildOutputDir "%OUTPUT_DIR%" %*
exit /b %errorlevel%
