@echo off
setlocal
set CERT_PATH=%~dp0..\tools\signing\output\hairgpu-internal-test-signing.cer
powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\signing\Create-InternalTestCodeSigningCert.ps1"
if errorlevel 1 exit /b %errorlevel%
powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\signing\Install-InternalTestCodeSigningCert.ps1" -CertPath "%CERT_PATH%" -Scope CurrentUser
if exist "%~dp0..\bin" powershell -ExecutionPolicy Bypass -File "%~dp0..\tools\signing\Sign-BuildOutputIfPossible.ps1" -BuildOutputDir "%~dp0..\bin"
echo.
echo Local test signing setup complete.
exit /b 0
