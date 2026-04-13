@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
powershell -ExecutionPolicy Bypass -File "%ROOT%\tools\signing\Sign-HairSystemBuild.ps1" -BuildRoot "%ROOT%\build" %*
