@echo off
powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\signing\Create-InternalTestCodeSigningCert.ps1" %*
