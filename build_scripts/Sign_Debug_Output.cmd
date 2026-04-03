@echo off
powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\signing\Sign-HairSystemBuild.ps1" -BuildRoot "%~dp0..\build" %*
