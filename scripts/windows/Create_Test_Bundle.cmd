@echo off
powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\signing\Create-HairSystemTestBundle.ps1" -BuildOutputDir "%~dp0..\build\apps\dx11_viewer\Debug" %*
