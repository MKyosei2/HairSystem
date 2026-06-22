@echo off
setlocal
powershell -ExecutionPolicy Bypass -File "%~dp0restore_complete_fixed_zip.ps1" %*
endlocal
