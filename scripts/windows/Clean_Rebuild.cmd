@echo off
setlocal
for %%I in ("%~dp0\..\..") do set ROOT=%%~fI
cd /d "%ROOT%"
echo Cleaning Visual Studio / CMake intermediate outputs...
for %%D in (.vs bin bin-int x64 Debug Release out build cmake-build-debug cmake-build-release) do (
    if exist "%%D" rmdir /s /q "%%D"
)
for %%F in (CMakeCache.txt) do (
    if exist "%%F" del /f /q "%%F"
)
if exist CMakeFiles rmdir /s /q CMakeFiles
echo Done.
endlocal
