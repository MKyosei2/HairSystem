@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
set CERT_PATH=%ROOT%\tools\signing\output\hairgpu-internal-test-signing.cer
powershell -ExecutionPolicy Bypass -File "%ROOT%\tools\signing\Create-InternalTestCodeSigningCert.ps1"
if errorlevel 1 exit /b %errorlevel%
powershell -ExecutionPolicy Bypass -File "%ROOT%\tools\signing\Install-InternalTestCodeSigningCert.ps1" -CertPath "%CERT_PATH%" -Scope CurrentUser
if exist "%ROOT%\bin" powershell -ExecutionPolicy Bypass -File "%ROOT%\tools\signing\Sign-BuildOutputIfPossible.ps1" -BuildOutputDir "%ROOT%\bin"
echo.
echo Local test signing setup complete.
exit /b 0
