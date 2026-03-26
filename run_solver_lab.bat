@echo off
setlocal
set CONFIG=%~1
if "%CONFIG%"=="" set CONFIG=Debug
call "%~dp0verify_environment.bat" %CONFIG%
if %errorlevel% neq 0 exit /b %errorlevel%
start "" "%~dp0bin\%CONFIG%\solver_lab.exe"
