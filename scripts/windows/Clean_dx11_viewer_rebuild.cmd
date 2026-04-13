@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
if exist "%ROOT%\.vs" rmdir /s /q "%ROOT%\.vs"
if exist "%ROOT%\bin" rmdir /s /q "%ROOT%\bin"
if exist "%ROOT%\bin-int" rmdir /s /q "%ROOT%\bin-int"
if exist "%ROOT%\build\vs2022-debug" rmdir /s /q "%ROOT%\build\vs2022-debug"
echo Cleaned .vs, bin, bin-int, and build\vs2022-debug. Reopen Visual Studio and Rebuild hairgpu_dx11_viewer.
pause
