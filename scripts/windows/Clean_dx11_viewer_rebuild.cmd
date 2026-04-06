@echo off
setlocal
set ROOT=%~dp0..\
if exist "%ROOT%\.vs" rmdir /s /q "%ROOT%\.vs"
if exist "%ROOT%\bin" rmdir /s /q "%ROOT%\bin"
if exist "%ROOT%\bin-int" rmdir /s /q "%ROOT%\bin-int"
echo Cleaned .vs, bin, and bin-int. Reopen Visual Studio and Rebuild hairgpu_dx11_viewer.
pause
